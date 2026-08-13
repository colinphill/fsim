// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct Capture {
    fsim::runtime::RunResult result;
    std::vector<std::tuple<
        std::string,
        fsim::runtime::SimulationTick,
        std::uint64_t>>
        changes;
    std::string final_value;
    std::string vcd;
    std::string resolution;
    std::size_t compiled_processes { };
    fsim::app::NativeCacheStatistics native_cache;
};

struct ScalarSurfaceCapture {
    std::array<std::uint64_t, 7> payloads { };
    std::string callable_checks;
    std::string math_checks;
    std::size_t change_count { };
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    std::string resolution = "auto")
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "time-rounding";
    config.project.top = "sv:work.time_rounding";
    config.project.time_resolution = std::move(resolution);
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "cache-o0"
                : "cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project);
    Capture capture;
    capture.resolution = project->time_resolution;
    fsim::app::Simulation simulation {
        std::move(*project), 1000, engine
    };
    capture.compiled_processes = simulation.compiled_process_count();
    capture.native_cache = simulation.native_cache_statistics();
    const auto marker = simulation.find_signal("time_rounding.marker");
    assert(marker);

    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
    const auto vcd_marker = vcd.declare_signal("time_rounding.marker", 4);
    vcd.begin(simulation.now());
    vcd.change(vcd_marker, simulation.read_signal(*marker));
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            if (signal != *marker) {
                return;
            }
            capture.changes.emplace_back(
                value.to_msb_string(), time, delta);
            vcd.set_time(time);
            vcd.change(vcd_marker, value);
        });
    capture.result = simulation.run();
    capture.final_value = simulation.read_signal(*marker).to_msb_string();
    vcd.flush();
    capture.vcd = vcd_output.str();
    return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto config = config_for(
        directory, source, optimization);
    const auto reference = run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(config, fsim::app::SimulationEngine::compiled);

    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.time == 2835);
    assert(reference.resolution == "1ps");
    assert(reference.final_value == "0101");
    assert(reference.changes.size() == 6);
    constexpr std::array expected_times {
        fsim::runtime::SimulationTick { 0 },
        fsim::runtime::SimulationTick { 0 },
        fsim::runtime::SimulationTick { 1 },
        fsim::runtime::SimulationTick { 1235 },
        fsim::runtime::SimulationTick { 1835 },
        fsim::runtime::SimulationTick { 2835 },
    };
    for (std::size_t index = 0; index < expected_times.size(); ++index) {
        assert(std::get<1>(reference.changes[index]) == expected_times[index]);
    }
    assert(reference.vcd.find("#1235") != std::string::npos);
    assert(reference.vcd.find("#2835") != std::string::npos);
    assert(reference.result.status == cold.result.status);
    assert(reference.result.time == cold.result.time);
    assert(reference.changes == cold.changes);
    assert(reference.final_value == cold.final_value);
    assert(reference.vcd == cold.vcd);
    assert(reference.result.status == warm.result.status);
    assert(reference.result.time == warm.result.time);
    assert(reference.changes == warm.changes);
    assert(reference.final_value == warm.final_value);
    assert(reference.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1);
    assert(cold.native_cache.hits == 0);
    assert(cold.native_cache.misses == 1);
    assert(cold.native_cache.stores == 1);
    assert(warm.compiled_processes == 1);
    assert(warm.native_cache.hits == 1);
    assert(warm.native_cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
}

void verify_resolution_diagnostic(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    auto config = config_for(
        directory,
        source,
        fsim::project::Optimization::o2,
        "10ps");
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project);
    assert(
        std::ranges::any_of(
            diagnostics.diagnostics(),
            [](const fsim::diagnostic::Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-TIME-0004";
            }));
}

void verify_overflow_diagnostic(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(timeunit 1s / 1s;
module time_rounding;
  trireg (small) #18446744073709551615 retained;
  assign retained = 1'b1;
endmodule
)";
        assert(output.good());
    }
    const auto config = config_for(
        directory,
        source,
        fsim::project::Optimization::o2,
        "1fs");
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project);
    assert(
        std::ranges::any_of(
            diagnostics.diagnostics(),
            [](const fsim::diagnostic::Diagnostic& diagnostic) {
                return diagnostic.code == "FSIM-TIME-0003"
                    && diagnostic.message.find("overflows")
                    != std::string::npos;
            }));
}

