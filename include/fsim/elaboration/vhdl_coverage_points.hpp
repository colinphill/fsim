// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/coverage_point_identity.hpp"
#include "fsim/frontend/coverage_source_control.hpp"
#include "fsim/frontend/design.hpp"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::elaboration {

inline constexpr std::string_view kVhdlCoveragePointDiagnostic
    = "FSIM-COV-005";

struct VhdlCoverageSource {
    std::string source_name;
    frontend::CodeCoverageSourceIdentity identity;
    std::string_view source_text;

    VhdlCoverageSource(
        std::string name,
        frontend::CodeCoverageSourceIdentity source_identity,
        const std::string_view text = { })
        : source_name(std::move(name))
        , identity(std::move(source_identity))
        , source_text(text)
    {
    }
};

struct VhdlStatementCoveragePoint {
    runtime::CodeCoveragePointId id;
    frontend::StatementKind statement_kind {
        frontend::StatementKind::Null
    };
    frontend::CodeCoverageSourceSpan span;
    std::size_t source_index { };
    std::uint64_t line { };

    friend bool operator==(const VhdlStatementCoveragePoint&,
        const VhdlStatementCoveragePoint&)
        = default;
};

struct VhdlStatementCoverageExclusion {
    VhdlStatementCoveragePoint point;
    std::string reason;

    friend bool operator==(const VhdlStatementCoverageExclusion&,
        const VhdlStatementCoverageExclusion&)
        = default;
};

struct VhdlCoveragePointLimits {
    std::size_t maximum_sources { 1U << 16U };
    std::size_t maximum_statements { 1U << 20U };
    std::size_t maximum_nesting { 1U << 12U };
};

enum class VhdlCoveragePointError {
    None,
    InvalidLanguage,
    InvalidStandard,
    ResourceLimit,
    EmptySourceName,
    DuplicateSourceName,
    InvalidSourceIdentity,
    UnknownStatementSource,
    InvalidStatementSpan,
    DuplicatePoint,
    InvalidSourceControl,
};

struct VhdlCoveragePointResult {
    std::vector<VhdlStatementCoveragePoint> points;
    std::vector<VhdlStatementCoverageExclusion> exclusions;
    VhdlCoveragePointError error { VhdlCoveragePointError::None };
    std::size_t statement_index { };

    [[nodiscard]] bool ok() const noexcept
    {
        return error == VhdlCoveragePointError::None;
    }
};

[[nodiscard]] constexpr bool is_executable_vhdl_statement_kind(
    frontend::StatementKind kind) noexcept;

// Sequential process/subprogram bodies and concurrent statement collections
// use the same retained Statement forest. Invoke discovery for each retained
// forest after static elaboration has selected the constructs that survive.
[[nodiscard]] VhdlCoveragePointResult discover_vhdl_statement_points(
    std::span<const frontend::Statement> statements,
    frontend::Language language,
    frontend::VhdlStandard standard,
    std::span<const VhdlCoverageSource> sources,
    VhdlCoveragePointLimits limits = { }) noexcept;

constexpr bool is_executable_vhdl_statement_kind(
    const frontend::StatementKind kind) noexcept
{
    switch (kind) {
    case frontend::StatementKind::Block:
    case frontend::StatementKind::Null:
        return false;
    case frontend::StatementKind::Assignment:
    case frontend::StatementKind::Force:
    case frontend::StatementKind::Release:
    case frontend::StatementKind::If:
    case frontend::StatementKind::Case:
    case frontend::StatementKind::Loop:
    case frontend::StatementKind::Break:
    case frontend::StatementKind::Continue:
    case frontend::StatementKind::Return:
    case frontend::StatementKind::ProcedureCall:
    case frontend::StatementKind::Assert:
    case frontend::StatementKind::Delay:
    case frontend::StatementKind::WaitOn:
    case frontend::StatementKind::WaitUntil:
    case frontend::StatementKind::Report:
        return true;
    case frontend::StatementKind::TaskCall:
    case frontend::StatementKind::EventTrigger:
    case frontend::StatementKind::Fork:
    case frontend::StatementKind::WaitFork:
    case frontend::StatementKind::DisableFork:
    case frontend::StatementKind::Disable:
    case frontend::StatementKind::Display:
    case frontend::StatementKind::FileClose:
    case frontend::StatementKind::FileFlush:
    case frontend::StatementKind::FileDisplay:
    case frontend::StatementKind::MemoryLoad:
    case frontend::StatementKind::ContainerMethod:
    case frontend::StatementKind::MonitorControl:
    case frontend::StatementKind::Pause:
    case frontend::StatementKind::Finish:
    case frontend::StatementKind::Exit:
    case frontend::StatementKind::ProceduralAssign:
    case frontend::StatementKind::Deassign:
    case frontend::StatementKind::WaitOrder:
        return false;
    }
    return false;
}

} // namespace fsim::elaboration
