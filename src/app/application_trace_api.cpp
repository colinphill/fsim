// SPDX-License-Identifier: Apache-2.0

#include "application_internal.hpp"
#include "application_trace_control.hpp"
#include "fsim/app/trace_api.hpp"
#include "fsim/support/path.hpp"
#include "fsim/support/sha256.hpp"

#include <algorithm>
#include <cctype>
#include <set>
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

    [[nodiscard]] std::string report_identity(
        const TraceControlReportEntry& entry)
    {
        std::string result = "trace-control-report-v1";
        append_field(result,
            std::to_string(static_cast<unsigned>(entry.kind)));
        append_field(result, entry.name);
        append_field(result, entry.value);
        return support::Sha256::hex(support::Sha256::digest(result));
    }

    [[nodiscard]] std::optional<project::TraceFormat> effective_format(
        const TraceControlRequest& request)
    {
        if (request.output.empty()) {
            return request.lifecycle == TraceLifecycle::Disabled
                ? std::optional { request.format }
                : std::nullopt;
        }
        const auto extension
            = support::path_to_utf8(request.output.extension());
        std::string normalized(extension);
        std::ranges::transform(normalized, normalized.begin(),
            [](const unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        if (request.format == project::TraceFormat::automatic) {
            return normalized == ".fst"
                ? project::TraceFormat::fst
                : project::TraceFormat::vcd;
        }
        const auto conflict
            = (request.format == project::TraceFormat::vcd
                  && normalized == ".fst")
            || (request.format == project::TraceFormat::fst
                && normalized == ".vcd");
        return conflict ? std::nullopt
                        : std::optional { request.format };
    }

    void append_report(std::vector<TraceControlReportEntry>& report,
        const TraceControlEntryKind kind,
        std::string name,
        std::string value,
        const std::size_t report_limit)
    {
        if (report.size() >= report_limit) {
            return;
        }
        TraceControlReportEntry entry {
            kind, std::move(name), std::move(value), { }
        };
        entry.canonical_identity = report_identity(entry);
        report.push_back(std::move(entry));
    }
}

const TraceControlRequest& TraceControlApplication::request() const noexcept
{
    return request_;
}

const TraceControlStatus& TraceControlApplication::status() const noexcept
{
    return status_;
}

std::span<const TraceControlReportEntry>
TraceControlApplication::report() const noexcept
{
    return report_;
}

std::string_view TraceControlApplication::semantic_identity() const noexcept
{
    return semantic_identity_;
}

TraceControlApplication::TraceControlApplication(TraceControlRequest request,
    TraceControlStatus status,
    std::vector<TraceControlReportEntry> report,
    std::string semantic_identity)
    : request_(std::move(request))
    , status_(status)
    , report_(std::move(report))
    , semantic_identity_(std::move(semantic_identity))
{
}

bool TraceControlResult::ok() const noexcept
{
    return application != nullptr && diagnostics.empty();
}

std::string_view trace_control_surface_name(
    const TraceControlSurface surface) noexcept
{
    switch (surface) {
    case TraceControlSurface::ProjectCli:
        return "project-cli";
    case TraceControlSurface::NonProjectCompile:
        return "non-project-compile";
    case TraceControlSurface::NonProjectElaborate:
        return "non-project-elaborate";
    case TraceControlSurface::NonProjectSimulate:
        return "non-project-simulate";
    case TraceControlSurface::Tcl:
        return "tcl";
    case TraceControlSurface::Debugger:
        return "debugger";
    case TraceControlSurface::CApi:
        return "c-api";
    case TraceControlSurface::CppApi:
        return "cpp-api";
    }
    return "invalid";
}

std::string_view trace_control_phase_name(
    const TraceControlPhase phase) noexcept
{
    switch (phase) {
    case TraceControlPhase::Compile:
        return "compile";
    case TraceControlPhase::Elaborate:
        return "elaborate";
    case TraceControlPhase::Simulate:
        return "simulate";
    }
    return "invalid";
}

std::string_view trace_lifecycle_name(
    const TraceLifecycle lifecycle) noexcept
{
    switch (lifecycle) {
    case TraceLifecycle::Disabled:
        return "disabled";
    case TraceLifecycle::Configured:
        return "configured";
    case TraceLifecycle::Open:
        return "open";
    case TraceLifecycle::Complete:
        return "complete";
    case TraceLifecycle::Failed:
        return "failed";
    }
    return "invalid";
}

std::string_view trace_control_entry_kind_name(
    const TraceControlEntryKind kind) noexcept
{
    switch (kind) {
    case TraceControlEntryKind::Output:
        return "output";
    case TraceControlEntryKind::Format:
        return "format";
    case TraceControlEntryKind::Compression:
        return "compression";
    case TraceControlEntryKind::Selection:
        return "selection";
    case TraceControlEntryKind::Lifecycle:
        return "lifecycle";
    }
    return "invalid";
}

TraceControlResult apply_trace_control(TraceControlRequest request,
    const TraceControlLimits limits)
{
    TraceControlResult result;
    if (request.surface > TraceControlSurface::CppApi
        || request.phase > TraceControlPhase::Simulate
        || request.format > project::TraceFormat::fst
        || request.compression > project::TraceCompression::deterministic
        || request.lifecycle > TraceLifecycle::Configured
        || request.report_limit == 0U
        || limits.max_selection_count == 0U
        || limits.max_report_entries == 0U
        || limits.max_output_bytes == 0U
        || limits.max_selection_bytes == 0U) {
        diagnose(result.diagnostics, "FSIM-TRACE-CONTROL-001",
            "trace control requires valid policies and nonzero resource limits");
        return result;
    }
    if (request.report_limit > limits.max_report_entries
        || request.selection.size() > limits.max_selection_count) {
        diagnose(result.diagnostics, "FSIM-TRACE-CONTROL-004",
            "trace control exceeds its configured selection or report limit");
        return result;
    }
    const auto output = support::path_to_utf8(request.output);
    if (output.size() > limits.max_output_bytes
        || (request.lifecycle == TraceLifecycle::Configured
            && output.empty())) {
        diagnose(result.diagnostics, "FSIM-TRACE-CONTROL-001",
            "enabled trace control requires a bounded non-empty output path");
        return result;
    }
    if (!request.output.empty()) {
        request.output = request.output.lexically_normal();
    }
    const auto format = effective_format(request);
    if (!format) {
        diagnose(result.diagnostics, "FSIM-TRACE-CONTROL-002",
            "trace format conflicts with the output extension");
        return result;
    }

    std::size_t selection_bytes { };
    std::set<std::string_view> unique_selection;
    for (const auto& selection : request.selection) {
        if (selection.empty()
            || selection.size() > limits.max_selection_bytes
            || selection_bytes > limits.max_selection_bytes - selection.size()
            || !unique_selection.insert(selection).second) {
            diagnose(result.diagnostics, "FSIM-TRACE-CONTROL-003",
                "trace selections must be non-empty, unique, and within the configured byte limit");
            return result;
        }
        selection_bytes += selection.size();
    }

    TraceControlStatus status;
    status.requested_format = request.format;
    status.effective_format = *format;
    status.requested_compression = request.compression;
    if (*format != project::TraceFormat::fst
        && request.compression == project::TraceCompression::deterministic) {
        diagnose(result.diagnostics, "FSIM-TRACE-CONTROL-001",
            "deterministic trace compression is available only for FST output");
        return result;
    }
    status.effective_compression = *format == project::TraceFormat::fst
            && request.compression != project::TraceCompression::none
        ? project::TraceCompression::deterministic
        : project::TraceCompression::none;
    status.lifecycle = request.lifecycle;
    status.selection_count = request.selection.size();
    status.report_entry_count = 4U + request.selection.size();
    status.returned_report_entry_count
        = std::min(status.report_entry_count, request.report_limit);
    status.generation = request.generation == 0U ? 1U : request.generation;
    status.report_truncated
        = status.returned_report_entry_count != status.report_entry_count;
    request.generation = status.generation;

    std::vector<TraceControlReportEntry> report;
    report.reserve(status.returned_report_entry_count);
    append_report(report, TraceControlEntryKind::Output, "output",
        support::path_to_utf8(request.output), request.report_limit);
    append_report(report, TraceControlEntryKind::Format, "format",
        std::string { project::to_string(*format) }, request.report_limit);
    append_report(report, TraceControlEntryKind::Compression, "compression",
        std::string { project::to_string(status.effective_compression) },
        request.report_limit);
    for (const auto& selection : request.selection) {
        append_report(report, TraceControlEntryKind::Selection, "selection",
            selection, request.report_limit);
    }
    append_report(report, TraceControlEntryKind::Lifecycle, "lifecycle",
        std::string { trace_lifecycle_name(request.lifecycle) },
        request.report_limit);

    std::string identity = "trace-control-v1";
    append_field(identity, std::string { trace_control_phase_name(request.phase) });
    append_field(identity, support::path_to_utf8(request.output));
    append_field(identity, std::string { project::to_string(request.format) });
    append_field(identity, std::string { project::to_string(*format) });
    append_field(identity,
        std::string { project::to_string(request.compression) });
    append_field(identity, std::string { trace_lifecycle_name(request.lifecycle) });
    append_field(identity, std::to_string(request.generation));
    for (const auto& selection : request.selection) {
        append_field(identity, selection);
    }
    identity = support::Sha256::hex(support::Sha256::digest(identity));
    result.application = std::make_shared<const TraceControlApplication>(
        std::move(request), status, std::move(report), std::move(identity));
    return result;
}

TraceControlRequest trace_control_request(const project::RunSection& run,
    const TraceControlSurface surface,
    const TraceControlPhase phase)
{
    TraceControlRequest request;
    request.surface = surface;
    request.phase = phase;
    request.output = run.trace_file.value_or(std::filesystem::path { });
    request.format = run.trace_format;
    request.compression = run.trace_compression;
    request.selection = run.trace_filters;
    request.lifecycle = run.trace_enabled && run.trace_file
        ? TraceLifecycle::Configured
        : TraceLifecycle::Disabled;
    request.report_limit = run.trace_report_limit;
    return request;
}

void publish_trace_control(const TraceControlApplication& application,
    project::RunSection& run)
{
    const auto& request = application.request();
    run.trace_file = request.output.empty()
        ? std::optional<std::filesystem::path> { }
        : std::optional { request.output };
    run.trace_format = request.format;
    run.trace_compression = request.compression;
    run.trace_filters = request.selection;
    run.trace_report_limit = request.report_limit;
    run.trace_enabled = request.lifecycle != TraceLifecycle::Disabled;
}

struct TraceRuntime::Impl {
    std::unique_ptr<application_detail::TraceState> trace;
};

std::unique_ptr<TraceRuntime> TraceRuntime::attach(Simulation& simulation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    const bool dynamic_selection,
    std::shared_ptr<const TraceControlApplication> configured)
{
    if (configured) {
        auto expected_request = trace_control_request(config.run,
            configured->request().surface, configured->request().phase);
        expected_request.generation = configured->request().generation;
        const auto expected = apply_trace_control(std::move(expected_request));
        if (!expected.ok()
            || expected.application->semantic_identity()
                != configured->semantic_identity()) {
            diagnostics.error("FSIM-TRACE-CONTROL-001",
                "preconfigured trace control does not match the project run configuration");
            return nullptr;
        }
    }
    auto trace = application_detail::attach_trace(
        simulation, config, diagnostics, dynamic_selection,
        std::move(configured));
    if (!trace) {
        return nullptr;
    }
    auto impl = std::make_unique<Impl>();
    impl->trace = std::move(trace);
    return std::unique_ptr<TraceRuntime>(
        new TraceRuntime(std::move(impl)));
}

TraceRuntime::TraceRuntime(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl))
{
}

