// SPDX-License-Identifier: Apache-2.0

#include "fsim/runtime/simir_region_activation.hpp"
#include "runtime_fused_staging_failure_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

using namespace fsim::runtime;
using namespace fsim::runtime::simir;
using fsim::tests::runtime::staging_failure_support::arm_allocation_failure;
using fsim::tests::runtime::staging_failure_support::begin_allocation_count;
using fsim::tests::runtime::staging_failure_support::clear_allocation_failure;
using fsim::tests::runtime::staging_failure_support::end_allocation_count;
using fsim::tests::runtime::staging_failure_support::require;

RegionConeActivationKernel make_kernel()
{
    RegionConeActivationKernel kernel;
    kernel.program.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    kernel.program.register_count = 1U;
    kernel.program.register_value_kinds = { ValueKind::logic4 };
    kernel.program.operations.push_back(Branch { 0U, 1U, 2U });
    kernel.program.operations.push_back(Jump { 1U });

    RegionConeKernelMember member;
    member.process = 7U;
    member.readiness_register = 0U;
    member.branch_instruction = 0U;
    member.begin = 1U;
    member.end = 2U;
    kernel.members.push_back(std::move(member));
    return kernel;
}

RegionKernelSchedulerPrefix make_prefix(
    const std::size_t cursor, const std::size_t ordinal,
    const std::uint64_t sequence)
{
    RegionKernelSchedulerPrefix prefix;
    prefix.frontier_generation = 3U;
    prefix.frontier_cursor = cursor;
    prefix.frontier_end = 2U;
    prefix.time = 11U;
    prefix.delta = 4U;
    prefix.phase = SchedulerPhase::active;
    prefix.systemverilog_round = 9U;
    RegionKernelReadyMember request;
    request.process = 7U;
    request.trigger_mask = Process::full_static_trigger_mask;
    request.origin = { ProcessSchedulingDomain::systemverilog,
        SchedulerPhase::active, 11U, 4U, 17U, sequence, 9U };
    prefix.tasks.push_back({ ordinal, request });
    return prefix;
}

RegionConeActivationKernel make_two_internal_signal_kernel()
{
    RegionConeActivationKernel kernel;
    kernel.program.scheduling_domain
        = ProcessSchedulingDomain::systemverilog;
    kernel.program.register_count = 3U;
    kernel.program.register_value_kinds = {
        ValueKind::logic4, ValueKind::logic4, ValueKind::logic4 };
    kernel.program.operations.push_back(Branch { 0U, 1U, 2U });
    kernel.program.operations.push_back(Jump { 1U });
    kernel.program.operations.push_back(CopyRegister { 1U, 0U });
    kernel.program.operations.push_back(CopyRegister { 2U, 0U });

    RegionConeKernelMember member;
    member.process = 7U;
    member.readiness_register = 0U;
    member.branch_instruction = 0U;
    member.begin = 1U;
    member.end = 2U;
    kernel.members.push_back(std::move(member));
    kernel.internal_signals = { 10U, 11U };

    RegionConeOutputBinding first;
    first.owner = 7U;
    first.signal = 10U;
    first.width = 129U;
    first.value_register = 1U;
    first.kernel_instruction = 2U;
    first.source_instruction = 2U;
    first.compute_instruction = 2U;
    kernel.outputs.push_back(std::move(first));

    RegionConeOutputBinding second;
    second.owner = 7U;
    second.signal = 11U;
    second.width = 129U;
    second.value_register = 2U;
    second.kernel_instruction = 3U;
    second.source_instruction = 3U;
    second.compute_instruction = 3U;
    kernel.outputs.push_back(std::move(second));
    return kernel;
}

std::array<RegionKernelInternalSeed, 2U> make_wide_seeds(
    const Logic4 first_current, const Logic4 first_previous,
    const Logic4 first_raw, const Logic4 second_current,
    const Logic4 second_previous, const Logic4 second_raw)
{
    return { {
        { 10U, 7U, PackedLogic4 { 129U, first_current },
            PackedLogic4 { 129U, first_previous },
            PackedLogic4 { 129U, first_raw } },
        { 11U, 7U, PackedLogic4 { 129U, second_current },
            PackedLogic4 { 129U, second_previous },
            PackedLogic4 { 129U, second_raw } },
    } };
}

struct InternalRoleSnapshot {
    PackedLogic4 current;
    PackedLogic4 previous;
    PackedLogic4 raw_driver;
};

InternalRoleSnapshot take_internal_snapshot(
    const RegionKernelActivationState& activation, const SignalId signal)
{
    const auto& state = activation.internal_state(signal);
    return { state.current, state.previous, state.raw_driver };
}

void require_internal_snapshot(
    const RegionKernelActivationState& activation, const SignalId signal,
    const InternalRoleSnapshot& expected, const char* const message)
{
    const auto& state = activation.internal_state(signal);
    require(state.current == expected.current
            && state.previous == expected.previous
            && state.raw_driver == expected.raw_driver,
        message);
}

