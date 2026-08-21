// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "path_test_support.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
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

struct Capture {
  fsim::runtime::RunResult result;
  std::array<std::string, 3> values;
  std::vector<std::pair<std::string, std::string>>
      specialization_keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& child_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-type-generic";
  config.project.top = "vhdl:work.type_generic_top(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {child_source, top_source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view word_value) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  assert(project->design.specializations().size() == 4);
  constexpr std::array<std::string_view, 3> internal_paths{
      "type_generic_top.word_copy.local_value",
      "type_generic_top.packet_copy.local_value",
      "type_generic_top.bit_copy.local_value"};
  for (const auto path : internal_paths) {
    const auto signal = project->design.find_signal(path);
    assert(signal);
    const auto& info = project->design.signals().at(*signal);
    assert(fsim::test::same_source_path(
        fsim::frontend::physical_source(info.declaration_span),
        config.source_sets.front().files.front()));
  }

  Capture capture;
  assert(
      project->specialization_cache_keys.size()
      == project->design.specializations().size());
  for (std::size_t index = 0;
       index < project->specialization_cache_keys.size();
       ++index) {
    capture.specialization_keys.emplace_back(
        project->design.specializations()[index].instance,
        project->specialization_cache_keys[index]);
  }
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 3> input_paths{
      "type_generic_top.word_input",
      "type_generic_top.packet_input",
      "type_generic_top.bit_input"};
  constexpr std::array<std::string_view, 3> output_paths{
      "type_generic_top.word_output",
      "type_generic_top.packet_output",
      "type_generic_top.bit_output"};
  const std::array<std::string_view, 3> input_values{
      word_value, "1101", "1"};
  std::array<fsim::runtime::simir::SignalId, 3> outputs{};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    const auto input = simulation.find_signal(input_paths[index]);
    const auto output = simulation.find_signal(output_paths[index]);
    assert(input && output);
    simulation.deposit_signal(
        *input,
        fsim::runtime::PackedLogic4::from_msb_string(
            input_values[index]));
    outputs[index] = *output;
  }

  capture.result = simulation.run();
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(outputs[index]).to_msb_string();
  }
  return capture;
}

void verify_capture(
    const Capture& capture,
    const std::string_view word_value) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 0);
  assert((
      capture.values
      == std::array<std::string, 3>{
          std::string{word_value}, "1101", "1"}));
  assert(capture.specialization_keys.size() == 4);
}

