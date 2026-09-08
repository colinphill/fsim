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
  assert(std::ranges::all_of(
      checked->vhdl_hir.overload_sets(), [](const auto& overload) {
        return std::ranges::is_sorted(
            overload.declarations,
            [](const auto left, const auto right) {
              return left.value() < right.value();
            });
      }));

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
  assert(repeated->vhdl_hir.overload_sets().size()
         == checked->vhdl_hir.overload_sets().size());
  for (std::size_t index = 0;
       index < checked->vhdl_hir.overload_sets().size(); ++index) {
    const auto& first = checked->vhdl_hir.overload_sets()[index];
    const auto& second = repeated->vhdl_hir.overload_sets()[index];
    assert(first.scope == second.scope);
    assert(first.canonical_name == second.canonical_name);
    assert(first.declarations == second.declarations);
  }
  const auto retained_unit_id = unit->id;
  const auto retained_unit_name = unit->name;
  checked->parsed.units.clear();
  assert(unit->id == retained_unit_id);
  assert(unit->name == retained_unit_name);
  assert(packet_type->record_elements[1].name == "payload");
}

void verify_vhdl_mode_view_hir(
    const std::filesystem::path& directory) {
  const auto source = directory / "vhdl_2019_mode_view_hir.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
package mode_view_types is
  type lane_t is record
    valid : bit;
    data : bit_vector(7 downto 0);
    ready : bit;
  end record;
  type state_t is (idle, busy, done);
  subtype lane_subtype_t is lane_t;
  type pair_t is record
    left : lane_subtype_t;
    right : lane_subtype_t;
  end record;
  type lane_array_t is array (natural range <>) of lane_t;
  type lane_access_t is access lane_t;
  type lane_file_t is file of lane_t;
  subtype lane_index_t is lane_array_t'index;
  subtype lane_designated_t is lane_access_t'designated_subtype;
  subtype lane_file_element_t is lane_file_t'designated_subtype;
  type bus_t is record
    pair : pair_t;
    lanes : lane_array_t(0 to 1);
  end record;
  view producer of lane_t is
    valid, data : out;
    ready : in;
  end view producer;
  alias consumer is producer'converse;
  view pair_view of pair_t is
    left, right : view producer;
  end view pair_view;
  view bus_view of bus_t is
    pair : view pair_view;
    lanes : view (producer);
  end view bus_view;
end package;

use work.mode_view_types.all;
entity mode_view_hir_top is
  port (
    channel : view consumer;
    lanes : view (producer) of lane_array_t(0 to 1)
  );
end entity;
architecture rtl of mode_view_hir_top is begin end architecture;
)";
    assert(output.good());
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-2019-mode-view-hir";
  config.project.top = "vhdl:work.mode_view_hir_top(rtl)";
  config.project.time_resolution = "1ns";
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2019";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  if (!checked) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(checked);
  const auto package = std::ranges::find_if(
      checked->vhdl_hir.units(), [](const auto& unit) {
        return unit.kind == fsim::semantic::vhdl::UnitKind::package
            && unit.name == "mode_view_types";
      });
  assert(package != checked->vhdl_hir.units().end());
  const auto declaration_for = [&](const std::string_view name) {
    return std::ranges::find_if(
        checked->vhdl_hir.declarations(), [&](const auto& declaration) {
          return declaration.scope == package->scope
              && declaration.name == name;
        });
  };
  const auto producer = declaration_for("producer");
  const auto consumer = declaration_for("consumer");
  const auto pair_view = declaration_for("pair_view");
  const auto bus_view = declaration_for("bus_view");
  assert(producer != checked->vhdl_hir.declarations().end());
  assert(consumer != checked->vhdl_hir.declarations().end());
  assert(pair_view != checked->vhdl_hir.declarations().end());
  assert(bus_view != checked->vhdl_hir.declarations().end());
  assert(producer->form
      == fsim::semantic::vhdl::DeclarationForm::mode_view);
  assert(producer->mode_view);
  assert(producer->mode_view->record_subtype.type_mark.spelling
      == "lane_t");
  assert(producer->mode_view->composition
      == fsim::semantic::vhdl::ModeViewCompositionState::complete);
  assert(producer->mode_view->elements.size() == 3);
  assert(producer->mode_view->elements[0].element.canonical == "valid");
  assert(producer->mode_view->elements[0].direction
      == fsim::semantic::vhdl::Direction::output);
  assert(producer->mode_view->elements[1].element.canonical == "data");
  assert(producer->mode_view->elements[1].direction
      == fsim::semantic::vhdl::Direction::output);
  assert(producer->mode_view->elements[2].direction
      == fsim::semantic::vhdl::Direction::input);
  assert(producer->mode_view->elements[0].subtype);
  assert(producer->mode_view->elements[0].subtype->type_mark.spelling
      == "bit");
  assert(consumer->form
      == fsim::semantic::vhdl::DeclarationForm::mode_view);
  assert(consumer->mode_view);
  assert(consumer->mode_view->converse_of);
  assert(consumer->mode_view->converse_of->canonical == "producer");
  assert(consumer->mode_view->composition
      == fsim::semantic::vhdl::ModeViewCompositionState::complete);
  assert(consumer->mode_view->elements.size() == 3);
  assert(consumer->mode_view->elements[0].direction
      == fsim::semantic::vhdl::Direction::input);
  assert(consumer->mode_view->elements[1].direction
      == fsim::semantic::vhdl::Direction::input);
  assert(consumer->mode_view->elements[2].direction
      == fsim::semantic::vhdl::Direction::output);

  const auto type_for = [&](const std::string_view name) {
    return std::ranges::find_if(
        checked->vhdl_hir.types(), [&](const auto& type) {
          return type.name == name;
        });
  };
  const auto state_type = type_for("state_t");
  const auto record_type = type_for("lane_t");
  const auto array_type = type_for("lane_array_t");
  const auto access_type = type_for("lane_access_t");
  const auto file_type = type_for("lane_file_t");
  const auto index_subtype = type_for("lane_index_t");
  const auto designated_subtype = type_for("lane_designated_t");
  const auto file_element_subtype = type_for("lane_file_element_t");
  assert(state_type != checked->vhdl_hir.types().end());
  assert(record_type != checked->vhdl_hir.types().end());
  assert(array_type != checked->vhdl_hir.types().end());
  assert(access_type != checked->vhdl_hir.types().end());
  assert(file_type != checked->vhdl_hir.types().end());
  assert(index_subtype != checked->vhdl_hir.types().end());
  assert(designated_subtype != checked->vhdl_hir.types().end());
  assert(file_element_subtype != checked->vhdl_hir.types().end());
  const auto has_attribute = [](const auto& type, const auto attribute) {
    return std::ranges::find(type.attributes, attribute)
        != type.attributes.end();
  };
  using Attribute = fsim::semantic::vhdl::PredefinedAttribute;
  assert(has_attribute(*state_type, Attribute::length));
  assert(has_attribute(*state_type, Attribute::range));
  assert(has_attribute(*state_type, Attribute::reverse_range));
  assert(has_attribute(*state_type, Attribute::reflect));
  assert(has_attribute(*record_type, Attribute::image));
  assert(has_attribute(*record_type, Attribute::value));
  assert(has_attribute(*record_type, Attribute::reflect));
  assert(has_attribute(*array_type, Attribute::index));
  assert(has_attribute(*array_type, Attribute::image));
  assert(has_attribute(*array_type, Attribute::value));
  assert(has_attribute(*array_type, Attribute::reflect));
  assert(has_attribute(*access_type, Attribute::designated_subtype));
  assert(has_attribute(*file_type, Attribute::designated_subtype));
  assert(index_subtype->base.predefined_attribute == Attribute::index);
  assert(designated_subtype->base.predefined_attribute
      == Attribute::designated_subtype);
  assert(file_element_subtype->base.predefined_attribute
      == Attribute::designated_subtype);


  assert(pair_view->mode_view
      && pair_view->mode_view->composition
          == fsim::semantic::vhdl::ModeViewCompositionState::complete);
  assert(pair_view->mode_view->elements.size() == 2);
  const auto& pair_left = pair_view->mode_view->elements.front();
  assert(pair_left.subtype
      && pair_left.subtype->type_mark.spelling == "lane_subtype_t");
  assert(pair_left.elements.size() == 3);
  assert(pair_left.elements[0].element.canonical == "valid");
  assert(pair_left.elements[0].direction
      == fsim::semantic::vhdl::Direction::output);

  assert(bus_view->mode_view
      && bus_view->mode_view->composition
          == fsim::semantic::vhdl::ModeViewCompositionState::complete);
  assert(bus_view->mode_view->elements.size() == 2);
  const auto& nested_pair = bus_view->mode_view->elements[0];
  assert(nested_pair.form
      == fsim::semantic::vhdl::ModeViewElementForm::record_view);
  assert(nested_pair.referenced_view);
  assert(nested_pair.referenced_view->canonical == "pair_view");
  assert(nested_pair.referenced_view->selected == pair_view->id);
  assert(nested_pair.subtype
      && nested_pair.subtype->type_mark.spelling == "pair_t");
  assert(nested_pair.elements.size() == 2);
  assert(nested_pair.elements[0].elements.size() == 3);
  assert(nested_pair.elements[1].elements[2].direction
      == fsim::semantic::vhdl::Direction::input);
  const auto& nested_array = bus_view->mode_view->elements[1];
  assert(nested_array.form
      == fsim::semantic::vhdl::ModeViewElementForm::array_view);
  assert(nested_array.referenced_view
      && nested_array.referenced_view->selected == producer->id);
  assert(nested_array.subtype
      && nested_array.subtype->type_mark.spelling == "lane_array_t");
  assert(nested_array.elements.size() == 3);
  assert(nested_array.elements[1].direction
      == fsim::semantic::vhdl::Direction::output);

  const auto array_interface = std::ranges::find_if(
      checked->vhdl_hir.declarations(), [](const auto& declaration) {
        return declaration.name == "lanes" && declaration.interface_view;
      });
  assert(array_interface != checked->vhdl_hir.declarations().end());
  assert(array_interface->interface_view->form
      == fsim::semantic::vhdl::ModeViewElementForm::array_view);
  assert(array_interface->interface_view->composition
      == fsim::semantic::vhdl::ModeViewCompositionState::complete);
  assert(array_interface->interface_view->elements.size() == 3);

  const auto record_interface = std::ranges::find_if(
      checked->vhdl_hir.declarations(), [](const auto& declaration) {
        return declaration.name == "channel" && declaration.interface_view;
      });
  assert(record_interface != checked->vhdl_hir.declarations().end());
  assert(record_interface->interface_view->view.canonical == "consumer");
  assert(record_interface->interface_view->composition
      == fsim::semantic::vhdl::ModeViewCompositionState::complete);
  assert(record_interface->interface_view->elements.size() == 3);
  assert(record_interface->interface_view->elements[0].direction
      == fsim::semantic::vhdl::Direction::input);
  assert(record_interface->interface_view->elements[2].direction
      == fsim::semantic::vhdl::Direction::output);

  const auto retained_view = *bus_view->mode_view;
  checked->parsed.units.clear();
  assert(retained_view.elements.size() == 2);
  assert(retained_view.elements.front().elements.size() == 2);
  assert(retained_view.elements.front().elements.front().elements.size()
      == 3);

  const auto invalid_source = directory / "vhdl_2019_mode_view_invalid.vhd";
  {
    std::ofstream output(invalid_source, std::ios::binary);
    output << R"(
package invalid_mode_views is
  type lane_t is record
    valid : bit;
    data : bit;
  end record;
  type other_t is record
    other : bit;
  end record;
  type lane_array_t is array (natural range <>) of lane_t;
  type holder_t is record
    lanes : lane_array_t;
    flag : bit;
  end record;
  view lane_view of lane_t is
    valid : out;
    data : in;
  end view;
  view other_view of other_t is
    other : in;
  end view;
  view incomplete_view of lane_t is
    valid : in;
  end view;
  view wrong_record_view of lane_t is
    valid : view other_view;
    data : in;
  end view;
  view wrong_array_view of holder_t is
    lanes : view (other_view);
    flag : in;
  end view;
end package;

use work.invalid_mode_views.all;
entity invalid_mode_view_top is
  port (channel : view lane_view of other_t);
end entity;
architecture rtl of invalid_mode_view_top is begin end architecture;
)";
    assert(output.good());
  }
  auto invalid_config = config;
  invalid_config.project.name = "vhdl-2019-invalid-mode-views";
  invalid_config.project.top = "vhdl:work.invalid_mode_view_top(rtl)";
  invalid_config.source_sets.front().files = {invalid_source};
  fsim::diagnostic::Engine invalid_diagnostics;
  const auto invalid = fsim::app::check_project(
      invalid_config, invalid_diagnostics);
  assert(!invalid);
  const auto semantic_declaration_errors = std::ranges::count_if(
      invalid_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-VHDL-SEM-110";
      });
  assert(semantic_declaration_errors == 3);
  assert(std::ranges::any_of(
      invalid_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-VHDL-SEM-111";
      }));
}

