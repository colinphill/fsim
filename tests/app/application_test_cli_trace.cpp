// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "fsim/app/built_project.hpp"
#include "../api/api_workspace_support.hpp"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
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
    // A dump holds the design's variables only (IEEE 1800-2023 21.7);
    // no runtime-internal scope is written.
    assert(scaled_vcd.find("$scope module __fsim $end") == std::string::npos
        && scaled_vcd.find("$scope module activity $end")
            == std::string::npos);

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

    test_cli_trace_vhdl_hierarchy();
}

namespace {

    [[nodiscard]] std::string read_text(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        return { std::istreambuf_iterator<char> { stream },
            std::istreambuf_iterator<char> { } };
    }

    // Every `$var` is inside a `$scope` (IEEE 1800-2023 21.7.2.2), and the
    // declarations name the hierarchical objects once each.
    [[nodiscard]] std::vector<std::string> vcd_declarations(const std::string& vcd)
    {
        std::vector<std::string> names;
        std::vector<std::string> scopes;
        std::istringstream lines(vcd);
        std::string word;
        while (lines >> word) {
            if (word == "$enddefinitions") {
                break;
            }
            if (word == "$scope") {
                std::string kind;
                std::string name;
                lines >> kind >> name;
                scopes.push_back(name);
            } else if (word == "$upscope") {
                assert(!scopes.empty());
                scopes.pop_back();
            } else if (word == "$var") {
                std::string kind;
                std::string width;
                std::string code;
                std::string name;
                lines >> kind >> width >> code >> name;
                assert(!scopes.empty());
                std::string path;
                for (const auto& scope : scopes) {
                    path += scope + ".";
                }
                names.push_back(path + name + "=" + code);
            }
        }
        return names;
    }

    // The value changes of `code` after the definitions, as "time:value".
    [[nodiscard]] std::vector<std::string> vcd_changes(
        const std::string& vcd, const std::string& code)
    {
        std::vector<std::string> changes;
        std::istringstream lines(vcd.substr(vcd.find("$enddefinitions")));
        std::string line;
        std::string time;
        while (std::getline(lines, line)) {
            if (line.starts_with("#")) {
                time = line.substr(1U);
            } else if (line.size() == 1U + code.size() && line.substr(1U) == code) {
                changes.push_back(time + ":" + line.substr(0U, 1U));
            }
        }
        return changes;
    }

    [[nodiscard]] std::string declared_code(
        const std::vector<std::string>& declarations, const std::string& path)
    {
        const auto found = std::ranges::find_if(declarations,
            [&](const std::string& entry) { return entry.starts_with(path + "="); });
        assert(found != declarations.end());
        return found->substr(path.size() + 1U);
    }

} // namespace

