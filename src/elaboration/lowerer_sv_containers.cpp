// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

namespace {

    [[nodiscard]] bool aggregate_box(
        const ContainerType& type) noexcept
    {
        return type.element_kind == ContainerElementKind::Aggregate
            && type.aggregate_value;
    }

    [[nodiscard]] std::optional<std::uint64_t> container_value_bits(
        const ContainerType& type);

    [[nodiscard]] std::optional<std::uint64_t> aggregate_type_bits(
        const frontend::Type& type)
    {
        const bool unpacked_union = type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedUnion;
        const bool unpacked_structure = type.packed_aggregate
            == frontend::PackedAggregateKind::UnpackedStruct;
        if (!unpacked_union && !unpacked_structure) {
            return type.width();
        }
        if (type.packed_members.empty()) {
            return std::nullopt;
        }
        std::uint64_t result { };
        for (const auto& member : type.packed_members) {
            if (member.nested_types.size() != 1U) {
                return std::nullopt;
            }
            const auto bits = aggregate_type_bits(member.nested_types.front());
            if (!bits) {
                return std::nullopt;
            }
            if (unpacked_union) {
                result = std::max(result, *bits);
            } else {
                if (*bits > std::numeric_limits<std::uint64_t>::max() - result) {
                    return std::nullopt;
                }
                result += *bits;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::uint64_t> container_element_bits(
        const ContainerType& type)
    {
        switch (type.element_kind) {
        case ContainerElementKind::Packed:
        case ContainerElementKind::Scalar:
            return type.element_width;
        case ContainerElementKind::String:
            return std::nullopt;
        case ContainerElementKind::Container:
            return type.element_types.size() == 1U
                ? container_value_bits(type.element_types.front())
                : std::nullopt;
        case ContainerElementKind::Aggregate:
            break;
        }
        if (type.element_types.empty()) {
            return std::nullopt;
        }
        std::uint64_t result { };
        for (const auto& member : type.element_types) {
            const auto bits = container_value_bits(member);
            if (!bits) {
                return std::nullopt;
            }
            if (type.union_aggregate) {
                result = std::max(result, *bits);
            } else {
                if (*bits > std::numeric_limits<std::uint64_t>::max() - result) {
                    return std::nullopt;
                }
                result += *bits;
            }
        }
        return result;
    }

    [[nodiscard]] std::optional<std::uint64_t> container_value_bits(
        const ContainerType& type)
    {
        const auto element = container_element_bits(type);
        if (!element) {
            return std::nullopt;
        }
        if (aggregate_box(type)) {
            return element;
        }
        if (!type.fixed) {
            return std::nullopt;
        }
        std::uint64_t count { 1 };
        if (type.dimensions.empty()) {
            count = static_cast<std::uint64_t>(
                std::abs(
                    static_cast<std::int64_t>(type.index_left)
                    - type.index_right)
                + 1);
        } else {
            for (const auto& dimension : type.dimensions) {
                const auto extent = static_cast<std::uint64_t>(
                    std::abs(
                        static_cast<std::int64_t>(dimension.first)
                        - dimension.second)
                    + 1);
                if (extent != 0
                    && count
                        > std::numeric_limits<std::uint64_t>::max() / extent) {
                    return std::nullopt;
                }
                count *= extent;
            }
        }
        if (*element != 0
            && count
                > std::numeric_limits<std::uint64_t>::max() / *element) {
            return std::nullopt;
        }
        return count * *element;
    }

} // namespace

ContainerRegisterId Lowerer::allocate_container_register(
    const ContainerType& type)
{
    const auto id = next_container_register_++;
    process_.container_register_types.push_back(type);
    return id;
}

bool Lowerer::is_container_expression(
    const Expression& expression) const
{
    if (expression.kind != ExpressionKind::Identifier
        && expression.kind != ExpressionKind::Call
        && expression.kind != ExpressionKind::Index) {
        return false;
    }
    return container_expression_type(expression) != nullptr;
}

const frontend::Type* Lowerer::container_expression_type(
    const Expression& expression) const
{
    if (expression.kind == ExpressionKind::Identifier) {
        const auto* type = object_type(expression.text);
        return type != nullptr && type->systemverilog_container
            ? type
            : nullptr;
    }
    if (expression.kind == ExpressionKind::Call) {
        if (expression.text == "?:"
            && expression.operands.size() == 3) {
            const auto* when_true = container_expression_type(expression.operands[1]);
            const auto* when_false = container_expression_type(expression.operands[2]);
            return when_true != nullptr && when_false != nullptr
                ? when_true
                : nullptr;
        }
        const auto* function = visible_function(expression.text);
        return function != nullptr
                && function->return_type.systemverilog_container
            ? &function->return_type
            : nullptr;
    }
    if (expression.kind == ExpressionKind::Slice
        && !expression.operands.empty()) {
        return container_expression_type(expression.operands.front());
    }
    if (expression.kind == ExpressionKind::Index) {
        if (expression.operands.size() == 2) {
            const auto* base_type = container_expression_type(expression.operands.front());
            if (base_type != nullptr && base_type->systemverilog_container
                && base_type->systemverilog_container
                        ->element_types.size()
                    == 1) {
                const auto& element = base_type->systemverilog_container->element_types.front();
                if (element.systemverilog_container) {
                    return &element;
                }
            }
        }
        const Expression* base = &expression;
        std::size_t selected_dimensions { };
        while (base->kind == ExpressionKind::Index
            && base->operands.size() == 2) {
            ++selected_dimensions;
            base = &base->operands.front();
        }
        if (base->kind == ExpressionKind::Identifier) {
            const auto* type = object_type(base->text);
            if (type != nullptr && type->systemverilog_container
                && selected_dimensions > 0
                && selected_dimensions
                    < type->systemverilog_container
                        ->static_range_expressions.size()) {
                return type;
            }
        }
    }
    return nullptr;
}

std::optional<ContainerType>
Lowerer::container_expression_runtime_type(
    const Expression& expression)
{
    if (expression.kind == ExpressionKind::Call
        && expression.text == "?:"
        && expression.operands.size() == 3) {
        const auto when_true = container_expression_runtime_type(expression.operands[1]);
        const auto when_false = container_expression_runtime_type(expression.operands[2]);
        if (!when_true || !when_false || *when_true != *when_false) {
            report(
                "FSIM-ELAB-SVCOND-002",
                "container conditional alternatives require an exactly "
                "compatible kind and profile",
                expression.span);
            return std::nullopt;
        }
        if (when_true->associative) {
            report(
                "FSIM-ELAB-SVCOND-003",
                "associative-array conditional values are outside the "
                "bounded read-only consumer subset",
                expression.span);
            return std::nullopt;
        }
        return when_true;
    }
    if (expression.kind == ExpressionKind::Slice) {
        const auto selection = static_container_slice(expression);
        return selection
            ? std::optional<ContainerType> { selection->selected_type }
            : std::nullopt;
    }
    if (expression.kind == ExpressionKind::Index) {
        if (expression.operands.size() == 2) {
            const auto base = container_expression_runtime_type(
                expression.operands.front());
            if (base
                && base->element_kind == ContainerElementKind::Container
                && base->element_types.size() == 1) {
                return base->element_types.front();
            }
        }
        return multidimensional_container_subarray_type(expression);
    }
    const auto* type = container_expression_type(expression);
    return type != nullptr
        ? container_type(*type, expression.span)
        : std::nullopt;
}

std::optional<ContainerRegisterId>
Lowerer::lower_container_expression(
    const Expression& expression)
{
    if (expression.kind == ExpressionKind::Index) {
        if (expression.operands.size() == 2) {
            const auto runtime_type = container_expression_runtime_type(
                expression.operands.front());
            if (runtime_type
                && runtime_type->element_kind
                    == ContainerElementKind::Container
                && runtime_type->element_types.size() == 1) {
                const auto source = lower_container_expression(
                    expression.operands.front());
                const auto* source_type = container_expression_type(expression.operands.front());
                const auto index_width = runtime_type->associative
                    ? static_cast<std::size_t>(runtime_type->index_width)
                    : runtime_type->fixed
                    ? std::size_t { 32 }
                    : infer_width(expression.operands[1]).value_or(32U);
                auto index = lower_expression(
                    expression.operands[1], index_width,
                    runtime_type->associative && source_type != nullptr
                            && source_type->systemverilog_container
                        ? source_type->systemverilog_container
                              ->associative_index_type.get()
                        : nullptr);
                if (!source || !index) {
                    return std::nullopt;
                }
                if (runtime_type->associative
                    && register_width(*index) != runtime_type->index_width) {
                    *index = resize_register(
                        *index, runtime_type->index_width,
                        runtime_type->signed_indices);
                }
                const auto destination = allocate_container_register(
                    runtime_type->element_types.front());
                process_.operations.emplace_back(ContainerElementRead {
                    destination, *source, *index,
                    runtime_type->associative
                        ? runtime_type->signed_indices
                        : runtime_type->fixed
                            || is_signed_expression(expression.operands[1]) });
                return destination;
            }
        }
        return lower_multidimensional_container_subarray(expression);
    }
    if (expression.kind == ExpressionKind::Slice) {
        const auto value = lower_static_container_value(expression);
        return value
            ? std::optional<ContainerRegisterId> { value->value }
            : std::nullopt;
    }
    if (expression.kind == ExpressionKind::Call) {
        if (expression.text == "?:") {
            if (language_ != frontend::Language::SystemVerilog2017
                || expression.operands.size() != 3) {
                report(
                    "FSIM-ELAB-SVCOND-001",
                    "a container conditional requires one condition and two "
                    "container alternatives",
                    expression.span);
                return std::nullopt;
            }
            const auto* when_true_type = container_expression_type(expression.operands[1]);
            const auto* when_false_type = container_expression_type(expression.operands[2]);
            const auto when_true_runtime = container_expression_runtime_type(expression.operands[1]);
            const auto when_false_runtime = container_expression_runtime_type(expression.operands[2]);
            if (when_true_type == nullptr || when_false_type == nullptr
                || !when_true_runtime || !when_false_runtime
                || *when_true_runtime != *when_false_runtime) {
                report(
                    "FSIM-ELAB-SVCOND-002",
                    "container conditional alternatives require an exactly "
                    "compatible kind and profile",
                    expression.span);
                return std::nullopt;
            }
            if (when_true_runtime->associative) {
                report(
                    "FSIM-ELAB-SVCOND-003",
                    "associative-array conditional values are outside the "
                    "bounded read-only consumer subset",
                    expression.span);
                return std::nullopt;
            }
            const auto condition = lower_expression(expression.operands[0], 1);
            if (!condition || register_width(*condition) != 1) {
                report(
                    "FSIM-ELAB-SVCOND-004",
                    "a container conditional condition must produce one bit",
                    expression.operands[0].span);
                return std::nullopt;
            }
            const auto when_true = lower_container_expression(expression.operands[1]);
            const auto when_false = lower_container_expression(expression.operands[2]);
            if (!when_true || !when_false) {
                return std::nullopt;
            }
            const auto destination = allocate_container_register(*when_true_runtime);
            process_.operations.emplace_back(
                ConditionalContainerSelect {
                    destination, *condition, *when_true, *when_false });
            return destination;
        }
        return lower_user_container_function_expression(expression);
    }
    if (expression.kind != ExpressionKind::Identifier) {
        report(
            "FSIM-ELAB-SVCONTAINER-005",
            "container values currently require a direct object reference",
            expression.span);
        return std::nullopt;
    }
    if (const auto local = container_locals_.find(expression.text);
        local != container_locals_.end()) {
        return local->second;
    }
    const auto object = container_objects_.find(expression.text);
    const auto* type = object_type(expression.text);
    if (object == container_objects_.end() || type == nullptr) {
        report(
            "FSIM-ELAB-SVCONTAINER-006",
            "unknown container object '" + expression.text + "'",
            expression.span);
        return std::nullopt;
    }
    const auto runtime_type = container_type(*type, expression.span);
    if (!runtime_type) {
        return std::nullopt;
    }
    const auto destination = allocate_container_register(*runtime_type);
    const auto alias = std::find_if(
        design_.container_signal_aliases_.rbegin(),
        design_.container_signal_aliases_.rend(),
        [&](const ContainerSignalAlias& candidate) {
            return candidate.object == object->second
                && candidate.readable;
        });
    if (alias != design_.container_signal_aliases_.rend()) {
        implicit_signal_dependencies_.push_back(alias->signal);
    }
    process_.operations.emplace_back(
        ReadContainerObject { destination, object->second });
    return destination;
}

void Lowerer::lower_nonstatic_container_assignment(
    const ContainerRegisterId target,
    const Expression& expression,
    const ContainerType& destination_type)
{
    const bool plain_new = expression.kind == ExpressionKind::Index
        && expression.operands.size() == 2U
        && expression.operands[0].kind == ExpressionKind::Identifier
        && expression.operands[0].text == "new";
    const bool initialized_new = expression.kind == ExpressionKind::Call
        && expression.text == "@new-array";
    if (plain_new || initialized_new) {
        if (destination_type.associative || destination_type.fixed
            || destination_type.queue) {
            report(
                "FSIM-ELAB-SVCONTAINER-014",
                destination_type.fixed
                    ? "new[size] cannot resize a static array"
                    : destination_type.associative
                    ? "new[size] cannot resize an associative array"
                    : "new[size] cannot resize a queue",
                expression.span);
            return;
        }
        if (initialized_new && expression.operands.size() != 2U) {
            return;
        }
        const auto& size_expression = expression.operands[plain_new ? 1U : 0U];
        const auto size = lower_expression(
            size_expression,
            infer_width(size_expression).value_or(std::size_t { 32 }));
        if (!size) {
            return;
        }
        std::optional<ContainerRegisterId> initializer;
        if (initialized_new) {
            initializer = lower_container_expression(expression.operands[1]);
            if (!initializer
                || process_.container_register_types.at(*initializer)
                    != destination_type) {
                report(
                    "FSIM-ELAB-SVCONTAINER-023",
                    "new[size](initializer) requires an exactly compatible "
                    "dynamic-array value",
                    expression.operands[1].span);
                return;
            }
        }
        process_.operations.emplace_back(
            ResizeContainer { target, *size, initializer });
        return;
    }
    const auto value = lower_container_expression(expression);
    if (!value) {
        report(
            "FSIM-ELAB-SVCONTAINER-010",
            "whole-container assignment requires new[size], "
            "new[size](initializer), or a compatible container value",
            expression.span);
        return;
    }
    if (process_.container_register_types.at(*value) != destination_type) {
        report(
            expression.kind == ExpressionKind::Call
                ? "FSIM-ELAB-SVFUNC-008"
                : "FSIM-ELAB-SVCONTAINER-010",
            "whole-container assignment requires an exactly compatible kind "
            "and profile",
            expression.span);
        return;
    }
    process_.operations.emplace_back(
        CopyContainerRegister { target, *value });
}

Lowerer::ExpressionAttempt Lowerer::lower_container_query(
    const Expression& expression)
{
    if (expression.kind != ExpressionKind::Call) {
        return { };
    }
    const bool bound_query = expression.text == "$left"
        || expression.text == "$right"
        || expression.text == "$low"
        || expression.text == "$high"
        || expression.text == "$increment";
    const bool size_query = expression.text == "$size";
    const bool bits_query = expression.text == "$bits";
    const bool dimensions_query = expression.text == "$dimensions";
    const bool unpacked_dimensions_query = expression.text == "$unpacked_dimensions";
    if (!bound_query && !size_query && !bits_query
        && !dimensions_query
        && !unpacked_dimensions_query) {
        return { };
    }
    if (expression.operands.empty()) {
        return { };
    }
    if (!is_container_expression(
            expression.operands.front())
        && !is_static_container_slice_candidate(
            expression.operands.front())) {
        const auto& operand = expression.operands.front();
        if (operand.kind == ExpressionKind::Identifier
            && visible_type_mark(operand.text) != nullptr) {
            const auto* type = visible_type_mark(operand.text);
            if (type != nullptr && !type->packed_members.empty()) {
                const bool unpacked_aggregate = type->packed_aggregate
                        == frontend::PackedAggregateKind::UnpackedStruct
                    || type->packed_aggregate
                        == frontend::PackedAggregateKind::UnpackedUnion;
                const bool accepts_dimension = bound_query || size_query;
                if (language_ != frontend::Language::SystemVerilog2017
                    || expression.operands.size()
                        > (accepts_dimension ? 2U : 1U)) {
                    report(
                        "FSIM-ELAB-SVQUERY-004",
                        expression.text
                            + " aggregate type form has invalid arguments",
                        expression.span);
                    return std::nullopt;
                }
                if (expression.operands.size() == 2U) {
                    const auto dimension = constant_index(expression.operands[1]);
                    if (!dimension || *dimension != 1) {
                        report(
                            "FSIM-ELAB-SVQUERY-002",
                            expression.text
                                + " aggregate type form supports only packed dimension 1",
                            expression.operands[1].span);
                        return std::nullopt;
                    }
                }
                const auto width = unpacked_aggregate
                    ? aggregate_type_bits(*type)
                    : type->width();
                const auto range = type->packed_range;
                if (!width || *width == 0
                    || (!unpacked_aggregate && !range)
                    || *width
                        > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int32_t>::max())) {
                    report(
                        "FSIM-ELAB-SVQUERY-004",
                        expression.text
                            + " aggregate type has no representable packed layout",
                        operand.span);
                    return std::nullopt;
                }
                std::int64_t value = 0;
                if (bits_query) {
                    value = static_cast<std::int64_t>(*width);
                } else if (unpacked_aggregate) {
                    if (dimensions_query || unpacked_dimensions_query) {
                        value = 0;
                    } else {
                        report(
                            "FSIM-ELAB-SVQUERY-004",
                            expression.text
                                + " is not defined for an unpacked aggregate type",
                            operand.span);
                        return std::nullopt;
                    }
                } else if (size_query) {
                    value = static_cast<std::int64_t>(*width);
                } else if (dimensions_query) {
                    value = 1;
                } else if (unpacked_dimensions_query) {
                    value = 0;
                } else if (expression.text == "$left") {
                    value = range->left;
                } else if (expression.text == "$right") {
                    value = range->right;
                } else if (expression.text == "$low") {
                    value = std::min(range->left, range->right);
                } else if (expression.text == "$high") {
                    value = std::max(range->left, range->right);
                } else {
                    value = range->left >= range->right ? 1 : -1;
                }
                const auto destination = allocate_register(
                    32, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    destination,
                    unsigned_value(static_cast<std::uint32_t>(value), 32) });
                return destination;
            }
            report(
                "FSIM-ELAB-SVQUERY-004",
                expression.text
                    + " type-only forms are outside the bounded "
                      "container-query subset",
                operand.span);
            return std::nullopt;
        }
        return { };
    }
    const bool accepts_dimension = bound_query || size_query;
    const auto maximum_arguments = accepts_dimension ? std::size_t { 2 } : std::size_t { 1 };
    if (language_ != frontend::Language::SystemVerilog2017
        || expression.operands.size() > maximum_arguments) {
        report(
            "FSIM-ELAB-SVQUERY-001",
            expression.text
                + " requires a direct one-dimensional SystemVerilog "
                  "container object",
            expression.span);
        return std::nullopt;
    }
    std::optional<std::int64_t> requested_dimension { 1 };
    if (expression.operands.size() == 2) {
        requested_dimension = constant_index(expression.operands[1]);
        if (!requested_dimension) {
            std::string error;
            if (const auto value = evaluate_systemverilog_constant_expression(
                    expression.operands[1], { }, { }, error)) {
                requested_dimension = value->integer_value();
            }
        }
    }
    const auto& operand = expression.operands.front();
    const auto* source_type = container_expression_type(operand);
    const auto runtime_type = container_expression_runtime_type(operand);
    if (!source_type || !runtime_type) {
        report(
            "FSIM-ELAB-SVQUERY-001",
            expression.text
                + " requires a typed container object, slice, or function "
                  "result",
            operand.span);
        return std::nullopt;
    }
    const auto unpacked_dimensions = runtime_type->fixed && !runtime_type->dimensions.empty()
        ? runtime_type->dimensions.size()
        : std::size_t { 1 };
    if (!requested_dimension || *requested_dimension < 1
        || static_cast<std::uint64_t>(*requested_dimension)
            > unpacked_dimensions) {
        report(
            "FSIM-ELAB-SVQUERY-002",
            expression.text
                + " requires a constant unpacked dimension inside the "
                  "declared rank",
            expression.operands.size() == 2
                ? expression.operands[1].span
                : expression.span);
        return std::nullopt;
    }
    const auto constant_result =
        [&](const std::int64_t value) {
            const auto destination = allocate_register(
                32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                destination,
                unsigned_value(
                    static_cast<std::uint32_t>(value), 32) });
            return ExpressionAttempt { destination };
        };
    if (dimensions_query) {
        const bool packed_element = runtime_type->element_kind
                == ContainerElementKind::Packed
            || runtime_type->element_kind
                == ContainerElementKind::Scalar;
        return constant_result(
            static_cast<std::int64_t>(
                unpacked_dimensions + (packed_element ? 1U : 0U)));
    }
    if (unpacked_dimensions_query) {
        return constant_result(
            static_cast<std::int64_t>(unpacked_dimensions));
    }
    if (runtime_type->associative && bound_query) {
        report(
            "FSIM-ELAB-SVQUERY-003",
            expression.text
                + " has no finite bound for an associative array",
            operand.span);
        return std::nullopt;
    }
    if (runtime_type->fixed) {
        const auto dimension_index = static_cast<std::size_t>(
            *requested_dimension - 1);
        const auto selected_dimension = runtime_type->dimensions.empty()
            ? ContainerDimension {
                  runtime_type->index_left,
                  runtime_type->index_right
              }
            : runtime_type->dimensions[dimension_index];
        const auto left = static_cast<std::int64_t>(
            selected_dimension.first);
        const auto right = static_cast<std::int64_t>(
            selected_dimension.second);
        const auto count = left >= right
            ? left - right + 1
            : right - left + 1;
        if (bits_query) {
            const auto element_bits = container_element_bits(*runtime_type);
            if (!element_bits) {
                report(
                    "FSIM-ELAB-SVQUERY-004",
                    "$bits requires a recursively fixed-width container element",
                    operand.span);
                return std::nullopt;
            }
            std::uint64_t total = *element_bits;
            for (const auto& dimension : runtime_type->dimensions) {
                const auto extent = static_cast<std::uint64_t>(
                    std::abs(
                        static_cast<std::int64_t>(dimension.first)
                        - dimension.second)
                    + 1);
                if (extent != 0
                    && total
                        > static_cast<std::uint64_t>(
                              std::numeric_limits<std::int32_t>::max())
                            / extent) {
                    report(
                        "FSIM-ELAB-SVQUERY-004",
                        "$bits container result exceeds the 32-bit query range",
                        operand.span);
                    return std::nullopt;
                }
                total *= extent;
            }
            if (runtime_type->dimensions.empty()) {
                if (count < 0
                    || (count != 0
                        && total
                            > static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int32_t>::max())
                                / static_cast<std::uint64_t>(count))) {
                    report(
                        "FSIM-ELAB-SVQUERY-004",
                        "$bits container result exceeds the 32-bit query range",
                        operand.span);
                    return std::nullopt;
                }
                total *= static_cast<std::uint64_t>(count);
            }
            return constant_result(static_cast<std::int64_t>(total));
        }
        if (size_query) {
            return constant_result(count);
        }
        if (expression.text == "$left") {
            return constant_result(left);
        }
        if (expression.text == "$right") {
            return constant_result(right);
        }
        if (expression.text == "$low") {
            return constant_result(std::min(left, right));
        }
        if (expression.text == "$high") {
            return constant_result(std::max(left, right));
        }
        return constant_result(left >= right ? 1 : -1);
    }
    if (bound_query
        && (expression.text == "$left"
            || expression.text == "$low")) {
        return constant_result(0);
    }
    if (bound_query
        && expression.text == "$increment") {
        return constant_result(-1);
    }
    const auto source = lower_container_expression(operand);
    if (!source) {
        return std::nullopt;
    }
    const auto size = allocate_register(32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(
        ContainerSize { size, *source });
    if (size_query) {
        return size;
    }
    const auto factor = allocate_register(32, frontend::ValueDomain::Bit2);
    const auto element_bits = bits_query
        ? container_element_bits(*runtime_type)
        : std::optional<std::uint64_t> { 1U };
    if (!element_bits
        || *element_bits
            > static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max())) {
        report(
            "FSIM-ELAB-SVQUERY-004",
            "$bits requires a representable recursively fixed-width "
            "container element",
            operand.span);
        return std::nullopt;
    }
    process_.operations.emplace_back(LoadConstant {
        factor,
        unsigned_value(
            static_cast<std::uint32_t>(*element_bits),
            32) });
    const auto destination = allocate_register(32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary {
        bits_query
            ? BinaryOperator::multiply_unsigned
            : BinaryOperator::subtract_signed,
        destination,
        size,
        factor });
    return destination;
}

