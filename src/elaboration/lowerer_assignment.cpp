// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"
#include "fsim/runtime/systemverilog_string.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

void Lowerer::validate_read_only_signal_writes(
    const frontend::SourceSpan& source)
{
    if (process_.name.find("$port_input_driver")
        != std::string::npos) {
        return;
    }
    std::set<SignalId> reported;
    const auto check = [&](const SignalId signal) {
        if (read_only_signals_.contains(signal)
            && reported.insert(signal).second) {
            report(
                "FSIM-ELAB-SVIFACE-006",
                "an input signal is read-only within process '"
                    + process_.name + "'",
                source);
        }
    };
    for (const auto& operation : process_.operations) {
        fsim::runtime::simir::visit_operation(
            [&](const auto& candidate) {
                using Operation = std::decay_t<decltype(candidate)>;
                if constexpr (
                    std::is_same_v<Operation, WriteBlocking>
                    || std::is_same_v<Operation, WriteUpdate>
                    || std::is_same_v<Operation, WriteAfter>
                    || std::is_same_v<Operation, WriteInertial>
                    || std::is_same_v<Operation, WriteProjected>
                    || std::is_same_v<Operation, WriteProjectedWaveform>
                    || std::is_same_v<Operation, WriteBlockingSlice>
                    || std::is_same_v<Operation, WriteUpdateSlice>
                    || std::is_same_v<Operation, WriteAfterSlice>
                    || std::is_same_v<Operation, WriteInertialSlice>
                    || std::is_same_v<Operation, WriteProjectedSlice>
                    || std::is_same_v<Operation, WriteProjectedWaveformSlice>
                    || std::is_same_v<Operation, ForceSignalSlice>) {
                    check(candidate.signal);
                }
            },
            operation);
    }
}

bool Lowerer::validate_vhdl_mode_view_write(
    const SignalId signal,
    const Expression& target,
    const frontend::SourceSpan& source)
{
    if (language_ != frontend::Language::Vhdl2008
        || signal >= design_.signal_info_.size()) {
        return true;
    }
    constexpr std::string_view member_prefix { "@vhdl-member:" };
    const auto target_path = [&](const auto& self,
                                 const Expression& expression)
        -> std::optional<std::string> {
        if (expression.kind == ExpressionKind::Identifier) {
            return expression.text;
        }
        if ((expression.kind == ExpressionKind::Index
                || expression.kind == ExpressionKind::Slice)
            && !expression.operands.empty()) {
            return self(self, expression.operands.front());
        }
        if (expression.kind == ExpressionKind::Call
            && expression.text.starts_with(member_prefix)
            && expression.operands.size() == 1U) {
            auto base = self(self, expression.operands.front());
            if (!base) {
                return std::nullopt;
            }
            *base += '.';
            *base += expression.text.substr(member_prefix.size());
            return base;
        }
        return std::nullopt;
    };
    auto relative = target_path(target_path, target);
    if (!relative) {
        return true;
    }
    auto normalized = [](const std::string_view path) {
        std::string result;
        result.reserve(path.size());
        std::size_t index = 0U;
        while (index < path.size()) {
            if (path[index] != '(') {
                result += path[index++];
                continue;
            }
            const auto close = path.find(')', index + 1U);
            if (close == std::string_view::npos) {
                result.append(path.substr(index));
                break;
            }
            index = close + 1U;
        }
        return result;
    };
    const auto is_prefix = [](const std::string_view prefix,
                              const std::string_view value) {
        return value == prefix
            || (value.size() > prefix.size()
                && value.starts_with(prefix)
                && value[prefix.size()] == '.');
    };
    const auto full_target = relative->starts_with(hierarchy_ + ".")
        ? *relative
        : hierarchy_ + "." + *relative;
    const auto normalized_target = normalized(full_target);
    for (const auto& binding :
         design_.signal_info_[signal].vhdl_mode_view_bindings) {
        for (const auto& endpoint : binding.elements) {
            const auto normalized_endpoint = normalized(endpoint.formal_path);
            if (!is_prefix(normalized_target, normalized_endpoint)
                && !is_prefix(normalized_endpoint, normalized_target)) {
                continue;
            }
            if (endpoint.direction == frontend::PortDirection::Input) {
                report(
                    "FSIM-ELAB-VHVIEW-007",
                    "VHDL mode-view input endpoint '"
                        + endpoint.formal_path
                        + "' is read-only within instance '"
                        + hierarchy_ + "'",
                    source);
                return false;
            }
        }
    }
    return true;
}

std::optional<std::int64_t>
Lowerer::static_integer_value(const Expression& expression)
{
    if (language_ != frontend::Language::Vhdl2008) {
        if (const auto direct = constant_index(expression)) {
            return direct;
        }
        std::string error;
        const auto evaluated =
            evaluate_systemverilog_constant_expression(
                expression, { }, { }, error);
        return evaluated ? evaluated->integer_value()
                         : std::optional<std::int64_t> { };
    }
    const auto side_effect_free =
        [&](const auto& self, const Expression& candidate) -> bool {
        for (const auto& operand : candidate.operands) {
            if (!self(self, operand)) {
                return false;
            }
        }
        for (const auto& choices :
             candidate.aggregate_choice_expressions) {
            for (const auto& choice : choices) {
                if (!self(self, choice)) {
                    return false;
                }
            }
        }
        if (candidate.kind != ExpressionKind::Call
            && candidate.kind != ExpressionKind::Binary) {
            return true;
        }
        const auto separator = candidate.text.find_last_of('.');
        const auto intrinsic = std::string_view { candidate.text }.substr(
            separator == std::string::npos ? 0 : separator + 1);
        if (candidate.kind == ExpressionKind::Call
            && (candidate.text.starts_with("@")
                || candidate.text.starts_with("'")
                || visible_type_mark(candidate.text) != nullptr
                || object_type(candidate.text) != nullptr
                || intrinsic == "to_integer"
                || intrinsic == "to_unsigned"
                || intrinsic == "to_signed"
                || intrinsic == "resize"
                || intrinsic == "unsigned"
                || intrinsic == "signed"
                || intrinsic == "std_logic_vector"
                || intrinsic == "std_ulogic_vector"
                || intrinsic == "bit_vector")) {
            return true;
        }
        const auto found = function_indices_.find(candidate.text);
        if (found == function_indices_.end()) {
            // Calls not represented by a visible function frame are
            // conversions, attributes, or lowerer intrinsics. They neither
            // access impure VHDL state nor publish observable side effects.
            return candidate.kind != ExpressionKind::Call;
        }
        return std::ranges::all_of(
            found->second,
            [&](const std::size_t index) {
                return function_frames_[index].source->pure;
            });
    };
    if (expression.kind == ExpressionKind::Binary
        && expression.operands.size() == 2
        && (expression.text == "and"
            || expression.text == "nand"
            || expression.text == "or"
            || expression.text == "nor")) {
        const auto& left_expression = expression.operands[0];
        const auto& right_expression = expression.operands[1];
        const auto left = static_integer_value(left_expression);
        const bool and_family = expression.text == "and"
            || expression.text == "nand";
        const bool invert = expression.text == "nand"
            || expression.text == "nor";
        if (left
            && ((*left == 0) == and_family)
            && side_effect_free(side_effect_free, right_expression)) {
            const auto decisive = and_family ? 0 : 1;
            return invert ? 1 - decisive : decisive;
        }
        const auto right = static_integer_value(right_expression);
        if (right
            && ((*right == 0) == and_family)
            && side_effect_free(side_effect_free, left_expression)) {
            const auto decisive = and_family ? 0 : 1;
            return invert ? 1 - decisive : decisive;
        }
    }
    auto folded = expression;
    const auto fold_attributes =
        [&](const auto& self,
            Expression& candidate) -> bool {
        for (auto& operand : candidate.operands) {
            if (!self(self, operand)) {
                return false;
            }
        }
        if (candidate.kind != ExpressionKind::Call
            || (candidate.text != "'left"
                && candidate.text != "'right"
                && candidate.text != "'low"
                && candidate.text != "'high"
                && candidate.text != "'length"
                && candidate.text != "'ascending")
            || vhdl_array_attribute_prefix_type(candidate)
                == nullptr) {
            return true;
        }
        const auto span = candidate.span;
        const auto range = vhdl_array_attribute_range(candidate, false);
        if (!range) {
            return false;
        }
        std::int64_t value = 0;
        if (candidate.text == "'left") {
            value = range->left;
        } else if (candidate.text == "'right") {
            value = range->right;
        } else if (candidate.text == "'low") {
            value = std::min(
                range->left, range->right);
        } else if (candidate.text == "'high") {
            value = std::max(
                range->left, range->right);
        } else if (candidate.text == "'ascending") {
            value = range->descending ? 0 : 1;
        } else {
            const auto* type = vhdl_array_attribute_prefix_type(candidate);
            const auto dimension = candidate.operands.size() == 2
                ? static_integer_value(candidate.operands[1]).value_or(1)
                : std::int64_t { 1 };
            const bool null_array = type != nullptr && type->vhdl_array
                && dimension > 0
                && static_cast<std::uint64_t>(dimension)
                    <= type->vhdl_array->dimensions.size()
                && type->vhdl_array->dimensions[static_cast<std::size_t>(dimension - 1)].null;
            const auto width = null_array ? std::uint64_t { 0 } : range->width();
            if (width
                > static_cast<std::uint64_t>(
                    std::numeric_limits<
                        std::int64_t>::max())) {
                return false;
            }
            value = static_cast<std::int64_t>(width);
        }
        candidate = constant_expression(
            value,
            span,
            frontend::ValueDomain::Integer,
            frontend::Language::Vhdl2008);
        return true;
    };
    if (!fold_attributes(
            fold_attributes, folded)) {
        return std::nullopt;
    }
    return constant_index(folded);
}

