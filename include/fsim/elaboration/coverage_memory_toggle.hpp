// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_toggle_selection.hpp"
#include "fsim/runtime/coverage_toggle.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageMemoryToggleSchema
    = "fsim-coverage-memory-toggle-v3";
inline constexpr std::string_view kCoverageMemoryToggleDiagnostic
    = "FSIM-COV-024";

struct CoverageMemoryToggleIndexRange {
    std::int64_t first { };
    std::int64_t last { };

    friend bool operator==(const CoverageMemoryToggleIndexRange&,
        const CoverageMemoryToggleIndexRange&)
        = default;
};

struct CoverageMemoryToggleSelector {
    // Numeric containers require one explicit range per dimension. String-keyed
    // associative arrays instead require one or more exact keys. No wildcard or
    // implicit whole-container selector exists.
    std::vector<CoverageMemoryToggleIndexRange> dimensions;
    std::vector<std::string> string_keys;
    std::size_t first_bit { };
    std::size_t bit_count { };
};

struct CoverageMemoryToggleRule {
    std::string hierarchy_path;
    std::vector<CoverageMemoryToggleSelector> selectors;
};

struct CoverageMemoryToggleElement {
    runtime::CodeCoveragePointId source_point;
    runtime::CodeCoveragePointId point;
    runtime::CodeCoveragePointId exclusion;
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::SystemVerilog
    };
    std::string hierarchy_path;
    std::vector<std::int64_t> indices;
    std::optional<std::string> string_key;
    std::size_t first_bit { };
    std::size_t bit_count { };
    std::size_t first_outcome { };

    friend bool operator==(const CoverageMemoryToggleElement&,
        const CoverageMemoryToggleElement&)
        = default;
};

struct CoverageMemoryToggleInventory {
    CoverageInstanceIdentity instance_identity;
    std::uint32_t specialization { };
    std::string instance;
    std::vector<CoverageMemoryToggleElement> elements;
    std::vector<runtime::CoverageToggleOutcome> outcomes;

    friend bool operator==(const CoverageMemoryToggleInventory&,
        const CoverageMemoryToggleInventory&)
        = default;
};

struct CoverageMemoryToggleLimits {
    std::size_t maximum_exclusions { 1U << 20U };
    std::size_t maximum_rules { 1U << 16U };
    std::size_t maximum_selectors { 1U << 20U };
    std::size_t maximum_dimensions { 64U };
    std::size_t maximum_string_keys { 1U << 20U };
    std::size_t maximum_string_key_bytes { 1U << 16U };
    std::size_t maximum_range_span { 1U << 20U };
    std::size_t maximum_elements { 1U << 20U };
    std::size_t maximum_bits { 1U << 22U };
    std::size_t maximum_hierarchy_bytes { 1U << 20U };
};

enum class CoverageMemoryToggleError : std::uint8_t {
    None,
    ResourceLimit,
    InvalidSelectionIdentity,
    InvalidExclusionIdentity,
    DuplicateExclusionIdentity,
    DuplicateExclusionPath,
    EmptyRulePath,
    InvalidRulePath,
    DuplicateRulePath,
    UnknownRulePath,
    ObjectNotExcludedContainer,
    UnsupportedElementType,
    UnspecializedDimensions,
    MissingSelector,
    InvalidSelectorForm,
    InvalidDimensionCount,
    IndexOutOfRange,
    EmptyStringKey,
    InvalidStringKey,
    InvalidBitRange,
    DuplicateBitSelection,
    DuplicateElementIdentity,
};

struct CoverageMemoryToggleResult {
    std::optional<CoverageMemoryToggleInventory> inventory;
    CoverageMemoryToggleError error { CoverageMemoryToggleError::None };
    std::size_t rule_index { };
    std::size_t selector_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return inventory.has_value()
            && error == CoverageMemoryToggleError::None;
    }
};

[[nodiscard]] CoverageMemoryToggleResult make_coverage_memory_toggle_inventory(
    const CoverageToggleSelection& defaults,
    std::span<const CoverageMemoryToggleRule> rules,
    CoverageMemoryToggleLimits limits = { }) noexcept;

} // namespace fsim::elaboration
