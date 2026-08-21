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
    std::vector<std::string> values;
    std::size_t compiled_processes { };
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "pla-test";
    config.project.top = "sv:work.pla_test";
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
        "aa_and_111", "aa_nand_111", "aa_or_111", "aa_nor_111",
        "ap_and_111", "ap_nand_111", "ap_or_111", "ap_nor_111",
        "aa_and_000", "aa_nand_000", "aa_or_000", "aa_nor_000",
        "ap_and_000", "ap_nand_000", "ap_or_000", "ap_nor_000",
        "ap_memory_change",
        "sa_and", "sa_nand", "sa_or", "sa_nor",
        "sp_and", "sp_nand", "sp_or", "sp_nor",
        "wide_input_result", "wide_output_result"
    };
    for (const auto name : names) {
        const auto signal = simulation.find_signal(
            "pla_test." + std::string { name });
        assert(signal);
        capture.values.push_back(
            simulation.read_signal(*signal).to_msb_string());
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
        *encoded, "pla-runtime", diagnostics);
    assert(restored && !diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
        == encoded);
    project->design = std::move(*restored);
    return execute(std::move(*project), engine);
}

void test_pla(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto config = config_for(directory, source, optimization);
    const auto interpreted = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const std::vector<std::string> expected {
        "1111", "0000", "1101", "0010",
        "0101", "1010", "1100", "0011",
        "0010", "1101", "0000", "1111",
        "0011", "1100", "1010", "0101",
        "1011",
        "1111", "0000", "1101", "0010",
        "0101", "1010", "1100", "0011",
        "11", std::string(137U, '1')
    };
    if (interpreted.values != expected) {
        for (std::size_t index = 0; index < expected.size(); ++index) {
            std::cerr << "PLA value " << index << " observed="
                      << interpreted.values[index] << " expected="
                      << expected[index] << '\n';
        }
    }
    assert(interpreted.values == expected);
    assert(compiled.values == expected);
    assert(interpreted.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes >= 1);
#else
    assert(compiled.compiled_processes == 0);
#endif
}

void test_invalid_pla(const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_pla.sv";
    {
        std::ofstream output(source);
        output << R"(
module pla_test;
  logic [0:2] mem [3:0];
  logic [2:0] a;
  logic [3:0] b;
  initial $sync$and$array(mem, a, b);
endmodule
)";
    }
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(
        config_for(
            directory, source, fsim::project::Optimization::o0),
        diagnostics);
    assert(!project && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVPLA-001";
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
        / ("fsim-pla-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "pla_test.sv";
    {
        std::ofstream output(source);
        output << R"(`timescale 1ns/1ns
module pla_test;
  logic [1:3] array_mem [1:4];
  logic [1:3] plane_mem [1:4];
  logic [1:3] input_terms;
  logic [1:4] aa_and, aa_nand, aa_or, aa_nor;
  logic [1:4] ap_and, ap_nand, ap_or, ap_nor;
  logic [1:4] aa_and_111, aa_nand_111, aa_or_111, aa_nor_111;
  logic [1:4] ap_and_111, ap_nand_111, ap_or_111, ap_nor_111;
  logic [1:4] aa_and_000, aa_nand_000, aa_or_000, aa_nor_000;
  logic [1:4] ap_and_000, ap_nand_000, ap_or_000, ap_nor_000;
  logic [1:4] ap_memory_change;
  logic [1:4] sa_and, sa_nand, sa_or, sa_nor;
  logic [1:4] sp_and, sp_nand, sp_or, sp_nor;
  logic [1:137] wide_input_memory [1:2];
  logic [1:137] wide_input_terms;
  logic [1:2] wide_input_result;
  logic [1:1] wide_output_memory [1:137];
  logic [1:1] wide_output_terms;
  logic [1:137] wide_output_result;
  integer i;

  initial begin
    array_mem[1] = 3'b110;
    array_mem[2] = 3'b001;
    array_mem[3] = 3'b000;
    array_mem[4] = 3'b111;
    plane_mem[1] = 3'b10?;
    plane_mem[2] = 3'b??1;
    plane_mem[3] = 3'b0?0;
    plane_mem[4] = 3'b???;
    input_terms = 3'b111;
    $async$and$array(array_mem, input_terms, aa_and);
    $async$nand$array(array_mem, input_terms, aa_nand);
    $async$or$array(array_mem, input_terms, aa_or);
    $async$nor$array(array_mem, input_terms, aa_nor);
    $async$and$plane(plane_mem, input_terms, ap_and);
    $async$nand$plane(plane_mem, input_terms, ap_nand);
    $async$or$plane(plane_mem, input_terms, ap_or);
    $async$nor$plane(plane_mem, input_terms, ap_nor);
    #1;
    aa_and_111 = aa_and; aa_nand_111 = aa_nand;
    aa_or_111 = aa_or; aa_nor_111 = aa_nor;
    ap_and_111 = ap_and; ap_nand_111 = ap_nand;
    ap_or_111 = ap_or; ap_nor_111 = ap_nor;
    input_terms = 3'b000;
    #1;
    aa_and_000 = aa_and; aa_nand_000 = aa_nand;
    aa_or_000 = aa_or; aa_nor_000 = aa_nor;
    ap_and_000 = ap_and; ap_nand_000 = ap_nand;
    ap_or_000 = ap_or; ap_nor_000 = ap_nor;
    plane_mem[1] = 3'b???;
    #1 ap_memory_change = ap_and;
    plane_mem[1] = 3'b10?;
    input_terms = 3'b111;
    $sync$and$array(array_mem, input_terms, sa_and);
    $sync$nand$array(array_mem, input_terms, sa_nand);
    $sync$or$array(array_mem, input_terms, sa_or);
    $sync$nor$array(array_mem, input_terms, sa_nor);
    $sync$and$plane(plane_mem, input_terms, sp_and);
    $sync$nand$plane(plane_mem, input_terms, sp_nand);
    $sync$or$plane(plane_mem, input_terms, sp_or);
    $sync$nor$plane(plane_mem, input_terms, sp_nor);
    #1;
  end
  initial begin
    wide_input_memory[1] = {137{1'b1}};
    wide_input_memory[2] = {137{1'b0}};
    wide_input_terms = {137{1'b1}};
    for (i = 1; i <= 137; i = i + 1)
      wide_output_memory[i] = 1'b1;
    wide_output_terms = 1'b1;
    $sync$and$array(
      wide_input_memory, wide_input_terms, wide_input_result);
    $sync$and$array(
      wide_output_memory, wide_output_terms, wide_output_result);
  end
endmodule
)";
    }
    test_pla(directory.path, source, fsim::project::Optimization::o0);
    test_pla(directory.path, source, fsim::project::Optimization::o2);
    test_invalid_pla(directory.path);
    std::cout << "PLA application tests passed\n";
    return 0;
}
