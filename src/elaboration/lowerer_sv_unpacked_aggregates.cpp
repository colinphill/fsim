// SPDX-License-Identifier: Apache-2.0

#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

namespace {

    constexpr std::string_view member_index_prefix { "index." };

    [[nodiscard]] bool aggregate_element_container(
        const frontend::Type& type)
    {
        return type.systemverilog_container
            && type.systemverilog_container->element_types.size() == 1
            && (type.systemverilog_container->element_types.front()
                        .packed_aggregate
                    == frontend::PackedAggregateKind::UnpackedStruct
                || type.systemverilog_container->element_types.front()
                        .packed_aggregate
                    == frontend::PackedAggregateKind::UnpackedUnion);
    }

    [[nodiscard]] const frontend::Expression* direct_container_base(
        const frontend::Expression& expression)
    {
        if (expression.kind != frontend::ExpressionKind::Index
            || expression.operands.size() != 2) {
            return nullptr;
        }
        return &expression.operands.front();
    }

} // namespace

std::optional<Lowerer::UnpackedAggregateMemberReference>
Lowerer::unpacked_aggregate_member_reference(
    const Expression& expression) const
{
    const auto* base = direct_container_base(expression);
    if (base == nullptr
        || !expression.text.starts_with(member_index_prefix)) {
        return std::nullopt;
    }
    const auto resolved_container = base->kind == ExpressionKind::Identifier
        ? std::optional<frontend::Type> { }
        : vhdl_expression_type(*base);
    const auto* container = base->kind == ExpressionKind::Identifier
        ? object_type(base->text)
        : resolved_container ? &*resolved_container : nullptr;
    if (container == nullptr || !aggregate_element_container(*container)) {
        return std::nullopt;
    }

    const frontend::Type* selected = &container->systemverilog_container->element_types.front();
    UnpackedAggregateMemberReference result;
    auto remaining = std::string_view { expression.text }.substr(
        member_index_prefix.size());
    while (!remaining.empty()) {
        const auto separator = remaining.find('.');
        const auto name = remaining.substr(0, separator);
        if (name.empty()
            || (selected->packed_aggregate
                    != frontend::PackedAggregateKind::UnpackedStruct
                && selected->packed_aggregate
                    != frontend::PackedAggregateKind::UnpackedUnion)) {
            return std::nullopt;
        }
        const auto member = std::ranges::find(
            selected->packed_members, name, &frontend::PackedMember::name);
        if (member == selected->packed_members.end()
            || member->nested_types.size() != 1) {
            return std::nullopt;
        }
        result.members.push_back(static_cast<std::uint32_t>(
            std::distance(selected->packed_members.begin(), member)));
        selected = &member->nested_types.front();
        if (separator == std::string_view::npos) {
            remaining = { };
        } else {
            remaining.remove_prefix(separator + 1);
        }
    }
    if (result.members.empty()) {
        return std::nullopt;
    }
    result.leaf_type = *selected;
    return result;
}

