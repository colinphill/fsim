// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_class_simulation_integration() {
  auto config = base_config();
  config.project.name = "class-simulation-integration";
  config.project.top = "sv:work.class_top";
  config.build.cache_path = directory / "class-simulation-cache";
  config.source_sets.clear();
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(class_source);
  config.source_sets.push_back(std::move(sources));

  for (const auto engine : {
           fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::compiled,
           fsim::app::SimulationEngine::debug}) {
    fsim::diagnostic::Engine diagnostics;
    auto built = fsim::app::build_project(config, diagnostics);
    assert(built);
    const auto derived = std::ranges::find_if(
        built->systemverilog_class_specializations,
        [](const auto& specialization) {
          return specialization.declaration_identity.ends_with(
              "::AppDerived");
        });
    assert(derived != built->systemverilog_class_specializations.end());
    const auto base = std::ranges::find_if(
        built->systemverilog_class_specializations,
        [](const auto& specialization) {
          return specialization.declaration_identity.ends_with("::AppBase");
        });
    assert(base != built->systemverilog_class_specializations.end());
    const auto derived_method = std::ranges::find(
        derived->methods,
        std::string{"bump"},
        &fsim::frontend::SystemVerilogClassMethodProfile::name);
    assert(derived_method != derived->methods.end());
    assert(derived_method->virtual_slot);
    const auto base_method_profile = std::ranges::find(
        base->methods,
        std::string{"bump"},
        &fsim::frontend::SystemVerilogClassMethodProfile::name);
    assert(base_method_profile != base->methods.end());
    const auto derived_specialization = derived->specialization_identity;
    const auto derived_identity = derived->declaration_identity;
    const auto base_identity = base->declaration_identity;
    const auto derived_method_identity = derived_method->canonical_identity;
    const auto base_method_identity = base_method_profile->canonical_identity;
    if (engine == fsim::app::SimulationEngine::interpreter) {
      fsim::diagnostic::Engine class_state_diagnostics;
      const auto class_state = fsim::app::serialize_class_state(
          built->systemverilog_class_specializations,
          class_state_diagnostics);
      assert(class_state);
      const auto restored = fsim::app::deserialize_class_state(
          *class_state, "classes.bin", class_state_diagnostics);
      assert(restored);
      assert(
          restored->size()
          == built->systemverilog_class_specializations.size());
      auto trailing = *class_state;
      trailing.push_back('\0');
      fsim::diagnostic::Engine trailing_diagnostics;
      assert(!fsim::app::deserialize_class_state(
          trailing, "trailing-classes.bin", trailing_diagnostics));
      auto future = *class_state;
      future[8] = static_cast<char>(fsim::app::kClassStateSchema + 1U);
      fsim::diagnostic::Engine future_diagnostics;
      assert(!fsim::app::deserialize_class_state(
          future, "future-classes.bin", future_diagnostics));
    }
    const auto virtual_slot = derived_method->virtual_slot;

    fsim::app::Simulation simulation(
        std::move(*built), config.run.max_deltas, engine);
    const auto handle = simulation.allocate_class(
        derived_specialization,
        base_identity);
    simulation.deposit_class_property(
        handle,
        base_identity + "::value",
        fsim::runtime::PackedLogic4::from_aval_bval(8, 0, 0));
    simulation.deposit_class_property(
        handle,
        derived_identity + "::value",
        fsim::runtime::PackedLogic4::from_aval_bval(8, 0, 0));
    auto& shared = simulation.class_static_store()
                       .property(base_identity, "shared")
                       .packed;
    assert(shared.low_word().aval == 2);
    shared = fsim::runtime::PackedLogic4::from_aval_bval(64, 5, 0);

    fsim::runtime::SystemVerilogClassMethodDescriptor base_method;
    base_method.canonical_identity = base_method_identity;
    base_method.owner_type = base_identity;
    base_method.arguments = {
        fsim::runtime::SystemVerilogClassArgumentMode::Input};
    base_method.virtual_slot = *virtual_slot;
    base_method.entry = [](auto& frame) {
      const auto amount = frame.argument(0).packed.low_word().aval;
      auto& value = frame.property("value").packed;
      value = fsim::runtime::PackedLogic4::from_aval_bval(
          8, value.low_word().aval + amount, 0);
      return fsim::runtime::SystemVerilogClassMethodStatus::Completed;
    };
    simulation.class_methods().register_method(std::move(base_method));
    fsim::runtime::SystemVerilogClassMethodDescriptor derived_override;
    derived_override.canonical_identity = derived_method_identity;
    derived_override.owner_type = derived_identity;
    derived_override.arguments = {
        fsim::runtime::SystemVerilogClassArgumentMode::Input};
    derived_override.virtual_slot = *virtual_slot;
    derived_override.entry = [](auto& frame) {
      const auto amount = frame.argument(0).packed.low_word().aval;
      auto& value = frame.property("value").packed;
      value = fsim::runtime::PackedLogic4::from_aval_bval(
          8, value.low_word().aval + amount + 1, 0);
      return fsim::runtime::SystemVerilogClassMethodStatus::Completed;
    };
    simulation.class_methods().register_method(
        std::move(derived_override));

    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>>
        property_changes;
    simulation.set_class_property_change_hook(
        [&](const auto changed_handle,
            const std::string_view property,
            const auto& value,
            const auto time,
            const auto delta) {
          assert(changed_handle == handle);
          assert(property.ends_with("::value"));
          property_changes.emplace_back(
              time, delta, value.to_msb_string());
        });
    std::vector<fsim::runtime::SystemVerilogClassMethodValue> actuals(1);
    actuals.front().packed =
        fsim::runtime::PackedLogic4::from_aval_bval(32, 3, 0);
    bool completion_called = false;
    std::vector<fsim::runtime::SystemVerilogClassMethodValue> base_actuals(1);
    base_actuals.front().packed =
        fsim::runtime::PackedLogic4::from_aval_bval(32, 2, 0);
    simulation.schedule_class_method(
        0, 6, base_method_identity, handle, std::move(base_actuals));
    simulation.schedule_class_method(
        1,
        7,
        {},
        handle,
        std::move(actuals),
        virtual_slot,
        [&](const auto& result, const auto& retained_actuals) {
          assert(
              result.status
              == fsim::runtime::SystemVerilogClassMethodStatus::Completed);
          assert(retained_actuals.size() == 1);
          completion_called = true;
        });
    const auto result = simulation.run();
    assert(result.time == 3);
    assert(completion_called);
    assert(
        simulation.read_class_property(handle, "value")
                .packed.low_word().aval == 4);
    assert(
        simulation.read_class_property(handle, base_identity + "::value")
                .packed.low_word().aval == 2);
    assert(
        simulation.class_static_store()
                .property(derived_identity, "shared")
                .packed.low_word().aval == 5);
    const std::vector<
        std::tuple<std::uint64_t, std::uint64_t, std::string>>
        expected_changes{
            {0, 0, "00000010"},
            {1, 0, "00000100"}};
    assert(property_changes == expected_changes);
    std::ostringstream debug_output;
    std::ostringstream debug_error;
    fsim::app::DebuggerControl debugger(
        simulation, debug_output, debug_error);
    debugger.execute({"classes"});
    debugger.execute({"class", std::to_string(handle), "value"});
    assert(debug_error.str().empty());
    assert(debug_output.str().find(derived_identity) != std::string::npos);
    assert(debug_output.str().find("value = 00000100") != std::string::npos);
  }

  const auto object = directory / "class-object.fsimobj";
  const auto library = directory / "class-library.fsimlib";
  const auto design = directory / "class-design.fsimdesign";
  fsim::diagnostic::Engine artifact_diagnostics;
  assert(fsim::app::compile_artifact(config, object, artifact_diagnostics));
  assert(fsim::app::export_library(
      config, "work", library, artifact_diagnostics));
  const std::vector<std::filesystem::path> objects{object};
  auto artifact_config = config;
  artifact_config.source_sets.clear();
  artifact_config.build.cache_path = directory / "class-artifact-cache";
  const auto artifact_elaborated = fsim::app::elaborate_artifact(
      artifact_config, objects, design, artifact_diagnostics);
  if (!artifact_elaborated) {
    for (const auto& diagnostic : artifact_diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(artifact_elaborated);
  assert(!artifact_diagnostics.has_error());

  const auto hidden_source = class_source.string() + ".hidden";
  const auto hidden_object = object.string() + ".hidden";
  std::filesystem::rename(class_source, hidden_source);
  std::filesystem::rename(object, hidden_object);
  fsim::diagnostic::Engine standalone_diagnostics;
  auto standalone = fsim::app::load_design_artifact(
      design, standalone_diagnostics);
  assert(standalone);
  assert(standalone->systemverilog_class_specializations.size() == 2);
  fsim::app::Simulation standalone_simulation(
      std::move(*standalone), config.run.max_deltas,
      fsim::app::SimulationEngine::interpreter);
  const auto standalone_derived = std::ranges::find_if(
      standalone_simulation.class_specializations(),
      [](const auto& specialization) {
        return specialization.declaration_identity.ends_with("::AppDerived");
      });
  assert(
      standalone_derived
      != standalone_simulation.class_specializations().end());
  const auto standalone_handle = standalone_simulation.allocate_class(
      standalone_derived->specialization_identity);
  assert(
      standalone_simulation.read_class_property(
          standalone_handle, "value").packed.to_msb_string()
      == "XXXXXXXX");

  auto mapped_config = config;
  mapped_config.project.name = "mapped-class-simulation";
  mapped_config.source_sets.clear();
  mapped_config.library_mappings = {{"work", library}};
  mapped_config.elaboration.search_libraries = {"work"};
  mapped_config.build.cache_path = directory / "mapped-class-cache";
  fsim::diagnostic::Engine mapped_diagnostics;
  auto mapped = fsim::app::build_project(mapped_config, mapped_diagnostics);
  assert(mapped);
  assert(mapped->systemverilog_class_specializations.size() == 2);
}

}  // namespace fsim::test
