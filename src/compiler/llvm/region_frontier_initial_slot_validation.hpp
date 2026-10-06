// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier_v2.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace llvm {
class Error;
namespace orc {
class LLJIT;
}
} // namespace llvm

namespace fsim::compiler::llvm_detail {

/// Private row shared by the generated expected-shape table and the host
/// initial-slot validation helper. Keep the field order synchronized with the
/// emitted LLVM constant record.
struct ExpectedInitialPendingSiteV1 {
    std::uint32_t pending_slot { };
    std::uint32_t value_kind { };
    std::uint32_t width { };
    std::uint32_t word_count { };
    std::uint32_t plane_count { };
    std::uint32_t member_index { };
    std::uint32_t signal_slot { };
    std::uint32_t source_instruction { };
    std::uint32_t update_kind { };
    std::uint32_t event_kind { };
};

static_assert(std::is_standard_layout_v<ExpectedInitialPendingSiteV1>);
static_assert(std::is_trivially_copyable_v<ExpectedInitialPendingSiteV1>);
static_assert(sizeof(ExpectedInitialPendingSiteV1) == 40U);
static_assert(alignof(ExpectedInitialPendingSiteV1) == alignof(std::uint32_t));
static_assert(offsetof(ExpectedInitialPendingSiteV1, pending_slot) == 0U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, value_kind) == 4U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, width) == 8U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, word_count) == 12U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, plane_count) == 16U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, member_index) == 20U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, signal_slot) == 24U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, source_instruction) == 28U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, update_kind) == 32U);
static_assert(offsetof(ExpectedInitialPendingSiteV1, event_kind) == 36U);

inline constexpr char kInitialSlotValidationHelperSymbol[]
    = "fsim_region_frontier_validate_initial_slots_v1";

/// Validate the mutable pending rows against the exact emitted site table.
/// The caller has already validated the frame's complete extents and this
/// table's compiled layout. The caller initializes `*live_pending_count` to
/// zero and gives `slot_states` the extent max(compiled pending capacity, 1). The
/// immutable expected table has one row per compiled write site, with unique
/// pending slots below the compiled capacity and member indices below the
/// compiled member count.
/// Returns 1 on success and 0 on decline. It mutates only the caller's stack
/// scratch and count, never the frame.
extern "C" std::uint32_t fsim_region_frontier_validate_initial_slots_v1(
    const fsim::runtime::simir::RegionFrontierFrameV2* frame,
    const ExpectedInitialPendingSiteV1* expected_sites,
    std::uint32_t expected_site_count,
    std::uint8_t* slot_states,
    std::uint64_t* live_pending_count) noexcept;

/// Install the private helper address in an ORC JITDylib. Both production
/// LlvmJit and direct compiler fixtures use this same registrar.
[[nodiscard]] llvm::Error define_initial_slot_validation_helper(
    llvm::orc::LLJIT& jit);

} // namespace fsim::compiler::llvm_detail