void check_internal_reset_prevalidates_all_rows()
{
    const auto kernel = make_two_internal_signal_kernel();
    auto initial = make_wide_seeds(Logic4::zero, Logic4::one, Logic4::x,
        Logic4::z, Logic4::zero, Logic4::one);
    RegionKernelActivationState activation { kernel, initial };
    const auto first_before = take_internal_snapshot(activation, 10U);
    const auto second_before = take_internal_snapshot(activation, 11U);

    auto invalid_owner = make_wide_seeds(Logic4::one, Logic4::z, Logic4::zero,
        Logic4::x, Logic4::one, Logic4::z);
    invalid_owner[1U].owner = 8U;
    bool owner_rejected { };
    try {
        activation.reset_internal_state(invalid_owner);
    } catch (const std::invalid_argument&) {
        owner_rejected = true;
    }
    require(owner_rejected,
        "a late invalid owner is rejected before resetting earlier signals");
    require_internal_snapshot(activation, 10U, first_before,
        "a late owner failure leaves the earlier internal roles unchanged");
    require_internal_snapshot(activation, 11U, second_before,
        "a late owner failure leaves the later internal roles unchanged");

    auto invalid_shape = make_wide_seeds(Logic4::one, Logic4::z, Logic4::zero,
        Logic4::x, Logic4::one, Logic4::z);
    invalid_shape[1U].current = PackedLogic4 { 128U, Logic4::zero };
    bool shape_rejected { };
    try {
        activation.reset_internal_state(invalid_shape);
    } catch (const std::invalid_argument&) {
        shape_rejected = true;
    }
    require(shape_rejected,
        "a late invalid value shape is rejected before resetting earlier signals");
    require_internal_snapshot(activation, 10U, first_before,
        "a late shape failure leaves the earlier internal roles unchanged");
    require_internal_snapshot(activation, 11U, second_before,
        "a late shape failure leaves the later internal roles unchanged");

    auto valid = make_wide_seeds(Logic4::x, Logic4::zero, Logic4::one,
        Logic4::one, Logic4::x, Logic4::z);
    begin_allocation_count();
    activation.reset_internal_state(valid);
    const auto reset_allocations = end_allocation_count();
    require(reset_allocations == 0U,
        "resetting prepared owning wide seeds does not allocate after validation");
    const auto& first_after = activation.internal_state(10U);
    const auto& second_after = activation.internal_state(11U);
    require(first_after.current == valid[0U].current
            && first_after.previous == valid[0U].previous
            && first_after.raw_driver == valid[0U].raw_driver
            && second_after.current == valid[1U].current
            && second_after.previous == valid[1U].previous
            && second_after.raw_driver == valid[1U].raw_driver,
        "a fully validated owning wide reset replaces every internal role");
}

void check_by_value_copy_failure_is_retryable()
{
    const auto kernel = make_kernel();
    const auto first_prefix = make_prefix(0U, 0U, 1U);
    const auto next_prefix = make_prefix(1U, 1U, 2U);
    const std::vector<PackedLogic4> no_boundary_inputs;

    RegionKernelActivationState reusable_probe { kernel };
    begin_allocation_count();
    static_cast<void>(reusable_probe.begin_wave_reusable(
        first_prefix, no_boundary_inputs));
    const auto reusable_allocations = end_allocation_count();
    reusable_probe.discard_wave();
    require(reusable_allocations == 0U,
        "the reusable begin path is preallocated before measuring value-copy failure");

    RegionKernelActivationState copy_probe { kernel };
    begin_allocation_count();
    static_cast<void>(copy_probe.begin_wave(first_prefix, no_boundary_inputs));
    const auto copy_allocations = end_allocation_count();
    copy_probe.discard_wave();
    require(copy_allocations != 0U,
        "the by-value API performs an allocation after reusable staging");

    RegionKernelActivationState activation { kernel };
    static_cast<void>(activation.begin_wave_reusable(
        first_prefix, no_boundary_inputs));
    activation.discard_wave();

    bool copy_failed { };
    arm_allocation_failure(0U);
    try {
        static_cast<void>(activation.begin_wave(
            next_prefix, no_boundary_inputs));
    } catch (const std::bad_alloc&) {
        copy_failed = true;
    }
    clear_allocation_failure();
    require(copy_failed,
        "allocation failure is injected into the by-value activation image copy");

    const auto retried = activation.begin_wave(next_prefix, no_boundary_inputs);
    require(retried.generation == 2U
            && retried.scheduler_prefix.frontier_cursor == 1U
            && retried.ready_processes == std::vector<ProcessId> { 7U },
        "failed by-value copy restores the prior generation and frontier for retry");
    activation.discard_wave();
}

} // namespace

int main()
{
    try {
        check_by_value_copy_failure_is_retryable();
        check_internal_reset_prevalidates_all_rows();
        std::cout << "region activation failure recovery passed\n";
        return 0;
    } catch (const std::exception& error) {
        clear_allocation_failure();
        std::cerr << "region activation copy-failure recovery failed: "
                  << error.what() << '\n';
        return 1;
    }
}
