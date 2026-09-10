// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

#include <algorithm>
#include <map>

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

    struct MultidimensionalSelection {
        const Expression* base { };
        std::vector<const Expression*> indices;
    };

    [[nodiscard]] MultidimensionalSelection selection_chain(
        const Expression& expression)
    {
        MultidimensionalSelection result;
        result.base = &expression;
        while (result.base->kind == ExpressionKind::Index
            && result.base->operands.size() == 2) {
            result.indices.push_back(&result.base->operands[1]);
            result.base = &result.base->operands[0];
        }
        std::ranges::reverse(result.indices);
        return result;
    }

    [[nodiscard]] std::uint64_t flattened_element_count(
        const ContainerType& type)
    {
        std::uint64_t result = 1;
        for (const auto& dimension : type.dimensions) {
            result *= static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    std::max(dimension.first, dimension.second))
                - std::min(dimension.first, dimension.second) + 1);
        }
        return result;
    }

} // namespace

std::optional<RegisterId>
Lowerer::lower_multidimensional_index(
    const Expression& expression,
    const frontend::Type& type,
    const bool require_complete)
{
    if (!type.systemverilog_container) {
        return std::nullopt;
    }
    const auto& ranges = type.systemverilog_container
                             ->static_range_expressions;
    const auto selection = selection_chain(expression);
    if (selection.base->kind != ExpressionKind::Identifier
        || ranges.size() <= 1 || selection.indices.empty()
        || selection.indices.size() > ranges.size()
        || (require_complete
            && selection.indices.size() != ranges.size())) {
        report(
            "FSIM-ELAB-SVMDARRAY-001",
            require_complete
                ? "a multidimensional static-array element access must supply "
                  "exactly one index per declared dimension"
                : "a multidimensional static-array subarray selection must "
                  "supply a nonempty leading index prefix",
            expression.span);
        return std::nullopt;
    }
    const auto runtime_type = container_type(type, expression.span);
    if (!runtime_type
        || runtime_type->dimensions.size() != ranges.size()) {
        return std::nullopt;
    }
    std::optional<RegisterId> linear;
    for (std::size_t dimension = 0;
        dimension < selection.indices.size(); ++dimension) {
        const auto& bounds = runtime_type->dimensions[dimension];
        const auto low = std::min(bounds.first, bounds.second);
        const auto high = std::max(bounds.first, bounds.second);
        std::optional<RegisterId> ordinal;
        if (const auto constant = static_integer_value(*selection.indices[dimension])) {
            if (*constant < low || *constant > high) {
                report(
                    "FSIM-ELAB-SVMDARRAY-003",
                    "multidimensional static-array index is outside its declared "
                    "range",
                    selection.indices[dimension]->span);
                return std::nullopt;
            }
            const auto value = static_cast<std::uint64_t>(
                bounds.first >= bounds.second
                    ? static_cast<std::int64_t>(bounds.first) - *constant
                    : *constant - bounds.first);
            ordinal = allocate_register(32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(
                LoadConstant { *ordinal, unsigned_value(value, 32) });
        } else {
            const auto index = lower_expression(*selection.indices[dimension], 32);
            if (!index || register_width(*index) != 32) {
                report(
                    "FSIM-ELAB-SVMDARRAY-002",
                    "a runtime multidimensional index must lower to a signed "
                    "32-bit integral value",
                    selection.indices[dimension]->span);
                return std::nullopt;
            }
            process_.operations.emplace_back(IntegerCheck {
                *index, low, high });
            const auto declared_left = allocate_register(32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                declared_left, integer_value(bounds.first) });
            ordinal = allocate_register(32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::subtract,
                *ordinal,
                bounds.first >= bounds.second ? declared_left : *index,
                bounds.first >= bounds.second ? *index : declared_left });
        }
        if (!linear) {
            linear = *ordinal;
            continue;
        }
        const auto count = static_cast<std::int64_t>(
            static_cast<std::int64_t>(high) - low + 1);
        const auto count_register = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(
            LoadConstant { count_register, integer_value(count) });
        const auto scaled = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            scaled, *linear, count_register });
        const auto combined = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::add,
            combined, scaled, *ordinal });
        linear = combined;
    }
    return linear;
}

