// SPDX-License-Identifier: Apache-2.0
//
// A2 combinational cone fusion: a gf_mult-shaped continuous-assignment netlist
// (per-bit AND terms with constant zero drivers, an XOR reduction, and a
// constant-index reduction array with 4-state ternaries) must produce the same
// transcript fused and unfused, and hidden internal nets must read back their
// exact current values after observation materializes them.
#include "fsim/app/application.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

using fsim::app::BuiltProject;
using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::app::SystemVerilogVpiRuntimeUpdates;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "cone fusion test failure: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::string_view source = R"(
module mul4(input wire [3:0] a, input wire [3:0] b, output wire [3:0] p);
  wire [6:0] unr;
  genvar k, i;
  generate
    for (k = 0; k < 7; k = k + 1) begin : g_unr
      wire [3:0] t;
      for (i = 0; i < 4; i = i + 1) begin : g_t
        if (i <= k && k - i < 4)
          assign t[i] = a[i] & b[k - i];
        else
          assign t[i] = 1'b0;
      end
      assign unr[k] = ^t;
    end
  endgenerate
  wire [6:0] red [0:3];
  assign red[0] = unr;
  generate
    for (i = 0; i < 3; i = i + 1) begin : g_red
      localparam integer HI = 6 - i;
      wire [6:0] sh = 7'b0010011 << (HI - 4);
      assign red[i + 1] = red[i][HI] ? (red[i] ^ sh) : red[i];
    end
  endgenerate
  assign p = red[3][3:0];
endmodule

module cone_top;
  reg [3:0] a, b;
  wire [3:0] p;
  mul4 u(.a(a), .b(b), .p(p));
  integer i, j;
  initial begin
    for (i = 0; i < 16; i = i + 1)
      for (j = 0; j < 16; j = j + 1) begin
        a = i; b = j;
        #1 $display("%0d %0d %b", i, j, p);
      end
    a = 4'bx01x; b = 4'b1z01;
    #1 $display("x %b", p);
    a = 4'b0110; b = 4'b1011;
    #1 $display("final %b", p);
    $finish;
  end
endmodule
)";

[[nodiscard]] BuiltProject build(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto path = root / "cone_fusion.sv";
    {
        std::ofstream file(path);
        file << source;
        require(static_cast<bool>(file), "the fixture source must be written");
    }
    fsim::project::Config config;
    config.project.name = "cone-fusion";
    config.project.top = "sv:work.cone_top";
    config.base_directory = root;
    config.build.optimization = fsim::project::Optimization::o2;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(path);
    config.source_sets.push_back(std::move(sources));
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& item : diagnostics.diagnostics()) {
            std::cerr << item.code << ": " << item.message << '\n';
        }
    }
    require(project.has_value(), "the fixture must elaborate");
    return std::move(*project);
}

struct RunCapture {
    std::vector<std::string> lines;
    // Internal nets sampled mid-run (after the exhaustive loop) and at the end.
    std::vector<std::string> mid_internal;
    std::vector<std::string> final_internal;
    std::string final_output;
};

const std::vector<std::string> internal_paths {
    "cone_top.u.unr",
    "cone_top.u.g_unr[3].t",
    "cone_top.u.red[1]",
    "cone_top.u.red[2]",
    "cone_top.u.g_red[1].sh",
};

[[nodiscard]] RunCapture run(const std::filesystem::path& root,
    const SimulationEngine engine, const bool fused)
{
    auto project = build(root);
    if (fused) {
        const auto plan = project.design.plan_cone_fusion();
        require(!plan.disabled, "the fixture must be eligible for fusion");
        require(!plan.cones.empty() && !plan.dormant.empty()
                && plan.internal_nets != 0U,
            "the multiplier netlist must form a fused cone");
    }
    project.cone_fusion = fused;
    Simulation simulation(std::move(project), 100'000U, engine,
        SystemVerilogVpiRuntimeUpdates::omitted);
    RunCapture capture;
    std::string pending;
    simulation.set_output_hook(
        [&](fsim::runtime::simir::ProcessId, std::string_view text,
            const bool newline, fsim::runtime::SimulationTick,
            std::uint64_t) {
            pending += text;
            if (newline) {
                capture.lines.push_back(pending);
                pending.clear();
            }
        });
    const auto sample = [&](std::vector<std::string>& into) {
        for (const auto& path : internal_paths) {
            const auto signal = simulation.find_signal(path);
            require(signal.has_value(), "internal net must remain addressable");
            into.push_back(
                simulation.read_signal_snapshot(*signal).to_msb_string());
        }
    };
    // Stop inside the exhaustive loop, observe hidden nets, then continue.
    // Doubling stops keep this independent of the default time resolution.
    for (fsim::runtime::SimulationTick until = 1U;
         capture.lines.size() < 137U; until *= 2U) {
        (void)simulation.run(until);
        require(!simulation.finished(),
            "the mid-run observation point must precede $finish");
    }
    sample(capture.mid_internal);
    (void)simulation.run();
    sample(capture.final_internal);
    const auto product = simulation.find_signal("cone_top.p");
    require(product.has_value(), "the boundary output must be addressable");
    capture.final_output
        = simulation.read_signal_snapshot(*product).to_msb_string();
    return capture;
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-cone-fusion-test-"
            + std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    for (const auto engine :
        { SimulationEngine::interpreter, SimulationEngine::compiled }) {
        const auto reference = run(root / "reference", engine, false);
        const auto fused = run(root / "fused", engine, true);
        require(reference.lines.size() == 258U,
            "the reference transcript must cover every operand pair");
        require(fused.lines == reference.lines,
            "fused and unfused transcripts must be identical");
        require(fused.mid_internal == reference.mid_internal,
            "hidden nets must materialize their exact values mid-run");
        require(fused.final_internal == reference.final_internal,
            "hidden nets must materialize their exact values at the end");
        require(fused.final_output == reference.final_output,
            "the fused boundary output must match");
    }
    std::filesystem::remove_all(root);
    std::cout << "cone fusion tests passed\n";
    return 0;
}
