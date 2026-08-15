// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::app {

class Simulation;

enum class TraceControlSurface : std::uint8_t {
    ProjectCli,
    NonProjectCompile,
    NonProjectElaborate,
    NonProjectSimulate,
    Tcl,
    Debugger,
    CApi,
    CppApi
};

enum class TraceControlPhase : std::uint8_t {
    Compile,
    Elaborate,
    Simulate
};

enum class TraceLifecycle : std::uint8_t {
    Disabled,
    Configured,
    Open,
    Complete,
    Failed
};

enum class TraceControlEntryKind : std::uint8_t {
    Output,
    Format,
    Compression,
    Selection,
    Lifecycle
};

struct TraceControlRequest {
    TraceControlSurface surface { TraceControlSurface::CppApi };
    TraceControlPhase phase { TraceControlPhase::Simulate };
    std::filesystem::path output;
    project::TraceFormat format { project::TraceFormat::automatic };
    project::TraceCompression compression { project::TraceCompression::automatic };
    std::vector<std::string> selection;
    TraceLifecycle lifecycle { TraceLifecycle::Configured };
    std::uint64_t generation { };
    std::size_t report_limit { 4'096U };
};

struct TraceControlReportEntry {
    TraceControlEntryKind kind { TraceControlEntryKind::Output };
    std::string name;
    std::string value;
    std::string canonical_identity;

    friend bool operator==(const TraceControlReportEntry&,
        const TraceControlReportEntry&) = default;
};

struct TraceControlStatus {
    project::TraceFormat requested_format { project::TraceFormat::automatic };
    project::TraceFormat effective_format { project::TraceFormat::automatic };
    project::TraceCompression requested_compression {
        project::TraceCompression::automatic
    };
    project::TraceCompression effective_compression {
        project::TraceCompression::none
    };
    TraceLifecycle lifecycle { TraceLifecycle::Disabled };
    std::size_t selection_count { };
    std::size_t report_entry_count { };
    std::size_t returned_report_entry_count { };
    std::uint64_t generation { };
    bool report_truncated { };
};

class TraceControlApplication final {
public:
    static constexpr std::uint32_t schema_version = 1U;

    [[nodiscard]] const TraceControlRequest& request() const noexcept;
    [[nodiscard]] const TraceControlStatus& status() const noexcept;
    [[nodiscard]] std::span<const TraceControlReportEntry> report() const
        noexcept;
    [[nodiscard]] std::string_view semantic_identity() const noexcept;

    TraceControlApplication(TraceControlRequest request,
        TraceControlStatus status,
        std::vector<TraceControlReportEntry> report,
        std::string semantic_identity);

private:
    TraceControlRequest request_;
    TraceControlStatus status_;
    std::vector<TraceControlReportEntry> report_;
    std::string semantic_identity_;
};

struct TraceControlLimits {
    std::size_t max_selection_count { 16'384U };
    std::size_t max_report_entries { 1'000'000U };
    std::size_t max_output_bytes { 32'768U };
    std::size_t max_selection_bytes { 1U << 20U };
};

struct TraceControlResult {
    std::shared_ptr<const TraceControlApplication> application;
    std::vector<frontend::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const noexcept;
};

[[nodiscard]] TraceControlResult apply_trace_control(
    TraceControlRequest request,
    TraceControlLimits limits = { });

[[nodiscard]] TraceControlRequest trace_control_request(
    const project::RunSection& run,
    TraceControlSurface surface,
    TraceControlPhase phase);

void publish_trace_control(
    const TraceControlApplication& application,
    project::RunSection& run);

class TraceRuntime final {
public:
    [[nodiscard]] static std::unique_ptr<TraceRuntime> attach(
        Simulation& simulation,
        const project::Config& config,
        diagnostic::Engine& diagnostics,
        bool dynamic_selection = false,
        std::shared_ptr<const TraceControlApplication> configured = { });

    ~TraceRuntime();
    TraceRuntime(TraceRuntime&&) noexcept;
    TraceRuntime& operator=(TraceRuntime&&) noexcept;
    TraceRuntime(const TraceRuntime&) = delete;
    TraceRuntime& operator=(const TraceRuntime&) = delete;

    [[nodiscard]] const TraceControlApplication& control() const noexcept;
    [[nodiscard]] std::shared_ptr<const TraceControlApplication>
        control_handle() const noexcept;
    [[nodiscard]] TraceControlStatus status() const;
    [[nodiscard]] bool flush(diagnostic::Engine& diagnostics);
    [[nodiscard]] bool close(diagnostic::Engine& diagnostics);
    void fail(diagnostic::Engine& diagnostics,
        std::string_view message) noexcept;

private:
    struct Impl;
    explicit TraceRuntime(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::string_view trace_control_surface_name(
    TraceControlSurface surface) noexcept;
[[nodiscard]] std::string_view trace_control_phase_name(
    TraceControlPhase phase) noexcept;
[[nodiscard]] std::string_view trace_lifecycle_name(
    TraceLifecycle lifecycle) noexcept;
[[nodiscard]] std::string_view trace_control_entry_kind_name(
    TraceControlEntryKind kind) noexcept;

} // namespace fsim::app