std::optional<ContainerType>
Lowerer::multidimensional_container_subarray_type(
    const Expression& expression)
{
    const auto selection = selection_chain(expression);
    if (selection.base->kind != ExpressionKind::Identifier
        || selection.indices.empty()) {
        return std::nullopt;
    }
    const auto* type = object_type(selection.base->text);
    if (type == nullptr || !type->systemverilog_container) {
        return std::nullopt;
    }
    auto runtime_type = container_type(*type, expression.span);
    if (!runtime_type || !runtime_type->fixed
        || runtime_type->dimensions.size() <= 1
        || selection.indices.size()
            >= runtime_type->dimensions.size()) {
        return std::nullopt;
    }
    runtime_type->dimensions.erase(
        runtime_type->dimensions.begin(),
        runtime_type->dimensions.begin()
            + static_cast<std::ptrdiff_t>(selection.indices.size()));
    runtime_type->index_left = runtime_type->dimensions.front().first;
    runtime_type->index_right = runtime_type->dimensions.front().second;
    return runtime_type;
}

void Lowerer::copy_multidimensional_container_elements(
    const ContainerRegisterId destination,
    const RegisterId destination_base,
    const ContainerRegisterId source,
    const RegisterId source_base,
    const ContainerType& selected_type)
{
    const auto count = flattened_element_count(selected_type);
    const auto linear_index =
        [&](const RegisterId base, const std::uint64_t ordinal) {
            if (ordinal == 0)
                return base;
            const auto displacement = allocate_register(32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                displacement, integer_value(static_cast<std::int64_t>(ordinal)) });
            const auto result = allocate_register(32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::add,
                result, base, displacement });
            return result;
        };
    for (std::uint64_t ordinal = 0; ordinal < count; ++ordinal) {
        const auto source_index = linear_index(source_base, ordinal);
        const auto value = allocate_register(
            selected_type.element_width,
            selected_type.two_state
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(ContainerRead {
            value, source, source_index, true, true });
        const auto destination_index = linear_index(destination_base, ordinal);
        process_.operations.emplace_back(ContainerWrite {
            destination, destination_index, value, true, true });
    }
}