void verify_vhdl_2019_predefined_attributes(
    const std::filesystem::path& directory) {
  const auto source = directory / "vhdl_2019_predefined_attributes.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity predefined_attribute_top is end entity;
architecture rtl of predefined_attribute_top is
  type matrix_t is array
    (integer range <>, positive range <>) of bit;
  subtype row_index_t is matrix_t'index;
  subtype column_index_t is matrix_t'index(2);
  type integer_pointer_t is access integer;
  type integer_file_t is file of integer;
  subtype pointee_t is integer_pointer_t'designated_subtype;
  subtype file_element_t is integer_file_t'designated_subtype;
  type state_t is (idle, busy, done);
  signal state : state_t := busy;
  signal next_state : state_t := idle;
  signal state_count : integer := 0;
  signal state_position : integer := 0;
begin
  observe : process
  begin
    state_count <= state_t'length;
    state_position <= state'pos;
    next_state <= state'succ;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-2019-predefined-attributes";
  config.project.top = "vhdl:work.predefined_attribute_top(rtl)";
  config.project.time_resolution = "1ns";
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2019";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto built = fsim::app::build_project(config, diagnostics);
  if (!built) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(built);
  fsim::app::Simulation simulation{
      std::move(*built), config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  const auto count = simulation.find_signal(
      "predefined_attribute_top.state_count");
  const auto position = simulation.find_signal(
      "predefined_attribute_top.state_position");
  const auto successor = simulation.find_signal(
      "predefined_attribute_top.next_state");
  assert(count && position && successor);
  const auto result = simulation.run();
  assert(result.status == fsim::runtime::RunStatus::completed);
  assert(simulation.read_signal(*count).to_msb_string().ends_with("0011"));
  assert(simulation.read_signal(*position).to_msb_string().ends_with("0001"));
  assert(simulation.read_signal(*successor).to_msb_string().ends_with("10"));

  const auto invalid_source =
      directory / "vhdl_2019_invalid_predefined_attributes.vhd";
  {
    std::ofstream output(invalid_source, std::ios::binary);
    output << R"(
entity invalid_predefined_attribute_top is end entity;
architecture rtl of invalid_predefined_attribute_top is
  subtype scalar_t is integer range 0 to 7;
  type vector_t is array (integer range <>) of bit;
  subtype scalar_index_t is scalar_t'index;
  subtype bad_dimension_t is vector_t'index(2);
  subtype scalar_designated_t is scalar_t'designated_subtype;
begin
end architecture;
)";
    assert(output.good());
  }
  auto invalid_config = config;
  invalid_config.project.name = "vhdl-2019-invalid-predefined-attributes";
  invalid_config.project.top =
      "vhdl:work.invalid_predefined_attribute_top(rtl)";
  invalid_config.source_sets.front().files = {invalid_source};
  fsim::diagnostic::Engine invalid_diagnostics;
  const auto invalid = fsim::app::build_project(
      invalid_config, invalid_diagnostics);
  assert(!invalid);
  assert(std::ranges::any_of(
      invalid_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VHATTR-009";
      }));
  assert(std::ranges::count_if(
      invalid_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VHATTR-010";
      }) == 2);
}

