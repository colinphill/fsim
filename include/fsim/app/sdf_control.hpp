// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/sdf_reannotation.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

enum class SdfControlSurface : std::uint8_t {
    ProjectCli,
    Tcl,
    CApi,
    CppApi
};

enum class SdfControlPhase : std::uint8_t {
    Compile,
    Elaborate,
    Simulate
};

enum class SdfControlEntryKind : std::uint8_t {
    Input,
    Path,
    TimingCheck
};

struct SdfControlInput {
    std::string source_identity;
    std::string root;
    std::string cell_pattern { "*" };
    std::uint64_t file_precedence { };
    std::uint64_t cell_precedence { };

    friend bool operator==(const SdfControlInput&,
        const SdfControlInput&) = default;
};

struct SdfControlRequest {
    SdfControlSurface surface { SdfControlSurface::CppApi };
    SdfControlPhase phase { SdfControlPhase::Elaborate };
    SdfDelaySelection selection { SdfDelaySelection::Typical };
    std::vector<SdfControlInput> inputs;
    std::uint64_t generation { };
    std::size_t report_limit { 4'096U };
};

struct SdfControlReportEntry {
    SdfControlEntryKind kind { SdfControlEntryKind::Input };
    std::string source_identity;
    std::string object_identity;
    std::string canonical_identity;

    friend bool operator==(const SdfControlReportEntry&,
        const SdfControlReportEntry&) = default;
};

struct SdfControlSummary {
    std::size_t input_count { };
    std::size_t file_count { };
    std::size_t applied_path_count { };
    std::size_t applied_timing_check_count { };
    std::size_t report_entry_count { };
    std::uint64_t generation { };
    bool effective { };
    bool report_truncated { };
};

class SdfControlApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const SdfControlRequest& request() const noexcept;
    [[nodiscard]] const std::shared_ptr<const SdfReannotationApplication>&
    effective() const noexcept;
    [[nodiscard]] const SdfControlSummary& summary() const noexcept;
    [[nodiscard]] std::span<const SdfControlReportEntry> report() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    SdfControlApplication(SdfControlRequest request,
        std::shared_ptr<const SdfReannotationApplication> effective,
        SdfControlSummary summary,
        std::vector<SdfControlReportEntry> report,
        std::string semantic_identity);

private:
    SdfControlRequest request_;
    std::shared_ptr<const SdfReannotationApplication> effective_;
    SdfControlSummary summary_;
    std::vector<SdfControlReportEntry> report_;
    std::string semantic_identity_;
};

struct SdfControlLimits {
    std::size_t max_inputs { 16'384U };
    std::size_t max_report_entries { 1'000'000U };
    std::size_t max_source_bytes { 1U << 20U };
    std::size_t max_identity_bytes { 1U << 20U };
};

struct SdfControlResult {
    std::shared_ptr<const SdfControlApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] SdfControlResult apply_sdf_control(
    SdfControlRequest request,
    std::shared_ptr<const SdfReannotationApplication> effective = { },
    SdfControlLimits limits = { });

[[nodiscard]] std::string_view sdf_control_phase_name(
    SdfControlPhase phase) noexcept;
[[nodiscard]] std::string_view sdf_control_surface_name(
    SdfControlSurface surface) noexcept;

} // namespace fsim::app
