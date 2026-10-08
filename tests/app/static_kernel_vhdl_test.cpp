// SPDX-License-Identifier: Apache-2.0
//
// Engine v4 static kernel, VHDL delta mode: a VHDL design driven by a
// SystemVerilog testbench must produce the same transcript in the kernel as
// in the reference scheduler, and hidden kernel-owned VHDL signals must read
// back their exact values mid-run and at the end. The design covers package
// functions with loops (calls, automatic frames, integer checks), expression
// port actuals (port adapter processes), registers clocked through a
// one-delta-delayed clock (one samples a same-delta update, one samples
// through an adapter), a derived clock, a shared-variable RAM, process
// variables with initializers, weak Logic9 values, resolved vectors with
// disjoint concurrent drivers, dynamic slice assignments, an X injection, and
// a one-delta glitch that the testbench counts and feeds back into the design
// (each kernel round must be its own host delta).
#include "fsim/app/application.hpp"
#include "fsim/compiler/static_kernel_codegen.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
        std::cerr << "static kernel VHDL test failure: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::string_view vhdl_source = R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package vk_pkg is
  function gf_mul(a : std_logic_vector; b : std_logic_vector; M : integer;
                  POLY : integer) return std_logic_vector;
  function popcount(v : std_logic_vector; W : integer) return integer;
end package vk_pkg;

package body vk_pkg is
  function gf_mul(a : std_logic_vector; b : std_logic_vector; M : integer;
                  POLY : integer) return std_logic_vector is
    variable au : std_logic_vector(M - 1 downto 0) := a;
    variable bu : std_logic_vector(M - 1 downto 0) := b;
    variable unred : std_logic_vector(2 * M - 2 downto 0) := (others => '0');
    variable red : std_logic_vector(2 * M - 2 downto 0);
    variable t : std_logic;
    variable prim_low : std_logic_vector(M - 1 downto 0) :=
      std_logic_vector(to_unsigned(POLY mod (2 ** M), M));
    variable prim_sh : std_logic_vector(2 * M - 2 downto 0);
    variable hi, sh : integer;
  begin
    for k in 0 to 2 * M - 2 loop
      t := '0';
      for ii in 0 to M - 1 loop
        if ii <= k and (k - ii) < M then
          t := t xor (au(ii) and bu(k - ii));
        end if;
      end loop;
      unred(k) := t;
    end loop;
    red := unred;
    for i in 0 to M - 2 loop
      hi := 2 * M - 2 - i;
      sh := hi - M;
      prim_sh := (others => '0');
      for j in 0 to M - 1 loop
        prim_sh(j + sh) := prim_low(j);
      end loop;
      if red(hi) = '1' then
        red := red xor prim_sh;
      end if;
    end loop;
    return red(M - 1 downto 0);
  end function;

  function popcount(v : std_logic_vector; W : integer) return integer is
    variable vv : std_logic_vector(W - 1 downto 0) := v;
    variable n : integer := 0;
  begin
    for i in 0 to W - 1 loop
      if vv(i) = '1' then
        n := n + 1;
      end if;
    end loop;
    return n;
  end function;
end package body vk_pkg;

library ieee;
use ieee.std_logic_1164.all;
use work.vk_pkg.all;

entity vk_mult is
  port (a : in std_logic_vector(3 downto 0);
        b : in std_logic_vector(3 downto 0);
        p : out std_logic_vector(3 downto 0));
end entity vk_mult;

architecture rtl of vk_mult is
begin
  p <= gf_mul(a, b, 4, 19);
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;

entity vk_sub is
  port (clk : in std_logic;
        d : in std_logic_vector(3 downto 0);
        q : out std_logic_vector(3 downto 0));
end entity vk_sub;

architecture rtl of vk_sub is
begin
  process(clk)
  begin
    if rising_edge(clk) then
      q <= d;
    end if;
  end process;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vk_ram is
  port (clk : in std_logic;
        we : in std_logic;
        addr : in std_logic_vector(3 downto 0);
        wd : in std_logic_vector(7 downto 0);
        rd : out std_logic_vector(7 downto 0));
end entity vk_ram;

architecture rtl of vk_ram is
  type mem_t is array (0 to 15) of std_logic_vector(7 downto 0);
  shared variable mem : mem_t;
begin
  process(clk)
  begin
    if rising_edge(clk) then
      rd <= mem(to_integer(unsigned(addr)));
      if we = '1' then
        mem(to_integer(unsigned(addr))) := wd;
      end if;
    end if;
  end process;
end architecture rtl;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.vk_pkg.all;

entity vk_dut is
  port (clk : in std_logic;
        rst_n : in std_logic;
        a : in std_logic_vector(3 downto 0);
        b : in std_logic_vector(3 downto 0);
        we : in std_logic;
        addr : in std_logic_vector(3 downto 0);
        prod : out std_logic_vector(3 downto 0);
        acc : out std_logic_vector(7 downto 0);
        rd : out std_logic_vector(7 downto 0);
        slow : out std_logic_vector(3 downto 0);
        mix : out std_logic_vector(7 downto 0);
        flags : out std_logic_vector(3 downto 0);
        cnt : out std_logic_vector(3 downto 0);
        late : out std_logic_vector(3 downto 0);
        late2 : out std_logic_vector(3 downto 0);
        pulse : out std_logic;
        fb : in std_logic;
        fbc : out std_logic_vector(3 downto 0));