ScalarSurfaceCapture verify_scalar_surfaces(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine,
    const std::string_view identity)
{
    auto config = config_for(directory, source, optimization, "1ps");
    config.project.name = "scalar-surfaces";
    config.project.top = "sv:work.scalar_surfaces";
    config.build.cache_path = directory / ("scalar-cache-" + std::string { identity });
    config.run.trace_file = directory / ("scalar-" + std::string { identity } + ".vcd");
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project);
    const auto runtime_bytes = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(runtime_bytes && !diagnostics.has_error());
    auto restored_runtime = fsim::app::deserialize_runtime_state(
        *runtime_bytes, "scalar-runtime.bin", diagnostics);
    assert(restored_runtime && !diagnostics.has_error());
    assert(
        fsim::app::serialize_runtime_state(*restored_runtime, diagnostics)
        == runtime_bytes);
    project->design = std::move(*restored_runtime);
    fsim::app::Simulation simulation { std::move(*project), 1000, engine };
    const auto real_signal = simulation.find_signal("scalar_surfaces.r");
    const auto short_signal = simulation.find_signal("scalar_surfaces.s");
    const auto realtime_signal = simulation.find_signal("scalar_surfaces.rt");
    const auto time_signal = simulation.find_signal("scalar_surfaces.ticks");
    const auto chandle_signal = simulation.find_signal("scalar_surfaces.foreign");
    const auto callable_checks = simulation.find_signal(
        "scalar_surfaces.callable_checks");
    const auto offset_result = simulation.find_signal(
        "scalar_surfaces.offset_result");
    const auto scaled_result = simulation.find_signal(
        "scalar_surfaces.scaled_result");
    const auto math_checks = simulation.find_signal(
        "scalar_surfaces.math_checks");
    assert(
        real_signal && short_signal && realtime_signal && time_signal
        && chandle_signal && callable_checks && offset_result && scaled_result
        && math_checks);
    assert(simulation.read_scalar_signal(*real_signal).as_real() == 0.0);

    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    std::vector<std::pair<fsim::runtime::simir::SignalId,
        fsim::runtime::SystemVerilogScalarValue>>
        changes;
    std::vector<fsim::runtime::SystemVerilogChandleEvent> chandle_events;
    ScalarSurfaceCapture capture;
    const auto chandle_observer = simulation.chandle_registry().add_observer(
        [&](const auto& event) { chandle_events.push_back(event); });
    const auto foreign = simulation.chandle_registry().create(
        { "dpi:test-resource", "fixture-resource", { } });
    {
        fsim::app::DebuggerControl debugger(
            simulation, debugger_output, debugger_error, config, diagnostics);
        debugger.execute({ "trace", "all" });
        simulation.set_scalar_signal_change_hook(
            [&](const auto signal, const auto& value, const auto, const auto) {
                changes.emplace_back(signal, value);
            });
        simulation.deposit_scalar_signal(
            *real_signal,
            fsim::runtime::SystemVerilogScalarValue::real(-0.0));
        assert(
            simulation.read_scalar_signal(*real_signal).bits
            == UINT64_C(0x8000000000000000));
        simulation.deposit_scalar_signal(
            *short_signal,
            fsim::runtime::SystemVerilogScalarValue::shortreal(-1.25F));
        simulation.deposit_scalar_signal(
            *realtime_signal,
            fsim::runtime::SystemVerilogScalarValue::realtime(2.5));
        simulation.deposit_scalar_signal(
            *time_signal,
            fsim::runtime::SystemVerilogScalarValue::time(
                UINT64_C(9007199254740993)));
        simulation.deposit_scalar_signal(
            *chandle_signal,
            fsim::runtime::SystemVerilogScalarValue::chandle(foreign));
        debugger.execute({ "show", "scalar_surfaces.r" });
        debugger.execute({ "deposit", "scalar_surfaces.r", "1.25" });
        debugger.execute({ "show", "scalar_surfaces.r" });
        debugger.execute({ "force", "scalar_surfaces.ticks", "9007199254740995" });
        debugger.execute({ "show", "scalar_surfaces.ticks" });
        debugger.execute({ "release", "scalar_surfaces.ticks" });
        debugger.execute({ "show", "scalar_surfaces.foreign" });
        debugger.execute({ "chandles" });
        debugger.execute({ "chandle", std::to_string(foreign) });
        debugger.execute({ "deposit", "scalar_surfaces.foreign", std::to_string(foreign) });
        const auto snapshots = simulation.scalar_signal_snapshots();
        assert(snapshots.size() == 7);
        assert(
            std::ranges::any_of(snapshots, [&](const auto& snapshot) {
                return snapshot.signal == *time_signal
                    && snapshot.value.bits == UINT64_C(9007199254740993);
            }));
        assert(
            std::ranges::any_of(snapshots, [&](const auto& snapshot) {
                return snapshot.signal == *chandle_signal
                    && snapshot.value.as_chandle() == foreign;
            }));
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::stopped);
        assert(result.time == 1000);
        const auto callable_result = simulation.read_signal(*callable_checks).to_msb_string();
        if (callable_result != "11111111") {
            std::cerr << identity << " callable checks = "
                      << callable_result << ", offset = "
                      << *simulation.read_scalar_signal(*offset_result).as_real()
                      << ", scaled = "
                      << *simulation.read_scalar_signal(*scaled_result).as_real()
                      << '\n';
        }
        assert(callable_result == "11111111");
        capture.callable_checks = callable_result;
        capture.math_checks = simulation.read_signal(*math_checks).to_msb_string();
        assert(capture.math_checks == std::string(27, '1'));
        const std::array scalar_signals {
            *real_signal, *short_signal, *realtime_signal, *time_signal,
            *chandle_signal, *offset_result, *scaled_result
        };
        for (std::size_t index = 0; index < scalar_signals.size(); ++index) {
            capture.payloads[index] = simulation.read_scalar_signal(scalar_signals[index]).bits;
        }
        assert(simulation.chandle_registry().release(foreign));
        debugger.execute({ "show", "scalar_surfaces.foreign" });
        debugger.execute({ "deposit", "scalar_surfaces.foreign", std::to_string(foreign) });
        bool stale_deposit_rejected { };
        try {
            simulation.deposit_scalar_signal(
                *chandle_signal,
                fsim::runtime::SystemVerilogScalarValue::chandle(foreign));
        } catch (const std::out_of_range&) {
            stale_deposit_rejected = true;
        }
        assert(stale_deposit_rejected);
    }
    assert(simulation.chandle_registry().remove_observer(chandle_observer));
    assert(changes.size() >= 8);
    capture.change_count = changes.size();
    assert(
        chandle_events.size() == 4
        && chandle_events.front().kind
            == fsim::runtime::SystemVerilogChandleEventKind::Created
        && chandle_events.back().kind
            == fsim::runtime::SystemVerilogChandleEventKind::Released);
    assert(debugger_error.str().empty());
    assert(debugger_output.str().find("scalar_surfaces.r = -0")
        != std::string::npos);
    assert(debugger_output.str().find("scalar_surfaces.r = 1.25")
        != std::string::npos);
    assert(debugger_output.str().find("9007199254740995 (forced)")
        != std::string::npos);
    assert(debugger_output.str().find("dpi:test-resource")
        != std::string::npos);
    assert(debugger_output.str().find("fixture-resource")
        != std::string::npos);
    assert(debugger_output.str().find("stale chandle <opaque ")
        != std::string::npos);
    assert(debugger_output.str().find("invalid or stale chandle value")
        != std::string::npos);
    std::ifstream trace(*config.run.trace_file, std::ios::binary);
    const std::string vcd {
        std::istreambuf_iterator<char> { trace },
        std::istreambuf_iterator<char> { }
    };
    assert(vcd.find("$var real 1") != std::string::npos);
    assert(vcd.find("$var wire 64") != std::string::npos);
    assert(
        vcd.find("$var wire 64", vcd.find("$var wire 64") + 1)
        != std::string::npos);
    assert(vcd.find("r-0 ") != std::string::npos);
    assert(
        vcd.find(
            "b0000000000100000000000000000000000000000000000000000000000000001")
        != std::string::npos);
