// SPDX-License-Identifier: Apache-2.0
// The concurrent-property monitor calls of a generated assertion process
// (IEEE 1800-2017 16.14): `$__fsim_sva_tick(site, plan, guard_count, guards,
// start, disabled, bounds...)` and `$__fsim_sva_outcome(site)`. See
// runtime::simir::SvaOperation.
#include "lowerer_internal.hpp"

#include <utility>

namespace fsim::elaboration {

std::optional<RegisterId> Lowerer::lower_hir_sva_call(
    const semantic::sv::Expression& call,
    const std::size_t expected_width)
{
    const bool tick = call.text == "$__fsim_sva_tick";
    const auto malformed = [&] {
        report("FSIM-ELAB-SVA-001",
            "the generated concurrent-property monitor call " + call.text
                + " is malformed",
            hir_source_span(call.source));
        return std::optional<RegisterId> { };
    };
    if (call.operands.size() < (tick ? 6U : 1U)
        || (!tick && call.operands.size() != 1U)) {
        return malformed();
    }
    const auto site = hir_constant_integer(call.operands[0]);
    if (!site || *site < 0) {
        return malformed();
    }
    SvaOperation operation;
    operation.kind = tick ? SvaKind::tick : SvaKind::outcome;
    operation.site = static_cast<std::uint32_t>(*site);
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
        const auto guard_width
            = std::max<std::size_t>(operation.guard_count, 1U);
        const auto guards = lower_hir_expression(call.operands[3], guard_width);
        // The start operand is a procedural assertion's enable, which its
        // procedure set in this time step (16.14.6.2): its current value,
        // not its sampled one.
        const auto sampled = std::exchange(
            sample_concurrent_assertion_reads_, false);
        const auto start = lower_hir_expression(call.operands[4], 1U);
        sample_concurrent_assertion_reads_ = sampled;
        const auto disabled = lower_hir_expression(call.operands[5], 1U);
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
        for (std::size_t index = 6U; index < call.operands.size(); ++index) {
            const auto bound = lower_hir_expression(call.operands[index], 64U);
            if (!bound) {
                return std::nullopt;
            }
            operation.bounds.push_back(sized(*bound, 64U));
        }
    }
    operation.result = allocate_register(32U, frontend::ValueDomain::Integer);
    const auto result = operation.result;
    process_.operations.emplace_back(std::move(operation));
    return expected_width != 0U && expected_width != 32U
        ? resize_register(result, expected_width, true)
        : result;
}

} // namespace fsim::elaboration
