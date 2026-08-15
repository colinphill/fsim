// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"
#include "fsim/app/trace_api.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace {

using fsim::app::TraceControlEntryKind;
using fsim::app::TraceControlPhase;
using fsim::app::TraceControlRequest;
using fsim::app::TraceControlSurface;
using fsim::app::TraceLifecycle;

fsim_string_view_t view(const std::string_view value)
{
    return { value.data(), value.size() };
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : path_(std::filesystem::temp_directory_path()
              / ("fsim-trace-api-"
                  + std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count())))
    {
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void write_text(const std::filesystem::path& path,
    const std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    assert(output);
}

TraceControlRequest request(const TraceControlSurface surface)
{
    TraceControlRequest value;
    value.surface = surface;
    value.phase = TraceControlPhase::Simulate;
    value.output = "waves.fst";
    value.format = fsim::project::TraceFormat::automatic;
    value.compression = fsim::project::TraceCompression::automatic;
    value.selection = { "top.clock", "top.data*" };
    value.lifecycle = TraceLifecycle::Configured;
    value.generation = 7U;
    value.report_limit = 32U;
    return value;
}

void test_surface_equivalence_and_status()
{
    const auto cpp = fsim::app::apply_trace_control(
        request(TraceControlSurface::CppApi));
    const auto cli = fsim::app::apply_trace_control(
        request(TraceControlSurface::ProjectCli));
    const auto tcl = fsim::app::apply_trace_control(
        request(TraceControlSurface::Tcl));
    const auto debugger = fsim::app::apply_trace_control(
        request(TraceControlSurface::Debugger));
    assert(cpp.ok() && cli.ok() && tcl.ok() && debugger.ok());
    assert(cpp.application->semantic_identity()
        == cli.application->semantic_identity());
    assert(cpp.application->semantic_identity()
        == tcl.application->semantic_identity());
    assert(cpp.application->semantic_identity()
        == debugger.application->semantic_identity());

    const auto& status = cpp.application->status();
    assert(status.requested_format == fsim::project::TraceFormat::automatic);
    assert(status.effective_format == fsim::project::TraceFormat::fst);
    assert(status.requested_compression
        == fsim::project::TraceCompression::automatic);
    assert(status.effective_compression
        == fsim::project::TraceCompression::deterministic);
    assert(status.lifecycle == TraceLifecycle::Configured);
    assert(status.selection_count == 2U);
    assert(status.report_entry_count == 6U);
    assert(status.returned_report_entry_count == 6U);
    assert(status.generation == 7U);
    assert(!status.report_truncated);

    const auto report = cpp.application->report();
    assert(report.size() == 6U);
    assert(report[0].kind == TraceControlEntryKind::Output);
    assert(report[0].value == "waves.fst");
    assert(report[1].kind == TraceControlEntryKind::Format);
    assert(report[1].value == "fst");
    assert(report[2].kind == TraceControlEntryKind::Compression);
    assert(report[2].value == "deterministic");
    assert(report[3].kind == TraceControlEntryKind::Selection);
    assert(report[3].value == "top.clock");
    assert(report[5].kind == TraceControlEntryKind::Lifecycle);
    for (const auto& entry : report) {
        assert(entry.canonical_identity.size() == 64U);
    }
}

void test_atomic_negatives_and_truncation()
{
    const auto retained = fsim::app::apply_trace_control(
        request(TraceControlSurface::CppApi));
    assert(retained.ok());
    const auto identity = std::string { retained.application->semantic_identity() };

    auto invalid = request(TraceControlSurface::CppApi);
    invalid.format = fsim::project::TraceFormat::vcd;
    const auto conflict = fsim::app::apply_trace_control(std::move(invalid));
    assert(!conflict.ok());
    assert(conflict.application == nullptr);
    assert(conflict.diagnostics.size() == 1U);
    assert(conflict.diagnostics[0].code == "FSIM-TRACE-CONTROL-002");
    assert(retained.application->semantic_identity() == identity);

    invalid = request(TraceControlSurface::CppApi);
    invalid.selection.push_back("top.clock");
    const auto duplicate = fsim::app::apply_trace_control(std::move(invalid));
    assert(!duplicate.ok());
    assert(duplicate.diagnostics[0].code == "FSIM-TRACE-CONTROL-003");

    invalid = request(TraceControlSurface::CppApi);
    invalid.output = "waves.vcd";
    invalid.format = fsim::project::TraceFormat::vcd;
    invalid.compression = fsim::project::TraceCompression::deterministic;
    const auto unsupported = fsim::app::apply_trace_control(std::move(invalid));
    assert(!unsupported.ok());
    assert(unsupported.diagnostics[0].code == "FSIM-TRACE-CONTROL-001");

    invalid = request(TraceControlSurface::CppApi);
    invalid.report_limit = 2U;
    const auto truncated = fsim::app::apply_trace_control(std::move(invalid));
    assert(truncated.ok());
    assert(truncated.application->status().report_entry_count == 6U);
    assert(truncated.application->status().returned_report_entry_count == 2U);
    assert(truncated.application->status().report_truncated);
    assert(truncated.application->report().size() == 2U);

    invalid = request(TraceControlSurface::CppApi);
    fsim::app::TraceControlLimits limits;
    limits.max_selection_count = 1U;
    const auto exhausted
        = fsim::app::apply_trace_control(std::move(invalid), limits);
    assert(!exhausted.ok());
    assert(exhausted.diagnostics[0].code == "FSIM-TRACE-CONTROL-004");
}

void test_run_section_round_trip_and_disabled_lifecycle()
{
    fsim::project::RunSection run;
    run.trace_file = std::filesystem::path { "round-trip.fst" };
    run.trace_format = fsim::project::TraceFormat::fst;
    run.trace_compression = fsim::project::TraceCompression::none;
    run.trace_filters = { "top.value" };
    run.trace_report_limit = 9U;
    run.trace_enabled = true;
    auto converted = fsim::app::trace_control_request(run,
        TraceControlSurface::CppApi, TraceControlPhase::Elaborate);
    const auto applied = fsim::app::apply_trace_control(std::move(converted));
    assert(applied.ok());
    fsim::project::RunSection published;
    fsim::app::publish_trace_control(*applied.application, published);
    assert(published.trace_file == run.trace_file);
    assert(published.trace_format == run.trace_format);
    assert(published.trace_compression == run.trace_compression);
    assert(published.trace_filters == run.trace_filters);
    assert(published.trace_report_limit == run.trace_report_limit);
    assert(published.trace_enabled);

    TraceControlRequest disabled;
    disabled.surface = TraceControlSurface::NonProjectCompile;
    disabled.phase = TraceControlPhase::Compile;
    disabled.lifecycle = TraceLifecycle::Disabled;
    const auto no_trace = fsim::app::apply_trace_control(std::move(disabled));
    assert(no_trace.ok());
    assert(no_trace.application->status().lifecycle
        == TraceLifecycle::Disabled);
    assert(no_trace.application->request().output.empty());
}

void test_c_api_status_report_and_rollback()
{
    fsim_session_t session = FSIM_INVALID_SESSION;
    assert(fsim_session_create(nullptr, &session) == FSIM_STATUS_OK);
    const fsim_string_view_t selections[] {
        view("top.clock"), view("top.data*")
    };
    fsim_trace_options_t options { };
    options.struct_size = sizeof(options);
    options.api_version = FSIM_API_VERSION;
    options.format = FSIM_TRACE_FORMAT_AUTO;
    options.compression = FSIM_TRACE_COMPRESSION_NONE;
    options.lifecycle = FSIM_TRACE_LIFECYCLE_CONFIGURED;
    options.phase = FSIM_TRACE_PHASE_SIMULATE;
    options.output = view("c-api.fst");
    options.selections = selections;
    options.selection_count = 2U;
    options.report_limit = 16U;
    options.generation = 9U;
    assert(fsim_session_configure_trace(session, &options) == FSIM_STATUS_OK);

    fsim_trace_status_t status { };
    status.struct_size = sizeof(status);
    status.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_trace_status(session, &status) == FSIM_STATUS_OK);
    assert(status.requested_format == FSIM_TRACE_FORMAT_AUTO);
    assert(status.effective_format == FSIM_TRACE_FORMAT_FST);
    assert(status.requested_compression == FSIM_TRACE_COMPRESSION_NONE);
    assert(status.effective_compression == FSIM_TRACE_COMPRESSION_NONE);
    assert(status.lifecycle == FSIM_TRACE_LIFECYCLE_CONFIGURED);
    assert(status.selection_count == 2U);
    assert(status.report_entry_count == 6U);
    assert(status.returned_report_entry_count == 6U);
    assert(status.generation == 9U);
    assert(status.report_truncated == 0U);
    assert((std::string_view { status.output.data, status.output.size }
        == "c-api.fst"));
    options.compression = FSIM_TRACE_COMPRESSION_DETERMINISTIC;
    assert(fsim_session_configure_trace(session, &options) == FSIM_STATUS_OK);
    assert(fsim_session_get_trace_status(session, &status) == FSIM_STATUS_OK);
    assert(status.requested_compression
        == FSIM_TRACE_COMPRESSION_DETERMINISTIC);
    assert(status.effective_compression
        == FSIM_TRACE_COMPRESSION_DETERMINISTIC);
    const std::string identity { status.semantic_identity.data,
        status.semantic_identity.size };

    fsim_trace_report_entry_t entry { };
    entry.struct_size = sizeof(entry);
    entry.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_trace_report_entry(session, 3U, &entry)
        == FSIM_STATUS_OK);
    assert(entry.kind == FSIM_TRACE_REPORT_SELECTION);
    assert((std::string_view { entry.value.data, entry.value.size }
        == "top.clock"));
    assert(entry.canonical_identity.size == 64U);

    const fsim_string_view_t duplicate_selections[] {
        view("top.clock"), view("top.clock")
    };
    options.selections = duplicate_selections;
    assert(fsim_session_configure_trace(session, &options)
        == FSIM_STATUS_INVALID_ARGUMENT);
    fsim_trace_status_t retained { };
    retained.struct_size = sizeof(retained);
    retained.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_trace_status(session, &retained)
        == FSIM_STATUS_OK);
    assert((std::string_view { retained.semantic_identity.data,
                retained.semantic_identity.size }
        == identity));
    size_t diagnostic_count = 0U;
    assert(fsim_session_diagnostic_count(session, &diagnostic_count)
        == FSIM_STATUS_OK);
    assert(diagnostic_count == 1U);
    fsim_diagnostic_t diagnostic { };
    diagnostic.struct_size = sizeof(diagnostic);
    diagnostic.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_diagnostic(session, 0U, &diagnostic)
        == FSIM_STATUS_OK);
    assert((std::string_view { diagnostic.code.data, diagnostic.code.size }
        == "FSIM-TRACE-CONTROL-003"));
    assert(fsim_session_destroy(session) == FSIM_STATUS_OK);
}

