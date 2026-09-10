// SPDX-License-Identifier: Apache-2.0
#include "application_internal.hpp"
#include "application_trace_control.hpp"
#include "application_trace_hierarchy.hpp"
#include "application_trace_observation.hpp"
#include "fsim/runtime/fst_value_encoder.hpp"
#include "fsim/runtime/fst_writer.hpp"
#include "fsim/support/path.hpp"

#include <chrono>

namespace fsim::app::application_detail {

HdlVcdState::~HdlVcdState()
{
    if (remove_observer && observer != 0) {
        remove_observer(observer);
    }
    if (current_time) {
        for (auto& file : extended_files) {
            if (!file->begun || file->closed) {
                continue;
            }
            const auto now = current_time();
            if (now > std::numeric_limits<SimulationTick>::max()
                    / file->tick_multiplier) {
                continue;
            }
            file->stream << "$vcdclose #"
                         << now * file->tick_multiplier << " $end\n";
            file->stream.flush();
            file->closed = true;
        }
    }
}

std::optional<SimulationTick> configured_duration(
    const project::Config& config,
    const std::string_view resolution,
    diagnostic::Engine& diagnostics)
{
    if (!config.run.duration) {
        return std::nullopt;
    }
    std::string error;
    const auto duration = parse_time(
        *config.run.duration, resolution, error);
    if (!duration) {
        diagnostics.error("FSIM-TIME-0002", error);
    }
    return duration;
}

void install_interrupt_hook(Simulation& simulation)
{
    interrupt_requested.store(false, std::memory_order_relaxed);
    simulation.set_safe_point_hook([](runtime::Scheduler& scheduler,
                                       runtime::SchedulerPhase) {
        if (interrupt_requested.exchange(false, std::memory_order_relaxed)) {
            scheduler.request_stop();
        }
    });
}

void report_native_cache_failures(
    const Simulation& simulation,
    diagnostic::Engine& diagnostics,
    const bool synchronize)
{
    const auto cache = simulation.native_cache_statistics(synchronize);
    if (cache.load_failures == 0 && cache.store_failures == 0
        && cache.prune_failures == 0) {
        return;
    }
    diagnostics.warning(
        "FSIM-CACHE-0004",
        "native LLVM object cache reported "
            + std::to_string(cache.load_failures)
            + " load failure(s) and "
            + std::to_string(cache.store_failures)
            + " store failure(s), and "
            + std::to_string(cache.prune_failures)
            + " prune failure(s); simulation remains valid, but cache reuse "
              "or eviction may be incomplete");
}

int handle_check(
    const cli::Invocation&,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto checked = check_project(config, diagnostics);
    if (!checked) {
        return 1;
    }
    output << "checked " << checked->source_count << " source file(s), "
           << checked->parsed.units.size() << " design unit(s)\n";
    return 0;
}

int handle_build(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto built = build_project(config, diagnostics);
    if (!built) {
        return 1;
    }
    const auto roots = built->design_ir.roots();
    const auto signal_count = static_cast<std::size_t>(std::ranges::count_if(
        built->design_ir.objects(), [](const auto& object) {
            return object.kind == semantic::design::ObjectKind::signal
                && !object.parent_object;
        }));
    const auto process_count = built->design_ir.processes().size();
    const auto plugin_count = built->systemc_plugins.size();
    const auto cache_hit = built->cache_hit;
    const auto mapped_libraries = built->mapped_libraries;
    const auto selected_seed = built->seed;
    const auto entropy_seed_selected = built->entropy_seed;
    Simulation prepared(
        std::move(*built),
        config.run.max_deltas,
        SimulationEngine::compiled);
    prepared.await_native_compilation();
    report_native_cache_failures(prepared, diagnostics);
    const auto native_cache = prepared.native_cache_statistics();
    for (const auto& request : invocation.library_exports) {
        if (!export_library(
                config, request.library, request.path, diagnostics)) {
            return 1;
        }
        output << "exported logical library '" << request.library
               << "' to " << support::path_to_utf8(request.path) << '\n';
    }
    output << "built ";
    for (std::size_t index = 0; index < roots.size(); ++index) {
        if (index != 0) {
            output << ", ";
        }
        output << roots[index];
    }
    output << " ("
           << signal_count << " signals, "
           << process_count << " processes";
    if (plugin_count != 0) {
        output << ", " << plugin_count
               << " validated SystemC plug-in artifact(s)";
    }
    if (!mapped_libraries.empty()) {
        output << ", " << mapped_libraries.size()
               << " mapped precompiled librar"
               << (mapped_libraries.size() == 1 ? "y" : "ies") << " [";
        for (std::size_t index = 0; index < mapped_libraries.size(); ++index) {
            if (index != 0) {
                output << ", ";
            }
            output << mapped_libraries[index].library;
        }
        output << ']';
    }
    output << ", " << prepared.compiled_process_count()
           << " LLVM-compiled process(es) in "
           << prepared.compiled_module_count()
           << " specialization module(s)";
    output << "; analysis cache "
           << (cache_hit ? "hit" : "populated");
    if (prepared.compiled_process_count() == 0) {
        output << "; native cache unused";
    } else {
        output << "; native cache "
               << native_cache.hits << " hit(s), "
               << native_cache.misses << " miss(es), "
               << native_cache.stores << " store(s), "
               << native_cache.rejected_entries << " rejected";
    }
    output << ")\n";
    if (entropy_seed_selected) {
        output << "random seed " << selected_seed << '\n';
    }
    return 0;
}

int run_built_project(
    BuiltProject built,
    const SimulationEngine engine,
    const project::Config& config,
    const std::span<const std::string> plusargs,
    diagnostic::Engine& diagnostics,
    std::ostream& output)
{
    const auto duration = configured_duration(
        config, built.time_resolution, diagnostics);
    if (config.run.duration && !duration) {
        return 1;
    }
    if (built.entropy_seed) {
        output << "random seed " << built.seed << '\n';
    }
    project::Config trace_config = config;
    std::shared_ptr<const TraceControlApplication> archived_control;
    if (built.trace_archive) {
        auto consumer_root = config.base_directory;
        if (config.run.trace_file && config.run.trace_enabled
            && built.trace_archive->output_intent.parent_path().empty()) {
            consumer_root = config.run.trace_file->parent_path();
        }
        auto restored = restore_trace_archive_control(
            *built.trace_archive, consumer_root);
        if (!restored.ok()) {
            for (const auto& diagnostic : restored.diagnostics)
                application_detail::import_diagnostic(diagnostics, diagnostic);
            return 1;
        }
        if (config.run.trace_file && config.run.trace_enabled) {
            const auto surface
                = config.manifest_path == std::filesystem::path { "<command-line>" }
                ? TraceControlSurface::NonProjectSimulate
                : TraceControlSurface::ProjectCli;
            auto current = apply_trace_control(trace_control_request(
                config.run, surface, TraceControlPhase::Simulate));
            if (!current.ok()) {
                for (const auto& diagnostic : current.diagnostics)
                    application_detail::import_diagnostic(diagnostics, diagnostic);
                return 1;
            }
            const auto current_snapshot = make_trace_archive_snapshot(
                *current.application, config.base_directory);
            if (!trace_archive_profiles_compatible(
                    current_snapshot, *built.trace_archive)) {
                diagnostics.error("FSIM-TRACE-ARCHIVE-003",
                    "simulation trace request conflicts with the archived design profile");
                return 1;
            }
        }
        archived_control = std::move(restored.application);
        publish_trace_control(*archived_control, trace_config.run);
    }
    std::vector<std::string> process_names;
    process_names.reserve(built.design.processes().size());
    for (const auto& process : built.design.processes()) {
        process_names.push_back(process.name);
    }
    const bool profile_phases = std::getenv("FSIM_PROFILE_PHASES") != nullptr;
    const auto simulation_setup_begin = std::chrono::steady_clock::now();
    Simulation simulation(
        std::move(built),
        config.run.max_deltas,
        engine,
        SystemVerilogVpiRuntimeUpdates::omitted);
    const auto simulation_setup_elapsed = std::chrono::steady_clock::now()
        - simulation_setup_begin;
    if (!apply_uvm_command_line(simulation, plusargs, diagnostics)) {
        return 1;
    }
    simulation.set_output_hook(
        [&output](
            const runtime::simir::ProcessId,
            const std::string_view text,
            const bool newline,
            const SimulationTick,
            const std::uint64_t) {
            output << text;
            if (newline) {
                output << '\n';
            }
        });
    simulation.set_report_hook(
        [&output](
            const runtime::simir::ProcessId,
            const std::string_view message,
            const runtime::simir::AssertionSeverity severity,
            const runtime::simir::SourceLocation& source,
            const SimulationTick,
            const std::uint64_t) {
            output << source.path << ':' << source.line << ':'
                   << source.column << ": "
                   << report_severity_name(severity)
                   << "[FSIM-HDL-REPORT]: " << message << '\n';
        });
    report_native_cache_failures(simulation, diagnostics, false);
    auto trace = attach_trace(simulation, trace_config, diagnostics, false,
        std::move(archived_control));
    if (trace_config.run.trace_file && trace_config.run.trace_enabled && !trace) {
        return 1;
    }
    install_interrupt_hook(simulation);
    const InterruptSignalGuard interrupt_signal;
    try {
        // Native materialization overlaps the initial interpreted execution.
        // Each deferred executor is installed only at a process-resume safe
        // point, preserving its interpreter frame while removing LLVM
        // compilation from the cold simulation critical path. The explicit
        // build command still waits for every requested cache artifact.
        const auto native_await_elapsed
            = std::chrono::steady_clock::duration::zero();
        const auto simulation_run_begin = std::chrono::steady_clock::now();
        const auto result = simulation.run(duration);
        const auto simulation_execution_elapsed
            = std::chrono::steady_clock::now() - simulation_run_begin;
        if (trace && !finish_trace(*trace, diagnostics)) {
            return 1;
        }
        output << "simulation "
               << (result.status == runtime::RunStatus::completed
                          ? "completed"
                          : result.status == runtime::RunStatus::time_limit
                          ? "reached time limit"
                          : "stopped")
               << " at tick " << result.time << ", delta " << result.delta << '\n';
        if (profile_phases) {
            const auto milliseconds = [](const auto elapsed) {
                return std::chrono::duration<double, std::milli>(elapsed)
                    .count();
            };
            output << "FSIM-PROFILE setup_ms="
                   << milliseconds(simulation_setup_elapsed)
                   << " run_ms=" << milliseconds(simulation_execution_elapsed)
                   << " native_await_ms=" << milliseconds(native_await_elapsed)
                   << '\n';
        }
        if (result.simulator_status) {
            if (*result.simulator_status < 0
                || *result.simulator_status > 255) {
                diagnostics.error(
                    "FSIM-RUN-VHENV-001",
                    "STD.ENV simulator status must be in the portable range 0 through 255");
                return 1;
            }
            return static_cast<int>(*result.simulator_status);
        }
        return 0;
    } catch (const runtime::DeltaCycleLimitError& error) {
        std::ostringstream message;
        message << error.what() << "; active process IDs: ";
        if (error.pending_orders().empty()) {
            message << "none";
        } else {
            for (std::size_t index = 0;
                index < error.pending_orders().size(); ++index) {
                if (index != 0) {
                    message << ',';
                }
                message << error.pending_orders()[index];
            }
        }
        message << "; recently changed signal IDs: ";
        if (error.recent_signals().empty()) {
            message << "none";
        } else {
            for (std::size_t index = 0;
                index < error.recent_signals().size(); ++index) {
                if (index != 0) {
                    message << ',';
                }
                message << error.recent_signals()[index];
            }
        }
        diagnostics.error(
            "FSIM-RUN-DELTA-0001", message.str());
    } catch (const runtime::simir::AssertionError& error) {
        diagnostic::SourceSpan span;
        span.path = error.source().path;
        span.begin.line = error.source().line;
        span.begin.column = error.source().column;
        span.end = span.begin;
        const auto severity = [&] {
            switch (error.severity()) {
            case runtime::simir::AssertionSeverity::note:
                return diagnostic::Severity::note;
            case runtime::simir::AssertionSeverity::warning:
                return diagnostic::Severity::warning;
            case runtime::simir::AssertionSeverity::error:
                return diagnostic::Severity::error;
            case runtime::simir::AssertionSeverity::failure:
                return diagnostic::Severity::fatal;
            }
            return diagnostic::Severity::error;
        }();
        diagnostics.report(diagnostic::Diagnostic {
            severity, "FSIM-RUN-ASSERT-0001", error.what(), std::move(span), { } });
    } catch (const runtime::simir::InterpreterError& error) {
        auto message = std::string { error.what() };
        if (error.process() < process_names.size()) {
            message += "; process '"
                + process_names[error.process()] + "'";
        }
        diagnostics.error("FSIM-RUN-0001", std::move(message));
    } catch (const std::exception& error) {
        diagnostics.error("FSIM-RUN-0002", error.what());
    }
    return 1;
}

int handle_run(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream&)
{
    auto built = build_project(config, diagnostics);
    if (!built) {
        return 1;
    }
    return run_built_project(
        std::move(*built), SimulationEngine::compiled,
        config, invocation.plusargs, diagnostics, output);
}

void print_debug_help(std::ostream& output)
{
    output
        << "Commands: continue|run [DURATION], run-until TIME, "
           "step statement|process|phase|delta|time,\n"
        << "          break source [PATH:]LINE, break time TIME, "
           "break signal SIGNAL [==|!= VALUE], break phase IDENTITY|*, "
           "break uvm IDENTITY|*,\n"
        << "          breakpoints,\n"
        << "          delete ID, clear, scope [PATH], scopes [PATH], "
           "signals [PATH],\n"
        << "          show SIGNAL,\n"
        << "          classes, class HANDLE [PROPERTY], chandles, "
           "chandle HANDLE,\n"
        << "          uvm [summary|phases|objections|tlm1|tlm2|all],\n"
        << "          vhdl [summary|scopes|objects|processes|psl|all],\n"
        << "          deposit SIGNAL VALUE, force SIGNAL VALUE, release SIGNAL,\n"
        << "          trace add|remove SIGNAL, trace all|clear|list|status|report|flush|close,\n"
        << "          locals, where, help, quit\n";
}


} // namespace fsim::app::application_detail
