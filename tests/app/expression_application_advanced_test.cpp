// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
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
    std::vector<std::string> values;
    fsim::app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
};

struct StopCapture {
    fsim::runtime::RunResult paused;
    fsim::runtime::RunResult resumed;
    std::vector<std::string> paused_values;
    std::vector<std::string> resumed_values;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
};

struct FatalCapture {
    fsim::runtime::simir::AssertionSeverity severity { };
    std::string message;
    std::uint32_t line { };
    std::uint32_t column { };
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        for (const auto& note : diagnostic.notes) {
            std::cerr << "  " << note.message << '\n';
        }
    }
}

template <std::size_t SignalCount>
[[nodiscard]] Capture run(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::array<std::string, SignalCount>& signal_paths)
{
    std::array<
        fsim::runtime::simir::SignalId, SignalCount>
        signals { };
    for (std::size_t index = 0; index < signal_paths.size(); ++index) {
        const auto signal = project.design.find_signal(signal_paths[index]);
        if (!signal) {
            std::cerr << "missing expression-test signal: "
                      << signal_paths[index] << '\n';
        }
        assert(signal);
        signals[index] = *signal;
    }

    fsim::app::Simulation simulation(
        std::move(project), 1000, engine);
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.native_cache = simulation.native_cache_statistics();
    capture.result = simulation.run();
    capture.values.reserve(signals.size());
    for (const auto signal : signals) {
        capture.values.push_back(
            simulation.read_signal(signal).to_msb_string());
    }
    return capture;
}

[[nodiscard]] StopCapture run_stop(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    const std::array<std::string, 3> signal_paths {
        "stop_app.before_stop",
        "stop_app.after_stop",
        "stop_app.final_hit"
    };
    std::array<fsim::runtime::simir::SignalId, 3> signals { };
    for (std::size_t index = 0; index < signal_paths.size(); ++index) {
        const auto signal = project.design.find_signal(signal_paths[index]);
        assert(signal);
        signals[index] = *signal;
    }

    fsim::app::Simulation simulation(
        std::move(project), 1000, engine);
    StopCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.paused = simulation.run();
    assert(!simulation.finished());
    for (const auto signal : signals) {
        capture.paused_values.push_back(
            simulation.read_signal(signal).to_msb_string());
    }

    simulation.clear_stop();
    capture.resumed = simulation.run();
    assert(simulation.finished());
    for (const auto signal : signals) {
        capture.resumed_values.push_back(
            simulation.read_signal(signal).to_msb_string());
    }
    return capture;
}

[[nodiscard]] FatalCapture run_fatal(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation(
        std::move(project), 1000, engine);
    FatalCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    try {
        (void)simulation.run();
        assert(false && "$fatal must fail simulation");
    } catch (const fsim::runtime::simir::AssertionError& error) {
        capture.severity = error.severity();
        capture.message = error.what();
        capture.line = error.source().line;
        capture.column = error.source().column;
    }
    return capture;
}