#if defined(FSIM_HAS_LLVM)
    if (engine == fsim::app::SimulationEngine::compiled) {
        assert(simulation.compiled_process_count() == 1);
    }
#endif
    return capture;
}

void verify_math_diagnostics(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(module invalid_math;
  logic [31:0] bits;
  real value;
  initial begin
    value = $bitstoreal(bits);
    bits = $rtoi(bits);
    $timeformat(-9, 2, " ns");
    $printtimescale(1);
  end
endmodule
)";
        assert(output.good());
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o2, "1ns");
    config.project.top = "sv:work.invalid_math";
    fsim::diagnostic::Engine diagnostics;
    assert(!fsim::app::build_project(config, diagnostics));
    const auto has_code = [&](const std::string_view code) {
        return std::ranges::any_of(
            diagnostics.diagnostics(), [&](const auto& diagnostic) {
                return diagnostic.code == code;
            });
    };
    assert(has_code("FSIM-ELAB-SVMATH-002"));
    assert(has_code("FSIM-ELAB-SVTIME-001"));
    assert(has_code("FSIM-ELAB-SVTIME-002"));
}

struct TimingRegionCapture {
    fsim::runtime::RunResult result;
    std::array<std::string, 4> values;
    std::vector<std::tuple<
        fsim::runtime::SimulationTick,
        std::uint64_t,
        fsim::runtime::SchedulerPhase>>
        program_points;
};