void verify_vhdl_hir(const fsim::project::Config& config) {
  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  assert(checked);
  const auto unit = std::ranges::find_if(
      checked->vhdl_hir.units(), [](const auto& candidate) {
        return candidate.kind
                   == fsim::semantic::vhdl::UnitKind::architecture
            && candidate.library == "work"
            && candidate.name == "rtl"
            && candidate.primary_name == "type_generic_top";
      });
  assert(unit != checked->vhdl_hir.units().end());
  const auto declaration_for = [&](const std::string_view name) {
    return std::ranges::find_if(
        checked->vhdl_hir.declarations(), [&](const auto& declaration) {
          return declaration.scope == unit->scope
              && declaration.name == name;
        });
  };
  const auto word = declaration_for("word_t");
  const auto packet = declaration_for("packet_t");
  assert(word != checked->vhdl_hir.declarations().end());
  assert(packet != checked->vhdl_hir.declarations().end());
  assert(word->form == fsim::semantic::vhdl::DeclarationForm::subtype);
  assert(packet->form == fsim::semantic::vhdl::DeclarationForm::type);
  assert(word->declared_type && packet->declared_type);
  const auto packet_type = std::ranges::find_if(
      checked->vhdl_hir.types(), [&](const auto& type) {
        return type.id == *packet->declared_type;
      });
  assert(packet_type != checked->vhdl_hir.types().end());
  assert(packet_type->form == fsim::semantic::vhdl::TypeForm::record);
  assert(packet_type->record_elements.size() == 2);
  assert(packet_type->record_elements[0].name == "valid");
  assert(packet_type->record_elements[1].name == "payload");
  assert(!checked->vhdl_hir.overload_sets().empty());

  fsim::diagnostic::Engine repeated_diagnostics;
  const auto repeated = fsim::app::check_project(config, repeated_diagnostics);
  assert(repeated);
  const auto repeated_unit = std::ranges::find_if(
      repeated->vhdl_hir.units(), [&](const auto& candidate) {
        return candidate.library == unit->library
            && candidate.name == unit->name
            && candidate.primary_name == unit->primary_name;
      });
  assert(repeated_unit != repeated->vhdl_hir.units().end());
  assert(repeated_unit->id == unit->id);
  assert(repeated_unit->scope == unit->scope);
  const auto retained_unit_id = unit->id;
  const auto retained_unit_name = unit->name;
  checked->parsed.units.clear();
  assert(unit->id == retained_unit_id);
  assert(unit->name == retained_unit_name);
  assert(packet_type->record_elements[1].name == "payload");
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-type-generic-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto child_source = directory.path / "generic_copy.vhd";
  const auto top_source = directory.path / "type_generic_top.vhd";
  {
    std::ofstream output(child_source, std::ios::binary);
    output << R"(
entity generic_copy is
  generic (type Data_T);
  port (
    input_value : in Data_T;
    output_value : out Data_T
  );
end entity;

architecture rtl of generic_copy is
  signal local_value : Data_T;
begin
  local_value <= input_value;
  output_value <= local_value;
end architecture;
)";
    assert(output.good());
  }

  const auto write_top =
      [&](const std::int64_t left) {
        std::ofstream output(top_source, std::ios::binary);
        output << R"(
entity type_generic_top is
end entity;

architecture rtl of type_generic_top is
  subtype Word_T is bit_vector()"
               << left << R"( downto 0);
  type Packet_T is record
    valid : bit;
    payload : bit_vector(2 downto 0);
  end record;
  signal word_input : Word_T;
  signal word_output : Word_T;
  signal packet_input : Packet_T;
  signal packet_output : Packet_T;
  signal bit_input : bit;
  signal bit_output : bit;
begin
  word_copy: entity work.generic_copy(rtl)
    generic map (bit_vector()"
               << left << R"( downto 0))
    port map (
      input_value => word_input,
      output_value => word_output
    );
  packet_copy: entity work.generic_copy(rtl)
    generic map (Data_T => Packet_T)
    port map (
      input_value => packet_input,
      output_value => packet_output
    );
  bit_copy: entity work.generic_copy(rtl)
    generic map (bit)
    port map (
      input_value => bit_input,
      output_value => bit_output
    );
end architecture;
)";
        assert(output.good());
      };

  write_top(3);
  verify_vhdl_hir(make_config(
      directory.path,
      child_source,
      top_source,
      fsim::project::Optimization::o0));

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_top(3);
    const auto config =
        make_config(
            directory.path,
            child_source,
            top_source,
            optimization);
    const auto reference =
        run_once(
            config,
            fsim::app::SimulationEngine::interpreter,
            "1010");
    const auto cold =
        run_once(
            config,
            fsim::app::SimulationEngine::compiled,
            "1010");
    const auto warm =
        run_once(
            config,
            fsim::app::SimulationEngine::compiled,
            "1010");
    verify_capture(reference, "1010");
    verify_capture(cold, "1010");
    verify_capture(warm, "1010");
    assert(reference.values == cold.values);
    assert(reference.values == warm.values);
    assert(reference.specialization_keys == cold.specialization_keys);
    assert(cold.specialization_keys == warm.specialization_keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 6);
    assert(cold.compiled_modules == 3);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 3);
    assert(cold.cache.stores == 3);
    assert(warm.cache.hits == 3);
    assert(warm.cache.misses == 0);

    write_top(7);
    const auto changed_reference =
        run_once(
            config,
            fsim::app::SimulationEngine::interpreter,
            "10100101");
    const auto changed =
        run_once(
            config,
            fsim::app::SimulationEngine::compiled,
            "10100101");
    verify_capture(changed_reference, "10100101");
    verify_capture(changed, "10100101");
    assert(changed_reference.values == changed.values);
    const auto key_for =
        [](const Capture& capture,
           const std::string_view instance) {
          const auto found = std::ranges::find_if(
              capture.specialization_keys,
              [&](const auto& item) {
                return item.first == instance;
              });
          assert(found != capture.specialization_keys.end());
          return found->second;
        };
    assert(
        key_for(cold, "type_generic_top.word_copy")
        != key_for(changed, "type_generic_top.word_copy"));
    assert(
        key_for(cold, "type_generic_top.packet_copy")
        == key_for(changed, "type_generic_top.packet_copy"));
    assert(
        key_for(cold, "type_generic_top.bit_copy")
        == key_for(changed, "type_generic_top.bit_copy"));
    // The changed word specialization shares a native module with one peer,
    // while the independent bit specialization remains reusable.
    assert(changed.cache.hits == 1);
    assert(changed.cache.misses == 2);
    assert(changed.cache.stores == 2);
#else
    assert(cold.compiled_processes == 0);
    assert(cold.compiled_modules == 0);
#endif
  }
  return 0;
}
