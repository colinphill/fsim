// SPDX-License-Identifier: Apache-2.0
// The concurrent-property monitor calls of a generated assertion process
// (IEEE 1800-2017 16.14): `$__fsim_sva_tick(site, plan, guard_count, guards,
// start, disabled, bounds..., witnesses... [, wake, abort, flags, ticks...])`
// and `$__fsim_sva_outcome(site)`,
// and the local-variable requests of a tick (IEEE 1800-2023 16.10):
// `$__fsim_sva_request(site, r)`, `$__fsim_sva_local(site, r, k)` and
// `$__fsim_sva_answer(site, r, value)`, and the queued subroutine calls of a
// tick (16.11): `$__fsim_sva_call(site)`. See runtime::simir::SvaOperation.
#include "lowerer_internal.hpp"

#include "fsim/semantic/sva_plan.hpp"

#include <algorithm>
#include <bit>
#include <utility>

namespace fsim::elaboration {

std::optional<RegisterId> Lowerer::lower_hir_sva_call(
    const semantic::sv::Expression& call,
    const std::size_t expected_width)
{
    const bool tick = call.text == "$__fsim_sva_tick";
    const bool request = call.text == "$__fsim_sva_request";
    const bool local = call.text == "$__fsim_sva_local";
    const bool answer = call.text == "$__fsim_sva_answer";
    const bool queued_call = call.text == "$__fsim_sva_call";
    const auto malformed = [&] {
        report("FSIM-ELAB-SVA-001",
            "the generated concurrent-property monitor call " + call.text
                + " is malformed",
            hir_source_span(call.source));
        return std::optional<RegisterId> { };
    };
    const std::size_t operands = tick ? 6U
        : request                     ? 2U
        : local || answer             ? 3U
                                      : 1U;
    if (call.operands.size() < operands
        || (!tick && call.operands.size() != operands)) {
        return malformed();
    }
    const auto site = hir_constant_integer(call.operands[0]);
    if (!site || *site < 0) {
        return malformed();
    }
    SvaOperation operation;
    operation.kind = tick ? SvaKind::tick
        : request         ? SvaKind::request
        : local           ? SvaKind::local
        : answer          ? SvaKind::answer
        : queued_call     ? SvaKind::call
                          : SvaKind::outcome;
    operation.site = static_cast<std::uint32_t>(*site);
    std::size_t result_width = 32U;
    if (request || local || answer) {
        // The request number reads the process's own loop variable.
        const auto number = lower_hir_expression(call.operands[1], 32U);
        if (!number) {
            return std::nullopt;
        }
        operation.start = register_width(*number) == 32U
            ? *number
            : resize_register(*number, 32U, false);
        if (local) {
            const auto found = sva_local_widths_.find(operation.site);
            const auto index = hir_constant_integer(call.operands[2]);
            if (found == sva_local_widths_.end() || !index || *index < 0
                || static_cast<std::size_t>(*index) >= found->second.size()) {
                return malformed();
            }
            operation.guard_count = static_cast<std::uint32_t>(*index);
            result_width = found->second[operation.guard_count];
        }
        if (answer) {
            const auto value = lower_hir_expression(call.operands[2], 0U);
            if (!value) {
                return std::nullopt;
            }
            operation.guards = *value;
            operation.guard_count
                = static_cast<std::uint32_t>(register_width(*value));
        }
    }
    if (tick) {
        const auto plan
            = specialized_hir_unit_->find_expression(call.operands[1]);
        const auto count = hir_constant_integer(call.operands[2]);
        if (!plan || plan->systemverilog == nullptr
            || plan->systemverilog->kind
                != semantic::sv::ExpressionKind::string_literal
            || !plan->systemverilog->decoded_string || !count
            || *count < 0) {
            return malformed();
        }
        operation.plan = *plan->systemverilog->decoded_string;
        operation.guard_count = static_cast<std::uint32_t>(*count);
        auto decoded = semantic::sva::decode(operation.plan);
        const auto operand_count = decoded
            ? 6U + std::size_t { decoded->bounds } + decoded->locals.size()
            : std::size_t { };
        // The wake group: the wake bit, the abort net, the flags, then one
        // tick bit per clock of a plan whose clocks report their ticks so.
        const auto tick_bits = decoded
            ? std::max<std::size_t>(1U, decoded->clock_guards.size())
            : std::size_t { };
        if (!decoded
            || (call.operands.size() != operand_count
                && call.operands.size() != operand_count + 3U + tick_bits)) {
            return malformed();
        }
        const auto guard_width
            = std::max<std::size_t>(operation.guard_count, 1U);
        const auto guards = lower_hir_expression(call.operands[3], guard_width);
        // The start operand is a procedural assertion's enable, which its
        // procedure set in this time step (16.14.6.2): its current value,
        // not its sampled one. The disable condition reads current values
        // too (IEEE 1800-2023 16.12: "not sampled"; 16.9.3).
        const auto sampled = std::exchange(
            sample_concurrent_assertion_reads_, false);
        const auto start = lower_hir_expression(call.operands[4], 1U);
        const auto disabled = lower_hir_expression(call.operands[5], 1U);
        sample_concurrent_assertion_reads_ = sampled;
        if (!guards || !start || !disabled) {
            return std::nullopt;
        }
        const auto sized = [&](const RegisterId value, const std::size_t width) {
            return register_width(value) == width
                ? value
                : resize_register(value, width, false);
        };
        operation.guards = sized(*guards, guard_width);
        operation.start = sized(*start, 1U);
        operation.disabled = sized(*disabled, 1U);
        // A disabled operand that is a signal is the directive's disable
        // net, which the engine watches between the ticks (16.12).
        if (const auto net = hir_direct_signal(call.operands[5])) {
            operation.disable_signal = *net;
        }
        // The bounds, the local variables' witnesses, then (for a monitor
        // that also evaluates between its clock ticks) the wake bit, the
        // abort net, the monitor flags and the tick bit.
        const auto witnesses = 6U + std::size_t { decoded->bounds };
        const auto wake_at = witnesses + decoded->locals.size();
        for (std::size_t index = 6U; index < witnesses; ++index) {
            const auto bound = lower_hir_expression(call.operands[index], 64U);
            if (!bound) {
                return std::nullopt;
            }
            operation.bounds.push_back(sized(*bound, 64U));
        }
        if (call.operands.size() == wake_at + 3U + tick_bits) {
            // The wake bit the engine toggles to wake the monitor at the
            // start of a time step without a clock tick, and the abort net
            // whose changes make it do so (16.12.14); a literal names none.
            if (const auto wake = hir_direct_signal(call.operands[wake_at])) {
                operation.wake_signal = *wake;
            }
            if (const auto abort
                = hir_direct_signal(call.operands[wake_at + 1U])) {
                operation.abort_signal = *abort;
            }
            const auto flags = hir_constant_integer(call.operands[wake_at + 2U]);
            if (!flags || *flags < 0 || *flags > 0xff) {
                return malformed();
            }
            operation.monitor_flags = static_cast<std::uint8_t>(*flags);
            // Each clock's tick guard is the tick bit its clock process
            // sets in the time step (16.13): its current value, not its
            // sampled one, replaces the guard's bit.
            for (std::size_t clock = 0; clock < decoded->clock_guards.size();
                 ++clock) {
                const auto& operand = call.operands[wake_at + 3U + clock];
                if (!hir_direct_signal(operand)) {
                    continue;
                }
                const auto at = decoded->clock_guards[clock];
                const auto reads = std::exchange(
                    sample_concurrent_assertion_reads_, false);
                const auto ticked = lower_hir_expression(operand, 1U);
                sample_concurrent_assertion_reads_ = reads;
                if (!ticked || at >= operation.guard_count) {
                    return malformed();
                }
                const auto replaced = allocate_register(
                    register_width(operation.guards),
                    frontend::ValueDomain::Logic4);
                process_.operations.emplace_back(Insert { replaced,
                    operation.guards, sized(*ticked, 1U), at });
                operation.guards = replaced;
            }
        }
        // Each local variable's witness is a variable of its type (16.10):
        // the plan takes the type's width, which sizes the local's words in
        // the monitor's threads and the process's local reads.
        if (!decoded->locals.empty()) {
            auto& widths = sva_local_widths_[operation.site];
            widths.clear();
            for (std::size_t index = 0; index < decoded->locals.size(); ++index) {
                const auto witness
                    = lower_hir_expression(call.operands[witnesses + index], 0U);
                if (!witness) {
                    return std::nullopt;
                }
                const auto width
                    = static_cast<std::uint32_t>(register_width(*witness));
                decoded->locals[index].width = width;
                widths.push_back(width);
            }
            operation.plan = semantic::sva::encode(*decoded);
        }
    }
    operation.result = allocate_register(result_width,
        local ? frontend::ValueDomain::Logic4 : frontend::ValueDomain::Integer);
    const auto result = operation.result;
    process_.operations.emplace_back(std::move(operation));
    return expected_width != 0U && expected_width != result_width
        ? resize_register(result, expected_width, !local)
        : result;
}

std::optional<RegisterId> Lowerer::lower_hir_assertion_control_call(
    const semantic::sv::Expression& call)
{
    const auto type = call.operands.size() == 3U
        ? hir_constant_integer(call.operands[0])
        : std::nullopt;
    const auto directive = call.operands.size() == 3U
        ? hir_constant_integer(call.operands[1])
        : std::nullopt;
    const auto name = call.operands.size() == 3U
        ? specialized_hir_unit_->find_expression(call.operands[2])
        : std::nullopt;
    if (!type || !directive || *type <= 0 || *directive < 0 || !name
        || name->systemverilog == nullptr
        || name->systemverilog->kind
            != semantic::sv::ExpressionKind::string_literal
        || !name->systemverilog->decoded_string) {
        report("FSIM-ELAB-SVA-001",
            "the generated assertion control call " + call.text
                + " is malformed",
            hir_source_span(call.source));
        return std::nullopt;
    }
    return lower_assertion_control_read(static_cast<std::uint32_t>(*type),
        static_cast<std::uint32_t>(*directive),
        hir_assertion_control_name(*name->systemverilog->decoded_string));
}

RegisterId Lowerer::lower_assertion_control_read(const std::uint32_t type,
    const std::uint32_t directive, const std::string_view name)
{
    auto description = std::to_string(type) + "|" + std::to_string(directive)
        + "|" + std::string { name };
    const auto found = std::ranges::find(assertion_control_sites_, description);
    const auto site = static_cast<std::uint32_t>(
        found - assertion_control_sites_.begin());
    if (found == assertion_control_sites_.end()) {
        assertion_control_sites_.push_back(description);
    }
    SvaOperation operation;
    operation.kind = SvaKind::control;
    operation.site = site;
    operation.plan = std::move(description);
    operation.result = allocate_register(32U, frontend::ValueDomain::Bit2);
    const auto result = operation.result;
    process_.operations.emplace_back(std::move(operation));
    return result;
}

std::string Lowerer::hir_assertion_control_name(
    const std::string_view label) const
{
    std::string result;
    const auto append = [&](const std::string_view part) {
        if (!result.empty()) {
            result += '.';
        }
        result += part;
    };
    // The task or function and the named blocks around the assertion.
    if (active_hir_callable_
        && *active_hir_callable_ < hir_callable_frames_.size()
        && specialized_hir_unit_ != nullptr) {
        const auto declaration = specialized_hir_unit_->find_declaration(
            hir_callable_frames_[*active_hir_callable_].declaration);
        if (declaration && declaration->systemverilog != nullptr
            && !declaration->systemverilog->name.empty()) {
            append(declaration->systemverilog->name);
        }
    }
    for (std::size_t index = 0; index < hir_named_blocks_.size(); ++index) {
        const auto& block = hir_named_blocks_[index];
        // A process named by its outermost block already ends with it (as
        // hir_scope_hierarchy).
        if (index == 0U && result.empty() && hierarchy_.size() > block.size()
            && hierarchy_.ends_with(block)
            && hierarchy_[hierarchy_.size() - block.size() - 1U] == '.') {
            continue;
        }
        append(block);
    }
    // `blocks.label`: an unlabelled assertion (empty label) has no name a
    // list item can select, but the scopes above it still do (20.11).
    result += '.';
    result += label;
    return result;
}

InstructionIndex Lowerer::emit_assertion_control_gate(
    const RegisterId word, const std::uint32_t bit)
{
    const auto selected = allocate_register(1U, frontend::ValueDomain::Bit2);
    process_.operations.emplace_back(Extract { selected, word,
        static_cast<std::uint32_t>(std::countr_zero(bit)), 1U });
    const auto gate = static_cast<InstructionIndex>(process_.operations.size());
    process_.operations.emplace_back(Branch { selected,
        static_cast<InstructionIndex>(gate + 1U), 0U,
        UnknownBranchPolicy::when_false });
    return gate;
}

void Lowerer::patch_assertion_control_gate(
    const InstructionIndex gate, const InstructionIndex target)
{
    if (auto* const branch = runtime::simir::operation_get_if<Branch>(
            &process_.operations[gate])) {
        branch->when_false = target;
    }
}

void Lowerer::emit_violation_report(const std::uint32_t type,
    std::string message, const frontend::SourceSpan& span)
{
    // The statement may sit in a function a deferred assertion's action
    // arguments call: that assertion's pending handoff stays as it was.
    auto phase = std::exchange(deferred_assertion_action_phase_, std::nullopt);
    auto handoff
        = std::exchange(deferred_assertion_action_handoff_, std::nullopt);
    auto effects = std::exchange(deferred_assertion_action_effects_, { });
    auto control_in_force
        = std::exchange(deferred_assertion_control_, std::nullopt);
    // Off and Kill stop new reports; a Kill also flushes the queued ones
    // that have not matured (20.11), which the handoff's maturity gate
    // tells from the Kill count the word held when the report was queued.
    const auto name = hir_assertion_control_name({ });
    const auto control = lower_assertion_control_read(type, 0U, name);
    const auto on_gate
        = emit_assertion_control_gate(control, semantic::sva::control_on);
    deferred_assertion_control_
        = DeferredAssertionControl { type, 0U, name, control };
    deferred_assertion_action_phase_ = runtime::SchedulerPhase::observed;
    emit_deferred_assertion_action_effect(Report {
        std::move(message),
        runtime::simir::AssertionSeverity::warning,
        runtime_source_location(span),
    });
    (void)finish_deferred_assertion_action_handoff();
    patch_assertion_control_gate(on_gate,
        static_cast<InstructionIndex>(process_.operations.size()));
    deferred_assertion_action_phase_ = phase;
    deferred_assertion_action_handoff_ = handoff;
    deferred_assertion_action_effects_ = std::move(effects);
    deferred_assertion_control_ = std::move(control_in_force);
}

} // namespace fsim::elaboration
