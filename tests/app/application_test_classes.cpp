// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <ranges>
#include <set>
#include <string>
#include <sstream>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_class_simulation_integration() {
  std::cerr << "application classes: configure\n";
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

  using EngineSnapshot = std::tuple<
      std::uint64_t, std::uint64_t, std::uint64_t, std::uint64_t,
      std::uint64_t, std::uint64_t, std::uint64_t,
      std::size_t, std::size_t>;
  std::vector<EngineSnapshot> engine_snapshots;

  for (const auto engine : {
           fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::compiled,
           fsim::app::SimulationEngine::debug}) {
    std::cerr << "application classes: engine "
              << static_cast<unsigned>(engine) << " build\n";
    fsim::diagnostic::Engine diagnostics;
    auto built = fsim::app::build_project(config, diagnostics);
    if (!built) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
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
      const auto restored_derived = std::ranges::find(
          *restored,
          derived_specialization,
          &fsim::frontend::SystemVerilogClassSpecialization::
              specialization_identity);
      assert(restored_derived != restored->end());
      const auto restored_random = std::ranges::find(
          restored_derived->properties,
          std::string{"generated_value"},
          &fsim::frontend::SystemVerilogClassPropertyLayout::name);
      assert(restored_random != restored_derived->properties.end());
      assert(restored_random->is_randc && !restored_random->is_rand);
      const auto restored_wide = std::ranges::find(
          restored_derived->properties,
          std::string{"wide_value"},
          &fsim::frontend::SystemVerilogClassPropertyLayout::name);
      assert(
          restored_wide != restored_derived->properties.end()
          && restored_wide->bit_width == 137
          && restored_wide->type.width() == 137);
      const auto restored_interface = std::ranges::find(
          restored_derived->properties,
          std::string{"interface_view"},
          &fsim::frontend::SystemVerilogClassPropertyLayout::name);
      assert(
          restored_interface != restored_derived->properties.end()
          && restored_interface->bit_width == 64
          && restored_interface->type.systemverilog_virtual_interface
          && restored_interface->type.systemverilog_interface_type
              == "class_view_if"
          && restored_interface->type.systemverilog_interface_modport
              == "view"
          && restored_interface->type
                 .systemverilog_class_parameter_actuals.size() == 1
          && restored_interface->type
                 .systemverilog_class_parameter_actuals.front().name
              == std::optional<std::string>{"WIDTH"}
          && restored_interface->type
                 .systemverilog_class_parameter_actuals.front().value.text
              == "4");
      auto malformed_classes = *restored;
      auto malformed_derived = std::ranges::find(
          malformed_classes,
          derived_specialization,
          &fsim::frontend::SystemVerilogClassSpecialization::
              specialization_identity);
      assert(malformed_derived != malformed_classes.end());
      auto malformed_wide = std::ranges::find(
          malformed_derived->properties,
          std::string{"wide_value"},
          &fsim::frontend::SystemVerilogClassPropertyLayout::name);
      assert(malformed_wide != malformed_derived->properties.end());
      malformed_wide->type.packed_aggregate =
          static_cast<fsim::frontend::PackedAggregateKind>(255);
      fsim::diagnostic::Engine malformed_class_diagnostics;
      assert(!fsim::app::serialize_class_state(
          malformed_classes, malformed_class_diagnostics));
      auto trailing = *class_state;
      trailing.push_back('\0');
      fsim::diagnostic::Engine trailing_diagnostics;
      assert(!fsim::app::deserialize_class_state(
          trailing, "trailing-classes.bin", trailing_diagnostics));
      const auto truncated = class_state->substr(0, class_state->size() - 1U);
      fsim::diagnostic::Engine truncated_diagnostics;
      assert(!fsim::app::deserialize_class_state(
          truncated, "truncated-classes.bin", truncated_diagnostics));
      auto future = *class_state;
      future[8] = static_cast<char>(fsim::app::kClassStateSchema + 1U);
      fsim::diagnostic::Engine future_diagnostics;
      assert(!fsim::app::deserialize_class_state(
          future, "future-classes.bin", future_diagnostics));

      const auto constraint_hir_state =
          fsim::app::serialize_systemverilog_constraint_hir_state(
              built->systemverilog_hir, class_state_diagnostics);
      assert(constraint_hir_state);
      fsim::semantic::sv::Hir malformed_hir;
      malformed_hir.mutable_classes() = {
          built->systemverilog_hir.classes().begin(),
          built->systemverilog_hir.classes().end()};
      const auto malformed_hir_base = std::ranges::find_if(
          malformed_hir.mutable_classes(), [](const auto& declaration) {
            return declaration.canonical_identity.ends_with("::AppBase");
          });
      assert(malformed_hir_base != malformed_hir.mutable_classes().end());
      const auto malformed_hir_wide = std::ranges::find(
          malformed_hir_base->properties,
          std::string{"wide_value"},
          &fsim::semantic::sv::ClassProperty::name);
      assert(malformed_hir_wide != malformed_hir_base->properties.end());
      malformed_hir_wide->type.value_form =
          static_cast<fsim::semantic::sv::TypeForm>(255);
      fsim::diagnostic::Engine malformed_hir_diagnostics;
      assert(!fsim::app::serialize_systemverilog_constraint_hir_state(
          malformed_hir, malformed_hir_diagnostics));
      const auto restored_constraint_hir =
          fsim::app::deserialize_systemverilog_constraint_hir_state(
              *constraint_hir_state,
              "sv-constraint-hir.bin",
              class_state_diagnostics);
      assert(restored_constraint_hir);
      assert(
          restored_constraint_hir->classes().size()
          == built->systemverilog_hir.classes().size());
      const auto restored_constraint_class = std::ranges::find(
          restored_constraint_hir->classes(),
          derived_identity,
          &fsim::semantic::sv::ClassDeclaration::canonical_identity);
      assert(
          restored_constraint_class
              != restored_constraint_hir->classes().end()
          && !restored_constraint_class->constraints.empty()
          && !restored_constraint_class->composed_constraints.empty());
      const auto restored_constraint_base = std::ranges::find_if(
          restored_constraint_hir->classes(), [](const auto& declaration) {
            return declaration.canonical_identity.ends_with("::AppBase");
          });
      assert(restored_constraint_base != restored_constraint_hir->classes().end());
      const auto restored_constraint_wide = std::ranges::find(
          restored_constraint_base->properties,
          std::string{"wide_value"},
          &fsim::semantic::sv::ClassProperty::name);
      assert(
          restored_constraint_wide != restored_constraint_base->properties.end()
          && restored_constraint_wide->type.executable_width == 137);
      auto future_constraint_hir = *constraint_hir_state;
      future_constraint_hir[8] = static_cast<char>(
          fsim::app::kSystemVerilogConstraintHirStateSchema + 1U);
      fsim::diagnostic::Engine future_constraint_hir_diagnostics;
      assert(!fsim::app::deserialize_systemverilog_constraint_hir_state(
          future_constraint_hir,
          "future-sv-constraint-hir.bin",
          future_constraint_hir_diagnostics));
    }
    const auto virtual_slot = derived_method->virtual_slot;
    fsim::app::Simulation simulation(
        std::move(*built), config.run.max_deltas, engine);
    std::cerr << "application classes: engine "
              << static_cast<unsigned>(engine) << " execute\n";
    const auto handle = simulation.allocate_class(
        derived_specialization,
        base_identity);
    assert(simulation.class_heap()
               .random_state(handle, derived_identity + "::value").kind
           == fsim::runtime::SystemVerilogClassRandomKind::Rand);
    const auto& generated_random = simulation.class_heap().random_state(
        handle, derived_identity + "::generated_value");
    assert(generated_random.kind
           == fsim::runtime::SystemVerilogClassRandomKind::Randc);
    assert(generated_random.width == 4);
    assert(generated_random.nominal_type == "logic");
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
    const auto suspended_method_identity = base_identity + "::debug_suspend";
    fsim::runtime::SystemVerilogClassMethodDescriptor suspended_method;
    suspended_method.canonical_identity = suspended_method_identity;
    suspended_method.owner_type = base_identity;
    suspended_method.arguments = {
        fsim::runtime::SystemVerilogClassArgumentMode::Input};
    suspended_method.automatic_value_count = 1;
    suspended_method.is_task = true;
    suspended_method.entry = [](auto& frame) {
      if (frame.continuation_point() == 0) {
        frame.local(0).packed =
            fsim::runtime::PackedLogic4::from_aval_bval(8, 42, 0);
        frame.suspend_at(7);
        return fsim::runtime::SystemVerilogClassMethodStatus::Suspended;
      }
      return fsim::runtime::SystemVerilogClassMethodStatus::Completed;
    };
    simulation.class_methods().register_method(std::move(suspended_method));

    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::string>>
        property_changes;
    std::vector<std::tuple<
        fsim::runtime::SystemVerilogClassHandle,
        std::string,
        std::string>> all_property_changes;
    std::vector<std::tuple<
        fsim::runtime::SystemVerilogClassHandle,
        std::uint64_t,
        std::uint64_t,
        std::uint64_t>>
        randomization_callback_states;
    simulation.set_class_property_change_hook(
        [&](const auto changed_handle,
            const std::string_view property,
            const auto& value,
            const auto time,
            const auto delta) {
          all_property_changes.emplace_back(
              changed_handle, std::string{property}, value.to_msb_string());
          if (property.ends_with("::generated_value")) {
            const auto& random = simulation.class_heap().random_state(
                changed_handle, property);
            randomization_callback_states.emplace_back(
                changed_handle,
                random.revision,
                random.randc_cycle,
                random.randc_used_values.size());
          }
          if (changed_handle != handle) return;
          if (property.ends_with("::wide_value")) return;
          assert(property.ends_with("::value"));
          property_changes.emplace_back(
              time, delta, value.to_msb_string());
        });
    std::vector<std::tuple<std::string, std::string, std::string>>
        static_changes;
    simulation.set_class_static_property_change_hook(
        [&](const std::string_view specialization,
            const std::string_view property,
            const auto& value,
            const auto,
            const auto) {
          static_changes.emplace_back(
              specialization, property, value.to_msb_string());
        });
    auto wide_service_value = fsim::runtime::PackedLogic4{
        137, fsim::runtime::Logic4::zero};
    wide_service_value.set(136, fsim::runtime::Logic4::one);
    wide_service_value.set(91, fsim::runtime::Logic4::one);
    wide_service_value.set(5, fsim::runtime::Logic4::one);
    simulation.deposit_class_property(
        handle, base_identity + "::wide_value", wide_service_value);
    std::size_t safe_points{};
    std::size_t safe_point_live_objects{};
    const auto safe_point = simulation.add_safe_point_hook(
        [&](const auto&, const auto) {
          ++safe_points;
          safe_point_live_objects = std::max(
              safe_point_live_objects,
              simulation.class_heap().live_objects());
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
    if (engine == fsim::app::SimulationEngine::debug) {
      std::ifstream source_input(class_source);
      std::uint32_t source_object_line{};
      for (std::string line; std::getline(source_input, line);) {
        ++source_object_line;
        if (line.find("source_object = new(3, WIDE_SEED);")
            != std::string::npos) {
          break;
        }
      }
      assert(source_object_line != 0 && source_input.good());
      std::ostringstream early_debug_output;
      std::ostringstream early_debug_error;
      {
        fsim::app::DebuggerControl early_debugger(
            simulation, early_debug_output, early_debug_error);
        early_debugger.execute({
            "break", "source",
            class_source.filename().string() + ":"
                + std::to_string(source_object_line)});
        early_debugger.execute({"continue"});
        early_debugger.execute({"show", "class_top.source_object"});
      }
      assert(early_debug_error.str().empty());
      assert(
          early_debug_output.str().find("source_object = ")
          != std::string::npos);
      assert(
          early_debug_output.str().find(" declared " + derived_identity)
          != std::string::npos);
      simulation.clear_stop();
    }
    const auto result = simulation.run();
    simulation.remove_safe_point_hook(safe_point);
    assert(result.time == 9);
    assert(safe_points != 0 && safe_point_live_objects >= 4);
    const auto source_object = simulation.find_signal(
        "class_top.source_object");
    assert(source_object);
    const auto source_handle = simulation.read_signal(
        *source_object).low_word().aval;
    assert(source_handle != 0 && source_handle != handle);
    assert(simulation.class_heap().object(source_handle).dynamic_type
           == derived_identity);
    const auto& source_object_state =
        simulation.class_heap().object(source_handle);
    assert(source_object_state.random_root_identity == "class_top");
    auto source_random_stream = simulation.class_heap().random_stream(
        source_handle, derived_identity + "::randomize@source");
    const auto source_random_sample = source_random_stream.next_u64();
    assert(
        simulation.read_class_property(
            source_handle, base_identity + "::value")
                .packed.low_word().aval == 5);
    assert(
        simulation.read_class_property(
            source_handle, derived_identity + "::value")
                .packed.low_word().aval == 18);
    auto wide_seed = fsim::runtime::PackedLogic4{
        137, fsim::runtime::Logic4::zero};
    wide_seed.set(136, fsim::runtime::Logic4::one);
    wide_seed.set(73, fsim::runtime::Logic4::one);
    wide_seed.set(3, fsim::runtime::Logic4::one);
    auto wide_amount = fsim::runtime::PackedLogic4{
        137, fsim::runtime::Logic4::zero};
    wide_amount.set(128, fsim::runtime::Logic4::one);
    wide_amount.set(7, fsim::runtime::Logic4::one);
    wide_amount.set(0, fsim::runtime::Logic4::one);
    assert(
        simulation.read_class_property(
            source_handle, base_identity + "::wide_value").packed
        == wide_seed);
    const auto assert_wide_signal = [&](
        const std::string_view path,
        const fsim::runtime::PackedLogic4& expected) {
      const auto signal = simulation.find_signal(path);
      assert(signal);
      assert(simulation.read_signal(*signal) == expected);
    };
    for (const auto path : {
             "class_top.source_wide_prior",
             "class_top.source_wide_alias",
             "class_top.source_wide_result",
             "class_top.source_wide_recursive",
             "class_top.source_module_wide_prior",
             "class_top.source_module_wide_alias",
             "class_top.source_module_wide_result"}) {
      assert_wide_signal(path, wide_seed);
    }
    for (const auto path : {
             "class_top.source_wide_accumulator",
             "class_top.source_wide_task_observed",
             "class_top.source_wide_task_accumulator",
             "class_top.source_module_wide_accumulator",
             "class_top.source_module_wide_task_observed",
             "class_top.source_module_wide_task_accumulator"}) {
      assert_wide_signal(path, wide_amount);
    }
    for (const auto path : {
             "class_top.source_wide_static_first",
             "class_top.source_wide_static_second",
             "class_top.source_module_wide_static_first",
             "class_top.source_module_wide_static_second"}) {
      assert_wide_signal(path, wide_seed);
    }
    assert(std::ranges::any_of(
        all_property_changes, [&](const auto& change) {
          return std::get<0>(change) == source_handle
              && std::get<1>(change) == derived_identity + "::value"
              && std::get<2>(change) == "00010010";
        }));
    assert(std::ranges::any_of(
        static_changes, [&](const auto& change) {
          return std::get<0>(change) == base->specialization_identity
              && std::get<1>(change) == "shared"
              && std::get<2>(change)
                  == fsim::runtime::PackedLogic4::from_aval_bval(
                         64, 10, 0).to_msb_string();
        }));
    for (const auto& [path, expected] : std::vector<
             std::pair<std::string, std::uint64_t>>{
             {"class_top.source_prior", 3},
             {"class_top.source_accumulator", 6},
             {"class_top.source_alias", 6},
             {"class_top.source_result", 12},
             {"class_top.source_recursive", 12},
             {"class_top.source_static_first", 1},
             {"class_top.source_static_second", 2},
             {"class_top.source_base_result", 5},
             {"class_top.source_task_observed", 16},
             {"class_top.source_task_accumulator", 17},
             {"class_top.task_event_observed", 1},
             {"class_top.source_virtual_result", 18},
             {"class_top.source_static_result", 8},
             {"class_top.source_static_task_observed", 10},
             {"class_top.source_static_property", 10},
             {"class_top.source_handle_alias", 1},
             {"class_top.source_handle_property_alias", 1},
             {"class_top.source_task_handle_alias", 1},
             {"class_top.source_static_handle_alias", 1},
             {"class_top.source_fixed_handle_alias", 1},
             {"class_top.source_dynamic_handle_alias", 1},
             {"class_top.source_queued_handle_alias", 1},
             {"class_top.source_queue_pop_alias", 1},
             {"class_top.source_queue_size", 1},
             {"class_top.source_associative_handle_alias", 1},
             {"class_top.source_cast_alias", 1},
             {"class_top.source_failed_cast_preserved", 1},
             {"class_top.source_function_handle_alias", 1},
             {"class_top.source_module_task_handle_alias", 1},
             {"class_top.source_final_seen", 1},
             {"class_top.source_randomize_result", 1},
             {"class_top.source_randomize_pre", 30},
             {"class_top.source_randomize_post", 30},
             {"class_top.source_rand_mode_initial", 1},
             {"class_top.source_rand_mode_disabled", 0},
             {"class_top.source_rand_mode_enabled", 1},
             {"class_top.source_constraint_mode_initial", 1},
             {"class_top.source_constraint_mode_disabled", 0},
             {"class_top.source_constraint_mode_enabled", 1},
             {"class_top.source_impossible_enabled_result", 1},
             {"class_top.source_impossible_result", 0},
             {"class_top.source_impossible_pre", 20},
             {"class_top.source_impossible_post", 10},
             {"class_top.source_broken_pre_result", 0},
             {"class_top.source_broken_pre_count", 0},
             {"class_top.source_broken_post_result", 0},
             {"class_top.source_broken_post_pre", 0},
             {"class_top.source_broken_post_count", 0},
             {"class_top.source_selected_randomize_result", 1},
             {"class_top.source_selected_randomize_value", 77},
             {"class_top.source_property", 18}}) {
      const auto signal = simulation.find_signal(path);
      assert(signal);
      assert(simulation.read_signal(*signal).low_word().aval == expected);
    }
    const auto selected_randomized = simulation.find_signal(
        "class_top.source_selected_randomize_generated");
    const auto fully_randomized = simulation.find_signal(
        "class_top.source_randomize_generated");
    const auto third_randomized = simulation.find_signal(
        "class_top.source_randc_third");
    assert(selected_randomized && fully_randomized && third_randomized);
    assert(simulation.read_signal(*selected_randomized).low_word().aval >= 1
           && simulation.read_signal(*selected_randomized).low_word().aval <= 3);
    assert(simulation.read_signal(*fully_randomized).low_word().aval >= 1
           && simulation.read_signal(*fully_randomized).low_word().aval <= 3);
    const std::set<std::uint64_t> randc_cycle{
        simulation.read_signal(*selected_randomized).low_word().aval,
        simulation.read_signal(*fully_randomized).low_word().aval,
        simulation.read_signal(*third_randomized).low_word().aval};
    assert((randc_cycle == std::set<std::uint64_t>{1, 2, 3}));
    const auto randomized_object_signal = simulation.find_signal(
        "class_top.randomized_object");
    assert(randomized_object_signal);
    const auto randomized_handle = simulation.read_signal(
        *randomized_object_signal).low_word().aval;
    assert(simulation.class_heap().random_state(
               randomized_handle, derived_identity + "::value").revision == 0);
    assert(simulation.class_heap().random_state(
               randomized_handle,
               derived_identity + "::generated_value").revision == 3);
    std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>>
        randomized_callback_states;
    for (const auto& [changed_handle, revision, cycle, used] :
         randomization_callback_states) {
      if (changed_handle == randomized_handle) {
        randomized_callback_states.emplace_back(revision, cycle, used);
      }
    }
    assert((
        randomized_callback_states
        == std::vector<std::tuple<std::uint64_t, std::uint64_t, std::uint64_t>>{
            {1, 0, 1}, {2, 0, 2}, {3, 0, 3}}));
    const auto randomization_trace =
        simulation.class_randomization_trace_states();
    const auto generated_trace = std::ranges::find_if(
        randomization_trace, [&](const auto& state) {
          return state.object == randomized_handle
              && state.kind
                  == fsim::app::ClassRandomizationTraceKind::property
              && state.path.ends_with("::generated_value.$random-state");
        });
    assert(
        generated_trace != randomization_trace.end()
        && generated_trace->enabled
        && generated_trace->revision == 3
        && generated_trace->stream_seed != 0
        && generated_trace->domain_signature != 0
        && generated_trace->cycle == 0
        && generated_trace->used_values == 3);
    assert(std::ranges::any_of(
        randomization_trace, [&](const auto& state) {
          return state.object == randomized_handle
              && state.kind
                  == fsim::app::ClassRandomizationTraceKind::constraint
              && state.enabled
              && state.path.ends_with("::generated_small.$constraint-mode");
        }));
    if (engine == fsim::app::SimulationEngine::debug) {
      std::ostringstream random_debug_output;
      std::ostringstream random_debug_error;
      fsim::app::DebuggerControl random_debugger(
          simulation, random_debug_output, random_debug_error);
      random_debugger.execute({"class", std::to_string(randomized_handle)});
      assert(random_debug_error.str().empty());
      assert(
          random_debug_output.str().find(
              "randc enabled 1 revision 3") != std::string::npos
          && random_debug_output.str().find("cycle 0 used 3")
              != std::string::npos
          && random_debug_output.str().find(
              "constraint " + derived_identity
                  + "::generated_small enabled 1")
              != std::string::npos);
    }
    for (const auto name : {
             "class_top.broken_pre_object",
             "class_top.broken_post_object"}) {
      const auto failed_object = simulation.find_signal(name);
      assert(failed_object);
      const auto failed_handle = simulation.read_signal(
          *failed_object).low_word().aval;
      assert(simulation.class_heap().random_state(
                 failed_handle,
                 derived_identity + "::value").revision == 0);
      assert(simulation.class_heap().random_state(
                 failed_handle,
                 derived_identity + "::generated_value").revision == 0);
    }
    assert(completion_called);
    assert(
        simulation.read_class_property(handle, "value")
                .packed.low_word().aval == 4);
    assert(
        simulation.read_class_property(handle, base_identity + "::value")
                .packed.low_word().aval == 2);
    const auto source_other = simulation.find_signal(
        "class_top.source_other");
    assert(source_other);
    assert(
        simulation.read_class_property(
            source_handle, base_identity + "::peer").handle
        == simulation.read_signal(*source_other).low_word().aval);
    assert(
        simulation.class_static_store()
                .property(derived_identity, "shared")
                .packed.low_word().aval == 10);
    const std::vector<
        std::tuple<std::uint64_t, std::uint64_t, std::string>>
        expected_changes{
            {0, 0, "00000010"},
            {1, 0, "00000100"}};
    assert(property_changes == expected_changes);
    const auto trace_values = simulation.class_packed_trace_values();
    assert(std::ranges::any_of(trace_values, [&](const auto& trace) {
      return trace.object == source_handle
          && trace.path.ends_with(derived_identity + "::value")
          && trace.value.to_msb_string() == "00010010";
    }));
    assert(std::ranges::any_of(trace_values, [&](const auto& trace) {
      return trace.object == handle
          && trace.path.ends_with(base_identity + "::wide_value")
          && trace.value == wide_service_value;
    }));
    assert(std::ranges::any_of(trace_values, [&](const auto& trace) {
      return !trace.object
          && trace.path.ends_with(
              base->specialization_identity + ".shared")
          && trace.value.low_word().aval == 10;
    }));
    std::ostringstream class_vcd_output;
    fsim::runtime::VcdWriter class_vcd{class_vcd_output, "1ns", 64};
    std::vector<fsim::runtime::VcdSignal> class_vcd_signals;
    for (const auto& trace : trace_values) {
      class_vcd_signals.push_back(
          class_vcd.declare_signal(trace.path, trace.value.width()));
    }
    class_vcd.begin(simulation.now());
    for (std::size_t index = 0; index < trace_values.size(); ++index) {
      class_vcd.change(class_vcd_signals[index], trace_values[index].value);
    }
    class_vcd.flush();
    assert(class_vcd_output.str().find("00010010") != std::string::npos);
    assert(class_vcd_output.str().find(wide_service_value.to_msb_string())
           != std::string::npos);

    std::vector<fsim::runtime::SystemVerilogClassMethodValue>
        suspended_actuals(1);
    suspended_actuals.front().packed =
        fsim::runtime::PackedLogic4::from_aval_bval(8, 1, 0);
    const auto suspended = simulation.class_methods().invoke(
        suspended_method_identity, handle, suspended_actuals);
    assert(
        suspended.status
        == fsim::runtime::SystemVerilogClassMethodStatus::Suspended);
    std::ostringstream debug_output;
    std::ostringstream debug_error;
    fsim::app::DebuggerControl debugger(
        simulation, debug_output, debug_error);
    debugger.execute({"classes"});
    debugger.execute({"class", std::to_string(handle), "value"});
    debugger.execute({"class", "statics"});
    debugger.execute({
        "class", "static", base->specialization_identity, "shared"});
    debugger.execute({"class", "frames"});
    assert(debug_error.str().empty());
    assert(debug_output.str().find(derived_identity) != std::string::npos);
    assert(debug_output.str().find("value = 00000100") != std::string::npos);
    assert(debug_output.str().find("shared = ") != std::string::npos);
    assert(
        debug_output.str().find(suspended_method_identity)
        != std::string::npos);
    assert(debug_output.str().find("point 7") != std::string::npos);
    assert(debug_output.str().find("local[0] = 00101010")
           != std::string::npos);
    const auto resumed = simulation.class_methods().resume(
        suspended.continuation, suspended_actuals);
    assert(
        resumed.status
        == fsim::runtime::SystemVerilogClassMethodStatus::Completed);
    engine_snapshots.emplace_back(
        result.time,
        simulation.read_class_property(
            source_handle, base_identity + "::value").packed.low_word().aval,
        simulation.read_class_property(
            source_handle, derived_identity + "::value").packed.low_word().aval,
        simulation.class_static_store()
            .property(base_identity, "shared").packed.low_word().aval,
        source_object_state.random_root_seed,
        source_object_state.random_object_seed,
        source_random_sample,
        simulation.class_heap().live_objects(),
        trace_values.size());
    std::cerr << "application classes: engine "
              << static_cast<unsigned>(engine) << " complete\n";
  }
  assert(engine_snapshots.size() == 3);
  assert(std::ranges::all_of(
      engine_snapshots | std::views::drop(1),
      [&](const auto& snapshot) { return snapshot == engine_snapshots.front(); }));

  auto cache_config = config;
  std::cerr << "application classes: native cache\n";
  cache_config.project.name = "class-native-cache";
  cache_config.project.top = "sv:work.class_cache_top";
  cache_config.build.cache_path = directory / "class-native-cache";
  const auto cached_run = [&]() {
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(cache_config, diagnostics);
    assert(project && !diagnostics.has_error());
    fsim::app::Simulation simulation(
        std::move(*project), cache_config.run.max_deltas,
        fsim::app::SimulationEngine::compiled);
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::stopped);
    return simulation.native_cache_statistics();
  };
  const auto cold_cache = cached_run();
