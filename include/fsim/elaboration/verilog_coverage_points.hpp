// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_point_identity.hpp"
#include "fsim/frontend/coverage_source_control.hpp"
#include "fsim/frontend/design.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kVerilogCoveragePointDiagnostic
    = "FSIM-COV-004";

// The name is the exact physical source spelling retained by SourceSpan. The
// identity is checkout-independent and authenticates the corresponding bytes.
struct VerilogCoverageSource {
    std::string source_name;
    frontend::CodeCoverageSourceIdentity identity;
    // Optional authenticated source bytes used to apply source controls during
    // this immediate discovery call. The caller retains ownership.
    std::string_view source_text;

    VerilogCoverageSource(
        std::string name,
        frontend::CodeCoverageSourceIdentity source_identity,
        const std::string_view text = { })
        : source_name(std::move(name))
        , identity(std::move(source_identity))
        , source_text(text)
    {
    }
};

struct VerilogStatementCoveragePoint {
    runtime::CodeCoveragePointId id;
    frontend::CodeCoverageLanguage language {
        frontend::CodeCoverageLanguage::Verilog
    };
    frontend::StatementKind statement_kind {
        frontend::StatementKind::Null
    };
    frontend::CodeCoverageSourceSpan span;
    std::size_t source_index { };
    std::uint64_t line { };

    friend bool operator==(const VerilogStatementCoveragePoint&,
        const VerilogStatementCoveragePoint&)
        = default;
};

struct VerilogStatementCoverageExclusion {
    VerilogStatementCoveragePoint point;
    std::string reason;

    friend bool operator==(const VerilogStatementCoverageExclusion&,
        const VerilogStatementCoverageExclusion&)
        = default;
};

struct VerilogCoveragePointLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_statements { 1U << 20U };
    std::size_t maximum_nesting { 1U << 12U };
};

enum class VerilogCoveragePointError {
    None,
    InvalidLanguage,
    ResourceLimit,
    EmptySourceName,
    DuplicateSourceName,
    InvalidSourceIdentity,
    UnknownStatementSource,
    InvalidStatementSpan,
    DuplicatePoint,
    InvalidSourceControl,
};

struct VerilogCoveragePointResult {
    std::vector<VerilogStatementCoveragePoint> points;
    // Excluded executable points retain the same stable identity and source
    // coordinates as scored points, but never receive runtime counters.
    std::vector<VerilogStatementCoverageExclusion> exclusions;
    VerilogCoveragePointError error { VerilogCoveragePointError::None };
    std::size_t statement_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == VerilogCoveragePointError::None;
    }
};

} // namespace fsim::elaboration
