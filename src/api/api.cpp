// SPDX-License-Identifier: Apache-2.0
#include "api_internal.hpp"
#include "fsim/support/path.hpp"

using namespace fsim::api::detail;

namespace {

bool valid_sdf_view(const fsim_string_view_t value) noexcept
{
    return value.data != nullptr || value.size == 0;
}

std::string sdf_string(const fsim_string_view_t value)
{
    return value.data == nullptr ? std::string { }
                                 : std::string { value.data, value.size };
}

std::optional<fsim::app::SdfDelaySelection> sdf_selection(
    const fsim_sdf_delay_selection_t value) noexcept
{
    switch (value) {
    case FSIM_SDF_DELAY_MINIMUM:
        return fsim::app::SdfDelaySelection::Minimum;
    case FSIM_SDF_DELAY_TYPICAL:
        return fsim::app::SdfDelaySelection::Typical;
    case FSIM_SDF_DELAY_MAXIMUM:
        return fsim::app::SdfDelaySelection::Maximum;
    }
    return std::nullopt;
}

std::optional<fsim::app::SdfControlPhase> sdf_phase(
    const fsim_sdf_phase_t value) noexcept
{
    switch (value) {
    case FSIM_SDF_PHASE_COMPILE:
        return fsim::app::SdfControlPhase::Compile;
    case FSIM_SDF_PHASE_ELABORATE:
        return fsim::app::SdfControlPhase::Elaborate;
    case FSIM_SDF_PHASE_SIMULATE:
        return fsim::app::SdfControlPhase::Simulate;
    }
    return std::nullopt;
}

fsim_sdf_report_kind_t sdf_report_kind(
    const fsim::app::SdfControlEntryKind value) noexcept
{
    switch (value) {
    case fsim::app::SdfControlEntryKind::Input:
        return FSIM_SDF_REPORT_INPUT;
    case fsim::app::SdfControlEntryKind::Path:
        return FSIM_SDF_REPORT_PATH;
    case fsim::app::SdfControlEntryKind::TimingCheck:
        return FSIM_SDF_REPORT_TIMING_CHECK;
    }
    return FSIM_SDF_REPORT_INPUT;
}

std::optional<fsim::project::TraceFormat> trace_format(
    const fsim_trace_format_t value) noexcept
{
    switch (value) {
    case FSIM_TRACE_FORMAT_AUTO:
        return fsim::project::TraceFormat::automatic;
    case FSIM_TRACE_FORMAT_VCD:
        return fsim::project::TraceFormat::vcd;
    case FSIM_TRACE_FORMAT_FST:
        return fsim::project::TraceFormat::fst;
    }
    return std::nullopt;
}

fsim_trace_format_t trace_format(
    const fsim::project::TraceFormat value) noexcept
{
    switch (value) {
    case fsim::project::TraceFormat::automatic:
        return FSIM_TRACE_FORMAT_AUTO;
    case fsim::project::TraceFormat::vcd:
        return FSIM_TRACE_FORMAT_VCD;
    case fsim::project::TraceFormat::fst:
        return FSIM_TRACE_FORMAT_FST;
    }
    return FSIM_TRACE_FORMAT_AUTO;
}

std::optional<fsim::project::TraceCompression> trace_compression(
    const fsim_trace_compression_t value) noexcept
{
    switch (value) {
    case FSIM_TRACE_COMPRESSION_AUTO:
        return fsim::project::TraceCompression::automatic;
    case FSIM_TRACE_COMPRESSION_NONE:
        return fsim::project::TraceCompression::none;
    case FSIM_TRACE_COMPRESSION_DETERMINISTIC:
        return fsim::project::TraceCompression::deterministic;
    }
    return std::nullopt;
}

fsim_trace_compression_t trace_compression(
    const fsim::project::TraceCompression value) noexcept
{
    switch (value) {
    case fsim::project::TraceCompression::automatic:
        return FSIM_TRACE_COMPRESSION_AUTO;
    case fsim::project::TraceCompression::none:
        return FSIM_TRACE_COMPRESSION_NONE;
    case fsim::project::TraceCompression::deterministic:
        return FSIM_TRACE_COMPRESSION_DETERMINISTIC;
    }
    return FSIM_TRACE_COMPRESSION_AUTO;
}

std::optional<fsim::app::TraceLifecycle> trace_lifecycle(
    const fsim_trace_lifecycle_t value) noexcept
{
    switch (value) {
    case FSIM_TRACE_LIFECYCLE_DISABLED:
        return fsim::app::TraceLifecycle::Disabled;
    case FSIM_TRACE_LIFECYCLE_CONFIGURED:
        return fsim::app::TraceLifecycle::Configured;
    case FSIM_TRACE_LIFECYCLE_OPEN:
    case FSIM_TRACE_LIFECYCLE_COMPLETE:
    case FSIM_TRACE_LIFECYCLE_FAILED:
        return std::nullopt;
    }
    return std::nullopt;
}

fsim_trace_lifecycle_t trace_lifecycle(
    const fsim::app::TraceLifecycle value) noexcept
{
    switch (value) {
    case fsim::app::TraceLifecycle::Disabled:
        return FSIM_TRACE_LIFECYCLE_DISABLED;
    case fsim::app::TraceLifecycle::Configured:
        return FSIM_TRACE_LIFECYCLE_CONFIGURED;
    case fsim::app::TraceLifecycle::Open:
        return FSIM_TRACE_LIFECYCLE_OPEN;
    case fsim::app::TraceLifecycle::Complete:
        return FSIM_TRACE_LIFECYCLE_COMPLETE;
    case fsim::app::TraceLifecycle::Failed:
        return FSIM_TRACE_LIFECYCLE_FAILED;
    }
    return FSIM_TRACE_LIFECYCLE_FAILED;
}

std::optional<fsim::app::TraceControlPhase> trace_phase(
    const fsim_trace_phase_t value) noexcept
{
    switch (value) {
    case FSIM_TRACE_PHASE_COMPILE:
        return fsim::app::TraceControlPhase::Compile;
    case FSIM_TRACE_PHASE_ELABORATE:
        return fsim::app::TraceControlPhase::Elaborate;
    case FSIM_TRACE_PHASE_SIMULATE:
        return fsim::app::TraceControlPhase::Simulate;
    }
    return std::nullopt;
}

fsim_trace_report_kind_t trace_report_kind(
    const fsim::app::TraceControlEntryKind value) noexcept
{
    switch (value) {
    case fsim::app::TraceControlEntryKind::Output:
        return FSIM_TRACE_REPORT_OUTPUT;
    case fsim::app::TraceControlEntryKind::Format:
        return FSIM_TRACE_REPORT_FORMAT;
    case fsim::app::TraceControlEntryKind::Compression:
        return FSIM_TRACE_REPORT_COMPRESSION;
    case fsim::app::TraceControlEntryKind::Selection:
        return FSIM_TRACE_REPORT_SELECTION;
    case fsim::app::TraceControlEntryKind::Lifecycle:
        return FSIM_TRACE_REPORT_LIFECYCLE;
    }
    return FSIM_TRACE_REPORT_OUTPUT;
}

} // namespace

