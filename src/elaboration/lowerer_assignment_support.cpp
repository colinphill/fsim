// SPDX-License-Identifier: Apache-2.0
#include "lowerer_internal.hpp"
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

bool Lowerer::lower_special_assignment_target(const Statement& statement)
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
            return true;
        }
        lower_concatenated_assignment(normalized);
        return true;
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
            return true;
        }
        Statement normalized = statement;
        const auto slice = normalized.target.operands.front();
        std::vector<Expression> targets(
            std::next(normalized.target.operands.begin()),
            normalized.target.operands.end());
        for (auto& target : targets) {
            if (!normalize_aggregate_target(
                    normalize_aggregate_target, target)) {
                return true;
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
        return true;
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
                return true;
            }
            if (statement.assignment_kind != AssignmentKind::Blocking
                || statement.procedural_assignment_control
                    != frontend::ProceduralAssignmentControl::None) {
                report(
                    "FSIM-ELAB-SVEVENT-010",
                    "named-event alias assignment must be blocking and time-free",
                    statement.span);
                return true;
            }
            process_.operations.emplace_back(
                EventAlias {
                    target->second,
                    null_event ? SignalId { } : source->second,
                    !null_event });
            return true;
        } else if (source_type != nullptr
            && source_type->spelling == "event") {
            report(
                "FSIM-ELAB-SVEVENT-009",
                "named-event assignment requires an event-variable target",
                statement.span);
            return true;
        }
    }
    if (statement.target.kind == ExpressionKind::Concatenation) {
        lower_concatenated_assignment(statement);
        return true;
    }
    if (lower_class_assignment(statement)) {
        return true;
    }
    if (lower_vhdl_access_assignment(statement)) {
        return true;
    }
    return false;
}

} // namespace fsim::elaboration