std::optional<ContainerRegisterId>
Lowerer::lower_container_pattern(
    const Expression& expression,
    const frontend::Type& source_type,
    const ContainerType& runtime_type)
{
    if (language_ != frontend::Language::SystemVerilog2017
        || expression.kind != ExpressionKind::Aggregate
        || expression.text != "sv-pattern"
        || expression.aggregate_choices.size()
            != expression.operands.size()
        || expression.aggregate_choice_expressions.size()
            != expression.operands.size()) {
        report(
            "FSIM-ELAB-SVPATTERN-001",
            "a container assignment pattern requires consistent "
            "SystemVerilog aggregate metadata",
            expression.span);
        return std::nullopt;
    }
    if (runtime_type.element_kind
        == ContainerElementKind::Aggregate) {
        return lower_unpacked_aggregate_pattern(
            expression, source_type, runtime_type);
    }
    const auto* source_element_type = &source_type;
    if (source_type.systemverilog_container
        && source_type.systemverilog_container->element_types.size() == 1) {
        source_element_type = &source_type.systemverilog_container->element_types.front();
    }
    const bool string_elements = runtime_type.element_kind == ContainerElementKind::String;
    if (runtime_type.fixed && runtime_type.dimensions.size() > 1) {
        return lower_multidimensional_container_pattern(
            expression, source_type, runtime_type);
    }
    bool has_positional = false;
    bool has_keyed = false;
    bool has_default = false;
    std::size_t default_count { };
    for (std::size_t member = 0;
        member < expression.aggregate_choices.size();
        ++member) {
        const auto& choice = expression.aggregate_choices[member];
        const auto& choice_expressions = expression.aggregate_choice_expressions[member];
        has_positional |= choice.empty();
        has_keyed |= choice == "@key";
        has_default |= choice == "default";
        default_count += choice == "default" ? 1U : 0U;
        if (!choice.empty() && choice != "@key"
            && choice != "default") {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "unknown container assignment-pattern association",
                expression.span);
            return std::nullopt;
        }
        const bool valid_choice_metadata = choice.empty()
            ? choice_expressions.empty()
            : choice == "@key"
            ? choice_expressions.size() == 1
            : choice_expressions.size() == 1
                && choice_expressions.front().kind
                    == ExpressionKind::DefaultChoice;
        if (!valid_choice_metadata) {
            report(
                "FSIM-ELAB-SVPATTERN-001",
                "a container assignment-pattern association has "
                "inconsistent choice metadata",
                expression.span);
            return std::nullopt;
        }
    }
    if (has_positional && (has_keyed || has_default)) {
        report(
            "FSIM-ELAB-SVPATTERN-004",
            "container assignment patterns cannot mix positional "
            "members with keyed or default members",
            expression.span);
        return std::nullopt;
    }
    if (has_default && !runtime_type.fixed) {
        report(
            "FSIM-ELAB-SVPATTERN-004",
            "default assignment-pattern members require a direct "
            "one-dimensional static-array target",
            expression.span);
        return std::nullopt;
    }
    if (runtime_type.fixed && (has_keyed || has_default)
        && default_count != 1) {
        report(
            "FSIM-ELAB-SVPATTERN-005",
            default_count == 0
                ? "a keyed static-array assignment pattern requires "
                  "exactly one default member"
                : "a static-array assignment pattern cannot contain "
                  "more than one default member",
            expression.span);
        return std::nullopt;
    }
    if (runtime_type.associative
            ? (!has_keyed && !expression.operands.empty())
            : (has_keyed && !runtime_type.fixed)) {
        report(
            "FSIM-ELAB-SVPATTERN-001",
            runtime_type.associative
                ? "associative-array assignment patterns require keyed "
                  "members"
                : "non-associative container patterns require positional "
                  "members",
            expression.span);
        return std::nullopt;
    }
    const auto count = expression.operands.size();
    const auto fixed_count = static_cast<std::uint64_t>(
                                 runtime_type.index_left >= runtime_type.index_right
                                     ? static_cast<std::int64_t>(runtime_type.index_left)
                                         - runtime_type.index_right
                                     : static_cast<std::int64_t>(runtime_type.index_right)
                                         - runtime_type.index_left)
        + 1U;
    const bool static_default_pattern = runtime_type.fixed && has_default;
    if ((runtime_type.fixed && !static_default_pattern
            && count != fixed_count)
        || count
            > maximum_container_elements(runtime_type)
                + (static_default_pattern ? 1U : 0U)
        || (runtime_type.maximum_elements
            && count > *runtime_type.maximum_elements)) {
        report(
            "FSIM-ELAB-SVPATTERN-002",
            runtime_type.fixed
                ? "a static-array assignment pattern must match the "
                  "specialized element count"
                : "an assignment pattern exceeds the bounded container "
                  "capacity",
            expression.span);
        return std::nullopt;
    }
    const auto destination = allocate_container_register(runtime_type);
    if (static_default_pattern) {
        struct ExplicitMember {
            std::int32_t index { };
            RegisterId value { };
        };
        std::optional<RegisterId> default_value;
        std::vector<ExplicitMember> explicit_members;
        std::set<std::int32_t> converted_keys;
        for (std::size_t member = 0; member < count; ++member) {
            const auto value = string_elements
                ? lower_string_expression(expression.operands[member])
                : lower_expression(
                      expression.operands[member],
                      runtime_type.element_width,
                      source_element_type);
            if (!value) {
                return std::nullopt;
            }
            if (expression.aggregate_choices[member] == "default") {
                default_value = *value;
                continue;
            }
            const auto& key_expression = expression.aggregate_choice_expressions[member].front();
            std::string error;
            const auto constant = evaluate_systemverilog_constant_expression(
                key_expression, { }, { }, error);
            if (!constant || !constant->known()) {
                report(
                    "FSIM-ELAB-SVPATTERN-006",
                    "static-array assignment-pattern keys must be locally "
                    "constant known integral values",
                    key_expression.span);
                return std::nullopt;
            }
            const auto converted_bits = static_cast<std::uint32_t>(constant->bits);
            const auto converted = converted_bits
                    <= static_cast<std::uint32_t>(
                        std::numeric_limits<std::int32_t>::max())
                ? static_cast<std::int64_t>(converted_bits)
                : static_cast<std::int64_t>(converted_bits)
                    - (INT64_C(1) << 32U);
            const auto low = std::min(
                runtime_type.index_left, runtime_type.index_right);
            const auto high = std::max(
                runtime_type.index_left, runtime_type.index_right);
            if (converted < low || converted > high) {
                report(
                    "FSIM-ELAB-SVPATTERN-007",
                    "a converted static-array assignment-pattern key is "
                    "outside the declared index range",
                    key_expression.span);
                return std::nullopt;
            }
            const auto declared_index = static_cast<std::int32_t>(converted);
            if (!converted_keys.insert(declared_index).second) {
                report(
                    "FSIM-ELAB-SVPATTERN-007",
                    "static-array assignment-pattern keys must be unique "
                    "after signed index conversion",
                    key_expression.span);
                return std::nullopt;
            }
            explicit_members.push_back(
                ExplicitMember { declared_index, *value });
        }
        if (!default_value) {
            report(
                "FSIM-ELAB-SVPATTERN-005",
                "a keyed static-array assignment pattern requires exactly "
                "one default member",
                expression.span);
            return std::nullopt;
        }
        for (std::uint64_t element = 0;
            element < fixed_count;
            ++element) {
            const auto step = runtime_type.index_left >= runtime_type.index_right
                ? -static_cast<std::int64_t>(element)
                : static_cast<std::int64_t>(element);
            const auto declared_index = static_cast<std::int32_t>(
                static_cast<std::int64_t>(
                    runtime_type.index_left)
                + step);
            if (converted_keys.contains(declared_index)) {
                continue;
            }
            const auto index = allocate_register(32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                index,
                unsigned_value(
                    static_cast<std::uint32_t>(declared_index), 32) });
            if (string_elements) {
                process_.operations.emplace_back(ContainerStringWrite {
                    destination, index, *default_value, true });
            } else {
                process_.operations.emplace_back(ContainerWrite {
                    destination, index, *default_value, true });
            }
        }
        for (const auto& member : explicit_members) {
            const auto index = allocate_register(32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                index,
                unsigned_value(
                    static_cast<std::uint32_t>(member.index), 32) });
            if (string_elements) {
                process_.operations.emplace_back(ContainerStringWrite {
                    destination, index, member.value, true });
            } else {
                process_.operations.emplace_back(ContainerWrite {
                    destination, index, member.value, true });
            }
        }
        return destination;
    }
    if (!runtime_type.fixed && !runtime_type.associative
        && !runtime_type.queue) {
        const auto size = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            size, unsigned_value(count, 32) });
        process_.operations.emplace_back(
            ResizeContainer { destination, size });
    }
    if (runtime_type.queue && string_elements) {
        const auto size = allocate_register(32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant {
            size, unsigned_value(count, 32) });
        process_.operations.emplace_back(
            ResizeContainer { destination, size, std::nullopt, true });
    }
    std::vector<PackedLogic4> keys;
    for (std::size_t element = 0; element < count; ++element) {
        if (runtime_type.queue && !string_elements) {
            const auto value = lower_expression(
                expression.operands[element],
                runtime_type.element_width,
                source_element_type);
            if (!value) {
                return std::nullopt;
            }
            process_.operations.emplace_back(
                PushContainer { destination, *value, false });
            continue;
        }
        RegisterId index { };
        bool signed_index = false;
        if (runtime_type.associative) {
            const auto& choices = expression.aggregate_choice_expressions[element];
            if (choices.size() != 1) {
                report(
                    "FSIM-ELAB-SVPATTERN-003",
                    "an associative assignment-pattern member requires one "
                    "locally constant key",
                    expression.span);
                return std::nullopt;
            }
            const auto* index_type = source_type.systemverilog_container
                                         ->associative_index_type.get();
            std::string error;
            const auto key = index_type
                ? evaluate_systemverilog_packed_constant(
                      choices.front(), *index_type, { }, { }, error)
                : std::nullopt;
            const bool unknown = key
                && (key->is_logic9()
                    || std::ranges::any_of(
                        key->bval_words(),
                        [](const std::uint64_t word) { return word != 0; }));
            if (!key || unknown) {
                report(
                    "FSIM-ELAB-SVPATTERN-003",
                    "associative assignment-pattern keys must be locally "
                    "constant known integral values",
                    choices.front().span);
                return std::nullopt;
            }
            if (std::ranges::find(keys, *key) != keys.end()) {
                report(
                    "FSIM-ELAB-SVPATTERN-003",
                    "associative assignment-pattern keys must be unique "
                    "after index-type conversion",
                    choices.front().span);
                return std::nullopt;
            }
            keys.push_back(*key);
            const auto lowered = lower_expression(
                choices.front(), runtime_type.index_width, index_type);
            if (!lowered) {
                return std::nullopt;
            }
            index = register_width(*lowered)
                    == runtime_type.index_width
                ? *lowered
                : resize_register(
                      *lowered,
                      runtime_type.index_width,
                      runtime_type.signed_indices);
            signed_index = runtime_type.signed_indices;
        } else {
            std::int64_t declared_index = static_cast<std::int64_t>(element);
            if (runtime_type.fixed) {
                const auto step = runtime_type.index_left >= runtime_type.index_right
                    ? -static_cast<std::int64_t>(element)
                    : static_cast<std::int64_t>(element);
                declared_index = static_cast<std::int64_t>(runtime_type.index_left)
                    + step;
                signed_index = true;
            }
            index = allocate_register(32, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant {
                index,
                unsigned_value(
                    static_cast<std::uint32_t>(declared_index), 32) });
        }
        const auto value = string_elements
            ? lower_string_expression(expression.operands[element])
            : lower_expression(
                  expression.operands[element],
                  runtime_type.element_width,
                  source_element_type);
        if (!value) {
            return std::nullopt;
        }
        if (string_elements) {
            process_.operations.emplace_back(ContainerStringWrite {
                destination, index, *value, signed_index });
        } else {
            process_.operations.emplace_back(ContainerWrite {
                destination, index, *value, signed_index });
        }
    }
    return destination;
}

