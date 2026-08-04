// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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
    const std::vector<std::filesystem::path>& sources,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "ieee_integration";
  config.project.top = "vhdl:work.ieee_integration(rtl)";
  config.project.time_resolution = "1ns";
  config.build.jobs = 8;
  config.build.optimization = optimization;
  config.build.cache_path = directory / (
      optimization == fsim::project::Optimization::o0 ? "cache-o0" : "cache-o2");
  config.run.max_deltas = 1000;
  for (const auto& source : sources) {
    fsim::project::SourceSet source_set;
    source_set.language = fsim::project::Language::vhdl;
    source_set.standard = "2008";
    source_set.library = "work";
    source_set.compilation_unit = "file";
    source_set.files.push_back(source);
    config.source_sets.push_back(std::move(source_set));
  }
  return config;
}

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<std::string> values;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics native_cache;
  std::vector<std::string> local_values;
  std::string vcd;
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
  const auto specialization = std::ranges::find_if(
      project->design.specializations(), [](const auto& candidate) {
        return candidate.instance == "ieee_integration";
      });
  assert(specialization != project->design.specializations().end());
  const auto delays = std::ranges::find_if(
      specialization->parameter_identity_values,
      [](const auto& value) {
        return value.first == "vital_delays_generic";
      });
  const auto map = std::ranges::find_if(
      specialization->parameter_identity_values,
      [](const auto& value) {
        return value.first == "vital_map_generic";
      });
  assert(delays != specialization->parameter_identity_values.end());
  assert(map != specialization->parameter_identity_values.end());
  assert(delays->second.find("vhdlcomposite-v1") != std::string::npos);
  assert(delays->second.find("width=128") != std::string::npos);
  assert(delays->second.ends_with(
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000010"));
  assert(map->second.find("vhdlcomposite-v1") != std::string::npos);
  assert(map->second.find("value=UX01") != std::string::npos);
  for (const std::string_view dependency : {
           "numeric_bit.vhdl", "numeric_std.vhdl", "fixed_pkg.vhdl",
           "float_pkg.vhdl", "vital_timing.vhdl",
           "vital_primitives.vhdl"}) {
    assert(std::ranges::any_of(
        specialization->source_dependencies,
        [&](const std::string& source) {
          return source.ends_with(dependency);
        }));
  }
  std::vector<std::pair<
      fsim::runtime::simir::ProcessId, std::size_t>> debug_locals;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0; index < process.debug_locals.size(); ++index) {
      if (process.debug_locals[index].name.starts_with("package_local_")) {
        debug_locals.emplace_back(process.id, index);
      }
    }
  }
  assert(debug_locals.size() == 4);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.native_cache = simulation.native_cache_statistics();
  std::vector<fsim::runtime::simir::SignalId> signals;
  std::vector<fsim::runtime::VcdSignal> traces;
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (const auto& name : names) {
    const auto signal = simulation.find_signal(name);
    assert(signal);
    signals.push_back(*signal);
    traces.push_back(vcd.declare_signal(
        name, simulation.read_signal(*signal).width()));
  }
  vcd.begin();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(traces[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        const auto found = std::ranges::find(signals, signal);
        if (found == signals.end()) {
          return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(signals.begin(), found));
        vcd.set_time(time);
        vcd.change(traces[index], value);
      });
  capture.result = simulation.run();
  for (const auto signal : signals) {
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  for (const auto& [process, local] : debug_locals) {
    capture.local_values.push_back(
        simulation.read_process_local(process, local).to_msb_string());
  }
  std::ranges::sort(capture.local_values);
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_analysis(const fsim::project::Config& config) {
  fsim::diagnostic::Engine diagnostics;
  const auto checked = fsim::app::check_project(config, diagnostics);
  assert(checked);
  assert(!diagnostics.has_error());
  assert(checked->source_count == 3);
  assert(checked->hdl_sources.size() == 3);
  assert(checked->standard_sources.size() == 18);
  for (const auto& source : checked->standard_sources) {
    assert(source.path.generic_string().starts_with("fsim-standard/ieee/"));
    if (source.path.filename() == "vital_timing.vhdl"
        || source.path.filename() == "vital_primitives.vhdl") {
      assert(source.backing_path.empty());
    } else {
      assert(source.backing_path.generic_string().find(
                 "third_party/ieee-1076-2019/ieee/")
             != std::string::npos);
    }
  }
  std::vector<std::string> packages;
  for (const auto& unit : checked->parsed.units) {
    if (unit.kind == fsim::frontend::UnitKind::VhdlPackage
        && unit.library == "ieee" && unit.primary_name.empty()) {
      packages.push_back(unit.name);
    }
  }
  assert((packages == std::vector<std::string>{
      "std_logic_1164", "std_logic_textio", "numeric_bit",
      "numeric_std", "math_real", "fixed_float_types",
      "fixed_generic_pkg", "fixed_pkg", "float_generic_pkg",
      "float_pkg", "vital_timing", "vital_primitives"}));
  for (const std::string_view package : {
           "std_logic_1164", "numeric_bit", "numeric_std", "math_real",
           "fixed_generic_pkg", "float_generic_pkg"}) {
    const auto declaration = std::ranges::find_if(
        checked->parsed.units,
        [&](const fsim::frontend::DesignUnit& unit) {
          return unit.kind == fsim::frontend::UnitKind::VhdlPackage
              && unit.library == "ieee" && unit.name == package
              && unit.primary_name.empty();
        });
    assert(declaration != checked->parsed.units.end());
    const auto body = std::next(declaration);
    assert(body != checked->parsed.units.end());
    assert(body->kind == fsim::frontend::UnitKind::VhdlPackage);
    assert(body->library == "ieee" && body->name == package);
    assert(body->primary_name == package);
  }
  const auto architecture = std::ranges::find_if(
      checked->parsed.units,
      [](const fsim::frontend::DesignUnit& unit) {
        return unit.kind == fsim::frontend::UnitKind::VhdlArchitecture
            && unit.name == "rtl";
      });
  assert(architecture != checked->parsed.units.end());
  const auto signal_type = [&](const std::string_view name) {
    const auto signal = std::ranges::find_if(
        architecture->signals,
        [&](const fsim::frontend::SignalDeclaration& candidate) {
          return candidate.name == name;
        });
    assert(signal != architecture->signals.end());
    return signal->type;
  };
  assert(signal_type("numeric_logic").domain
         == fsim::frontend::ValueDomain::Logic9);
  assert(signal_type("numeric_bits").domain
         == fsim::frontend::ValueDomain::Bit2);
  assert(signal_type("fixed_rounded").spelling == "ufixed");
  assert(signal_type("float_sum").spelling == "float");
  assert(signal_type("vital_transition").spelling
         == "vitaltransitiontype");
  assert(signal_type("vital_delays").spelling == "vitaldelaytype01");
  assert(signal_type("vital_map").spelling == "vitalresultmaptype");
  assert(signal_type("vital_table").spelling == "vitaltruthtabletype");
  const auto package_type = [&](const std::string_view package,
                                const std::string_view name) {
    const auto declaration = std::ranges::find_if(
        checked->parsed.units,
        [&](const fsim::frontend::DesignUnit& unit) {
          return unit.kind == fsim::frontend::UnitKind::VhdlPackage
              && unit.library == "ieee" && unit.name == package
              && unit.primary_name.empty();
        });
    assert(declaration != checked->parsed.units.end());
    const auto alias = std::ranges::find_if(
        declaration->type_aliases,
        [&](const fsim::frontend::TypeAliasDeclaration& candidate) {
          return candidate.name == name;
        });
    assert(alias != declaration->type_aliases.end());
    return alias->type;
  };
  const auto transition = package_type(
      "vital_timing", "vitaltransitiontype");
  assert(transition.enumeration_literals.size() == 12);
  assert(transition.width() == 4);
  const auto delays = package_type("vital_timing", "vitaldelaytype01");
  assert(delays.width() == 128);
  assert(delays.vhdl_array);
  assert(delays.vhdl_array->dimensions.size() == 1);
  assert(delays.vhdl_array->dimensions.front().range);
  assert(delays.vhdl_array->dimensions.front().range->left == 0);
  assert(delays.vhdl_array->dimensions.front().range->right == 1);
  const auto map = package_type("vital_timing", "vitalresultmaptype");
  assert(map.width() == 4);
  const auto table = package_type(
      "vital_primitives", "vitaltruthtabletype");
  assert(!table.width());
  assert(table.vhdl_array);
  assert(table.vhdl_array->dimensions.size() == 2);
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-ieee-integration-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto context = directory.path / "00_context.vhd";
  const auto entity = directory.path / "01_entity.vhd";
  const auto architecture = directory.path / "02_architecture.vhd";
  {
    // FSIM-CONFORMANCE CF-VHDL-PACKAGE-001 source=SRC-IEEE-P1076 expectation=execute
    std::ofstream output{context};
    output << R"(
context ieee_all is
  library ieee;
  use ieee.std_logic_1164.all;
  use ieee.std_logic_textio.all;
  use ieee.numeric_bit.all;
  use ieee.numeric_std.all;
  use ieee.fixed_pkg.all;
  use ieee.float_pkg.all;
  use ieee.vital_timing.all;
  use ieee.vital_primitives.all;
end context ieee_all;
)";
    assert(output.good());
  }
  {
    std::ofstream output{entity};
    output << R"(
context work.ieee_all;
entity ieee_integration is
  generic (
    vital_delays_generic : VitalDelayType01 := (1 ns, 2 ns);
    vital_map_generic : VitalResultMapType := VitalDefaultResultMap);
end entity;
)";
    assert(output.good());
  }
  {
    std::ofstream output{architecture};
    output << R"(
context work.ieee_all;
architecture rtl of ieee_integration is
  signal mapped : std_logic_vector(7 downto 0);
  signal numeric_logic : ieee.numeric_std.unsigned(11 downto 0);
  signal numeric_bits : ieee.numeric_bit.unsigned(7 downto 0);
  signal fixed_source : ufixed(1 downto -4);
  signal fixed_rounded : ufixed(1 downto -2);
  signal float_sum : float(8 downto -23);
  signal vital_transition : VitalTransitionType;
  signal vital_delays : VitalDelayType01;
  signal vital_map : VitalResultMapType;
  signal vital_table : VitalTruthTableType(0 to 0, 0 to 1);
  signal vital_buf : std_logic;
  signal vital_inv : std_logic;
  signal vital_and : std_logic;
  signal vital_nand4 : std_logic;
  signal vital_custom_map : std_logic;
  signal vital_generic_map : std_logic;
  signal vital_ident : std_logic;
  signal vital_extended : VitalDelayType01Z;
  signal vital_delay_match : boolean;
  signal vital_bufif_enabled : std_logic;
  signal vital_bufif_disabled : std_logic;
  signal vital_invif_unknown : std_logic;
  signal vital_bufif_mapped : std_logic;
  signal vital_mux_unknown : std_logic;
  signal vital_mux_same : std_logic;
  signal vital_mux4 : std_logic;
  signal vital_decoder4 : std_logic_vector(3 downto 0);
  signal vital_decoder_disabled : std_logic_vector(1 downto 0);
  signal vital_truth_scalar : std_logic;
  signal vital_truth_first : std_logic;
  signal vital_truth_vector : std_logic_vector(1 downto 0);
  signal vital_ascending_data : std_logic_vector(0 to 3);
  signal vital_wide_data : std_logic_vector(0 to 64);
  signal vital_and_ascending : std_logic;
  signal vital_and_wide : std_logic;
  signal vital_and_singleton : std_logic;
  signal vital_and_null : std_logic;
  signal vital_or2 : std_logic;
  signal vital_xor3 : std_logic;
  signal vital_xnor4 : std_logic;
  signal vital_mux8 : std_logic;
  signal vital_decoder8 : std_logic_vector(7 downto 0);
  signal vital_ident_u : std_logic;
  signal vital_ident_dash : std_logic;
  signal vital_mux_ascending : std_logic;
begin
  mapped <= to_x01("ULH-WZ01");
  numeric_logic <= ieee.numeric_std.resize(
      ieee.numeric_std.to_unsigned(3, 8), 12);
  numeric_bits <= ieee.numeric_bit.to_unsigned(5, 8);
  fixed_source <= "000110";
  fixed_rounded <= resize(fixed_source, 1, -2);
  float_sum <= add(to_float(1, 8, 23), to_float(2, 8, 23));
  vital_buf <= VitalBUF('H');
  vital_inv <= VitalINV(Data => 'L');
  vital_and <= VitalAND("11H1");
  vital_nand4 <= VitalNAND4('1', '1', 'H', '1');
  vital_custom_map <= VitalOR2(
      a => '0', b => '0', ResultMap => "10X0");
  vital_generic_map <= VitalAND("11H1", vital_map_generic);
  vital_ident <= VitalIDENT('W');
  vital_delays <= VitalZeroDelay01;
  vital_extended <= VitalExtendToFillDelay(vital_delays_generic);
  vital_delay_match <= VitalCalcDelay(
      'Z', '0', VitalExtendToFillDelay(vital_delays_generic)) = 1 ns;
  vital_bufif_enabled <= VitalBUFIF1('H', '1');
  vital_bufif_disabled <= VitalBUFIF0('1', '1');
  vital_invif_unknown <= VitalINVIF1('0', 'X');
  vital_bufif_mapped <= VitalBUFIF1('1', '0', "UX010");
  vital_mux_unknown <= VitalMUX2('1', '0', 'X');
  vital_mux_same <= VitalMUX2('H', '1', 'X');
  vital_mux4 <= VitalMUX4("1010", "01");
  vital_decoder4 <= VitalDECODER4("10", '1');
  vital_decoder_disabled <= VitalDECODER2('X', '0');
  vital_truth_scalar <= VitalTruthTable(
      (('0', '1'), ('1', '0')), "0");
  vital_truth_first <= VitalTruthTable(
      (('-', '1'), ('0', '0')), "0");
  vital_truth_vector <= VitalTruthTable(
      (('0', '1', 'Z'), ('1', '0', '1')), "1");
  vital_ascending_data <= "11H1";
  vital_wide_data <=
      "11111111111111111111111111111111111111111111111111111111111111111";
  vital_and_ascending <= VitalAND(vital_ascending_data);
  vital_and_wide <= VitalAND(vital_wide_data);
  vital_and_singleton <= VitalAND("H");
  vital_and_null <= VitalAND("");
  vital_or2 <= VitalOR2('0', 'H');
  vital_xor3 <= VitalXOR3('1', '0', '1');
  vital_xnor4 <= VitalXNOR4('1', '0', '1', '0');
  vital_mux8 <= VitalMUX8("01011010", "110");
  vital_decoder8 <= VitalDECODER8("101", '1');
  vital_ident_u <= VitalIDENT('U');
  vital_ident_dash <= VitalIDENT('-');
  vital_mux_ascending <= VitalMUX4(vital_ascending_data, "01");
  package_debug : process
    variable package_local_numeric_logic : ieee.numeric_std.unsigned(7 downto 0)
        := ieee.numeric_std.to_unsigned(9, 8);
    variable package_local_numeric_bits : ieee.numeric_bit.unsigned(7 downto 0)
        := ieee.numeric_bit.to_unsigned(7, 8);
    variable package_local_fixed : ufixed(1 downto -4)
        := to_ufixed(2, 1, -4);
    variable package_local_float : float(8 downto -23)
        := to_float(2, 8, 23);
  begin
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  const std::vector<std::filesystem::path> sources{
      context, entity, architecture};
  const std::vector<std::string> names{
      "ieee_integration.mapped", "ieee_integration.numeric_logic",
      "ieee_integration.numeric_bits", "ieee_integration.fixed_source",
      "ieee_integration.fixed_rounded", "ieee_integration.float_sum",
      "ieee_integration.vital_buf", "ieee_integration.vital_inv",
      "ieee_integration.vital_and", "ieee_integration.vital_nand4",
      "ieee_integration.vital_custom_map",
      "ieee_integration.vital_generic_map",
      "ieee_integration.vital_ident",
      "ieee_integration.vital_extended",
      "ieee_integration.vital_delay_match",
      "ieee_integration.vital_bufif_enabled",
      "ieee_integration.vital_bufif_disabled",
      "ieee_integration.vital_invif_unknown",
      "ieee_integration.vital_bufif_mapped",
      "ieee_integration.vital_mux_unknown",
      "ieee_integration.vital_mux_same",
      "ieee_integration.vital_mux4",
      "ieee_integration.vital_decoder4",
      "ieee_integration.vital_decoder_disabled",
      "ieee_integration.vital_truth_scalar",
      "ieee_integration.vital_truth_first",
      "ieee_integration.vital_truth_vector",
      "ieee_integration.vital_and_ascending",
      "ieee_integration.vital_and_wide",
      "ieee_integration.vital_and_singleton",
      "ieee_integration.vital_and_null",
      "ieee_integration.vital_or2",
      "ieee_integration.vital_xor3",
      "ieee_integration.vital_xnor4",
      "ieee_integration.vital_mux8",
      "ieee_integration.vital_decoder8",
      "ieee_integration.vital_ident_u",
      "ieee_integration.vital_ident_dash",
      "ieee_integration.vital_mux_ascending"};
  const std::vector<std::string> expected{
      "X01XXX01", "000000000011", "00000101", "000110", "0010",
      "01000000010000000000000000000000", "1", "1", "1", "0",
      "X", "1", "W",
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000010"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000010"
      "0000000000000000000000000000000000000000000000000000000000000010",
      "1", "1", "Z", "X", "0", "X", "1", "1", "0100", "00",
      "1", "1", "01", "1", "1", "1", "1", "1", "0", "1", "1",
      "00100000", "U", "-", "1"};
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(directory.path, sources, optimization);
    verify_analysis(config);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter, names);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled, names);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled, names);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    if (reference.values != expected) {
      for (std::size_t index = 0; index < names.size(); ++index) {
        std::cerr << names[index] << "=" << reference.values[index]
                  << " expected " << expected[index] << '\n';
      }
    }
    assert(reference.values == expected);
    assert(reference.values == compiled.values);
    assert(reference.values == warm.values);
    assert((reference.local_values == std::vector<std::string>{
        "00000111", "00001001", "01000000000000000000000000000000",
        "100000"}));
    assert(reference.local_values == compiled.local_values);
    assert(reference.local_values == warm.local_values);
    assert(reference.vcd == compiled.vcd);
    assert(reference.vcd == warm.vcd);
    assert(reference.vcd.find("b01000000010000000000000000000000")
           != std::string::npos);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes > 0);
    assert(compiled.compiled_modules > 0);
    assert(compiled.native_cache.misses == compiled.compiled_modules);
    assert(warm.native_cache.hits == warm.compiled_modules);
