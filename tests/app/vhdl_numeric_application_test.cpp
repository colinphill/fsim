// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "governed_process_limits.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::string& top,
    const std::string& cache,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = top;
    config.project.top = "vhdl:work." + top + "(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory / (cache + (optimization == fsim::project::Optimization::o0 ? "-o0" : "-o2"));
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

struct Capture {
    fsim::runtime::RunResult result;
    std::vector<std::string> values;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics native_cache;
};

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::vector<std::string>& names)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.native_cache = simulation.native_cache_statistics();
    capture.result = simulation.run();
    for (const auto& name : names) {
        const auto signal = simulation.find_signal(name);
        assert(signal);
        capture.values.push_back(
            simulation.read_signal(*signal).to_msb_string());
    }
    return capture;
}

void verify_package(
    const fsim::project::Config& config,
    const std::string_view package,
    const std::size_t source_count,
    const fsim::frontend::ValueDomain domain)
{
    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::check_project(config, diagnostics);
    assert(checked);
    assert(!diagnostics.has_error());
    assert(checked->source_count == 1);
    assert(checked->hdl_sources.size() == 1);
    assert(checked->standard_sources.size() == source_count);
    const auto declaration_hash = package == "numeric_std"
        ? "fcb9b1d05f8d98cd068e464bf20804d432a234128b253218524901bc96d19631"
        : "e54a257a6da6141ef2fb0b51dccbde127a665e474e4e8e354f28e720b5d440be";
    const auto body_hash = package == "numeric_std"
        ? "10e8bdc4fedc881a972f5900abe833d24397d686e07b566479c47495acf39721"
        : "21fc27ef3d7ff0932ebb6c92a4d5f10865b1867ce392a008c2de1d6fde3e11ab";
    assert(std::ranges::any_of(
        checked->standard_sources,
        [&](const fsim::app::CheckedSource& source) {
            return source.content_digest == declaration_hash;
        }));
    assert(std::ranges::any_of(
        checked->standard_sources,
        [&](const fsim::app::CheckedSource& source) {
            return source.content_digest == body_hash;
        }));
    const auto declaration = std::ranges::find_if(
        checked->parsed.units,
        [&](const fsim::frontend::DesignUnit& unit) {
            return unit.kind == fsim::frontend::UnitKind::VhdlPackage
                && unit.library == "ieee" && unit.name == package
                && unit.primary_name.empty();
        });
    assert(declaration != checked->parsed.units.end());
    assert(
        declaration->standard_package_revision
        == "ieee-p1076:1076-2019:16a012320947d378611cc7457f64ed76cb52bac4");
    assert(std::ranges::find(
               declaration->standard_package_declarations, "resize")
        != declaration->standard_package_declarations.end());
    const auto entity = std::ranges::find_if(
        checked->parsed.units,
        [](const fsim::frontend::DesignUnit& unit) {
            return unit.kind == fsim::frontend::UnitKind::VhdlEntity;
        });
    assert(entity != checked->parsed.units.end());
    assert(!entity->ports.empty());
    assert(entity->ports.front().type.domain == domain);
}

void verify_runs(
    const fsim::project::Config& config,
    const std::vector<std::string>& names,
    const std::vector<std::string>& expected)
{
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter, names);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled, names);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled, names);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    if (reference.values != expected) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            std::cerr << names[index] << ": observed=" << reference.values[index]
                      << ", expected=" << expected[index] << '\n';
        }
    }
    assert(reference.values == expected);
    assert(reference.values == compiled.values);
    assert(reference.values == warm.values);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes > 0);
    assert(compiled.compiled_modules > 0);
    assert(compiled.native_cache.misses == compiled.compiled_modules);
    assert(compiled.native_cache.stores == compiled.compiled_modules);
    assert(warm.native_cache.hits == warm.compiled_modules);
    assert(warm.native_cache.misses == 0);
#endif
}

void verify_diagnostics(
    const fsim::project::Config& config,
    const std::vector<std::string_view>& expected)
{
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project);
    for (const auto code : expected) {
        assert(std::ranges::any_of(
            diagnostics.diagnostics(),
            [&](const fsim::diagnostic::Diagnostic& diagnostic) {
                return diagnostic.code == code;
            }));
    }
}

