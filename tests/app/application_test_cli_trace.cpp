// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include <cassert>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_cli_trace_and_timescale(
    fsim::cli::Services& services)
{
    const auto wide_cli_source = directory / "wide-cli.sv";
    const auto wide_cli_manifest = directory / "wide-cli.toml";
    const auto wide_cli_trace = directory / "wide-cli.vcd";
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
    {
        std::ofstream output(wide_cli_manifest);
        output << R"(
schema = 3
[project]
name = "wide-cli"
top = "sv:work.wide_cli"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["wide-cli.sv"]

[build]
cache_path = "wide-cli-cache"

[run]
max_deltas = 1000
trace_file = "wide-cli.vcd"
)";
    }
    std::ostringstream wide_cli_output;
    std::ostringstream wide_cli_error;
    const auto wide_cli_manifest_text = wide_cli_manifest.string();
    const std::vector<const char*> wide_cli_arguments {
        "fsim", "run", "-p", wide_cli_manifest_text.c_str()
    };
    assert(
        fsim::cli::run(
            static_cast<int>(wide_cli_arguments.size()),
            wide_cli_arguments.data(),
            services,
            wide_cli_output,
            wide_cli_error)
        == 0);
    assert(wide_cli_error.str().empty());
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

    const auto scaled_manifest = directory / "scaled.toml";
    const auto scaled_trace = directory / "scaled.vcd";
    {
        std::ofstream output(scaled_manifest);
        output << R"(
schema = 3
[project]
name = "scaled-vcd"
top = "sv:work.tb"
time_resolution = "2ps"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "scaled-cache"

[run]
max_deltas = 1000
trace_file = "scaled.vcd"
)";
    }
    std::ostringstream scaled_output;
    std::ostringstream scaled_error;
    const auto scaled_manifest_text = scaled_manifest.string();
    const std::vector<const char*> scaled_arguments {
        "fsim", "run", "-p", scaled_manifest_text.c_str()
    };
    assert(
        fsim::cli::run(
            static_cast<int>(scaled_arguments.size()),
            scaled_arguments.data(),
            services,
            scaled_output,
            scaled_error)
        == 0);
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
