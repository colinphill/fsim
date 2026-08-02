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
  assert(checked->standard_sources.size() == 13);
  for (const std::string_view digest : {
           "f6bdde6dcd120358d8ce51b2760bdffaebd2e5a0ab5267fe9f7433eec89451d9",
           "093b095a30ca301968d3a3680c995fee5e59cd34b3b93bdd04bfb1e7b00002d4",
           "46e9f26610bd457184960dadb39c7d0e9fd5e15a7662414e5c3e2a79e377e05a"}) {
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
            && unit.library == "ieee" && unit.name == "float_pkg"
            && unit.primary_name.empty();
      });
  assert(package != checked->parsed.units.end());
  assert(
      package->standard_package_revision
      == "ieee-p1076:1076-2019:16a012320947d378611cc7457f64ed76cb52bac4");
  assert(std::ranges::find(
             package->standard_package_declarations, "float32")
         != package->standard_package_declarations.end());
  assert(std::ranges::find(
             package->standard_package_declarations, "isnan")
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
  assert(entity->ports.front().type.packed_range->left == 8);
  assert(entity->ports.front().type.packed_range->right == -23);
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
      / ("fsim-vhdl-float-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto source = directory.path / "float_app.vhd";
  {
    std::ofstream output{source};
    output << R"(
library ieee;
use ieee.float_pkg.all;
entity float_app is
  port (seed : in float(8 downto -23));
end entity;
library ieee;
use ieee.float_pkg.all;
architecture rtl of float_app is
  signal one, sum, difference, product, quotient, root, magnitude : float(8 downto -23);
  signal positive_infinity, quiet_nan, negative_zero : float(8 downto -23);
  signal infinity_bits : std_logic_vector(31 downto 0);
  signal finite_one, finite_infinity, nan_value, unordered_value : boolean;
  signal negative_value, compared : boolean;
  signal rounded_integer : integer;
begin
  one <= to_float(1, 8, 23);
  sum <= add(to_float(1, 8, 23), to_float(2, 8, 23));
  difference <= subtract(to_float(5, 8, 23), to_float(2, 8, 23));
  product <= multiply(to_float(3, 8, 23), to_float(2, 8, 23));
  quotient <= divide(to_float(7, 8, 23), to_float(2, 8, 23));
  root <= sqrt(to_float(9, 8, 23));
  magnitude <= subtract(to_float(0, 8, 23), to_float(-2, 8, 23));
  positive_infinity <= pos_inffp(8, 23);
  quiet_nan <= qnanfp(8, 23);
  negative_zero <= neg_zerofp(8, 23);
  infinity_bits <= to_slv(pos_inffp(8, 23));
  finite_one <= finite(to_float(1, 8, 23));
  finite_infinity <= finite(pos_inffp(8, 23));
  nan_value <= isnan(qnanfp(8, 23));
  unordered_value <= unordered(qnanfp(8, 23), to_float(1, 8, 23));
  negative_value <= is_negative(neg_zerofp(8, 23));
  compared <= gt(to_float(3, 8, 23), to_float(2, 8, 23));
  rounded_integer <= to_integer(divide(to_float(7, 8, 23), to_float(2, 8, 23)));
end architecture;
)";
    assert(output.good());
  }

  const auto invalid = directory.path / "float_invalid.vhd";
  {
    std::ofstream output{invalid};
    output << R"(
library ieee;
use ieee.float_pkg.all;
entity float_invalid is end entity;
library ieee;
use ieee.float_pkg.all;
architecture rtl of float_invalid is
  signal dynamic_integer : integer;
  signal wrong_range : float(5 downto -10);
  signal dynamic_float : float(8 downto -23);
  signal invalid_integer : integer;
begin
  wrong_range <= to_float(1, 8, 23);
  dynamic_float <= to_float(dynamic_integer, 8, 23);
  invalid_integer <= to_integer(pos_inffp(8, 23));
end architecture;
)";
    assert(output.good());
  }

  const std::vector<std::string> names{
      "float_app.one", "float_app.sum", "float_app.difference",
      "float_app.product", "float_app.quotient", "float_app.root",
      "float_app.magnitude", "float_app.positive_infinity",
      "float_app.quiet_nan", "float_app.negative_zero",
      "float_app.infinity_bits", "float_app.finite_one",
      "float_app.finite_infinity", "float_app.nan_value",
      "float_app.unordered_value", "float_app.negative_value",
      "float_app.compared", "float_app.rounded_integer"};
  const std::vector<std::string> expected{
      "00111111100000000000000000000000",
      "01000000010000000000000000000000",
      "01000000010000000000000000000000",
      "01000000110000000000000000000000",
      "01000000011000000000000000000000",
      "01000000010000000000000000000000",
      "01000000000000000000000000000000",
      "01111111100000000000000000000000",
      "01111111110000000000000000000000",
      "10000000000000000000000000000000",
      "01111111100000000000000000000000",
      "1", "0", "1", "1", "1", "1",
      "00000000000000000000000000000100"};
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path, source, "float_app", optimization);
    verify_projection(config);
    verify_runs(config, names, expected);
  }

  fsim::diagnostic::Engine diagnostics;
  const auto invalid_project = fsim::app::build_project(
      make_config(
          directory.path, invalid, "float_invalid",
          fsim::project::Optimization::o0),
      diagnostics);
  assert(!invalid_project);
  for (const std::string_view code : {
           "FSIM-ELAB-VHFLT-001", "FSIM-ELAB-VHFLT-002",
           "FSIM-ELAB-VHFLT-004"}) {
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [&](const fsim::diagnostic::Diagnostic& diagnostic) {
          return diagnostic.code == code;
        }));
  }
  return 0;
}