#endif
  }

  {
    std::ofstream output{context, std::ios::app};
    output << "-- Task 8 context provenance edit\n";
    assert(output.good());
  }
  const auto edited = run_once(
      make_config(
          directory.path, sources, fsim::project::Optimization::o2),
      fsim::app::SimulationEngine::compiled, names);
  assert(edited.values == expected);
#if defined(FSIM_HAS_LLVM)
  assert(edited.native_cache.misses == edited.compiled_modules);
  assert(edited.native_cache.stores == edited.compiled_modules);
  assert(edited.native_cache.hits == 0);
#endif

  const auto expect_vital_failure = [&](const std::string_view declaration,
                                        const std::string_view assignment,
                                        const std::string_view code) {
    std::ofstream output{architecture};
    output << "context work.ieee_all;\n"
              "architecture rtl of ieee_integration is\n  "
           << declaration
           << "\nbegin\n  " << assignment
           << "\nend architecture;\n";
    assert(output.good());
    output.close();
    fsim::diagnostic::Engine diagnostics;
    const auto rejected = fsim::app::build_project(
        make_config(
            directory.path, sources, fsim::project::Optimization::o0),
        diagnostics);
    assert(!rejected);
    const auto found = std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
          return diagnostic.code == code;
        });
    if (!found) {
      std::cerr << "expected " << code << " for " << assignment << '\n';
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(found);
  };
  expect_vital_failure(
      "signal invalid : std_logic;", "invalid <= VitalBUF();",
      "FSIM-ELAB-VITAL-003");
  expect_vital_failure(
      "signal invalid : std_logic;",
      "invalid <= VitalExtendToFillDelay(\"01\")(0);",
      "FSIM-ELAB-VITAL-006");
  expect_vital_failure(
      "signal invalid : boolean;",
      "invalid <= VitalCalcDelay('1', '0', "
      "VitalExtendToFillDelay((-1 ns, 2 ns))) = 0 ns;",
      "FSIM-ELAB-VITAL-006");
  expect_vital_failure(
      "signal invalid : std_logic;",
      "invalid <= VitalTruthTable((('0')), \"0\");",
      "FSIM-ELAB-VITAL-008");
  expect_vital_failure(
      "signal invalid : std_logic;",
      "invalid <= VitalTruthTable((('Q', '1'), ('0', '0')), \"0\");",
      "FSIM-ELAB-VITAL-009");
  expect_vital_failure(
      "signal table_value : VitalTruthTableType(0 to 0, 0 to 1); "
      "signal invalid : std_logic;",
      "invalid <= VitalTruthTable(table_value, \"0\");",
      "FSIM-ELAB-VITAL-008");
  expect_vital_failure(
      "signal invalid : std_logic;",
      "invalid <= VitalMUX4(\"1010\", \"0\");",
      "FSIM-ELAB-VITAL-007");
  return 0;
}