void verify_unspecified_type_inference(
    const std::filesystem::path& directory) {
  const auto source = directory / "unspecified_type_inference.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity restricted_type_holder is
  generic (type Data_T is range <>);
end entity;

architecture rtl of restricted_type_holder is
begin
end architecture;

entity inferred_pair is
  port (
    Left, Right : in type is range <>;
    Result_Value : out type is range <>
  );
end entity;

architecture rtl of inferred_pair is
begin
  Result_Value <= Left + Right;
end architecture;

entity unspecified_type_inference is
end entity;

architecture rtl of unspecified_type_inference is
  signal observed : integer := 0;
  signal Pair_Left : integer := 2;
  signal Pair_Right : integer := 3;
  signal Pair_Result : integer := 0;

  function Pair(Left, Right : type is range <>) return integer is
  begin
    return Left + Right;
  end function;

  procedure Increment(variable Value : inout type is range <>) is
  begin
    Value := Value + 1;
  end procedure;
begin
  legal_type_actual: entity work.restricted_type_holder(rtl)
    generic map (Data_T => integer);

  inferred_port_types: entity work.inferred_pair(rtl)
    port map (
      Left => Pair_Left,
      Right => Pair_Right,
      Result_Value => Pair_Result
    );

  process
    variable Local : integer := 4;
  begin
    Local := Pair(Local, Local);
    Increment(Local);
    observed <= Local;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-unspecified-type-inference";
  config.project.top = "vhdl:work.unspecified_type_inference(rtl)";
  config.project.time_resolution = "1ns";
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2019";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine check_diagnostics;
  const auto checked = fsim::app::check_project(config, check_diagnostics);
  if (!checked) {
    for (const auto& diagnostic : check_diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(checked);
  const auto inferred = std::ranges::find_if(
      checked->vhdl_hir.expressions(), [](const auto& expression) {
        return expression.kind == fsim::semantic::vhdl::ExpressionKind::call
            && expression.text == "pair";
      });
  assert(inferred != checked->vhdl_hir.expressions().end());
  assert(inferred->unspecified_type_inference_unique);
  assert(inferred->inferred_type_identities.size() == 1);
  assert(inferred->referenced_name &&
         inferred->referenced_name->selected);

  fsim::diagnostic::Engine build_diagnostics;
  auto built = fsim::app::build_project(config, build_diagnostics);
  if (!built) {
    for (const auto& diagnostic : build_diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(built);
  fsim::app::Simulation simulation{
      std::move(*built), config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter};
  const auto observed = simulation.find_signal(
      "unspecified_type_inference.observed");
  const auto pair_result = simulation.find_signal(
      "unspecified_type_inference.pair_result");
  assert(observed && pair_result);
  const auto result = simulation.run();
  assert(result.status == fsim::runtime::RunStatus::completed);
  assert(simulation.read_signal(*observed).to_msb_string().ends_with("1001"));
  assert(simulation.read_signal(*pair_result).to_msb_string().ends_with("0101"));

  const auto invalid_source =
      directory / "invalid_unspecified_type_inference.vhd";
  {
    std::ofstream output(invalid_source, std::ios::binary);
    output << R"(
entity invalid_unspecified_type_inference is
end entity;

architecture rtl of invalid_unspecified_type_inference is
  function Pair(Left, Right : type is private) return integer is
  begin
    return 0;
  end function;
  signal Integer_Value : integer := 1;
  signal Boolean_Value : boolean := true;
  signal Result_Value : integer;
begin
  Result_Value <= Pair(Integer_Value, Boolean_Value);
end architecture;
)";
    assert(output.good());
  }
  auto invalid_config = config;
  invalid_config.project.name = "vhdl-invalid-unspecified-type-inference";
  invalid_config.project.top =
      "vhdl:work.invalid_unspecified_type_inference(rtl)";
  invalid_config.source_sets.front().files = {invalid_source};
  fsim::diagnostic::Engine invalid_diagnostics;
  const auto invalid =
      fsim::app::build_project(invalid_config, invalid_diagnostics);
  assert(!invalid);
  assert(std::ranges::any_of(
      invalid_diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VHUNSPEC-001";
      }));

  const auto invalid_generic_source =
      directory / "invalid_unspecified_type_generic.vhd";
  {
    std::ofstream output(invalid_generic_source, std::ios::binary);
    output << R"(
entity restricted_type_holder is
  generic (type Data_T is range <>);
end entity;

architecture rtl of restricted_type_holder is
begin
end architecture;

entity invalid_unspecified_type_generic is
end entity;

architecture rtl of invalid_unspecified_type_generic is
begin
  invalid_type_actual: entity work.restricted_type_holder(rtl)
    generic map (Data_T => bit_vector(3 downto 0));
end architecture;
)";
    assert(output.good());
  }
  auto invalid_generic_config = config;
  invalid_generic_config.project.name =
      "vhdl-invalid-unspecified-type-generic";
  invalid_generic_config.project.top =
      "vhdl:work.invalid_unspecified_type_generic(rtl)";
  invalid_generic_config.source_sets.front().files =
      {invalid_generic_source};
  fsim::diagnostic::Engine invalid_generic_diagnostics;
  const auto invalid_generic = fsim::app::build_project(
      invalid_generic_config, invalid_generic_diagnostics);
  assert(!invalid_generic);
  assert(std::ranges::any_of(
      invalid_generic_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VHUNSPEC-002";
      }));

  const auto invalid_port_source =
      directory / "invalid_unspecified_port_inference.vhd";
  {
    std::ofstream output(invalid_port_source, std::ios::binary);
    output << R"(
entity inferred_pair is
  port (Left, Right : in type is private);
end entity;

architecture rtl of inferred_pair is
begin
end architecture;

entity invalid_unspecified_port_inference is
end entity;

architecture rtl of invalid_unspecified_port_inference is
  signal Integer_Value : integer := 1;
  signal Boolean_Value : boolean := true;
begin
  conflicting_types: entity work.inferred_pair(rtl)
    port map (Integer_Value, Boolean_Value);
end architecture;
)";
    assert(output.good());
  }
  auto invalid_port_config = config;
  invalid_port_config.project.name =
      "vhdl-invalid-unspecified-port-inference";
  invalid_port_config.project.top =
      "vhdl:work.invalid_unspecified_port_inference(rtl)";
  invalid_port_config.source_sets.front().files = {invalid_port_source};
  fsim::diagnostic::Engine invalid_port_diagnostics;
  const auto invalid_port = fsim::app::build_project(
      invalid_port_config, invalid_port_diagnostics);
  assert(!invalid_port);
  assert(std::ranges::any_of(
      invalid_port_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VHUNSPEC-001";
      }));
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
  verify_unspecified_type_inference(directory.path);
  verify_vhdl_mode_view_hir(directory.path);
  verify_vhdl_2019_predefined_attributes(directory.path);

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
