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
    auto [signals, edges] = resolve_wait_sensitivities(statement);
    if (signals.empty() && !statement.delay) {
        return false;
    }
    const auto general = std::ranges::find_if(
        statement.sensitivities,
        [](const frontend::Sensitivity& sensitivity) {
            return sensitivity.expression.valid();
        });
    if (language_ == frontend::Language::Vhdl2008) {
        WaitOn wait{std::move(signals), std::move(edges)};
        if (statement.delay) {
            wait.timeout = statement.delay->magnitude;
        }
        process_.operations.emplace_back(std::move(wait));
        return true;
    }
    if (general == statement.sensitivities.end()) {
        WaitOn wait{std::move(signals), std::move(edges)};
        if (statement.delay) {
            wait.timeout = statement.delay->magnitude;
        }
        process_.operations.emplace_back(std::move(wait));
        return true;
    }
    if (statement.sensitivities.size() != 1 || statement.delay) {
        report(
            "FSIM-ELAB-SVEVENT-002",
            "packed event-expression metadata is mixed with another "
            "event or timeout",
            general->span);
        return false;
    }
    const auto width = infer_width(general->expression);
    if (!width || *width == 0 || *width > 64) {
        report(
            "FSIM-ELAB-SVEVENT-003",
            "packed event expression must have an executable width from "
            "1 through 64 bits",
            general->span);
        return false;
    }
    const auto baseline =
        lower_expression(general->expression, *width);
    if (!baseline) {
        return false;
    }
    const auto wait_start = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(
        WaitOn{std::move(signals), std::move(edges)});
    const auto current =
        lower_expression(general->expression, *width);
    if (!current) {
        return false;
    }
    const auto equal = allocate_register(
        1, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Binary{
        BinaryOperator::case_equal, equal, *baseline, *current});
    process_.operations.emplace_back(
        CopyRegister{*baseline, *current});
    const auto branch = static_cast<InstructionIndex>(
        process_.operations.size());
    process_.operations.emplace_back(Branch{
        equal,
        wait_start,
        static_cast<InstructionIndex>(branch + 1),
        UnknownBranchPolicy::when_false});
    return true;
}

}  // namespace fsim::elaboration
