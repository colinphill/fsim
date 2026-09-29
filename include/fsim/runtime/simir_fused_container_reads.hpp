// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir.hpp"

namespace fsim::runtime::simir {

/// A whole, unique, readable and writable signal alias certified by the
/// complete elaborated graph. The caller must exclude container cache writers
/// and invalidate the fused plan on force, alias changes or dynamic writers.
struct FusedMaskedContainerRead {
    ContainerObjectId object { };
    SignalId signal { };
    const ContainerType* type { };
    std::uint32_t signal_width { };
};

/// Normalize a private fusion copy. The original program remains the fallback.
/// Uses already lowered typed index constants; never re-evaluates source HIR.
[[nodiscard]] std::optional<Process> normalize_fused_container_reads(
    const Process&,
    std::span<const FusedMaskedContainerRead>);

} // namespace fsim::runtime::simir