std::optional<std::vector<ContainerPredicateNode>>
Lowerer::lower_container_expression_graph(
    const Expression& expression,
    const std::string_view iterator_name,
    const frontend::Type& source_type,
    const ContainerType& runtime_type,
    const ContainerExpressionPurpose purpose)
{
    using PredicateKind = ContainerPredicateValueKind;
    const bool transformation = purpose != ContainerExpressionPurpose::predicate;
    const bool ordering_key = purpose == ContainerExpressionPurpose::ordering_key;
    const bool locator_transformation = purpose == ContainerExpressionPurpose::locator_transformation;
    std::vector<ContainerPredicateNode> nodes;
    std::size_t conditional_count { };
    const auto unsupported_code = ordering_key
        ? "FSIM-ELAB-SVORDER-006"
        : locator_transformation
        ? "FSIM-ELAB-SVLOCATOR-006"
        : transformation
        ? "FSIM-ELAB-SVREDUCE-004"
        : "FSIM-ELAB-SVFIND-004";
    const auto reference_code = ordering_key
        ? "FSIM-ELAB-SVORDER-007"
        : locator_transformation
        ? "FSIM-ELAB-SVLOCATOR-007"
        : transformation
        ? "FSIM-ELAB-SVREDUCE-005"
        : "FSIM-ELAB-SVFIND-008";
    const auto expression_name = ordering_key
        ? std::string_view { "container ordering key expression" }
        : locator_transformation
        ? std::string_view { "container locator transformation" }
        : transformation
        ? std::string_view { "reduction transformation" }
        : std::string_view { "container locator predicate" };
    const auto append =
        [&](ContainerPredicateNode node)
        -> std::optional<std::uint32_t> {
        if (nodes.size() >= maximum_container_predicate_nodes) {
            report(
                unsupported_code,
                std::string { expression_name }
                    + " is limited to 64 nodes",
                expression.span);
            return std::nullopt;
        }
        nodes.push_back(std::move(node));
        return static_cast<std::uint32_t>(nodes.size() - 1U);
    };
    const auto direct_kind =
        [&](const Expression& candidate)
        -> std::optional<PredicateKind> {
        if (candidate.kind != ExpressionKind::Identifier) {
            return std::nullopt;
        }
        if (candidate.text == iterator_name) {
            return PredicateKind::element;
        }
        if (candidate.text
            == std::string { iterator_name } + ".index") {
            return PredicateKind::index;
        }
        return std::nullopt;
    };
    const auto contains_iterator =
        [&](const auto& self, const Expression& candidate) -> bool {
        return (candidate.kind == ExpressionKind::Identifier
                   && (candidate.text == iterator_name
                       || candidate.text.starts_with(
                           std::string { iterator_name } + ".")))
            || std::ranges::any_of(
                candidate.operands,
                [&](const auto& operand) {
                    return self(self, operand);
                });
    };
    const auto contains_identifier =
        [&](const auto& self, const Expression& candidate) -> bool {
        return candidate.kind == ExpressionKind::Identifier
            || std::ranges::any_of(
                candidate.operands,
                [&](const auto& operand) {
                    return self(self, operand);
                });
    };
    const auto contains_index_reference =
        [&](const auto& self, const Expression& candidate) -> bool {
        return (candidate.kind == ExpressionKind::Identifier
                   && (candidate.text
                           == std::string { iterator_name } + ".index"
                       || candidate.text.starts_with(
                           std::string { iterator_name }
                           + ".index.")))
            || (candidate.kind == ExpressionKind::Call
                && candidate.text == ".index"
                && contains_iterator(
                    contains_iterator, candidate))
            || std::ranges::any_of(
                candidate.operands,
                [&](const auto& operand) {
                    return self(self, operand);
                });
    };
    const auto report_invalid_reference =
        [&](const Expression& candidate,
            const std::string_view reason) {
            report(
                reference_code,
                "container iterator '" + std::string { iterator_name }
                    + "' " + std::string { reason },
                candidate.span);
        };
    const auto lower_constant =
        [&](const Expression& candidate,
            const PredicateKind value_kind)
        -> std::optional<std::uint32_t> {
        const frontend::Type index_type {
            frontend::ValueDomain::Integer, "int",
            std::nullopt, true
        };
        const auto& conversion_type = value_kind == PredicateKind::index
            ? index_type
            : source_type;
        const auto width = value_kind == PredicateKind::index
            ? 32U
            : runtime_type.element_width;
        std::string error;
        const auto value = evaluate_systemverilog_constant_expression(
            candidate, { }, { }, error);
        const auto converted = value
            ? convert_systemverilog_parameter_value(
                  *value, conversion_type, error)
            : std::nullopt;
        const auto literal = converted
            ? literal_value(
                  converted->expression(candidate.span),
                  width,
                  frontend::Language::SystemVerilog2017)
            : std::nullopt;
        if (!literal
            || literal->value.width() != width
            || ((value_kind == PredicateKind::index
                    || runtime_type.two_state)
                && std::ranges::any_of(
                    literal->value.bval_words(),
                    [](const auto word) { return word != 0; }))) {
            if (!value
                && contains_identifier(
                    contains_identifier, candidate)) {
                report_invalid_reference(
                    candidate,
                    "predicate contains an unknown iterator reference");
                return std::nullopt;
            }
            report(
                unsupported_code,
                std::string { expression_name }
                    + " constants must be locally "
                      "constant and convertible to the selected "
                    + std::string {
                        value_kind == PredicateKind::index
                            ? "signed 32-bit index"
                            : "element type" }
                    + (error.empty() ? std::string { } : ": " + error),
                candidate.span);
            return std::nullopt;
        }
        ContainerPredicateNode node;
        node.operation = ContainerPredicateOperator::constant;
        node.constant = literal->value;
        node.value_kind = value_kind;
        return append(std::move(node));
    };
    const auto lower_value =
        [&](const Expression& candidate,
            const PredicateKind expected_kind)
        -> std::optional<std::uint32_t> {
        if (const auto kind = direct_kind(candidate)) {
            if (*kind != expected_kind) {
                report_invalid_reference(
                    candidate,
                    "cannot mix element and index comparison operands");
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = *kind == PredicateKind::index
                ? ContainerPredicateOperator::index
                : ContainerPredicateOperator::item;
            node.value_kind = *kind;
            return append(std::move(node));
        }
        if (contains_iterator(contains_iterator, candidate)) {
            if (contains_index_reference(
                    contains_index_reference, candidate)) {
                report_invalid_reference(
                    candidate,
                    "supports only its direct value or direct .index leaf");
            } else {
                report(
                    unsupported_code,
                    std::string { expression_name }
                        + " accepts only the scoped '"
                        + std::string { iterator_name }
                        + "' iterator or locally constant operands",
                    candidate.span);
            }
            return std::nullopt;
        }
        if (candidate.kind == ExpressionKind::Identifier
            && candidate.text.find(".index")
                != std::string::npos) {
            report_invalid_reference(
                candidate,
                "predicate contains an unknown iterator reference");
            return std::nullopt;
        }
        return lower_constant(candidate, expected_kind);
    };
    std::function<std::optional<std::uint32_t>(
        const Expression&)>
        lower;
    lower =
        [&](const Expression& candidate)
        -> std::optional<std::uint32_t> {
        if (const auto kind = direct_kind(candidate)) {
            ContainerPredicateNode node;
            node.operation = *kind == PredicateKind::index
                ? ContainerPredicateOperator::index
                : ContainerPredicateOperator::item;
            node.value_kind = *kind;
            return append(std::move(node));
        }
        if (!contains_iterator(
                contains_iterator, candidate)) {
            if (candidate.kind == ExpressionKind::Identifier
                && candidate.text.find(".index")
                    != std::string::npos) {
                report_invalid_reference(
                    candidate,
                    "predicate contains an unknown iterator reference");
                return std::nullopt;
            }
            return lower_constant(
                candidate, PredicateKind::element);
        }
        if (candidate.kind == ExpressionKind::Unary
            && candidate.text == "!"
            && candidate.operands.size() == 1) {
            const auto operand = lower(candidate.operands.front());
            if (!operand) {
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = ContainerPredicateOperator::logical_not;
            node.left = *operand;
            node.value_kind = PredicateKind::logical;
            return append(std::move(node));
        }
        if (transformation
            && candidate.kind == ExpressionKind::Call
            && candidate.text == "?:"
            && candidate.operands.size() == 3) {
            if (++conditional_count > 1U) {
                report(
                    unsupported_code,
                    ordering_key
                        ? "container ordering key expressions permit one "
                          "conditional key selection"
                        : locator_transformation
                        ? "container locator transformations permit one "
                          "conditional key selection"
                        : "container reduction transformations permit one "
                          "conditional element selection",
                    candidate.span);
                return std::nullopt;
            }
            const auto condition = lower(candidate.operands[0]);
            const auto when_true = lower_value(
                candidate.operands[1],
                PredicateKind::element);
            const auto when_false = lower_value(
                candidate.operands[2],
                PredicateKind::element);
            if (!condition || !when_true || !when_false) {
                return std::nullopt;
            }
            ContainerPredicateNode node;
            node.operation = ContainerPredicateOperator::conditional;
            node.left = *condition;
            node.right = *when_true;
            node.third = *when_false;
            node.value_kind = PredicateKind::element;
            return append(std::move(node));
        }
        if (candidate.kind == ExpressionKind::Binary
            && candidate.operands.size() == 2) {
            const bool logical = candidate.text == "&&"
                || candidate.text == "||";
            const bool comparison = candidate.text == "=="
                || candidate.text == "!="
                || candidate.text == "<"
                || candidate.text == "<="
                || candidate.text == ">"
                || candidate.text == ">=";
            if (logical || comparison) {
                std::optional<std::uint32_t> left;
                std::optional<std::uint32_t> right;
                if (comparison) {
                    const auto left_kind = direct_kind(candidate.operands[0]);
                    const auto right_kind = direct_kind(candidate.operands[1]);
                    if (left_kind && right_kind
                        && *left_kind != *right_kind) {
                        report_invalid_reference(
                            candidate,
                            "cannot mix element and index comparison operands");
                        return std::nullopt;
                    }
                    const auto comparison_kind = left_kind.value_or(
                        right_kind.value_or(
                            PredicateKind::element));
                    left = lower_value(
                        candidate.operands[0], comparison_kind);
                    right = lower_value(
                        candidate.operands[1], comparison_kind);
                } else {
                    left = lower(candidate.operands[0]);
                    right = lower(candidate.operands[1]);
                }
                if (!left || !right) {
                    return std::nullopt;
                }
                ContainerPredicateNode node;
                node.left = *left;
                node.right = *right;
                node.value_kind = PredicateKind::logical;
                if (candidate.text == "==") {
                    node.operation = ContainerPredicateOperator::equal;
                } else if (candidate.text == "!=") {
                    node.operation = ContainerPredicateOperator::not_equal;
                } else if (candidate.text == "<") {
                    node.operation = ContainerPredicateOperator::less;
                } else if (candidate.text == "<=") {
                    node.operation = ContainerPredicateOperator::less_equal;
                } else if (candidate.text == ">") {
                    node.operation = ContainerPredicateOperator::greater;
                } else if (candidate.text == ">=") {
                    node.operation = ContainerPredicateOperator::greater_equal;
                } else if (candidate.text == "&&") {
                    node.operation = ContainerPredicateOperator::logical_and;
                } else {
                    node.operation = ContainerPredicateOperator::logical_or;
                }
                return append(std::move(node));
            }
        }
        report(
            unsupported_code,
            std::string { expression_name }
                + " supports the scoped '"
                + std::string { iterator_name }
                + "' iterator, its direct .index leaf, locally constant "
                  "operands, comparisons, logical &&, ||, and !"
                + (transformation
                        ? ", and one conditional element selection"
                        : ""),
            candidate.span);
        return std::nullopt;
    };
    const auto root = lower(expression);
    if (!root) {
        return std::nullopt;
    }
    if (transformation
        && nodes[*root].value_kind != PredicateKind::element) {
        report(
            ordering_key
                ? "FSIM-ELAB-SVORDER-008"
                : locator_transformation
                ? "FSIM-ELAB-SVLOCATOR-008"
                : "FSIM-ELAB-SVREDUCE-006",
            ordering_key
                ? "container ordering key root must have the receiver "
                  "element type"
                : locator_transformation
                ? "container locator transformation root must have the "
                  "receiver element type"
                : "container reduction transformation root must have the "
                  "receiver element type",
            expression.span);
        return std::nullopt;
    }
    return nodes;
}

bool Lowerer::lower_container_locator(
    const Expression& expression,
    const ContainerRegisterId destination,
    const ContainerType& destination_type)
{
    const bool predicate_locator = expression.text == ".find"
        || expression.text == ".find_index"
        || expression.text == ".find_first"
        || expression.text == ".find_first_index"
        || expression.text == ".find_last"
        || expression.text == ".find_last_index";
    const bool has_transformation = !predicate_locator
        && expression.operands.size() >= 2U;
    if (language_ != frontend::Language::SystemVerilog2017
        || expression.operands.empty()
        || (!is_container_expression(
                expression.operands.front())
            && !is_static_container_slice_candidate(
                expression.operands.front()))) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-001"
                : "FSIM-ELAB-SVLOCATOR-001",
            "container locators require a direct supported "
            "SystemVerilog unpacked-container receiver",
            expression.span);
        return false;
    }
    if ((!predicate_locator
            && (expression.operands.size() < 1U
                || expression.operands.size() > 3U))
        || (predicate_locator
            && expression.operands.size() != 2U
            && expression.operands.size() != 3U)) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-002"
                : "FSIM-ELAB-SVLOCATOR-002",
            predicate_locator
                ? "predicate container locator methods require exactly "
                  "one with-clause predicate"
                : "bounded container locator methods take no value "
                  "arguments and retain at most one iterator plus one "
                  "with-clause transformation",
            expression.span);
        return false;
    }
    const bool explicit_iterator = expression.operands.size() == 3U;
    if (explicit_iterator
        && expression.operands[1].kind
            != ExpressionKind::Identifier) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-007"
                : "FSIM-ELAB-SVLOCATOR-008",
            "a named container locator iterator must be one identifier",
            expression.operands[1].span);
        return false;
    }
    const std::string_view iterator_name = explicit_iterator
        ? std::string_view { expression.operands[1].text }
        : std::string_view { "item" };
    const auto& receiver = expression.operands.front();
    const auto source = lower_container_expression(receiver);
    const auto* source_frontend_type = container_expression_type(receiver);
    const auto source_type = container_expression_runtime_type(receiver);
    if (!source || !source_type) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-001"
                : "FSIM-ELAB-SVLOCATOR-001",
            "bounded container locators do not support associative "
            "or unresolved receivers",
            expression.span);
        return false;
    }
    if (source_type->associative) {
        report(
            has_transformation
                ? "FSIM-ELAB-SVLOCATOR-008"
                : predicate_locator
                ? "FSIM-ELAB-SVFIND-001"
                : "FSIM-ELAB-SVLOCATOR-001",
            "bounded container locators do not support associative "
            "receivers",
            expression.span);
        return false;
    }
    if (source_type->element_kind != ContainerElementKind::Packed) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-008"
                : "FSIM-ELAB-SVLOCATOR-008",
            "container locators require packed integral elements",
            expression.span);
        return false;
    }
    const auto iterator_key = std::string { iterator_name };
    const bool iterator_collision = explicit_iterator
        && (object_type(iterator_name) != nullptr
            || locals_.contains(iterator_key)
            || string_locals_.contains(iterator_key)
            || container_locals_.contains(iterator_key)
            || signals_.contains(iterator_key)
            || string_objects_.contains(iterator_key)
            || container_objects_.contains(iterator_key));
    if (iterator_collision) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-007"
                : "FSIM-ELAB-SVLOCATOR-008",
            "named container locator iterator '" + std::string { iterator_name }
                + "' collides with a visible object",
            expression.operands[1].span);
        return false;
    }
    const bool index_result = expression.text == ".unique_index"
        || expression.text == ".find_index"
        || expression.text == ".find_first_index"
        || expression.text == ".find_last_index";
    const bool compatible = destination_type.queue
        && !destination_type.associative
        && !destination_type.fixed
        && (index_result
                ? destination_type.element_width == 32
                    && destination_type.two_state
                    && destination_type.signed_elements
                : destination_type.element_width
                        == source_type->element_width
                    && destination_type.two_state
                        == source_type->two_state
                    && destination_type.signed_elements
                        == source_type->signed_elements);
    if (!compatible) {
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-003"
                : "FSIM-ELAB-SVLOCATOR-003",
            "container locator result requires a compatible queue target",
            expression.span);
        return false;
    }
    auto operation = ContainerLocatorOperator::minimum;
    if (expression.text == ".max") {
        operation = ContainerLocatorOperator::maximum;
    } else if (expression.text == ".unique") {
        operation = ContainerLocatorOperator::unique;
    } else if (index_result) {
        operation = ContainerLocatorOperator::unique_index;
    }
    if (expression.text == ".find") {
        operation = ContainerLocatorOperator::find;
    } else if (expression.text == ".find_index") {
        operation = ContainerLocatorOperator::find_index;
    } else if (expression.text == ".find_first") {
        operation = ContainerLocatorOperator::find_first;
    } else if (expression.text == ".find_first_index") {
        operation = ContainerLocatorOperator::find_first_index;
    } else if (expression.text == ".find_last") {
        operation = ContainerLocatorOperator::find_last;
    } else if (expression.text == ".find_last_index") {
        operation = ContainerLocatorOperator::find_last_index;
    }
    std::vector<ContainerPredicateNode> predicate;
    std::vector<ContainerPredicateNode> transformation;
    if (predicate_locator) {
        const auto& predicate_expression = expression.operands[explicit_iterator ? 2U : 1U];
        const auto lowered = lower_container_expression_graph(
            predicate_expression, iterator_name,
            *source_frontend_type,
            *source_type,
            ContainerExpressionPurpose::predicate);
        if (!lowered) {
            return false;
        }
        predicate = std::move(*lowered);
    } else if (has_transformation) {
        const auto& transformation_expression = expression.operands[explicit_iterator ? 2U : 1U];
        const auto lowered = lower_container_expression_graph(
            transformation_expression, iterator_name,
            *source_frontend_type, *source_type,
            ContainerExpressionPurpose::locator_transformation);
        if (!lowered) {
            return false;
        }
        transformation = std::move(*lowered);
    }
    process_.operations.emplace_back(
        LocateContainer {
            operation, destination, *source,
            std::move(predicate),
            std::move(transformation) });
    return true;
}

