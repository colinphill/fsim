// SPDX-License-Identifier: Apache-2.0
#include "elaborator_internal.hpp"

namespace fsim::elaboration {
using namespace runtime::simir;

[[nodiscard]] std::pair<
    std::vector<SignalId>,
    std::vector<runtime::simir::EdgeKind>>
Lowerer::resolve_wait_sensitivities(const Statement& statement) {
    std::vector<SignalId> signals;
    std::vector<runtime::simir::EdgeKind> edges;
    for (const auto& sensitivity : statement.sensitivities) {
        if (sensitivity.signal == "*"
            || sensitivity.expression.valid()) {
            if (language_ == frontend::Language::Vhdl2008
                && sensitivity.expression.valid()) {
                const auto dependency_start =
                    implicit_signal_dependencies_.size();
                const auto width = infer_width(sensitivity.expression);
                if (width && *width != 0) {
                    static_cast<void>(lower_expression(
                        sensitivity.expression, *width));
                }
                for (auto index = dependency_start;
                     index < implicit_signal_dependencies_.size();
                     ++index) {
                    signals.push_back(
                        implicit_signal_dependencies_[index]);
                    edges.push_back(runtime::simir::EdgeKind::any);
                }
                if (dependency_start
                    == implicit_signal_dependencies_.size()) {
                    report(
                        "FSIM-ELAB-VHATTR-006",
                        "a VHDL wait sensitivity attribute must denote an "
                        "implicit signal",
                        sensitivity.span);
                }
                continue;
            }
            std::set<std::string> dependencies;
            if (sensitivity.signal == "*") {
                collect_wildcard_identifiers(
                    statement.statements, dependencies);
                if (statement.kind == StatementKind::Assignment) {
                    collect_identifiers(statement.value, dependencies);
                }
            } else {
                collect_identifiers(
                    sensitivity.expression, dependencies);
            }
            for (const auto& dependency : dependencies) {
                if (locals_.contains(dependency)) {
                    continue;
                }
                if (const auto found = signals_.find(dependency);
                    found != signals_.end()) {
                    signals.push_back(found->second);
                    edges.push_back(
                        runtime::simir::EdgeKind::any);
                }
            }
            if (dependencies.empty() || signals.empty()) {
                report(
                    sensitivity.signal == "*"
                        ? "FSIM-ELAB-062"
                        : "FSIM-ELAB-SVEVENT-001",
                    sensitivity.signal == "*"
                        ? "dynamic wildcard event control has no readable "
                          "signal dependencies"
                        : "packed event expression has no readable signal "
                          "dependencies",
                    sensitivity.span);
            }
            continue;
        }
        const auto found = signals_.find(sensitivity.signal);
        if (found == signals_.end()) {
            report(
                "FSIM-ELAB-059",
                "unknown wait signal '" + sensitivity.signal + "'",
                sensitivity.span);
            continue;
        }
        if (sensitivity.edge != frontend::EdgeKind::Any
            && design_.signal_info_[found->second].width != 1) {
            report(
                "FSIM-ELAB-060",
                "dynamic edge-qualified wait signal '"
                    + sensitivity.signal + "' must be scalar",
                sensitivity.span);
            continue;
        }
        signals.push_back(found->second);
        auto edge = runtime::simir::EdgeKind::any;
        if (sensitivity.edge == frontend::EdgeKind::Positive) {
            edge = runtime::simir::EdgeKind::posedge;
        } else if (sensitivity.edge == frontend::EdgeKind::Negative) {
            edge = runtime::simir::EdgeKind::negedge;
        }
        edges.push_back(edge);
    }
    return {std::move(signals), std::move(edges)};
}

bool Lowerer::emit_event_control_wait(const Statement& statement) {
    if (!statement.procedural_assignment_repeat) {
        return emit_single_event_control_wait(statement);
    }
    auto limit = lower_expression(statement.loop_limit, 32);
    if (!limit) {
        report(
            "FSIM-ELAB-SVEVENT-004",
            "repeated event-control count is not an executable integral "
            "value",
            statement.loop_limit.span);
        return false;
    }
    if (register_width(*limit) != 32) {
        *limit = resize_register(
            *limit,
            32,
            is_signed_expression(statement.loop_limit));
    }
    const auto counter = allocate_register(
        32, frontend::ValueDomain::Bit2);
    const auto one = allocate_register(
        32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(
        LoadConstant{counter, unsigned_value(0, 32)});
    process_.operations.emplace_back(
        LoadConstant{one, unsigned_value(1, 32)});
    const auto loop_start = static_cast<InstructionIndex>(
        process_.operations.size());
    const auto active = allocate_register(
        1,
        is_two_state_domain(register_domain(*limit))
            ? frontend::ValueDomain::Bit2
            : frontend::ValueDomain::Logic4);
    process_.operations.emplace_back(Binary{
        is_signed_expression(statement.loop_limit)
            ? BinaryOperator::less_signed
            : BinaryOperator::less_unsigned,
        active,
        counter,
        *limit});
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        active, 0, 0, UnknownBranchPolicy::when_false});
    const auto wait_start = static_cast<InstructionIndex>(
        process_.operations.size());
    if (!emit_single_event_control_wait(statement)) {
        return false;
    }
    const auto next = allocate_register(
        32, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        BinaryOperator::add_signed,
        next,
        counter,
        one});
    process_.operations.emplace_back(
        CopyRegister{counter, next});
    process_.operations.emplace_back(Jump{loop_start});
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[branch] = Branch{
        active,
        wait_start,
        end,
        UnknownBranchPolicy::when_false};
    return true;
}