void test_systemverilog_literal_closure(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "literal-closure-expression-test";
    config.project.top = "sv:work.literal_closure_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "literal-closure-cache-o0"
                : "literal-closure-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 5> signal_paths {
        "literal_closure_app.decimal_x",
        "literal_closure_app.decimal_z",
        "literal_closure_app.decimal_question",
        "literal_closure_app.unbased_one",
        "literal_closure_app.unbased_x"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert((compiled.values
        == std::vector<std::string> {
            std::string(257, 'X'),
            std::string(257, 'Z'),
            std::string(257, 'Z'),
            std::string(257, '1'),
            std::string(257, 'X') }));
    assert(reference.compiled_processes == 0);
    assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_systemverilog_packed_index_self_determined(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "packed-index-self-determined-test";
    config.project.top = "sv:work.packed_index_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "packed-index-cache-o0"
                : "packed-index-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2005";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 1> paths {
        "packed_index_app.wrapped_select"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        paths);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.result.status == compiled.result.status);
    assert(reference.values == compiled.values);
    assert((compiled.values == std::vector<std::string> { "1" }));
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_concurrent_assertion(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-concurrent-assertion-test";
    config.project.top = "vhdl:work.concurrent_assertion_app(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "concurrent-assert-cache-o0"
                : "concurrent-assert-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 1> signal_paths {
        "concurrent_assertion_app.passed"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert(compiled.values == std::vector<std::string> { "1" });
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 2);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_falling_edge(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-falling-edge-test";
    config.project.top = "vhdl:work.falling_edge_app(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "falling-edge-cache-o0"
                : "falling-edge-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    auto warm_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project || !warm_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);
    assert(warm_project);

    const std::array<std::string, 22> signal_paths {
        "falling_edge_app.hit",
        "falling_edge_app.legacy_hit",
        "falling_edge_app.falling_previous",
        "falling_edge_app.elapsed",
        "falling_edge_app.active_elapsed",
        "falling_edge_app.stable_during_event",
        "falling_edge_app.stable_after_event",
        "falling_edge_app.redundant_active",
        "falling_edge_app.redundant_event",
        "falling_edge_app.driving_seen",
        "falling_edge_app.driving_value_seen",
        "falling_edge_app.driving_vector_seen",
        "falling_edge_app.foreign_driving",
        "falling_edge_app.stable_window_early",
        "falling_edge_app.quiet_window_early",
        "falling_edge_app.delayed_sample",
        "falling_edge_app.redundant_transaction",
        "falling_edge_app.single_transaction",
        "falling_edge_app.stable_window_late",
        "falling_edge_app.quiet_window_late",
        "falling_edge_app.transaction_sensitive",
        "falling_edge_app.transaction_waited"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);
    const auto warm = run(
        std::move(*warm_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert(warm.result.status == compiled.result.status);
    assert(warm.values == compiled.values);
    assert((
        compiled.values
        == std::vector<std::string> {
            "1",
            "1",
            "1",
            "0000000000000000000000000000000000000000000000000000000000000001",
            "0000000000000000000000000000000000000000000000000000000000000001",
            "1",
            "1",
            "1",
            "0",
            "1",
            "1",
            "10ZX",
            "0",
            "0",
            "0",
            "1",
            "0",
            "1",
            "1",
            "1",
            "0",
            "1" }));
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 12);
    assert(compiled.compiled_modules == 1);
    assert(warm.native_cache.hits == 1);
    assert(warm.native_cache.misses == 0);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_verilog_stop(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "verilog-stop-test";
    config.project.top = "sv:work.stop_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "stop-cache-o0"
                : "stop-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const auto reference = run_stop(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_stop(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    assert(reference.paused.status == fsim::runtime::RunStatus::stopped);
    assert(reference.paused.status == compiled.paused.status);
    assert(reference.paused.time == compiled.paused.time);
    assert(reference.paused.delta == compiled.paused.delta);
    assert(reference.paused_values == compiled.paused_values);
    assert((
        compiled.paused_values
        == std::vector<std::string> { "1", "X", "X" }));
    assert(reference.resumed.status == fsim::runtime::RunStatus::stopped);
    assert(reference.resumed.status == compiled.resumed.status);
    assert(reference.resumed.time == compiled.resumed.time);
    assert(reference.resumed.delta == compiled.resumed.delta);
    assert(reference.resumed_values == compiled.resumed_values);
    assert((
        compiled.resumed_values
        == std::vector<std::string> { "1", "1", "1" }));
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 2);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_systemverilog_exit(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "systemverilog-exit-test";
    config.project.top = "sv:work.exit_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "exit-cache-o0"
                : "exit-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    const std::array<std::string, 5> paths {
        "exit_app.first_after",
        "exit_app.first_background",
        "exit_app.second_done",
        "exit_app.module_late",
        "exit_app.final_hit"
    };
    const auto build = [&]() {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        if (!project)
            print_diagnostics(diagnostics);
        assert(project && !diagnostics.has_error());
        fsim::diagnostic::Engine artifact_diagnostics;
        const auto state = fsim::app::serialize_runtime_state(
            project->design, artifact_diagnostics);
        assert(state && !artifact_diagnostics.has_error());
        auto restored = fsim::app::deserialize_runtime_state(
            *state, "exit-runtime", artifact_diagnostics);
        assert(restored && !artifact_diagnostics.has_error());
        assert(fsim::app::serialize_runtime_state(
                   *restored, artifact_diagnostics)
            == state);
        project->design = std::move(*restored);
        return project;
    };
    const auto interpreted = run(
        *build(), fsim::app::SimulationEngine::interpreter, paths);
    const auto compiled_cold = run(
        *build(), fsim::app::SimulationEngine::compiled, paths);
    const auto compiled_warm = run(
        *build(), fsim::app::SimulationEngine::compiled, paths);
    const auto expected = std::vector<std::string> {
        "0", "0", "1", "0", "1"
    };
    assert(interpreted.result.status == fsim::runtime::RunStatus::stopped);
    assert(interpreted.result.time == 2);
    assert(compiled_cold.result.status == interpreted.result.status);
    assert(compiled_warm.result.status == interpreted.result.status);
    assert(compiled_cold.result.time == interpreted.result.time);
    assert(compiled_warm.result.time == interpreted.result.time);
    assert(interpreted.values == expected);
    assert(compiled_cold.values == expected);
    assert(compiled_warm.values == expected);
#if defined(FSIM_HAS_LLVM)
    assert(compiled_cold.compiled_processes != 0);
    assert(compiled_warm.native_cache.hits != 0);
#else
    assert(compiled_cold.compiled_processes == 0);
#endif
}

void test_invalid_systemverilog_exit(
    const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_exit.sv";
    {
        std::ofstream output(source);
        output << R"(
module invalid_exit;
  initial $exit;
endmodule
)";
    }
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "invalid-systemverilog-exit-test";
    config.project.top = "sv:work.invalid_exit";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVEXIT-001";
        }));
}