struct TraceCallbackState {
    const fsim_trace_options_t* options { };
    bool loaded_status { };
    bool loaded_report { };
    bool finished_status { };
    fsim_status_t callback_configuration { FSIM_STATUS_OK };
    fsim_status_t callback_flush { FSIM_STATUS_OK };
};

void trace_lifecycle_callback(const fsim_session_t session,
    const fsim_lifecycle_event_t event,
    void* const user_data)
{
    auto& state = *static_cast<TraceCallbackState*>(user_data);
    fsim_trace_status_t status { };
    status.struct_size = sizeof(status);
    status.api_version = FSIM_API_VERSION;
    if (event == FSIM_LIFECYCLE_DESIGN_LOADED) {
        state.loaded_status
            = fsim_session_get_trace_status(session, &status)
                == FSIM_STATUS_OK
            && status.lifecycle == FSIM_TRACE_LIFECYCLE_OPEN;
        fsim_trace_report_entry_t entry { };
        entry.struct_size = sizeof(entry);
        entry.api_version = FSIM_API_VERSION;
        state.loaded_report
            = fsim_session_get_trace_report_entry(session, 0U, &entry)
                == FSIM_STATUS_OK
            && entry.kind == FSIM_TRACE_REPORT_OUTPUT;
        state.callback_configuration
            = fsim_session_configure_trace(session, state.options);
        state.callback_flush = fsim_session_flush_trace(session);
    } else if (event == FSIM_LIFECYCLE_SIMULATION_FINISHED) {
        state.finished_status
            = fsim_session_get_trace_status(session, &status)
                == FSIM_STATUS_OK
            && status.lifecycle == FSIM_TRACE_LIFECYCLE_COMPLETE;
    }
}

