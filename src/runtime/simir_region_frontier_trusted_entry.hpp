// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier_v2.hpp"

namespace fsim::runtime::simir::detail {

/// Exact private code/layout identity exposed by the in-tree LLVM bridge.
/// This source-only interface does not extend the installed backend vtable.
struct RegionFrontierTrustedEntryView final {
    RegionFrontierStepEntryV2 entry { };
    const RegionFrontierLayoutV2* layout { };
};

/// Optional capability for the interpreter's built-in runtime adapter.
/// Foreign and test backends remain on the public, fully checked entry.
class RegionFrontierTrustedEntryCapability {
public:
    virtual ~RegionFrontierTrustedEntryCapability() = default;

    /// The caller may invoke this entry only after proving the exact current
    /// nested buffer ranges, including their extents, valid under the layout's
    /// alias exception for the exact frame after its latest bind.
    /// The trusted entry skips only geometric range comparisons; all other V2
    /// frame and event checks remain active. Public step_entry() remains fully
    /// checked for untrusted callers.
    [[nodiscard]] virtual RegionFrontierTrustedEntryView
    trusted_entry() const noexcept = 0;
};

} // namespace fsim::runtime::simir::detail