void test_invalid_systemverilog_sampled_value(
    const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_sampled_value.sv";
    {
        std::ofstream output(source);
        output << R"(
module invalid_sampled_value;
  logic data;
  logic result;
  initial result = $past(data, 0);
endmodule
)";
    }
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "invalid-systemverilog-sampled-value-test";
    config.project.top = "sv:work.invalid_sampled_value";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVSAMPLE-001";
        }));
}

void test_systemverilog_sampled_values(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "systemverilog-sampled-value-test";
    config.project.top = "sv:work.sampled_value_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "sampled-cache-o0"
                : "sampled-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    const std::array<std::string, 12> paths {
        "sampled_value_app.first",
        "sampled_value_app.second",
        "sampled_value_app.third",
        "sampled_value_app.explicit_first",
        "sampled_value_app.explicit_second",
        "sampled_value_app.explicit_third",
        "sampled_value_app.global_past_first",
        "sampled_value_app.global_past_second",
        "sampled_value_app.global_past_third",
        "sampled_value_app.global_future_first",
        "sampled_value_app.global_future_second",
        "sampled_value_app.global_future_third"
    };
    const auto build = [&]() {
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        if (!project)
            print_diagnostics(diagnostics);
        assert(project && !diagnostics.has_error());
        fsim::diagnostic::Engine artifact_diagnostics;
        const auto state = fsim::app::serialize_runtime_state(
            project->design, artifact_diagnostics);
        assert(state && !artifact_diagnostics.has_error());
        auto restored = fsim::app::deserialize_runtime_state(
            *state, "sampled-runtime", artifact_diagnostics);
        assert(restored && !artifact_diagnostics.has_error());
        assert(fsim::app::serialize_runtime_state(
                   *restored, artifact_diagnostics)
            == state);
        project->design = std::move(*restored);
        return project;
    };
    const auto interpreted = run(
        *build(), fsim::app::SimulationEngine::interpreter, paths);
    const auto compiled_cold = run(
        *build(), fsim::app::SimulationEngine::compiled, paths);
    const auto compiled_warm = run(
        *build(), fsim::app::SimulationEngine::compiled, paths);
    const auto expected = std::vector<std::string> {
        "00101XX", "110010X", "0010110",
        "00XX", "110X", "0110",
        "X0101", "01001", "10101",
        "11001", "00101", "11001"
    };
    assert(interpreted.result.status == fsim::runtime::RunStatus::stopped);
    assert(interpreted.result.time == 6);
    assert(interpreted.values == expected);
    assert(compiled_cold.values == expected);
    assert(compiled_warm.values == expected);