TraceRuntime::~TraceRuntime() = default;
TraceRuntime::TraceRuntime(TraceRuntime&&) noexcept = default;
TraceRuntime& TraceRuntime::operator=(TraceRuntime&&) noexcept = default;

const TraceControlApplication& TraceRuntime::control() const noexcept
{
    return *impl_->trace->control;
}

std::shared_ptr<const TraceControlApplication>
TraceRuntime::control_handle() const noexcept
{
    return impl_->trace->control;
}

TraceControlStatus TraceRuntime::status() const
{
    auto result = control().status();
    switch (impl_->trace->terminal_status) {
    case application_detail::TraceTerminalStatus::open:
        result.lifecycle = TraceLifecycle::Open;
        break;
    case application_detail::TraceTerminalStatus::complete:
        result.lifecycle = TraceLifecycle::Complete;
        break;
    case application_detail::TraceTerminalStatus::failed:
        result.lifecycle = TraceLifecycle::Failed;
        break;
    }
    if (impl_->trace->selection) {
        const auto selection = impl_->trace->selection->status();
        result.selection_count = selection.selected;
        result.generation = selection.generation;
    }
    return result;
}

bool TraceRuntime::flush(diagnostic::Engine& diagnostics)
{
    return application_detail::flush_trace(*impl_->trace, diagnostics);
}

bool TraceRuntime::close(diagnostic::Engine& diagnostics)
{
    return application_detail::finish_trace(*impl_->trace, diagnostics);
}

void TraceRuntime::fail(diagnostic::Engine& diagnostics,
    const std::string_view message) noexcept
{
    application_detail::fail_trace(*impl_->trace, message, &diagnostics);
}

} // namespace fsim::app
