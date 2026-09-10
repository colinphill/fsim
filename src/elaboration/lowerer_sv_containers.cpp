// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"

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
    if (language_ == frontend::Language::Vhdl2008
        && vhdl_standard_ >= frontend::VhdlStandard::Vhdl2019) {
        const auto api = frontend::vhdl_simulator_api(expression.text);
        if (api == frontend::VhdlSimulatorApi::get_call_path) {
            if ((expression.kind != ExpressionKind::Identifier
                    && expression.kind != ExpressionKind::Call)
                || !expression.operands.empty()) {
                report(
                    "FSIM-ELAB-VHENV-003",
                    "STD.ENV GET_CALL_PATH does not accept arguments",
                    expression.span);
                return std::nullopt;
            }
            const auto type =
                frontend::vhdl_environment_call_path_vector_ptr_type();
            const auto runtime_type = container_type(type, expression.span);
            if (!runtime_type) {
                return std::nullopt;
            }
            const auto destination = allocate_container_register(*runtime_type);
            process_.operations.emplace_back(VhdlEnvironmentGetCallPath {
                destination,
                SourceLocation {
                    expression.span.source_name.str(),
                    static_cast<std::uint32_t>(expression.span.begin.line),
                    static_cast<std::uint32_t>(expression.span.begin.column) },
                debug_scope_name() });
            return destination;
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text == "@vhdl-dereference"
            && expression.operands.size() == 1U) {
            const auto source_type = vhdl_expression_type(
                expression.operands.front());
            if (source_type
                && frontend::is_vhdl_environment_call_path_type(
                    *source_type)) {
                return lower_container_expression(
                    expression.operands.front());
            }
        }
    }
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

} // namespace fsim::elaboration
