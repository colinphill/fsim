// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <ranges>
#include <string>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vcd-control-test";
    config.project.top = "sv:work.vcd_control_test";
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

std::string execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::filesystem::path& output)
{
    {
        fsim::app::Simulation simulation { std::move(project), 1000, engine };
        const auto compiled = simulation.compiled_process_count();
        const auto result = simulation.run();
        assert(result.status == fsim::runtime::RunStatus::completed);
        if (engine == fsim::app::SimulationEngine::compiled) {
#if defined(FSIM_HAS_LLVM)
            assert(compiled != 0);
#else
            assert(compiled == 0);
#endif
        }
    }
    std::ifstream input(output, std::ios::binary);
    assert(input);
    return { std::istreambuf_iterator<char> { input }, { } };
}

std::string run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::filesystem::path& output)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project);
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "vcd-control-runtime", diagnostics);
    assert(restored && !diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
        == encoded);
    project->design = std::move(*restored);
    return execute(std::move(*project), engine, output);
}

void check_trace(const std::string& trace)
{
    const auto contains = [&](const std::string_view text) {
        return trace.find(text) != std::string::npos;
    };
    assert(contains("$timescale 1ns $end"));
    assert(contains("$scope module vcd_control_test $end"));
    assert(contains("$var wire 137"));
    assert(contains("$dumpvars\n"));
    assert(contains("$dumpoff\n"));
    assert(contains("$dumpon\n"));
    assert(contains("$dumpall\n"));
    assert(contains("#1\n"));
    assert(contains("#2\n"));
    assert(contains("#3\n"));
    assert(contains(std::string(137, 'x')));
    assert(contains(std::string(137, '1')));
    assert(!contains("ignored"));
}

void test_vcd_controls(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto config = config_for(directory, source, optimization);
    const auto output = directory / "hdl-control.vcd";
    const auto interpreted = run_once(
        config, fsim::app::SimulationEngine::interpreter, output);
    check_trace(interpreted);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled, output);
    check_trace(compiled);
    assert(interpreted == compiled);
}

void test_invalid_vcd_control(const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_vcd_control.sv";
    std::ofstream(source) << R"(
module vcd_control_test;
  initial $dumplimit;
endmodule
)";
    auto config = config_for(
        directory, source, fsim::project::Optimization::o0);
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVVCD-001";
        }));
}

void test_default_file_and_limit(const std::filesystem::path& directory)
{
    const auto source = directory / "default_vcd_control.sv";
    std::ofstream(source) << R"(
module vcd_control_test;
  logic value;
  initial begin
    value = 1'b0;
    $dumplimit(1);
    $dumpvars;
    #1 value = 1'b1;
  end
endmodule
)";
    const auto config = config_for(
        directory, source, fsim::project::Optimization::o0);
    const auto trace = run_once(
        config,
        fsim::app::SimulationEngine::interpreter,
        directory / "dump.vcd");
    assert(trace.find("$comment dump limit reached $end")
        != std::string::npos);
}

void check_extended_trace(const std::string& trace)
{
    const auto contains = [&](const std::string_view text) {
        return trace.find(text) != std::string::npos;
    };
    assert(contains("$timescale 1ns $end"));
    assert(contains("$scope module vcd_control_test.selected $end"));
    assert(contains("$var port [136:0] <0 in_port $end"));
    assert(contains("$var port [136:0] <1 out_port $end"));
    assert(!contains("nested_port"));
    assert(contains("$dumpports\n"));
    assert(contains("$dumpportsoff\n"));
    assert(contains("$dumpportson\n"));
    assert(contains("$dumpportsall\n"));
    assert(contains("p" + std::string(137, 'D') + " 6 0 <0\n"));
    assert(contains("p" + std::string(137, 'L') + " 6 0 <1\n"));
    assert(contains("p" + std::string(137, 'U') + " 0 6 <0\n"));
    assert(contains("p" + std::string(137, 'H') + " 0 6 <1\n"));
    assert(contains("p" + std::string(137, 'N') + " 6 6 <0\n"));
    assert(contains("p" + std::string(137, 'X') + " 6 6 <1\n"));
    assert(contains("$vcdclose #3 $end"));
}

