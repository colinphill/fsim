// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 behavioral tier: testbench processes run inside the static kernel
// as resumable threads and must produce the same transcript as the reference
// scheduler. The testbench mirrors the throughput fixtures: a delay-driven
// clock, a stimulus table built by an automatic task calling an automatic
// function, repeat/@(edge) waits, a fork/join of a driver loop and a checker
// loop, a level wait, formatted $display (including %t, %b, %h and a string),
// a timeout initial block and $finish.
#include "fsim/app/application.hpp"

#include "../../src/runtime/simir_static_kernel.hpp"

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
        std::cerr << "static kernel behavioral test failure: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::string_view source = R"(
module bk_dut(input clk, input rst_n, input [7:0] din, input vin,
              output reg [7:0] acc, output reg vout, output [7:0] mix);
  reg [7:0] mem [0:15];
  reg [3:0] wp;
  always @(posedge clk) begin
    if (!rst_n) begin
      acc <= 0; vout <= 0; wp <= 0;
    end else begin
      vout <= vin;
      if (vin) begin
        acc <= acc + din;
        mem[wp] <= din ^ acc;
        wp <= wp + 1;
      end
    end
  end
  assign mix = mem[wp - 4'd1] ^ acc;
endmodule

module bk_top;
  reg clk = 0;
  always #5 clk = ~clk;
  reg rst_n;
  reg [7:0] din;
  reg vin;
  wire [7:0] acc, mix;
  wire vout;
  bk_dut dut(.clk(clk), .rst_n(rst_n), .din(din), .vin(vin), .acc(acc),
             .vout(vout), .mix(mix));
  reg [7:0] table_q [0:31];
  integer seen, sum_out, k;
  reg done;

  function automatic [31:0] xorshift32(input [31:0] x);
    reg [31:0] y;
    begin
      y = x ^ (x << 13);
      y = y ^ (y >> 17);
      y = y ^ (y << 5);
      xorshift32 = y;
    end
  endfunction

  task automatic build(input [31:0] seed);
    integer i;
    reg [31:0] s;
    begin
      s = seed;
      for (i = 0; i < 32; i = i + 1) begin
        s = xorshift32(s);
        table_q[i] = s[7:0];
      end
    end
  endtask

  always @(posedge clk)
    if (vout) $display("%0t out acc=%h mix=%b", $time, acc, mix);

  initial begin
    rst_n = 0; din = 0; vin = 0; done = 0; seen = 0; sum_out = 0;
    build(32'h12345678);
    repeat (3) @(negedge clk);
    rst_n = 1;
    fork
      begin
        for (k = 0; k < 32; k = k + 1) begin
          @(negedge clk);
          din = table_q[k];
          vin = (k % 3) != 2;
        end
        @(negedge clk);
        vin = 0;
      end
      begin
        while (seen < 22) begin
          @(posedge clk);
          if (vout) begin
            seen = seen + 1;
            sum_out = sum_out + acc;
          end
        end
      end
    join
    wait (seen == 22);
    $display("DONE seen=%0d sum=%0d acc=%h tag=%s", seen, sum_out, acc, "ok");
    done = 1;
    repeat (2) @(posedge clk);
    $finish;
  end

  initial begin
    #100000;
    $display("TIMEOUT");
    $finish;
  end
endmodule
)";

[[nodiscard]] BuiltProject build(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto path = root / "static_kernel_behavioral.sv";
    {
        std::ofstream file(path);
        file << source;
        require(static_cast<bool>(file), "the source must be written");
    }
    fsim::project::Config config;
    config.project.name = "static-kernel-behavioral";
    config.project.top = "sv:work.bk_top";
    config.base_directory = root;
    config.build.optimization = fsim::project::Optimization::o2;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet sv;
    sv.language = fsim::project::Language::system_verilog;
    sv.standard = "2017";
    sv.library = "work";
    sv.files.push_back(path);
    config.source_sets.push_back(std::move(sv));
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

[[nodiscard]] std::vector<std::string> run(const std::filesystem::path& root,
    const SimulationEngine engine, const bool kernel)
{
    auto project = build(root);
    if (kernel) {
        ::setenv("FSIM_STATIC_KERNEL_BEHAVIORAL", "1", 1);
        const auto plan = project.design.plan_static_kernel();
        if (plan.disabled) {
            std::cerr << "kernel disabled: " << plan.disabled_reason << '\n';
        }
        require(!plan.disabled, "the fixture must be eligible for the kernel");
        std::size_t behavioral = 0U;
        for (const auto& member : plan.spec->members) {
            behavioral += member.kind
                    == fsim::runtime::simir::StaticKernelMemberKind::behavioral
                ? 1U : 0U;
        }
        require(behavioral >= 4U,
            "the testbench processes must be behavioral kernel members");
    }
    project.cone_fusion = kernel;
    ::setenv("FSIM_STATIC_KERNEL", kernel ? "1" : "0", 1);
    Simulation simulation(std::move(project), 100'000U, engine,
        SystemVerilogVpiRuntimeUpdates::omitted);
    std::vector<std::string> lines;
    std::string pending;
    simulation.set_output_hook(
        [&](fsim::runtime::simir::ProcessId, std::string_view text,
            const bool newline, fsim::runtime::SimulationTick,
            std::uint64_t) {
            pending += text;
            if (newline) {
                lines.push_back(pending);
                pending.clear();
            }
        });
    (void)simulation.run();
    require(simulation.finished(), "the testbench must reach $finish");
    ::unsetenv("FSIM_STATIC_KERNEL_BEHAVIORAL");
    return lines;
}

} // namespace

int main()
{
    const auto root = std::filesystem::temp_directory_path()
        / ("fsim-static-kernel-behavioral-test-"
            + std::to_string(std::chrono::steady_clock::now()
                    .time_since_epoch().count()));
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    for (const auto engine :
        { SimulationEngine::interpreter, SimulationEngine::compiled }) {
        const auto reference = run(root / "reference", engine, false);
        const auto kernel = run(root / "kernel", engine, true);
        bool done = false;
        for (const auto& line : reference) {
            done = done || line.starts_with("DONE ");
        }
        require(done, "the reference testbench must complete its checks");
        require(reference.size() > 20U,
            "the reference transcript must cover the checker output");
        if (kernel != reference) {
            for (std::size_t index = 0U;
                 index < reference.size() || index < kernel.size(); ++index) {
                const auto left = index < reference.size() ? reference[index]
                                                           : "<none>";
                const auto right = index < kernel.size() ? kernel[index]
                                                         : "<none>";
                if (left != right) {
                    std::cerr << "line " << index << "\nreference: " << left
                              << "\nkernel:    " << right << '\n';
                    break;
                }
            }
            std::cerr << "reference lines=" << reference.size()
                      << " kernel lines=" << kernel.size() << '\n';
        }
        require(kernel == reference,
            "kernel and reference transcripts must be identical");
    }
    std::filesystem::remove_all(root);
    std::cout << "static kernel behavioral tests passed\n";
    return 0;
}
