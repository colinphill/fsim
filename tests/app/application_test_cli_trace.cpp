// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "fsim/app/built_project.hpp"
#include "../api/api_workspace_support.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_cli_trace_and_timescale()
{
    const auto wide_cli_source = directory / "wide-cli.sv";
    namespace workspace = fsim::tests::api_workspace;
    const auto wide_cli_workspace = directory / "wide-cli-workspace";
    const auto wide_cli_trace = wide_cli_workspace / "wide-cli.vcd";
    {
        std::ofstream output(wide_cli_source);
        output << R"(
module wide_cli;
  logic [136:0] value;
  initial begin
    value = {1'b1, 63'b0, 1'bx, 63'b0, 1'bz, 8'b10101010};
    #1 $finish;
  end
endmodule
)";
    }
    std::filesystem::create_directories(wide_cli_workspace);
    std::filesystem::copy_file(wide_cli_source,
        wide_cli_workspace / "wide-cli.sv",
        std::filesystem::copy_options::overwrite_existing);
    workspace::prepare(wide_cli_workspace,
        "fsim::config set elaborate.time_resolution 1ns\n"
        "fsim::config set simulate.max_deltas 1000\n",
        { "wide-cli.sv" }, "sv:work.wide_cli");
    const auto wide_cli = workspace::run_streams(wide_cli_workspace,
        { "simulate", "--trace-output", "wide-cli.vcd" });
    assert(wide_cli.status == 0);
    assert(wide_cli.error.empty());
    std::ifstream wide_cli_stream(wide_cli_trace);
    const std::string wide_cli_vcd {
        std::istreambuf_iterator<char> { wide_cli_stream },
        std::istreambuf_iterator<char> { }
    };
    const auto wide_cli_expected = "1" + std::string(63, '0') + "x" + std::string(63, '0')
        + "z10101010";
    assert(wide_cli_vcd.find("$timescale 1ns $end") != std::string::npos);
    assert(wide_cli_vcd.find(wide_cli_expected) != std::string::npos);
    assert(
        wide_cli_vcd.find(
            "fsim-verilog-scope path=wide_cli unit=work:wide_cli")
        != std::string::npos);
    assert(
        wide_cli_vcd.find(
            "language=systemverilog standard=systemverilog-2017 profile=none")
        != std::string::npos);

    const auto scaled_workspace = directory / "scaled-workspace";
    const auto scaled_trace = scaled_workspace / "scaled.vcd";
    std::filesystem::create_directories(scaled_workspace);
    std::filesystem::copy_file(directory / "tb.sv", scaled_workspace / "tb.sv",
        std::filesystem::copy_options::overwrite_existing);
    workspace::prepare(scaled_workspace,
        "fsim::config set elaborate.time_resolution 2ps\n"
        "fsim::config set simulate.max_deltas 1000\n",
        { "tb.sv" }, "sv:work.tb");
    const auto scaled = workspace::run_streams(scaled_workspace,
        { "simulate", "--trace-output", "scaled.vcd" });
    assert(scaled.status == 0);
    std::ifstream scaled_stream(scaled_trace);
    const std::string scaled_vcd {
        std::istreambuf_iterator<char> { scaled_stream },
        std::istreambuf_iterator<char> { }
    };
    assert(
        scaled_vcd.find("$timescale 1ps $end") != std::string::npos);
    assert(scaled_vcd.find("#4") != std::string::npos);
    assert(
        scaled_vcd.find("$scope module __fsim $end") != std::string::npos
        && scaled_vcd.find("$scope module uvm $end") != std::string::npos
        && scaled_vcd.find("$scope module activity $end") != std::string::npos
        && scaled_vcd.find(" sequence $end") != std::string::npos
        && scaled_vcd.find(" kind $end") != std::string::npos
        && scaled_vcd.find(" action $end") != std::string::npos
        && scaled_vcd.find(" root $end") != std::string::npos
        && scaled_vcd.find(" value $end") != std::string::npos
        && scaled_vcd.find(" identity_hash $end") != std::string::npos
        && scaled_vcd.find(" detail_hash $end") != std::string::npos);

    const auto timescale_source = directory / "timescale.sv";
    {
        std::ofstream output(timescale_source);
        output << R"(`timescale 10ns/100ps
module timed;
initial #2 $finish;
endmodule
)";
    }
    fsim::project::Config timescale_config;
    timescale_config.base_directory = directory;
    timescale_config.project.name = "timescale";
    timescale_config.project.top = "sv:work.timed";
    timescale_config.project.time_resolution = "auto";
    timescale_config.build.cache_path = directory / "timescale-cache";
    timescale_config.run.max_deltas = 1000;
    fsim::project::SourceSet timescale_sources;
    timescale_sources.language = fsim::project::Language::system_verilog;
    timescale_sources.standard = "2017";
    timescale_sources.library = "work";
    timescale_sources.files.push_back(timescale_source);
    timescale_config.source_sets.push_back(
        std::move(timescale_sources));
    fsim::diagnostic::Engine timescale_diagnostics;
    auto timed_project = fsim::app::build_project(
        timescale_config, timescale_diagnostics);
    assert(timed_project);
    assert(timed_project->time_resolution == "100ps");
    fsim::app::Simulation timed_simulation(
        std::move(*timed_project),
        timescale_config.run.max_deltas);
    const auto timed_result = timed_simulation.run();
    assert(timed_result.status == fsim::runtime::RunStatus::stopped);
    assert(timed_result.time == 200);
    timescale_config.project.time_resolution = "1ns";
    fsim::diagnostic::Engine coarse_time_diagnostics;
    assert(!fsim::app::build_project(
        timescale_config, coarse_time_diagnostics));
    assert(coarse_time_diagnostics.has_error());
}

} // namespace fsim::test
