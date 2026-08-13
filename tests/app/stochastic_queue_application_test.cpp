// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
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
    std::vector<std::uint32_t> values;
    std::size_t compiled_processes { };
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "stochastic-queue-test";
    config.project.top = "sv:work.stochastic_queue_test";
    config.project.time_resolution = "1ns";
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

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation { std::move(project), 1000, engine };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    assert(simulation.run().status == fsim::runtime::RunStatus::completed);
    constexpr std::string_view names[] = {
        "init_status", "full_before", "full_status",
        "mean_interarrival", "maximum_occupancy", "removed_job",
        "removed_information", "shortest_wait", "longest_wait",
        "average_wait", "full_after", "empty_status",
        "duplicate_status", "unsupported_status", "length_status",
        "undefined_status", "lifo_job", "lifo_information"
    };
    for (const auto name : names) {
        const auto signal = simulation.find_signal(
            "stochastic_queue_test." + std::string { name });
        assert(signal);
        const auto word = simulation.read_signal(*signal).low_word();
        assert(word.bval == 0);
        capture.values.push_back(static_cast<std::uint32_t>(word.aval));
    }
    return capture;
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
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "stochastic-queue-runtime", diagnostics);
    assert(restored && !diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
        == encoded);
    project->design = std::move(*restored);
    return execute(std::move(*project), engine);
}

void test_stochastic_queues(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto config = config_for(directory, source, optimization);
    const auto interpreted = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::uint32_t> expected {
        0, 1, 0, 4, 2, 11, 101, 10, 6, 10, 0, 3,
        6, 4, 5, 2, 22, 202
    };
    assert(interpreted.values == expected);
    assert(compiled.values == expected);
    assert(interpreted.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
#else
    assert(compiled.compiled_processes == 0);
#endif
}

void test_invalid_stochastic_queue(const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_stochastic_queue.sv";
    {
        std::ofstream output(source);
        output << R"(
module stochastic_queue_test;
  integer status;
  initial $q_initialize(1, 1, status);
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o0);
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVQUEUE-001";
        }));
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-stochastic-queue-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "stochastic_queue_test.sv";
    {
        std::ofstream output(source);
        output << R"(`timescale 1ns/1ns
module stochastic_queue_test;
  integer status, statistic, job, information;
  integer init_status, full_before, full_status;
  integer mean_interarrival, maximum_occupancy;
  integer removed_job, removed_information;
  integer shortest_wait, longest_wait, average_wait;
  integer full_after, empty_status, duplicate_status;
  integer unsupported_status, length_status, undefined_status;
  integer lifo_job, lifo_information;
  initial begin
    $q_initialize(7, 1, 2, status); init_status = status;
    $q_add(7, 11, 101, status);
    #4 $q_add(7, 12, 102, status);
    full_before = $q_full(7, status); full_status = status;
    $q_exam(7, 2, statistic, status); mean_interarrival = statistic;
    $q_exam(7, 3, statistic, status); maximum_occupancy = statistic;
    #6 $q_remove(7, job, information, status);
    removed_job = job; removed_information = information;
    $q_exam(7, 4, statistic, status); shortest_wait = statistic;
    $q_exam(7, 5, statistic, status); longest_wait = statistic;
    $q_exam(7, 6, statistic, status); average_wait = statistic;
    $q_remove(7, job, information, status);
    full_after = $q_full(7, status);
    $q_remove(7, job, information, status); empty_status = status;
    $q_initialize(7, 1, 2, status); duplicate_status = status;
    $q_initialize(9, 3, 2, status); unsupported_status = status;
    $q_initialize(10, 1, 0, status); length_status = status;
    full_after = $q_full(99, status); undefined_status = status;
    $q_initialize(8, 2, 2, status);
    $q_add(8, 21, 201, status); $q_add(8, 22, 202, status);
    $q_remove(8, lifo_job, lifo_information, status);
  end
endmodule
)";
    }
    test_stochastic_queues(
        directory.path, source, fsim::project::Optimization::o0);
    test_stochastic_queues(
        directory.path, source, fsim::project::Optimization::o2);
    test_invalid_stochastic_queue(directory.path);
    std::cout << "stochastic queue application tests passed\n";
    return 0;
}