void Lowerer::lower_container_method(
    const Statement& statement)
{
    if (lower_synchronization_method_statement(statement)) {
        return;
    }
    if (lower_process_method_statement(statement)) {
        return;
    }
    if (lower_string_method_statement(statement)) {
        return;
    }
    const auto& call = statement.value;
    constexpr std::string_view class_container_method_prefix {
        "@sv-container-method:"
    };
    if (call.kind == ExpressionKind::Call
        && call.text.starts_with(class_container_method_prefix)) {
        constexpr std::string_view push_back { "push_back:" };
        const auto operation = std::string_view { call.text }.substr(
            class_container_method_prefix.size());
        if (!operation.starts_with(push_back)
            || call.operands.size() != 2U) {
            report(
                "FSIM-ELAB-SVCLASS-015",
                "class handle container statement requires push_back(value)",
                statement.span);
            return;
        }
        const auto receiver = lower_expression(call.operands[0], 64);
        const auto value = lower_expression(call.operands[1], 64);
        if (!receiver || !value)
            return;
        const auto destination = allocate_register(
            64, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(ClassMethodCall {
            destination,
            *receiver,
            "@container-push-back:"
                + std::string { operation.substr(push_back.size()) },
            { *value },
            { "" },
            { static_cast<std::uint8_t>(frontend::PortDirection::Input) },
            64,
            false });
        return;
    }
    const bool ordering_method = call.kind == ExpressionKind::Call
        && (call.text == ".reverse"
            || call.text == ".sort"
            || call.text == ".rsort"
            || call.text == ".shuffle");
    const bool ordering_slice_receiver = ordering_method
        && !call.operands.empty()
        && is_static_container_slice_candidate(
            call.operands.front());
    const bool locator_method = call.kind == ExpressionKind::Call
        && (call.text == ".min"
            || call.text == ".max"
            || call.text == ".unique"
            || call.text == ".unique_index"
            || call.text == ".find"
            || call.text == ".find_index"
            || call.text == ".find_first"
            || call.text == ".find_first_index"
            || call.text == ".find_last"
            || call.text == ".find_last_index");
    if (locator_method) {
        const bool predicate_locator = call.text.starts_with(".find");
        report(
            predicate_locator
                ? "FSIM-ELAB-SVFIND-005"
                : "FSIM-ELAB-SVLOCATOR-005",
            "container locator results cannot be discarded",
            call.span);
        return;
    }
    if (ordering_method) {
        if (language_
                != frontend::Language::SystemVerilog2017
            || call.operands.empty()
            || (!ordering_slice_receiver
                && (call.operands.front().kind
                        != ExpressionKind::Identifier
                    || !is_container_expression(
                        call.operands.front())))) {
            report(
                "FSIM-ELAB-SVORDER-001",
                "container ordering requires a direct writable "
                "SystemVerilog unpacked-container receiver",
                call.span);
            return;
        }
        const bool ordering_key_method = call.text == ".sort"
            || call.text == ".rsort";
        const bool valid_arity = ordering_key_method
            ? call.operands.size() >= 1
                && call.operands.size() <= 3
            : call.operands.size() == 1;
        if (!valid_arity) {
            report(
                "FSIM-ELAB-SVORDER-002",
                ordering_key_method
                    ? "sort and rsort take no value arguments and retain "
                      "at most one iterator plus one with-clause key"
                    : "reverse and shuffle take no arguments",
                call.span);
            return;
        }
        if (call.operands.size() == 3
            && call.operands[1].kind
                != ExpressionKind::Identifier) {
            report(
                "FSIM-ELAB-SVORDER-008",
                "a named container ordering iterator must be one identifier",
                call.operands[1].span);
            return;
        }
    }
    if (call.kind == ExpressionKind::Call
        && (call.text == ".sum"
            || call.text == ".product"
            || call.text == ".and"
            || call.text == ".or"
            || call.text == ".xor")) {
        report(
            "FSIM-ELAB-SVREDUCE-003",
            "a container reduction result must be used in an expression",
            call.span);
        return;
    }
    if (call.kind == ExpressionKind::Call
        && (call.text == ".exists"
            || call.text == ".first"
            || call.text == ".last"
            || call.text == ".next"
            || call.text == ".prev")) {
        (void)lower_expression(call, 32);
        return;
    }
    if (call.kind != ExpressionKind::Call
        || call.operands.empty()
        || (call.operands.front().kind
                != ExpressionKind::Identifier
            && !ordering_slice_receiver)) {
        report(
            "FSIM-ELAB-SVCONTAINER-007",
            "container method requires a direct object receiver",
            statement.span);
        return;
    }
    const auto& receiver = call.operands.front();
    const auto& receiver_base = ordering_slice_receiver
        ? receiver.operands.front()
        : receiver;
    const auto mutates_receiver = call.text == ".delete"
        || call.text == ".insert"
        || call.text == ".push_front"
        || call.text == ".push_back"
        || call.text == ".pop_front"
        || call.text == ".pop_back"
        || ordering_method;
    if (mutates_receiver
        && read_only_container_objects_.contains(
            receiver_base.text)) {
        report(
            "FSIM-ELAB-SVPORT-009",
            "an input container port is read-only within its module",
            receiver_base.span);
        return;
    }
    const auto target = lower_container_expression(receiver_base);
    const auto* type = object_type(receiver_base.text);
    if (!target || type == nullptr) {
        return;
    }
    const auto runtime_type = container_type(*type, call.span);
    if (!runtime_type) {
        return;
    }
    if (ordering_method
        && runtime_type->element_kind
            != ContainerElementKind::Packed) {
        report(
            "FSIM-ELAB-SVORDER-008",
            "container ordering requires packed integral elements",
            call.span);
        return;
    }
    const auto width = type->width();
    if (!width) {
        return;
    }
    if (call.text == ".delete") {
        if (call.operands.size() == 2) {
            if (!runtime_type->associative && !runtime_type->queue) {
                report(
                    "FSIM-ELAB-SVCONTAINER-018",
                    "delete(index) requires a queue or associative-array receiver",
                    call.span);
                return;
            }
            const bool string_index = runtime_type->associative
                && runtime_type->string_indices;
            const auto index = string_index
                ? lower_string_expression(call.operands[1])
                : lower_expression(
                      call.operands[1],
                      runtime_type->associative
                          ? runtime_type->index_width
                          : 32U,
                      runtime_type->associative
                          ? type->systemverilog_container
                                ->associative_index_type.get()
                          : nullptr);
            if (!index) {
                return;
            }
            process_.operations.emplace_back(
                DeleteContainer { *target, *index, string_index });
        } else {
            if (runtime_type->fixed) {
                report(
                    "FSIM-ELAB-SVCONTAINER-021",
                    "delete() cannot clear a static unpacked array",
                    call.span);
                return;
            }
            process_.operations.emplace_back(
                DeleteContainer { *target, std::nullopt });
        }
    } else if (call.text == ".insert") {
        if (!runtime_type->queue) {
            report(
                "FSIM-ELAB-SVCONTAINER-019",
                "insert requires a queue receiver", call.span);
            return;
        }
        if (call.operands.size() != 3U) {
            return;
        }
        const auto index = lower_expression(call.operands[1], 32);
        const auto value = lower_expression(call.operands[2], *width, type);
        if (!index || !value) {
            return;
        }
        process_.operations.emplace_back(
            PushContainer { *target, *value, false, *index });
    } else if (
        call.text == ".push_front"
        || call.text == ".push_back") {
        if (!runtime_type->queue) {
            report(
                "FSIM-ELAB-SVCONTAINER-019",
                "push_front/push_back require a queue receiver",
                call.span);
            return;
        }
        if (call.operands.size() != 2) {
            return;
        }
        const auto value = lower_expression(call.operands[1], *width, type);
        if (!value) {
            return;
        }
        process_.operations.emplace_back(
            PushContainer {
                *target, *value,
                call.text == ".push_front" });
    } else if (
        call.text == ".pop_front"
        || call.text == ".pop_back") {
        if (!runtime_type->queue) {
            report(
                "FSIM-ELAB-SVCONTAINER-019",
                "pop_front/pop_back require a queue receiver",
                call.span);
            return;
        }
        const auto discarded = allocate_register(*width, type->domain);
        process_.operations.emplace_back(
            PopContainer {
                discarded, *target,
                call.text == ".pop_front" });
    } else if (ordering_method) {
        auto ordering_target = *target;
        auto ordering_type = *runtime_type;
        if (ordering_slice_receiver) {
            const auto selection = static_container_slice(receiver);
            if (!selection) {
                return;
            }
            ordering_type = selection->selected_type;
            ordering_target = allocate_container_register(ordering_type);
            copy_static_container_ordinals(
                ordering_target,
                ordering_type,
                *target,
                ordering_type);
        }
        if (ordering_type.associative) {
            report(
                "FSIM-ELAB-SVORDER-003",
                "container ordering does not support associative arrays",
                call.span);
            return;
        }
        const bool explicit_iterator = call.operands.size() == 3;
        const bool has_key = call.operands.size() >= 2;
        const std::string_view iterator_name = explicit_iterator
            ? std::string_view { call.operands[1].text }
            : std::string_view { "item" };
        const auto iterator_key = std::string { iterator_name };
        const bool iterator_collision = explicit_iterator
            && (object_type(iterator_name) != nullptr
                || locals_.contains(iterator_key)
                || string_locals_.contains(iterator_key)
                || container_locals_.contains(iterator_key)
                || signals_.contains(iterator_key)
                || string_objects_.contains(iterator_key)
                || container_objects_.contains(iterator_key));
        if (iterator_collision) {
            report(
                "FSIM-ELAB-SVORDER-008",
                "named container ordering iterator '"
                    + std::string { iterator_name }
                    + "' collides with a visible object",
                call.operands[1].span);
            return;
        }
        std::vector<ContainerPredicateNode> key;
        if (has_key) {
            const auto& key_expression = call.operands[explicit_iterator ? 2U : 1U];
            const auto lowered = lower_container_expression_graph(
                key_expression, iterator_name,
                *type, ordering_type,
                ContainerExpressionPurpose::ordering_key);
            if (!lowered) {
                return;
            }
            key = std::move(*lowered);
        }
        auto operation = ContainerOrderingOperator::reverse;
        if (call.text == ".sort") {
            operation = ContainerOrderingOperator::ascending;
        } else if (call.text == ".rsort") {
            operation = ContainerOrderingOperator::descending;
        } else if (call.text == ".shuffle") {
            operation = ContainerOrderingOperator::shuffle;
        }
        process_.operations.emplace_back(
            OrderContainer {
                operation, ordering_target, std::move(key) });
        if (ordering_slice_receiver) {
            const auto replacement = allocate_container_register(*runtime_type);
            process_.operations.emplace_back(
                CopyContainerRegister { replacement, *target });
            copy_static_container_ordinals(
                replacement,
                ordering_type,
                ordering_target,
                ordering_type);
            process_.operations.emplace_back(
                CopyContainerRegister { *target, replacement });
        }
    } else {
        report(
            "FSIM-ELAB-SVCONTAINER-008",
            "unsupported container method '" + call.text + "'",
            call.span);
        return;
    }
    if (const auto object = container_objects_.find(receiver_base.text);
        object != container_objects_.end()) {
        process_.operations.emplace_back(
            WriteContainerObject { object->second, *target, std::nullopt });
    }
}

} // namespace fsim::elaboration