end entity vk_dut;

architecture rtl of vk_dut is
  type lane_t is array (0 to 3) of std_logic_vector(3 downto 0);
  signal s : lane_t;
  signal s_mult : lane_t;
  signal acc_r : std_logic_vector(7 downto 0);
  signal prod_i : std_logic_vector(3 downto 0);
  signal pc : std_logic_vector(3 downto 0);
  signal div : std_logic := '0';
  signal slow_r : unsigned(3 downto 0) := (others => '0');
  signal clk_d : std_logic;
  signal late_i : std_logic_vector(3 downto 0);
  signal weak : std_logic;
  signal nib : std_logic_vector(3 downto 0);
  signal g1, g2 : std_logic := '0';
  signal fb_r : unsigned(3 downto 0) := (others => '0');
begin
  m : entity work.vk_mult port map (a => a, b => b, p => prod_i);

  -- A change of a(0) makes pulse rise and fall in consecutive deltas.
  g1 <= a(0);
  g2 <= g1;
  pulse <= g1 xor g2;
  process(fb)
  begin
    if rising_edge(fb) then
      fb_r <= fb_r + 1;
    end if;
  end process;
  fbc <= std_logic_vector(fb_r);
  mc : entity work.vk_mult
    port map (a => acc_r(3 downto 0), b => "0110", p => pc);
  g_lane : for i in 0 to 3 generate
    u : entity work.vk_mult
      port map (a => s(i), b => std_logic_vector(to_unsigned(i + 2, 4)),
                p => s_mult(i));
  end generate;
  ram : entity work.vk_ram
    port map (clk => clk, we => we, addr => addr, wd => acc_r, rd => rd);

  -- One delta behind clk: vk_sub samples acc_r after this edge's update.
  clk_d <= clk;
  sub : entity work.vk_sub
    port map (clk => clk_d, d => acc_r(7 downto 4), q => late_i);
  -- A plain actual adds no delta: this register sees nib's new value.
  sub2 : entity work.vk_sub port map (clk => clk_d, d => nib, q => late2);

  process(clk)
  begin
    if rising_edge(clk) then
      if rst_n = '0' then
        acc_r <= (others => '0');
        for i in 0 to 3 loop
          s(i) <= (others => '0');
        end loop;
        div <= '0';
      else
        acc_r <= std_logic_vector(unsigned(acc_r) + resize(unsigned(prod_i), 8))
          xor (pc & "0000");
        for i in 0 to 3 loop
          if i = to_integer(unsigned(a(1 downto 0))) then
            s(i) <= s_mult(i) xor b;
          end if;
        end loop;
        div <= not div;
        nib <= s_mult(0) xor a;
      end if;
    end if;
  end process;

  process(div)
  begin
    if rising_edge(div) then
      slow_r <= slow_r + unsigned(acc_r(3 downto 0));
    end if;
  end process;

  process(acc_r, s)
    variable v : unsigned(7 downto 0) := (others => '0');
    variable n : integer;
  begin
    v := unsigned(acc_r);
    for i in 0 to 3 loop
      v := v xor unsigned(s(i) & s(3 - i));
    end loop;
    n := popcount(std_logic_vector(v), 8);
    mix <= std_logic_vector(v);
    cnt <= std_logic_vector(to_unsigned(n, 4));
  end process;

  weak <= 'H' when acc_r(0) = '1' else 'L';
  flags(0) <= '1' when weak = '1' else '0';
  flags(1) <= to_x01(weak);
  flags(2) <= late_i(0) xor late_i(3);
  flags(3) <= '1' when popcount(acc_r, 8) > 3 else '0';

  assert rst_n /= '1' or unsigned(acc_r) /= 255
    report "accumulator saturated" severity note;

  prod <= prod_i;
  acc <= acc_r;
  slow <= std_logic_vector(slow_r);
  late <= late_i;
end architecture rtl;
)";

