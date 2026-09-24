// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/compiler/llvm_jit.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
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

constexpr std::size_t supported_processes = 128U;

void write_source(const std::filesystem::path& path)
{
    std::ofstream output { path };
    output << "module pack_leaf #(parameter integer VALUE = 0);\n"
              "  logic [7:0] value;\n"
              "  initial value = VALUE;\n"
              "endmodule\n"
              "module unsupported_pack;\n"
              "  logic [127:0] wide;\n"
              "  initial $monitor(\"wide=%h\", wide);\n";
    for (std::size_t index = 0; index < supported_processes; ++index) {
        output << "  pack_leaf #(.VALUE(" << index << ")) leaf_"
               << index << "();\n";
    }
    output << "endmodule\n";
    assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "unsupported-pack";
    config.project.top = "sv:work.unsupported_pack";
    config.project.time_resolution = "1ns";
    config.build.optimization = fsim::project::Optimization::o0;
    config.build.cache_path = directory / "cache";
    config.run.max_deltas = 1000U;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

fsim::app::BuiltProject build(const fsim::project::Config& config)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(project);
    project->compiled_process_selection
        = fsim::app::BuiltProject::CompiledProcessSelection::all;
    return std::move(*project);
}

void check_preflight(const fsim::app::BuiltProject& project)
{
    std::vector<std::uint32_t> widths;
    std::vector<fsim::runtime::simir::ValueKind> kinds;
    widths.reserve(project.design.signals().size());
    kinds.reserve(project.design.signals().size());
    for (const auto& signal : project.design.signals()) {
        widths.push_back(static_cast<std::uint32_t>(signal.width));
        kinds.push_back(fsim::runtime::simir::ValueKind::logic4);
    }
    fsim::compiler::LlvmJit probe;
    std::size_t supported = 0U;
    std::size_t unsupported = 0U;
    for (const auto& process : project.design.processes()) {
        if (probe.supports_process(process, widths, kinds)) {
            ++supported;
        } else {
            ++unsupported;
        }
    }
    assert(supported == supported_processes);
    assert(unsupported == 1U);
}

struct Capture {
    std::array<std::uint32_t, supported_processes> values { };
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics cache;
};

Capture execute(fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation { std::move(project), 1000U, engine };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    simulation.await_all_native_compilation();
    capture.cache = simulation.native_cache_statistics();
    assert(simulation.run().status == fsim::runtime::RunStatus::completed);
    for (std::size_t index = 0; index < supported_processes; ++index) {
        const auto signal = simulation.find_signal(
            "unsupported_pack.leaf_" + std::to_string(index) + ".value");
        assert(signal);
        const auto word = simulation.read_signal(*signal).low_word();
        assert(word.bval == 0U);
        capture.values[index] = static_cast<std::uint32_t>(word.aval);
    }
    return capture;
}

} // namespace

int main()
{
    const auto unique = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-unsupported-pack-" + std::to_string(unique))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "unsupported_pack.sv";
    write_source(source);
    const auto config = make_config(directory.path, source);

    auto project = build(config);
    check_preflight(project);
    const auto reference = execute(
        project, fsim::app::SimulationEngine::interpreter);
    const auto cold = execute(
        std::move(project), fsim::app::SimulationEngine::compiled);
    const auto warm = execute(
        build(config), fsim::app::SimulationEngine::compiled);

    for (std::size_t index = 0; index < supported_processes; ++index) {
        assert(reference.values[index] == index);
    }
    assert(cold.values == reference.values);
    assert(warm.values == reference.values);
    assert(reference.compiled_processes == 0U);
    assert(cold.compiled_processes == supported_processes);
    assert(cold.compiled_modules > 0U);
    assert(cold.compiled_modules < supported_processes);
    assert(cold.cache.hits == 0U);
    assert(cold.cache.misses == cold.compiled_modules);
    assert(cold.cache.stores == cold.compiled_modules);
    assert(warm.compiled_processes == cold.compiled_processes);
    assert(warm.compiled_modules == cold.compiled_modules);
    assert(warm.cache.hits == warm.compiled_modules);
    assert(warm.cache.misses == 0U);
    assert(warm.cache.stores == 0U);
    std::cout << "unsupported process pack: passed\n";
}
