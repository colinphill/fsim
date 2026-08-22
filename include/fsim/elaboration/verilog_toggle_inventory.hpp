// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_inventory.hpp"
#include "fsim/elaboration/verilog_coverage_points.hpp"
#include "fsim/runtime/coverage_toggle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kVerilogToggleInventorySchema
    = "fsim-verilog-toggle-inventory-v3";
inline constexpr std::string_view kVerilogToggleInventoryDiagnostic
    = "FSIM-COV-021";

enum class VerilogToggleObjectKind : std::uint8_t {
    Port = 1U,
    Net = 2U,
    Signal = 3U,
    RetainedVariable = 4U,
};

struct VerilogToggleObject {
    // The source point remains common to every elaborated instance of one
    // declaration. `point` additionally authenticates the concrete instance
    // and complete hierarchical object path used by Change 7 outcomes.
    runtime::CodeCoveragePointId source_point;
    runtime::CodeCoveragePointId point;
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    std::string hierarchy_path;
    VerilogToggleObjectKind kind { VerilogToggleObjectKind::Signal };
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::SystemVerilog
    };
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };
    std::size_t width { };
    std::size_t first_outcome { };

    friend bool operator==(
        const VerilogToggleObject&, const VerilogToggleObject&)
        = default;
};

struct VerilogToggleInventory {
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    std::string instance;
    std::vector<VerilogToggleObject> objects;
    std::vector<runtime::CoverageToggleOutcome> outcomes;

    friend bool operator==(
        const VerilogToggleInventory&, const VerilogToggleInventory&)
        = default;
};

struct VerilogToggleInventoryLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_objects { 1U << 20U };
    std::size_t maximum_bits { 1U << 22U };
    std::size_t maximum_object_width { 1U << 20U };
    std::size_t maximum_object_name_bytes { 1U << 16U };
    std::size_t maximum_hierarchy_bytes { 1U << 20U };
    std::uint64_t maximum_line_number { 1ULL << 31U };
    CoverageInstanceIdentityLimits instance_identity;
};

enum class VerilogToggleInventoryError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidLanguage,
    InvalidUnitKind,
    InstanceOwnerMismatch,
    InvalidInstanceIdentity,
    EmptySourceName,
    DuplicateSourceName,
    DuplicateSourceIdentity,
    InvalidSourceIdentity,
    EmptyObjectName,
    InvalidObjectName,
    UnknownObjectSource,
    ObjectSourceOwnershipMismatch,
    InvalidObjectSpan,
    InvalidObjectLine,
    UnspecializedObjectWidth,
    InvalidObjectWidth,
    DuplicateObjectPath,
    DuplicateObjectIdentity,
};

struct VerilogToggleInventoryResult {
    std::optional<VerilogToggleInventory> inventory;
    VerilogToggleInventoryError error {
        VerilogToggleInventoryError::None
    };
    std::size_t object_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return inventory.has_value()
            && error == VerilogToggleInventoryError::None;
    }
};

// True only for scalar or packed integral objects represented by binary or
// four-state bits. Real, string, handle, interface, and container objects do
// not manufacture binary toggle bins. Change 10 owns explicit default
// exclusion records; Change 11 owns selected container elements.
[[nodiscard]] bool is_verilog_toggle_type(
    const frontend::Type& type) noexcept;

// The supplied unit is the already specialized semantic unit for exactly one
// CoverageInventoryOwner. Generate expansion has qualified retained object
// names before this boundary, so the resulting hierarchy never depends on
// parsing process or runtime display names.
[[nodiscard]] VerilogToggleInventoryResult make_verilog_toggle_inventory(
    const frontend::DesignUnit& unit,
    const CoverageInventoryOwner& owner,
    std::span<const VerilogCoverageSource> sources,
    VerilogToggleInventoryLimits limits = { }) noexcept;

} // namespace fsim::elaboration
