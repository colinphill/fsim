// SPDX-License-Identifier: Apache-2.0
#include "region_frontier_initial_slot_validation.hpp"

#include <llvm/ExecutionEngine/Orc/AbsoluteSymbols.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>

#include <limits>
#include <utility>

namespace fsim::compiler::llvm_detail {
namespace {

using runtime::simir::RegionFrontierEventKindV2;
using runtime::simir::RegionFrontierKeyV2;
using runtime::simir::RegionFrontierPendingWriteFlagsV2;
using runtime::simir::RegionFrontierPendingWriteV2;
using runtime::simir::RegionFrontierSlotV2;

[[nodiscard]] bool keys_equal(const RegionFrontierKeyV2& left,
    const RegionFrontierKeyV2& right) noexcept
{
    return left.time == right.time
        && left.delta == right.delta
        && left.systemverilog_round == right.systemverilog_round
        && left.stable_order == right.stable_order
        && left.sequence == right.sequence
        && left.process_domain == right.process_domain
        && left.phase == right.phase;
}

[[nodiscard]] bool key_matches_slot(const RegionFrontierKeyV2& key,
    const RegionFrontierSlotV2& slot,
    const bool allow_pending_next_round) noexcept
{
    auto round_matches = key.systemverilog_round
        == slot.systemverilog_round;
    if (allow_pending_next_round
        && slot.systemverilog_round
            != std::numeric_limits<std::uint64_t>::max()) {
        round_matches = round_matches
            || key.systemverilog_round == slot.systemverilog_round + 1U;
    }
    return key.time == slot.time
        && key.delta == slot.delta
        && round_matches
        && key.process_domain == slot.process_domain
        && key.phase == slot.phase;
}

[[nodiscard]] bool target_flags_match(const std::uint32_t flags,
    const std::uint32_t event_kind) noexcept
{
    const bool internal = event_kind == static_cast<std::uint32_t>(
        RegionFrontierEventKindV2::internal_commit);
    const auto target_flag = internal
        ? RegionFrontierPendingWriteFlagsV2::pending_internal_target
        : RegionFrontierPendingWriteFlagsV2::pending_boundary_target;
    const auto other_target = internal
        ? RegionFrontierPendingWriteFlagsV2::pending_boundary_target
        : RegionFrontierPendingWriteFlagsV2::pending_internal_target;
    constexpr auto common_required
        = RegionFrontierPendingWriteFlagsV2::pending_active
        | RegionFrontierPendingWriteFlagsV2::pending_value_ready
        | RegionFrontierPendingWriteFlagsV2::pending_key_assigned;
    constexpr auto known_flags
        = RegionFrontierPendingWriteFlagsV2::pending_active
        | RegionFrontierPendingWriteFlagsV2::pending_value_ready
        | RegionFrontierPendingWriteFlagsV2::pending_key_assigned
        | RegionFrontierPendingWriteFlagsV2::pending_internal_target
        | RegionFrontierPendingWriteFlagsV2::pending_boundary_target
        | RegionFrontierPendingWriteFlagsV2::pending_committed;
    const auto required = common_required | target_flag;
    const bool allow_committed = event_kind == static_cast<std::uint32_t>(
        RegionFrontierEventKindV2::boundary_commit);
    return (flags & required) == required
        && (flags & other_target) == 0U
        && (flags & ~known_flags) == 0U
        && (allow_committed
            || (flags & RegionFrontierPendingWriteFlagsV2::pending_committed)
                == 0U);
}

[[nodiscard]] bool site_identity_matches(
    const RegionFrontierPendingWriteV2& pending,
    const ExpectedInitialPendingSiteV1& expected) noexcept
{
    return pending.member_index == expected.member_index
        && pending.signal_slot == expected.signal_slot
        && pending.source_instruction == expected.source_instruction
        && pending.update_kind == expected.update_kind
        && pending.reserved == 0U
        && static_cast<std::uint32_t>(pending.value_kind)
            == expected.value_kind
        && pending.width == expected.width
        && pending.word_count == expected.word_count
        && pending.plane_count == expected.plane_count;
}

} // namespace

extern "C" std::uint32_t fsim_region_frontier_validate_initial_slots_v1(
    const fsim::runtime::simir::RegionFrontierFrameV2* const frame,
    const ExpectedInitialPendingSiteV1* const expected_sites,
    const std::uint32_t expected_site_count,
    std::uint8_t* const slot_states,
    std::uint64_t* const live_pending_count) noexcept
{
    if (frame == nullptr || slot_states == nullptr
        || live_pending_count == nullptr
        || (expected_site_count != 0U && expected_sites == nullptr)
        || (expected_site_count != 0U && (frame->pending_writes == nullptr
            || frame->members == nullptr))) {
        return 0U;
    }

    for (std::uint32_t index = 0U; index < expected_site_count; ++index) {
        const auto& expected = expected_sites[index];
        const auto& pending = frame->pending_writes[expected.pending_slot];
        const auto flags = pending.flags;
        if ((flags & RegionFrontierPendingWriteFlagsV2::pending_active)
            == 0U) {
            slot_states[expected.pending_slot] = 0U;
            continue;
        }

        const auto event_kind = expected.event_kind;
        if (!target_flags_match(flags, event_kind)
            || !site_identity_matches(pending, expected)
            || !key_matches_slot(pending.commit_key, frame->slot, true)) {
            return 0U;
        }

        const auto& member = frame->members[expected.member_index];
        if (!keys_equal(pending.origin, member.activation_origin)
            || pending.origin.process_domain
                != runtime::simir::kRegionFrontierSystemVerilogDomainV2
            || pending.origin.phase
                != runtime::simir::kRegionFrontierActivePhaseV2) {
            return 0U;
        }

        slot_states[expected.pending_slot] = 1U;
        *live_pending_count += 1U;
    }
    return 1U;
}

llvm::Error define_initial_slot_validation_helper(llvm::orc::LLJIT& jit)
{
    llvm::orc::MangleAndInterner mangle {
        jit.getExecutionSession(), jit.getDataLayout()
    };
    llvm::orc::SymbolMap symbols;
    symbols[mangle(kInitialSlotValidationHelperSymbol)]
        = llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr::fromPtr(
                &fsim_region_frontier_validate_initial_slots_v1),
            llvm::JITSymbolFlags::Exported);
    return jit.getMainJITDylib().define(
        llvm::orc::absoluteSymbols(std::move(symbols)));
}

} // namespace fsim::compiler::llvm_detail