std::optional<ContainerRegisterId>
Lowerer::lower_multidimensional_container_subarray(
    const Expression& expression)
{
    const auto selection = selection_chain(expression);
    const auto selected_type = multidimensional_container_subarray_type(expression);
    if (!selected_type || selection.base->kind != ExpressionKind::Identifier) {
        return std::nullopt;
    }
    if (selected_type->element_kind != ContainerElementKind::Packed
        && selected_type->element_kind != ContainerElementKind::Scalar) {
        report(
            "FSIM-ELAB-SVMDARRAY-001",
            "multidimensional subarray selection currently requires a packed "
            "or scalar leaf profile",
            expression.span);
        return std::nullopt;
    }
    const auto* type = object_type(selection.base->text);
    const auto prefix = type != nullptr
        ? lower_multidimensional_index(expression, *type, false)
        : std::nullopt;
    const auto source = lower_container_expression(*selection.base);
    if (!prefix || !source) {
        return std::nullopt;
    }
    const auto count = flattened_element_count(*selected_type);
    const auto count_register = allocate_register(32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(LoadConstant {
        count_register,
        integer_value(static_cast<std::int64_t>(count)) });
    const auto source_base = allocate_register(32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(IntegerBinary {
        IntegerBinaryOperator::multiply,
        source_base, *prefix, count_register });
    const auto destination_base = allocate_register(32, frontend::ValueDomain::Integer);
    process_.operations.emplace_back(
        LoadConstant { destination_base, integer_value(0) });
    const auto destination = allocate_container_register(*selected_type);
    copy_multidimensional_container_elements(
        destination, destination_base,
        *source, source_base, *selected_type);
    return destination;
}

Lowerer::ExpressionAttempt
Lowerer::lower_multidimensional_container_read(
    const Expression& expression)
{
    if (expression.kind != ExpressionKind::Index) {
        return { };
    }
    const Expression* base = &expression;
    while (base->kind == ExpressionKind::Index
        && base->operands.size() == 2) {
        base = &base->operands.front();
    }
    if (base->kind != ExpressionKind::Identifier) {
        return { };
    }
    const auto* type = object_type(base->text);
    if (type == nullptr || !type->systemverilog_container
        || type->systemverilog_container
                ->static_range_expressions.size()
            <= 1) {
        return { };
    }
    const auto selection = selection_chain(expression);
    if (selection.indices.size()
        < type->systemverilog_container
            ->static_range_expressions.size()) {
        return { };
    }
    const auto linear = lower_multidimensional_index(expression, *type);
    const auto width = type->width();
    if (!linear || !width) {
        return std::nullopt;
    }
    const auto source = lower_container_expression(*base);
    if (!source) {
        return std::nullopt;
    }
    const auto destination = allocate_register(*width, type->domain);
    process_.operations.emplace_back(ContainerRead {
        destination, *source, *linear, true, true });
    return destination;
}

bool Lowerer::lower_multidimensional_container_assignment(
    const Statement& statement,
    const frontend::Type& type,
    const ContainerRegisterId target,
    const std::optional<ContainerObjectId> object)
{
    if (!type.systemverilog_container
        || type.systemverilog_container
                ->static_range_expressions.size()
            <= 1) {
        return false;
    }
    const bool nested_slice = statement.target.kind == ExpressionKind::Slice
        && statement.target.operands.size() == 3
        && statement.target.operands.front().kind
            == ExpressionKind::Index;
    if (nested_slice) {
        const auto& receiver = statement.target.operands.front();
        const auto selection = static_container_slice(statement.target);
        const auto receiver_type = multidimensional_container_subarray_type(receiver);
        if (!selection || !receiver_type) {
            return true;
        }
        const auto source = lower_static_container_assignment_value(
            statement.value, selection->selected_type);
        const auto prefix = lower_multidimensional_index(receiver, type, false);
        if (!source || !prefix) {
            return true;
        }
        const auto receiver_count = flattened_element_count(*receiver_type);
        const auto receiver_count_register = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            receiver_count_register,
            integer_value(static_cast<std::int64_t>(receiver_count)) });
        const auto prefix_base = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            prefix_base, *prefix, receiver_count_register });
        const auto trailing_count = receiver_count
            / static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    std::max(
                        receiver_type->dimensions.front().first,
                        receiver_type->dimensions.front().second))
                - std::min(
                    receiver_type->dimensions.front().first,
                    receiver_type->dimensions.front().second)
                + 1);
        const auto selected_outer_ordinal = static_cast<std::uint64_t>(
            receiver_type->index_left >= receiver_type->index_right
                ? static_cast<std::int64_t>(receiver_type->index_left)
                    - selection->selected_type.index_left
                : static_cast<std::int64_t>(
                      selection->selected_type.index_left)
                    - receiver_type->index_left);
        auto destination_base = prefix_base;
        if (selected_outer_ordinal != 0) {
            const auto displacement = allocate_register(32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant {
                displacement,
                integer_value(static_cast<std::int64_t>(
                    selected_outer_ordinal * trailing_count)) });
            destination_base = allocate_register(32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(IntegerBinary {
                IntegerBinaryOperator::add,
                destination_base, prefix_base, displacement });
        }
        const auto source_base = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(
            LoadConstant { source_base, integer_value(0) });
        copy_multidimensional_container_elements(
            target, destination_base,
            *source, source_base, selection->selected_type);
        if (object) {
            process_.operations.emplace_back(
                WriteContainerObject { *object, target, std::nullopt });
        }
        return true;
    }
    if (statement.target.kind != ExpressionKind::Index) {
        return false;
    }
    const auto selection = selection_chain(statement.target);
    const auto rank = type.systemverilog_container
                          ->static_range_expressions.size();
    if (!selection.indices.empty()
        && selection.indices.size() < rank) {
        const auto selected_type = multidimensional_container_subarray_type(statement.target);
        if (!selected_type
            || (selected_type->element_kind
                    != ContainerElementKind::Packed
                && selected_type->element_kind
                    != ContainerElementKind::Scalar)) {
            return true;
        }
        if (container_expression_type(statement.value) == nullptr
            && statement.value.kind != ExpressionKind::Slice) {
            report(
                "FSIM-ELAB-SVMDARRAY-001",
                "a partial multidimensional target requires a compatible "
                "remaining-rank subarray value",
                statement.value.span);
            return true;
        }
        const auto source = lower_static_container_assignment_value(
            statement.value, *selected_type);
        const auto prefix = lower_multidimensional_index(statement.target, type, false);
        if (!source || !prefix) {
            return true;
        }
        const auto count = flattened_element_count(*selected_type);
        const auto count_register = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(LoadConstant {
            count_register,
            integer_value(static_cast<std::int64_t>(count)) });
        const auto destination_base = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary {
            IntegerBinaryOperator::multiply,
            destination_base, *prefix, count_register });
        const auto source_base = allocate_register(32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(
            LoadConstant { source_base, integer_value(0) });
        copy_multidimensional_container_elements(
            target, destination_base,
            *source, source_base, *selected_type);
        if (object) {
            process_.operations.emplace_back(
                WriteContainerObject { *object, target, std::nullopt });
        }
        return true;
    }
    const auto linear = lower_multidimensional_index(
        statement.target, type);
    const auto element_width = type.width();
    const bool string_element = type.systemverilog_container->element_types.size() == 1
        && type.systemverilog_container->element_types.front().domain
            == frontend::ValueDomain::String;
    if (!linear || (!element_width && !string_element)) {
        return true;
    }
    if (string_element) {
        const auto value = lower_string_expression(statement.value);
        if (!value) {
            return true;
        }
        process_.operations.emplace_back(ContainerStringWrite {
            target, *linear, *value, true, true });
        if (object) {
            process_.operations.emplace_back(
                WriteContainerObject { *object, target, std::nullopt });
        }
        return true;
    }
    auto scalar_element_type = type;
    scalar_element_type.systemverilog_container.reset();
    auto value = lower_expression(
        statement.value, *element_width, &scalar_element_type);
    if (!value) {
        return true;
    }
    if (register_width(*value) != *element_width) {
        *value = resize_register(
            *value, *element_width,
            is_signed_expression(statement.value));
    }
    process_.operations.emplace_back(ContainerWrite {
        target, *linear, *value, true, true });
    if (object) {
        process_.operations.emplace_back(
            WriteContainerObject { *object, target, std::nullopt });
    }
    return true;
}