void ApplicationTestFixture::test_cli_trace_vhdl_hierarchy()
{
    namespace workspace = fsim::tests::api_workspace;
    // A traced VHDL process: its first run is its initialization (IEEE
    // 1076-2019 14.7.5.2), so the clocked process assigns at the first
    // rising edge only; the trace starts with the signals' initial values.
    const auto clocked_workspace = directory / "vhdl-clocked-trace";
    std::filesystem::create_directories(clocked_workspace);
    {
        std::ofstream output(clocked_workspace / "clocked.vhd");
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity clocked is
end entity;
architecture test of clocked is
  signal clk : std_logic := '0';
  signal resetn : std_logic := '0';
  signal done : std_logic;
  signal state : std_logic_vector(2 downto 0);
begin
  clk <= not clk after 5 ns when now < 40 ns;
  resetn <= '1' after 12 ns;
  process (clk)
    variable count : unsigned(7 downto 0);
    variable seen : boolean;
  begin
    if rising_edge(clk) then
      if resetn = '0' then
        state <= "000";
        done <= '0';
      else
        done <= '0';
        count := count + 1;
        seen := count > 2;
        case state is
          when "000" => state <= "001";
          when "001" => if seen then state <= "100"; end if;
          when others => done <= '1';
        end case;
      end if;
    end if;
  end process;
end architecture;
)";
    }
    workspace::prepare(clocked_workspace,
        "fsim::config set elaborate.time_resolution 1ns\n",
        { "clocked.vhd" }, "vhdl:work.clocked");
    const auto clocked = workspace::run_streams(clocked_workspace,
        { "simulate", "--trace-output", "clocked.vcd", "--trace-format", "vcd" });
    assert(clocked.status == 0);
    const auto clocked_vcd = read_text(clocked_workspace / "clocked.vcd");
    const auto clocked_names = vcd_declarations(clocked_vcd);
    assert(clocked_names.size() == 4U);
    const auto done = declared_code(clocked_names, "clocked.done");
    assert((vcd_changes(clocked_vcd, done)
        == std::vector<std::string> { "0:x", "5:0" }));

    // A VHDL instance under a SystemVerilog root is declared once, in the
    // root's scope (no root-relative lookup name is a scope).
    const auto mixed_workspace = directory / "vhdl-under-sv-trace";
    std::filesystem::create_directories(mixed_workspace);
    {
        std::ofstream output(mixed_workspace / "child.vhd");
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
entity child is
  port (a : in std_logic; y : out std_logic);
end entity;
architecture rtl of child is
  signal inner : std_logic;
begin
  inner <= not a;
  y <= inner;
end architecture;
)";
    }
    {
        std::ofstream output(mixed_workspace / "top.sv");
        output << R"(
module top;
  logic a = 0;
  logic y;
  child u (.a(a), .y(y));
  initial begin
    #5 a = 1;
    #5 $finish;
  end
endmodule
)";
    }
    workspace::isolate_user_configuration(mixed_workspace / "user-configuration");
    workspace::write_configuration(mixed_workspace,
        "fsim::config set elaborate.time_resolution 1ns\n");
    workspace::expect(mixed_workspace, { "compile", "child.vhd" });
    workspace::expect(mixed_workspace, { "compile", "top.sv" });
    workspace::expect(mixed_workspace, { "elaborate", "sv:work.top" });
    const auto mixed = workspace::run_streams(mixed_workspace,
        { "simulate", "--trace-output", "mixed.vcd", "--trace-format", "vcd" });
    assert(mixed.status == 0);
    auto mixed_names = vcd_declarations(read_text(mixed_workspace / "mixed.vcd"));
    for (auto& entry : mixed_names) {
        entry.erase(entry.find('='));
    }
    std::ranges::sort(mixed_names);
    assert((mixed_names
        == std::vector<std::string> {
            "top.a", "top.u.a", "top.u.inner", "top.u.y", "top.y" }));

    // An instance inside a generate block belongs to the enclosing
    // instance's hierarchy (IEEE 1076-2019 11.8): its FST scope is under
    // the root.
    const auto generate_workspace = directory / "vhdl-generate-fst";
    std::filesystem::create_directories(generate_workspace);
    {
        std::ofstream output(generate_workspace / "gen.vhd");
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
entity leaf is
  port (a : in std_logic; y : out std_logic);
end entity;
architecture rtl of leaf is
begin
  y <= not a;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
entity gentop is
  generic (USE_LEAF : integer := 1);
end entity;
architecture test of gentop is
  signal a : std_logic := '0';
  signal y : std_logic;
begin
  a <= '1' after 5 ns;
  g_on : if USE_LEAF /= 0 generate
    u_leaf : entity work.leaf port map (a => a, y => y);
  end generate;
  g_off : if USE_LEAF = 0 generate
    y <= a;
  end generate;
end architecture;
)";
    }
    workspace::prepare(generate_workspace,
        "fsim::config set elaborate.time_resolution 1ns\n",
        { "gen.vhd" }, "vhdl:work.gentop");
    const auto generated = workspace::run_streams(generate_workspace,
        { "simulate", "--trace-output", "gen.fst", "--trace-format", "fst" });
    assert(generated.status == 0);
    assert(generated.error.find("FSIM-TRACE") == std::string::npos);
    assert(std::filesystem::file_size(generate_workspace / "gen.fst") != 0U);
}

} // namespace fsim::test