void test_extended_vcd_controls(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization)
{
    const auto source = directory
        / (optimization == fsim::project::Optimization::o0
                ? "extended_vcd_o0.sv"
                : "extended_vcd_o2.sv");
    std::ofstream(source) << R"(
module nested(input logic [136:0] nested_port);
endmodule

module selected(
    input logic [136:0] in_port,
    output logic [136:0] out_port);
  nested below(in_port);
  assign out_port = in_port;
endmodule

module vcd_control_test;
  logic [136:0] source;
  logic [136:0] result;
  string dump_name;
  selected selected(source, result);
  initial begin
    source = '0;
    dump_name = "ports-control.vcd";
    $dumpports(selected, dump_name);
    $dumpportslimit(1000000, "ports-control.vcd");
    #1 source = '1;
    #1 $dumpportsoff("ports-control.vcd");
    source = '0;
    #1 $dumpportson("ports-control.vcd");
    $dumpportsall;
    $dumpportsflush;
  end
endmodule
)";
    const auto config = config_for(directory, source, optimization);
    const auto output = directory / "ports-control.vcd";
    const auto interpreted = run_once(
        config, fsim::app::SimulationEngine::interpreter, output);
    check_extended_trace(interpreted);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled, output);
    check_extended_trace(compiled);
    assert(interpreted == compiled);
}

void test_extended_default_scope(const std::filesystem::path& directory)
{
    const auto source = directory / "extended_vcd_default.sv";
    std::ofstream(source) << R"(
module vcd_control_test(
    input logic input_port,
    output logic output_port);
  assign output_port = input_port;
  initial begin
    $dumpports(, "null-scope.vcd");
    $dumpportsflush("null-scope.vcd");
  end
endmodule
)";
    const auto config = config_for(
        directory, source, fsim::project::Optimization::o0);
    const auto trace = run_once(config,
        fsim::app::SimulationEngine::interpreter,
        directory / "null-scope.vcd");
    assert(trace.find("$scope module vcd_control_test $end")
        != std::string::npos);
    assert(trace.find("$var port 1 <0 input_port $end")
        != std::string::npos);
    assert(trace.find("$var port 1 <1 output_port $end")
        != std::string::npos);
}

void test_extended_verilog_default_file(
    const std::filesystem::path& directory)
{
    const auto source = directory / "extended_vcd_verilog.v";
    std::ofstream(source) << R"(
module vcd_control_test(input_port, output_port);
  input input_port;
  output output_port;
  wire output_port;
  assign output_port = input_port;
  initial begin
    $dumpports;
    $dumpportsflush;
  end
endmodule
)";
    auto config = config_for(
        directory, source, fsim::project::Optimization::o0);
    config.source_sets.front().language = fsim::project::Language::verilog;
    config.source_sets.front().standard = "2005";
    const auto trace = run_once(config,
        fsim::app::SimulationEngine::interpreter,
        directory / "dumpports.vcd");
    assert(trace.find("$scope module vcd_control_test $end")
        != std::string::npos);
    assert(trace.find("$var port 1 <0 input_port $end")
        != std::string::npos);
    assert(trace.find("$vcdclose #0 $end") != std::string::npos);
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vcd-control-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "vcd_control_test.sv";
    std::ofstream(source) << R"(
module vcd_control_test;
  logic [136:0] wide;
  logic ignored;
  initial begin
    wide = '0;
    ignored = 1'b0;
    $dumpfile("hdl-control.vcd");
    $dumplimit(1000000);
    $dumpvars(1, vcd_control_test.wide);
    #1 wide = {137{1'b1}};
    #1 $dumpoff;
    wide = '0;
    #1 $dumpon;
    $dumpall;
    $dumpflush;
  end
endmodule
)";
    test_vcd_controls(
        directory.path, source, fsim::project::Optimization::o0);
    test_vcd_controls(
        directory.path, source, fsim::project::Optimization::o2);
    test_invalid_vcd_control(directory.path);
    test_default_file_and_limit(directory.path);
    test_extended_vcd_controls(
        directory.path, fsim::project::Optimization::o0);
    test_extended_vcd_controls(
        directory.path, fsim::project::Optimization::o2);
    test_extended_default_scope(directory.path);
    test_extended_verilog_default_file(directory.path);
    std::cout << "VCD control application tests passed\n";
    return 0;
}
