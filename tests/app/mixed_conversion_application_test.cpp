// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"

#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<std::string> values;
  std::string debug_local;
  std::string vcd;
  std::vector<std::pair<std::string, std::string>> keys;
  fsim::app::NativeCacheStatistics native_cache;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  std::size_t conversion_count{};
  std::size_t adapter_count{};
};

constexpr std::array<std::string_view, 7> signal_names{
    "mixed_conversion_sv_top.width_result",
    "mixed_conversion_sv_top.unsigned_to_signed_result",
    "mixed_conversion_sv_top.signed_to_unsigned_result",
    "mixed_conversion_sv_top.boolean_result",
    "mixed_conversion_sv_top.integer_result",
    "mixed_conversion_sv_top.bit_result",
    "mixed_conversion_sv_top.state_result"};

struct DiagnosticCaptureEntry {
  fsim::diagnostic::Severity severity;
  std::string code;
  std::string message;
  std::string path;
  std::uint32_t begin_line{};
  std::uint32_t begin_column{};
  std::uint64_t begin_offset{};
  std::uint32_t end_line{};
  std::uint32_t end_column{};
  std::uint64_t end_offset{};

  friend bool operator==(
      const DiagnosticCaptureEntry&,
      const DiagnosticCaptureEntry&) = default;
};

using DiagnosticCapture = std::vector<DiagnosticCaptureEntry>;

struct NamedDiagnosticCapture {
  std::string_view name;
  const DiagnosticCapture* diagnostics{};
};

DiagnosticCapture capture_diagnostics(
    const fsim::diagnostic::Engine& diagnostics) {
  DiagnosticCapture capture;
  capture.reserve(diagnostics.diagnostics().size());
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    capture.push_back({
        diagnostic.severity,
        diagnostic.code,
        diagnostic.message,
        diagnostic.span.path,
        diagnostic.span.begin.line,
        diagnostic.span.begin.column,
        diagnostic.span.begin.offset,
        diagnostic.span.end.line,
        diagnostic.span.end.column,
        diagnostic.span.end.offset});
  }
  return capture;
}

bool is_five_path_warning(
    const DiagnosticCapture& diagnostics,
    const std::string_view logical_source) {
  if (diagnostics.size() != 1U) {
    return false;
  }
  const auto& warning = diagnostics.front();
  return warning.severity == fsim::diagnostic::Severity::warning
      && warning.code == "FSIM-ELAB-SVCONST-002"
      && warning.message
          == "$warning during constant evaluation: compiled HIR path=17"
      && warning.path == logical_source
      && warning.begin_line > 0U && warning.begin_column > 0U
      && warning.end_line >= warning.begin_line
      && warning.end_offset >= warning.begin_offset;
}

void assert_five_path_warning_equivalence(
    const std::array<NamedDiagnosticCapture, 5>& captures,
    const std::string_view logical_source) {
  bool valid = true;
  const auto& expected = *captures.front().diagnostics;
  for (const auto& capture : captures) {
    const auto well_formed = is_five_path_warning(
        *capture.diagnostics, logical_source);
    const auto equivalent = *capture.diagnostics == expected;
    valid = well_formed && equivalent && valid;
  }
  if (!valid) {
    for (const auto& capture : captures) {
      std::cerr << capture.name << " diagnostics:\n";
      if (capture.diagnostics->empty()) {
        std::cerr << "  <none>\n";
      }
      for (const auto& diagnostic : *capture.diagnostics) {
        std::cerr << "  severity="
                  << static_cast<unsigned>(diagnostic.severity)
                  << " code=" << diagnostic.code
                  << " message=" << diagnostic.message
                  << " path=" << diagnostic.path
                  << " begin=" << diagnostic.begin_line << ':'
                  << diagnostic.begin_column << ':'
                  << diagnostic.begin_offset
                  << " end=" << diagnostic.end_line << ':'
                  << diagnostic.end_column << ':'
                  << diagnostic.end_offset << '\n';
      }
    }
  }
  assert(valid);
}