#if defined(FSIM_HAS_LLVM)
  assert(cold_cache.misses != 0 && cold_cache.stores != 0);
#else
  assert(cold_cache.hits == 0 && cold_cache.misses == 0);
  assert(cold_cache.stores == 0);
#endif
  const auto warm_cache = cached_run();
#if defined(FSIM_HAS_LLVM)
  assert(warm_cache.hits != 0 && warm_cache.misses == 0);
#else
  assert(warm_cache.hits == 0 && warm_cache.misses == 0);
  assert(warm_cache.stores == 0);
#endif
  const auto original_source = [&] {
    std::ifstream input(class_source, std::ios::binary);
    assert(input);
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
  }();
  auto edited_source = original_source;
  const auto edited_delay = edited_source.find(
      "#1 $finish; // class-cache-delay");
  assert(edited_delay != std::string::npos);
  edited_source.replace(
      edited_delay, 32, "#2 $finish; // class-cache-delay");
  {
    std::ofstream edited_output(
        class_source, std::ios::binary | std::ios::trunc);
    edited_output << edited_source;
  }
  const auto edited_cache = cached_run();
#if defined(FSIM_HAS_LLVM)
  assert(edited_cache.misses != 0 && edited_cache.stores != 0);
#else
  assert(edited_cache.hits == 0 && edited_cache.misses == 0);
  assert(edited_cache.stores == 0);
