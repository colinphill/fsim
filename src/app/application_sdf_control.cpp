// SPDX-License-Identifier: Apache-2.0

#include "fsim/app/sdf_control.hpp"

#include <algorithm>
#include <map>
#include <ranges>
#include <set>
#include <tuple>
#include <utility>

namespace fsim::app {
namespace {
    using frontend::Diagnostic;
    using frontend::DiagnosticSeverity;

    void diagnose(std::vector<Diagnostic>& diagnostics, std::string code,
        std::string message)
    {
        diagnostics.push_back(Diagnostic { DiagnosticSeverity::Error,
            std::move(code), std::move(message), { }, { } });
    }

    void append_field(std::string& target, const std::string_view value)
    {
        target += std::to_string(value.size());
        target.push_back(':');
        target.append(value);
    }

    [[nodiscard]] std::string_view selection_name(
        const SdfDelaySelection selection) noexcept
    {
        switch (selection) {
        case SdfDelaySelection::Minimum:
            return "min";
        case SdfDelaySelection::Typical:
            return "typ";
        case SdfDelaySelection::Maximum:
            return "max";
        }
        return "invalid";
    }

    [[nodiscard]] std::string report_identity(
        const SdfControlReportEntry& entry)
    {
        std::string result = "sdf-control-report-v1";
        append_field(result,
            std::to_string(static_cast<unsigned>(entry.kind)));
        append_field(result, entry.source_identity);
        append_field(result, entry.object_identity);
        return result;
    }