void write_leaf(
    const std::filesystem::path& source,
    const bool edited) {
  // FSIM-CONFORMANCE CF-MIX-CONVERSION-001 source=SRC-FSIM expectation=execute
  std::ofstream output{source};
  output << R"(
module mixed_conversion_sv_leaf(
  input logic [7:0] width_value,
  output logic [7:0] width_result,
  input logic signed [7:0] unsigned_to_signed_value,
  output logic signed [7:0] unsigned_to_signed_result,
  input logic [7:0] signed_to_unsigned_value,
  output logic [7:0] signed_to_unsigned_result,
  input logic boolean_value,
  output logic boolean_result,
  input bit signed [31:0] integer_value,
  output logic signed [31:0] integer_result,
  input bit [3:0] bit_value,
  output bit [3:0] bit_result,
  input logic [3:0] state_value,
  output logic [3:0] state_result
);
)";
  output << "  assign width_result = width_value"
         << (edited ? " ^ 8'h01;\n" : ";\n");
  output << R"(
  assign unsigned_to_signed_result = unsigned_to_signed_value;
  assign signed_to_unsigned_result = signed_to_unsigned_value;
  assign boolean_result = boolean_value;
  assign integer_result = integer_value + 1;
  assign bit_result = bit_value;
  assign state_result = state_value;
endmodule
)";
  assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& top_source,
    const std::filesystem::path& middle_source,
    const std::filesystem::path& leaf_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "mixed-conversion-matrix";
  config.project.top = "sv:work.mixed_conversion_sv_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet vhdl_sources;
  vhdl_sources.language = fsim::project::Language::vhdl;
  vhdl_sources.standard = "2008";
  vhdl_sources.library = "work";
  vhdl_sources.compilation_unit = "file";
  vhdl_sources.files.push_back(middle_source);
  config.source_sets.push_back(std::move(vhdl_sources));

  fsim::project::SourceSet sv_sources;
  sv_sources.language = fsim::project::Language::system_verilog;
  sv_sources.standard = "2017";
  sv_sources.library = "work";
  sv_sources.compilation_unit = "file";
  sv_sources.files = {top_source, leaf_source};
  config.source_sets.push_back(std::move(sv_sources));

  config.bindings = {
      {"mixed_conversion_sv_top.middle",
       "vhdl:work.mixed_conversion_vhdl_middle(rtl)",
       std::nullopt},
      {"mixed_conversion_sv_top.middle.leaf",
       "sv:work.mixed_conversion_sv_leaf",
       std::nullopt}};
  return config;
}