TimingRegionCapture run_timing_regions(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine)
{
    auto config = config_for(directory, source, optimization, "1ns");
    config.project.name = "timing-regions";
    config.project.top = "sv:work.timing_top";
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(project);
    assert(project->design.verilog_timing_checks().size() == 2);
    const auto observed = std::ranges::find_if(
        project->design.processes(),
        [](const auto& process) { return process.observed; });
    const auto reactive = std::ranges::find_if(
        project->design.processes(),
        [](const auto& process) {
            return process.reactive && !process.final;
        });
    assert(observed != project->design.processes().end());
    assert(reactive != project->design.processes().end());
    const auto reactive_id = reactive->id;

    const auto direct_restored = fsim::elaboration::ElaboratedDesign::from_state(
        project->design.state());
    if (!direct_restored) {
        std::cerr << "live timing-region state is structurally invalid\n";
    }
    assert(direct_restored);

    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, artifact_diagnostics);
    assert(encoded && !artifact_diagnostics.has_error());
    const auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "timing-regions", artifact_diagnostics);
    if (!restored || artifact_diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, artifact_diagnostics);
    }
    assert(restored && !artifact_diagnostics.has_error());
    assert(std::ranges::any_of(
        restored->processes(),
        [](const auto& process) { return process.observed; }));

    fsim::app::Simulation simulation { std::move(*project), 1000, engine };
    TimingRegionCapture capture;
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::simir::ExecutionPoint& point) {
            if (point.process != reactive_id) {
                return;
            }
            capture.program_points.emplace_back(
                scheduler.now(), scheduler.delta(),
                scheduler.current_phase().value_or(
                    fsim::runtime::SchedulerPhase::active));
        });
    capture.result = simulation.run();
    constexpr std::array<std::string_view, 4> names {
        "timing_top.first_sample",
        "timing_top.third_sample",
        "timing_top.zero_done",
        "timing_top.driven"
    };
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto signal = simulation.find_signal(names[index]);
        assert(signal);
        capture.values[index] = simulation.read_signal(*signal).to_msb_string();
    }
    return capture;
}

void verify_timing_regions(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto reference = run_timing_regions(
        directory, source, optimization,
        fsim::app::SimulationEngine::interpreter);
    const auto cold = run_timing_regions(
        directory, source, optimization,
        fsim::app::SimulationEngine::compiled);
    const auto warm = run_timing_regions(
        directory, source, optimization,
        fsim::app::SimulationEngine::compiled);
    const auto verify = [](const TimingRegionCapture& capture) {
        assert(capture.result.status == fsim::runtime::RunStatus::stopped);
        assert(capture.result.time == 31);
        assert((capture.values
            == std::array<std::string, 4> { "00000001", "00000011", "1", "10100101" }));
        assert(std::ranges::any_of(
            capture.program_points,
            [](const auto& point) {
                return std::get<0>(point) == 25
                    && std::get<2>(point)
                    == fsim::runtime::SchedulerPhase::re_inactive;
            }));
    };
    verify(reference);
    verify(cold);
    verify(warm);
    assert(reference.values == cold.values && cold.values == warm.values);
    assert(reference.program_points == cold.program_points);
    assert(cold.program_points == warm.program_points);
#if defined(FSIM_HAS_LLVM)
    (void)optimization;
#endif
}