bool Lowerer::emit_single_event_control_wait(
    const Statement& statement) {
    const auto general = std::ranges::find_if(
        statement.sensitivities,
        [](const frontend::Sensitivity& sensitivity) {
            return sensitivity.expression.valid();
        });
    if (language_ == frontend::Language::Vhdl2008
        || general == statement.sensitivities.end()) {
        auto [signals, edges] = resolve_wait_sensitivities(statement);
        if (signals.empty() && !statement.delay) {
            return false;
        }
        WaitOn wait{std::move(signals), std::move(edges)};
        if (statement.delay) {
            wait.timeout = statement.delay->magnitude;
        }
        process_.operations.emplace_back(std::move(wait));
        return true;
    }

    struct EventExpressionState {
        Expression expression;
        frontend::EdgeKind edge{frontend::EdgeKind::Any};
        std::size_t width{};
        RegisterId baseline{};
    };
    std::vector<EventExpressionState> expressions;
    std::vector<SignalId> waited_signals;
    for (const auto& sensitivity : statement.sensitivities) {
        EventExpressionState state;
        state.edge = sensitivity.edge;
        if (sensitivity.expression.valid()) {
            state.expression = sensitivity.expression;
        } else {
            state.expression.kind = ExpressionKind::Identifier;
            state.expression.text = sensitivity.signal;
            state.expression.span = sensitivity.span;
        }
        const auto width = infer_width(state.expression);
        if (!width || *width == 0 || *width > 64
            || (state.edge != frontend::EdgeKind::Any && *width != 1)) {
            report(
                "FSIM-ELAB-SVEVENT-003",
                state.edge == frontend::EdgeKind::Any
                    ? "packed event expression must have an executable width "
                      "from 1 through 64 bits"
                    : "edge-qualified event expression must be an executable "
                      "scalar",
                sensitivity.span);
            return false;
        }
        state.width = *width;
        const auto dependency_start = waited_signals.size();
        if (!sensitivity.expression.valid()) {
            const auto found = signals_.find(sensitivity.signal);
            if (found != signals_.end()) {
                waited_signals.push_back(found->second);
            }
        } else {
            std::set<std::string> dependencies;
            collect_identifiers(state.expression, dependencies);
            for (const auto& dependency : dependencies) {
                if (locals_.contains(dependency)) {
                    continue;
                }
                if (const auto found = signals_.find(dependency);
                    found != signals_.end()) {
                    waited_signals.push_back(found->second);
                }
            }
        }
        if (waited_signals.size() == dependency_start) {
            report(
                "FSIM-ELAB-SVEVENT-001",
                "packed event expression has no readable signal dependencies",
                sensitivity.span);
            return false;
        }
        const auto baseline = lower_expression(
            state.expression, state.width);
        if (!baseline) {
            return false;
        }
        state.baseline = *baseline;
        expressions.push_back(std::move(state));
    }
    std::ranges::sort(waited_signals);
    waited_signals.erase(
        std::unique(waited_signals.begin(), waited_signals.end()),
        waited_signals.end());
    const auto waited_signal_count = waited_signals.size();

    std::optional<RegisterId> timed_out;
    if (statement.delay) {
        timed_out = allocate_register(
            1, frontend::ValueDomain::Boolean);
    }
    const auto initial_wait = static_cast<InstructionIndex>(
        process_.operations.size());
    WaitOn first_wait{
        waited_signals,
        std::vector<runtime::simir::EdgeKind>(
            waited_signals.size(), runtime::simir::EdgeKind::any)};
    if (statement.delay) {
        first_wait.timeout = statement.delay->magnitude;
        first_wait.timeout_result = timed_out;
    }
    process_.operations.emplace_back(std::move(first_wait));

    const auto evaluation_entry = static_cast<InstructionIndex>(
        process_.operations.size());
    std::optional<InstructionIndex> timeout_branch;
    if (timed_out) {
        timeout_branch = static_cast<InstructionIndex>(
            process_.operations.size());
        process_.operations.emplace_back(Branch{
            *timed_out, 0, 0, UnknownBranchPolicy::error});
    }
    const auto expression_start = static_cast<InstructionIndex>(
        process_.operations.size());
    std::vector<RegisterId> matches;
    const auto negate = [&](const RegisterId source) {
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(LogicalNot{destination, source});
        return destination;
    };
    const auto logical = [&](const LogicalBinaryOperator operation,
                             const RegisterId lhs,
                             const RegisterId rhs) {
        const auto destination = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(
            LogicalBinary{operation, destination, lhs, rhs});
        return destination;
    };
    for (auto& state : expressions) {
        const auto current = lower_expression(
            state.expression, state.width);
        if (!current) {
            return false;
        }
        const auto equal = allocate_register(
            1, frontend::ValueDomain::Bit2);
        process_.operations.emplace_back(Binary{
            BinaryOperator::case_equal,
            equal,
            state.baseline,
            *current});
        auto matched = negate(equal);
        if (state.edge != frontend::EdgeKind::Any) {
            const auto zero = allocate_register(
                1, frontend::ValueDomain::Bit2);
            const auto one = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(
                LoadConstant{zero, unsigned_value(0, 1)});
            process_.operations.emplace_back(
                LoadConstant{one, unsigned_value(1, 1)});
            const auto previous_forbidden = allocate_register(
                1, frontend::ValueDomain::Bit2);
            const auto current_forbidden = allocate_register(
                1, frontend::ValueDomain::Bit2);
            process_.operations.emplace_back(Binary{
                BinaryOperator::case_equal,
                previous_forbidden,
                state.baseline,
                state.edge == frontend::EdgeKind::Positive ? one : zero});
            process_.operations.emplace_back(Binary{
                BinaryOperator::case_equal,
                current_forbidden,
                *current,
                state.edge == frontend::EdgeKind::Positive ? zero : one});
            matched = logical(
                LogicalBinaryOperator::logical_and,
                matched,
                negate(previous_forbidden));
            matched = logical(
                LogicalBinaryOperator::logical_and,
                matched,
                negate(current_forbidden));
        }
        matches.push_back(matched);
        process_.operations.emplace_back(
            CopyRegister{state.baseline, *current});
    }
    auto matched = matches.front();
    for (std::size_t index = 1; index < matches.size(); ++index) {
        matched = logical(
            LogicalBinaryOperator::logical_or,
            matched,
            matches[index]);
    }
    const auto match_branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        matched, 0, 0, UnknownBranchPolicy::error});

    const auto rewait_start = static_cast<InstructionIndex>(
        process_.operations.size());
    WaitOn rewait{
        std::move(waited_signals),
        std::vector<runtime::simir::EdgeKind>(
            waited_signal_count,
            runtime::simir::EdgeKind::any)};
    if (statement.delay) {
        rewait.timeout = statement.delay->magnitude;
        rewait.timeout_result = timed_out;
        rewait.timeout_origin = initial_wait;
    }
    process_.operations.emplace_back(std::move(rewait));
    process_.operations.emplace_back(Jump{evaluation_entry});

    const auto satisfied = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[match_branch] = Branch{
        matched,
        satisfied,
        rewait_start,
        UnknownBranchPolicy::error};
    if (timeout_branch) {
        process_.operations[*timeout_branch] = Branch{
            *timed_out,
            satisfied,
            expression_start,
            UnknownBranchPolicy::error};
    }
    return true;
}