extern "C" {

uint32_t fsim_get_api_version(void)
{
    return FSIM_API_VERSION;
}

const char* fsim_status_string(const fsim_status_t status)
{
    switch (status) {
    case FSIM_STATUS_OK:
        return "ok";
    case FSIM_STATUS_INVALID_ARGUMENT:
        return "invalid argument";
    case FSIM_STATUS_INVALID_HANDLE:
        return "invalid handle";
    case FSIM_STATUS_INCOMPATIBLE_ABI:
        return "incompatible ABI";
    case FSIM_STATUS_IO_ERROR:
        return "I/O error";
    case FSIM_STATUS_COMPILE_ERROR:
        return "compile error";
    case FSIM_STATUS_RUNTIME_ERROR:
        return "runtime error";
    case FSIM_STATUS_STOPPED:
        return "stopped";
    case FSIM_STATUS_UNAVAILABLE:
        return "unavailable";
    case FSIM_STATUS_INTERNAL_ERROR:
        return "internal error";
    }
    return "unknown status";
}

fsim_status_t fsim_session_create(
    const fsim_session_options_t* options,
    fsim_session_t* out_session)
{
    if (out_session == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    *out_session = FSIM_INVALID_SESSION;
    if (options != nullptr
        && !valid_struct_header(
            options->struct_size,
            options->api_version,
            FSIM_STRUCT_HEADER_SIZE)) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    try {
        auto session = std::make_shared<Session>();
        if (options != nullptr) {
            if (FSIM_STRUCT_CONTAINS(
                    options->struct_size,
                    fsim_session_options_t,
                    max_deltas)) {
                session->max_deltas = options->max_deltas == 0 ? std::uint64_t { 100'000 }
                                                               : options->max_deltas;
            }
            if (FSIM_STRUCT_CONTAINS(
                    options->struct_size, fsim_session_options_t, seed)) {
                session->seed = options->seed;
                session->seed_override = true;
            }
        }
        fsim_session_t handle = next_session.fetch_add(1, std::memory_order_relaxed);
        if (handle == FSIM_INVALID_SESSION) {
            handle = next_session.fetch_add(1, std::memory_order_relaxed);
        }
        session->handle = handle;
        {
            std::lock_guard lock(registry_mutex);
            sessions.emplace(handle, session);
        }
        *out_session = handle;
        return FSIM_STATUS_OK;
    } catch (...) {
        return FSIM_STATUS_INTERNAL_ERROR;
    }
}

fsim_status_t fsim_session_destroy(const fsim_session_t session)
{
    try {
        auto state = find_session(session);
        if (!state) {
            return FSIM_STATUS_INVALID_HANDLE;
        }
        std::lock_guard session_lock(state->mutex);
        if (state->destroyed.load(std::memory_order_acquire)) {
            return FSIM_STATUS_INVALID_HANDLE;
        }
        if (mutation_forbidden(*state)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        {
            std::lock_guard lock(registry_mutex);
            const auto found = sessions.find(session);
            if (found == sessions.end() || found->second.get() != state.get()) {
                return FSIM_STATUS_INVALID_HANDLE;
            }
            state->destroyed.store(true, std::memory_order_release);
            sessions.erase(found);
        }
        state->stop_requested.store(true, std::memory_order_relaxed);
        state->external_stop_seen.store(true, std::memory_order_relaxed);
        if (state->running.load(std::memory_order_acquire)
            && state->simulation) {
            state->simulation->request_stop();
        }
        return FSIM_STATUS_OK;
    } catch (...) {
        return FSIM_STATUS_INTERNAL_ERROR;
    }
}

fsim_status_t fsim_session_load_project(
    const fsim_session_t session,
    const char* manifest_path)
{
    if (manifest_path == nullptr || *manifest_path == '\0') {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        value.trace_runtime.reset();
        value.diagnostics.clear();
        value.project.reset();
        value.trace_control.reset();
        value.trace_lifecycle_override.reset();
        value.simulation.reset();
        value.scopes.clear();
        value.process_names.clear();
        value.variables.clear();
        value.drivers.clear();
        advance_design_generation(value);
        value.finished = false;
        value.current_execution_process.reset();
        auto loaded = fsim::project::load(
            fsim::support::path_from_utf8(manifest_path), value.diagnostics);
        if (!loaded) {
            return FSIM_STATUS_COMPILE_ERROR;
        }
        value.max_deltas = loaded->run.max_deltas;
        if (value.seed_override) {
            loaded->project.seed = value.seed;
            loaded->project.random_seed = false;
        } else if (!loaded->project.random_seed) {
            value.seed = loaded->project.seed;
        }
        auto trace = fsim::app::apply_trace_control(
            fsim::app::trace_control_request(loaded->run,
                fsim::app::TraceControlSurface::CApi,
                fsim::app::TraceControlPhase::Simulate));
        if (!trace.ok()) {
            for (const auto& entry : trace.diagnostics) {
                value.diagnostics.error(entry.code, entry.message);
            }
            return FSIM_STATUS_COMPILE_ERROR;
        }
        value.trace_control = std::move(trace.application);
        value.project = std::move(loaded);
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_check(const fsim_session_t session)
{
    return with_session(session, [](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        value.diagnostics.clear();
        if (!value.project) {
            value.diagnostics.error(
                "FSIM-API-0002", "load a project before checking it");
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        return fsim::app::check_project(*value.project, value.diagnostics)
            ? FSIM_STATUS_OK
            : FSIM_STATUS_COMPILE_ERROR;
    });
}

fsim_status_t fsim_session_build(const fsim_session_t session)
{
    return with_session(session, [](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        value.trace_runtime.reset();
        value.trace_lifecycle_override.reset();
        value.diagnostics.clear();
        value.simulation.reset();
        value.scopes.clear();
        value.process_names.clear();
        value.variables.clear();
        value.drivers.clear();
        advance_design_generation(value);
        value.finished = false;
        value.current_execution_process.reset();
        if (!value.project) {
            value.diagnostics.error(
                "FSIM-API-0002", "load a project before building it");
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        auto built = fsim::app::build_project(
            *value.project, value.diagnostics);
        if (!built) {
            return FSIM_STATUS_COMPILE_ERROR;
        }
        value.simulation = std::make_unique<fsim::app::Simulation>(
            std::move(*built), value.max_deltas);
        if (value.project->run.trace_file
            && value.project->run.trace_enabled) {
            value.trace_runtime = fsim::app::TraceRuntime::attach(
                *value.simulation, *value.project, value.diagnostics, true,
                value.trace_control);
            if (!value.trace_runtime) {
                value.trace_lifecycle_override
                    = fsim::app::TraceLifecycle::Failed;
                value.simulation.reset();
                return FSIM_STATUS_RUNTIME_ERROR;
            }
            value.trace_control = value.trace_runtime->control_handle();
        }
        if (!rebuild_debug_objects(value)) {
            if (value.trace_runtime) {
                value.trace_lifecycle_override
                    = fsim::app::TraceLifecycle::Failed;
            }
            value.trace_runtime.reset();
            value.simulation.reset();
            value.diagnostics.error(
                "FSIM-API-0004",
                "the design contains too many debug-visible scopes, local "
                "variables, or drivers for the version-1 object handle encoding");
            return FSIM_STATUS_INTERNAL_ERROR;
        }
        const auto native_cache = value.simulation->native_cache_statistics();
        if (native_cache.load_failures != 0
            || native_cache.store_failures != 0) {
            value.diagnostics.warning(
                "FSIM-CACHE-0004",
                "native LLVM object cache reported "
                    + std::to_string(native_cache.load_failures)
                    + " load failure(s) and "
                    + std::to_string(native_cache.store_failures)
                    + " store failure(s); simulation remains valid, but cache "
                      "reuse may be incomplete");
        }
        attach_callbacks(value);
        lifecycle(value, FSIM_LIFECYCLE_DESIGN_LOADED);
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_configure_sdf(
    const fsim_session_t session,
    const fsim_sdf_options_t* options)
{
    if (options == nullptr
        || !valid_struct_header(
            options->struct_size, options->api_version, sizeof(*options))
        || (options->inputs == nullptr && options->input_count != 0)) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    const auto selection = sdf_selection(options->selection);
    const auto phase = sdf_phase(options->phase);
    if (!selection || !phase) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        fsim::app::SdfControlRequest request;
        request.surface = fsim::app::SdfControlSurface::CApi;
        request.phase = *phase;
        request.selection = *selection;
        request.report_limit = options->report_limit;
        request.inputs.reserve(options->input_count);
        for (std::size_t index = 0; index < options->input_count; ++index) {
            const auto& input = options->inputs[index];
            if (!valid_struct_header(
                    input.struct_size, input.api_version, sizeof(input))
                || !valid_sdf_view(input.source_identity)
                || !valid_sdf_view(input.root)
                || !valid_sdf_view(input.cell_pattern)) {
                return FSIM_STATUS_INCOMPATIBLE_ABI;
            }
            request.inputs.push_back(fsim::app::SdfControlInput {
                sdf_string(input.source_identity),
                sdf_string(input.root),
                sdf_string(input.cell_pattern),
                input.file_precedence,
                input.cell_precedence });
        }
        auto configured = fsim::app::apply_sdf_control(std::move(request));
        if (!configured.ok()) {
            value.diagnostics.clear();
            for (const auto& entry : configured.diagnostics) {
                value.diagnostics.error(entry.code, entry.message);
            }
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        value.diagnostics.clear();
        value.sdf_control = std::move(configured.application);
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_get_sdf_summary(
    const fsim_session_t session,
    fsim_sdf_summary_t* out_summary)
{
    if (out_summary == nullptr
        || !valid_struct_header(out_summary->struct_size,
            out_summary->api_version,
            sizeof(*out_summary))) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        if (!value.sdf_control) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        const auto& summary = value.sdf_control->summary();
        out_summary->input_count = summary.input_count;
        out_summary->file_count = summary.file_count;
        out_summary->applied_path_count = summary.applied_path_count;
        out_summary->applied_timing_check_count = summary.applied_timing_check_count;
        out_summary->report_entry_count = summary.report_entry_count;
        out_summary->returned_report_entry_count = value.sdf_control->report().size();
        out_summary->generation = summary.generation;
        out_summary->effective = summary.effective ? 1U : 0U;
        out_summary->report_truncated = summary.report_truncated ? 1U : 0U;
        out_summary->semantic_identity = view(value.sdf_control->semantic_identity());
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_get_sdf_report_entry(
    const fsim_session_t session,
    const size_t index,
    fsim_sdf_report_entry_t* out_entry)
{
    if (out_entry == nullptr
        || !valid_struct_header(out_entry->struct_size,
            out_entry->api_version,
            sizeof(*out_entry))) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        if (!value.sdf_control || index >= value.sdf_control->report().size()) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        const auto& entry = value.sdf_control->report()[index];
        out_entry->kind = sdf_report_kind(entry.kind);
        out_entry->reserved = 0;
        out_entry->source_identity = view(entry.source_identity);
        out_entry->object_identity = view(entry.object_identity);
        out_entry->canonical_identity = view(entry.canonical_identity);
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_configure_trace(
    const fsim_session_t session,
    const fsim_trace_options_t* options)
{
    if (options == nullptr
        || !valid_struct_header(
            options->struct_size, options->api_version, sizeof(*options))
        || !valid_sdf_view(options->output)
        || (options->selections == nullptr && options->selection_count != 0U)) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    const auto format = trace_format(options->format);
    const auto compression = trace_compression(options->compression);
    const auto lifecycle_value = trace_lifecycle(options->lifecycle);
    const auto phase = trace_phase(options->phase);
    if (!format || !compression || !lifecycle_value || !phase) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value) || value.simulation) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        fsim::app::TraceControlRequest request;
        request.surface = fsim::app::TraceControlSurface::CApi;
        request.phase = *phase;
        request.output = fsim::support::path_from_utf8(
            sdf_string(options->output));
        if (!request.output.empty() && request.output.is_relative()
            && value.project) {
            request.output
                = value.project->base_directory / request.output;
        }
        request.format = *format;
        request.compression = *compression;
        request.lifecycle = *lifecycle_value;
        request.report_limit = options->report_limit;
        request.generation = options->generation;
        request.selection.reserve(options->selection_count);
        for (std::size_t index = 0; index < options->selection_count; ++index) {
            const auto selection = options->selections[index];
            if (!valid_sdf_view(selection)) {
                return FSIM_STATUS_INCOMPATIBLE_ABI;
            }
            request.selection.push_back(sdf_string(selection));
        }
        auto configured = fsim::app::apply_trace_control(std::move(request));
        if (!configured.ok()) {
            value.diagnostics.clear();
            for (const auto& entry : configured.diagnostics) {
                value.diagnostics.error(entry.code, entry.message);
            }
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        value.diagnostics.clear();
        value.trace_control = std::move(configured.application);
        value.trace_lifecycle_override.reset();
        if (value.project) {
            fsim::app::publish_trace_control(
                *value.trace_control, value.project->run);
        }
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_get_trace_status(
    const fsim_session_t session,
    fsim_trace_status_t* out_status)
{
    if (out_status == nullptr
        || !valid_struct_header(out_status->struct_size,
            out_status->api_version,
            sizeof(*out_status))) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        if (!value.trace_control) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        auto status = value.trace_runtime
            ? value.trace_runtime->status()
            : value.trace_control->status();
        if (value.trace_lifecycle_override) {
            status.lifecycle = *value.trace_lifecycle_override;
        }
        out_status->requested_format = trace_format(status.requested_format);
        out_status->effective_format = trace_format(status.effective_format);
        out_status->requested_compression
            = trace_compression(status.requested_compression);
        out_status->effective_compression
            = trace_compression(status.effective_compression);
        out_status->lifecycle = trace_lifecycle(status.lifecycle);
        out_status->reserved = 0U;
        out_status->selection_count = status.selection_count;
        out_status->report_entry_count = status.report_entry_count;
        out_status->returned_report_entry_count
            = status.returned_report_entry_count;
        out_status->generation = status.generation;
        out_status->report_truncated = status.report_truncated ? 1U : 0U;
        out_status->reserved2 = 0U;
        out_status->output = view(value.trace_control->report().front().value);
        out_status->semantic_identity
            = view(value.trace_control->semantic_identity());
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_get_trace_report_entry(
    const fsim_session_t session,
    const size_t index,
    fsim_trace_report_entry_t* out_entry)
{
    if (out_entry == nullptr
        || !valid_struct_header(out_entry->struct_size,
            out_entry->api_version,
            sizeof(*out_entry))) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        if (!value.trace_control
            || index >= value.trace_control->report().size()) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        const auto& entry = value.trace_control->report()[index];
        out_entry->kind = trace_report_kind(entry.kind);
        out_entry->reserved = 0U;
        out_entry->name = view(entry.name);
        out_entry->value = view(entry.value);
        out_entry->canonical_identity = view(entry.canonical_identity);
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_flush_trace(const fsim_session_t session)
{
    return with_session(session, [](Session& value) {
        if (mutation_forbidden(value) || !value.trace_runtime) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        const auto lifecycle = value.trace_runtime->status().lifecycle;
        if (lifecycle == fsim::app::TraceLifecycle::Complete) {
            return FSIM_STATUS_STOPPED;
        }
        if (lifecycle == fsim::app::TraceLifecycle::Failed) {
            return FSIM_STATUS_RUNTIME_ERROR;
        }
        return value.trace_runtime->flush(value.diagnostics)
            ? FSIM_STATUS_OK
            : FSIM_STATUS_RUNTIME_ERROR;
    });
}

fsim_status_t fsim_session_close_trace(const fsim_session_t session)
{
    return with_session(session, [](Session& value) {
        if (mutation_forbidden(value) || !value.trace_runtime
            || !value.finished) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        return value.trace_runtime->close(value.diagnostics)
            ? FSIM_STATUS_OK
            : FSIM_STATUS_RUNTIME_ERROR;
    });
}

fsim_status_t fsim_session_root(
    const fsim_session_t session,
    fsim_object_t* out_root)
{
    if (out_root == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    *out_root = FSIM_INVALID_OBJECT;
    return with_session(session, [&](Session& value) {
        if (!ready(value, "hierarchy access")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        *out_root = root_handle(value);
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_find_object(
    const fsim_session_t session,
    const fsim_string_view_t path,
    fsim_object_t* out_object)
{
    if ((path.data == nullptr && path.size != 0) || out_object == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    *out_object = FSIM_INVALID_OBJECT;
    return with_session(session, [&](Session& value) {
        if (!ready(value, "object lookup")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        const auto requested = path.data == nullptr ? std::string_view { }
                                                    : std::string_view { path.data, path.size };
        const auto& design = value.simulation->design_ir();
        if (requested.empty()
            || (!has_synthetic_root(design)
                && is_design_root(design, requested))) {
            *out_object = root_handle(value);
            return FSIM_STATUS_OK;
        }
        for (std::size_t index = 0;
            index < value.systemc_design_object_by_object.size(); ++index) {
            const auto* design_object = design_systemc_object(value, index);
            const auto* process = design_systemc_process(value, index);
            if ((design_object != nullptr && design_object->path == requested)
                || (process != nullptr && process->name == requested)) {
                *out_object = debug_systemc_object_handle(value, index);
                return FSIM_STATUS_OK;
            }
        }
        if (const auto signal = value.simulation->find_signal(requested)) {
            *out_object = signal_handle(value, *signal);
            return FSIM_STATUS_OK;
        }
        for (std::size_t index = 0;
            index < value.scopes.size(); ++index) {
            if (value.scopes[index].full_name == requested) {
                *out_object = scope_handle(value, index);
                return FSIM_STATUS_OK;
            }
        }
        for (std::size_t index = 0;
            index < value.process_names.size(); ++index) {
            if (value.process_names[index] == requested) {
                *out_object = process_handle(value, index);
                return FSIM_STATUS_OK;
            }
        }
        for (std::size_t index = 0;
            index < value.variables.size(); ++index) {
            if (value.variables[index].full_name == requested) {
                *out_object = variable_handle(value, index);
                return FSIM_STATUS_OK;
            }
        }
        for (std::size_t index = 0;
            index < value.drivers.size(); ++index) {
            if (value.drivers[index].full_name == requested) {
                *out_object = driver_handle(value, index);
                return FSIM_STATUS_OK;
            }
        }
        return FSIM_STATUS_INVALID_HANDLE;
    });
}

fsim_status_t fsim_session_visit_children(
    const fsim_session_t session,
    const fsim_object_t parent,
    const fsim_visit_object_callback_t callback,
    void* user_data)
{
    if (callback == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (!ready(value, "hierarchy enumeration")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        const auto visit_systemc_children =
            [&](const std::string_view parent_name) {
                for (std::size_t index = 0;
                    index < value.systemc_design_object_by_object.size();
                    ++index) {
                    const auto* object = design_systemc_object(value, index);
                    const auto* process = design_systemc_process(value, index);
                    const auto path = object != nullptr
                        ? std::string_view { object->path }
                        : process != nullptr ? std::string_view { process->name }
                                             : std::string_view { };
                    const auto separator = path.rfind('.');
                    const auto object_parent = separator == std::string_view::npos
                        ? std::string_view { }
                        : path.substr(0, separator);
                    if (object_parent != parent_name
                        || (object != nullptr
                            && object->kind
                                == fsim::semantic::design::ObjectKind::systemc_module)) {
                        continue;
                    }
                    CallbackGuard guard { value };
                    if (callback(
                            value.handle,
                            debug_systemc_object_handle(value, index),
                            user_data)
                        == 0) {
                        return false;
                    }
                }
                return true;
            };
        const auto systemc_replaces_signal =
            [&](const fsim::semantic::design::Object& signal,
                const std::string_view parent_name) {
                for (std::size_t index = 0;
                    index < value.systemc_design_object_by_object.size();
                    ++index) {
                    const auto* object = design_systemc_object(value, index);
                    if (object == nullptr || object->width == 0
                        || object->runtime_index != signal.runtime_index
                        || leaf_name(object->path) != leaf_name(signal.path)) {
                        continue;
                    }
                    const auto separator = object->path.rfind('.');
                    const auto object_parent = separator == std::string::npos
                        ? std::string_view { }
                        : std::string_view { object->path }.substr(0, separator);
                    if (object_parent == parent_name) {
                        return true;
                    }
                }
                return false;
            };
        if (parent != root_handle(value)) {
            if (const auto signal = object_signal(value, parent)) {
                for (std::size_t index = 0;
                    index < value.drivers.size(); ++index) {
                    if (value.drivers[index].signal != *signal) {
                        continue;
                    }
                    CallbackGuard guard { value };
                    if (callback(
                            value.handle,
                            driver_handle(value, index),
                            user_data)
                        == 0) {
                        break;
                    }
                }
                return FSIM_STATUS_OK;
            }
            if (object_variable(value, parent)
                || object_driver(value, parent)) {
                return FSIM_STATUS_OK;
            }
            if (const auto process = object_process(value, parent)) {
                for (std::size_t index = 0;
                    index < value.scopes.size(); ++index) {
                    const auto& scope = value.scopes[index];
                    if (scope.kind != ScopeObjectKind::lexical
                        || scope.process != *process || scope.parent_scope) {
                        continue;
                    }
                    CallbackGuard guard { value };
                    if (callback(
                            value.handle,
                            scope_handle(value, index),
                            user_data)
                        == 0) {
                        return FSIM_STATUS_OK;
                    }
                }
                for (std::size_t index = 0;
                    index < value.variables.size(); ++index) {
                    if (value.variables[index].process != *process
                        || value.variables[index].parent_scope) {
                        continue;
                    }
                    CallbackGuard guard { value };
                    if (callback(
                            value.handle,
                            variable_handle(value, index),
                            user_data)
                        == 0) {
                        break;
                    }
                }
                return FSIM_STATUS_OK;
            }
            if (object_systemc(value, parent)) {
                return FSIM_STATUS_OK;
            }
            if (const auto scope_index = object_scope(value, parent)) {
                const auto& parent_scope = value.scopes[*scope_index];
                for (std::size_t index = 0;
                    index < value.scopes.size(); ++index) {
                    const auto& scope = value.scopes[index];
                    if (scope.parent_scope != scope_index) {
                        continue;
                    }
                    CallbackGuard guard { value };
                    if (callback(
                            value.handle,
                            scope_handle(value, index),
                            user_data)
                        == 0) {
                        return FSIM_STATUS_OK;
                    }
                }
                if (parent_scope.kind != ScopeObjectKind::lexical) {
                    if (!visit_systemc_children(parent_scope.full_name)) {
                        return FSIM_STATUS_OK;
                    }
                    for (const auto& signal :
                        value.simulation->design_ir().objects()) {
                        if (signal.kind
                                != fsim::semantic::design::ObjectKind::signal
                            || signal.parent_object
                            || owning_design_scope(value, signal.path)
                                != scope_index
                            || systemc_replaces_signal(
                                signal, parent_scope.full_name)) {
                            continue;
                        }
                        CallbackGuard guard { value };
                        if (callback(
                                value.handle,
                                signal_handle(
                                    value,
                                    static_cast<fsim::runtime::simir::SignalId>(
                                        signal.runtime_index)),
                                user_data)
                            == 0) {
                            return FSIM_STATUS_OK;
                        }
                    }
                    for (const auto& process :
                        value.simulation->design_ir().processes()) {
                        const auto index = static_cast<std::size_t>(
                            process.runtime_index);
                        if (owning_design_scope(value, process.name)
                                != scope_index
                            || (index < value.systemc_object_by_process.size()
                                && value.systemc_object_by_process[index])) {
                            continue;
                        }
                        CallbackGuard guard { value };
                        if (callback(
                                value.handle,
                                process_handle(value, index),
                                user_data)
                            == 0) {
                            return FSIM_STATUS_OK;
                        }
                    }
                    return FSIM_STATUS_OK;
                }
                for (std::size_t index = 0;
                    index < value.variables.size(); ++index) {
                    if (value.variables[index].parent_scope != scope_index) {
                        continue;
                    }
                    CallbackGuard guard { value };
                    if (callback(
                            value.handle,
                            variable_handle(value, index),
                            user_data)
                        == 0) {
                        break;
                    }
                }
                return FSIM_STATUS_OK;
            }
            return FSIM_STATUS_INVALID_HANDLE;
        }
        for (std::size_t index = 0;
            index < value.scopes.size(); ++index) {
            const auto& scope = value.scopes[index];
            if (scope.kind == ScopeObjectKind::lexical
                || scope.parent_scope) {
                continue;
            }
            CallbackGuard guard { value };
            if (callback(
                    value.handle, scope_handle(value, index), user_data)
                == 0) {
                return FSIM_STATUS_OK;
            }
        }
        if (has_synthetic_root(value.simulation->design_ir())) {
            return FSIM_STATUS_OK;
        }
        const auto& top = value.simulation->design_ir().top();
        if (!visit_systemc_children(top)) {
            return FSIM_STATUS_OK;
        }
        for (const auto& signal : value.simulation->design_ir().objects()) {
            if (signal.kind != fsim::semantic::design::ObjectKind::signal
                || signal.parent_object
                || owning_design_scope(value, signal.path)
                || systemc_replaces_signal(signal, top)) {
                continue;
            }
            CallbackGuard guard { value };
            if (callback(
                    value.handle,
                    signal_handle(
                        value,
                        static_cast<fsim::runtime::simir::SignalId>(
                            signal.runtime_index)),
                    user_data)
                == 0) {
                return FSIM_STATUS_OK;
            }
        }
        for (const auto& process : value.simulation->design_ir().processes()) {
            const auto index = static_cast<std::size_t>(process.runtime_index);
            if (owning_design_scope(value, process.name)
                || (index < value.systemc_object_by_process.size()
                    && value.systemc_object_by_process[index])) {
                continue;
            }
            CallbackGuard guard { value };
            if (callback(value.handle, process_handle(value, index), user_data) == 0) {
                break;
            }
        }
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_get_object_info(
    const fsim_session_t session,
    const fsim_object_t object,
    fsim_object_info_t* out_info)
{
    if (out_info == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    if (!valid_struct_header(
            out_info->struct_size,
            out_info->api_version,
            FSIM_OBJECT_INFO_V1_SIZE)) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        if (!ready(value, "object metadata")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        out_info->handle = object;
        out_info->flags = 0;
        const auto write_source_path = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, source_path);
        const auto write_source_line = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, source_line);
        const auto write_source_column = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, source_column);
        const auto write_provenance_unit = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_unit);
        const auto write_provenance_source_path = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_source_path);
        const auto write_provenance_language = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_language);
        const auto write_provenance_standard = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_standard);
        const auto write_provenance_profile = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t,
            provenance_compatibility_profile);
        const auto write_provenance_unit_id = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_unit_id);
        const auto write_provenance_source_id = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_source_id);
        const auto write_provenance_source_line = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_source_line);
        const auto write_provenance_source_column = FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, provenance_source_column);
        if (write_source_path) {
            out_info->source_path = view("");
        }
        if (write_source_line) {
            out_info->source_line = 0;
        }
        if (write_source_column) {
            out_info->source_column = 0;
        }
        if (write_provenance_unit)
            out_info->provenance_unit = view("");
        if (write_provenance_source_path) {
            out_info->provenance_source_path = view("");
        }
        if (write_provenance_language)
            out_info->provenance_language = view("");
        if (write_provenance_standard)
            out_info->provenance_standard = view("");
        if (write_provenance_profile) {
            out_info->provenance_compatibility_profile = view("");
        }
        if (write_provenance_unit_id) {
            out_info->provenance_unit_id = std::numeric_limits<std::uint32_t>::max();
        }
        if (write_provenance_source_id) {
            out_info->provenance_source_id
                = std::numeric_limits<std::uint32_t>::max();
        }
        if (write_provenance_source_line)
            out_info->provenance_source_line = 0;
        if (write_provenance_source_column)
            out_info->provenance_source_column = 0;
        value.query_provenance.reset();
        const auto set_source =
            [&](const std::string_view path,
                const std::uint32_t line,
                const std::uint32_t column) {
                if (path.empty()
                    || (!write_source_path && !write_source_line
                        && !write_source_column)) {
                    return;
                }
                if (write_source_path) {
                    out_info->source_path = view(path);
                }
                if (write_source_line) {
                    out_info->source_line = line;
                }
                if (write_source_column) {
                    out_info->source_column = column;
                }
                out_info->flags |= FSIM_OBJECT_FLAG_HAS_SOURCE;
            };
        const auto set_provenance = [&](const std::string_view path) {
            const bool requested = write_provenance_unit
                || write_provenance_source_path || write_provenance_language
                || write_provenance_standard || write_provenance_profile
                || write_provenance_unit_id || write_provenance_source_id
                || write_provenance_source_line || write_provenance_source_column;
            if (!requested)
                return;
            value.query_provenance
                = value.simulation->verilog_scope_provenance(path);
            if (!value.query_provenance)
                return;
            const auto& provenance = *value.query_provenance;
            if (write_provenance_unit) {
                out_info->provenance_unit = view(provenance.semantic_unit);
            }
            if (write_provenance_source_path) {
                out_info->provenance_source_path = view(provenance.source_path);
            }
            if (write_provenance_language) {
                out_info->provenance_language = view(
                    provenance.language == fsim::semantic::Language::verilog
                        ? "verilog"
                        : "systemverilog");
            }
            if (write_provenance_standard) {
                out_info->provenance_standard = view(provenance.standard);
            }
            if (write_provenance_profile) {
                out_info->provenance_compatibility_profile
                    = view(provenance.compatibility_profile);
            }
            if (write_provenance_unit_id) {
                out_info->provenance_unit_id = provenance.unit.value();
            }
            if (write_provenance_source_id) {
                out_info->provenance_source_id = provenance.source.value();
            }
            if (write_provenance_source_line) {
                out_info->provenance_source_line = provenance.source_line;
            }
            if (write_provenance_source_column) {
                out_info->provenance_source_column = provenance.source_column;
            }
            out_info->flags |= FSIM_OBJECT_FLAG_HAS_PROVENANCE;
        };
        if (object == root_handle(value)) {
            const auto& design = value.simulation->design_ir();
            const auto name = has_synthetic_root(design)
                ? std::string_view { "$root" }
                : std::string_view { design.top() };
            out_info->parent = FSIM_INVALID_OBJECT;
            out_info->kind = FSIM_OBJECT_ROOT;
            out_info->width = 0;
            out_info->name = view(name);
            out_info->full_name = view(name);
            out_info->type_name = view("design");
            set_provenance(name);
            return FSIM_STATUS_OK;
        }
        if (const auto systemc = object_systemc(value, object)) {
            const auto* design_object = design_systemc_object(value, *systemc);
            const auto* process = design_systemc_process(value, *systemc);
            const auto path = design_object != nullptr
                ? std::string_view { design_object->path }
                : process != nullptr ? std::string_view { process->name }
                                     : std::string_view { };
            if (path.empty()) {
                return FSIM_STATUS_INVALID_HANDLE;
            }
            out_info->parent = root_handle(value);
            const auto separator = path.rfind('.');
            const auto parent_path = separator == std::string_view::npos
                ? std::string_view { }
                : path.substr(0, separator);
            const auto& design = value.simulation->design_ir();
            if (!parent_path.empty()
                && (has_synthetic_root(design)
                    || !is_design_root(design, parent_path))) {
                const auto parent = std::ranges::find_if(
                    value.simulation->design_ir().objects(),
                    [&](const fsim::semantic::design::Object& candidate) {
                        return candidate.path == parent_path;
                    });
                if (parent != value.simulation->design_ir().objects().end()) {
                    if (const auto adapter = systemc_adapter_for_object(value, parent->id)) {
                        out_info->parent = debug_systemc_object_handle(value, *adapter);
                    }
                } else {
                    const auto scope_parent = std::find_if(
                        value.scopes.begin(), value.scopes.end(),
                        [&](const ScopeObject& candidate) {
                            return candidate.full_name == parent_path;
                        });
                    if (scope_parent != value.scopes.end()) {
                        out_info->parent = scope_handle(
                            value,
                            static_cast<std::size_t>(
                                std::distance(value.scopes.begin(), scope_parent)));
                    }
                }
            }
            out_info->width = design_object == nullptr ? 0 : design_object->width;
            out_info->name = view(leaf_name(path));
            out_info->full_name = view(path);
            out_info->type_name = design_object == nullptr
                ? view("process")
                : view(design_object->external_type);
            if (process != nullptr) {
                out_info->kind = FSIM_OBJECT_PROCESS;
            } else {
                switch (design_object->kind) {
                case fsim::semantic::design::ObjectKind::systemc_module:
                    out_info->kind = FSIM_OBJECT_SCOPE;
                    break;
                case fsim::semantic::design::ObjectKind::systemc_port:
                    out_info->kind = FSIM_OBJECT_PORT;
                    break;
                case fsim::semantic::design::ObjectKind::systemc_event:
                    out_info->kind = FSIM_OBJECT_EVENT;
                    break;
                case fsim::semantic::design::ObjectKind::systemc_channel:
                    out_info->kind = FSIM_OBJECT_CHANNEL;
                    break;
                case fsim::semantic::design::ObjectKind::systemc_signal:
                    out_info->kind = FSIM_OBJECT_SIGNAL;
                    break;
                case fsim::semantic::design::ObjectKind::systemc_export:
                    out_info->kind = FSIM_OBJECT_EXPORT;
                    break;
                default:
                    return FSIM_STATUS_INTERNAL_ERROR;
                }
            }
            if (design_object != nullptr && design_object->width != 0) {
                const auto signal_id = static_cast<fsim::runtime::simir::SignalId>(
                    design_object->runtime_index);
                const auto& signal = value.simulation->runtime_adapter().signals().at(signal_id);
                if (value.simulation->signal_is_forced(signal_id)) {
                    out_info->flags |= FSIM_OBJECT_FLAG_FORCED;
                }
                if (signal.resolution
                    != fsim::runtime::simir::ResolutionKind::none) {
                    out_info->flags |= FSIM_OBJECT_FLAG_RESOLVED;
                }
            }
            const auto source = design_source(
                value, design_object != nullptr ? design_object->source : process->source);
            set_source(source.path, source.line, source.column);
            set_provenance(path);
            return FSIM_STATUS_OK;
        }
        if (const auto signal = object_signal(value, object)) {
            const auto* design_object = design_signal(value, *signal);
            if (design_object == nullptr) {
                return FSIM_STATUS_INTERNAL_ERROR;
            }
            const auto& runtime_info = value.simulation->runtime_adapter().signals().at(*signal);
            const auto owner = owning_design_scope(value, design_object->path);
            out_info->parent = owner ? scope_handle(value, *owner) : root_handle(value);
            const auto is_port = std::ranges::any_of(
                value.simulation->design_ir().ports(), [&](const auto& port) {
                    return port.object == design_object->id;
                });
            out_info->kind = is_port ? FSIM_OBJECT_PORT : FSIM_OBJECT_SIGNAL;
            out_info->width = design_object->width;
            out_info->name = view(leaf_name(design_object->path));
            out_info->full_name = view(design_object->path);
            out_info->type_name = design_object->type.spelling.empty()
                ? view("logic4")
                : view(design_object->type.spelling);
            out_info->flags = value.simulation->signal_is_forced(*signal)
                ? FSIM_OBJECT_FLAG_FORCED
                : 0U;
            if (runtime_info.resolution
                != fsim::runtime::simir::ResolutionKind::none) {
                out_info->flags |= FSIM_OBJECT_FLAG_RESOLVED;
            }
            const auto source = design_source(value, design_object->source);
            set_source(source.path, source.line, source.column);
            set_provenance(design_object->path);
            return FSIM_STATUS_OK;
        }
        if (const auto process = object_process(value, object)) {
            const auto* occurrence = design_process(value, *process);
            if (occurrence == nullptr) {
                return FSIM_STATUS_INTERNAL_ERROR;
            }
            const auto& public_name = value.process_names.at(*process);
            const auto owner = owning_design_scope(value, occurrence->name);
            out_info->parent = owner ? scope_handle(value, *owner) : root_handle(value);
            out_info->kind = FSIM_OBJECT_PROCESS;
            out_info->width = 0;
            out_info->name = view(leaf_name(public_name));
            out_info->full_name = view(public_name);
            out_info->type_name = view("process");
            const auto source = design_source(value, occurrence->source);
            set_source(source.path, source.line, source.column);
            set_provenance(public_name);
            return FSIM_STATUS_OK;
        }
        if (const auto scope = object_scope(value, object)) {
            const auto& info = value.scopes[*scope];
            out_info->parent = info.parent_scope
                ? scope_handle(value, *info.parent_scope)
                : info.process
                ? process_handle(value, *info.process)
                : root_handle(value);
            out_info->kind = FSIM_OBJECT_SCOPE;
            out_info->width = 0;
            out_info->name = view(leaf_name(info.full_name));
            out_info->full_name = view(info.full_name);
            out_info->type_name = view(info.type_name);
            if (scope_entered(value, info)) {
                out_info->flags |= FSIM_OBJECT_FLAG_ENTERED;
            }
            set_source(
                info.source.path, info.source.line, info.source.column);
            set_provenance(info.full_name);
            return FSIM_STATUS_OK;
        }
        if (const auto variable = object_variable(value, object)) {
            const auto& reference = value.variables[*variable];
            const auto& process = value.simulation->runtime_adapter().processes().at(
                reference.process);
            const auto& info = process.debug_locals.at(reference.local);
            out_info->parent = reference.parent_scope
                ? scope_handle(value, *reference.parent_scope)
                : process_handle(value, reference.process);
            out_info->kind = FSIM_OBJECT_VARIABLE;
            out_info->width = info.width;
            out_info->name = view(leaf_name(info.name));
            out_info->full_name = view(reference.full_name);
            out_info->type_name = view(info.type_name);
            if (variable_initialized(value, reference)) {
                out_info->flags |= FSIM_OBJECT_FLAG_INITIALIZED;
            }
            set_source(
                info.source.path, info.source.line, info.source.column);
            set_provenance(reference.full_name);
            return FSIM_STATUS_OK;
        }
        if (const auto driver = object_driver(value, object)) {
            const auto& reference = value.drivers[*driver];
            const auto* signal = design_signal(value, reference.signal);
            const auto* process = design_process(value, reference.process);
            if (signal == nullptr || process == nullptr) {
                return FSIM_STATUS_INTERNAL_ERROR;
            }
            out_info->parent = signal_handle(value, reference.signal);
            out_info->kind = FSIM_OBJECT_DRIVER;
            out_info->width = signal->width;
            out_info->name = view(leaf_name(reference.full_name));
            out_info->full_name = view(reference.full_name);
            out_info->type_name = view("driver");
            const auto source = design_source(value, process->source);
            set_source(source.path, source.line, source.column);
            set_provenance(reference.full_name);
            return FSIM_STATUS_OK;
        }
        return FSIM_STATUS_INVALID_HANDLE;
    });
}

fsim_status_t fsim_session_read_value(
    const fsim_session_t session,
    const fsim_object_t object,
    char* buffer,
    const size_t buffer_size,
    size_t* out_required)
{
    if (out_required == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    *out_required = 0;
    return with_session(session, [&](Session& value) {
        if (!ready(value, "value reads")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        std::optional<fsim::runtime::PackedLogic4> packed;
        if (const auto signal = object_signal(value, object)) {
            packed = value.simulation->read_signal(*signal);
        } else if (const auto driver = object_driver(value, object)) {
            const auto& reference = value.drivers[*driver];
            packed = value.simulation->read_driver(
                static_cast<fsim::runtime::simir::ProcessId>(
                    reference.process),
                reference.signal);
        } else if (const auto variable = object_variable(value, object)) {
            const auto& reference = value.variables[*variable];
            try {
                packed = value.simulation->read_process_local(
                    static_cast<fsim::runtime::simir::ProcessId>(
                        reference.process),
                    reference.local);
            } catch (const std::logic_error&) {
                return FSIM_STATUS_UNAVAILABLE;
            }
        } else {
            return FSIM_STATUS_INVALID_HANDLE;
        }
        const auto encoded = packed->to_msb_string();
        *out_required = encoded.size() + 1;
        if (buffer == nullptr || buffer_size < *out_required) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        std::memcpy(buffer, encoded.data(), encoded.size());
        buffer[encoded.size()] = '\0';
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_deposit(
    const fsim_session_t session,
    const fsim_object_t object,
    const fsim_string_view_t input)
{
    if (input.data == nullptr && input.size != 0) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        if (!ready(value, "deposit")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        const auto signal = object_signal(value, object);
        if (!signal) {
            return FSIM_STATUS_INVALID_HANDLE;
        }
        const auto* info = design_signal(value, *signal);
        if (info == nullptr) {
            return FSIM_STATUS_INTERNAL_ERROR;
        }
        const auto text = input.data == nullptr ? std::string_view { }
                                                : std::string_view { input.data, input.size };
        std::string error;
        auto parsed = fsim::app::parse_value(text, info->width, error);
        if (!parsed) {
            value.diagnostics.error("FSIM-API-VALUE-0001", error);
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        value.simulation->deposit_signal(*signal, std::move(*parsed));
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_force(
    const fsim_session_t session,
    const fsim_object_t object,
    const fsim_string_view_t input)
{
    if (input.data == nullptr && input.size != 0) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        if (!ready(value, "force")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        const auto signal = object_signal(value, object);
        if (!signal) {
            return FSIM_STATUS_INVALID_HANDLE;
        }
        const auto* info = design_signal(value, *signal);
        if (info == nullptr) {
            return FSIM_STATUS_INTERNAL_ERROR;
        }
        const auto text = input.data == nullptr ? std::string_view { }
                                                : std::string_view { input.data, input.size };
        std::string error;
        auto parsed = fsim::app::parse_value(text, info->width, error);
        if (!parsed) {
            value.diagnostics.error("FSIM-API-VALUE-0001", error);
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        value.simulation->force_signal(*signal, std::move(*parsed));
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_release(
    const fsim_session_t session,
    const fsim_object_t object)
{
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        if (!ready(value, "release")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        const auto signal = object_signal(value, object);
        if (!signal) {
            return FSIM_STATUS_INVALID_HANDLE;
        }
        value.simulation->release_signal(*signal);
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_run(
    const fsim_session_t session,
    const fsim_time_t until_time)
{
    return with_session(session, [&](Session& value) {
        return run_session(value, until_time);
    });
}

fsim_status_t fsim_session_step(
    const fsim_session_t session,
    const fsim_step_kind_t kind)
{
    if (kind < FSIM_STEP_STATEMENT || kind > FSIM_STEP_TIME) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        if (!ready(value, "simulation stepping")) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        if (kind == FSIM_STEP_STATEMENT || kind == FSIM_STEP_PROCESS) {
            auto* state = &value;
            value.simulation->set_execution_point_hook(
                [state, kind](
                    fsim::runtime::Scheduler& scheduler,
                    const fsim::runtime::simir::ExecutionPoint& point) {
                    invoke_safe_point(
                        *state, scheduler,
                        process_handle(*state, point.process),
                        &point);
                    bool stop = false;
                    if (kind == FSIM_STEP_STATEMENT) {
                        const auto statement = point.kind
                                == fsim::runtime::simir::ExecutionPointKind::statement
                            || point.kind
                                == fsim::runtime::simir::ExecutionPointKind::call
                            || point.kind
                                == fsim::runtime::simir::ExecutionPointKind::wait
                            || point.kind
                                == fsim::runtime::simir::ExecutionPointKind::assertion;
                        stop = statement
                            && (!state->current_execution_process
                                || point.process
                                    == *state->current_execution_process);
                    } else if (!state->current_execution_process) {
                        if (point.kind
                            == fsim::runtime::simir::ExecutionPointKind::
                                process_entry) {
                            state->current_execution_process = point.process;
                        }
                    } else {
                        stop = point.process == *state->current_execution_process
                            && point.kind
                                == fsim::runtime::simir::ExecutionPointKind::
                                    process_suspend;
                    }
                    if (stop) {
                        state->current_execution_process = point.process;
                        state->external_stop_seen.store(
                            true, std::memory_order_relaxed);
                        scheduler.request_stop();
                    }
                });
            const auto status = run_session(value, std::nullopt);
            attach_callbacks(value);
            return status == FSIM_STATUS_STOPPED ? FSIM_STATUS_OK : status;
        }
        const auto start_time = value.simulation->now();
        auto* state = &value;
        value.simulation->set_safe_point_hook(
            [state, start_time, kind](
                fsim::runtime::Scheduler& scheduler,
                const fsim::runtime::SchedulerPhase phase) {
                invoke_safe_point(
                    *state,
                    scheduler,
                    FSIM_INVALID_OBJECT,
                    nullptr,
                    phase);
                if ((kind == FSIM_STEP_DELTA
                        && phase == fsim::runtime::SchedulerPhase::postponed)
                    || (kind == FSIM_STEP_TIME
                        && scheduler.now() > start_time)) {
                    state->external_stop_seen.store(true, std::memory_order_relaxed);
                    scheduler.request_stop();
                }
            });
        const auto status = run_session(value, std::nullopt);
        attach_callbacks(value);
        return status == FSIM_STATUS_STOPPED ? FSIM_STATUS_OK : status;
    });
}

fsim_status_t fsim_session_request_stop(const fsim_session_t session)
{
    try {
        auto value = find_session(session);
        if (!value || value->destroyed.load(std::memory_order_acquire)) {
            return FSIM_STATUS_INVALID_HANDLE;
        }
        value->stop_requested.store(true, std::memory_order_relaxed);
        value->external_stop_seen.store(true, std::memory_order_relaxed);
        if (value->running.load(std::memory_order_acquire)) {
            value->simulation->request_stop();
            return FSIM_STATUS_OK;
        }
        std::lock_guard lock(value->mutex);
        if (value->simulation) {
            value->simulation->request_stop();
        }
        return FSIM_STATUS_OK;
    } catch (...) {
        return FSIM_STATUS_INTERNAL_ERROR;
    }
}

fsim_status_t fsim_session_set_callbacks(
    const fsim_session_t session,
    const fsim_callbacks_t* callbacks)
{
    if (callbacks != nullptr
        && !valid_struct_header(
            callbacks->struct_size,
            callbacks->api_version,
            FSIM_STRUCT_HEADER_SIZE)) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        if (mutation_forbidden(value)) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        value.callbacks = { };
        if (callbacks != nullptr) {
            value.callbacks.struct_size = sizeof(value.callbacks);
            value.callbacks.api_version = FSIM_API_VERSION;
            if (FSIM_STRUCT_CONTAINS(
                    callbacks->struct_size, fsim_callbacks_t, user_data)) {
                value.callbacks.user_data = callbacks->user_data;
            }
            if (FSIM_STRUCT_CONTAINS(
                    callbacks->struct_size, fsim_callbacks_t, safe_point)) {
                value.callbacks.safe_point = callbacks->safe_point;
            }
            if (FSIM_STRUCT_CONTAINS(
                    callbacks->struct_size, fsim_callbacks_t, value_change)) {
                value.callbacks.value_change = callbacks->value_change;
            }
            if (FSIM_STRUCT_CONTAINS(
                    callbacks->struct_size, fsim_callbacks_t, assertion)) {
                value.callbacks.assertion = callbacks->assertion;
            }
            if (FSIM_STRUCT_CONTAINS(
                    callbacks->struct_size, fsim_callbacks_t, lifecycle)) {
                value.callbacks.lifecycle = callbacks->lifecycle;
            }
            if (FSIM_STRUCT_CONTAINS(
                    callbacks->struct_size,
                    fsim_callbacks_t,
                    safe_point_info)) {
                value.callbacks.safe_point_info = callbacks->safe_point_info;
            }
        }
        if (value.simulation) {
            attach_callbacks(value);
        }
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_get_diagnostic(
    const fsim_session_t session,
    const size_t index,
    fsim_diagnostic_t* out_diagnostic)
{
    if (out_diagnostic == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    if (!valid_struct_header(
            out_diagnostic->struct_size,
            out_diagnostic->api_version,
            sizeof(fsim_diagnostic_t))) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        const auto& diagnostics = value.diagnostics.diagnostics();
        if (index >= diagnostics.size()) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        const auto& diagnostic = diagnostics[index];
        out_diagnostic->severity = convert_severity(diagnostic.severity);
        out_diagnostic->code = view(diagnostic.code);
        out_diagnostic->message = view(diagnostic.message);
        out_diagnostic->path = view(diagnostic.span.path);
        out_diagnostic->line = diagnostic.span.begin.line;
        out_diagnostic->column = diagnostic.span.begin.column;
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_diagnostic_count(
    const fsim_session_t session,
    size_t* out_count)
{
    if (out_count == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        *out_count = value.diagnostics.diagnostics().size();
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_mapped_library_count(
    const fsim_session_t session,
    size_t* out_count)
{
    if (out_count == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return with_session(session, [&](Session& value) {
        if (!ready(value, "mapped library inspection")) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        *out_count = value.simulation->mapped_libraries().size();
        return FSIM_STATUS_OK;
    });
}

fsim_status_t fsim_session_get_mapped_library_info(
    const fsim_session_t session,
    const size_t index,
    fsim_mapped_library_info_t* out_info)
{
    if (out_info == nullptr) {
        return FSIM_STATUS_INVALID_ARGUMENT;
    }
    if (!valid_struct_header(
            out_info->struct_size,
            out_info->api_version,
            sizeof(fsim_mapped_library_info_t))) {
        return FSIM_STATUS_INCOMPATIBLE_ABI;
    }
    return with_session(session, [&](Session& value) {
        if (!ready(value, "mapped library inspection")) {
            return FSIM_STATUS_UNAVAILABLE;
        }
        const auto& mapped = value.simulation->mapped_libraries();
        if (index >= mapped.size()) {
            return FSIM_STATUS_INVALID_ARGUMENT;
        }
        const auto& item = mapped[index];
        out_info->library = view(item.library);
        out_info->metadata_digest = view(item.metadata_digest);
        out_info->unit_count = item.unit_checksums.size();
        out_info->native_accepted = item.native_accepted ? 1U : 0U;
        out_info->reserved = 0;
        out_info->native_kind = view(item.native_kind);
        out_info->native_fingerprint = view(item.native_fingerprint);
        return FSIM_STATUS_OK;
    });
}

} // extern "C"
