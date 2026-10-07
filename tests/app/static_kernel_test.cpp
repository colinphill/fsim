// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static kernel: a design mixing a combinational netlist with
// constant operands, edge-triggered registers with asynchronous reset, a
// derived clock, a memory with an initializer loop and registered read,
// dynamic bit and part-select nonblocking writes, a net array with a dynamic
// read, and a combinational case block must produce the same transcript in
// the kernel as in the reference scheduler. Hidden kernel-owned signals must
// read back their exact values mid-run and at the end.
#include "fsim/app/application.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using fsim::app::BuiltProject;
using fsim::app::Simulation;
using fsim::app::SimulationEngine;
using fsim::app::SystemVerilogVpiRuntimeUpdates;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "static kernel test failure: " << message << '\n';
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

module regfile(input wire clk, input wire we, input wire [3:0] wa,
               input wire [7:0] wd, input wire [3:0] ra, output reg [7:0] rd);
  reg [7:0] mem [0:15];
  integer i;
  initial for (i = 0; i < 16; i = i + 1) mem[i] = i * 3;
  always @(posedge clk) begin
    if (we) mem[wa] <= wd;
    rd <= mem[ra];
  end
endmodule

module sk_dut(input wire clk, input wire rst_n, input wire [3:0] a,
              input wire [3:0] b, input wire we, input wire [3:0] wa,
              input wire [3:0] ra, output wire [3:0] prod,
              output wire [7:0] rd, output reg [7:0] acc,
              output wire [7:0] mix, output reg [3:0] slow,
              output reg [3:0] sel);
  mul4 m(.a(a), .b(b), .p(prod));
  wire [3:0] pc;
  mul4 mc(.a(acc[3:0]), .b(4'b0110), .p(pc));
  regfile rf(.clk(clk), .we(we), .wa(wa), .wd(acc), .ra(ra), .rd(rd));
  reg div;
  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      acc <= 8'h00;
      div <= 1'b0;
    end else begin
      acc <= (acc + {4'b0, prod}) ^ {pc, 4'h0};
      div <= ~div;
    end
  always @(posedge div or negedge rst_n)
    if (!rst_n) slow <= 4'h0;
    else slow <= slow + 1'b1;
  reg [7:0] shreg;
  always @(posedge clk) begin
    shreg[a[2:0]] <= b[0];
    shreg[4 +: 4] <= shreg[0 +: 4] ^ b;
  end
  wire [7:0] arr [0:2];
  assign arr[0] = acc;
  assign arr[1] = arr[0] ^ {rd[3:0], rd[7:4]};
  assign arr[2] = arr[1] + shreg;
  assign mix = arr[ra[1:0] == 2'b11 ? 2'd2 : ra[1:0]];
  always @(*)
    case (a[1:0])
      2'b00: sel = b;
      2'b01: sel = ~b;
      2'b10: sel = prod;
      default: sel = slow;
    endcase
endmodule

module sk_top;
  reg clk = 0;
  always #5 clk = ~clk;
  reg rst_n, we;
  reg [3:0] a, b, wa, ra;
  wire [3:0] prod, slow, sel;
  wire [7:0] rd, acc, mix;
  sk_dut dut(.clk(clk), .rst_n(rst_n), .a(a), .b(b), .we(we), .wa(wa),
             .ra(ra), .prod(prod), .rd(rd), .acc(acc), .mix(mix),
             .slow(slow), .sel(sel));
  integer cyc;
  initial begin
    rst_n = 0; we = 0; a = 0; b = 0; wa = 0; ra = 0;
    repeat (2) @(negedge clk);
    rst_n = 1;
    for (cyc = 0; cyc < 120; cyc = cyc + 1) begin
      @(negedge clk);
      a = cyc[3:0] ^ cyc[7:4];
      b = (cyc * 7) & 15;
      we = cyc[0];
      wa = cyc[5:2];
      ra = (cyc * 5) & 15;
      if (cyc == 90) b = 4'bx01x;
      if (cyc == 100) begin
        rst_n = 0;
        @(negedge clk);
        rst_n = 1;
      end
    end
    $finish;
  end
  always @(posedge clk)
    $display("%0t p=%b rd=%h acc=%h mix=%h slow=%h sel=%h", $time, prod, rd,
             acc, mix, slow, sel);
endmodule
)";

[[nodiscard]] BuiltProject build(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto path = root / "static_kernel.sv";
    {
        std::ofstream file(path);
        file << source;
        require(static_cast<bool>(file), "the fixture source must be written");
    }
    fsim::project::Config config;
    config.project.name = "static-kernel";
    config.project.top = "sv:work.sk_top";
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
    std::vector<std::string> mid_internal;
    std::vector<std::string> final_internal;
};

const std::vector<std::string> internal_paths {
    "sk_top.dut.m.unr",
    "sk_top.dut.mc.red[2]",
    "sk_top.dut.pc",
    "sk_top.dut.div",
    "sk_top.dut.shreg",
    "sk_top.dut.arr[1]",
    "sk_top.dut.arr[2]",
};

[[nodiscard]] RunCapture run(const std::filesystem::path& root,
    const SimulationEngine engine, const bool kernel)
{
    auto project = build(root);
    if (kernel) {
        const auto plan = project.design.plan_static_kernel();
        require(!plan.disabled, "the fixture must be eligible for the kernel");
        // Constant drivers merge into one process per instance.
        require(plan.members > 60U && plan.owned_signals > 20U
                && plan.owned_containers != 0U,
            "the kernel must own the DUT, its memory and its arrays");
    }
    project.cone_fusion = kernel;
    ::setenv("FSIM_STATIC_KERNEL", kernel ? "1" : "0", 1);
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
    for (fsim::runtime::SimulationTick until = 1U;
         capture.lines.size() < 70U; until *= 2U) {
        (void)simulation.run(until);
        require(!simulation.finished(),
            "the mid-run observation point must precede $finish");
    }
    sample(capture.mid_internal);
    (void)simulation.run();
    sample(capture.final_internal);
    return capture;
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-static-kernel-test-"
            + std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    for (const auto engine :
        { SimulationEngine::interpreter, SimulationEngine::compiled }) {
        const auto reference = run(root / "reference", engine, false);
        const auto kernel = run(root / "kernel", engine, true);
        require(reference.lines.size() > 120U,
            "the reference transcript must cover every cycle");
        if (kernel.lines != reference.lines) {
            for (std::size_t index = 0U; index < reference.lines.size()
                 && index < kernel.lines.size(); ++index) {
                if (kernel.lines[index] != reference.lines[index]) {
                    std::cerr << "reference: " << reference.lines[index]
                              << "\nkernel:    " << kernel.lines[index] << '\n';
                    break;
                }
            }
            std::cerr << "reference lines=" << reference.lines.size()
                      << " kernel lines=" << kernel.lines.size() << '\n';
        }
        require(kernel.lines == reference.lines,
            "kernel and reference transcripts must be identical");
        for (std::size_t index = 0U; index < internal_paths.size(); ++index) {
            if (kernel.mid_internal[index] != reference.mid_internal[index]
                || kernel.final_internal[index]
                    != reference.final_internal[index]) {
                std::cerr << internal_paths[index] << " reference mid="
                          << reference.mid_internal[index] << " final="
                          << reference.final_internal[index] << " kernel mid="
                          << kernel.mid_internal[index] << " final="
                          << kernel.final_internal[index] << '\n';
            }
        }
        require(kernel.mid_internal == reference.mid_internal,
            "hidden signals must materialize their exact values mid-run");
        require(kernel.final_internal == reference.final_internal,
            "hidden signals must materialize their exact values at the end");
    }
    std::filesystem::remove_all(root);
    std::cout << "static kernel tests passed\n";
    return 0;
}
