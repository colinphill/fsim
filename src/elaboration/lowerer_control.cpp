// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;



    void Lowerer::lower_loop(const Statement& statement) {
        if (statement.loop_runtime) {
            if (!statement.loop_variable.empty()
                && statement.target.valid()
                && statement.value.valid()) {
                lower_runtime_for(statement);
            } else {
                lower_runtime_loop(statement);
            }
            return;
        }
        const auto assigns_loop_parameter =
            [&](const auto& self,
                const std::vector<Statement>& statements) -> bool {
            for (const auto& child : statements) {
                if (child.kind == StatementKind::Assignment) {
                    const Expression* target = &child.target;
                    while ((target->kind == ExpressionKind::Index
                            || target->kind
                                == ExpressionKind::Slice)
                           && !target->operands.empty()) {
                        target = &target->operands.front();
                    }
                    if (target->kind == ExpressionKind::Identifier
                        && target->text
                            == statement.loop_variable) {
                        return true;
                    }
                }
                if (self(self, child.statements)
                    || self(self, child.else_statements)) {
                    return true;
                }
                for (const auto& alternative :
                     child.case_alternatives) {
                    if (self(self, alternative.statements)) {
                        return true;
                    }
                }
            }
            return false;
        };
        if (assigns_loop_parameter(
                assigns_loop_parameter,
                statement.statements)) {
            report(
                "FSIM-ELAB-074",
                "sequential for-loop index '"
                    + statement.loop_variable
                    + (language_ == frontend::Language::Vhdl2008
                           ? "' is an implicit constant and cannot be "
                             "assigned"
                           : "' is statically substituted by this bounded "
                             "slice and cannot be assigned in the body"),
                statement.span);
            return;
        }

        std::optional<std::int64_t> initial;
        std::optional<std::int64_t> limit;
        bool loop_descending =
            statement.loop_descending;
        const bool attribute_range =
            language_ == frontend::Language::Vhdl2008
            && statement.loop_initial.kind
                == ExpressionKind::Call
            && (statement.loop_initial.text == "'range"
                || statement.loop_initial.text
                    == "'reverse_range");
        if (attribute_range) {
            const auto range =
                vhdl_array_attribute_range(
                    statement.loop_initial, true);
            if (!range) {
                return;
            }
            const bool reverse =
                statement.loop_initial.text
                    == "'reverse_range";
            initial =
                reverse ? range->right : range->left;
            limit =
                reverse ? range->left : range->right;
            loop_descending =
                reverse ? !range->descending
                        : range->descending;
        } else {
            std::string error;
            initial = evaluate_constant_expression(
                statement.loop_initial, {}, error);
            if (!initial) {
                report(
                    "FSIM-ELAB-071",
                    "cannot evaluate sequential for-loop initial bound: "
                        + error,
                    statement.loop_initial.span);
                return;
            }
            error.clear();
            limit = evaluate_constant_expression(
                statement.loop_limit, {}, error);
            if (!limit) {
                if (statement.loop_repeat) {
                    lower_runtime_repeat(statement);
                    return;
                }
                report(
                    "FSIM-ELAB-072",
                    "cannot evaluate sequential for-loop final bound: "
                        + error,
                    statement.loop_limit.span);
                return;
            }
        }
        if (statement.loop_repeat && *limit < 0) {
            return;
        }

        const bool null_range =
            loop_descending
                ? (statement.loop_limit_exclusive
                       ? *initial <= *limit
                       : *initial < *limit)
                : (statement.loop_limit_exclusive
                       ? *initial >= *limit
                       : *initial > *limit);
        if (null_range) {
            return;
        }
        const auto distance =
            index_distance(*initial, *limit);
        constexpr std::uint64_t maximum_iterations = 1'000'000;
        const bool too_many_iterations =
            statement.loop_limit_exclusive
                ? distance > maximum_iterations
                : distance >= maximum_iterations;
        if (too_many_iterations) {
            report(
                "FSIM-ELAB-073",
                "sequential for loop exceeds the bounded "
                "1,000,000-iteration elaboration limit",
                statement.span);
            return;
        }

        ConstantDomainEnvironment domains;
        if (!statement.loop_variable.empty()) {
            domains.emplace(
                statement.loop_variable,
                frontend::ValueDomain::Integer);
        }
        auto value = *initial;
        std::size_t count = 0;
        const auto in_range = [&]() {
            if (loop_descending) {
                return statement.loop_limit_exclusive
                    ? value > *limit
                    : value >= *limit;
            }
            return statement.loop_limit_exclusive
                ? value < *limit
                : value <= *limit;
        };
        loop_controls_.push_back({});
        loop_controls_.back().label =
            statement.loop_label;
        while (in_range()) {
            if (count++ == maximum_iterations) {
                report(
                    "FSIM-ELAB-073",
                    "sequential for loop exceeds the bounded "
                    "1,000,000-iteration elaboration limit",
                    statement.span);
                loop_controls_.pop_back();
                return;
            }
            auto body = statement.statements;
            ConstantEnvironment environment;
            if (!statement.loop_variable.empty()) {
                environment.emplace(
                    statement.loop_variable, value);
            }
            substitute_parameters(
                body,
                environment,
                domains,
                diagnostics_,
                language_);
            lower_statements(body);
            const auto next_iteration =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            for (const auto jump :
                 loop_controls_.back().continue_jumps) {
                process_.operations[jump] =
                    Jump{next_iteration};
            }
            loop_controls_.back().continue_jumps.clear();
            if (!statement.loop_limit_exclusive
                && value == *limit) {
                break;
            }
            value += loop_descending ? -1 : 1;
        }
        auto loop_control = std::move(loop_controls_.back());
        loop_controls_.pop_back();
        const auto end =
            static_cast<InstructionIndex>(
                process_.operations.size());
        for (const auto jump : loop_control.break_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }



    void Lowerer::lower_runtime_for(const Statement& statement) {
        bool inline_local = false;
        const auto erase_inline_local = [&]() {
            if (!inline_local) {
                return;
            }
            locals_.erase(statement.loop_variable);
            local_signed_.erase(statement.loop_variable);
            local_ranges_.erase(statement.loop_variable);
            local_integer_ranges_.erase(statement.loop_variable);
            local_members_.erase(statement.loop_variable);
        };
        if (statement.loop_variable_declared) {
            if (locals_.contains(statement.loop_variable)) {
                report(
                    "FSIM-ELAB-SVLOOP-001",
                    "runtime procedural for-loop variable shadows an "
                    "active local with the same name",
                    statement.target.span);
                return;
            }
            const auto index = allocate_register(
                32, frontend::ValueDomain::Bit2);
            locals_.emplace(statement.loop_variable, index);
            local_signed_.emplace(statement.loop_variable, true);
            local_ranges_.emplace(
                statement.loop_variable, std::nullopt);
            local_integer_ranges_.emplace(
                statement.loop_variable, std::nullopt);
            local_members_.emplace(
                statement.loop_variable,
                std::vector<frontend::PackedMember>{});
            inline_local = true;
        }

        Statement initializer;
        initializer.kind = StatementKind::Assignment;
        initializer.assignment_kind = AssignmentKind::Blocking;
        initializer.target = statement.target;
        initializer.value = statement.loop_initial;
        initializer.span = statement.loop_initial.span;
        lower_assignment(initializer);

        const auto loop_start = static_cast<InstructionIndex>(
            process_.operations.size());
        emit_debug_point(DebugPointKind::statement, statement.span);
        const auto condition = lower_condition(
            statement.condition,
            "FSIM-ELAB-SVLOOP-002",
            "procedural for-loop");
        if (!condition) {
            erase_inline_local();
            return;
        }
        const auto branch_index = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch{
            *condition, 0, 0, UnknownBranchPolicy::when_false});
        const auto body_start = static_cast<InstructionIndex>(
            process_.operations.size());
        loop_controls_.push_back({});
        lower_statements(statement.statements);
        auto loop_control = std::move(loop_controls_.back());
        loop_controls_.pop_back();
        const auto update_start = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump : loop_control.continue_jumps) {
            process_.operations[jump] = Jump{update_start};
        }
        Statement update;
        update.kind = StatementKind::Assignment;
        update.assignment_kind = AssignmentKind::Blocking;
        update.target = statement.target;
        update.value = statement.value;
        update.span = statement.value.span;
        lower_assignment(update);
        process_.operations.emplace_back(Jump{loop_start});
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch_index] = Branch{
            *condition,
            body_start,
            end,
            UnknownBranchPolicy::when_false};
        for (const auto jump : loop_control.break_jumps) {
            process_.operations[jump] = Jump{end};
        }
        erase_inline_local();
    }



    void Lowerer::lower_runtime_repeat(const Statement& statement) {
        auto limit = lower_expression(statement.loop_limit, 32);
        if (!limit) {
            report(
                "FSIM-ELAB-075",
                "runtime repeat count is not a supported integral value",
                statement.loop_limit.span);
            return;
        }
        if (register_width(*limit) != 32) {
            *limit = resize_register(
                *limit, 32,
                is_signed_expression(statement.loop_limit));
        }
        const auto counter = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant{counter, unsigned_value(0, 32)});
        const auto one = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LoadConstant{one, unsigned_value(1, 32)});

        const auto loop_start = static_cast<InstructionIndex>(
            process_.operations.size());
        emit_debug_point(DebugPointKind::statement, statement.span);
        const auto condition = allocate_register(
            1,
            is_two_state_domain(register_domain(*limit))
                ? frontend::ValueDomain::Bit2
                : frontend::ValueDomain::Logic4);
        process_.operations.emplace_back(Binary{
            is_signed_expression(statement.loop_limit)
                ? BinaryOperator::less_signed
                : BinaryOperator::less_unsigned,
            condition,
            counter,
            *limit});
        const auto branch_index = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch{
            condition, 0, 0, UnknownBranchPolicy::when_false});
        const auto body_start = static_cast<InstructionIndex>(
            process_.operations.size());
        loop_controls_.push_back({});
        lower_statements(statement.statements);
        auto loop_control = std::move(loop_controls_.back());
        loop_controls_.pop_back();
        const auto increment_start = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump : loop_control.continue_jumps) {
            process_.operations[jump] = Jump{increment_start};
        }
        const auto next = allocate_register(
            32, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary{
            BinaryOperator::add_signed,
            next,
            counter,
            one});
        process_.operations.emplace_back(CopyRegister{counter, next});
        process_.operations.emplace_back(Jump{loop_start});
        const auto end = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations[branch_index] = Branch{
            condition,
            body_start,
            end,
            UnknownBranchPolicy::when_false};
        for (const auto jump : loop_control.break_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }



    void Lowerer::lower_runtime_loop(const Statement& statement) {
        const auto loop_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        emit_debug_point(
            DebugPointKind::statement, statement.span);
        if (statement.loop_post_test) {
            loop_controls_.push_back({});
            loop_controls_.back().label =
                statement.loop_label;
            lower_statements(statement.statements);
            auto loop_control =
                std::move(loop_controls_.back());
            loop_controls_.pop_back();
            const auto condition_start =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            for (const auto jump :
                 loop_control.continue_jumps) {
                process_.operations[jump] =
                    Jump{condition_start};
            }
            const auto condition = lower_condition(
                statement.condition,
                "FSIM-ELAB-048",
                "do-while");
            if (!condition) {
                return;
            }
            const auto branch_index =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            const auto unknown_policy =
                language_ == frontend::Language::Vhdl2008
                    ? UnknownBranchPolicy::error
                    : UnknownBranchPolicy::when_false;
            process_.operations.emplace_back(
                Branch{
                    *condition,
                    loop_start,
                    branch_index + 1,
                    unknown_policy});
            const auto end =
                static_cast<InstructionIndex>(
                    process_.operations.size());
            for (const auto jump :
                 loop_control.break_jumps) {
                process_.operations[jump] = Jump{end};
            }
            return;
        }
        const auto condition = lower_condition(
            statement.condition, "FSIM-ELAB-077", "while");
        if (!condition) {
            return;
        }
        const auto branch_index =
            static_cast<InstructionIndex>(
                process_.operations.size());
        const auto unknown_policy =
            language_ == frontend::Language::Vhdl2008
                ? UnknownBranchPolicy::error
                : UnknownBranchPolicy::when_false;
        process_.operations.emplace_back(
            Branch{*condition, 0, 0, unknown_policy});
        const auto body_start =
            static_cast<InstructionIndex>(
                process_.operations.size());
        loop_controls_.push_back(
            LoopControlContext{
                loop_start,
                {},
                {},
                statement.loop_label});
        lower_statements(statement.statements);
        auto loop_control = std::move(loop_controls_.back());
        loop_controls_.pop_back();
        process_.operations.emplace_back(Jump{loop_start});
        const auto end =
            static_cast<InstructionIndex>(
                process_.operations.size());
        process_.operations[branch_index] = Branch{
            *condition, body_start, end, unknown_policy};
        for (const auto jump : loop_control.break_jumps) {
            process_.operations[jump] = Jump{end};
        }
    }



    void Lowerer::lower_loop_control(
        const Statement& statement, const bool is_break) {
        if (loop_controls_.empty()) {
            report(
                "FSIM-ELAB-078",
                std::string{"a "}
                    + (is_break ? "break/exit" : "continue/next")
                    + " statement has no enclosing loop",
                statement.span);
            return;
        }
        auto context = std::prev(loop_controls_.end());
        if (!statement.loop_control_label.empty()) {
            const auto found = std::find_if(
                loop_controls_.rbegin(),
                loop_controls_.rend(),
                [&](const LoopControlContext& candidate) {
                    return candidate.label
                        == statement.loop_control_label;
                });
            if (found == loop_controls_.rend()) {
                report(
                    "FSIM-ELAB-080",
                    "loop-control target '"
                        + statement.loop_control_label
                        + "' has no enclosing loop",
                    statement.span);
                return;
            }
            context = std::prev(found.base());
        }
        const auto jump =
            static_cast<InstructionIndex>(
                process_.operations.size());
        if (!is_break
            && context->continue_target) {
            process_.operations.emplace_back(
                Jump{*context->continue_target});
            return;
        }
        process_.operations.emplace_back(Jump{0});
        auto& targets =
            is_break
                ? context->break_jumps
                : context->continue_jumps;
        targets.push_back(jump);
    }



    std::optional<Lowerer::PackedMemberReference> Lowerer::packed_member_reference(
        const std::string_view name) const {
        const auto separator = name.find('.');
        if (separator == std::string_view::npos
            || separator == 0
            || separator + 1 >= name.size()) {
            return std::nullopt;
        }
        const auto base = std::string{name.substr(0, separator)};
        auto member_name = name.substr(separator + 1);
        const std::vector<frontend::PackedMember>* members = nullptr;
        if (const auto local = local_members_.find(base);
            local != local_members_.end()) {
            members = &local->second;
        } else if (const auto signal = signals_.find(base);
                   signal != signals_.end()) {
            members =
                &design_.signal_info_[signal->second].packed_members;
        }
        if (members == nullptr) {
            return std::nullopt;
        }
        std::uint64_t offset = 0;
        for (;;) {
            const auto dot = member_name.find('.');
            const auto segment = member_name.substr(0, dot);
            const auto member = std::find_if(
                members->begin(),
                members->end(),
                [&](const frontend::PackedMember& candidate) {
                    return candidate.name == segment;
                });
            if (member == members->end()
                || member->lsb_offset
                    > std::numeric_limits<std::uint64_t>::max() - offset) {
                return std::nullopt;
            }
            offset += member->lsb_offset;
            if (dot == std::string_view::npos) {
                return PackedMemberReference{base, &*member, offset};
            }
            if (member->nested_types.empty()
                || member->nested_types.front().packed_members.empty()) {
                return std::nullopt;
            }
            members = &member->nested_types.front().packed_members;
            member_name.remove_prefix(dot + 1);
        }
    }



    [[nodiscard]] RegisterId Lowerer::widen_enumeration_ordinal(
        const RegisterId source) {
        const auto source_width = register_width(source);
        const auto destination =
            allocate_register(
                32, frontend::ValueDomain::Integer);
        if (source_width == 32) {
            process_.operations.emplace_back(
                CopyRegister{destination, source});
            return destination;
        }
        const auto padding =
            allocate_register(
                32 - source_width,
                frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LoadConstant{
            padding,
            PackedLogic4(
                32 - source_width, Logic4::zero)});
        process_.operations.emplace_back(Concatenate{
            destination,
            {padding, source},
            32});
        return destination;
    }



    [[nodiscard]] RegisterId Lowerer::narrow_enumeration_ordinal(
        const RegisterId source,
        const frontend::Type& type) {
        const auto width =
            static_cast<std::size_t>(
                type.width().value_or(1));
        const auto destination =
            allocate_register(width, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Extract{
            destination,
            source,
            0,
            static_cast<std::uint32_t>(width)});
        return destination;
    }



    std::optional<RegisterId> Lowerer::lower_enumeration_attribute(
        const Expression& expression,
        const frontend::Type& type) {
        const auto width_value = type.width();
        if (!width_value || *width_value == 0
            || *width_value > 32
            || type.enumeration_literals.empty()
            || type.enumeration_literals.size()
                > static_cast<std::size_t>(
                    std::numeric_limits<std::int32_t>::max())) {
            report(
                "FSIM-ELAB-VHENUMATTR-001",
                "enumeration attribute prefix '"
                    + type.spelling
                    + "' has no executable ordinal range",
                expression.span);
            return std::nullopt;
        }
        const auto width =
            static_cast<std::size_t>(*width_value);
        const auto count = static_cast<std::int32_t>(
            type.enumeration_literals.size());
        const auto range =
            type.enumeration_range.value_or(
                frontend::EnumerationRange{
                    0,
                    static_cast<std::int64_t>(count - 1),
                    false});
        const auto range_left =
            static_cast<std::int32_t>(range.left);
        const auto range_right =
            static_cast<std::int32_t>(range.right);
        const auto range_low =
            std::min(range_left, range_right);
        const auto range_high =
            std::max(range_left, range_right);
        const auto require_arity =
            [&](const std::size_t expected) {
              if (expression.operands.size() != expected + 1U) {
                  report(
                      "FSIM-ELAB-VHENUMATTR-001",
                      expression.text + " on enumeration type '"
                          + type.spelling + "' requires "
                          + std::to_string(expected)
                          + (expected == 1
                                 ? " argument"
                                 : " arguments"),
                      expression.span);
                  return false;
              }
              return true;
            };
        if (expression.text == "'left"
            || expression.text == "'right"
            || expression.text == "'low"
            || expression.text == "'high") {
            if (!require_arity(0)) {
                return std::nullopt;
            }
            const auto ordinal =
                expression.text == "'left"
                    ? range_left
                : expression.text == "'right"
                    ? range_right
                : expression.text == "'low"
                    ? range_low
                    : range_high;
            const auto destination =
                allocate_register(
                    width, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(LoadConstant{
                destination,
                unsigned_value(
                    static_cast<std::uint64_t>(ordinal),
                    width)});
            return destination;
        }
        if (expression.text == "'length") {
            if (!require_arity(0)) {
                return std::nullopt;
            }
            const auto destination =
                allocate_register(
                    32, frontend::ValueDomain::Integer);
            process_.operations.emplace_back(LoadConstant{
                destination,
                integer_value(
                    range_high - range_low + 1)});
            return destination;
        }
        if (expression.text == "'ascending") {
            if (!require_arity(0)) {
                return std::nullopt;
            }
            const auto destination =
                allocate_register(
                    1, frontend::ValueDomain::Boolean);
            process_.operations.emplace_back(LoadConstant{
                destination,
                PackedLogic4(
                    1,
                    range.descending
                        ? Logic4::zero
                        : Logic4::one)});
            return destination;
        }

        const bool position = expression.text == "'pos";
        const bool value = expression.text == "'val";
        const bool successor =
            expression.text == "'succ"
            || (expression.text == "'leftof"
                && range.descending)
            || (expression.text == "'rightof"
                && !range.descending);
        const bool predecessor =
            expression.text == "'pred"
            || (expression.text == "'leftof"
                && !range.descending)
            || (expression.text == "'rightof"
                && range.descending);
        if (!position && !value && !successor && !predecessor) {
            report(
                "FSIM-ELAB-VHENUMATTR-001",
                "attribute '" + expression.text
                    + "' is not defined for enumeration type '"
                    + type.spelling + "'",
                expression.span);
            return std::nullopt;
        }
        if (!require_arity(1)) {
            return std::nullopt;
        }
        if (value) {
            if (const auto static_value =
                    constant_index(expression.operands[1]);
                static_value
                && (*static_value < range_low
                    || *static_value > range_high)) {
                report(
                    "FSIM-ELAB-VHENUMATTR-002",
                    "'val argument "
                        + std::to_string(*static_value)
                        + " is outside enumeration type '"
                        + type.spelling + "'",
                    expression.operands[1].span);
                return std::nullopt;
            }
            if (!is_integer_expression(
                    expression.operands[1])) {
                report(
                    "FSIM-ELAB-VHENUMATTR-001",
                    "'val requires an integer-family argument",
                    expression.operands[1].span);
                return std::nullopt;
            }
            const auto argument =
                lower_expression(
                    expression.operands[1], 32);
            if (!argument || register_width(*argument) != 32) {
                report(
                    "FSIM-ELAB-VHENUMATTR-001",
                    "'val argument does not have the portable "
                    "32-bit integer representation",
                    expression.operands[1].span);
                return std::nullopt;
            }
            process_.operations.emplace_back(IntegerCheck{
                *argument, range_low, range_high});
            return narrow_enumeration_ordinal(
                *argument, type);
        }

        const auto argument =
            [&]() -> std::optional<RegisterId> {
              const auto& argument_expression =
                  expression.operands[1];
              if (argument_expression.kind
                      == ExpressionKind::IntegerLiteral
                  || argument_expression.kind
                      == ExpressionKind::BooleanLiteral
                  || argument_expression.kind
                      == ExpressionKind::StringLiteral) {
                  report(
                      "FSIM-ELAB-VHENUMATTR-001",
                      expression.text
                          + " requires a value of enumeration type '"
                          + type.spelling + "'",
                      argument_expression.span);
                  return std::nullopt;
              }
              if (argument_expression.kind
                      == ExpressionKind::Identifier
                  && !vhdl_enumeration_ordinal(
                      argument_expression, type)) {
                  const auto* argument_type =
                      object_type(argument_expression.text);
                  if (argument_type != nullptr
                      && argument_type
                          ->enumeration_literals.empty()) {
                      report(
                          "FSIM-ELAB-VHENUMATTR-001",
                          expression.text
                              + " requires a value of enumeration type '"
                              + type.spelling + "'",
                          argument_expression.span);
                      return std::nullopt;
                  }
              }
              return lower_expression(
                  argument_expression, width, &type);
            }();
        if (!argument) {
            return std::nullopt;
        }
        const auto ordinal =
            widen_enumeration_ordinal(*argument);
        if (position) {
            process_.operations.emplace_back(IntegerCheck{
                ordinal, range_low, range_high});
            return ordinal;
        }
        const auto static_ordinal =
            vhdl_enumeration_ordinal(
                expression.operands[1], type);
        const auto lower =
            successor ? range_low : range_low + 1;
        const auto upper =
            successor ? range_high - 1 : range_high;
        if (lower > upper
            || (static_ordinal
                && (*static_ordinal < lower
                    || *static_ordinal > upper))) {
            report(
                "FSIM-ELAB-VHENUMATTR-002",
                expression.text + " argument has no result inside "
                    "enumeration type '" + type.spelling + "'",
                expression.operands[1].span);
            return std::nullopt;
        }
        process_.operations.emplace_back(
            IntegerCheck{ordinal, lower, upper});
        const auto one =
            allocate_register(
                32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(
            LoadConstant{one, integer_value(1)});
        const auto adjusted =
            allocate_register(
                32, frontend::ValueDomain::Integer);
        process_.operations.emplace_back(IntegerBinary{
            successor
                ? IntegerBinaryOperator::add
                : IntegerBinaryOperator::subtract,
            adjusted,
            ordinal,
            one});
        return narrow_enumeration_ordinal(
            adjusted, type);
    }



    std::optional<RegisterId> Lowerer::lower_vhdl_array_aggregate(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type& expected_type) {
        const auto aggregate_width = expected_type.width();
        if (!expected_type.vhdl_array
            || expected_type.vhdl_array->dimensions.empty()
            || !expected_type.vhdl_array->dimensions.front().range
            || expected_type.vhdl_array->dimensions.front().null
            || expected_type.vhdl_array->element_types.empty()
            || !aggregate_width
            || *aggregate_width != expected_width
            || expected_width == 0
            || expected_width - 1
                > std::numeric_limits<std::uint32_t>::max()
            || expression.aggregate_choices.size()
                != expression.operands.size()
            || expression.aggregate_choice_expressions.size()
                != expression.operands.size()) {
            report(
                "FSIM-ELAB-VHARRAYAGG-002",
                "VHDL array aggregate association metadata or contextual "
                "layout is inconsistent",
                expression.span);
            return std::nullopt;
        }

        const auto& contextual_dimension =
            expected_type.vhdl_array->dimensions.front();
        const auto element_width = contextual_dimension.stride;
        if (element_width == 0
            || expected_width % element_width != 0
            || element_width
                > std::numeric_limits<std::uint32_t>::max()) {
            report(
                "FSIM-ELAB-VHARRAYAGG-002",
                "VHDL array aggregate element stride is inconsistent "
                "with its contextual layout",
                expression.span);
            return std::nullopt;
        }
        const auto element_count = expected_width / element_width;
        frontend::Type element_type;
        if (expected_type.vhdl_array->dimensions.size() == 1U) {
            element_type =
                expected_type.vhdl_array->element_types.front();
        } else {
            element_type = expected_type;
            auto& array = *element_type.vhdl_array;
            array.dimensions.erase(array.dimensions.begin());
            array.index_subtype = array.dimensions.front().index_subtype;
            array.index_span = array.dimensions.front().index_span;
            array.index_base_range =
                array.dimensions.front().index_base_range;
            array.unconstrained = false;
            array.flat_width = element_width;
            element_type.vhdl_array_constraints.clear();
            element_type.packed_range_expression.reset();
            if (array.dimensions.size() == 1U
                && !array.dimensions.front().null) {
                const auto& range = *array.dimensions.front().range;
                element_type.packed_range = frontend::PackedRange{
                    range.left, range.right, range.descending};
            } else {
                element_type.packed_range = frontend::PackedRange{
                    static_cast<std::int64_t>(element_width - 1U),
                    0,
                    true};
            }
        }
        const auto destination = allocate_register(
            expected_width, expected_type.domain);
        process_.operations.emplace_back(LoadConstant{
            destination,
            default_packed_value(
                expected_type, expected_width)});
        std::vector<bool> assigned(element_count, false);
        std::optional<std::size_t> others_index;
        std::size_t positional_index = 0;
        bool valid = true;

        const auto source_index =
            [&](const std::size_t offset) {
              const auto distance =
                  static_cast<std::int64_t>(offset);
              return contextual_dimension.range->descending
                  ? contextual_dimension.range->right + distance
                  : contextual_dimension.range->right - distance;
            };
        const auto insert_offset =
            [&](const std::size_t offset,
                const Expression& value,
                const frontend::SourceSpan& span) {
              if (offset >= assigned.size()) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-004",
                      "VHDL array aggregate has too many positional "
                      "associations",
                      span);
                  valid = false;
                  return;
              }
              if (assigned[offset]) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-004",
                      "VHDL array aggregate index "
                          + std::to_string(source_index(offset))
                          + " is assigned more than once",
                      span);
                  valid = false;
                  return;
              }
              if (!element_type.packed_members.empty()
                  && value.kind != ExpressionKind::Aggregate) {
                  const auto* actual =
                      value.kind == ExpressionKind::Identifier
                          ? object_type(value.text)
                          : nullptr;
                  if (actual == nullptr
                      || actual->packed_members.empty()
                      || actual->nominal_type
                          != element_type.nominal_type) {
                      report(
                          "FSIM-ELAB-VHARRAYAGG-006",
                          "VHDL array aggregate record element requires "
                          "the exact nominal record subtype",
                          span);
                      valid = false;
                      return;
                  }
              }
              const auto lowered =
                  lower_expression(
                      value, element_width, &element_type);
              if (!lowered) {
                  valid = false;
                  return;
              }
              if (register_width(*lowered) != element_width) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-006",
                      "VHDL array aggregate element expects "
                          + std::to_string(element_width)
                          + " packed bits but its association has "
                          + std::to_string(register_width(*lowered))
                          + " bits",
                      span);
                  valid = false;
                  return;
              }
              if (is_two_state_domain(element_type.domain)
                  && !is_two_state_domain(
                      register_domain(*lowered))) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-007",
                      "two-state VHDL array aggregate element requires an "
                      "explicit conversion from a four- or nine-state "
                      "value",
                      span);
                  valid = false;
                  return;
              }
              process_.operations.emplace_back(Insert{
                  destination,
                  destination,
                  *lowered,
                  static_cast<std::uint32_t>(
                      offset * element_width)});
              assigned[offset] = true;
            };
        const auto insert_index =
            [&](const std::int64_t index,
                const Expression& value,
                const frontend::SourceSpan& span) {
              const auto& range = *contextual_dimension.range;
              const auto lower =
                  std::min(range.left, range.right);
              const auto upper =
                  std::max(range.left, range.right);
              if (index < lower || index > upper) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-003",
                      "VHDL array aggregate index "
                          + std::to_string(index)
                          + " is outside the contextual range "
                          + std::to_string(range.left)
                          + (range.descending
                                 ? " downto "
                                 : " to ")
                          + std::to_string(range.right),
                      span);
                  valid = false;
                  return;
              }
              const auto offset =
                  index_distance(index, range.right);
              if (offset
                  > std::numeric_limits<std::size_t>::max()) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-002",
                      "VHDL array aggregate index offset is not "
                      "representable by the packed runtime",
                      span);
                  valid = false;
                  return;
              }
              insert_offset(
                  static_cast<std::size_t>(offset),
                  value,
                  span);
            };

        for (std::size_t association = 0;
             association < expression.operands.size();
             ++association) {
            const auto& choices =
                expression.aggregate_choice_expressions[
                    association];
            const auto& value =
                expression.operands[association];
            if (choices.empty()) {
                if (positional_index >= element_count) {
                    insert_offset(
                        element_count, value, value.span);
                } else {
                    insert_offset(
                        element_count - 1
                            - positional_index,
                        value,
                        value.span);
                }
                ++positional_index;
                continue;
            }
            for (const auto& choice : choices) {
                if (choice.kind == ExpressionKind::Identifier
                    && choice.text == "others") {
                    if (choices.size() != 1) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-008",
                            "others must be the only choice in a VHDL "
                            "array aggregate association",
                            choice.span);
                        valid = false;
                    }
                    if (others_index) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-004",
                            "VHDL array aggregate has more than one "
                            "others association",
                            choice.span);
                        valid = false;
                    } else {
                        others_index = association;
                    }
                    continue;
                }
                if (choice.kind == ExpressionKind::Binary
                    && (choice.text == "to"
                        || choice.text == "downto")) {
                    if (choice.operands.size() != 2) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-002",
                            "VHDL array aggregate range choice metadata "
                            "is inconsistent",
                            choice.span);
                        valid = false;
                        continue;
                    }
                    const auto left =
                        static_integer_value(
                            choice.operands[0]);
                    const auto right =
                        static_integer_value(
                            choice.operands[1]);
                    if (!left || !right) {
                        report(
                            "FSIM-ELAB-VHARRAYAGG-003",
                            "VHDL array aggregate range choices require "
                            "locally static integer bounds",
                            choice.span);
                        valid = false;
                        continue;
                    }
                    const bool descending =
                        choice.text == "downto";
                    const bool null =
                        descending ? *left < *right
                                   : *left > *right;
                    if (null) {
                        continue;
                    }
                    auto index = *left;
                    while (true) {
                        insert_index(index, value, choice.span);
                        if (index == *right) {
                            break;
                        }
                        index += descending ? -1 : 1;
                    }
                    continue;
                }
                const auto index =
                    static_integer_value(choice);
                if (!index) {
                    report(
                        "FSIM-ELAB-VHARRAYAGG-003",
                        "VHDL array aggregate choices require locally "
                        "static integer indices",
                        choice.span);
                    valid = false;
                    continue;
                }
                insert_index(*index, value, choice.span);
            }
        }

        if (others_index) {
            for (std::size_t offset = 0;
                 offset < assigned.size();
                 ++offset) {
                if (!assigned[offset]) {
                    insert_offset(
                        offset,
                        expression.operands[*others_index],
                        expression.operands[*others_index].span);
                }
            }
        }
        for (std::size_t offset = 0;
             offset < assigned.size();
             ++offset) {
            if (assigned[offset]) {
                continue;
            }
            report(
                "FSIM-ELAB-VHARRAYAGG-005",
                "VHDL array aggregate is missing index "
                    + std::to_string(source_index(offset)),
                expression.span);
            valid = false;
        }
        return valid
            ? std::optional<RegisterId>{destination}
            : std::nullopt;
    }

} // namespace fsim::elaboration