struct TimeFormatCapture {
    fsim::runtime::RunResult result;
    std::vector<std::tuple<
        std::string,
        bool,
        fsim::runtime::SimulationTick,
        std::uint64_t>>
        output;
    std::size_t compiled_processes { };
};

TimeFormatCapture run_time_format(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(
        config_for(directory, source, optimization), diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(project);
    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, artifact_diagnostics);
    assert(encoded && !artifact_diagnostics.has_error());
    const auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "time-format-runtime.bin", artifact_diagnostics);
    assert(restored && !artifact_diagnostics.has_error());
    const auto has_time_format = [](const auto& design) {
        return std::ranges::any_of(
            design.processes(), [](const auto& process) {
                return std::ranges::any_of(
                    process.operations, [](const auto& operation) {
                        return fsim::runtime::simir::operation_get_if<
                                   fsim::runtime::simir::TimeFormatControl>(
                                   &operation)
                            != nullptr;
                    });
            });
    };
    assert(has_time_format(project->design));
    assert(has_time_format(*restored));
    fsim::app::Simulation simulation {
        std::move(*project), 1000, engine
    };
    TimeFormatCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    simulation.set_output_hook(
        [&capture](
            const fsim::runtime::simir::ProcessId,
            const std::string_view text,
            const bool newline,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            capture.output.emplace_back(text, newline, time, delta);
        });
    capture.result = simulation.run();
    return capture;
}

void verify_time_format(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto reference = run_time_format(
        directory, source, optimization,
        fsim::app::SimulationEngine::interpreter);
    const auto cold = run_time_format(
        directory, source, optimization,
        fsim::app::SimulationEngine::compiled);
    const auto warm = run_time_format(
        directory, source, optimization,
        fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.time == 2501);
    assert(reference.output == cold.output);
    assert(cold.output == warm.output);
    const auto contains = [&](const std::string_view expected) {
        return std::ranges::any_of(
            reference.output,
            [&](const auto& event) {
                return std::get<0>(event) == expected
                    && std::get<1>(event)
                    && std::get<2>(event) == 2500;
            });
    };
    assert(contains("bare=    2.500 ns"));
    assert(contains("zero=2.500 ns"));
    assert(contains("wide=        2.500 ns"));
    assert(contains("string=    2.500 ns"));
    assert(contains("monitor=    2.500 ns q=1"));
    assert(contains("top-time=3"));
    assert(contains("top-stime=3"));
    assert(contains("top-realtime=2.50000000000000000"));
    assert(contains("child-time=0"));
    assert(contains("child-stime=0"));
    assert(contains("child-realtime=0.25000000000000000"));
    const auto contains_at_zero = [&](const std::string_view expected) {
        return std::ranges::any_of(
            reference.output,
            [&](const auto& event) {
                return std::get<0>(event) == expected
                    && std::get<1>(event)
                    && std::get<2>(event) == 0;
            });
    };
    assert(contains_at_zero(
        "Time scale of (time_rounding) is 1ns / 1ps"));
    assert(contains_at_zero(
        "Time scale of (time_rounding.u) is 10ns / 100ps"));
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 2);
    assert(warm.compiled_processes == 2);