void verify_runtime_failure(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view expected)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    bool failed = false;
    try {
        (void)simulation.run();
    } catch (const fsim::runtime::simir::AssertionError& error) {
        failed = std::string_view { error.what() }.find(expected)
            != std::string_view::npos;
    }
    if (!failed) {
        std::cerr << "missing runtime failure '" << expected
                  << "' for engine " << static_cast<int>(engine) << '\n';
    }
    assert(failed);
}

} // namespace

int main()
{
    fsim::test::install_governed_process_address_space_ceiling();
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vhdl-numeric-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);

    const auto numeric_std = directory.path / "numeric_std_app.vhd";
    {
        std::ofstream output { numeric_std };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity numeric_std_app is
  port (seed : in unsigned(7 downto 0));
end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of numeric_std_app is
  signal u : unsigned(7 downto 0);
  signal s : signed(7 downto 0);
  signal add_u, sub_u, mul_u, div_u, mod_u : unsigned(7 downto 0);
  signal add_s, sub_s, absolute_s : signed(7 downto 0);
  signal wide_u, wide_s : signed(11 downto 0);
  signal shifted, rotated, converted : unsigned(7 downto 0);
  signal integer_s : integer;
  signal integer_s_wide : integer;
  signal compared : boolean;
  signal u65 : unsigned(64 downto 0);
  signal s129 : signed(128 downto 0);
  signal converted129 : unsigned(128 downto 0);
  signal resized257, sum257 : unsigned(256 downto 0);
  signal shifted521 : unsigned(520 downto 0);
  signal statically_pruned : integer;
begin
  u <= to_unsigned(13, 8);
  s <= to_signed(-5, 8);
  add_u <= u + to_unsigned(3, 8);
  sub_u <= u - to_unsigned(3, 8);
  mul_u <= u * to_unsigned(3, 8);
  div_u <= u / to_unsigned(3, 8);
  mod_u <= u mod to_unsigned(3, 8);
  add_s <= s + to_signed(2, 8);
  sub_s <= s - to_signed(2, 8);
  absolute_s <= abs s;
  wide_u <= signed(resize(u, 12));
  wide_s <= resize(s, 12);
  shifted <= shift_left(u, 2);
  rotated <= rotate_right(u, 1);
  converted <= unsigned(s);
  integer_s <= to_integer(to_signed(-5, 8));
  u65 <= to_unsigned(13, 65);
  s129 <= to_signed(-5, 129);
  converted129 <= unsigned(s129);
  resized257 <= resize(u65, 257);
  sum257 <= resized257 + to_unsigned(3, 257);
  shifted521 <= shift_left(resize(u65, 521), 500);
  integer_s_wide <= to_integer(to_signed(-5, 129));
  compared <= u > to_unsigned(12, 8);
  prune_runtime_lookup : process
    variable position : unsigned(0 downto 0) := "1";
    variable values : std_logic_vector(0 downto 0) := "1";
    variable enabled : boolean := false;
    variable allowed : boolean := true;
  begin
    if enabled and values(to_integer(position)) = '1' then
      statically_pruned <= 7;
    elsif allowed or values(to_integer(position)) = '1' then
      statically_pruned <= 3;
    else
      statically_pruned <= 9;
    end if;
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }

    const auto numeric_bit = directory.path / "numeric_bit_app.vhd";
    {
        std::ofstream output { numeric_bit };
        output << R"(
library ieee;
use ieee.numeric_bit.all;
entity numeric_bit_app is
  port (seed : in unsigned(7 downto 0));
end entity;
library ieee;
use ieee.numeric_bit.all;
architecture rtl of numeric_bit_app is
  signal u : unsigned(7 downto 0);
  signal s : signed(7 downto 0);
  signal sum, shifted, converted : unsigned(7 downto 0);
  signal wide : signed(11 downto 0);
  signal integer_u : integer;
  signal integer_u_wide : integer;
  signal u65 : unsigned(64 downto 0);
  signal s129 : signed(128 downto 0);
  signal resized257 : signed(256 downto 0);
  signal rotated521 : unsigned(520 downto 0);
begin
  u <= to_unsigned(9, 8);
  s <= to_signed(-3, 8);
  sum <= u + to_unsigned(5, 8);
  shifted <= shift_right(u, 1);
  converted <= unsigned(s);
  wide <= resize(s, 12);
  integer_u <= to_integer(to_unsigned(9, 8));
  u65 <= to_unsigned(9, 65);
  s129 <= to_signed(-3, 129);
  resized257 <= resize(s129, 257);
  rotated521 <= rotate_left(resize(u65, 521), 517);
  integer_u_wide <= to_integer(to_unsigned(9, 65));
end architecture;
)";
        assert(output.good());
    }

    const auto invalid = directory.path / "numeric_invalid.vhd";
    {
        std::ofstream output { invalid };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity numeric_invalid is
end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
architecture rtl of numeric_invalid is
  signal bad_size : unsigned(0 downto 0);
  signal bad_resource : unsigned(0 downto 0);
begin
  bad_size <= to_unsigned(1, 0);
  bad_resource <= to_unsigned(1, 4294967296);
end architecture;
)";
        assert(output.good());
    }

    const auto out_of_range = directory.path / "numeric_out_of_range.vhd";
    {
        std::ofstream output { out_of_range };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity numeric_out_of_range is end entity;
architecture rtl of numeric_out_of_range is
  signal result : integer;
begin
  result <= to_integer(shift_left(to_unsigned(1, 65), 31));
end architecture;
)";
        assert(output.good());
    }

    const auto unknown = directory.path / "numeric_unknown.vhd";
    {
        std::ofstream output { unknown };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity numeric_unknown is
  port (seed : in unsigned(64 downto 0));
end entity;
architecture rtl of numeric_unknown is
  signal result : integer;
begin
  result <= to_integer(seed);
end architecture;
)";
        assert(output.good());
    }

    const auto synopsys = directory.path / "synopsys_numeric_app.vhd";
    {
        std::ofstream output { synopsys };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_misc.all;
entity synopsys_numeric_app is end entity;
architecture rtl of synopsys_numeric_app is
  signal signed137 : signed(136 downto 0);
  signal unsigned137 : unsigned(136 downto 0);
  signal slv137, ext137, sxt137 : std_logic_vector(136 downto 0);
  signal shifted_left : unsigned(7 downto 0);
  signal shifted_right : signed(7 downto 0);
  signal signed_integer : integer;
  signal ones : std_logic_vector(3 downto 0);
  signal unknowns : std_logic_vector(3 downto 0);
  signal nulls : std_logic_vector(0 downto 1);
  signal reduced_and, reduced_nand, reduced_or, reduced_nor : std_logic;
  signal reduced_xor, reduced_xnor, reduced_unknown : std_logic;
  signal null_and, null_nand, null_or, null_nor : std_logic;
  signal null_xor, null_xnor : std_logic;
begin
  signed137 <= conv_signed(-1, 137);
  unsigned137 <= conv_unsigned(5, 137);
  slv137 <= conv_std_logic_vector(-2, 137);
  ext137 <= ext(conv_std_logic_vector(10, 4), 137);
  sxt137 <= sxt(conv_std_logic_vector(-3, 4), 137);
  shifted_left <= shl(conv_unsigned(3, 8), conv_unsigned(2, 8));
  shifted_right <= shr(conv_signed(-8, 8), conv_unsigned(2, 8));
  signed_integer <= conv_integer(conv_signed(-7, 137));
  ones <= "1111";
  unknowns <= "11X1";
  reduced_and <= and_reduce(ones);
  reduced_nand <= nand_reduce(ones);
  reduced_or <= or_reduce(ones);
  reduced_nor <= nor_reduce(ones);
  reduced_xor <= xor_reduce(ones);
  reduced_xnor <= xnor_reduce(ones);
  reduced_unknown <= and_reduce(unknowns);
  null_and <= and_reduce(nulls);
  null_nand <= nand_reduce(nulls);
  null_or <= or_reduce(nulls);
  null_nor <= nor_reduce(nulls);
  null_xor <= xor_reduce(nulls);
  null_xnor <= xnor_reduce(nulls);
end architecture;
)";
        assert(output.good());
    }

    const auto synopsys_signed = directory.path / "synopsys_signed_app.vhd";
    {
        std::ofstream output { synopsys_signed };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_signed.all;
entity synopsys_signed_app is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_signed.all;
architecture rtl of synopsys_signed_app is
  signal a_down, b_down : std_logic_vector(7 downto 0);
  signal sum_down, difference_down, product_down : std_logic_vector(7 downto 0);
  signal absolute_down, negated_down, plus_integer_down : std_logic_vector(7 downto 0);
  signal shifted_down : std_logic_vector(7 downto 0);
  signal less_down, less_zero_down : boolean;
  signal integer_down : integer;
  signal a_up, b_up : std_logic_vector(0 to 7);
  signal sum_up, shifted_up : std_logic_vector(0 to 7);
  signal less_up : boolean;
  signal wide_left, wide_right, wide_sum : std_logic_vector(136 downto 0);
  signal wide_less : boolean;
begin
  a_down <= "11111100";
  b_down <= "00000010";
  sum_down <= a_down + b_down;
  difference_down <= a_down - b_down;
  product_down <= a_down * b_down;
  absolute_down <= abs a_down;
  negated_down <= -b_down;
  plus_integer_down <= a_down + 1;
  shifted_down <= shr(a_down, "00000010");
  less_down <= a_down < b_down;
  less_zero_down <= a_down < 0;
  integer_down <= conv_integer("11111100");
  a_up <= "11111100";
  b_up <= "00000010";
  sum_up <= a_up + b_up;
  shifted_up <= shr(a_up, "00000010");
  less_up <= a_up < b_up;
  wide_left <= (136 => '1', others => '0');
  wide_right <= (0 => '1', others => '0');
  wide_sum <= wide_left + wide_right;
  wide_less <= wide_left < wide_right;
end architecture;
)";
        assert(output.good());
    }

    const auto synopsys_unsigned
        = directory.path / "synopsys_unsigned_app.vhd";
    {
        std::ofstream output { synopsys_unsigned };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
entity synopsys_unsigned_app is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_unsigned.all;
architecture rtl of synopsys_unsigned_app is
  signal a_down, b_down : std_logic_vector(7 downto 0);
  signal sum_down, difference_down, product_down : std_logic_vector(7 downto 0);
  signal plus_integer_down, shifted_down : std_logic_vector(7 downto 0);
  signal less_down, less_zero_down : boolean;
  signal integer_down : integer;
  signal a_up, b_up : std_logic_vector(0 to 7);
  signal sum_up, shifted_up : std_logic_vector(0 to 7);
  signal less_up : boolean;
  signal wide_left, wide_right, wide_sum : std_logic_vector(136 downto 0);
  signal wide_less : boolean;
begin
  a_down <= "11111100";
  b_down <= "00000010";
  sum_down <= a_down + b_down;
  difference_down <= a_down - b_down;
  product_down <= a_down * b_down;
  plus_integer_down <= a_down + 1;
  shifted_down <= shr(a_down, "00000010");
  less_down <= a_down < b_down;
  less_zero_down <= a_down < 0;
  integer_down <= conv_integer("11111100");
  a_up <= "11111100";
  b_up <= "00000010";
  sum_up <= a_up + b_up;
  shifted_up <= shr(a_up, "00000010");
  less_up <= a_up < b_up;
  wide_left <= (136 => '1', others => '0');
  wide_right <= (0 => '1', others => '0');
  wide_sum <= wide_left + wide_right;
  wide_less <= wide_left < wide_right;
end architecture;
)";
        assert(output.good());
    }

    const auto synopsys_ambiguous
        = directory.path / "synopsys_ambiguous.vhd";
    {
        std::ofstream output { synopsys_ambiguous };
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_signed.all;
use ieee.std_logic_unsigned.all;
entity synopsys_ambiguous is end entity;
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_signed.all;
use ieee.std_logic_unsigned.all;
architecture rtl of synopsys_ambiguous is
  signal left, right, result_value : std_logic_vector(7 downto 0);
begin
  result_value <= left + right;
end architecture;
)";
        assert(output.good());
    }

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto std_config = make_config(
            directory.path, numeric_std, "numeric_std_app", "numeric-std",
            optimization);
        verify_package(
            std_config, "numeric_std", 4, fsim::frontend::ValueDomain::Logic9);
        verify_runs(
            std_config,
            {
                "numeric_std_app.u",
                "numeric_std_app.s",
                "numeric_std_app.add_u",
                "numeric_std_app.sub_u",
                "numeric_std_app.mul_u",
                "numeric_std_app.div_u",
                "numeric_std_app.mod_u",
                "numeric_std_app.add_s",
                "numeric_std_app.sub_s",
                "numeric_std_app.absolute_s",
                "numeric_std_app.wide_u",
                "numeric_std_app.wide_s",
                "numeric_std_app.shifted",
                "numeric_std_app.rotated",
                "numeric_std_app.converted",
                "numeric_std_app.integer_s",
                "numeric_std_app.compared",
                "numeric_std_app.u65",
                "numeric_std_app.s129",
                "numeric_std_app.resized257",
                "numeric_std_app.converted129",
                "numeric_std_app.sum257",
                "numeric_std_app.shifted521",
                "numeric_std_app.integer_s_wide",
                "numeric_std_app.statically_pruned",
            },
            {
                "00001101",
                "11111011",
                "00010000",
                "00001010",
                "00100111",
                "00000100",
                "00000001",
                "11111101",
                "11111001",
                "00000101",
                "000000001101",
                "111111111011",
                "00110100",
                "10000110",
                "11111011",
                "11111111111111111111111111111011",
                "1",
                std::string(61, '0') + "1101",
                std::string(125, '1') + "1011",
                std::string(253, '0') + "1101",
                std::string(125, '1') + "1011",
                std::string(252, '0') + "10000",
                std::string(17, '0') + "1101" + std::string(500, '0'),
                std::string(28, '1') + "1011",
                "00000000000000000000000000000011",
            });

        const auto bit_config = make_config(
            directory.path, numeric_bit, "numeric_bit_app", "numeric-bit",
            optimization);
        verify_package(
            bit_config, "numeric_bit", 2, fsim::frontend::ValueDomain::Bit2);
        verify_runs(
            bit_config,
            {
                "numeric_bit_app.u",
                "numeric_bit_app.s",
                "numeric_bit_app.sum",
                "numeric_bit_app.shifted",
                "numeric_bit_app.converted",
                "numeric_bit_app.wide",
                "numeric_bit_app.integer_u",
                "numeric_bit_app.u65",
                "numeric_bit_app.s129",
                "numeric_bit_app.resized257",
                "numeric_bit_app.rotated521",
                "numeric_bit_app.integer_u_wide",
            },
            {
                "00001001",
                "11111101",
                "00001110",
                "00000100",
                "11111101",
                "111111111101",
                "00000000000000000000000000001001",
                std::string(61, '0') + "1001",
                std::string(127, '1') + "01",
                std::string(255, '1') + "01",
                "1001" + std::string(517, '0'),
                std::string(28, '0') + "1001",
            });

        const auto synopsys_config = make_config(
            directory.path, synopsys, "synopsys_numeric_app",
            "synopsys-numeric", optimization);
        verify_runs(
            synopsys_config,
            {
                "synopsys_numeric_app.signed137",
                "synopsys_numeric_app.unsigned137",
                "synopsys_numeric_app.slv137",
                "synopsys_numeric_app.ext137",
                "synopsys_numeric_app.sxt137",
                "synopsys_numeric_app.shifted_left",
                "synopsys_numeric_app.shifted_right",
                "synopsys_numeric_app.signed_integer",
                "synopsys_numeric_app.reduced_and",
                "synopsys_numeric_app.reduced_nand",
                "synopsys_numeric_app.reduced_or",
                "synopsys_numeric_app.reduced_nor",
                "synopsys_numeric_app.reduced_xor",
                "synopsys_numeric_app.reduced_xnor",
                "synopsys_numeric_app.reduced_unknown",
                "synopsys_numeric_app.null_and",
                "synopsys_numeric_app.null_nand",
                "synopsys_numeric_app.null_or",
                "synopsys_numeric_app.null_nor",
                "synopsys_numeric_app.null_xor",
                "synopsys_numeric_app.null_xnor",
            },
            {
                std::string(137, '1'),
                std::string(134, '0') + "101",
                std::string(136, '1') + "0",
                std::string(133, '0') + "1010",
                std::string(135, '1') + "01",
                "00001100",
                "11111110",
                std::string(28, '1') + "1001",
                "1",
                "0",
                "1",
                "0",
                "0",
                "1",
                "X",
                "1",
                "0",
                "0",
                "1",
                "0",
                "1",
            });

        const auto signed_config = make_config(
            directory.path, synopsys_signed, "synopsys_signed_app",
            "synopsys-signed", optimization);
        verify_runs(
            signed_config,
            {
                "synopsys_signed_app.sum_down",
                "synopsys_signed_app.difference_down",
                "synopsys_signed_app.product_down",
                "synopsys_signed_app.absolute_down",
                "synopsys_signed_app.negated_down",
                "synopsys_signed_app.plus_integer_down",
                "synopsys_signed_app.shifted_down",
                "synopsys_signed_app.less_down",
                "synopsys_signed_app.less_zero_down",
                "synopsys_signed_app.integer_down",
                "synopsys_signed_app.sum_up",
                "synopsys_signed_app.shifted_up",
                "synopsys_signed_app.less_up",
                "synopsys_signed_app.wide_sum",
                "synopsys_signed_app.wide_less",
            },
            {
                "11111110",
                "11111010",
                "11111000",
                "00000100",
                "11111110",
                "11111101",
                "11111111",
                "1",
                "1",
                std::string(30, '1') + "00",
                "11111110",
                "11111111",
                "1",
                "1" + std::string(135, '0') + "1",
                "1",
            });

        const auto unsigned_config = make_config(
            directory.path, synopsys_unsigned, "synopsys_unsigned_app",
            "synopsys-unsigned", optimization);
        verify_runs(
            unsigned_config,
            {
                "synopsys_unsigned_app.sum_down",
                "synopsys_unsigned_app.difference_down",
                "synopsys_unsigned_app.product_down",
                "synopsys_unsigned_app.plus_integer_down",
                "synopsys_unsigned_app.shifted_down",
                "synopsys_unsigned_app.less_down",
                "synopsys_unsigned_app.less_zero_down",
                "synopsys_unsigned_app.integer_down",
                "synopsys_unsigned_app.sum_up",
                "synopsys_unsigned_app.shifted_up",
                "synopsys_unsigned_app.less_up",
                "synopsys_unsigned_app.wide_sum",
                "synopsys_unsigned_app.wide_less",
            },
            {
                "11111110",
                "11111010",
                "11111000",
                "11111101",
                "00111111",
                "0",
                "0",
                std::string(24, '0') + "11111100",
                "11111110",
                "00111111",
                "0",
                "1" + std::string(135, '0') + "1",
                "0",
            });
    }
    verify_diagnostics(
        make_config(
            directory.path, synopsys_ambiguous, "synopsys_ambiguous",
            "synopsys-ambiguous", fsim::project::Optimization::o0),
        { "FSIM-ELAB-VHSYN-001" });
    verify_diagnostics(
        make_config(
            directory.path, invalid, "numeric_invalid", "numeric-invalid",
            fsim::project::Optimization::o0),
        { "FSIM-ELAB-VHNUM-002" });
    const auto range_config = make_config(
        directory.path, out_of_range, "numeric_out_of_range",
        "numeric-out-of-range", fsim::project::Optimization::o0);
    verify_runtime_failure(
        range_config, fsim::app::SimulationEngine::interpreter,
        "outside the predefined integer range");
    verify_runtime_failure(
        range_config, fsim::app::SimulationEngine::compiled,
        "outside the predefined integer range");
    const auto unknown_config = make_config(
        directory.path, unknown, "numeric_unknown",
        "numeric-unknown", fsim::project::Optimization::o0);
    verify_runs(
        unknown_config,
        { "numeric_unknown.result" },
        { std::string(32, '0') });
    std::cout
        << "FSIM-VHDL-SYNOPSYS-PACKAGES-PASS "
           "packages=std_logic_signed/std_logic_unsigned/std_logic_arith/"
           "std_logic_misc engines=interpreter/llvm-o0/llvm-o2 widths=137 "
           "nulls=6 directions=ascending/descending "
           "ambiguity=FSIM-ELAB-VHSYN-001 resources=as6g/delta1000\n";
    return 0;
}