Capture capture_project(
    const fsim::project::Config& config,
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine) {
  assert(project.design_ir.valid());
  assert(project.design_ir.valid(project.semantics));
  assert(project.design_ir.specializations().size()
         == project.design.specializations().size());
  assert(project.design_ir.processes().size()
         == project.design.processes().size());
  assert(project.design_ir.conversions().size()
         == project.design.boundary_conversions().size());
  assert(static_cast<std::size_t>(std::ranges::count_if(
      project.design_ir.boundaries(), [](const auto& boundary) {
        return boundary.kind
            == fsim::semantic::design::BoundaryKind::language_conversion;
      })) == project.design.boundary_conversions().size());
  assert(std::ranges::all_of(
      project.design_ir.conversions(), [](const auto& conversion) {
        return conversion.formal.valid() && conversion.actual.valid()
            && conversion.source.has_value();
      }));

  Capture capture;
  assert(
      project.specialization_cache_keys.size()
      == project.design.specializations().size());
  for (std::size_t index = 0;
       index < project.design.specializations().size();
       ++index) {
    capture.keys.emplace_back(
        project.design.specializations()[index].instance,
        project.specialization_cache_keys[index]);
  }

  const auto& conversions = project.design.boundary_conversions();
  capture.conversion_count = conversions.size();
  capture.adapter_count = static_cast<std::size_t>(std::ranges::count_if(
      conversions,
      [](const auto& conversion) {
        return conversion.process.has_value();
      }));
  assert(capture.conversion_count == 28);
  assert(capture.adapter_count == 11);
  for (const auto& conversion : conversions) {
    const auto connection =
        fsim::frontend::physical_source(conversion.connection_span);
    const auto formal =
        fsim::frontend::physical_source(conversion.formal_span);
    const auto actual =
        fsim::frontend::physical_source(conversion.actual_span);
    assert(!conversion.path.empty());
    assert(!connection.empty());
    assert(!formal.empty());
    assert(!actual.empty());
  }

  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> debug_local;
  for (const auto& process : project.design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      if (process.debug_locals[index].name == "boundary_probe") {
        debug_local = std::pair{process.id, index};
      }
    }
  }
  assert(debug_local);

  fsim::app::Simulation simulation{
      std::move(project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.native_cache = simulation.native_cache_statistics();

  std::array<
      fsim::runtime::simir::SignalId,
      signal_names.size()> signals{};
  std::array<
      fsim::runtime::VcdSignal,
      signal_names.size()> traces{};
  constexpr std::array<std::size_t, signal_names.size()> widths{
      8, 8, 8, 1, 32, 4, 4};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0; index < signal_names.size(); ++index) {
    const auto signal = simulation.find_signal(signal_names[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{signal_names[index]}, widths[index]);
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
        const auto found =
            std::find(signals.begin(), signals.end(), signal);
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
  capture.debug_local = simulation.read_process_local(
      debug_local->first, debug_local->second).to_msb_string();
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(project && !diagnostics.has_error());
  return capture_project(config, std::move(*project), engine);
}

void compare_captures(
    const Capture& reference,
    const Capture& compiled) {
  assert(reference.result.status == compiled.result.status);
  assert(reference.result.time == compiled.result.time);
  assert(reference.result.delta == compiled.result.delta);
  assert(reference.values == compiled.values);
  assert(reference.debug_local == compiled.debug_local);
  assert(reference.vcd == compiled.vcd);
  assert(reference.keys == compiled.keys);
  assert(reference.conversion_count == compiled.conversion_count);
  assert(reference.adapter_count == compiled.adapter_count);
}

void verify_capture(const Capture& capture, const bool edited) {
  assert(capture.result.status == fsim::runtime::RunStatus::stopped);
  assert(capture.result.time == 1);
  assert((capture.values == std::vector<std::string>{
      edited ? "00001011" : "00001010",
      "00001010",
      "11111010",
      "1",
      "11111111111111111111111111111111",
      "0110",
      "01XZ"}));
  assert(capture.debug_local == "01XZ");
  assert(capture.vcd.find("b01xz") != std::string::npos);
  assert(capture.vcd.find("$enddefinitions $end") != std::string::npos);
}

void run_five_path_compiled_hir_differential(
    const std::filesystem::path& directory,
    const std::filesystem::path& top_source,
    const std::filesystem::path& middle_source,
    const std::filesystem::path& leaf_source) {
  auto source_config = make_config(
      directory,
      top_source,
      middle_source,
      leaf_source,
      fsim::project::Optimization::o0);
  source_config.project.name = "compiled-hir-five-path-differential";
  source_config.build.cache_path = directory / "five-path-source-cache";

  fsim::diagnostic::Engine direct_diagnostics;
  auto direct_project = fsim::app::build_project(
      source_config, direct_diagnostics);
  if (!direct_project || direct_diagnostics.has_error()) {
    fsim::diagnostic::print_text(std::cerr, direct_diagnostics);
  }
  assert(
      direct_project && !direct_diagnostics.has_error()
      && !direct_project->cache_hit);
  const auto direct_diagnostic_capture = capture_diagnostics(
      direct_diagnostics);
  const auto direct = capture_project(
      source_config,
      std::move(*direct_project),
      fsim::app::SimulationEngine::interpreter);

  fsim::diagnostic::Engine warm_diagnostics;
  auto warm_project = fsim::app::build_project(
      source_config, warm_diagnostics);
  if (!warm_project || warm_diagnostics.has_error()) {
    fsim::diagnostic::print_text(std::cerr, warm_diagnostics);
  }
  assert(
      warm_project && !warm_diagnostics.has_error()
      && warm_project->cache_hit);
  const auto warm_diagnostic_capture = capture_diagnostics(
      warm_diagnostics);
  const auto warm = capture_project(
      source_config,
      std::move(*warm_project),
      fsim::app::SimulationEngine::interpreter);

  const auto vhdl_object = directory / "five-path-vhdl.fsimobj";
  const auto sv_object = directory / "five-path-sv.fsimobj";
  const auto publish_object = [&](const std::size_t source_set,
                                  const std::filesystem::path& object,
                                  const std::string_view cache_name) {
    auto publish_config = source_config;
    publish_config.source_sets = {
        source_config.source_sets.at(source_set)};
    publish_config.build.cache_path = directory / cache_name;
    fsim::diagnostic::Engine diagnostics;
    const auto published = fsim::app::compile_artifact(
        publish_config, object, diagnostics);
    if (!published || diagnostics.has_error()) {
      fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(published && diagnostics.empty());
  };
  publish_object(
      0U, vhdl_object, "five-path-vhdl-object-publish-cache");
  publish_object(
      1U, sv_object, "five-path-sv-object-publish-cache");

  auto object_config = source_config;
  object_config.source_sets.clear();
  object_config.build.cache_path = directory / "five-path-object-cache";
  const std::array objects { vhdl_object, sv_object };
  fsim::diagnostic::Engine object_diagnostics;
  auto object_project = fsim::app::build_objects(
      object_config, objects, object_diagnostics);
  if (!object_project || object_diagnostics.has_error()) {
    fsim::diagnostic::print_text(std::cerr, object_diagnostics);
  }
  assert(
      object_project && !object_diagnostics.has_error()
      && !object_project->cache_hit);
  const auto object_diagnostic_capture = capture_diagnostics(
      object_diagnostics);
  const auto explicit_object = capture_project(
      object_config,
      std::move(*object_project),
      fsim::app::SimulationEngine::interpreter);

  const auto library = directory / "five-path.fsimlib";
  auto library_publish_config = source_config;
  library_publish_config.build.cache_path
      = directory / "five-path-library-publish-cache";
  fsim::diagnostic::Engine library_publish_diagnostics;
  const auto library_published = fsim::app::export_library(
      library_publish_config,
      "work",
      library,
      library_publish_diagnostics);
  if (!library_published || library_publish_diagnostics.has_error()) {
    fsim::diagnostic::print_text(
        std::cerr, library_publish_diagnostics);
  }
  assert(library_published && library_publish_diagnostics.empty());

  auto mapped_config = source_config;
  mapped_config.source_sets.clear();
  mapped_config.build.cache_path = directory / "five-path-mapped-cache";
  mapped_config.library_mappings.push_back({ "work", library });
  fsim::diagnostic::Engine mapped_diagnostics;
  auto mapped_project = fsim::app::build_project(
      mapped_config, mapped_diagnostics);
  if (!mapped_project || mapped_diagnostics.has_error()) {
    fsim::diagnostic::print_text(std::cerr, mapped_diagnostics);
  }
  assert(
      mapped_project && !mapped_diagnostics.has_error()
      && !mapped_project->cache_hit);
  const auto mapped_diagnostic_capture = capture_diagnostics(
      mapped_diagnostics);
  const auto mapped_library = capture_project(
      mapped_config,
      std::move(*mapped_project),
      fsim::app::SimulationEngine::interpreter);

  const auto design = directory / "five-path.fsimdesign";
  auto design_config = object_config;
  design_config.build.cache_path = directory / "five-path-design-cache";
  fsim::diagnostic::Engine design_publish_diagnostics;
  const auto design_published = fsim::app::elaborate_artifact(
      design_config,
      objects,
      design,
      design_publish_diagnostics);
  if (!design_published || design_publish_diagnostics.has_error()) {
    fsim::diagnostic::print_text(
        std::cerr, design_publish_diagnostics);
  }
  assert(design_published && !design_publish_diagnostics.has_error());
  const auto design_diagnostic_capture = capture_diagnostics(
      design_publish_diagnostics);
  const std::array diagnostics_by_path {
      NamedDiagnosticCapture { "direct", &direct_diagnostic_capture },
      NamedDiagnosticCapture { "warm-cache", &warm_diagnostic_capture },
      NamedDiagnosticCapture { "object", &object_diagnostic_capture },
      NamedDiagnosticCapture {
          "mapped-library", &mapped_diagnostic_capture },
      NamedDiagnosticCapture {
          "design-artifact", &design_diagnostic_capture },
  };
  assert_five_path_warning_equivalence(
      diagnostics_by_path, "top.sv");

  fsim::diagnostic::Engine design_diagnostics;
  auto design_project = fsim::app::load_design_artifact(
      design, design_diagnostics);
  if (!design_project || design_diagnostics.has_error()) {
    fsim::diagnostic::print_text(std::cerr, design_diagnostics);
  }
  assert(design_project && design_diagnostics.empty());
  design_project->cache_path = directory / "five-path-design-run-cache";
  const auto design_artifact = capture_project(
      design_config,
      std::move(*design_project),
      fsim::app::SimulationEngine::interpreter);

  verify_capture(direct, false);
  compare_captures(direct, warm);
  compare_captures(direct, explicit_object);
  compare_captures(direct, mapped_library);
  compare_captures(direct, design_artifact);
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-mixed-conversions-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);

  const auto top_source = directory.path / "top.sv";
  {
    std::ofstream output{top_source};
    output << R"(`line 1 "top.sv" 0
module mixed_conversion_sv_top;
  function automatic int announce_compiled_hir_path(input int value);
    $warning("compiled HIR path=%0d", value);
    return value;
  endfunction
  localparam int COMPILED_HIR_PATH = announce_compiled_hir_path(17);

  logic [3:0] width_value;
  logic [7:0] width_result;
  logic [3:0] unsigned_to_signed_value;
  logic signed [7:0] unsigned_to_signed_result;
  logic signed [3:0] signed_to_unsigned_value;
  logic [7:0] signed_to_unsigned_result;
  logic boolean_value;
  logic boolean_result;
  bit signed [31:0] integer_value;
  logic signed [31:0] integer_result;
  bit [3:0] bit_value;
  bit [3:0] bit_result;
  logic [3:0] state_value;
  logic [3:0] state_result;

  assign width_value = 4'b1010;
  assign unsigned_to_signed_value = 4'b1010;
  assign signed_to_unsigned_value = 4'b1010;
  assign boolean_value = 1'b1;
  assign integer_value = -2;
  assign bit_value = 4'b0110;
  assign state_value = 4'b01xz;

  mixed_conversion_vhdl_middle middle(
    .width_value(width_value),
    .width_result(width_result),
    .unsigned_to_signed_value(unsigned_to_signed_value),
    .unsigned_to_signed_result(unsigned_to_signed_result),
    .signed_to_unsigned_value(signed_to_unsigned_value),
    .signed_to_unsigned_result(signed_to_unsigned_result),
    .boolean_value(boolean_value),
    .boolean_result(boolean_result),
    .integer_value(integer_value),
    .integer_result(integer_result),
    .bit_value(bit_value),
    .bit_result(bit_result),
    .state_value(state_value),
    .state_result(state_result)
  );

  initial #1 $finish;
endmodule
)";
    assert(output.good());
  }

  const auto middle_source = directory.path / "middle.vhd";
  {
    std::ofstream output{middle_source};
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity Mixed_Conversion_Vhdl_Middle is
  port (
    Width_Value : in unsigned(7 downto 0);
    Width_Result : out unsigned(7 downto 0);
    Unsigned_To_Signed_Value : in signed(7 downto 0);
    Unsigned_To_Signed_Result : out signed(7 downto 0);
    Signed_To_Unsigned_Value : in unsigned(7 downto 0);
    Signed_To_Unsigned_Result : out unsigned(7 downto 0);
    Boolean_Value : in boolean;
    Boolean_Result : out boolean;
    Integer_Value : in integer range -8 to 7;
    Integer_Result : out integer range -8 to 7;
    Bit_Value : in bit_vector(3 downto 0);
    Bit_Result : out bit_vector(3 downto 0);
    State_Value : in std_ulogic_vector(3 downto 0);
    State_Result : out std_logic_vector(3 downto 0)
  );
end entity;

architecture rtl of Mixed_Conversion_Vhdl_Middle is
begin
  leaf : mixed_conversion_sv_leaf
    port map (
      width_value => Width_Value,
      width_result => Width_Result,
      unsigned_to_signed_value => Unsigned_To_Signed_Value,
      unsigned_to_signed_result => Unsigned_To_Signed_Result,
      signed_to_unsigned_value => Signed_To_Unsigned_Value,
      signed_to_unsigned_result => Signed_To_Unsigned_Result,
      boolean_value => Boolean_Value,
      boolean_result => Boolean_Result,
      integer_value => Integer_Value,
      integer_result => Integer_Result,
      bit_value => Bit_Value,
      bit_result => Bit_Result,
      state_value => State_Value,
      state_result => State_Result
    );

  probe : process(State_Value)
    variable Boundary_Probe : std_logic_vector(3 downto 0);
  begin
    Boundary_Probe := std_logic_vector(State_Value);
  end process;
end architecture;
)";
    assert(output.good());
  }

  const auto leaf_source = directory.path / "leaf.sv";
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    write_leaf(leaf_source, false);
    const auto config = make_config(
        directory.path,
        top_source,
        middle_source,
        leaf_source,
        optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference, false);
    verify_capture(cold, false);
    verify_capture(warm, false);
    compare_captures(reference, cold);
    compare_captures(reference, warm);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.compiled_modules > 0);
    assert(cold.native_cache.hits == 0);
    assert(cold.native_cache.misses == cold.compiled_modules);
    assert(cold.native_cache.stores == cold.compiled_modules);
    assert(warm.native_cache.hits == warm.compiled_modules);
    assert(warm.native_cache.misses == 0);
#endif

    write_leaf(leaf_source, true);
    const auto edited_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto edited = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify_capture(edited_reference, true);
    verify_capture(edited, true);
    compare_captures(edited_reference, edited);
    assert(edited.keys != cold.keys);
#if defined(FSIM_HAS_LLVM)
    assert(edited.native_cache.misses > 0);
    assert(edited.native_cache.stores == edited.native_cache.misses);
#endif
  }

  write_leaf(leaf_source, false);
  run_five_path_compiled_hir_differential(
      directory.path,
      top_source,
      middle_source,
      leaf_source);

  std::cout << "mixed conversion application tests passed\n";
  return 0;
}