#endif
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-time-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "time_rounding.sv";
    const auto scalar_source = directory.path / "scalar_surfaces.sv";
    const auto invalid_math_source = directory.path / "invalid_math.sv";
    const auto timing_source = directory.path / "timing_regions.sv";
    const auto time_format_source = directory.path / "time_format.sv";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(timeunit 1ns / 1ps;
module time_rounding;
  logic [3:0] marker;
  realtime rounded_step;
  int variable_step;
  initial begin
    rounded_step = 0.0005;
    variable_step = 1;
    marker = 0;
    #0.0004 marker = 1;
    #(rounded_step) marker = 2;
    #1.2344ns marker = 3;
    #0.0006us marker = 4;
    #(variable_step) marker = 5;
    $finish;
  end
endmodule
)";
        assert(output.good());
    }
    {
        std::ofstream output(scalar_source, std::ios::binary);
        output << R"(timeunit 1ns / 1ps;
package scalar_callable_pkg;
  parameter real OFFSET = 1.25;
  parameter string PREFIX = ":pkg";
  function automatic real offset(input real value);
    return value + OFFSET;
  endfunction
  function automatic time add_ticks(
      input time value, input time amount = 2);
    return value + amount;
  endfunction
  function automatic string decorate(input string value);
    return {value, PREFIX};
  endfunction
  function automatic chandle retain(input chandle value = null);
    return value;
  endfunction
endpackage

module scalar_surfaces #(parameter real FACTOR = 2.0);
  import scalar_callable_pkg::*;
  real r;
  shortreal s;
  realtime rt;
  time ticks;
  chandle foreign;
  real offset_result;
  realtime scaled_result;
  logic [7:0] callable_checks;
  logic [26:0] math_checks;
  function static real accumulate_real(input real value);
    real retained = 1.0;
    retained = retained + value;
    return retained;
  endfunction
  function automatic realtime scaled(input realtime value);
    return value * FACTOR;
  endfunction
  task automatic transfer(
      input real real_in, output real real_out,
      input time time_in, output time time_out,
      input string string_in, output string string_out,
      input chandle handle_in, output chandle handle_out);
    real_out = real_in;
    time_out = time_in;
    string_out = string_in;
    handle_out = handle_in;
  endtask
  initial begin
    real real_out;
    time time_out;
    string string_out;
    chandle handle_out;
    r = r;
    s = s;
    rt = rt;
    ticks = ticks;
    foreign = foreign;
    callable_checks = 0;
    offset_result = offset(2.75);
    callable_checks[0] = offset_result == 4.0;
    callable_checks[1] = add_ticks(40) == 42;
    callable_checks[2] = add_ticks(40, 3) == 43;
    callable_checks[3] = decorate("value") == "value:pkg";
    callable_checks[4] = retain() == null;
    scaled_result = scaled(1.5);
    callable_checks[5] = scaled_result == 3.0;
    callable_checks[6] =
        accumulate_real(2.0) == 3.0
        && accumulate_real(4.0) == 7.0;
    transfer(6.5, real_out, 77, time_out,
             "copied", string_out, null, handle_out);
    callable_checks[7] =
        real_out == 6.5 && time_out == 77
        && string_out == "copied" && handle_out == null;
    math_checks = '0;
    math_checks[0] = $rtoi(-3.75) == -3;
    math_checks[1] = $itor(-7) == -7.0;
    math_checks[2] = $bitstoreal($realtobits(-2.5)) == -2.5;
    s = -1.25;
    math_checks[3] =
        $bitstoshortreal($shortrealtobits(s)) == -1.25;
    math_checks[4] = $ln($exp(1.0)) > 0.999
        && $ln($exp(1.0)) < 1.001;
    math_checks[5] = $log10(1000.0) == 3.0;
    math_checks[6] = $exp(0.0) == 1.0;
    math_checks[7] = $sqrt(81.0) == 9.0;
    math_checks[8] = $pow(2.0, 10.0) == 1024.0;
    math_checks[9] = $floor(-1.25) == -2.0;
    math_checks[10] = $ceil(-1.25) == -1.0;
    math_checks[11] = $sin(0.0) == 0.0;
    math_checks[12] = $cos(0.0) == 1.0;
    math_checks[13] = $tan(0.0) == 0.0;
    math_checks[14] = $asin(0.0) == 0.0;
    math_checks[15] = $acos(1.0) == 0.0;
    math_checks[16] = $atan(0.0) == 0.0;
    math_checks[17] = $atan2(0.0, 1.0) == 0.0;
    math_checks[18] = $hypot(3.0, 4.0) == 5.0;
    math_checks[19] = $sinh(0.0) == 0.0;
    math_checks[20] = $cosh(0.0) == 1.0;
    math_checks[21] = $tanh(0.0) == 0.0;
    math_checks[22] = $asinh(0.0) == 0.0;
    math_checks[23] = $acosh(1.0) == 0.0;
    math_checks[24] = $atanh(0.0) == 0.0;
    math_checks[25] =
        $realtobits(-0.0) == 64'h8000000000000000;
    s = -0.0;
    math_checks[26] =
        $shortrealtobits(s) == 32'h80000000;
    #1;
    $finish;
  end
endmodule
)";
        assert(output.good());
    }
    {
        std::ofstream output(timing_source, std::ios::binary);
        output << R"(timeunit 1ns / 1ns;
program timing_driver(
    input logic clock,
    input logic [7:0] data,
    output logic [7:0] driven,
    output logic [7:0] first_sample,
    output logic [7:0] third_sample,
    output logic zero_done);
  clocking cb @(posedge clock);
    default input #0 output #1;
    input data;
    output driven;
  endclocking
  default clocking cb;
  initial begin
    first_sample = '0;
    third_sample = '0;
    zero_done = 1'b0;
    ##1 first_sample = cb.data;
    ##2 begin
      third_sample = cb.data;
      cb.driven = 8'ha5;
      #0 zero_done = 1'b1;
    end
  end
endprogram

module timing_top;
  logic clock;
  logic [7:0] first_sample;
  logic [7:0] third_sample;
  logic zero_done;
  logic [7:0] data;
  logic [7:0] driven;
  logic notifier;
  timing_driver driver(
      clock, data, driven, first_sample, third_sample, zero_done);
  specify
    $period(posedge clock, 10, notifier);
    $width(posedge clock, 1, 0, notifier);
  endspecify
  initial begin
    clock = 1'b0;
    data = '0;
    repeat (3) begin
      #5 clock = 1'b1;
      data <= data + 1'b1;
      #5 clock = 1'b0;
    end
    #1 $finish;
  end
endmodule
)";
        assert(output.good());
    }
    {
        std::ofstream output(time_format_source, std::ios::binary);
        output << R"(timeunit 1ns / 1ps;
module print_child;
  timeunit 10ns / 100ps;
  initial begin
    $printtimescale;
    #0.25;
    $display("child-time=%0d", $time);
    $display("child-stime=%0d", $stime());
    $display("child-realtime=%f", $realtime);
  end
endmodule

module time_rounding;
  logic q;
  string formatted;
  print_child u();
  initial begin
    q = 1'b0;
    $printtimescale;
    $printtimescale(u);
    $printtimescale($root.time_rounding.u);
    #2.5;
    $display("top-time=%0d", $time());
    $display("top-stime=%0d", $stime);
    $display("top-realtime=%f", $realtime());
    $timeformat(-9, 3, " ns", 12);
    $display("bare=%t");
    $display("zero=%0t");
    $display("wide=%16t");
    $sformat(formatted, "string=%t");
    $display("%s", formatted);
    $monitor("monitor=%t q=%b", q);
    #0 q = 1'b1;
    #1ps $finish;
  end
endmodule
)";
        assert(output.good());
    }

    verify_mode(
        directory.path,
        source,
        fsim::project::Optimization::o0);
    verify_mode(
        directory.path,
        source,
        fsim::project::Optimization::o2);
    verify_resolution_diagnostic(directory.path, source);
    verify_overflow_diagnostic(directory.path, source);
    const auto scalar_interpreter = verify_scalar_surfaces(
        directory.path, scalar_source, fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::interpreter, "interpreter");
    const auto scalar_o0 = verify_scalar_surfaces(
        directory.path, scalar_source, fsim::project::Optimization::o0,
        fsim::app::SimulationEngine::compiled, "o0");
    const auto scalar_o2 = verify_scalar_surfaces(
        directory.path, scalar_source, fsim::project::Optimization::o2,
        fsim::app::SimulationEngine::compiled, "o2");
    assert(
        scalar_interpreter.payloads == scalar_o0.payloads
        && scalar_o0.payloads == scalar_o2.payloads
        && scalar_interpreter.callable_checks == scalar_o0.callable_checks
        && scalar_o0.callable_checks == scalar_o2.callable_checks
        && scalar_interpreter.math_checks == scalar_o0.math_checks
        && scalar_o0.math_checks == scalar_o2.math_checks
        && scalar_interpreter.change_count == scalar_o0.change_count
        && scalar_o0.change_count == scalar_o2.change_count);
    verify_math_diagnostics(directory.path, invalid_math_source);
    verify_timing_regions(
        directory.path, timing_source, fsim::project::Optimization::o0);
    verify_timing_regions(
        directory.path, timing_source, fsim::project::Optimization::o2);
    verify_time_format(
        directory.path, time_format_source,
        fsim::project::Optimization::o0);
    verify_time_format(
        directory.path, time_format_source,
        fsim::project::Optimization::o2);
    std::cout << "time application tests passed\n";
    return 0;
}