void Lowerer::lower_assignment(const Statement& statement)
{
    const auto normalize_aggregate_target =
        [&](const auto& self, Expression& target) -> bool {
        if (target.kind == ExpressionKind::Call
            && (target.text == "@stream-left"
                || target.text == "@stream-right")) {
            report(
                "FSIM-ELAB-SVSTREAM-001",
                "a streaming assignment target cannot be nested inside another target",
                target.span);
            return false;
        }
        if (target.kind == ExpressionKind::Aggregate
            && target.text == "sv-pattern") {
            if (language_ != frontend::Language::SystemVerilog2017
                || target.operands.empty()
                || target.aggregate_choices.size() != target.operands.size()
                || target.aggregate_choice_expressions.size()
                    != target.operands.size()
                || std::ranges::any_of(
                    target.aggregate_choices,
                    [](const std::string& choice) {
                        return !choice.empty();
                    })
                || std::ranges::any_of(
                    target.aggregate_choice_expressions,
                    [](const auto& choices) { return !choices.empty(); })) {
                report(
                    "FSIM-ELAB-SVASSIGN-002",
                    "an assignment-pattern target requires one or more positional lvalues",
                    target.span);
                return false;
            }
            target.kind = ExpressionKind::Concatenation;
            target.text = "concat";
            target.aggregate_choices.clear();
            target.aggregate_choice_expressions.clear();
        }
        if (target.kind == ExpressionKind::Concatenation) {
            for (auto& operand : target.operands) {
                if (!self(self, operand)) {
                    return false;
                }
            }
        }
        return true;
    };
    if (statement.target.kind == ExpressionKind::Aggregate
        && statement.target.text == "sv-pattern") {
        Statement normalized = statement;
        if (!normalize_aggregate_target(
                normalize_aggregate_target, normalized.target)) {
            return;
        }
        lower_concatenated_assignment(normalized);
        return;
    }
    if (statement.target.kind == ExpressionKind::Call
        && (statement.target.text == "@stream-left"
            || statement.target.text == "@stream-right")) {
        if (language_ != frontend::Language::SystemVerilog2017
            || statement.target.operands.size() < 2U) {
            report(
                "FSIM-ELAB-SVSTREAM-001",
                "a streaming assignment target requires SystemVerilog and at least one lvalue",
                statement.target.span);
            return;
        }
        Statement normalized = statement;
        const auto slice = normalized.target.operands.front();
        std::vector<Expression> targets(
            std::next(normalized.target.operands.begin()),
            normalized.target.operands.end());
        for (auto& target : targets) {
            if (!normalize_aggregate_target(
                    normalize_aggregate_target, target)) {
                return;
            }
        }
        normalized.target = Expression {
            ExpressionKind::Concatenation, "concat",
            std::move(targets), statement.target.span };
        normalized.value = Expression {
            ExpressionKind::Call,
            statement.target.text == "@stream-left"
                ? "@stream-target-left"
                : "@stream-target-right",
            { slice, statement.value }, statement.value.span };
        lower_concatenated_assignment(normalized);
        return;
    }
    if (statement.target.kind == ExpressionKind::Identifier) {
        const auto* target_type = object_type(statement.target.text);
        const bool null_event = statement.value.kind == ExpressionKind::Call
            && statement.value.text == "@sv-null";
        const auto* source_type = statement.value.kind
                == ExpressionKind::Identifier
            ? object_type(statement.value.text)
            : nullptr;
        if (target_type != nullptr && target_type->spelling == "event") {
            const auto target = signals_.find(statement.target.text);
            const auto source = source_type != nullptr
                ? signals_.find(statement.value.text)
                : signals_.end();
            if (target == signals_.end()
                || (!null_event
                    && (source_type == nullptr
                        || source_type->spelling != "event"
                        || source == signals_.end()))) {
                report(
                    "FSIM-ELAB-SVEVENT-009",
                    "named-event assignment requires an event variable or null source",
                    statement.span);
                return;
            }
            if (statement.assignment_kind != AssignmentKind::Blocking
                || statement.procedural_assignment_control
                    != frontend::ProceduralAssignmentControl::None) {
                report(
                    "FSIM-ELAB-SVEVENT-010",
                    "named-event alias assignment must be blocking and time-free",
                    statement.span);
                return;
            }
            process_.operations.emplace_back(
                EventAlias {
                    target->second,
                    null_event ? SignalId { } : source->second,
                    !null_event });
            return;
        } else if (source_type != nullptr
            && source_type->spelling == "event") {
            report(
                "FSIM-ELAB-SVEVENT-009",
                "named-event assignment requires an event-variable target",
                statement.span);
            return;
        }
    }
    if (statement.target.kind == ExpressionKind::Concatenation) {
        lower_concatenated_assignment(statement);
        return;
    }
    if (lower_class_assignment(statement)) {
        return;
    }
    if (lower_vhdl_access_assignment(statement)) {
        return;
    }
    const Expression* base = &statement.target;
    std::uint32_t selected_offset = 0;
    bool has_selected_offset = false;
    std::optional<std::size_t> selected_width;
    std::optional<frontend::ValueDomain> selected_domain;
    std::optional<DynamicIndex> dynamic_selection;
    std::optional<DynamicPartIndex> dynamic_part_selection;
    std::optional<frontend::Type> selected_type;
    std::optional<PackedMemberReference> selected_member_reference;
    std::vector<const Expression*> packed_selections;
    constexpr std::string_view vhdl_member_prefix {
        "@vhdl-member:"
    };
    while (base->kind == ExpressionKind::Index
        || base->kind == ExpressionKind::Slice
        || (base->kind == ExpressionKind::Call
            && base->text.starts_with(vhdl_member_prefix))) {
        const auto expected_operands = base->kind == ExpressionKind::Index
            ? 2U
            : base->kind == ExpressionKind::Slice ? 3U
                                                  : 1U;
        if (base->operands.size() != expected_operands) {
            break;
        }
        packed_selections.push_back(base);
        base = &base->operands.front();
    }
    std::ranges::reverse(packed_selections);
    if (base->kind != ExpressionKind::Identifier) {
        report(
            "FSIM-ELAB-031",
            "an assignment target must be a packed object, bit-select, "
            "or constant part-select",
            statement.target.span);
        return;
    }

    auto target_name = base->text;
    const auto container_local = container_locals_.find(target_name);
    const auto container_object = container_objects_.find(target_name);
    const bool targets_container_object
        = container_local == container_locals_.end()
        && container_object != container_objects_.end();
    if (container_local != container_locals_.end()
        || container_object != container_objects_.end()) {
        if (targets_container_object
            && read_only_container_objects_.contains(
                target_name)) {
            report(
                "FSIM-ELAB-SVPORT-009",
                "an input container port is read-only within its "
                "module",
                statement.target.span);
            return;
        }
        const auto* source_type = object_type(target_name);
        if (source_type == nullptr) {
            return;
        }
        const auto runtime_type = container_type(*source_type, statement.target.span);
        if (!runtime_type) {
            return;
        }
        const auto element_width = runtime_type->element_kind
                    == ContainerElementKind::Packed
                || runtime_type->element_kind
                    == ContainerElementKind::Scalar
            ? runtime_type->element_width
            : 0U;
        const bool indexed_dynamic_object
            = targets_container_object
            && !runtime_type->fixed
            && !runtime_type->associative
            && statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2U
            && packed_selections.size() == 1U;
        if (indexed_dynamic_object
            && statement.assignment_kind
                == AssignmentKind::NonBlocking
            && systemverilog_standard_
                == frontend::StandardRevision::SystemVerilog2023) {
            report(
                "FSIM-ELAB-SVASSIGN-001",
                "SystemVerilog-2023 prohibits a nonblocking assignment to "
                "an element of a dynamically sized array",
                statement.span);
            return;
        }
        if (targets_container_object
            && runtime_type->fixed
            && runtime_type->dimensions.size() == 1U
            && element_width != 0U
            && statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2U
            && packed_selections.size() == 1U
            && statement.procedural_assignment_control
                == frontend::ProceduralAssignmentControl::None) {
            const auto alias = std::find_if(
                design_.container_signal_aliases_.rbegin(),
                design_.container_signal_aliases_.rend(),
                [&](const ContainerSignalAlias& candidate) {
                    return candidate.object == container_object->second
                        && candidate.readable && candidate.writable;
                });
            if (alias != design_.container_signal_aliases_.rend()) {
                const auto [left, right] = runtime_type->dimensions.front();
                const auto low = std::min(left, right);
                const auto high = std::max(left, right);
                const auto count = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(high) - low + 1);
                const auto total_width = count * element_width;
                if (total_width
                    > std::numeric_limits<std::uint32_t>::max()) {
                    report(
                        "FSIM-ELAB-SVCONTAINER-009",
                        "static-array packed storage exceeds the executable "
                        "signal width limit",
                        statement.span);
                    return;
                }
                auto element_type = *source_type;
                if (source_type->systemverilog_container
                    && source_type->systemverilog_container
                           ->element_types.size() == 1U) {
                    element_type = source_type->systemverilog_container
                                       ->element_types.front();
                }
                element_type.systemverilog_container.reset();
                auto value = lower_expression(
                    statement.value, element_width, &element_type);
                if (!value) {
                    return;
                }
                if (register_width(*value) != element_width) {
                    *value = resize_register(
                        *value, element_width,
                        is_signed_expression(statement.value));
                }
                const auto blocking = statement.assignment_kind
                    == AssignmentKind::Blocking;
                if (const auto selected = static_integer_value(
                        statement.target.operands[1])) {
                    if (*selected < low || *selected > high) {
                        report(
                            "FSIM-ELAB-SVCONTAINER-009",
                            "static-array assignment index is out of range",
                            statement.target.operands[1].span);
                        return;
                    }
                    const auto ordinal = left >= right
                        ? static_cast<std::uint64_t>(left - *selected)
                        : static_cast<std::uint64_t>(*selected - left);
                    const auto offset = static_cast<std::uint32_t>(
                        (count - 1U - ordinal) * element_width);
                    if (blocking) {
                        process_.operations.emplace_back(
                            WriteBlockingSlice {
                                alias->signal, *value, offset });
                    } else {
                        process_.operations.emplace_back(
                            WriteUpdateSlice {
                                alias->signal, *value, offset });
                    }
                    return;
                }
                auto index = lower_expression(
                    statement.target.operands[1], 32U);
                if (!index) {
                    return;
                }
                if (register_width(*index) != 32U) {
                    *index = resize_register(
                        *index, 32U,
                        is_signed_expression(
                            statement.target.operands[1]));
                }
                process_.operations.emplace_back(IntegerCheck {
                    *index, low, high });
                const auto declared_left = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(LoadConstant {
                    declared_left, integer_value(left) });
                const auto ordinal = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(IntegerBinary {
                    IntegerBinaryOperator::subtract,
                    ordinal,
                    left >= right ? declared_left : *index,
                    left >= right ? *index : declared_left });
                const auto last = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(LoadConstant {
                    last,
                    integer_value(static_cast<std::int64_t>(count - 1U)) });
                const auto reverse_ordinal = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(IntegerBinary {
                    IntegerBinaryOperator::subtract,
                    reverse_ordinal, last, ordinal });
                const auto width = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(LoadConstant {
                    width,
                    integer_value(static_cast<std::int64_t>(
                        element_width)) });
                const auto packed_base = allocate_register(
                    32U, frontend::ValueDomain::Integer);
                process_.operations.emplace_back(IntegerBinary {
                    IntegerBinaryOperator::multiply,
                    packed_base, reverse_ordinal, width });
                const DynamicPartIndex selection {
                    packed_base,
                    static_cast<std::int64_t>(total_width - 1U),
                    0,
                    0,
                    static_cast<std::uint32_t>(element_width),
                    true,
                    true };
                if (blocking) {
                    process_.operations.emplace_back(
                        WriteBlockingDynamicPartSlice {
                            alias->signal, *value, selection });
                } else {
                    process_.operations.emplace_back(
                        WriteUpdateDynamicPartSlice {
                            alias->signal, *value, selection });
                }
                return;
            }
        }
        if (targets_container_object
            && (!runtime_type->fixed
                || runtime_type->dimensions.size() <= 1U)
            && !runtime_type->associative
            && element_width != 0U
            && statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2U
            && packed_selections.size() == 1U
            && (statement.assignment_kind == AssignmentKind::Blocking
                || (indexed_dynamic_object
                    && statement.assignment_kind
                        == AssignmentKind::NonBlocking))
            && statement.procedural_update_kind
                == frontend::ProceduralUpdateKind::None
            && statement.procedural_assignment_control
                == frontend::ProceduralAssignmentControl::None) {
            auto element_type = *source_type;
            if (source_type->systemverilog_container
                && source_type->systemverilog_container
                       ->element_types.size() == 1U) {
                element_type = source_type->systemverilog_container
                                   ->element_types.front();
            }
            element_type.systemverilog_container.reset();
            auto value = lower_expression(
                statement.value, element_width, &element_type);
            auto index = lower_expression(
                statement.target.operands[1], 32U);
            if (!value || !index) {
                return;
            }
            if (register_width(*value) != element_width) {
                *value = resize_register(
                    *value, element_width,
                    is_signed_expression(statement.value));
            }
            if (register_width(*index) != 32U) {
                *index = resize_register(
                    *index, 32U,
                    is_signed_expression(
                        statement.target.operands[1]));
            }
            process_.operations.emplace_back(
                WriteContainerObjectElement {
                    container_object->second,
                    *index,
                    *value,
                    true,
                    false,
                    statement.assignment_kind
                        == AssignmentKind::NonBlocking,
                    std::nullopt });
            return;
        }
        if (statement.assignment_kind
                != AssignmentKind::Blocking
            || statement.procedural_assignment_control
                != frontend::ProceduralAssignmentControl::None) {
            report(
                "FSIM-ELAB-SVCONTAINER-009",
                "container assignments must be blocking and time-free",
                statement.span);
            return;
        }
        ContainerRegisterId target { };
        if (container_local != container_locals_.end()) {
            target = container_local->second;
        } else {
            target = allocate_container_register(*runtime_type);
            process_.operations.emplace_back(
                ReadContainerObject {
                    target, container_object->second });
        }
        const auto object = targets_container_object
            ? std::optional<ContainerObjectId> {
                  container_object->second
              }
            : std::nullopt;
        if (lower_unpacked_aggregate_assignment(
                statement, *source_type, target, object)) {
            return;
        }
        if (lower_multidimensional_container_assignment(
                statement, *source_type, target, object)) {
            return;
        }
        if (statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2
            && statement.target.operands.front().kind
                == ExpressionKind::Index
            && statement.target.operands.front().operands.size() == 2
            && runtime_type->element_kind
                == ContainerElementKind::Container
            && runtime_type->element_types.size() == 1
            && runtime_type->element_types.front().element_kind
                == ContainerElementKind::String) {
            const auto& nested_type = runtime_type->element_types.front();
            const auto& outer_index_expression = statement.target.operands.front().operands[1];
            const auto& inner_index_expression = statement.target.operands[1];
            const auto outer_index_width = runtime_type->associative
                ? static_cast<std::size_t>(
                      runtime_type->index_width)
                : runtime_type->fixed
                ? std::size_t { 32 }
                : infer_width(outer_index_expression)
                      .value_or(32U);
            auto outer_index = lower_expression(
                outer_index_expression,
                outer_index_width,
                runtime_type->associative
                    ? source_type->systemverilog_container
                          ->associative_index_type.get()
                    : nullptr);
            const auto inner_index_width = nested_type.associative
                ? static_cast<std::size_t>(nested_type.index_width)
                : nested_type.fixed
                ? std::size_t { 32 }
                : infer_width(inner_index_expression)
                      .value_or(32U);
            const auto& nested_source_type = source_type->systemverilog_container
                                                 ->element_types.front();
            auto inner_index = lower_expression(
                inner_index_expression,
                inner_index_width,
                nested_type.associative
                        && nested_source_type.systemverilog_container
                    ? nested_source_type.systemverilog_container
                          ->associative_index_type.get()
                    : nullptr);
            const auto value = lower_string_expression(statement.value);
            if (!outer_index || !inner_index || !value) {
                return;
            }
            if (runtime_type->associative
                && register_width(*outer_index)
                    != runtime_type->index_width) {
                *outer_index = resize_register(
                    *outer_index,
                    runtime_type->index_width,
                    runtime_type->signed_indices);
            }
            if (nested_type.associative
                && register_width(*inner_index)
                    != nested_type.index_width) {
                *inner_index = resize_register(
                    *inner_index,
                    nested_type.index_width,
                    nested_type.signed_indices);
            }
            const auto nested = allocate_container_register(nested_type);
            const auto outer_signed = runtime_type->associative
                ? runtime_type->signed_indices
                : runtime_type->fixed
                    || is_signed_expression(
                        outer_index_expression);
            process_.operations.emplace_back(
                ContainerElementRead {
                    nested, target, *outer_index,
                    outer_signed });
            process_.operations.emplace_back(
                ContainerStringWrite {
                    nested, *inner_index, *value,
                    nested_type.associative
                        ? nested_type.signed_indices
                        : nested_type.fixed
                            || is_signed_expression(
                                inner_index_expression),
                    false });
            process_.operations.emplace_back(
                ContainerElementWrite {
                    target, *outer_index, nested,
                    outer_signed });
            if (object) {
                process_.operations.emplace_back(
                    WriteContainerObject { *object, target, std::nullopt });
            }
            return;
        }
        if (packed_selections.size() > 1
            && packed_selections.front()->kind
                == ExpressionKind::Index
            && packed_selections.front()->operands.size() == 2
            && element_width) {
            const auto& word_selection = *packed_selections.front();
            const auto index_width = runtime_type->associative
                ? static_cast<std::size_t>(runtime_type->index_width)
                : runtime_type->fixed
                ? std::size_t { 32 }
                : infer_width(word_selection.operands[1])
                      .value_or(std::size_t { 32 });
            const auto* index_type = runtime_type->associative
                ? source_type->systemverilog_container
                      ->associative_index_type.get()
                : nullptr;
            auto index = lower_expression(
                word_selection.operands[1], index_width, index_type);
            if (!index) {
                return;
            }
            if (runtime_type->associative
                && register_width(*index) != runtime_type->index_width) {
                *index = resize_register(
                    *index,
                    runtime_type->index_width,
                    runtime_type->signed_indices);
            }

            auto element_type = *source_type;
            if (source_type->systemverilog_container
                && source_type->systemverilog_container
                        ->element_types.size()
                    == 1) {
                element_type = source_type->systemverilog_container
                                   ->element_types.front();
            }
            element_type.systemverilog_container.reset();
            const auto word = allocate_register(
                runtime_type->element_width, element_type.domain);
            const auto signed_index = runtime_type->associative
                ? runtime_type->signed_indices
                : runtime_type->fixed
                    || is_signed_expression(word_selection.operands[1]);
            process_.operations.emplace_back(ContainerRead {
                word, target, *index, signed_index });

            std::uint32_t element_offset = 0;
            bool has_element_offset = false;
            std::optional<std::size_t> element_selected_width;
            std::optional<frontend::ValueDomain>
                element_selected_domain { element_type.domain };
            std::optional<DynamicIndex> element_dynamic_selection;
            std::optional<DynamicPartIndex>
                element_dynamic_part_selection;
            std::optional<frontend::Type> element_selected_type;
            const std::vector<const Expression*> element_selections {
                std::next(packed_selections.begin()),
                packed_selections.end()
            };
            if (!lower_assignment_selections(
                    statement,
                    target_name,
                    runtime_type->element_width,
                    element_selections,
                    element_offset,
                    has_element_offset,
                    element_selected_width,
                    element_selected_domain,
                    element_dynamic_selection,
                    element_dynamic_part_selection,
                    element_selected_type)) {
                return;
            }
            const auto assignment_width = element_selected_width.value_or(
                runtime_type->element_width);
            std::optional<RegisterId> captured;
            if (statement.procedural_update_kind
                != frontend::ProceduralUpdateKind::None) {
                captured = allocate_register(
                    assignment_width,
                    element_selected_domain.value_or(
                        element_type.domain));
                if (element_dynamic_part_selection) {
                    process_.operations.emplace_back(DynamicPartSelect {
                        *captured,
                        word,
                        element_dynamic_part_selection->base,
                        element_dynamic_part_selection->left,
                        element_dynamic_part_selection->right,
                        element_dynamic_part_selection->width,
                        element_dynamic_part_selection->increasing,
                        element_dynamic_part_selection->source_descending,
                        is_two_state_domain(element_type.domain),
                        element_dynamic_part_selection->base_offset });
                } else if (element_dynamic_selection) {
                    process_.operations.emplace_back(DynamicExtract {
                        *captured, word, *element_dynamic_selection });
                } else if (has_element_offset) {
                    process_.operations.emplace_back(Extract {
                        *captured,
                        word,
                        element_offset,
                        static_cast<std::uint32_t>(assignment_width) });
                } else {
                    process_.operations.emplace_back(
                        CopyRegister { *captured, word });
                }
            }
            auto value = captured
                ? lower_procedural_update_value(
                      statement,
                      *captured,
                      assignment_width,
                      element_selected_type
                          ? &*element_selected_type
                          : nullptr)
                : lower_expression(
                      statement.value,
                      assignment_width,
                      element_selected_type
                          ? &*element_selected_type
                          : nullptr);
            if (!value) {
                return;
            }
            if (register_width(*value) != assignment_width) {
                *value = resize_register(
                    *value,
                    assignment_width,
                    is_signed_expression(statement.value));
            }
            if (element_dynamic_part_selection) {
                process_.operations.emplace_back(DynamicPartInsert {
                    word,
                    word,
                    *value,
                    *element_dynamic_part_selection });
            } else if (element_dynamic_selection) {
                process_.operations.emplace_back(DynamicInsert {
                    word, word, *value, *element_dynamic_selection });
            } else if (has_element_offset) {
                process_.operations.emplace_back(Insert {
                    word, word, *value, element_offset });
            } else {
                process_.operations.emplace_back(
                    CopyRegister { word, *value });
            }
            process_.operations.emplace_back(ContainerWrite {
                target, *index, word, signed_index });
            if (object) {
                process_.operations.emplace_back(
                    WriteContainerObject { *object, target, std::nullopt });
            }
            return;
        }
        if (packed_selections.size() > 1) {
            report(
                "FSIM-ELAB-031",
                "nested selected assignment targets require a complete "
                "multidimensional static-array index",
                statement.target.span);
            return;
        }
        if (statement.target.kind
            == ExpressionKind::Identifier) {
            const bool locator = statement.value.kind == ExpressionKind::Call
                && (statement.value.text == ".min"
                    || statement.value.text == ".max"
                    || statement.value.text == ".unique"
                    || statement.value.text == ".unique_index"
                    || statement.value.text == ".find"
                    || statement.value.text == ".find_index"
                    || statement.value.text == ".find_first"
                    || statement.value.text
                        == ".find_first_index"
                    || statement.value.text == ".find_last"
                    || statement.value.text
                        == ".find_last_index");
            if (locator) {
                if (!lower_container_locator(
                        statement.value, target, *runtime_type)) {
                    return;
                }
            } else if (statement.value.kind
                    == ExpressionKind::Aggregate
                && statement.value.text == "sv-pattern") {
                const auto value = lower_container_pattern(
                    statement.value,
                    *source_type,
                    *runtime_type);
                if (!value) {
                    return;
                }
                process_.operations.emplace_back(
                    CopyContainerRegister { target, *value });
            } else if (
                runtime_type->fixed
                && (statement.value.kind
                        == ExpressionKind::Slice
                    || (statement.value.kind
                            == ExpressionKind::Call
                        && statement.value.text != "?:"))) {
                const auto value = lower_static_container_assignment_value(
                    statement.value, *runtime_type);
                if (!value) {
                    return;
                }
                process_.operations.emplace_back(
                    CopyContainerRegister { target, *value });
            } else {
                lower_nonstatic_container_assignment(
                    target, statement.value, *runtime_type);
            }
        } else if (
            statement.target.kind == ExpressionKind::Index
            && statement.target.operands.size() == 2) {
            if (runtime_type->element_kind
                    == ContainerElementKind::Container
                && runtime_type->element_types.size() == 1) {
                const auto index_width = runtime_type->associative
                    ? static_cast<std::size_t>(
                          runtime_type->index_width)
                    : runtime_type->fixed
                    ? std::size_t { 32 }
                    : infer_width(
                          statement.target.operands[1])
                          .value_or(32U);
                auto index = lower_expression(
                    statement.target.operands[1], index_width,
                    runtime_type->associative
                        ? source_type->systemverilog_container
                              ->associative_index_type.get()
                        : nullptr);
                if (!index) {
                    return;
                }
                if (runtime_type->associative
                    && register_width(*index)
                        != runtime_type->index_width) {
                    *index = resize_register(
                        *index,
                        runtime_type->index_width,
                        runtime_type->signed_indices);
                }
                const auto nested = allocate_container_register(
                    runtime_type->element_types.front());
                lower_nonstatic_container_assignment(
                    nested, statement.value,
                    runtime_type->element_types.front());
                process_.operations.emplace_back(
                    ContainerElementWrite {
                        target, *index, nested,
                        runtime_type->associative
                            ? runtime_type->signed_indices
                            : runtime_type->fixed
                                || is_signed_expression(
                                    statement.target.operands[1]) });
                if (object) {
                    process_.operations.emplace_back(
                        WriteContainerObject { *object, target, std::nullopt });
                }
                return;
            }
            if (!element_width
                && runtime_type->element_kind
                    != ContainerElementKind::String) {
                report(
                    "FSIM-ELAB-SVCONTAINER-024",
                    "selected string, nested-container, and unpacked "
                    "aggregate element assignment requires a typed "
                    "container element operation",
                    statement.target.span);
                return;
            }
            const auto index_width = runtime_type->associative
                ? static_cast<std::size_t>(
                      runtime_type->index_width)
                : runtime_type->fixed
                ? std::size_t { 32 }
                : infer_width(
                      statement.target.operands[1])
                      .value_or(std::size_t { 32 });
            const auto* index_type = runtime_type->associative
                ? source_type->systemverilog_container
                      ->associative_index_type.get()
                : nullptr;
            const bool string_index = runtime_type->associative
                && runtime_type->string_indices;
            auto index = string_index
                ? lower_string_expression(
                      statement.target.operands[1])
                : lower_expression(
                      statement.target.operands[1],
                      index_width, index_type);
            if (runtime_type->element_kind
                == ContainerElementKind::String) {
                const auto value = lower_string_expression(statement.value);
                if (!index || !value) {
                    return;
                }
                if (runtime_type->associative
                    && !string_index
                    && register_width(*index)
                        != runtime_type->index_width) {
                    *index = resize_register(
                        *index,
                        runtime_type->index_width,
                        runtime_type->signed_indices);
                }
                process_.operations.emplace_back(
                    ContainerStringWrite {
                        target,
                        *index,
                        *value,
                        runtime_type->associative
                            ? runtime_type->signed_indices
                            : runtime_type->fixed
                                || is_signed_expression(
                                    statement.target.operands[1]),
                        false,
                        string_index });
                if (object) {
                    process_.operations.emplace_back(
                        WriteContainerObject {
                            *object, target, std::nullopt });
                }
                return;
            }
            auto element_type = *source_type;
            if (source_type->systemverilog_container
                && source_type->systemverilog_container
                        ->element_types.size()
                    == 1) {
                element_type = source_type->systemverilog_container
                                   ->element_types.front();
            } else {
                element_type.systemverilog_container.reset();
            }
            const auto packed_element_width = runtime_type->element_width;
            auto value = lower_expression(
                statement.value,
                packed_element_width,
                &element_type);
            if (!index || !value) {
                return;
            }
            if (register_width(*value) != packed_element_width) {
                *value = resize_register(
                    *value,
                    packed_element_width,
                    is_signed_expression(statement.value));
            }
            if (runtime_type->associative
                && !string_index
                && register_width(*index)
                    != runtime_type->index_width) {
                *index = resize_register(
                    *index,
                    runtime_type->index_width,
                    runtime_type->signed_indices);
            }
            process_.operations.emplace_back(
                ContainerWrite {
                    target,
                    *index,
                    *value,
                    runtime_type->associative
                        ? runtime_type->signed_indices
                        : runtime_type->fixed
                            || is_signed_expression(
                                statement.target.operands[1]),
                    false,
                    string_index });
        } else if (
            statement.target.kind == ExpressionKind::Slice
            && statement.target.operands.size() == 3) {
            if (!lower_static_container_slice_assignment(
                    statement.target,
                    statement.value,
                    target,
                    *runtime_type)) {
                return;
            }
        } else {
            report(
                "FSIM-ELAB-SVCONTAINER-011",
                "container targets support only whole-value or "
                "element-index blocking assignment",
                statement.target.span);
            return;
        }
        if (object) {
            process_.operations.emplace_back(
                WriteContainerObject {
                    *object, target, std::nullopt });
        }
        return;
    }
    const auto string_local = string_locals_.find(target_name);
    const auto string_object = string_objects_.find(target_name);
    if (string_local != string_locals_.end()
        || string_object != string_objects_.end()) {
        if (string_object != string_objects_.end()
            && read_only_string_objects_.contains(
                string_object->second)) {
            report(
                "FSIM-ELAB-SVPORT-011",
                "an input mutable string port is read-only",
                statement.target.span);
            return;
        }
        if (statement.assignment_kind
                != AssignmentKind::Blocking
            || statement.procedural_assignment_control
                != frontend::ProceduralAssignmentControl::None) {
            report(
                "FSIM-ELAB-SVSTRING-013",
                "string assignments must be blocking and time-free",
                statement.span);
            return;
        }
        if (statement.target.kind
            == ExpressionKind::Identifier) {
            const auto value = lower_string_expression(statement.value);
            if (!value) {
                report(
                    "FSIM-ELAB-SVSTRING-014",
                    "string assignment requires a string value",
                    statement.value.span);
                return;
            }
            if (string_local != string_locals_.end()) {
                process_.operations.emplace_back(
                    CopyStringRegister {
                        string_local->second, *value });
            } else {
                process_.operations.emplace_back(
                    WriteStringObject {
                        string_object->second, *value });
            }
            return;
        }
        if (statement.target.kind != ExpressionKind::Index
            || statement.target.operands.size() != 2) {
            report(
                "FSIM-ELAB-SVSTRING-015",
                "string targets support only whole-value or byte-index "
                "blocking assignment",
                statement.target.span);
            return;
        }
        StringRegisterId target { };
        if (string_local != string_locals_.end()) {
            target = string_local->second;
        } else {
            target = allocate_string_register();
            process_.operations.emplace_back(
                ReadStringObject {
                    target, string_object->second });
        }
        const auto index_width = infer_width(statement.target.operands[1])
                                     .value_or(std::size_t { 32 });
        const auto index = lower_expression(
            statement.target.operands[1],
            index_width);
        std::optional<RegisterId> byte;
        if (statement.value.kind
                == ExpressionKind::StringLiteral
            && statement.value.decoded_string) {
            if (statement.value.decoded_string->size() == 1) {
                byte = allocate_register(
                    32, frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(
                    LoadConstant {
                        *byte,
                        unsigned_value(
                            static_cast<unsigned char>(
                                statement.value.decoded_string->front()),
                            32) });
            }
        }
        if (!byte) {
            byte = lower_expression(statement.value, 32);
        }
        if (!index || !byte) {
            return;
        }
        process_.operations.emplace_back(
            StringReplaceByte {
                target,
                *index,
                *byte,
                is_signed_expression(
                    statement.target.operands[1]) });
        if (string_object != string_objects_.end()) {
            process_.operations.emplace_back(
                WriteStringObject {
                    string_object->second, target });
        }
        return;
    }
    if (!locals_.contains(target_name)
        && !signals_.contains(target_name)) {
        if (const auto selected = packed_member_reference(target_name)) {
            const auto width = selected->member->width();
            if (!width || *width == 0
                || *width
                    > std::numeric_limits<std::uint32_t>::max()
                || selected->lsb_offset
                    > std::numeric_limits<std::uint32_t>::max()) {
                report(
                    "FSIM-ELAB-SVSTRUCT-002",
                    "packed aggregate member '" + target_name
                        + "' has no executable layout",
                    statement.target.span);
                return;
            }
            target_name = selected->base;
            selected_offset = static_cast<std::uint32_t>(
                selected->lsb_offset);
            has_selected_offset = true;
            selected_width = static_cast<std::size_t>(*width);
            selected_domain = selected->member->domain;
            selected_member_reference = *selected;
            if (language_ == frontend::Language::Vhdl2008
                && !selected->member->nested_types.empty()) {
                selected_type = selected->member->nested_types.front();
            }
        }
    }
    const auto local = locals_.find(target_name);
    const auto signal = signals_.find(target_name);
    if (local == locals_.end() && signal == signals_.end()) {
        if (!report_unsupported_cross_root_reference(
                target_name, statement.target.span)) {
            report(
                "FSIM-ELAB-032",
                "unknown assignment target '" + target_name + "'",
                statement.target.span);
        }
        return;
    }
    if (signal != signals_.end()
        && !validate_vhdl_mode_view_write(
            signal->second, statement.target, statement.target.span)) {
        return;
    }
    const auto whole_width = local != locals_.end()
        ? register_width(local->second)
        : design_.signal_info_[signal->second].width;
    if (!lower_assignment_selections(
            statement, base->text, whole_width,
            packed_selections, selected_offset,
            has_selected_offset, selected_width,
            selected_domain, dynamic_selection,
            dynamic_part_selection, selected_type)) {
        return;
    }

    const auto target_width = selected_width.value_or(whole_width);
    const bool has_disconnect = std::ranges::any_of(
        statement.vhdl_waveform,
        &frontend::VhdlWaveformElement::disconnect);
    const auto disconnect_domain = selected_domain.value_or(
        local != locals_.end()
            ? register_domain(local->second)
            : design_.signal_info_[signal->second].source_domain);
    if (has_disconnect
        && (local != locals_.end()
            || disconnect_domain
                != frontend::ValueDomain::Logic9)) {
        report(
            "FSIM-ELAB-VHDLGUARD-002",
            "guarded driver disconnection requires a nine-state "
            "resolved signal target",
            statement.span);
        return;
    }
    const auto load_disconnect = [&]() {
        const auto result = allocate_register(
            target_width, frontend::ValueDomain::Logic9);
        auto value = PackedLogic4 {
            target_width, Logic4::z
        };
        value.fill(runtime::Logic9::z);
        process_.operations.emplace_back(
            LoadConstant { result, std::move(value) });
        return result;
    };
    if (dynamic_part_selection
        && language_ != frontend::Language::Vhdl2008
        && statement.assignment_kind != AssignmentKind::Blocking
        && statement.assignment_kind != AssignmentKind::NonBlocking
        && statement.assignment_kind != AssignmentKind::Continuous) {
        report(
            "FSIM-ELAB-SVEXPR-006",
            "runtime-base packed part-select targets require a "
            "procedural blocking or nonblocking assignment, or a "
            "continuous assignment",
            statement.target.span);
        return;
    }
    const auto* contextual_target_type = selected_type
        ? &*selected_type
        : has_selected_offset || dynamic_selection
            || dynamic_part_selection
        ? nullptr
        : object_type(target_name);
    if (language_ == frontend::Language::Vhdl2008
        && contextual_target_type != nullptr
        && contextual_target_type->vhdl_access
        && signal != signals_.end()
        && !(statement.value.kind == ExpressionKind::Call
            && statement.value.text == "@vhdl-null")) {
        report(
            "FSIM-ELAB-VHACCESS-021",
            "a nonnull process-local VHDL access handle cannot escape "
            "through a signal",
            statement.value.span);
        return;
    }
    if (!has_disconnect
        && !validate_sv_nominal_assignment(
            contextual_target_type, statement.value)) {
        return;
    }
    if (!has_disconnect
        && !validate_vhdl_composite_assignment(
            contextual_target_type, statement.value)) {
        return;
    }
    const bool null_vhdl_target = language_ == frontend::Language::Vhdl2008
        && target_width == 0
        && contextual_target_type != nullptr
        && contextual_target_type->vhdl_array
        && contextual_target_type->vhdl_array->flat_width
        && *contextual_target_type->vhdl_array->flat_width == 0;
    if (null_vhdl_target) {
        const auto value = lower_expression(
            statement.value, 0, contextual_target_type);
        if (!value) {
            return;
        }
        if (register_width(*value) != 0) {
            report(
                "FSIM-ELAB-047",
                "assignment to null VHDL array '" + target_name
                    + "' requires a null array value",
                statement.span);
        }
        return;
    }
    const auto assignment_control = statement.procedural_assignment_control;
    const bool procedural_delay = assignment_control
        == frontend::ProceduralAssignmentControl::Delay;
    const bool procedural_event = assignment_control
        == frontend::ProceduralAssignmentControl::Event;
    if (selected_member_reference
        && !selected_member_reference->unions.empty()
        && (assignment_control
                != frontend::ProceduralAssignmentControl::None
            || statement.delay
            || statement.vhdl_delay_mechanism
            || statement.vhdl_waveform.size() > 1)) {
        report(
            "FSIM-ELAB-SVUNION-001",
            "selected union writes require a time-free scalar "
            "assignment so payload padding and the active tag update "
            "coherently",
            statement.span);
        return;
    }
    if (assignment_control
            != frontend::ProceduralAssignmentControl::None
        && statement.assignment_kind != AssignmentKind::Blocking
        && statement.assignment_kind
            != AssignmentKind::NonBlocking) {
        report(
            "FSIM-ELAB-105",
            "procedural assignment control is attached to a "
            "nonprocedural assignment kind",
            statement.span);
        return;
    }
    if (assignment_control
            == frontend::ProceduralAssignmentControl::None
        && !statement.sensitivities.empty()) {
        report(
            "FSIM-ELAB-105",
            "procedural assignment event metadata has no event-control "
            "kind",
            statement.span);
        return;
    }
    if (procedural_delay
        && (!statement.delay
            || !statement.sensitivities.empty())) {
        report(
            "FSIM-ELAB-105",
            "procedural assignment delay control has invalid delay or "
            "sensitivity metadata",
            statement.span);
        return;
    }
    if (procedural_event
        && (statement.delay
            || statement.sensitivities.empty())) {
        report(
            "FSIM-ELAB-105",
            "procedural assignment event control has invalid delay or "
            "sensitivity metadata",
            statement.span);
        return;
    }
    if (statement.procedural_assignment_repeat
        && (!procedural_event
            || !statement.loop_limit.valid())) {
        report(
            "FSIM-ELAB-105",
            "repeated procedural assignment event control has "
            "invalid control or count metadata",
            statement.span);
        return;
    }
    if (procedural_event) {
        emit_debug_point(
            DebugPointKind::wait, statement.span);
        if (!emit_event_control_wait(statement)) {
            return;
        }
    }
    if (local != locals_.end()) {
        if (statement.assignment_kind != AssignmentKind::Blocking) {
            report(
                "FSIM-ELAB-056",
                "local variable assignments require a blocking/variable "
                "assignment",
                statement.span);
            return;
        }
        std::optional<RegisterId> captured;
        if (statement.procedural_update_kind
            != frontend::ProceduralUpdateKind::None) {
            if (dynamic_part_selection) {
                captured = allocate_register(
                    target_width,
                    selected_domain.value_or(
                        register_domain(local->second)));
                process_.operations.emplace_back(
                    DynamicPartSelect {
                        *captured,
                        local->second,
                        dynamic_part_selection->base,
                        dynamic_part_selection->left,
                        dynamic_part_selection->right,
                        dynamic_part_selection->width,
                        dynamic_part_selection->increasing,
                        dynamic_part_selection->source_descending,
                        is_two_state_domain(
                            register_domain(local->second)),
                        dynamic_part_selection->base_offset });
            } else if (dynamic_selection) {
                captured = allocate_register(
                    target_width,
                    selected_domain.value_or(
                        register_domain(local->second)));
                process_.operations.emplace_back(
                    DynamicExtract {
                        *captured,
                        local->second,
                        *dynamic_selection });
            } else if (has_selected_offset) {
                captured = allocate_register(
                    target_width,
                    selected_domain.value_or(
                        register_domain(local->second)));
                process_.operations.emplace_back(Extract {
                    *captured,
                    local->second,
                    selected_offset,
                    static_cast<std::uint32_t>(target_width) });
            } else {
                captured = allocate_register(
                    target_width,
                    register_domain(local->second));
                process_.operations.emplace_back(
                    CopyRegister { *captured, local->second });
            }
        }
        auto value = captured
            ? lower_procedural_update_value(
                  statement,
                  *captured,
                  target_width,
                  contextual_target_type)
            : lower_expression(
                  statement.value,
                  target_width,
                  contextual_target_type);
        if (!value) {
            return;
        }
        if (register_width(*value) != target_width
            && language_
                != frontend::Language::Vhdl2008) {
            *value = resize_register(
                *value,
                target_width,
                is_signed_expression(statement.value));
        }
        if (register_width(*value) != target_width) {
            report(
                "FSIM-ELAB-057",
                "local variable assignment width mismatch for '"
                    + target_name + "'",
                statement.span);
            return;
        }
        const auto target_domain = selected_domain.value_or(
            register_domain(local->second));
        if (is_two_state_domain(target_domain)
            && !is_two_state_domain(register_domain(*value))) {
            if (language_ == frontend::Language::SystemVerilog2017) {
                *value = convert_to_two_state(*value);
            } else {
                report(
                    "FSIM-ELAB-058",
                    "assignment to two-state local variable '"
                        + target_name
                        + "' requires an explicit conversion",
                    statement.span);
                return;
            }
        }
        if (language_ == frontend::Language::Vhdl2008
            && target_domain
                == frontend::ValueDomain::Integer
            && !has_selected_offset
            && !dynamic_selection
            && !dynamic_part_selection) {
            if (!is_integer_expression(statement.value)) {
                report(
                    "FSIM-ELAB-INTEGER-004",
                    "assignment to VHDL integer local '"
                        + target_name
                        + "' requires an integer-family expression",
                    statement.span);
                return;
            }
            const auto range = local_integer_ranges_.contains(target_name)
                ? local_integer_ranges_.at(target_name)
                : std::optional<frontend::IntegerRange> { };
            if (!validate_static_integer_assignment(
                    statement.value, range, statement.span)) {
                return;
            }
            emit_integer_check(*value, range);
        }
        if (language_ == frontend::Language::Vhdl2008
            && !has_selected_offset
            && !dynamic_selection
            && !dynamic_part_selection
            && contextual_target_type != nullptr
            && !contextual_target_type
                ->enumeration_literals.empty()) {
            if (!validate_static_enumeration_assignment(
                    statement.value,
                    *contextual_target_type,
                    statement.span)) {
                return;
            }
            emit_enumeration_check(
                *value, *contextual_target_type);
        }
        if (procedural_delay) {
            emit_debug_point(
                DebugPointKind::wait, statement.span);
            if (!lower_delay_wait(*statement.delay, statement.span)) {
                return;
            }
        }
        std::optional<RegisterId> replaced_vhdl_access;
        const auto reclaiming_vhdl_access = contextual_target_type != nullptr
            && contextual_target_type->vhdl_access
            && contextual_target_type->vhdl_access
                   ->reclaim_when_unreachable
            && !dynamic_part_selection
            && !dynamic_selection
            && !has_selected_offset;
        if (reclaiming_vhdl_access) {
            replaced_vhdl_access = allocate_register(
                contextual_target_type->vhdl_access->handle_width,
                frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(CopyRegister {
                *replaced_vhdl_access, local->second });
        }
        if (dynamic_part_selection) {
            process_.operations.emplace_back(DynamicPartInsert {
                local->second,
                local->second,
                *value,
                *dynamic_part_selection });
        } else if (dynamic_selection) {
            process_.operations.emplace_back(DynamicInsert {
                local->second,
                local->second,
                *value,
                *dynamic_selection });
        } else if (has_selected_offset) {
            process_.operations.emplace_back(Insert {
                local->second,
                local->second,
                *value,
                selected_offset });
        } else {
            process_.operations.emplace_back(
                CopyRegister { local->second, *value });
        }
        if (replaced_vhdl_access) {
            if (const auto heap = vhdl_access_heap(
                    *contextual_target_type, statement.target.span)) {
                emit_vhdl_access_reclamation(
                    *contextual_target_type,
                    *heap,
                    *replaced_vhdl_access);
            }
        }
        if (selected_member_reference) {
            for (const auto& context :
                selected_member_reference->unions) {
                if (context.payload_width > context.member_width) {
                    const auto padding_width = context.payload_width - context.member_width;
                    const auto padding = allocate_register(
                        padding_width,
                        frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        padding,
                        PackedLogic4(
                            padding_width, Logic4::zero) });
                    process_.operations.emplace_back(Insert {
                        local->second,
                        local->second,
                        padding,
                        static_cast<std::uint32_t>(
                            context.payload_offset
                            + context.member_width) });
                }
                if (context.tag_width != 0) {
                    const auto tag = allocate_register(
                        context.tag_width,
                        frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        tag,
                        unsigned_value(
                            context.tag, context.tag_width) });
                    process_.operations.emplace_back(Insert {
                        local->second,
                        local->second,
                        tag,
                        static_cast<std::uint32_t>(
                            context.tag_offset) });
                }
            }
        }
        return;
    }
    if (statement.vhdl_unaffected) {
        return;
    }
    if (statement.vhdl_delay_mechanism
        && statement.vhdl_waveform.size() > 1) {
        std::vector<ProjectedWaveformElement> waveform;
        waveform.reserve(statement.vhdl_waveform.size());
        const auto* target_type = visible_type(target_name);
        const auto target_domain = selected_domain.value_or(
            target_type != nullptr
                ? target_type->domain
                : design_.signal_info_[signal->second]
                      .source_domain);
        std::optional<runtime::SimulationTick> previous_delay;
        for (const auto& element : statement.vhdl_waveform) {
            if (!element.disconnect
                && !validate_vhdl_composite_assignment(
                    contextual_target_type, element.value)) {
                return;
            }
            const auto value = element.disconnect
                ? std::optional<RegisterId> { load_disconnect() }
                : lower_expression(
                      element.value,
                      target_width,
                      contextual_target_type);
            if (!value) {
                return;
            }
            if (register_width(*value) != target_width) {
                report(
                    "FSIM-ELAB-047",
                    "assignment width mismatch in VHDL waveform for '"
                        + target_name + "'",
                    element.span);
                return;
            }
            if (is_two_state_domain(target_domain)
                && !is_two_state_domain(
                    register_domain(*value))) {
                report(
                    "FSIM-ELAB-050",
                    "assignment to two-state target '"
                        + target_name
                        + "' requires an explicit conversion from a "
                          "four-/nine-state expression",
                    element.span);
                return;
            }
            if (!element.disconnect
                && language_ == frontend::Language::Vhdl2008
                && target_domain
                    == frontend::ValueDomain::Integer
                && !has_selected_offset
                && !dynamic_selection) {
                if (!is_integer_expression(element.value)) {
                    report(
                        "FSIM-ELAB-INTEGER-004",
                        "VHDL integer waveform for '"
                            + target_name
                            + "' requires integer-family expressions",
                        element.span);
                    return;
                }
                const auto& range = target_type != nullptr
                    ? target_type->integer_range
                    : design_.signal_info_[signal->second]
                          .integer_range;
                if (!validate_static_integer_assignment(
                        element.value, range, element.span)) {
                    return;
                }
                emit_integer_check(*value, range);
            }
            if (!element.disconnect
                && language_
                    == frontend::Language::Vhdl2008
                && !has_selected_offset
                && !dynamic_selection
                && contextual_target_type != nullptr
                && !contextual_target_type
                    ->enumeration_literals.empty()) {
                if (!validate_static_enumeration_assignment(
                        element.value,
                        *contextual_target_type,
                        element.span)) {
                    return;
                }
                emit_enumeration_check(
                    *value, *contextual_target_type);
            }
            const auto delay = element.delay ? element.delay->magnitude : 0;
            if (previous_delay && delay <= *previous_delay) {
                report(
                    "FSIM-ELAB-055",
                    "VHDL waveform-element delays must be strictly "
                    "ascending",
                    element.span);
                return;
            }
            previous_delay = delay;
            waveform.push_back({ *value, delay });
        }
        const auto mode = *statement.vhdl_delay_mechanism
                == frontend::VhdlDelayMechanism::Transport
            ? ProjectedDelayMode::transport
            : ProjectedDelayMode::inertial;
        const auto first_delay = waveform.front().delay;
        const auto rejection = statement.vhdl_rejection_limit
            ? statement.vhdl_rejection_limit->magnitude
            : (mode == ProjectedDelayMode::inertial
                      ? first_delay
                      : 0);
        if (rejection > first_delay) {
            report(
                "FSIM-ELAB-055",
                "a VHDL rejection limit cannot exceed the first "
                "waveform element delay",
                statement.span);
            return;
        }
        if (dynamic_part_selection) {
            process_.operations.emplace_back(
                WriteProjectedWaveformDynamicSlice {
                    signal->second,
                    std::move(waveform),
                    DynamicIndex {
                        dynamic_part_selection->base,
                        dynamic_part_selection->left,
                        dynamic_part_selection->right,
                        dynamic_part_selection->base_offset,
                        true },
                    rejection,
                    mode });
        } else if (dynamic_selection) {
            process_.operations.emplace_back(
                WriteProjectedWaveformDynamicSlice {
                    signal->second,
                    std::move(waveform),
                    *dynamic_selection,
                    rejection,
                    mode });
        } else if (has_selected_offset) {
            process_.operations.emplace_back(
                WriteProjectedWaveformSlice {
                    signal->second,
                    std::move(waveform),
                    selected_offset,
                    rejection,
                    mode });
        } else {
            process_.operations.emplace_back(
                WriteProjectedWaveform {
                    signal->second,
                    std::move(waveform),
                    rejection,
                    mode });
        }
        return;
    }
    std::optional<RegisterId> captured;
    if (statement.procedural_update_kind
        != frontend::ProceduralUpdateKind::None) {
        const auto* target_type = visible_type(target_name);
        const auto target_domain = selected_domain.value_or(
            target_type != nullptr
                ? target_type->domain
                : design_.signal_info_[signal->second]
                      .source_domain);
        const auto whole = allocate_register(
            whole_width,
            target_type != nullptr
                ? target_type->domain
                : design_.signal_info_[signal->second]
                      .source_domain);
        process_.operations.emplace_back(
            ReadSignal { whole, signal->second });
        if (dynamic_part_selection) {
            captured = allocate_register(
                target_width, target_domain);
            process_.operations.emplace_back(DynamicPartSelect {
                *captured,
                whole,
                dynamic_part_selection->base,
                dynamic_part_selection->left,
                dynamic_part_selection->right,
                dynamic_part_selection->width,
                dynamic_part_selection->increasing,
                dynamic_part_selection->source_descending,
                is_two_state_domain(target_domain),
                dynamic_part_selection->base_offset });
        } else if (dynamic_selection) {
            captured = allocate_register(
                target_width, target_domain);
            process_.operations.emplace_back(DynamicExtract {
                *captured,
                whole,
                *dynamic_selection });
        } else if (has_selected_offset) {
            captured = allocate_register(
                target_width, target_domain);
            process_.operations.emplace_back(Extract {
                *captured,
                whole,
                selected_offset,
                static_cast<std::uint32_t>(target_width) });
        } else {
            captured = whole;
        }
    }
    const bool disconnect = statement.vhdl_waveform.size() == 1
        && statement.vhdl_waveform.front().disconnect;
    auto value = disconnect
        ? std::optional<RegisterId> { load_disconnect() }
        : captured
        ? lower_procedural_update_value(
              statement,
              *captured,
              target_width,
              contextual_target_type)
        : lower_expression(
              statement.value,
              target_width,
              contextual_target_type);
    if (!value) {
        return;
    }
    if (register_width(*value) != target_width
        && language_ != frontend::Language::Vhdl2008) {
        *value = resize_register(
            *value,
            target_width,
            is_signed_expression(statement.value));
    }
    if (register_width(*value) != target_width) {
        report(
            "FSIM-ELAB-047",
            "assignment width mismatch: target '"
                + target_name + "' is "
                + std::to_string(target_width)
                + " bits but the expression is "
                + std::to_string(register_width(*value)) + " bits",
            statement.span);
        return;
    }
    const auto* target_type = visible_type(target_name);
    if (language_ == frontend::Language::Vhdl2008
        && target_type != nullptr
        && target_type->vhdl_physical) {
        const auto source_type = vhdl_expression_type(statement.value);
        const bool physical_literal = statement.value.kind == ExpressionKind::Call
            && statement.value.text.starts_with(
                "@vhdl-physical:");
        const bool physical_operation = statement.value.kind == ExpressionKind::Binary;
        if (source_type && source_type->vhdl_physical
            && source_type->nominal_type
                != target_type->nominal_type) {
            report(
                "FSIM-ELAB-VHPHYSICAL-009",
                "assignment between distinct nominal physical types "
                "is not legal",
                statement.span);
            return;
        }
        if ((!source_type || !source_type->vhdl_physical)
            && !physical_literal && !physical_operation) {
            report(
                "FSIM-ELAB-VHPHYSICAL-010",
                "a physical target requires a same-type value, "
                "physical literal, physical operation, or explicit "
                "type conversion",
                statement.span);
            return;
        }
    }
    const auto target_domain = selected_domain.value_or(
        target_type != nullptr
            ? target_type->domain
            : design_.signal_info_[signal->second]
                  .source_domain);
    if (is_two_state_domain(target_domain)
        && !is_two_state_domain(register_domain(*value))
        && !(statement.value.kind == ExpressionKind::Call
            && statement.value.text == "@sv-null")) {
        if (language_ == frontend::Language::SystemVerilog2017) {
            *value = convert_to_two_state(*value);
        } else {
            report(
                "FSIM-ELAB-050",
                "assignment to two-state target '"
                    + target_name
                    + "' requires an explicit conversion from a "
                      "four-/nine-state expression",
                statement.span);
            return;
        }
    }
    if (!disconnect
        && language_ == frontend::Language::Vhdl2008
        && target_domain == frontend::ValueDomain::Integer
        && (target_type == nullptr || !target_type->vhdl_array)
        && !has_selected_offset
        && !dynamic_selection
        && !dynamic_part_selection) {
        if (!is_integer_expression(statement.value)) {
            report(
                "FSIM-ELAB-INTEGER-004",
                "assignment to VHDL integer signal '"
                    + target_name
                    + "' requires an integer-family expression",
                statement.span);
            return;
        }
        const auto& range = target_type != nullptr
            ? target_type->integer_range
            : design_.signal_info_[signal->second]
                  .integer_range;
        if (!validate_static_integer_assignment(
                statement.value, range, statement.span)) {
            return;
        }
        emit_integer_check(*value, range);
    }
    if (!disconnect
        && language_ == frontend::Language::Vhdl2008
        && !has_selected_offset
        && !dynamic_selection
        && !dynamic_part_selection
        && contextual_target_type != nullptr
        && !contextual_target_type
            ->enumeration_literals.empty()) {
        if (!validate_static_enumeration_assignment(
                statement.value,
                *contextual_target_type,
                statement.span)) {
            return;
        }
        emit_enumeration_check(
            *value, *contextual_target_type);
    }
    if (procedural_delay
        && statement.assignment_kind == AssignmentKind::Blocking) {
        emit_debug_point(
            DebugPointKind::wait, statement.span);
        if (!lower_delay_wait(*statement.delay, statement.span)) {
            return;
        }
        if (dynamic_part_selection) {
            process_.operations.emplace_back(
                WriteBlockingDynamicPartSlice {
                    signal->second,
                    *value,
                    *dynamic_part_selection });
        } else if (dynamic_selection) {
            process_.operations.emplace_back(
                WriteBlockingDynamicSlice {
                    signal->second,
                    *value,
                    *dynamic_selection });
        } else if (has_selected_offset) {
            process_.operations.emplace_back(
                WriteBlockingSlice {
                    signal->second, *value, selected_offset });
        } else {
            process_.operations.emplace_back(
                WriteBlocking { signal->second, *value });
        }
        return;
    }
    if (statement.vhdl_delay_mechanism) {
        const auto delay = statement.delay ? statement.delay->magnitude : 0;
        const auto mode = *statement.vhdl_delay_mechanism
                == frontend::VhdlDelayMechanism::Transport
            ? ProjectedDelayMode::transport
            : ProjectedDelayMode::inertial;
        const auto rejection = statement.vhdl_rejection_limit
            ? statement.vhdl_rejection_limit->magnitude
            : (mode == ProjectedDelayMode::inertial
                      ? delay
                      : 0);
        if (rejection > delay) {
            report(
                "FSIM-ELAB-055",
                "a VHDL rejection limit cannot exceed the first "
                "waveform element delay",
                statement.span);
            return;
        }
        if (dynamic_part_selection) {
            process_.operations.emplace_back(
                WriteProjectedDynamicSlice {
                    signal->second,
                    *value,
                    DynamicIndex {
                        dynamic_part_selection->base,
                        dynamic_part_selection->left,
                        dynamic_part_selection->right,
                        dynamic_part_selection->base_offset,
                        true },
                    delay,
                    rejection,
                    mode });
        } else if (dynamic_selection) {
            process_.operations.emplace_back(
                WriteProjectedDynamicSlice {
                    signal->second,
                    *value,
                    *dynamic_selection,
                    delay,
                    rejection,
                    mode });
        } else if (has_selected_offset) {
            process_.operations.emplace_back(
                WriteProjectedSlice {
                    signal->second,
                    *value,
                    selected_offset,
                    delay,
                    rejection,
                    mode });
        } else {
            process_.operations.emplace_back(
                WriteProjected {
                    signal->second,
                    *value,
                    delay,
                    rejection,
                    mode });
        }
    } else if (statement.delay) {
        if (statement.assignment_kind
            == AssignmentKind::Continuous) {
            const auto rise = statement.delay->magnitude;
            const auto fall = statement.delay->additional_values.empty()
                ? rise
                : statement.delay->additional_values[0].magnitude;
            const auto turnoff = statement.delay->additional_values.size() < 2
                ? std::min(rise, fall)
                : statement.delay->additional_values[1].magnitude;
            const TransitionDelays delays {
                rise, fall, turnoff
            };
            if (dynamic_part_selection) {
                process_.operations.emplace_back(
                    WriteInertialDynamicPartSlice {
                        signal->second,
                        *value,
                        *dynamic_part_selection,
                        delays });
            } else if (dynamic_selection) {
                process_.operations.emplace_back(
                    WriteInertialDynamicSlice {
                        signal->second,
                        *value,
                        *dynamic_selection,
                        delays });
            } else if (has_selected_offset) {
                process_.operations.emplace_back(
                    WriteInertialSlice {
                        signal->second,
                        *value,
                        selected_offset,
                        delays });
            } else {
                process_.operations.emplace_back(
                    WriteInertial {
                        signal->second, *value, delays });
            }
        } else if (dynamic_part_selection) {
            process_.operations.emplace_back(
                WriteAfterDynamicPartSlice {
                    signal->second,
                    *value,
                    *dynamic_part_selection,
                    statement.delay->magnitude });
        } else if (dynamic_selection) {
            process_.operations.emplace_back(
                WriteAfterDynamicSlice {
                    signal->second,
                    *value,
                    *dynamic_selection,
                    statement.delay->magnitude });
        } else if (has_selected_offset) {
            process_.operations.emplace_back(
                WriteAfterSlice {
                    signal->second,
                    *value,
                    selected_offset,
                    statement.delay->magnitude });
        } else {
            process_.operations.emplace_back(WriteAfter {
                signal->second,
                *value,
                statement.delay->magnitude });
        }
    } else if (
        statement.assignment_kind == AssignmentKind::Blocking) {
        if (dynamic_part_selection) {
            process_.operations.emplace_back(
                WriteBlockingDynamicPartSlice {
                    signal->second,
                    *value,
                    *dynamic_part_selection });
        } else if (dynamic_selection) {
            process_.operations.emplace_back(
                WriteBlockingDynamicSlice {
                    signal->second,
                    *value,
                    *dynamic_selection });
        } else if (has_selected_offset) {
            process_.operations.emplace_back(WriteBlockingSlice {
                signal->second, *value, selected_offset });
        } else {
            process_.operations.emplace_back(
                WriteBlocking { signal->second, *value });
        }
    } else {
        if (dynamic_part_selection) {
            process_.operations.emplace_back(
                WriteUpdateDynamicPartSlice {
                    signal->second,
                    *value,
                    *dynamic_part_selection });
        } else if (dynamic_selection) {
            process_.operations.emplace_back(
                WriteUpdateDynamicSlice {
                    signal->second,
                    *value,
                    *dynamic_selection });
        } else if (has_selected_offset) {
            process_.operations.emplace_back(WriteUpdateSlice {
                signal->second, *value, selected_offset });
        } else {
            process_.operations.emplace_back(
                WriteUpdate { signal->second, *value });
        }
    }
    if (selected_member_reference) {
        const auto write_metadata =
            [&](const RegisterId metadata,
                const std::uint32_t offset) {
                if (statement.assignment_kind
                    == AssignmentKind::Blocking) {
                    process_.operations.emplace_back(
                        WriteBlockingSlice {
                            signal->second, metadata, offset });
                } else {
                    process_.operations.emplace_back(
                        WriteUpdateSlice {
                            signal->second, metadata, offset });
                }
            };
        for (const auto& context :
            selected_member_reference->unions) {
            if (context.payload_width > context.member_width) {
                const auto padding_width = context.payload_width - context.member_width;
                const auto padding = allocate_register(
                    padding_width,
                    frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    padding,
                    PackedLogic4(
                        padding_width, Logic4::zero) });
                write_metadata(
                    padding,
                    static_cast<std::uint32_t>(
                        context.payload_offset
                        + context.member_width));
            }
            if (context.tag_width != 0) {
                const auto tag = allocate_register(
                    context.tag_width,
                    frontend::ValueDomain::Bit2);
                process_.operations.emplace_back(LoadConstant {
                    tag,
                    unsigned_value(context.tag, context.tag_width) });
                write_metadata(
                    tag,
                    static_cast<std::uint32_t>(context.tag_offset));
            }
        }
    }
}

} // namespace fsim::elaboration