Lowerer::ExpressionAttempt
Lowerer::lower_unpacked_aggregate_member_read(
    const Expression& expression)
{
    const auto* base = direct_container_base(expression);
    if (base == nullptr
        || !expression.text.starts_with(member_index_prefix)) {
        return { };
    }
    const auto resolved_type = base->kind == ExpressionKind::Identifier
        ? std::optional<frontend::Type> { }
        : vhdl_expression_type(*base);
    const auto* type = base->kind == ExpressionKind::Identifier
        ? object_type(base->text)
        : resolved_type ? &*resolved_type : nullptr;
    if (type == nullptr || !aggregate_element_container(*type)) {
        return { };
    }
    const auto reference = unpacked_aggregate_member_reference(expression);
    if (!reference) {
        report(
            "FSIM-ELAB-SVCONTAINER-024",
            "selected unpacked aggregate member path is not executable",
            expression.span);
        return std::nullopt;
    }
    const auto width = reference->leaf_type.width();
    if (!width || *width == 0
        || reference->leaf_type.domain
            == frontend::ValueDomain::String
        || reference->leaf_type.systemverilog_container
        || reference->leaf_type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedStruct
        || reference->leaf_type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedUnion) {
        report(
            "FSIM-ELAB-SVCONTAINER-024",
            "selected string, nested-container, and aggregate-valued members "
            "require a typed aggregate member operation",
            expression.span);
        return std::nullopt;
    }
    const auto runtime_type = container_type(*type, expression.span);
    if (!runtime_type
        || runtime_type->element_kind
            != ContainerElementKind::Aggregate
        || runtime_type->dimensions.size() > 1) {
        return std::nullopt;
    }
    const auto index_width = runtime_type->associative
        ? static_cast<std::size_t>(runtime_type->index_width)
        : runtime_type->fixed
        ? std::size_t { 32 }
        : infer_width(expression.operands[1]).value_or(32U);
    auto index = lower_expression(
        expression.operands[1], index_width,
        runtime_type->associative
            ? type->systemverilog_container
                  ->associative_index_type.get()
            : nullptr);
    const auto source = lower_container_expression(*base);
    if (!index || !source) {
        return std::nullopt;
    }
    if (runtime_type->associative
        && register_width(*index) != runtime_type->index_width) {
        *index = resize_register(
            *index, runtime_type->index_width,
            runtime_type->signed_indices);
    }
    const auto destination = allocate_register(*width, reference->leaf_type.domain);
    process_.operations.emplace_back(ContainerAggregateRead {
        destination, *source, *index, reference->members,
        runtime_type->associative
            ? runtime_type->signed_indices
            : runtime_type->fixed
                || is_signed_expression(expression.operands[1]),
        false });
    return destination;
}

