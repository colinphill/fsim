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

inline constexpr std::string_view kVhdlToggleInventorySchema
    = "fsim-vhdl-toggle-inventory-v3";
inline constexpr std::string_view kVhdlToggleInventoryDiagnostic
    = "FSIM-COV-022";

enum class VhdlToggleObjectKind : std::uint8_t {
    Port = 1U,
    Signal = 2U,
    RetainedVariable = 3U,
};

struct VhdlToggleObject {
    runtime::CodeCoveragePointId source_point;
    runtime::CodeCoveragePointId point;
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    std::string hierarchy_path;
    VhdlToggleObjectKind kind { VhdlToggleObjectKind::Signal };
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };
    std::size_t width { };
    std::size_t first_outcome { };

    friend bool operator==(const VhdlToggleObject&, const VhdlToggleObject&)
        = default;
};

struct VhdlToggleInventory {
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    std::string instance;
    frontend::VhdlStandard standard { frontend::VhdlStandard::Vhdl2008 };
    std::vector<VhdlToggleObject> objects;
    std::vector<runtime::CoverageToggleOutcome> outcomes;

    friend bool operator==(
        const VhdlToggleInventory&, const VhdlToggleInventory&)
        = default;
};

struct VhdlToggleInventoryLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_objects { 1U << 20U };
    std::size_t maximum_bits { 1U << 22U };
    std::size_t maximum_object_width { 1U << 20U };
    std::size_t maximum_object_name_bytes { 1U << 16U };
    std::size_t maximum_hierarchy_bytes { 1U << 20U };
    std::uint64_t maximum_line_number { 1ULL << 31U };
    CoverageInstanceIdentityLimits instance_identity;
};

enum class VhdlToggleInventoryError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidLanguage,
    InvalidStandard,
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

struct VhdlToggleInventoryResult {
    std::optional<VhdlToggleInventory> inventory;
    VhdlToggleInventoryError error { VhdlToggleInventoryError::None };
    std::size_t object_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return inventory.has_value()
            && error == VhdlToggleInventoryError::None;
    }
};

// Scalars and one-dimensional packed bit/std_logic vectors are directly
// scoreable. Composite or memory-like arrays remain owned by Changes 10-11.
[[nodiscard]] bool is_vhdl_toggle_type(
    const frontend::Type& type) noexcept;

// VHDL architectures and entity interfaces are separate semantic units. The
// caller supplies the already-specialized architecture plus its resolved,
// specialized entity-port view for one immutable elaborated owner.
[[nodiscard]] VhdlToggleInventoryResult make_vhdl_toggle_inventory(
    const frontend::DesignUnit& architecture,
    std::span<const frontend::SignalDeclaration> ports,
    const CoverageInventoryOwner& owner,
    std::span<const VerilogCoverageSource> sources,
    VhdlToggleInventoryLimits limits = { }) noexcept;

} // namespace fsim::elaboration
