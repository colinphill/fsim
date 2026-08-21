// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;
using namespace elaboration_detail;

[[maybe_unused]] constexpr std::string_view
    kRetiredExactCaseInsideProfileDiagnostic = "FSIM-ELAB-SVCASEINSIDE-004";

void Lowerer::lower_loop(const Statement& statement)
{
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
    bool loop_descending = statement.loop_descending;
    const frontend::Type* scalar_range_type = nullptr;
    const bool attribute_range = language_ == frontend::Language::Vhdl2008
        && statement.loop_initial.kind
            == ExpressionKind::Call
        && (statement.loop_initial.text == "'range"
            || statement.loop_initial.text
                == "'reverse_range");
    if (attribute_range) {
        std::optional<frontend::PackedRange> range;
        const auto& attribute = statement.loop_initial;
        if (!attribute.operands.empty()
            && attribute.operands.front().kind
                == ExpressionKind::Identifier) {
            const auto* candidate = visible_type_mark(
                attribute.operands.front().text);
            if (candidate != nullptr
                && !is_vhdl_array_like(*candidate)
                && candidate->packed_members.empty()
                && (candidate->domain
                        == frontend::ValueDomain::Integer
                    || candidate->domain
                        == frontend::ValueDomain::Boolean
                    || candidate->domain
                        == frontend::ValueDomain::Bit2
                    || !candidate->enumeration_literals.empty())) {
                scalar_range_type = candidate;
                if (attribute.operands.size() != 1) {
                    report(
                        "FSIM-ELAB-VHSCALARATTR-001",
                        attribute.text
                            + " on a scalar type takes no dimension",
                        attribute.span);
                    return;
                }
                if (!candidate->enumeration_literals.empty()) {
                    const auto selected = candidate->enumeration_range.value_or(
                        frontend::EnumerationRange {
                            0,
                            static_cast<std::int64_t>(
                                candidate->enumeration_literals.size()
                                - 1U),
                            false });
                    range = frontend::PackedRange {
                        selected.left,
                        selected.right,
                        selected.descending
                    };
                } else if (candidate->domain
                    == frontend::ValueDomain::Integer) {
                    const auto selected = candidate->integer_range.value_or(
                        frontend::IntegerRange {
                            std::numeric_limits<std::int32_t>::min(),
                            std::numeric_limits<std::int32_t>::max(),
                            false });
                    range = frontend::PackedRange {
                        selected.left,
                        selected.right,
                        selected.descending
                    };
                } else {
                    range = frontend::PackedRange { 0, 1, false };
                }
            }
        }
        if (!range) {
            range = vhdl_array_attribute_range(
                statement.loop_initial, true);
        }
        if (!range) {
            return;
        }
        const bool reverse = statement.loop_initial.text
            == "'reverse_range";
        initial = reverse ? range->right : range->left;
        limit = reverse ? range->left : range->right;
        loop_descending = reverse ? !range->descending
                                  : range->descending;
    } else {
        std::string error;
        initial = evaluate_constant_expression(
            statement.loop_initial, { }, error);
        if (!initial) {
            if (language_ != frontend::Language::Vhdl2008
                && statement.target.valid()
                && statement.value.valid()
                && statement.condition.valid()) {
                lower_runtime_for(statement);
                return;
            }
            report(
                "FSIM-ELAB-071",
                "cannot evaluate sequential for-loop initial bound: "
                    + error,
                statement.loop_initial.span);
            return;
        }
        error.clear();
        limit = evaluate_constant_expression(
            statement.loop_limit, { }, error);
        if (!limit) {
            if (statement.loop_repeat) {
                lower_runtime_repeat(statement);
                return;
            }
            if (language_ != frontend::Language::Vhdl2008
                && statement.target.valid()
                && statement.value.valid()
                && statement.condition.valid()) {
                lower_runtime_for(statement);
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

    const bool null_range = loop_descending
        ? (statement.loop_limit_exclusive
                  ? *initial <= *limit
                  : *initial < *limit)
        : (statement.loop_limit_exclusive
                  ? *initial >= *limit
                  : *initial > *limit);
    if (null_range) {
        return;
    }
    const auto distance = index_distance(*initial, *limit);
    constexpr std::uint64_t maximum_iterations = 1'000'000;
    const bool too_many_iterations = statement.loop_limit_exclusive
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
            scalar_range_type == nullptr
                ? ConstantTypeInfo { frontend::ValueDomain::Integer }
                : ConstantTypeInfo {
                      scalar_range_type->domain,
                      !scalar_range_type
                          ->enumeration_literals.empty(),
                      scalar_range_type->nominal_type });
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
    loop_controls_.push_back({ });
    loop_controls_.back().label = statement.loop_label;
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
        const auto next_iteration = static_cast<InstructionIndex>(
            process_.operations.size());
        for (const auto jump :
            loop_controls_.back().continue_jumps) {
            process_.operations[jump] = Jump { next_iteration };
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
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    for (const auto jump : loop_control.break_jumps) {
        process_.operations[jump] = Jump { end };
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
        update.target = statement.loop_update_target.valid()
            ? statement.loop_update_target
            : statement.target;
        update.value = statement.value;
        update.span = statement.value.span;
        lower_assignment(update);
        for (const auto& additional : statement.loop_updates) {
            lower_assignment(additional);
        }
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
        const frontend::Type* owner_type = object_type(base);
        const std::vector<frontend::PackedMember>* members = nullptr;
        if (owner_type != nullptr
            && !owner_type->packed_members.empty()) {
            members = &owner_type->packed_members;
        } else if (const auto local = local_members_.find(base);
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
        std::vector<PackedMemberReference::UnionContext> unions;
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
            if (owner_type != nullptr
                && (owner_type->packed_aggregate
                        == frontend::PackedAggregateKind::Union
                    || owner_type->packed_aggregate
                        == frontend::PackedAggregateKind::TaggedUnion)) {
                const auto owner_width = owner_type->width();
                std::uint64_t payload_width = 0;
                for (const auto& candidate : *members) {
                    const auto width = candidate.width();
                    if (!width) {
                        return std::nullopt;
                    }
                    payload_width = std::max(payload_width, *width);
                }
                const bool tagged = owner_type->packed_aggregate
                    == frontend::PackedAggregateKind::TaggedUnion;
                if (!owner_width
                    || *owner_width < payload_width
                    || (tagged && *owner_width == payload_width)) {
                    return std::nullopt;
                }
                const auto member_width = member->width();
                if (!member_width) {
                    return std::nullopt;
                }
                unions.push_back({
                    offset,
                    payload_width,
                    *member_width,
                    tagged ? offset + payload_width : 0,
                    tagged ? *owner_width - payload_width : 0,
                    static_cast<std::uint64_t>(
                        std::distance(members->begin(), member))});
            }
            offset += member->lsb_offset;
            if (dot == std::string_view::npos) {
                return PackedMemberReference{
                    base, &*member, offset, std::move(unions)};
            }
            if (member->nested_types.empty()
                || member->nested_types.front().packed_members.empty()) {
                return std::nullopt;
            }
            owner_type = &member->nested_types.front();
            members = &owner_type->packed_members;
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



    std::optional<RegisterId> Lowerer::lower_vhdl_array_aggregate(
        const Expression& expression,
        const std::size_t expected_width,
        const frontend::Type& expected_type) {
        if (!expected_type.vhdl_array && expected_type.packed_range
            && expected_type.packed_members.empty()
            && (expected_type.domain == frontend::ValueDomain::Bit2
                || expected_type.domain
                    == frontend::ValueDomain::Logic9)) {
            auto contextual_type = expected_type;
            const auto& packed = *expected_type.packed_range;
            frontend::VhdlArrayDimension dimension;
            dimension.index_subtype = "integer";
            dimension.range = frontend::IntegerRange{
                packed.left, packed.right, packed.descending};
            dimension.null = packed.descending
                ? packed.left < packed.right
                : packed.left > packed.right;
            dimension.stride = 1;
            frontend::Type element_type;
            element_type.domain = expected_type.domain;
            element_type.spelling =
                expected_type.domain == frontend::ValueDomain::Logic9
                ? "std_logic" : "bit";
            frontend::VhdlArrayInfo array;
            array.index_subtype = "integer";
            array.element_spelling = element_type.spelling;
            array.element_domain = element_type.domain;
            array.flat_width = expected_type.width();
            array.dimensions.push_back(std::move(dimension));
            array.element_types.push_back(std::move(element_type));
            contextual_type.vhdl_array = std::move(array);
            return lower_vhdl_array_aggregate(
                expression, expected_width, contextual_type);
        }
        const auto aggregate_width = expected_type.width();
        if (!expected_type.vhdl_array
            || expected_type.vhdl_array->dimensions.empty()
            || !expected_type.vhdl_array->dimensions.front().range
            || expected_type.vhdl_array->element_types.empty()
            || !aggregate_width
            || *aggregate_width != expected_width
            || (expected_width != 0
                && expected_width - 1
                    > std::numeric_limits<std::uint32_t>::max())
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
        if (expected_width != 0) {
            process_.operations.emplace_back(LoadConstant{
                destination,
                default_packed_value(
                    expected_type, expected_width)});
        }
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
              const auto actual_type = vhdl_expression_type(value);
              if (actual_type
                  && !vhdl_callable_type_matches(
                      element_type, *actual_type)) {
                  const bool state_loss =
                      is_two_state_domain(element_type.domain)
                      && !is_two_state_domain(actual_type->domain);
                  report(
                      state_loss ? "FSIM-ELAB-VHARRAYAGG-007"
                                 : "FSIM-ELAB-VHARRAYAGG-009",
                      state_loss
                          ? "two-state VHDL array aggregate element requires "
                                "an explicit conversion from a four- or "
                                "nine-state value"
                          : "VHDL array aggregate element requires its exact "
                                "contextual subtype",
                      span);
                  valid = false;
                  return;
              }
              if (!actual_type
                  && (element_type.vhdl_array
                      || !element_type.packed_members.empty())
                  && !vhdl_expression_matches_type(
                      value, element_type)) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-009",
                      "VHDL array aggregate element requires its exact "
                      "contextual subtype",
                      span);
                  valid = false;
                  return;
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
              if (register_domain(*lowered) != element_type.domain) {
                  report(
                      "FSIM-ELAB-VHARRAYAGG-009",
                      "VHDL array aggregate element has an incompatible "
                      "state domain",
                      span);
                  valid = false;
                  return;
              }
              if (!element_type.enumeration_literals.empty()) {
                  emit_enumeration_check(*lowered, element_type);
              } else if (
                  element_type.domain
                      == frontend::ValueDomain::Integer
                  && !element_type.vhdl_physical
                  && element_type.nominal_type != "@builtin:time") {
                  emit_integer_check(
                      *lowered, element_type.integer_range);
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
                if (choice.kind == ExpressionKind::Call
                    && (choice.text == "'range"
                        || choice.text == "'reverse_range")) {
                    const auto selected =
                        vhdl_array_attribute_range(choice, true);
                    if (!selected) {
                        valid = false;
                        continue;
                    }
                    const bool reverse =
                        choice.text == "'reverse_range";
                    const auto left = reverse
                        ? selected->right : selected->left;
                    const auto right = reverse
                        ? selected->left : selected->right;
                    const bool descending = reverse
                        ? !selected->descending : selected->descending;
                    const bool null = descending
                        ? left < right : left > right;
                    if (null) {
                        continue;
                    }
                    auto index = left;
                    while (true) {
                        insert_index(index, value, choice.span);
                        if (index == right) {
                            break;
                        }
                        index += descending ? -1 : 1;
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

    void Lowerer::lower_concatenated_assignment(const Statement& statement)
    {
        if (statement.target.operands.empty()) {
            report(
                "FSIM-ELAB-SVCONCAT-001",
                "a concatenated assignment target requires at least one packed "
                "operand",
                statement.target.span);
            return;
        }
        if (statement.procedural_update_kind
            != frontend::ProceduralUpdateKind::None) {
            report(
                "FSIM-ELAB-SVCONCAT-001",
                "compound and increment/decrement updates do not accept a "
                "concatenated assignment target",
                statement.target.span);
            return;
        }
        std::vector<std::size_t> widths;
        widths.reserve(statement.target.operands.size());
        std::size_t total_width { };
        for (const auto& target : statement.target.operands) {
            const auto width = infer_width(target);
            if (!width || *width == 0
                || *width > std::numeric_limits<std::uint32_t>::max()
                || total_width > std::numeric_limits<std::uint32_t>::max() - *width) {
                report(
                    "FSIM-ELAB-SVCONCAT-001",
                    "every concatenated assignment target must have a static nonzero "
                    "packed width and the total width must fit SimIR",
                    target.span);
                return;
            }
            widths.push_back(*width);
            total_width += *width;
        }

        auto targets = statement.target.operands;
        std::vector<std::string> captured_target_names;
        const auto capture_target = [&](const auto& self,
                                        Expression& target) -> bool {
            if (target.kind == ExpressionKind::Concatenation) {
                for (auto& operand : target.operands) {
                    if (!self(self, operand)) {
                        return false;
                    }
                }
                return true;
            }
            if ((target.kind != ExpressionKind::Index
                    && target.kind != ExpressionKind::Slice)
                || target.operands.empty()) {
                return true;
            }
            if (!self(self, target.operands.front())) {
                return false;
            }
            const bool dynamic_index = target.kind == ExpressionKind::Index
                && target.operands.size() == 2
                && !static_integer_value(target.operands[1]);
            const bool dynamic_part_base = target.kind == ExpressionKind::Slice
                && target.operands.size() == 3
                && (target.text == "+:" || target.text == "-:")
                && !static_integer_value(target.operands[1]);
            if (!dynamic_index && !dynamic_part_base) {
                return true;
            }
            auto value = lower_expression(target.operands[1], 32);
            if (!value) {
                return false;
            }
            if (register_width(*value) != 32) {
                *value = resize_register(*value, 32, true);
            }
            const auto name = "$fsim_concat_target_"
                + std::to_string(process_.operations.size()) + "_"
                + std::to_string(captured_target_names.size());
            locals_.emplace(name, *value);
            captured_target_names.push_back(name);
            target.operands[1] = Expression {
                ExpressionKind::Identifier, name, { },
                target.operands[1].span
            };
            return true;
        };
        for (auto& target : targets) {
            if (!capture_target(capture_target, target)) {
                for (const auto& name : captured_target_names) {
                    locals_.erase(name);
                }
                return;
            }
        }
        const auto release_captured_targets = [&]() {
            for (const auto& name : captured_target_names) {
                locals_.erase(name);
            }
        };
        auto value = lower_expression(statement.value, total_width);
        if (!value) {
            release_captured_targets();
            return;
        }
        if (register_width(*value) != total_width) {
            *value = resize_register(
                *value, total_width, is_signed_expression(statement.value));
        }

        Statement child = statement;
        child.procedural_assignment_control = frontend::ProceduralAssignmentControl::None;
        child.procedural_assignment_repeat = false;
        child.sensitivities.clear();
        if (statement.procedural_assignment_control
            == frontend::ProceduralAssignmentControl::Event) {
            emit_debug_point(DebugPointKind::wait, statement.span);
            if (!emit_event_control_wait(statement)) {
                release_captured_targets();
                return;
            }
            child.delay.reset();
        } else if (statement.procedural_assignment_control
            == frontend::ProceduralAssignmentControl::Delay) {
            if (!statement.delay) {
                report(
                    "FSIM-ELAB-105",
                    "concatenated procedural delay assignment has no delay",
                    statement.span);
                release_captured_targets();
                return;
            }
            if (statement.assignment_kind == AssignmentKind::Blocking) {
                emit_debug_point(DebugPointKind::wait, statement.span);
                if (!lower_delay_wait(*statement.delay, statement.span)) {
                    release_captured_targets();
                    return;
                }
                child.delay.reset();
            }
        }

        std::vector<RegisterId> values(targets.size());
        std::uint32_t offset { };
        for (std::size_t reverse = targets.size();
            reverse != 0; --reverse) {
            const auto index = reverse - 1;
            values[index] = allocate_register(
                widths[index], register_domain(*value));
            process_.operations.emplace_back(Extract {
                values[index], *value, offset,
                static_cast<std::uint32_t>(widths[index]) });
            offset += static_cast<std::uint32_t>(widths[index]);
        }
        for (std::size_t index = 0;
            index < targets.size(); ++index) {
            const auto temporary = "$fsim_concat_value_"
                + std::to_string(process_.operations.size()) + "_"
                + std::to_string(index);
            locals_.emplace(temporary, values[index]);
            child.target = targets[index];
            child.value = Expression {
                ExpressionKind::Identifier, temporary, { }, statement.value.span
            };
            lower_assignment(child);
            locals_.erase(temporary);
        }
        release_captured_targets();
    }

void Lowerer::lower_if(const Statement& statement)
{
    if (const auto static_condition =
            static_integer_value(statement.condition)) {
            lower_statements(
                *static_condition != 0
                    ? statement.statements
                    : statement.else_statements);
            return;
        }
        const auto condition = lower_condition(
            statement.condition,
            statement.vhdl_conditional_assignment
                ? "FSIM-ELAB-092"
                : "FSIM-ELAB-048",
            statement.vhdl_conditional_assignment
                ? "conditional-assignment"
                : "if");
        if (!condition) {
            return;
        }
        const auto branch_index = static_cast<InstructionIndex>(process_.operations.size());
        const auto unknown_policy = language_ == frontend::Language::Vhdl2008
            ? UnknownBranchPolicy::error
            : UnknownBranchPolicy::when_false;
        process_.operations.emplace_back(
            Branch { *condition, 0, 0, unknown_policy });
        const auto true_start = static_cast<InstructionIndex>(process_.operations.size());
        lower_statements(statement.statements);
        const auto jump_index = static_cast<InstructionIndex>(process_.operations.size());
        process_.operations.emplace_back(Jump { 0 });
        const auto false_start = static_cast<InstructionIndex>(process_.operations.size());
        lower_statements(statement.else_statements);
        const auto end = static_cast<InstructionIndex>(process_.operations.size());
        process_.operations[branch_index] = Branch {
            *condition, true_start, false_start, unknown_policy
        };
        process_.operations[jump_index] = Jump { end };
    }

    bool Lowerer::is_bounded_case_pattern_constant(
        const Expression& expression) const
    {
        switch (expression.kind) {
        case ExpressionKind::IntegerLiteral:
        case ExpressionKind::BooleanLiteral:
        case ExpressionKind::LogicLiteral:
            return true;
        case ExpressionKind::Unary:
        case ExpressionKind::Update:
        case ExpressionKind::Binary:
        case ExpressionKind::Concatenation:
        case ExpressionKind::Replication:
            return std::ranges::all_of(
                expression.operands,
                [this](const Expression& operand) {
                    return is_bounded_case_pattern_constant(operand);
                });
        case ExpressionKind::Call:
            return (expression.text == "?:"
                       || expression.text == "$signed"
                       || expression.text == "$unsigned"
                       || expression.text == "$clog2")
                && std::ranges::all_of(
                    expression.operands,
                    [this](const Expression& operand) {
                        return is_bounded_case_pattern_constant(operand);
                    });
        default:
            return false;
        }
    }

    void Lowerer::lower_case(const Statement& statement)
    {
        if (statement.case_qualifier
            != frontend::CaseQualifier::None) {
            lower_qualified_case(statement);
            return;
        }
        BinaryOperator match_operation = BinaryOperator::case_equal;
        bool inside_matching = false;
        bool pattern_matching = false;
        bool vhdl_matching = false;
        switch (statement.case_match_kind) {
        case frontend::CaseMatchKind::Exact:
            break;
        case frontend::CaseMatchKind::WildcardZ:
            match_operation = BinaryOperator::casez_equal;
            break;
        case frontend::CaseMatchKind::WildcardXZ:
            match_operation = BinaryOperator::casex_equal;
            break;
        case frontend::CaseMatchKind::Inside:
            inside_matching = true;
            match_operation = BinaryOperator::wildcard_equal;
            break;
        case frontend::CaseMatchKind::Matches:
            pattern_matching = true;
            break;
        case frontend::CaseMatchKind::VhdlMatching:
            vhdl_matching = true;
            match_operation = BinaryOperator::vhdl_match_equal;
            break;
        default:
            report(
                "FSIM-ELAB-081",
                "case statement has an invalid matching mode",
                statement.span);
            return;
        }
        if (inside_matching
            && language_ != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-SVCASEINSIDE-001",
                "case inside matching requires SystemVerilog",
                statement.span);
            return;
        }
        if (pattern_matching
            && language_ != frontend::Language::SystemVerilog2017) {
            report(
                "FSIM-ELAB-SVMATCH-001",
                "case matches pattern matching requires SystemVerilog",
                statement.span);
            return;
        }
        if ((inside_matching || pattern_matching)
            && (is_container_expression(statement.condition)
                || is_string_expression(statement.condition)
                || statement.condition.kind == ExpressionKind::Aggregate)) {
            report(
                pattern_matching
                    ? "FSIM-ELAB-SVMATCH-002"
                    : "FSIM-ELAB-SVCASEINSIDE-002",
                pattern_matching
                    ? "bounded case matches requires a scalar integral "
                      "selector"
                    : "bounded case inside requires a scalar integral "
                      "selector",
                statement.condition.span);
            return;
        }
        const auto inferred_selector_width = infer_width(statement.condition);
        if ((inside_matching || pattern_matching)
            && (!inferred_selector_width
                || *inferred_selector_width == 0)) {
            report(
                pattern_matching
                    ? "FSIM-ELAB-SVMATCH-002"
                    : "FSIM-ELAB-SVCASEINSIDE-002",
                pattern_matching
                    ? "the case matches selector width is not statically "
                      "inferable"
                    : "the case inside selector width is not statically "
                      "inferable",
                statement.condition.span);
            return;
        }
        const auto selector_width = inferred_selector_width.value_or(
            std::size_t { 1 });
        const bool selector_signed = is_signed_expression(statement.condition);
        const auto* selector_type = statement.condition.kind
                == ExpressionKind::Identifier
            ? object_type(statement.condition.text)
            : nullptr;
        const auto selector = lower_expression(
            statement.condition,
            selector_width,
            selector_type != nullptr
                    && !selector_type
                        ->enumeration_literals.empty()
                ? selector_type
                : nullptr);
        if (!selector) {
            return;
        }
        const InsideIntegralOperand selector_operand {
            *selector,
            register_width(*selector),
            selector_signed
        };
        if (vhdl_matching
            && !validate_vhdl_matching_case(
                statement,
                selector_type,
                selector_width,
                register_domain(*selector))) {
            return;
        }
        if (!vhdl_matching
            && language_ == frontend::Language::Vhdl2008
            && !validate_vhdl_case_choices(
                statement,
                selector_type,
                selector_width,
                register_domain(*selector))) {
            return;
        }

        std::vector<InstructionIndex> exit_jumps;
        const frontend::CaseAlternative* default_alternative = nullptr;
        for (const auto& alternative : statement.case_alternatives) {
            if (alternative.is_default) {
                default_alternative = &alternative;
                continue;
            }

            if (inside_matching && alternative.choices.empty()) {
                report(
                    "FSIM-ELAB-SVCASEINSIDE-005",
                    "a case inside alternative requires at least one choice",
                    alternative.span);
                continue;
            }
            if (pattern_matching && alternative.choices.size() != 1) {
                report(
                    "FSIM-ELAB-SVMATCH-005",
                    "a case matches item requires exactly one pattern",
                    alternative.span);
                continue;
            }

            std::optional<decltype(locals_)> outer_locals;
            std::optional<decltype(local_signed_)> outer_signed;
            std::optional<decltype(local_ranges_)> outer_ranges;
            std::optional<decltype(local_integer_ranges_)> outer_integer_ranges;
            std::optional<decltype(local_members_)> outer_members;
            std::optional<decltype(local_types_)> outer_types;
            if (pattern_matching) {
                outer_locals = locals_;
                outer_signed = local_signed_;
                outer_ranges = local_ranges_;
                outer_integer_ranges = local_integer_ranges_;
                outer_members = local_members_;
                outer_types = local_types_;
            }

            std::vector<InstructionIndex> branches;
            for (const auto& choice : alternative.choices) {
                const Expression* match_choice = &choice;
                const Expression* match_guard = nullptr;
                if (pattern_matching
                    && choice.kind == ExpressionKind::Call
                    && choice.text == "@match-guard") {
                    if (choice.operands.size() != 2U) {
                        report(
                            "FSIM-ELAB-SVMATCH-005",
                            "guarded case matches HIR requires one pattern "
                            "and one guard",
                            choice.span);
                        continue;
                    }
                    match_choice = &choice.operands[0];
                    match_guard = &choice.operands[1];
                }
                std::optional<RegisterId> condition;
                std::vector<CasePatternBinding> pattern_bindings;
                if (language_ == frontend::Language::Vhdl2008
                    && match_choice->kind == ExpressionKind::Call
                    && (match_choice->text == "@vhdl-case-range-to"
                        || match_choice->text
                            == "@vhdl-case-range-downto")) {
                    condition = lower_vhdl_case_range_condition(
                        *match_choice,
                        *selector,
                        selector_type,
                        selector_signed);
                } else if (pattern_matching) {
                    auto pattern = lower_case_match_pattern(
                        *match_choice,
                        *selector,
                        selector_width,
                        selector_signed,
                        selector_type);
                    if (!pattern) {
                        continue;
                    }
                    condition = pattern->condition;
                    pattern_bindings = std::move(pattern->bindings);
                } else if (inside_matching
                    && match_choice->kind == ExpressionKind::Call
                    && match_choice->text == "@inside-range") {
                    if (match_choice->operands.size() != 2) {
                        report(
                            "FSIM-ELAB-SVCASEINSIDE-006",
                            "a case inside range requires exactly one low "
                            "and high bound",
                            match_choice->span);
                        continue;
                    }
                    const auto low = lower_inside_integral_operand(
                        match_choice->operands[0],
                        "FSIM-ELAB-SVCASEINSIDE-003",
                        "case inside range bounds must be integral expressions");
                    const auto high = lower_inside_integral_operand(
                        match_choice->operands[1],
                        "FSIM-ELAB-SVCASEINSIDE-003",
                        "case inside range bounds must be integral expressions");
                    if (!low || !high) {
                        continue;
                    }
                    const auto valid_operands = size_integral_comparison(*low, *high);
                    const auto low_operands = size_integral_comparison(selector_operand, *low);
                    const auto high_operands = size_integral_comparison(selector_operand, *high);
                    const auto domain = frontend::ValueDomain::Logic4;
                    const auto valid = allocate_register(1, domain);
                    const auto above_low = allocate_register(1, domain);
                    const auto below_high = allocate_register(1, domain);
                    const auto within_lower = allocate_register(1, domain);
                    condition = allocate_register(1, domain);
                    process_.operations.emplace_back(Binary {
                        valid_operands.signed_value
                            ? BinaryOperator::less_equal_signed
                            : BinaryOperator::less_equal_unsigned,
                        valid, valid_operands.lhs, valid_operands.rhs });
                    process_.operations.emplace_back(Binary {
                        low_operands.signed_value
                            ? BinaryOperator::greater_equal_signed
                            : BinaryOperator::greater_equal_unsigned,
                        above_low, low_operands.lhs, low_operands.rhs });
                    process_.operations.emplace_back(Binary {
                        high_operands.signed_value
                            ? BinaryOperator::less_equal_signed
                            : BinaryOperator::less_equal_unsigned,
                        below_high, high_operands.lhs, high_operands.rhs });
                    process_.operations.emplace_back(LogicalBinary {
                        LogicalBinaryOperator::logical_and,
                        within_lower, valid, above_low });
                    process_.operations.emplace_back(LogicalBinary {
                        LogicalBinaryOperator::logical_and,
                        *condition, within_lower, below_high });
                } else {
                    std::optional<RegisterId> choice_register;
                    auto comparison_selector = *selector;
                    if (inside_matching) {
                        const auto operand = lower_inside_integral_operand(
                            *match_choice,
                            "FSIM-ELAB-SVCASEINSIDE-003",
                            "case inside choices must be integral expressions");
                        if (operand) {
                            const auto comparison = size_integral_comparison(selector_operand, *operand);
                            comparison_selector = comparison.lhs;
                            choice_register = comparison.rhs;
                        }
                    } else {
                        choice_register = lower_expression(
                            *match_choice,
                            register_width(*selector),
                            selector_type != nullptr
                                    && !selector_type
                                        ->enumeration_literals.empty()
                                ? selector_type
                                : nullptr);
                    }
                    if (!choice_register) {
                        continue;
                    }
                    if (!inside_matching
                        && register_width(*choice_register)
                            != register_width(*selector)) {
                        report(
                            "FSIM-ELAB-063",
                            "case item width "
                                + std::to_string(
                                    register_width(*choice_register))
                                + " does not match selector width "
                                + std::to_string(register_width(*selector)),
                            match_choice->span);
                        continue;
                    }
                    condition = allocate_register(
                        1,
                        inside_matching
                            ? frontend::ValueDomain::Logic4
                            : frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        match_operation,
                        *condition,
                        comparison_selector,
                        *choice_register });
                }
                if (!condition) {
                    continue;
                }
                for (const auto& binding : pattern_bindings) {
                    locals_.insert_or_assign(binding.name, binding.value);
                    local_signed_.insert_or_assign(
                        binding.name, binding.signed_value);
                    local_ranges_.insert_or_assign(
                        binding.name, binding.packed_range);
                    local_integer_ranges_.insert_or_assign(
                        binding.name, binding.integer_range);
                    local_members_.insert_or_assign(
                        binding.name, binding.members);
                    if (binding.type != nullptr) {
                        local_types_.insert_or_assign(
                            binding.name, binding.type);
                    } else {
                        local_types_.erase(binding.name);
                    }
                }
                if (match_guard != nullptr) {
                    const auto guarded = allocate_register(
                        1, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        guarded, PackedLogic4(1, Logic4::zero) });
                    const auto match_branch = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations.emplace_back(Branch {
                        *condition,
                        0,
                        0,
                        UnknownBranchPolicy::when_false });
                    const auto guard_start = static_cast<InstructionIndex>(
                        process_.operations.size());
                    const auto guard = lower_condition(
                        *match_guard,
                        "FSIM-ELAB-SVMATCH-006",
                        "case matches guard");
                    if (!guard) {
                        continue;
                    }
                    const auto one = allocate_register(
                        1, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(LoadConstant {
                        one, PackedLogic4(1, Logic4::one) });
                    const auto definite = allocate_register(
                        1, frontend::ValueDomain::Bit2);
                    process_.operations.emplace_back(Binary {
                        BinaryOperator::case_equal,
                        definite,
                        *guard,
                        one });
                    process_.operations.emplace_back(CopyRegister {
                        guarded, definite });
                    const auto guard_end = static_cast<InstructionIndex>(
                        process_.operations.size());
                    process_.operations[match_branch] = Branch {
                        *condition,
                        guard_start,
                        guard_end,
                        UnknownBranchPolicy::when_false
                    };
                    condition = guarded;
                }
                branches.push_back(
                    static_cast<InstructionIndex>(
                        process_.operations.size()));
                process_.operations.emplace_back(Branch {
                    *condition,
                    0,
                    0,
                    UnknownBranchPolicy::when_false });
            }

            const auto skip_body = static_cast<InstructionIndex>(process_.operations.size());
            process_.operations.emplace_back(Jump { 0 });
            const auto body_start = static_cast<InstructionIndex>(process_.operations.size());
            lower_case_alternative(alternative);
            if (outer_locals) {
                locals_ = std::move(*outer_locals);
                local_signed_ = std::move(*outer_signed);
                local_ranges_ = std::move(*outer_ranges);
                local_integer_ranges_ = std::move(*outer_integer_ranges);
                local_members_ = std::move(*outer_members);
                local_types_ = std::move(*outer_types);
            }
            exit_jumps.push_back(
                static_cast<InstructionIndex>(
                    process_.operations.size()));
            process_.operations.emplace_back(Jump { 0 });
            const auto next_alternative = static_cast<InstructionIndex>(process_.operations.size());

            for (std::size_t index = 0; index < branches.size(); ++index) {
                const auto false_target = index + 1 < branches.size()
                    ? static_cast<InstructionIndex>(
                          branches[index] + 1)
                    : skip_body;
                const auto& operation = fsim::runtime::simir::operation_get<Branch>(process_.operations[branches[index]]);
                process_.operations[branches[index]] = Branch {
                    operation.condition,
                    body_start,
                    false_target,
                    UnknownBranchPolicy::when_false
                };
            }
            process_.operations[skip_body] = Jump { next_alternative };
        }

        if (default_alternative != nullptr) {
            lower_case_alternative(*default_alternative);
        }
        const auto end = static_cast<InstructionIndex>(process_.operations.size());
        for (const auto jump : exit_jumps) {
            process_.operations[jump] = Jump { end };
        }
    }

} // namespace fsim::elaboration