bool Lowerer::lower_unpacked_aggregate_pattern_value(
    const Expression& expression,
    const frontend::Type& aggregate_type,
    const ContainerRegisterId target,
    const RegisterId index,
    std::vector<std::uint32_t> members,
    const bool signed_index,
    const bool linear_index)
{
    const bool aggregate = aggregate_type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedStruct
        || aggregate_type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedUnion;
    if (!aggregate || aggregate_type.packed_members.empty()) {
        report(
            "FSIM-ELAB-SVPATTERN-001",
            "an unpacked aggregate pattern requires a complete member profile",
            expression.span);
        return false;
    }
    const bool union_value = aggregate_type.packed_aggregate
        == frontend::PackedAggregateKind::UnpackedUnion;
    std::vector<const Expression*> values(
        aggregate_type.packed_members.size());
    if (expression.kind == ExpressionKind::Aggregate
        && expression.text == "sv-pattern") {
        if (expression.operands.size()
                != expression.aggregate_choices.size()
            || expression.operands.size()
                != expression.aggregate_choice_expressions.size()) {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "an unpacked aggregate pattern has inconsistent metadata",
                expression.span);
            return false;
        }
        bool positional { };
        bool keyed { };
        const Expression* default_value = nullptr;
        for (std::size_t item = 0;
            item < expression.operands.size(); ++item) {
            const auto& choice = expression.aggregate_choices[item];
            const auto& choices = expression.aggregate_choice_expressions[item];
            if (choice.empty()) {
                positional = true;
                if (!choices.empty()) {
                    report(
                        "FSIM-ELAB-SVPATTERN-001",
                        "a positional aggregate member has key metadata",
                        expression.operands[item].span);
                    return false;
                }
                continue;
            }
            keyed = true;
            if (choice == "default") {
                if (choices.size() != 1
                    || choices.front().kind
                        != ExpressionKind::DefaultChoice
                    || default_value != nullptr) {
                    report(
                        "FSIM-ELAB-SVPATTERN-005",
                        "an unpacked aggregate pattern permits one default member",
                        expression.operands[item].span);
                    return false;
                }
                default_value = &expression.operands[item];
                continue;
            }
            if (choice != "@key" || choices.size() != 1
                || choices.front().kind != ExpressionKind::Identifier) {
                report(
                    "FSIM-ELAB-SVPATTERN-001",
                    "unpacked aggregate pattern keys must name members",
                    expression.operands[item].span);
                return false;
            }
            const auto found = std::ranges::find(
                aggregate_type.packed_members,
                choices.front().text,
                &frontend::PackedMember::name);
            if (found == aggregate_type.packed_members.end()) {
                report(
                    "FSIM-ELAB-SVPATTERN-007",
                    "unpacked aggregate pattern names an unknown member '"
                        + choices.front().text + "'",
                    choices.front().span);
                return false;
            }
            const auto member = static_cast<std::size_t>(
                std::distance(
                    aggregate_type.packed_members.begin(), found));
            if (values[member] != nullptr) {
                report(
                    "FSIM-ELAB-SVPATTERN-007",
                    "unpacked aggregate pattern member keys must be unique",
                    choices.front().span);
                return false;
            }
            values[member] = &expression.operands[item];
        }
        if (positional && keyed) {
            report(
                "FSIM-ELAB-SVPATTERN-004",
                "an unpacked aggregate pattern cannot mix positional and keyed members",
                expression.span);
            return false;
        }
        if (positional) {
            const auto expected = union_value
                ? std::size_t { 1 }
                : aggregate_type.packed_members.size();
            if (expression.operands.size() != expected) {
                report(
                    "FSIM-ELAB-SVPATTERN-002",
                    "a positional unpacked aggregate pattern must match its member count",
                    expression.span);
                return false;
            }
            for (std::size_t member = 0; member < expected; ++member) {
                values[member] = &expression.operands[member];
            }
        } else if (union_value) {
            const auto selected = static_cast<std::size_t>(std::ranges::count_if(
                values, [](const Expression* value) {
                    return value != nullptr;
                }));
            if (selected != 1 || default_value != nullptr) {
                report(
                    "FSIM-ELAB-SVPATTERN-005",
                    "an unpacked union pattern requires exactly one named member",
                    expression.span);
                return false;
            }
        } else {
            for (auto& value : values) {
                if (value == nullptr)
                    value = default_value;
            }
        }
    } else {
        const auto count = union_value
            ? std::size_t { 1 }
            : aggregate_type.packed_members.size();
        for (std::size_t member = 0; member < count; ++member) {
            values[member] = &expression;
        }
    }

    for (std::size_t member = 0; member < values.size(); ++member) {
        if (values[member] == nullptr) {
            if (union_value) {
                continue;
            }
            report(
                "FSIM-ELAB-SVPATTERN-002",
                "an unpacked struct pattern leaves a member uncovered",
                expression.span);
            return false;
        }
        const auto& source_member = aggregate_type.packed_members[member];
        if (source_member.nested_types.size() != 1) {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "an unpacked aggregate pattern member lacks its complete type",
                source_member.span);
            return false;
        }
        const auto& member_type = source_member.nested_types.front();
        auto path = members;
        path.push_back(static_cast<std::uint32_t>(member));
        if (member_type.packed_aggregate
                == frontend::PackedAggregateKind::UnpackedStruct
            || member_type.packed_aggregate
                == frontend::PackedAggregateKind::UnpackedUnion) {
            if (!lower_unpacked_aggregate_pattern_value(
                    *values[member], member_type, target, index,
                    std::move(path), signed_index, linear_index)) {
                return false;
            }
            continue;
        }
        const auto width = member_type.width();
        if (!width || *width == 0
            || member_type.domain == frontend::ValueDomain::String
            || member_type.systemverilog_container) {
            report(
                "FSIM-ELAB-SVCONTAINER-024",
                "unpacked aggregate patterns require packed or scalar leaves",
                values[member]->span);
            return false;
        }
        auto value = lower_expression(
            *values[member], *width, &member_type);
        if (!value)
            return false;
        if (register_width(*value) != *width) {
            *value = resize_register(
                *value, *width,
                is_signed_expression(*values[member]));
        }
        process_.operations.emplace_back(ContainerAggregateWrite {
            target, index, *value, std::move(path),
            signed_index, linear_index });
    }
    return true;
}