void test_c_api_runtime_lifecycle_and_callback_queries()
{
    TemporaryDirectory temporary;
    const auto source = temporary.path() / "trace_api.sv";
    const auto manifest = temporary.path() / "fsim.toml";
    const auto output = temporary.path() / "callback.fst";
    write_text(source,
        "module trace_api;\n"
        "  logic q;\n"
        "  initial begin\n"
        "    q = 1'b0;\n"
        "    #1 q = 1'b1;\n"
        "    #1 $finish;\n"
        "  end\n"
        "endmodule\n");
    write_text(manifest,
        "schema = 2\n"
        "[project]\n"
        "name = \"trace-api\"\n"
        "top = \"sv:work.trace_api\"\n"
        "time_resolution = \"1ns\"\n\n"
        "[[source_set]]\n"
        "language = \"systemverilog\"\n"
        "standard = \"2017\"\n"
        "library = \"work\"\n"
        "files = [\"trace_api.sv\"]\n\n"
        "[build]\n"
        "optimization = \"O0\"\n"
        "cache_path = \"cache\"\n\n"
        "[run]\n"
        "max_deltas = 1000\n");

    fsim_session_t session = FSIM_INVALID_SESSION;
    assert(fsim_session_create(nullptr, &session) == FSIM_STATUS_OK);
    const auto manifest_text = manifest.string();
    assert(fsim_session_load_project(session, manifest_text.c_str())
        == FSIM_STATUS_OK);

    const fsim_string_view_t selections[] { view("trace_api.q") };
    fsim_trace_options_t options { };
    options.struct_size = sizeof(options);
    options.api_version = FSIM_API_VERSION;
    options.format = FSIM_TRACE_FORMAT_AUTO;
    options.compression = FSIM_TRACE_COMPRESSION_AUTO;
    options.lifecycle = FSIM_TRACE_LIFECYCLE_CONFIGURED;
    options.phase = FSIM_TRACE_PHASE_SIMULATE;
    options.output = view("callback.fst");
    options.selections = selections;
    options.selection_count = 1U;
    options.report_limit = 16U;
    options.generation = 23U;
    assert(fsim_session_configure_trace(session, &options) == FSIM_STATUS_OK);

    TraceCallbackState callback_state;
    callback_state.options = &options;
    fsim_callbacks_t callbacks { };
    callbacks.struct_size = sizeof(callbacks);
    callbacks.api_version = FSIM_API_VERSION;
    callbacks.user_data = &callback_state;
    callbacks.lifecycle = trace_lifecycle_callback;
    assert(fsim_session_set_callbacks(session, &callbacks) == FSIM_STATUS_OK);
    assert(fsim_session_build(session) == FSIM_STATUS_OK);
    assert(callback_state.loaded_status);
    assert(callback_state.loaded_report);
    assert(callback_state.callback_configuration == FSIM_STATUS_UNAVAILABLE);
    assert(callback_state.callback_flush == FSIM_STATUS_UNAVAILABLE);

    fsim_trace_status_t status { };
    status.struct_size = sizeof(status);
    status.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_trace_status(session, &status) == FSIM_STATUS_OK);
    assert(status.lifecycle == FSIM_TRACE_LIFECYCLE_OPEN);
    assert(status.generation == 0U);
    assert(status.selection_count == 1U);
    assert((std::filesystem::path(std::string {
                status.output.data, status.output.size })
        == output));
    assert(fsim_session_build(session) == FSIM_STATUS_OK);
    std::size_t diagnostic_count = 1U;
    assert(fsim_session_diagnostic_count(session, &diagnostic_count)
        == FSIM_STATUS_OK);
    assert(diagnostic_count == 0U);
    assert(fsim_session_get_trace_status(session, &status) == FSIM_STATUS_OK);
    assert(status.lifecycle == FSIM_TRACE_LIFECYCLE_OPEN);
    assert(fsim_session_flush_trace(session) == FSIM_STATUS_OK);
    assert(fsim_session_close_trace(session) == FSIM_STATUS_UNAVAILABLE);

    const auto run_status = fsim_session_run(session, 10U);
    assert(run_status == FSIM_STATUS_STOPPED
        || run_status == FSIM_STATUS_OK);
    assert(callback_state.finished_status);
    assert(fsim_session_get_trace_status(session, &status) == FSIM_STATUS_OK);
    assert(status.lifecycle == FSIM_TRACE_LIFECYCLE_COMPLETE);
    assert(fsim_session_close_trace(session) == FSIM_STATUS_OK);
    assert(fsim_session_flush_trace(session) == FSIM_STATUS_STOPPED);
    assert(std::filesystem::is_regular_file(output));
    assert(std::filesystem::file_size(output) != 0U);
    assert(fsim_session_destroy(session) == FSIM_STATUS_OK);

    const auto blocked_output = temporary.path() / "blocked.fst";
    std::filesystem::create_directory(blocked_output);
    fsim_session_t failed_session = FSIM_INVALID_SESSION;
    assert(fsim_session_create(nullptr, &failed_session) == FSIM_STATUS_OK);
    assert(fsim_session_load_project(failed_session, manifest_text.c_str())
        == FSIM_STATUS_OK);
    options.output = view("blocked.fst");
    assert(fsim_session_configure_trace(failed_session, &options)
        == FSIM_STATUS_OK);
    assert(fsim_session_build(failed_session) == FSIM_STATUS_RUNTIME_ERROR);
    status.struct_size = sizeof(status);
    status.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_trace_status(failed_session, &status)
        == FSIM_STATUS_OK);
    assert(status.lifecycle == FSIM_TRACE_LIFECYCLE_FAILED);
    fsim_trace_report_entry_t failed_report { };
    failed_report.struct_size = sizeof(failed_report);
    failed_report.api_version = FSIM_API_VERSION;
    assert(fsim_session_get_trace_report_entry(
               failed_session, 0U, &failed_report)
        == FSIM_STATUS_OK);
    assert(fsim_session_destroy(failed_session) == FSIM_STATUS_OK);
}

}

int main()
{
    test_surface_equivalence_and_status();
    test_atomic_negatives_and_truncation();
    test_run_section_round_trip_and_disabled_lifecycle();
    test_c_api_status_report_and_rollback();
    test_c_api_runtime_lifecycle_and_callback_queries();
    return 0;
}
