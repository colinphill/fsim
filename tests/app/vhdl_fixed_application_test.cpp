// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = top;
  config.project.top = "vhdl:work." + top + "(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path = directory / (
      top + (optimization == fsim::project::Optimization::o0 ? "-o0" : "-o2"));
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

void verify_projection(const fsim::project::Config& config) {
  fsim::diagnostic::Engine diagnostics;
  const auto checked = fsim::app::check_project(config, diagnostics);
  assert(checked);
  assert(!diagnostics.has_error());
  assert(checked->source_count == 1);
  assert(checked->hdl_sources.size() == 1);
  assert(checked->standard_sources.size() == 10);
  for (const std::string_view digest : {
           "76f98d70b4e5e80ee4411f3b669ac44beed112b8ea0d4fb89005a7aedd20ffef",
           "b5e3731985388b9e396bc7c62949775e7b13241c3b53281262947ef88cb28fad",
           "bbd601960c294aa0675a91b4576f1fcfff2d612fe911ad8cc51ca2c10b1565cd"}) {
    assert(std::ranges::any_of(
        checked->standard_sources,
        [&](const fsim::app::CheckedSource& source) {
          return source.content_digest == digest;
        }));
  }
  const auto package = std::ranges::find_if(
      checked->parsed.units,
      [](const fsim::frontend::DesignUnit& unit) {
        return unit.kind == fsim::frontend::UnitKind::VhdlPackage
            && unit.library == "ieee" && unit.name == "fixed_pkg"
            && unit.primary_name.empty();
      });
  assert(package != checked->parsed.units.end());
  assert(
      package->standard_package_revision
      == "ieee-p1076:1076-2019:16a012320947d378611cc7457f64ed76cb52bac4");
  assert(std::ranges::find(
             package->standard_package_declarations, "ufixed")
         != package->standard_package_declarations.end());
  const auto entity = std::ranges::find_if(
      checked->parsed.units,
      [](const fsim::frontend::DesignUnit& unit) {
        return unit.kind == fsim::frontend::UnitKind::VhdlEntity;
      });
  assert(entity != checked->parsed.units.end());
  assert(!entity->ports.empty());
  assert(entity->ports.front().type.domain
         == fsim::frontend::ValueDomain::Logic9);
  assert(entity->ports.front().type.packed_range);
  assert(entity->ports.front().type.packed_range->left == 3);
  assert(entity->ports.front().type.packed_range->right == -4);
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

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-fixed-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto source = directory.path / "fixed_app.vhd";
  {
    std::ofstream output{source};
    output << R"(
library ieee;
use ieee.fixed_pkg.all;
entity fixed_app is
  port (seed : in ufixed(3 downto -4));
end entity;
library ieee;
use ieee.fixed_pkg.all;
architecture rtl of fixed_app is
  signal u, add_u, sub_u, saturated_u : ufixed(3 downto -4);
  signal s, saturated_s : sfixed(3 downto -4);
  signal round_source : ufixed(1 downto -4);
  signal rounded : ufixed(1 downto -2);
  signal selected : ufixed(1 downto -2);
  signal compared : boolean;
  signal wide65 : ufixed(64 downto 0);
  signal wide129 : sfixed(64 downto -64);
  signal resized257 : ufixed(128 downto -128);
  signal wide521, sum521 : ufixed(260 downto -260);
begin
  u <= to_ufixed(3, 3, -4);
  s <= to_sfixed(-2, 3, -4);
  add_u <= u + to_ufixed(1, 3, -4);
  sub_u <= u - to_ufixed(1, 3, -4);
  saturated_u <= to_ufixed(20, 3, -4);
  saturated_s <= to_sfixed(10, 3, -4);
  round_source <= "000110";
  rounded <= resize(round_source, 1, -2);
  selected <= u(1 downto -2);
  compared <= u > to_ufixed(2, 3, -4);
  wide65 <= to_ufixed(3, 64, 0);
  wide129 <= to_sfixed(-2, 64, -64);
  resized257 <= resize(wide65, 128, -128);
  wide521 <= to_ufixed(1, 260, -260);
  sum521 <= wide521 + to_ufixed(1, 260, -260);
end architecture;
)";
    assert(output.good());
  }

  const auto invalid = directory.path / "fixed_invalid.vhd";
  {
    std::ofstream output{invalid};
    output << R"(
library ieee;
use ieee.fixed_pkg.all;
entity fixed_invalid is end entity;
library ieee;
use ieee.fixed_pkg.all;
architecture rtl of fixed_invalid is
  signal ascending : ufixed(-4 to 3);
  signal bad_resource : ufixed(0 downto 0);
begin
  ascending <= to_ufixed(1, 3, -4);
  bad_resource <= to_ufixed(1, 4294967295, 0);
end architecture;
)";
    assert(output.good());
  }

  const std::vector<std::string> names{
      "fixed_app.u", "fixed_app.s", "fixed_app.add_u",
      "fixed_app.sub_u", "fixed_app.saturated_u",
      "fixed_app.saturated_s", "fixed_app.round_source",
      "fixed_app.rounded", "fixed_app.selected", "fixed_app.compared",
      "fixed_app.wide65", "fixed_app.wide129",
      "fixed_app.resized257", "fixed_app.wide521",
      "fixed_app.sum521"};
  const std::vector<std::string> expected{
      "00110000", "11100000", "01000000", "00100000",
      "11111111", "01111111", "000110", "0010", "1100", "1",
      std::string(63, '0') + "11",
      std::string(64, '1') + std::string(65, '0'),
      std::string(127, '0') + "11" + std::string(128, '0'),
      std::string(260, '0') + "1" + std::string(260, '0'),
      std::string(259, '0') + "1" + std::string(261, '0')};
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path, source, "fixed_app", optimization);
    verify_projection(config);
    verify_runs(config, names, expected);
  }

  fsim::diagnostic::Engine diagnostics;
  const auto invalid_project = fsim::app::build_project(
      make_config(
          directory.path, invalid, "fixed_invalid",
          fsim::project::Optimization::o0),
      diagnostics);
  assert(!invalid_project);
  for (const std::string_view code : {
           "FSIM-ELAB-VHFIX-002", "FSIM-ELAB-VHFIX-004"}) {
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [&](const fsim::diagnostic::Diagnostic& diagnostic) {
          return diagnostic.code == code;
        }));
  }
  return 0;
}