std::optional<ContainerRegisterId>
Lowerer::lower_unpacked_aggregate_pattern(
    const Expression& expression,
    const frontend::Type& source_type,
    const ContainerType& runtime_type)
{
    if (runtime_type.element_kind != ContainerElementKind::Aggregate
        || !source_type.systemverilog_container
        || source_type.systemverilog_container->element_types.size() != 1
        || expression.kind != ExpressionKind::Aggregate
        || expression.text != "sv-pattern") {
        return std::nullopt;
    }
    const auto& element_type = source_type.systemverilog_container->element_types.front();
    const auto destination = allocate_container_register(runtime_type);
    const auto emit = [&](const std::uint64_t ordinal,
                          const Expression& value,
                          const bool linear_index) {
        const auto index = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            index, unsigned_value(ordinal, 32) });
        return lower_unpacked_aggregate_pattern_value(
            value, element_type, destination, index, { },
            true, linear_index);
    };

    if (runtime_type.fixed) {
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
                        "each static-array dimension requires a nested assignment pattern",
                        pattern.span);
                    return false;
                }
                const auto bounds = runtime_type.dimensions.empty()
                    ? ContainerDimension {
                          runtime_type.index_left,
                          runtime_type.index_right
                      }
                    : runtime_type.dimensions[dimension];
                const auto count = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(
                        std::max(bounds.first, bounds.second))
                    - std::min(bounds.first, bounds.second) + 1);
                bool positional { };
                bool keyed { };
                const Expression* default_value = nullptr;
                std::map<std::int32_t, const Expression*> explicit_values;
                for (std::size_t item = 0;
                    item < pattern.operands.size(); ++item) {
                    const auto& choice = pattern.aggregate_choices[item];
                    const auto& choices = pattern.aggregate_choice_expressions[item];
                    if (choice.empty() && choices.empty()) {
                        positional = true;
                        continue;
                    }
                    keyed = true;
                    if (choice == "default" && choices.size() == 1
                        && choices.front().kind
                            == ExpressionKind::DefaultChoice) {
                        if (default_value != nullptr) {
                            report(
                                "FSIM-ELAB-SVPATTERN-005",
                                "a static-array pattern dimension permits one default",
                                pattern.operands[item].span);
                            return false;
                        }
                        default_value = &pattern.operands[item];
                        continue;
                    }
                    if (choice != "@key" || choices.size() != 1) {
                        report(
                            "FSIM-ELAB-SVPATTERN-001",
                            "a static-array aggregate pattern has invalid key metadata",
                            pattern.operands[item].span);
                        return false;
                    }
                    const auto key = static_integer_value(choices.front());
                    if (!key || *key < std::min(bounds.first, bounds.second)
                        || *key > std::max(bounds.first, bounds.second)
                        || !explicit_values.emplace(
                                               static_cast<std::int32_t>(*key),
                                               &pattern.operands[item])
                            .second) {
                        report(
                            key ? "FSIM-ELAB-SVPATTERN-007"
                                : "FSIM-ELAB-SVPATTERN-006",
                            "a static-array aggregate pattern key is invalid, duplicate, or out of range",
                            choices.front().span);
                        return false;
                    }
                }
                if (positional && keyed) {
                    report(
                        "FSIM-ELAB-SVPATTERN-004",
                        "one static-array pattern dimension cannot mix positional and keyed members",
                        pattern.span);
                    return false;
                }
                if ((positional && pattern.operands.size() != count)
                    || (keyed && default_value == nullptr)) {
                    report(
                        keyed ? "FSIM-ELAB-SVPATTERN-005"
                              : "FSIM-ELAB-SVPATTERN-002",
                        "a static-array aggregate pattern does not cover its declared dimension",
                        pattern.span);
                    return false;
                }
                for (std::uint64_t ordinal = 0;
                    ordinal < count; ++ordinal) {
                    const auto declared = static_cast<std::int32_t>(
                        static_cast<std::int64_t>(bounds.first)
                        + (bounds.first >= bounds.second
                                ? -static_cast<std::int64_t>(ordinal)
                                : static_cast<std::int64_t>(ordinal)));
                    const Expression* value = positional
                        ? &pattern.operands[ordinal]
                        : default_value;
                    if (const auto found = explicit_values.find(declared);
                        found != explicit_values.end()) {
                        value = found->second;
                    }
                    const auto linear = base * count + ordinal;
                    if (dimension + 1U
                        < std::max<std::size_t>(
                            1U, runtime_type.dimensions.size())) {
                        if (!lower_dimension(
                                *value, dimension + 1U, linear)) {
                            return false;
                        }
                    } else if (!emit(linear, *value, true)) {
                        return false;
                    }
                }
                return true;
            };
        return lower_dimension(expression, 0, 0)
            ? std::optional<ContainerRegisterId> { destination }
            : std::nullopt;
    }

    if (expression.operands.size()
            != expression.aggregate_choices.size()
        || expression.operands.size()
            != expression.aggregate_choice_expressions.size()
        || expression.operands.size()
            > maximum_container_elements(runtime_type)
        || (runtime_type.maximum_elements
            && expression.operands.size()
                > *runtime_type.maximum_elements)) {
        report(
            "FSIM-ELAB-SVPATTERN-002",
            "an unpacked aggregate pattern exceeds its container capacity",
            expression.span);
        return std::nullopt;
    }
    if (!runtime_type.associative) {
        for (std::size_t item = 0;
            item < expression.operands.size(); ++item) {
            if (!expression.aggregate_choices[item].empty()
                || !expression.aggregate_choice_expressions[item].empty()) {
                report(
                    "FSIM-ELAB-SVPATTERN-001",
                    "dynamic-array and queue aggregate patterns require positional members",
                    expression.operands[item].span);
                return std::nullopt;
            }
        }
        const auto size = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            size, unsigned_value(expression.operands.size(), 32) });
        process_.operations.emplace_back(ResizeContainer {
            destination, size, std::nullopt, runtime_type.queue });
        for (std::size_t item = 0;
            item < expression.operands.size(); ++item) {
            if (!emit(item, expression.operands[item], false)) {
                return std::nullopt;
            }
        }
        return destination;
    }

    std::vector<PackedLogic4> keys;
    for (std::size_t item = 0;
        item < expression.operands.size(); ++item) {
        const auto& choices = expression.aggregate_choice_expressions[item];
        if (expression.aggregate_choices[item] != "@key"
            || choices.size() != 1) {
            report(
                "FSIM-ELAB-SVPATTERN-003",
                "associative aggregate patterns require one key per member",
                expression.operands[item].span);
            return std::nullopt;
        }
        const auto* index_type = source_type.systemverilog_container
                                     ->associative_index_type.get();
        std::string error;
        const auto key_value = index_type
            ? evaluate_systemverilog_packed_constant(
                  choices.front(), *index_type, { }, { }, error)
            : std::nullopt;
        const bool unknown = key_value
            && (key_value->is_logic9()
                || std::ranges::any_of(
                    key_value->bval_words(),
                    [](const std::uint64_t word) { return word != 0; }));
        if (!key_value || unknown) {
            report(
                "FSIM-ELAB-SVPATTERN-003",
                "associative aggregate pattern keys must be locally constant "
                "known integral values",
                choices.front().span);
            return std::nullopt;
        }
        if (std::ranges::find(keys, *key_value) != keys.end()) {
            report(
                "FSIM-ELAB-SVPATTERN-003",
                "associative aggregate pattern keys must be unique after conversion",
                choices.front().span);
            return std::nullopt;
        }
        keys.push_back(*key_value);
        auto key = lower_expression(
            choices.front(), runtime_type.index_width,
            index_type);
        if (!key)
            return std::nullopt;
        if (register_width(*key) != runtime_type.index_width) {
            *key = resize_register(
                *key, runtime_type.index_width,
                runtime_type.signed_indices);
        }
        if (!lower_unpacked_aggregate_pattern_value(
                expression.operands[item], element_type,
                destination, *key, { },
                runtime_type.signed_indices, false)) {
            return std::nullopt;
        }
    }
    return destination;
}

