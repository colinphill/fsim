// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

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

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::string& top,
    const std::string& cache,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = top;
  config.project.top = "vhdl:work." + top + "(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path = directory / (
      cache
      + (optimization == fsim::project::Optimization::o0 ? "-o0" : "-o2"));
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
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics native_cache;
};

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::vector<std::string>& names) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
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
    const fsim::frontend::ValueDomain domain) {
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
    const std::vector<std::string>& expected) {
  const auto reference = run_once(
      config, fsim::app::SimulationEngine::interpreter, names);
  const auto compiled = run_once(
      config, fsim::app::SimulationEngine::compiled, names);
  const auto warm = run_once(
      config, fsim::app::SimulationEngine::compiled, names);
  assert(reference.result.status == fsim::runtime::RunStatus::completed);
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
    const std::vector<std::string_view>& expected) {
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

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-numeric-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto numeric_std = directory.path / "numeric_std_app.vhd";
  {
    std::ofstream output{numeric_std};
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
  signal compared : boolean;
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
  compared <= u > to_unsigned(12, 8);
end architecture;
)";
    assert(output.good());
  }

  const auto numeric_bit = directory.path / "numeric_bit_app.vhd";
  {
    std::ofstream output{numeric_bit};
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
begin
  u <= to_unsigned(9, 8);
  s <= to_signed(-3, 8);
  sum <= u + to_unsigned(5, 8);
  shifted <= shift_right(u, 1);
  converted <= unsigned(s);
  wide <= resize(s, 12);
  integer_u <= to_integer(to_unsigned(9, 8));
end architecture;
)";
    assert(output.good());
  }

  const auto invalid = directory.path / "numeric_invalid.vhd";
  {
    std::ofstream output{invalid};
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
  signal bad_integer : integer;
begin
  bad_size <= to_unsigned(1, 0);
  bad_integer <= to_integer("11111111111111111111111111111111");
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto std_config = make_config(
        directory.path, numeric_std, "numeric_std_app", "numeric-std",
        optimization);
    verify_package(
        std_config, "numeric_std", 4, fsim::frontend::ValueDomain::Logic9);
    verify_runs(
        std_config,
        {
            "numeric_std_app.u", "numeric_std_app.s",
            "numeric_std_app.add_u", "numeric_std_app.sub_u",
            "numeric_std_app.mul_u", "numeric_std_app.div_u",
            "numeric_std_app.mod_u", "numeric_std_app.add_s",
            "numeric_std_app.sub_s", "numeric_std_app.absolute_s",
            "numeric_std_app.wide_u", "numeric_std_app.wide_s",
            "numeric_std_app.shifted", "numeric_std_app.rotated",
            "numeric_std_app.converted", "numeric_std_app.integer_s",
            "numeric_std_app.compared",
        },
        {
            "00001101", "11111011", "00010000", "00001010",
            "00100111", "00000100", "00000001", "11111101",
            "11111001", "00000101", "000000001101", "111111111011",
            "00110100", "10000110", "11111011",
            "11111111111111111111111111111011", "1",
        });

    const auto bit_config = make_config(
        directory.path, numeric_bit, "numeric_bit_app", "numeric-bit",
        optimization);
    verify_package(
        bit_config, "numeric_bit", 2, fsim::frontend::ValueDomain::Bit2);
    verify_runs(
        bit_config,
        {
            "numeric_bit_app.u", "numeric_bit_app.s",
            "numeric_bit_app.sum", "numeric_bit_app.shifted",
            "numeric_bit_app.converted", "numeric_bit_app.wide",
            "numeric_bit_app.integer_u",
        },
        {
            "00001001", "11111101", "00001110", "00000100",
            "11111101", "111111111101",
            "00000000000000000000000000001001",
        });
  }
  verify_diagnostics(
      make_config(
          directory.path, invalid, "numeric_invalid", "numeric-invalid",
          fsim::project::Optimization::o0),
      {"FSIM-ELAB-VHNUM-002", "FSIM-ELAB-VHNUM-003"});
  return 0;
}