#endif
  {
    std::ofstream restored_output(
        class_source, std::ios::binary | std::ios::trunc);
    restored_output << original_source;
  }

  const auto object = directory / "class-object.fsimobj";
  std::cerr << "application classes: artifacts\n";
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
  const auto relocated_root = directory / "relocated-classes";
  std::filesystem::create_directories(relocated_root);
  const auto relocated_design = relocated_root / design.filename();
  const auto relocated_library = relocated_root / library.filename();
  const auto relocate_directory = [](const auto& origin, const auto& target) {
    std::filesystem::create_directories(target);
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(origin)) {
      const auto destination = target / entry.path().lexically_relative(origin);
      if (entry.is_directory()) {
        std::filesystem::create_directories(destination);
      } else if (entry.is_regular_file()) {
        std::filesystem::copy_file(entry.path(), destination);
      }
    }
  };
  relocate_directory(design, relocated_design);
  relocate_directory(library, relocated_library);
  fsim::diagnostic::Engine standalone_diagnostics;
  auto standalone = fsim::app::load_design_artifact(
      relocated_design, standalone_diagnostics);
  assert(standalone);
  assert(standalone->systemverilog_class_specializations.size() == 6);
  assert(std::ranges::any_of(
      standalone->systemverilog_class_specializations,
      [](const auto& specialization) {
        return std::ranges::any_of(
            specialization.properties, [](const auto& property) {
              return property.name == "wide_value"
                  && property.bit_width == 137
                  && property.type.width() == 137;
            });
      }));
  assert(standalone->systemverilog_hir.classes().size() == 6);
  assert(std::ranges::any_of(
      standalone->systemverilog_hir.classes(), [](const auto& declaration) {
        return declaration.canonical_identity.ends_with("::AppDerived")
            && !declaration.constraints.empty()
            && !declaration.composed_constraints.empty();
      }));
  assert(std::ranges::any_of(
      standalone->design.processes(), [](const auto& process) {
        return std::ranges::any_of(process.operations, [](const auto& op) {
          return fsim::runtime::simir::operation_holds<
                     fsim::runtime::simir::ClassAllocate>(op);
        });
      }));
  assert(std::ranges::any_of(
      standalone->design.processes(), [](const auto& process) {
        const auto has_class_call = std::ranges::any_of(
            process.operations, [](const auto& op) {
              return fsim::runtime::simir::operation_holds<
                         fsim::runtime::simir::ClassMethodCall>(op);
            });
        const auto has_continuation = std::ranges::any_of(
            process.operations, [](const auto& op) {
              return fsim::runtime::simir::operation_holds<
                         fsim::runtime::simir::Call>(op);
            });
        return has_class_call && has_continuation;
      }));
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
  const auto standalone_transfer = std::ranges::find(
      standalone_derived->methods,
      std::string{"transfer"},
      &fsim::frontend::SystemVerilogClassMethodProfile::name);
  assert(
      standalone_transfer != standalone_derived->methods.end()
      && !standalone_transfer->statements.empty()
      && !standalone_transfer->statements.front().span.source_name.empty());
  const auto standalone_handle = standalone_simulation.allocate_class(
      standalone_derived->specialization_identity);
  assert(
      standalone_simulation.read_class_property(
          standalone_handle, "value").packed.to_msb_string()
      == "XXXXXXXX");
  const auto standalone_base = std::ranges::find_if(
      standalone_simulation.class_specializations(),
      [](const auto& specialization) {
        return specialization.declaration_identity.ends_with("::AppBase");
      });
  assert(standalone_base != standalone_simulation.class_specializations().end());
  assert(
      standalone_simulation.class_static_store()
              .property(standalone_base->declaration_identity, "shared")
              .packed.low_word().aval == 2);
  const auto standalone_result = standalone_simulation.run();
  assert(
      standalone_result.status == fsim::runtime::RunStatus::stopped
      && standalone_result.time == 9);
  const auto standalone_property = standalone_simulation.find_signal(
      "class_top.source_property");
  assert(
      standalone_property
      && standalone_simulation.read_signal(*standalone_property)
             .low_word().aval == 18);
  assert(
      standalone_simulation.class_static_store()
              .property(standalone_base->declaration_identity, "shared")
              .packed.low_word().aval == 7);

  auto mapped_config = config;
  std::cerr << "application classes: relocated library\n";
  mapped_config.project.name = "mapped-class-simulation";
  mapped_config.source_sets.clear();
  mapped_config.library_mappings = {{"work", relocated_library}};
  mapped_config.elaboration.search_libraries = {"work"};
  mapped_config.build.cache_path = directory / "mapped-class-cache";
  fsim::diagnostic::Engine mapped_diagnostics;
  auto mapped = fsim::app::build_project(mapped_config, mapped_diagnostics);
  assert(mapped);
  assert(mapped->systemverilog_class_specializations.size() == 6);
  assert(std::ranges::any_of(
      mapped->systemverilog_class_specializations,
      [](const auto& specialization) {
        return std::ranges::any_of(
            specialization.properties, [](const auto& property) {
              return property.name == "wide_value"
                  && property.bit_width == 137
                  && property.type.width() == 137;
            });
      }));
  fsim::app::Simulation mapped_simulation(
      std::move(*mapped), mapped_config.run.max_deltas,
      fsim::app::SimulationEngine::compiled);
  const auto mapped_result = mapped_simulation.run();
  assert(
      mapped_result.status == fsim::runtime::RunStatus::stopped
      && mapped_result.time == 9);
  const auto mapped_property = mapped_simulation.find_signal(
      "class_top.source_property");
  assert(
      mapped_property
      && mapped_simulation.read_signal(*mapped_property).low_word().aval == 18);

  auto multiple = config;
  std::cerr << "application classes: multiple roots\n";
  multiple.project.name = "multi-root-class-simulation";
  multiple.project.top.clear();
  multiple.project.tops = {
      {"sv:work.class_root_a", "left"},
      {"sv:work.class_root_b", "right"}};
  multiple.build.cache_path = directory / "multi-root-class-cache";
  multiple.source_sets.front().files = {hidden_source};
  for (const auto engine : {
           fsim::app::SimulationEngine::interpreter,
           fsim::app::SimulationEngine::compiled,
           fsim::app::SimulationEngine::debug}) {
    fsim::diagnostic::Engine multiple_diagnostics;
    auto multiple_project = fsim::app::build_project(
        multiple, multiple_diagnostics);
    if (!multiple_project) {
      for (const auto& diagnostic : multiple_diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(multiple_project);
    assert((multiple_project->design.roots()
            == std::vector<std::string>{"left", "right"}));
    fsim::app::Simulation multiple_simulation(
        std::move(*multiple_project), multiple.run.max_deltas, engine);
    const auto multiple_result = multiple_simulation.run();
    assert(multiple_result.status == fsim::runtime::RunStatus::stopped);
    assert(multiple_result.time == 2);
    for (const auto path : {"left.ready", "right.ready"}) {
      const auto signal = multiple_simulation.find_signal(path);
      assert(signal);
      assert(multiple_simulation.read_signal(*signal).low_word().aval == 1);
    }
    assert(multiple_simulation.class_heap().live_objects() == 2);
    const auto multi_base = std::ranges::find_if(
        multiple_simulation.class_specializations(),
        [](const auto& specialization) {
          return specialization.declaration_identity.ends_with("::AppBase");
        });
    assert(multi_base != multiple_simulation.class_specializations().end());
    assert(
        multiple_simulation.class_static_store()
                .property(multi_base->declaration_identity, "shared")
                .packed.low_word().aval == 13);
  }
}

}  // namespace fsim::test
