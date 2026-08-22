// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/elaboration/coverage_instance_identity.hpp"
#include "fsim/frontend/coverage_point_identity.hpp"
#include "fsim/frontend/design.hpp"
#include "fsim/runtime/code_coverage.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kCoverageInventoryDiagnostic
    = "FSIM-COV-008";

struct CoverageInventorySource {
    std::string source_name;
    frontend::CodeCoverageSourceIdentity identity;

    friend bool operator==(
        const CoverageInventorySource&, const CoverageInventorySource&)
        = default;
};

struct CoverageInventoryPointDraft {
    runtime::CodeCoveragePointId id;
    runtime::CodeCoverageMetric metric {
        runtime::CodeCoverageMetric::Statement
    };
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };

    friend bool operator==(
        const CoverageInventoryPointDraft&,
        const CoverageInventoryPointDraft&)
        = default;
};

struct CoverageInstanceInventoryDraft {
    std::uint32_t specialization { };
    std::vector<CoverageInventoryPointDraft> points;
};

struct CoverageInventoryPoint {
    runtime::CodeCoveragePoint point;
    std::size_t source_index { };
    frontend::CodeCoverageSourceSpan span;
    std::uint64_t line { };

    friend bool operator==(
        const CoverageInventoryPoint& left,
        const CoverageInventoryPoint& right) noexcept
    {
        return left.point.id == right.point.id
            && left.point.metric == right.point.metric
            && left.point.counter == right.point.counter
            && left.source_index == right.source_index
            && left.span == right.span && left.line == right.line;
    }
};

struct CoverageInstanceInventory {
    std::uint32_t specialization { };
    CoverageInstanceIdentity identity;
    std::string instance;
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    std::vector<CoverageInventoryPoint> points;

    friend bool operator==(
        const CoverageInstanceInventory&, const CoverageInstanceInventory&)
        = default;
};

struct CodeCoverageInventory {
    std::vector<CoverageInventorySource> sources;
    std::vector<CoverageInstanceInventory> instances;
    std::size_t total_points { };

    friend bool operator==(
        const CodeCoverageInventory&, const CodeCoverageInventory&)
        = default;
};

// The owner projection deliberately contains only immutable elaboration
// identity. Callers construct it from the dense specialization table rather
// than recovering hierarchy from process names.
struct CoverageInventoryOwner {
    std::uint32_t specialization { };
    std::string_view instance;
    frontend::Language language { frontend::Language::SystemVerilog2017 };
    std::string_view source;
    std::span<const std::string> source_dependencies;
    std::string_view library;
    std::string_view unit;
    std::span<const std::pair<std::string, std::string>>
        parameter_identities;
};

struct CoverageInventoryLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_instances { 1U << 20U };
    std::size_t maximum_points { 1U << 20U };
    std::uint64_t maximum_line_number { 1ULL << 31U };
    CoverageInstanceIdentityLimits instance_identity;
};

enum class CoverageInventoryError : std::uint8_t {
    None,
    ResourceLimit,
    EmptySourceName,
    DuplicateSourceName,
    DuplicateSourceIdentity,
    InvalidSourceIdentity,
    InvalidOwner,
    InvalidInstanceIdentity,
    DuplicateInstanceIdentity,
    MissingInstance,
    DuplicateInstance,
    UnknownInstance,
    InstanceOwnerMismatch,
    InvalidPointIdentity,
    InvalidMetric,
    UnknownPointSource,
    PointSourceOwnershipMismatch,
    InvalidPointSpan,
    InvalidLine,
    DuplicatePoint,
    CounterOwnershipMismatch,
    NonCanonicalOrder,
    TotalPointMismatch,
};

struct CoverageInventoryValidationResult {
    CoverageInventoryError error { CoverageInventoryError::None };
    std::size_t index { };

    [[nodiscard]] constexpr bool ok() const noexcept
    {
        return error == CoverageInventoryError::None;
    }
};

struct CoverageInventoryBuildResult {
    std::optional<CodeCoverageInventory> inventory;
    CoverageInventoryError error { CoverageInventoryError::None };
    std::size_t index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return inventory.has_value()
            && error == CoverageInventoryError::None;
    }
};

// Normalize arbitrary draft order into dense specialization order, then
// assign one design-global counter to every instance-local point. The result
// is complete only when every elaborated owner occurs exactly once.
[[nodiscard]] CoverageInventoryBuildResult make_code_coverage_inventory(
    std::span<const CoverageInventorySource> sources,
    std::span<const CoverageInstanceInventoryDraft> instances,
    std::span<const CoverageInventoryOwner> owners,
    CoverageInventoryLimits limits = { }) noexcept;

[[nodiscard]] CoverageInventoryValidationResult
validate_code_coverage_inventory(
    const CodeCoverageInventory& inventory,
    std::span<const CoverageInventoryOwner> owners,
    CoverageInventoryLimits limits = { }) noexcept;

} // namespace fsim::elaboration
