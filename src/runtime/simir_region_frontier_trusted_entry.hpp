// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/simir_region_frontier_v2.hpp"

namespace fsim::app::application_detail {
class LlvmRegionFrontierBackend;
}

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

/// Private, builtin-only entries that rely on the runtime's canonical-value
/// producer invariant. The first entry retains the checked geometric-range
/// comparisons; the second relies on an exact current range proof, either a
/// retained alias-range certificate or a fresh sorted range check. Both retain
/// the remaining V2 frame, event, generation, and status checks.
struct RegionFrontierCanonicalValuesEntryView final {
    RegionFrontierStepEntryV2 canonical_values_entry { };
    RegionFrontierStepEntryV2 alias_and_canonical_values_entry { };
    const RegionFrontierLayoutV2* layout { };
};

/// Optional capability for the in-tree LLVM adapter only. Foreign/custom
/// backends stay on the public fully checked entry. The runtime may use these
/// entries only when its fresh binding receipt and canonical storage
/// preparation are valid for this exact layout and frame.
class RegionFrontierCanonicalValuesEntryCapability {
public:
    virtual ~RegionFrontierCanonicalValuesEntryCapability() = default;

    /// The returned functions and layout are owned by the backend and remain
    /// valid only while that backend remains alive. `canonical_values_entry`
    /// skips repeated value-code/tail scans but keeps geometric range checks;
    /// `alias_and_canonical_values_entry` additionally relies on the runtime's
    /// exact current range proof, which may come from a current alias
    /// certificate or a fresh sorted range check.
    [[nodiscard]] virtual RegionFrontierCanonicalValuesEntryView
    canonical_values_entries() const noexcept = 0;
};

/// Exact private entry and layout identity for the combined alias-range,
/// canonical-value, and descriptor-shape proof path. This remains separate
/// from the older entries so their validation and authority do not change.
struct RegionFrontierDescriptorShapesEntryView final {
    RegionFrontierStepEntryV2 entry { };
    const RegionFrontierLayoutV2* layout { };
};

/// Optional capability sealed to the in-tree LLVM bridge. The runtime may use
/// this entry only when it holds the exact current alias-range, canonical-value,
/// and descriptor-shape proofs for this layout and frame. It skips only the
/// signal and pending descriptor-shape scans. Frame/header/generation/status checks
/// and dynamic pending flags, site, origin, and commit-key checks remain active.
/// Foreign and custom backends remain on the existing checked entries.
class RegionFrontierDescriptorShapesEntryCapability {
public:
    RegionFrontierDescriptorShapesEntryCapability(
        const RegionFrontierDescriptorShapesEntryCapability&) = delete;
    RegionFrontierDescriptorShapesEntryCapability& operator=(
        const RegionFrontierDescriptorShapesEntryCapability&) = delete;
    RegionFrontierDescriptorShapesEntryCapability(
        RegionFrontierDescriptorShapesEntryCapability&&) = delete;
    RegionFrontierDescriptorShapesEntryCapability& operator=(
        RegionFrontierDescriptorShapesEntryCapability&&) = delete;

    virtual ~RegionFrontierDescriptorShapesEntryCapability() = default;

    /// The function and layout remain owned by the backend and valid only
    /// while it remains alive. This combined entry is additive; it does not
    /// change the checks or authority of any existing private entry.
    [[nodiscard]] virtual RegionFrontierDescriptorShapesEntryView
    descriptor_shapes_entry() const noexcept = 0;

private:
    RegionFrontierDescriptorShapesEntryCapability() = default;
    friend class fsim::app::application_detail::LlvmRegionFrontierBackend;
};

/// Source-only builtin contract for the native member-state mutation envelope.
/// This authority is independent of alias geometry and canonical value codes.
/// The exact entry writes only consumed activation members and destinations of
/// changed-signal fanout, plus activation descriptors in its staged-event log.
/// The runtime must collect that envelope before either log is cleared, mark
/// host imports/key issuance, and validate all selected members before copying.
struct RegionFrontierMemberSyncEntryView final {
    RegionFrontierStepEntryV2 entry { };
    const RegionFrontierLayoutV2* layout { };
    /// Optional entry that also relies on the exact descriptor-shape proof.
    /// The existing `entry` retains its original member-sync semantics.
    RegionFrontierStepEntryV2 descriptor_shapes_entry { };
};

class RegionFrontierMemberSyncEntryCapability {
public:
    virtual ~RegionFrontierMemberSyncEntryCapability() = default;

    [[nodiscard]] virtual RegionFrontierMemberSyncEntryView
    member_sync_entry() const noexcept = 0;
};

} // namespace fsim::runtime::simir::detail