bool Lowerer::lower_unpacked_aggregate_assignment(
    const Statement& statement,
    const frontend::Type& type,
    const ContainerRegisterId target,
    const std::optional<ContainerObjectId> object)
{
    if (!aggregate_element_container(type)
        || statement.target.kind != ExpressionKind::Index
        || statement.target.operands.size() != 2
        || statement.target.operands.front().kind
            != ExpressionKind::Identifier) {
        return false;
    }
    const auto runtime_type = container_type(type, statement.target.span);
    if (!runtime_type
        || runtime_type->element_kind
            != ContainerElementKind::Aggregate
        || runtime_type->dimensions.size() > 1) {
        return false;
    }
    const auto index_width = runtime_type->associative
        ? static_cast<std::size_t>(runtime_type->index_width)
        : runtime_type->fixed
        ? std::size_t { 32 }
        : infer_width(statement.target.operands[1]).value_or(32U);
    auto index = lower_expression(
        statement.target.operands[1], index_width,
        runtime_type->associative
            ? type.systemverilog_container
                  ->associative_index_type.get()
            : nullptr);
    if (!index) {
        return true;
    }
    if (runtime_type->associative
        && register_width(*index) != runtime_type->index_width) {
        *index = resize_register(
            *index, runtime_type->index_width,
            runtime_type->signed_indices);
    }
    const auto target_signed = runtime_type->associative
        ? runtime_type->signed_indices
        : runtime_type->fixed
            || is_signed_expression(statement.target.operands[1]);

    if (statement.target.text.starts_with(member_index_prefix)) {
        const auto reference = unpacked_aggregate_member_reference(statement.target);
        const auto width = reference
            ? reference->leaf_type.width()
            : std::nullopt;
        if (!reference || !width || *width == 0
            || reference->leaf_type.domain
                == frontend::ValueDomain::String
            || reference->leaf_type.systemverilog_container
            || reference->leaf_type.packed_aggregate
                == frontend::PackedAggregateKind::UnpackedStruct
            || reference->leaf_type.packed_aggregate
                == frontend::PackedAggregateKind::UnpackedUnion) {
            report(
                "FSIM-ELAB-SVCONTAINER-024",
                "selected string, nested-container, and aggregate-valued members "
                "require a typed aggregate member operation",
                statement.target.span);
            return true;
        }
        auto value = lower_expression(
            statement.value, *width, &reference->leaf_type);
        if (!value) {
            return true;
        }
        if (register_width(*value) != *width) {
            *value = resize_register(
                *value, *width,
                is_signed_expression(statement.value));
        }
        process_.operations.emplace_back(ContainerAggregateWrite {
            target, *index, *value, reference->members,
            target_signed, false });
        if (object) {
            process_.operations.emplace_back(
                WriteContainerObject { *object, target, std::nullopt });
        }
        return true;
    }

    const auto* source_base = direct_container_base(statement.value);
    if (statement.target.text != "index" || source_base == nullptr
        || statement.value.text != "index") {
        report(
            "FSIM-ELAB-SVCONTAINER-024",
            "an unpacked aggregate element assignment requires a compatible "
            "selected aggregate element value",
            statement.value.span);
        return true;
    }
    const auto* source_type = object_type(source_base->text);
    const auto source_runtime_type = source_type == nullptr
        ? std::nullopt
        : container_type(*source_type, statement.value.span);
    if (!source_runtime_type || *source_runtime_type != *runtime_type
        || source_runtime_type->dimensions.size() > 1) {
        report(
            "FSIM-ELAB-SVCONTAINER-024",
            "unpacked aggregate element copies require identical container "
            "profiles",
            statement.value.span);
        return true;
    }
    const auto source_index_width = source_runtime_type->associative
        ? static_cast<std::size_t>(source_runtime_type->index_width)
        : source_runtime_type->fixed
        ? std::size_t { 32 }
        : infer_width(statement.value.operands[1]).value_or(32U);
    auto source_index = lower_expression(
        statement.value.operands[1], source_index_width,
        source_runtime_type->associative
            ? source_type->systemverilog_container
                  ->associative_index_type.get()
            : nullptr);
    const auto source = lower_container_expression(*source_base);
    if (!source_index || !source) {
        return true;
    }
    if (source_runtime_type->associative
        && register_width(*source_index)
            != source_runtime_type->index_width) {
        *source_index = resize_register(
            *source_index, source_runtime_type->index_width,
            source_runtime_type->signed_indices);
    }
    const auto source_signed = source_runtime_type->associative
        ? source_runtime_type->signed_indices
        : source_runtime_type->fixed
            || is_signed_expression(statement.value.operands[1]);
    process_.operations.emplace_back(CopyContainerAggregateElement {
        target, *index, *source, *source_index,
        target_signed, source_signed });
    if (object) {
        process_.operations.emplace_back(
            WriteContainerObject { *object, target, std::nullopt });
    }
    return true;
}

} // namespace fsim::elaboration