std::optional<ContainerRegisterId>
Lowerer::lower_multidimensional_container_pattern(
    const Expression& expression,
    const frontend::Type& source_type,
    const ContainerType& runtime_type)
{
    const auto destination = allocate_container_register(runtime_type);
    auto element_type = source_type;
    element_type.systemverilog_container.reset();
    std::function<bool(
        const Expression&, std::size_t, std::uint64_t)>
        lower_dimension;
    lower_dimension =
        [&](const Expression& pattern,
            const std::size_t dimension,
            const std::uint64_t base) {
            if (pattern.kind != ExpressionKind::Aggregate
                || pattern.text != "sv-pattern"
                || pattern.operands.size()
                    != pattern.aggregate_choices.size()
                || pattern.operands.size()
                    != pattern.aggregate_choice_expressions.size()) {
                report(
                    "FSIM-ELAB-SVPATTERN-001",
                    "each multidimensional static-array dimension requires "
                    "consistent nested assignment-pattern metadata",
                    pattern.span);
                return false;
            }
            const auto& bounds = runtime_type.dimensions[dimension];
            const auto count = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    std::max(bounds.first, bounds.second))
                - std::min(bounds.first, bounds.second) + 1);
            bool positional { };
            bool keyed { };
            std::optional<const Expression*> default_value;
            std::map<std::int32_t, const Expression*> explicit_values;
            for (std::size_t member = 0;
                member < pattern.operands.size(); ++member) {
                const auto& choice = pattern.aggregate_choices[member];
                const auto& choices = pattern.aggregate_choice_expressions[member];
                positional |= choice.empty();
                keyed |= !choice.empty();
                if (choice.empty() && choices.empty()) {
                    continue;
                }
                if (choice == "default" && choices.size() == 1
                    && choices.front().kind
                        == ExpressionKind::DefaultChoice) {
                    if (default_value) {
                        report(
                            "FSIM-ELAB-SVPATTERN-005",
                            "a multidimensional assignment-pattern dimension "
                            "cannot contain more than one default",
                            pattern.operands[member].span);
                        return false;
                    }
                    default_value = &pattern.operands[member];
                    continue;
                }
                if (choice != "@key" || choices.size() != 1) {
                    report(
                        "FSIM-ELAB-SVPATTERN-001",
                        "a multidimensional assignment-pattern association has "
                        "inconsistent key metadata",
                        pattern.operands[member].span);
                    return false;
                }
                const auto key = static_integer_value(choices.front());
                if (!key
                    || *key < std::min(bounds.first, bounds.second)
                    || *key > std::max(bounds.first, bounds.second)) {
                    report(
                        key ? "FSIM-ELAB-SVPATTERN-007"
                            : "FSIM-ELAB-SVPATTERN-006",
                        key
                            ? "a multidimensional assignment-pattern key is "
                              "outside the declared dimension"
                            : "multidimensional assignment-pattern keys must be "
                              "locally constant known integral values",
                        choices.front().span);
                    return false;
                }
                if (!explicit_values.emplace(
                                        static_cast<std::int32_t>(*key),
                                        &pattern.operands[member])
                        .second) {
                    report(
                        "FSIM-ELAB-SVPATTERN-007",
                        "multidimensional assignment-pattern keys must be unique",
                        choices.front().span);
                    return false;
                }
            }
            if (positional && keyed) {
                report(
                    "FSIM-ELAB-SVPATTERN-004",
                    "one multidimensional assignment-pattern dimension cannot "
                    "mix positional and keyed/default members",
                    pattern.span);
                return false;
            }
            if ((positional && pattern.operands.size() != count)
                || (keyed && !default_value)) {
                report(
                    keyed ? "FSIM-ELAB-SVPATTERN-005"
                          : "FSIM-ELAB-SVPATTERN-002",
                    keyed
                        ? "a keyed multidimensional assignment-pattern "
                          "dimension requires exactly one default"
                        : "a positional multidimensional assignment-pattern "
                          "dimension must match its declared element count",
                    pattern.span);
                return false;
            }
            for (std::uint64_t ordinal = 0; ordinal < count; ++ordinal) {
                const auto declared = static_cast<std::int32_t>(
                    static_cast<std::int64_t>(bounds.first)
                    + (bounds.first >= bounds.second
                            ? -static_cast<std::int64_t>(ordinal)
                            : static_cast<std::int64_t>(ordinal)));
                const Expression* value = nullptr;
                if (positional) {
                    value = &pattern.operands[ordinal];
                } else if (const auto found = explicit_values.find(declared);
                    found != explicit_values.end()) {
                    value = found->second;
                } else if (default_value) {
                    value = *default_value;
                }
                if (value == nullptr) {
                    report(
                        "FSIM-ELAB-SVPATTERN-002",
                        "a multidimensional assignment pattern leaves a declared "
                        "element uncovered",
                        pattern.span);
                    return false;
                }
                const auto linear = base * count + ordinal;
                if (dimension + 1U < runtime_type.dimensions.size()) {
                    if (!lower_dimension(*value, dimension + 1U, linear)) {
                        return false;
                    }
                    continue;
                }
                auto lowered = lower_expression(
                    *value, runtime_type.element_width, &element_type);
                if (!lowered) {
                    return false;
                }
                if (register_width(*lowered) != runtime_type.element_width) {
                    *lowered = resize_register(
                        *lowered,
                        runtime_type.element_width,
                        is_signed_expression(*value));
                }
                const auto index = allocate_register(32, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    index, unsigned_value(linear, 32) });
                process_.operations.emplace_back(ContainerWrite {
                    destination, index, *lowered, true, true });
            }
            return true;
        };
    return lower_dimension(expression, 0, 0)
        ? std::optional<ContainerRegisterId> { destination }
        : std::nullopt;
}

} // namespace fsim::elaboration