    [[nodiscard]] bool matches_input(const SdfReannotationRevision& revision,
        const SdfControlInput& input) noexcept
    {
        return revision.file_identity == input.source_identity
            && revision.root == input.root
            && revision.cell_pattern == input.cell_pattern
            && revision.file_precedence == input.file_precedence
            && revision.cell_precedence == input.cell_precedence;
    }
}

const SdfControlRequest& SdfControlApplication::request() const noexcept
{
    return request_;
}

const std::shared_ptr<const SdfReannotationApplication>&
SdfControlApplication::effective() const noexcept
{
    return effective_;
}

const SdfControlSummary& SdfControlApplication::summary() const noexcept
{
    return summary_;
}

std::span<const SdfControlReportEntry>
SdfControlApplication::report() const noexcept
{
    return report_;
}

std::string_view SdfControlApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

SdfControlApplication::SdfControlApplication(SdfControlRequest request,
    std::shared_ptr<const SdfReannotationApplication> effective,
    SdfControlSummary summary,
    std::vector<SdfControlReportEntry> report,
    std::string semantic_identity)
    : request_(std::move(request))
    , effective_(std::move(effective))
    , summary_(summary)
    , report_(std::move(report))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool SdfControlResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

std::string_view sdf_control_phase_name(const SdfControlPhase phase) noexcept
{
    switch (phase) {
    case SdfControlPhase::Compile:
        return "compile";
    case SdfControlPhase::Elaborate:
        return "elaborate";
    case SdfControlPhase::Simulate:
        return "simulate";
    }
    return "invalid";
}

std::string_view sdf_control_surface_name(
    const SdfControlSurface surface) noexcept
{
    switch (surface) {
    case SdfControlSurface::ProjectCli:
        return "project-cli";
    case SdfControlSurface::Tcl:
        return "tcl";
    case SdfControlSurface::CApi:
        return "c-api";
    case SdfControlSurface::CppApi:
        return "cpp-api";
    }
    return "invalid";
}

SdfControlResult apply_sdf_control(SdfControlRequest request,
    std::shared_ptr<const SdfReannotationApplication> effective,
    const SdfControlLimits limits)
{
    SdfControlResult result;
    if (request.report_limit == 0U || limits.max_inputs == 0U
        || limits.max_report_entries == 0U || limits.max_source_bytes == 0U
        || limits.max_identity_bytes == 0U
        || request.selection > SdfDelaySelection::Maximum
        || request.surface > SdfControlSurface::CppApi
        || request.phase > SdfControlPhase::Simulate) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-001",
            "SDF control requires valid policies and nonzero resource limits");
        return result;
    }
    if (request.phase == SdfControlPhase::Compile
        && !request.inputs.empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-003",
            "SDF annotation is illegal during the compile phase; register it for elaborate or simulate");
        return result;
    }
    if (request.phase != SdfControlPhase::Compile
        && request.inputs.empty()) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-001",
            "SDF elaborate or simulate control requires at least one input");
        return result;
    }
    if (request.inputs.size() > limits.max_inputs
        || request.report_limit > limits.max_report_entries) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-004",
            "SDF control exceeds its configured input or report limit");
        return result;
    }

    std::size_t source_bytes { };
    std::set<std::tuple<std::string_view, std::string_view, std::string_view>>
        scopes;
    std::set<std::string_view> files;
    for (const auto& input : request.inputs) {
        if (input.source_identity.empty() || input.root.empty()
            || input.cell_pattern.empty()) {
            diagnose(result.diagnostics, "FSIM-SDF-CONTROL-001",
                "SDF control input requires source, root and cell identities");
            return result;
        }
        const auto added = input.source_identity.size() + input.root.size()
            + input.cell_pattern.size();
        if (added > limits.max_source_bytes
            || source_bytes > limits.max_source_bytes - added) {
            diagnose(result.diagnostics, "FSIM-SDF-CONTROL-004",
                "SDF control exceeds its configured source-byte limit");
            return result;
        }
        source_bytes += added;
        const auto key = std::tie(
            input.source_identity, input.root, input.cell_pattern);
        if (!scopes.emplace(key).second) {
            diagnose(result.diagnostics, "FSIM-SDF-CONTROL-002",
                "SDF control contains a duplicate or conflicting file/cell input");
            return result;
        }
        files.emplace(input.source_identity);
    }

    if (effective && (request.phase == SdfControlPhase::Compile || request.generation == 0U || effective->generation() != request.generation)) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-003",
            "SDF effective timing is incompatible with the requested phase or generation");
        return result;
    }
    if (!effective && request.generation != 0U) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-003",
            "SDF generation is nonzero without an effective annotation");
        return result;
    }
    if (effective
        && std::ranges::any_of(
            effective->revisions(), [&](const auto& revision) {
                return std::ranges::none_of(request.inputs,
                    [&](const auto& input) {
                        return matches_input(revision, input);
                    });
            })) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-003",
            "SDF effective annotation provenance is absent from the control inputs");
        return result;
    }

    SdfControlSummary summary;
    summary.input_count = request.inputs.size();
    summary.file_count = files.size();
    summary.generation = request.generation;
    summary.effective = effective != nullptr;
    if (effective) {
        summary.applied_path_count = static_cast<std::size_t>(
            std::ranges::count_if(
                effective->revisions(), [](const auto& revision) {
                    return !revision.timing_check;
                }));
        summary.applied_timing_check_count = effective->revisions().size()
            - summary.applied_path_count;
    }

    std::vector<SdfControlReportEntry> full_report;
    full_report.reserve(request.inputs.size()
        + (effective ? effective->revisions().size() : 0U));
    for (const auto& input : request.inputs) {
        SdfControlReportEntry entry;
        entry.source_identity = input.source_identity;
        entry.object_identity = input.root + ":" + input.cell_pattern;
        entry.canonical_identity = report_identity(entry);
        full_report.push_back(std::move(entry));
    }
    if (effective) {
        for (const auto& revision : effective->revisions()) {
            SdfControlReportEntry entry;
            entry.kind = revision.timing_check
                ? SdfControlEntryKind::TimingCheck
                : SdfControlEntryKind::Path;
            entry.source_identity = revision.file_identity;
            entry.object_identity = revision.target_identity;
            entry.canonical_identity = report_identity(entry);
            full_report.push_back(std::move(entry));
        }
    }
    std::ranges::sort(full_report, [](const auto& left, const auto& right) {
        return std::tie(left.kind, left.source_identity, left.object_identity)
            < std::tie(right.kind, right.source_identity, right.object_identity);
    });
    summary.report_entry_count = full_report.size();
    summary.report_truncated = full_report.size() > request.report_limit;
    if (summary.report_truncated)
        full_report.resize(request.report_limit);

    std::string identity = "sdf-control-application-v1";
    append_field(identity, sdf_control_surface_name(request.surface));
    append_field(identity, sdf_control_phase_name(request.phase));
    append_field(identity, selection_name(request.selection));
    append_field(identity, std::to_string(request.generation));
    append_field(identity, effective ? effective->semantic_identity() : "-");
    for (const auto& input : request.inputs) {
        append_field(identity, input.source_identity);
        append_field(identity, input.root);
        append_field(identity, input.cell_pattern);
        append_field(identity, std::to_string(input.file_precedence));
        append_field(identity, std::to_string(input.cell_precedence));
    }
    if (identity.size() > limits.max_identity_bytes) {
        diagnose(result.diagnostics, "FSIM-SDF-CONTROL-004",
            "SDF control semantic identity exceeds its configured limit");
        return result;
    }
    result.application = std::make_shared<const SdfControlApplication>(
        std::move(request), std::move(effective), summary,
        std::move(full_report), std::move(identity));
    return result;
}

} // namespace fsim::app