#if defined(FSIM_HAS_LLVM)
    assert(compiled_cold.compiled_processes != 0);
    assert(compiled_warm.native_cache.hits != 0);
#else
    assert(compiled_cold.compiled_processes == 0);
#endif
}

void test_systemverilog_fatal(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "systemverilog-fatal-test";
    config.project.top = "sv:work.fatal_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "fatal-cache-o0"
                : "fatal-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const auto reference = run_fatal(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_fatal(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    assert(reference.severity
        == fsim::runtime::simir::AssertionSeverity::failure);
    assert(reference.severity == compiled.severity);
    assert(reference.message == compiled.message);
    assert(reference.line == compiled.line);
    assert(reference.column == compiled.column);
    assert(reference.message.find("fatal\nsource\tmessage \"quoted\"")
        != std::string::npos);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_array_attributes(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-array-attribute-test";
    config.project.top = "vhdl:work.attribute_app(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "attribute-cache-o0"
                : "attribute-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 12> paths {
        "attribute_app.descending_left",
        "attribute_app.descending_right",
        "attribute_app.descending_low",
        "attribute_app.descending_high",
        "attribute_app.descending_length",
        "attribute_app.descending_ascending",
        "attribute_app.ascending_left",
        "attribute_app.ascending_right",
        "attribute_app.ascending_low",
        "attribute_app.ascending_high",
        "attribute_app.ascending_length",
        "attribute_app.ascending_ascending"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        paths);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.result.status == compiled.result.status);
    assert(reference.values == compiled.values);
    assert((
        compiled.values
        == std::vector<std::string> {
            "00000000000000000000000000000111",
            "00000000000000000000000000000100",
            "00000000000000000000000000000100",
            "00000000000000000000000000000111",
            "00000000000000000000000000000100",
            "0",
            "00000000000000000000000000000010",
            "00000000000000000000000000000101",
            "00000000000000000000000000000010",
            "00000000000000000000000000000101",
            "00000000000000000000000000000100",
            "1" }));
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

} // namespace

void run_expression_application_part2(
    const std::filesystem::path& directory)
{
    const auto literal = directory / "literal_closure.sv";
    const auto packed = directory / "packed_index.sv";
    const auto assertion = directory / "concurrent_assertion.vhd";
    const auto falling = directory / "falling_edge.vhd";
    const auto stop = directory / "stop.sv";
    const auto exit = directory / "exit.sv";
    const auto sampled = directory / "sampled_values.sv";
    const auto fatal = directory / "fatal.sv";
    const auto attributes = directory / "attributes.vhd";
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        test_systemverilog_literal_closure(directory, literal, optimization);
        test_systemverilog_packed_index_self_determined(
            directory, packed, optimization);
        test_vhdl_concurrent_assertion(directory, assertion, optimization);
        test_vhdl_falling_edge(directory, falling, optimization);
        test_verilog_stop(directory, stop, optimization);
        test_systemverilog_exit(directory, exit, optimization);
    }
    test_invalid_systemverilog_exit(directory);
    test_invalid_systemverilog_sampled_value(directory);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        test_systemverilog_sampled_values(
            directory, sampled, optimization);
        test_systemverilog_fatal(directory, fatal, optimization);
        test_vhdl_array_attributes(directory, attributes, optimization);
    }
}