void Lowerer::lower_wait_order(const Statement& statement)
{
    std::vector<SignalId> events;
    events.reserve(statement.sensitivities.size());
    for (const auto& sensitivity : statement.sensitivities) {
        if (sensitivity.signal.empty()
            || sensitivity.expression.valid()) {
            report(
                "FSIM-ELAB-SVEVENT-005",
                "wait_order operands must be named-event identifiers",
                sensitivity.span);
            continue;
        }
        const auto found = signals_.find(sensitivity.signal);
        if (found == signals_.end()) {
            report(
                "FSIM-ELAB-SVEVENT-006",
                "unknown wait_order event '" + sensitivity.signal + "'",
                sensitivity.span);
            continue;
        }
        const auto& info = design_.signal_info_.at(found->second);
        if (info.type_name != "event") {
            report(
                "FSIM-ELAB-SVEVENT-007",
                "wait_order operand '" + sensitivity.signal
                    + "' is not declared as an event",
                sensitivity.span);
            continue;
        }
        events.push_back(found->second);
    }
    if (events.size() != statement.sensitivities.size()
        || events.empty()) {
        if (events.empty() && statement.sensitivities.empty()) {
            report(
                "FSIM-ELAB-SVEVENT-005",
                "wait_order requires at least one named event",
                statement.span);
        }
        return;
    }

    const auto result = allocate_register(
        1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(
        WaitOrder { std::move(events), result });
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch {
        result, 0, 0, UnknownBranchPolicy::error });

    const auto success = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(statement.statements);
    const auto success_jump = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Jump { 0 });

    const auto failure = static_cast<InstructionIndex>(
        process_.operations.size());
    lower_statements(statement.else_statements);
    const auto end = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations[branch] = Branch {
        result, success, failure, UnknownBranchPolicy::error
    };
    process_.operations[success_jump] = Jump { end };
}

}  // namespace fsim::elaboration
