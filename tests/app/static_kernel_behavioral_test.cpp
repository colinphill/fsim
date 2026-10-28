// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 behavioral tier: testbench processes run inside the static kernel
// as resumable threads and must produce the same transcript as the reference
// scheduler. The testbench mirrors the throughput fixtures: a delay-driven
// clock, a stimulus table built by an automatic task calling an automatic
// function, repeat/@(edge) waits, a fork/join of a driver loop and a checker
// loop, a level wait, formatted $display (including %t, %b, %h and a string),
// a timeout initial block and $finish. The clocked $display blocks are
// sequential members; a forced, shuffled multithreaded run logs their output
// on workers and must print it in the serial order, with a %t argument
// formatted after the $timeformat that precedes it. A third process disables
// a sibling fork branch's block and its own forever loop (IEEE 1800-2023
// 9.6.2), draws $urandom in a fork child (each thread its own stream, 18.14)
// and computes with reals ($rtoi, $ln, real arithmetic): it stays a kernel
// member too. A fourth runs one automatic task with delays in two fork
// branches at once (each activation its own locals, 13.4.2) and calls a task
// whose join_none child outlives its activation and still updates the
// activation's locals (6.21, 9.3.2): per-thread callable contexts keep it in
// the kernel.
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

// Sets, or with a null value unsets, a variable of this process.
void set_environment(const char* const name, const char* const value)
{
#if defined(_WIN32)
    (void)::_putenv_s(name, value == nullptr ? "" : value);
#else
    (void)(value == nullptr ? ::unsetenv(name) : ::setenv(name, value, 1));
#endif
}

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
  // $readmemh in the kernel (IEEE 1800-2023 21.4): a whole load read by
  // combinational logic, and a ranged load.
  reg [7:0] rom [0:7];
  reg [7:0] rom_part [0:7];
  initial $readmemh("bk_rom.hex", rom);
  initial $readmemh("bk_rom.hex", rom_part, 2, 5);
  wire [7:0] rom_pick = rom[din[2:0]];
  always @(posedge clk)
    if (vout) $display("%0t rom=%h", $time, rom_pick);

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

  reg [63:0] stamp = 0;
  always @(posedge clk) stamp <= stamp + 64'd10;
  always @(posedge clk)
    if (vout) $display("tf %t din=%h", stamp, din);
  always @(negedge clk)
    if (seen == 8) $timeformat(0, 2, " s", 14);

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
    $display("ROM part=%h %h %h %h", rom_part[1], rom_part[2], rom_part[5], rom_part[6]);
    $display("DONE seen=%0d sum=%0d acc=%h tag=%s", seen, sum_out, acc, "ok");
    done = 1;
    repeat (2) @(posedge clk);
    $finish;
  end

  integer dcount;
  real r;
  initial begin
    dcount = 0; r = 1.0;
    #3;
    fork
      begin : watch
        repeat (4) @(posedge clk);
        $display("%0t watch timeout", $time);
      end
      begin
        repeat (2) @(posedge clk);
        disable watch;
        r = r * 2.5 + 0.25;
        $display("%0t disabled watch r=%0d u=%0d", $time, $rtoi(r * 100.0),
                 $urandom % 1000);
      end
    join
    begin : loop
      forever begin
        @(posedge clk);
        dcount = dcount + 1 + ($urandom % 2);
        if (dcount > 5) disable loop;
      end
    end
    r = $ln(r + 1.0);
    $display("%0t loop done dcount=%0d lr=%0d", $time, dcount, $rtoi(r * 1000.0));
  end

  task automatic slow(input integer id, input integer n, output integer res);
    integer i, t;
    begin
      t = id * 100;
      for (i = 0; i < n; i = i + 1) begin
        #(3 + id);
        t = t + i + id + table_q[i];
      end
      res = t;
    end
  endtask

  task automatic tally(input integer base);
    integer a;
    begin
      a = base;
      fork
        begin
          #7;
          a = a + 1;
          $display("%0t tally child a=%0d", $time, a);
        end
      join_none
      #2;
      a = a + 10;
      $display("%0t tally parent a=%0d", $time, a);
    end
  endtask

  // A task that cannot suspend may keep an automatic array: its frame lives
  // within one run of its thread.
  task automatic blend(output integer m);
    integer i;
    reg [7:0] w [0:3];
    begin
      for (i = 0; i < 4; i = i + 1) w[i] = i * 7 + 1;
      m = w[0] + w[1] * w[2] - w[3];
    end
  endtask

  integer ra, rb, rm;
  initial begin
    blend(rm);
    #1;
    fork
      slow(1, 4, ra);
      slow(2, 3, rb);
    join
    $display("%0t slow ra=%0d rb=%0d rm=%0d", $time, ra, rb, rm);
    tally(5);
    tally(20);
    #20;
    $display("%0t tallies done", $time);
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
    {
        std::ofstream file(root / "bk_rom.hex");
        file << "3c a5 // words\n5a 0f\n@4 e1 7e 99 c3\n";
        require(static_cast<bool>(file), "the memory file must be written");
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
    const SimulationEngine engine, const bool kernel, const bool threads = false)
{
    auto project = build(root);
    if (kernel) {
        set_environment("FSIM_STATIC_KERNEL_BEHAVIORAL", "1");
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
        require(behavioral >= 6U,
            "the testbench processes must be behavioral kernel members");
        std::size_t sequential = 0U;
        for (const auto& member : plan.spec->members) {
            sequential += member.kind
                    == fsim::runtime::simir::StaticKernelMemberKind::sequential
                ? 1U : 0U;
        }
        require(sequential >= 5U,
            "the clocked $display blocks must be sequential kernel members");
        for (const auto& row : plan.hosted_reasons) {
            require(row.reason.find("LoadMemory") == std::string::npos
                    && row.reason.find("container_unowned") == std::string::npos,
                "$readmemh and the memories it loads must stay in the kernel");
            require(row.reason.find("DisableBlock") == std::string::npos
                    && row.reason.find("fork_random") == std::string::npos
                    && row.reason.find("SystemVerilogMath") == std::string::npos
                    && row.reason.find("SystemVerilogScalarBinary")
                        == std::string::npos
                    && row.reason.find("fork_callable_frames")
                        == std::string::npos,
                "disable, fork-child $urandom, real arithmetic and automatic "
                "tasks in fork branches must stay in the kernel: " + row.reason);
        }
    }
    project.cone_fusion = kernel;
    set_environment("FSIM_STATIC_KERNEL", kernel ? "1" : "0");
    set_environment("FSIM_KERNEL_THREADS", threads ? "4" : nullptr);
    set_environment("FSIM_KERNEL_MT_FORCE", threads ? "1" : nullptr);
    set_environment("FSIM_KERNEL_MT_SHUFFLE", threads ? "1" : nullptr);
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
    set_environment("FSIM_STATIC_KERNEL_BEHAVIORAL", nullptr);
    set_environment("FSIM_KERNEL_THREADS", nullptr);
    set_environment("FSIM_KERNEL_MT_FORCE", nullptr);
    set_environment("FSIM_KERNEL_MT_SHUFFLE", nullptr);
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
        const auto threaded = run(root / "threaded", engine, true, true);
        require(threaded == kernel,
            "a multithreaded kernel must print the serial kernel's transcript");
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