constexpr std::string_view sv_source = R"(
module vk_top;
  reg clk = 0;
  always #5 clk = ~clk;
  reg rst_n, we;
  reg [3:0] a, b, addr;
  wire [3:0] prod, slow, flags, cnt, late, late2, fbc;
  wire pulse;
  integer pulses = 0;
  always @(pulse) pulses = pulses + 1;
  wire [7:0] acc, rd, mix;
  vk_dut dut(.clk(clk), .rst_n(rst_n), .a(a), .b(b), .we(we), .addr(addr),
             .prod(prod), .acc(acc), .rd(rd), .slow(slow), .mix(mix),
             .flags(flags), .cnt(cnt), .late(late), .late2(late2),
             .pulse(pulse), .fb(pulse), .fbc(fbc));
  integer cyc;
  initial begin
    rst_n = 0; we = 0; a = 0; b = 0; addr = 0;
    repeat (3) @(negedge clk);
    rst_n = 1;
    for (cyc = 0; cyc < 120; cyc = cyc + 1) begin
      @(negedge clk);
      a = cyc[3:0] ^ cyc[7:4];
      b = (cyc * 7) & 15;
      we = cyc[0];
      addr = (cyc * 5) & 15;
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
    $display("%0t p=%b acc=%h rd=%h slow=%h mix=%h flags=%b cnt=%h late=%h/%h pulses=%0d fb=%h",
             $time, prod, acc, rd, slow, mix, flags, cnt, late, late2, pulses, fbc);
endmodule
)";

[[nodiscard]] BuiltProject build(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto vhdl_path = root / "static_kernel_vhdl.vhd";
    const auto sv_path = root / "static_kernel_vhdl_tb.sv";
    {
        std::ofstream file(vhdl_path);
        file << vhdl_source;
        require(static_cast<bool>(file), "the VHDL source must be written");
    }
    {
        std::ofstream file(sv_path);
        file << sv_source;
        require(static_cast<bool>(file), "the testbench must be written");
    }
    fsim::project::Config config;
    config.project.name = "static-kernel-vhdl";
    config.project.top = "sv:work.vk_top";
    config.base_directory = root;
    config.build.optimization = fsim::project::Optimization::o2;
    config.build.cache_path = root / "cache";
    config.run.max_deltas = 100'000U;
    config.run.trace_enabled = false;
    fsim::project::SourceSet vhdl;
    vhdl.language = fsim::project::Language::vhdl;
    vhdl.standard = "2008";
    vhdl.vhdl_compatibility = "legacy-unprotected-shared-variable";
    vhdl.library = "work";
    vhdl.files.push_back(vhdl_path);
    config.source_sets.push_back(std::move(vhdl));
    fsim::project::SourceSet sv;
    sv.language = fsim::project::Language::system_verilog;
    sv.standard = "2017";
    sv.library = "work";
    sv.files.push_back(sv_path);
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

struct RunCapture {
    std::vector<std::string> lines;
    std::vector<std::string> mid_internal;
    std::vector<std::string> final_internal;
};

const std::vector<std::string> internal_paths {
    "vk_top.dut.s",
    "vk_top.dut.s_mult",
    "vk_top.dut.pc",
    "vk_top.dut.div",
    "vk_top.dut.clk_d",
    "vk_top.dut.weak",
    "vk_top.dut.slow_r",
};

/// With a cache, the kernel's code is built ahead of time into it
/// (`ahead_of_time`, as elaborate --aot does) or loaded from it.
[[nodiscard]] RunCapture run(const std::filesystem::path& root,
    const SimulationEngine engine, const bool kernel,
    const std::filesystem::path& cache = { }, const bool ahead_of_time = false)
{
    auto project = build(root);
    project.cache_path = cache;
    if (kernel) {
        const auto plan = project.design.plan_static_kernel();
        if (plan.disabled) {
            std::cerr << "kernel disabled: " << plan.disabled_reason << '\n';
        }
        require(!plan.disabled, "the fixture must be eligible for the kernel");
        require(plan.members > 20U && plan.owned_signals > 20U,
            "the kernel must own the VHDL design");
    }
    project.cone_fusion = kernel;
    ::setenv("FSIM_STATIC_KERNEL", kernel ? "1" : "0", 1);
    std::optional<fsim::compiler::StaticKernelAheadOfTimeScope> aot;
    if (ahead_of_time) {
        aot.emplace();
    }
    Simulation simulation(std::move(project), 100'000U, engine,
        SystemVerilogVpiRuntimeUpdates::omitted);
    aot.reset();
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
            if (!signal) {
                std::cerr << "missing internal signal " << path << '\n';
            }
            require(signal.has_value(), "internal signal must remain addressable");
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
        / ("fsim-static-kernel-vhdl-test-"
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
        if (engine == SimulationEngine::compiled) {
            // Code built ahead of time, then loaded instead of compiled.
            const auto cache = root / "cache";
            const auto built = run(root / "aot", engine, true, cache, true);
            require(std::filesystem::exists(cache / "static-kernel")
                    && !std::filesystem::is_empty(cache / "static-kernel"),
                "an ahead-of-time build must store the kernel's code");
            const auto loaded = run(root / "loaded", engine, true, cache, false);
            for (const auto* capture : { &built, &loaded }) {
                require(capture->lines == reference.lines,
                    "kernel code built ahead of time must match the reference");
                require(capture->mid_internal == reference.mid_internal
                        && capture->final_internal == reference.final_internal,
                    "kernel code built ahead of time must keep hidden values");
            }
        }
    }
    std::filesystem::remove_all(root);
    std::cout << "static kernel VHDL tests passed\n";
    return 0;
}
