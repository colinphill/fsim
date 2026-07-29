// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include "fsim/systemc/hierarchy.hpp"

#include <algorithm>
#include <cassert>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_systemc_integration() {
  auto config = base_config();
fsim::diagnostic::Engine diagnostics;
auto checked = fsim::app::check_project(config, diagnostics);
assert(checked);
assert(checked->source_count == 2);
assert(checked->hdl_sources.size() == 1);
assert(checked->hdl_sources.front().path == source);
assert(checked->hdl_sources.front().content_digest.size() == 64);
assert(
    checked->hdl_sources.front().compilation_unit_digest.size()
    == 64);
assert(checked->parsed.units.size() == 2);

auto first = fsim::app::build_project(config, diagnostics);
if (!first) {
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": "
              << diagnostic.message << '\n';
    for (const auto& note : diagnostic.notes) {
      std::cerr << note.message << '\n';
    }
  }
}
assert(first);
assert(first->systemc_plugins.size() == 1);
assert(!first->cache_hit);
auto second = fsim::app::build_project(config, diagnostics);
assert(second);
#if defined(_WIN32)
// The conservative MSVC dependency scanner intentionally disables
// persistent caching when the plug-in reaches implicit SDK headers.
assert(!second->cache_hit);
#else
assert(second->cache_hit);
#endif
#if defined(_WIN32)
assert(first->systemc_hierarchy != second->systemc_hierarchy);
#else
assert(first->systemc_hierarchy == second->systemc_hierarchy);
#endif
const auto child_q = first->design.find_signal("tb.u_child.value");
assert(child_q);
const auto paths = first->design.signal_paths();
assert(std::find_if(
           paths.begin(), paths.end(),
           [](const auto& path) {
             return path.first == "tb.u_child.value";
       })
       != paths.end());

auto hdl_systemc_config = config;
hdl_systemc_config.project.top = "sv:work.systemc_host";
hdl_systemc_config.source_sets.front().files = {
    systemc_boundary_source};
hdl_systemc_config.bindings = {
    {"systemc_host.u_bridge",
     "systemc:models.bridge",
     std::nullopt},
    {"systemc_host.u_bridge.u_hdl",
     "sv:work.systemc_hdl_child",
     std::nullopt},
};
fsim::diagnostic::Engine hdl_systemc_diagnostics;
auto hdl_systemc_project = fsim::app::build_project(
    hdl_systemc_config, hdl_systemc_diagnostics);
assert(hdl_systemc_project);
assert(hdl_systemc_project->systemc_hierarchy);
assert(
    hdl_systemc_project->design.systemc_instances().size()
    == 1);
assert((
    hdl_systemc_project->systemc_hierarchy
        ->factory_parameters("bridge")
        ->front()
        .default_value
    == 0));
const auto hdl_systemc_value =
    hdl_systemc_project->design.find_signal("value");
const auto hdl_systemc_child_value =
    hdl_systemc_project->design.find_signal(
        "systemc_host.u_bridge.u_hdl.value");
const auto hdl_systemc_inverted =
    hdl_systemc_project->design.find_signal("inverted");
assert(
    hdl_systemc_value && hdl_systemc_child_value
    && hdl_systemc_inverted);
assert(*hdl_systemc_value == *hdl_systemc_child_value);
auto hdl_systemc_interpreter =
    hdl_systemc_project->design.create_interpreter();
const auto hdl_systemc_result =
    hdl_systemc_interpreter->run();
assert(
    hdl_systemc_result.status
    == fsim::runtime::RunStatus::stopped);
assert(
    hdl_systemc_interpreter
        ->signal_value(*hdl_systemc_inverted)
        .to_msb_string()
    == "0");
assert((
    hdl_systemc_project->design.systemc_instances().front()
        .construction_values
    == std::vector<std::pair<std::string, std::int64_t>>{
        {"CHILD_INVERT", 1}}));
const auto hdl_systemc_child_specialization =
    std::find_if(
        hdl_systemc_project->design.specializations().begin(),
        hdl_systemc_project->design.specializations().end(),
        [](const auto& specialization) {
          return specialization.instance
              == "systemc_host.u_bridge.u_hdl";
        });
assert(
    hdl_systemc_child_specialization
    != hdl_systemc_project->design.specializations().end());
assert((
    hdl_systemc_child_specialization->parameter_values
    == std::vector<std::pair<std::string, std::string>>{
        {"INVERT", "1"}}));
const auto run_compiled_systemc_actual =
    [&]() {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          hdl_systemc_config, run_diagnostics);
      assert(project);
      assert((
          project->design.systemc_instances().front()
              .construction_values
          == std::vector<
              std::pair<std::string, std::int64_t>>{
              {"CHILD_INVERT", 1}}));
      const auto output =
          project->design.find_signal("inverted");
      assert(output);
      fsim::app::Simulation simulation{
          std::move(*project),
          hdl_systemc_config.run.max_deltas,
          fsim::app::SimulationEngine::compiled};
      const auto result = simulation.run();
      assert(
          result.status == fsim::runtime::RunStatus::stopped);
      assert(
          simulation.read_signal(*output).to_msb_string()
          == "0");
      return std::pair{
          simulation.native_cache_statistics(),
          simulation.compiled_process_count()};
    };
const auto [systemc_actual_cold_cache,
            systemc_actual_cold_processes] =
    run_compiled_systemc_actual();
const auto [systemc_actual_warm_cache,
            systemc_actual_warm_processes] =
    run_compiled_systemc_actual();
assert(
    systemc_actual_warm_processes
    == systemc_actual_cold_processes);
#if defined(FSIM_HAS_LLVM)
assert(systemc_actual_cold_processes > 0);
assert(systemc_actual_cold_cache.misses > 0);
assert(systemc_actual_cold_cache.stores > 0);
assert(systemc_actual_warm_cache.hits > 0);
assert(systemc_actual_warm_cache.misses == 0);
#else
assert(systemc_actual_cold_processes == 0);
assert(
    systemc_actual_cold_cache
    == fsim::app::NativeCacheStatistics{});
assert(
    systemc_actual_warm_cache
    == fsim::app::NativeCacheStatistics{});
#endif

auto bound_port_config = hdl_systemc_config;
bound_port_config.project.top =
    "sv:work.systemc_bound_port_host";
bound_port_config.bindings = {
    {"systemc_bound_port_host.u_bound",
     "systemc:models.bound_ports",
     std::nullopt},
};
fsim::diagnostic::Engine bound_port_diagnostics;
auto bound_port_reference = fsim::app::build_project(
    bound_port_config, bound_port_diagnostics);
auto bound_port_compiled = fsim::app::build_project(
    bound_port_config, bound_port_diagnostics);
assert(bound_port_reference);
assert(bound_port_compiled);
const auto bound_input =
    bound_port_reference->design.find_signal("value");
const auto bound_input_channel =
    bound_port_reference->design.find_signal(
        "systemc_bound_port_host.u_bound.value_channel");
const auto bound_output =
    bound_port_reference->design.find_signal("inverted");
const auto bound_output_channel =
    bound_port_reference->design.find_signal(
        "systemc_bound_port_host.u_bound.inverted_channel");
assert(
    bound_input && bound_input_channel
    && bound_output && bound_output_channel);
assert(*bound_input == *bound_input_channel);
assert(*bound_output == *bound_output_channel);
const auto& bound_instance =
    bound_port_reference->design.systemc_instances().front();
assert(bound_instance.ports.size() == 2);
assert(bound_instance.internal_signals.size() == 2);
assert(
    bound_instance.ports[0].signal
    == bound_instance.internal_signals[0].signal);
assert(
    bound_instance.ports[1].signal
    == bound_instance.internal_signals[1].signal);
const auto run_bound_ports =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto output =
          project.design.find_signal("inverted");
      assert(output);
      fsim::app::Simulation simulation{
          std::move(project),
          bound_port_config.run.max_deltas,
          engine};
      assert(
          simulation.read_signal(*output).to_msb_string()
          == "1");
      const auto result = simulation.run();
      return std::pair{
          result,
          simulation.read_signal(*output).to_msb_string()};
    };
const auto bound_reference = run_bound_ports(
    std::move(*bound_port_reference),
    fsim::app::SimulationEngine::interpreter);
const auto bound_compiled = run_bound_ports(
    std::move(*bound_port_compiled),
    fsim::app::SimulationEngine::compiled);
assert(
    bound_reference.first.status
    == fsim::runtime::RunStatus::stopped);
assert(bound_reference.first.time == 2);
assert(bound_reference.second == "0");
assert(
    bound_reference.first.status
    == bound_compiled.first.status);
assert(bound_reference.first.time == bound_compiled.first.time);
assert(bound_reference.second == bound_compiled.second);

auto native_hierarchy_config = hdl_systemc_config;
native_hierarchy_config.project.top =
    "sv:work.systemc_native_hierarchy_host";
native_hierarchy_config.bindings = {
    {"systemc_native_hierarchy_host.u_native",
     "systemc:models.native_hierarchy",
     std::nullopt},
};
fsim::diagnostic::Engine native_hierarchy_diagnostics;
auto native_hierarchy_reference = fsim::app::build_project(
    native_hierarchy_config, native_hierarchy_diagnostics);
auto native_hierarchy_compiled = fsim::app::build_project(
    native_hierarchy_config, native_hierarchy_diagnostics);
assert(native_hierarchy_reference);
assert(native_hierarchy_compiled);
assert(
    native_hierarchy_reference->design.systemc_instances().size()
    == 2);
const auto native_parent = std::find_if(
    native_hierarchy_reference->design.systemc_instances().begin(),
    native_hierarchy_reference->design.systemc_instances().end(),
    [](const fsim::elaboration::SystemCInstanceInfo& instance) {
      return instance.instance
          == "systemc_native_hierarchy_host.u_native";
    });
const auto native_leaf = std::find_if(
    native_hierarchy_reference->design.systemc_instances().begin(),
    native_hierarchy_reference->design.systemc_instances().end(),
    [](const fsim::elaboration::SystemCInstanceInfo& instance) {
      return instance.instance
          == "systemc_native_hierarchy_host.u_native.leaf";
    });
assert(
    native_parent
    != native_hierarchy_reference->design.systemc_instances().end());
assert(
    native_leaf
    != native_hierarchy_reference->design.systemc_instances().end());
assert(native_parent->ports.size() == 2);
assert(native_parent->internal_signals.size() == 2);
assert(native_leaf->ports.size() == 2);
assert(
    native_leaf->ports[0].signal
    == native_parent->internal_signals[0].signal);
assert(
    native_leaf->ports[1].signal
    == native_parent->internal_signals[1].signal);
assert(std::any_of(
    native_hierarchy_reference->design.processes().begin(),
    native_hierarchy_reference->design.processes().end(),
    [](const fsim::runtime::simir::Process& process) {
      return process.name
          == "systemc_native_hierarchy_host.u_native.leaf.evaluate";
    }));
const auto run_native_hierarchy =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto output =
          project.design.find_signal("inverted");
      assert(output);
      fsim::app::Simulation simulation{
          std::move(project),
          native_hierarchy_config.run.max_deltas,
          engine};
      assert(
          simulation.read_signal(*output).to_msb_string()
          == "1");
      const auto result = simulation.run();
      return std::pair{
          result,
          simulation.read_signal(*output).to_msb_string()};
    };
const auto native_reference = run_native_hierarchy(
    std::move(*native_hierarchy_reference),
    fsim::app::SimulationEngine::interpreter);
const auto native_compiled = run_native_hierarchy(
    std::move(*native_hierarchy_compiled),
    fsim::app::SimulationEngine::compiled);
assert(
    native_reference.first.status
    == fsim::runtime::RunStatus::stopped);
assert(native_reference.first.time == 2);
assert(native_reference.second == "0");
assert(
    native_reference.first.status
    == native_compiled.first.status);
assert(native_reference.first.time == native_compiled.first.time);
assert(native_reference.second == native_compiled.second);

auto lifecycle_config = hdl_systemc_config;
lifecycle_config.project.top =
    "sv:work.systemc_lifecycle_host";
lifecycle_config.bindings = {
    {"systemc_lifecycle_host.u_lifecycle",
     "systemc:models.lifecycle_module",
     std::nullopt},
};
fsim::diagnostic::Engine lifecycle_diagnostics;
auto lifecycle_reference_project = fsim::app::build_project(
    lifecycle_config, lifecycle_diagnostics);
auto lifecycle_compiled_project = fsim::app::build_project(
    lifecycle_config, lifecycle_diagnostics);
assert(lifecycle_reference_project);
assert(lifecycle_compiled_project);
assert(lifecycle_reference_project->systemc_roots.size() == 1);
assert(lifecycle_compiled_project->systemc_roots.size() == 1);
const auto run_lifecycle =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto ready = project.design.find_signal("ready");
      assert(ready);
      fsim::app::Simulation simulation{
          std::move(project),
          lifecycle_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::pair{
          result,
          simulation.read_signal(*ready).to_msb_string()};
    };
const auto lifecycle_reference = run_lifecycle(
    std::move(*lifecycle_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto lifecycle_compiled = run_lifecycle(
    std::move(*lifecycle_compiled_project),
    fsim::app::SimulationEngine::compiled);
assert(
    lifecycle_reference.first.status
    == fsim::runtime::RunStatus::stopped);
assert(lifecycle_reference.first.time == 2);
assert(lifecycle_reference.second == "1");
assert(
    lifecycle_reference.first.status
    == lifecycle_compiled.first.status);
assert(
    lifecycle_reference.first.time
    == lifecycle_compiled.first.time);
assert(lifecycle_reference.second == lifecycle_compiled.second);

const auto exercise_native_binding =
    [&](const std::string& top,
        const std::string& instance_path,
        const std::string& target,
        const bool through_internal_signals) {
      auto binding_config = hdl_systemc_config;
      binding_config.project.top = "sv:work." + top;
      binding_config.bindings = {
          {instance_path, target, std::nullopt},
      };
      fsim::diagnostic::Engine binding_diagnostics;
      auto reference_project = fsim::app::build_project(
          binding_config, binding_diagnostics);
      auto compiled_project = fsim::app::build_project(
          binding_config, binding_diagnostics);
      assert(reference_project);
      assert(compiled_project);
      const auto& instances =
          reference_project->design.systemc_instances();
      const auto parent = std::find_if(
          instances.begin(),
          instances.end(),
          [&](const fsim::elaboration::SystemCInstanceInfo& instance) {
            return instance.instance == instance_path;
          });
      const auto leaf = std::find_if(
          instances.begin(),
          instances.end(),
          [&](const fsim::elaboration::SystemCInstanceInfo& instance) {
            return instance.instance == instance_path + ".leaf";
          });
      assert(parent != instances.end());
      assert(leaf != instances.end());
      assert(parent->ports.size() == 2);
      assert(leaf->ports.size() == 2);
      if (through_internal_signals) {
        assert(parent->internal_signals.size() == 2);
        assert(parent->exports.size() == 4);
        assert(
            parent->exports[0].signal
            == parent->internal_signals[0].signal);
        assert(
            parent->exports[1].signal
            == parent->internal_signals[0].signal);
        assert(
            parent->exports[2].signal
            == parent->internal_signals[1].signal);
        assert(
            parent->exports[3].signal
            == parent->internal_signals[1].signal);
        const auto exported_input =
            reference_project->design.find_signal(
                instance_path + ".value_export");
        const auto exported_output =
            reference_project->design.find_signal(
                instance_path + ".inverted_export");
        assert(exported_input && exported_output);
        assert(
            *exported_input
            == parent->internal_signals[0].signal);
        assert(
            *exported_output
            == parent->internal_signals[1].signal);
        assert(
            leaf->ports[0].signal
            == parent->internal_signals[0].signal);
        assert(
            leaf->ports[1].signal
            == parent->internal_signals[1].signal);
      } else {
        assert(
            leaf->ports[0].signal
            == parent->ports[0].signal);
        assert(
            leaf->ports[1].signal
            == parent->ports[1].signal);
      }
      const auto run_binding =
          [&](fsim::app::BuiltProject project,
              const fsim::app::SimulationEngine engine) {
            const auto output =
                project.design.find_signal("inverted");
            assert(output);
            fsim::app::Simulation simulation{
                std::move(project),
                binding_config.run.max_deltas,
                engine};
            const auto result = simulation.run();
            return std::pair{
                result,
                simulation.read_signal(*output).to_msb_string()};
          };
      const auto reference = run_binding(
          std::move(*reference_project),
          fsim::app::SimulationEngine::interpreter);
      const auto compiled = run_binding(
          std::move(*compiled_project),
          fsim::app::SimulationEngine::compiled);
      assert(
          reference.first.status
          == fsim::runtime::RunStatus::stopped);
      assert(reference.first.time == 2);
      assert(reference.second == "0");
      assert(reference.first.status == compiled.first.status);
      assert(reference.first.time == compiled.first.time);
      assert(reference.second == compiled.second);
    };
exercise_native_binding(
    "systemc_port_chain_host",
    "systemc_port_chain_host.u_chain",
    "systemc:models.port_chain_hierarchy",
    false);
exercise_native_binding(
    "systemc_export_host",
    "systemc_export_host.u_export",
    "systemc:models.export_hierarchy",
    true);

auto duplicate_native_config = hdl_systemc_config;
duplicate_native_config.project.top =
    "systemc:models.duplicate_native_hierarchy";
duplicate_native_config.bindings.clear();
fsim::diagnostic::Engine duplicate_native_diagnostics;
assert(!fsim::app::build_project(
    duplicate_native_config, duplicate_native_diagnostics));
assert(std::any_of(
    duplicate_native_diagnostics.diagnostics().begin(),
    duplicate_native_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SC-A004";
    }));

auto throwing_lifecycle_config = hdl_systemc_config;
throwing_lifecycle_config.project.top =
    "systemc:models.throwing_lifecycle";
throwing_lifecycle_config.bindings.clear();
fsim::diagnostic::Engine throwing_lifecycle_diagnostics;
assert(!fsim::app::build_project(
    throwing_lifecycle_config,
    throwing_lifecycle_diagnostics));
assert(std::any_of(
    throwing_lifecycle_diagnostics.diagnostics().begin(),
    throwing_lifecycle_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SC-A008";
    }));

auto throwing_end_config = hdl_systemc_config;
throwing_end_config.project.top =
    "systemc:models.throwing_end_lifecycle";
throwing_end_config.bindings.clear();
fsim::diagnostic::Engine throwing_end_diagnostics;
auto throwing_end_project = fsim::app::build_project(
    throwing_end_config, throwing_end_diagnostics);
assert(throwing_end_project);
fsim::app::Simulation throwing_end_simulation{
    std::move(*throwing_end_project),
    throwing_end_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter};
bool rejected_end_lifecycle = false;
try {
  (void)throwing_end_simulation.run();
} catch (const std::runtime_error&) {
  rejected_end_lifecycle = true;
}
assert(rejected_end_lifecycle);
assert(throwing_end_simulation.poisoned());

auto unbound_export_config = hdl_systemc_config;
unbound_export_config.project.top =
    "systemc:models.unbound_export";
unbound_export_config.bindings.clear();
fsim::diagnostic::Engine unbound_export_diagnostics;
assert(!fsim::app::build_project(
    unbound_export_config, unbound_export_diagnostics));
assert(std::any_of(
    unbound_export_diagnostics.diagnostics().begin(),
    unbound_export_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-ELAB-BIND-048";
    }));

auto invalid_deep_binding_config = hdl_systemc_config;
invalid_deep_binding_config.project.top =
    "systemc:models.invalid_deep_binding";
invalid_deep_binding_config.bindings.clear();
fsim::diagnostic::Engine invalid_deep_binding_diagnostics;
assert(!fsim::app::build_project(
    invalid_deep_binding_config,
    invalid_deep_binding_diagnostics));
assert(std::any_of(
    invalid_deep_binding_diagnostics.diagnostics().begin(),
    invalid_deep_binding_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SC-A004";
    }));

auto legacy_systemc_config = hdl_systemc_config;
legacy_systemc_config.bindings.front().target =
    "systemc:models.model";
fsim::diagnostic::Engine legacy_systemc_diagnostics;
assert(!fsim::app::build_project(
    legacy_systemc_config, legacy_systemc_diagnostics));
assert(std::any_of(
    legacy_systemc_diagnostics.diagnostics().begin(),
    legacy_systemc_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SC-A003";
    }));

auto systemc_hdl_config = hdl_systemc_config;
systemc_hdl_config.project.top = "systemc:models.bridge";
systemc_hdl_config.bindings = {
    {"bridge.u_hdl",
     "sv:work.systemc_hdl_child",
     std::nullopt},
};
fsim::diagnostic::Engine systemc_hdl_diagnostics;
auto systemc_hdl_project = fsim::app::build_project(
    systemc_hdl_config, systemc_hdl_diagnostics);
assert(systemc_hdl_project);
assert(systemc_hdl_project->systemc_hierarchy);
assert(
    systemc_hdl_project->design.systemc_instances().size()
    == 1);
assert(
    systemc_hdl_project->design.systemc_instances().front().instance
    == "bridge");
const auto systemc_root_value =
    systemc_hdl_project->design.find_signal("bridge.value");
const auto systemc_root_inverted =
    systemc_hdl_project->design.find_signal("bridge.inverted");
const auto systemc_root_child_inverted =
    systemc_hdl_project->design.find_signal(
        "bridge.u_hdl.inverted");
assert(
    systemc_root_value && systemc_root_inverted
    && systemc_root_child_inverted);
assert(*systemc_root_inverted == *systemc_root_child_inverted);
auto systemc_hdl_interpreter =
    systemc_hdl_project->design.create_interpreter();
systemc_hdl_interpreter->deposit_signal(
    *systemc_root_value,
    fsim::runtime::PackedLogic4::from_msb_string("1"));
const auto systemc_hdl_result =
    systemc_hdl_interpreter->run();
assert(
    systemc_hdl_result.status
    == fsim::runtime::RunStatus::completed);
assert(
    systemc_hdl_interpreter
        ->signal_value(*systemc_root_inverted)
        .to_msb_string()
    == "1");

#if defined(FSIM_HAS_BOOST_CONTEXT)
auto systemc_thread_config = hdl_systemc_config;
systemc_thread_config.project.top =
    "sv:work.systemc_thread_host";
systemc_thread_config.bindings = {
    {"systemc_thread_host.u_threads",
     "systemc:models.fiber_threads",
     std::nullopt},
};
fsim::diagnostic::Engine systemc_thread_diagnostics;
auto systemc_thread_project = fsim::app::build_project(
    systemc_thread_config, systemc_thread_diagnostics);
assert(systemc_thread_project);
const auto thread_count =
    systemc_thread_project->design.find_signal("count");
const auto thread_timed =
    systemc_thread_project->design.find_signal("timed");
const auto thread_event_count =
    systemc_thread_project->design.find_signal("event_count");
assert(thread_count && thread_timed && thread_event_count);
fsim::app::Simulation systemc_thread_simulation{
    std::move(*systemc_thread_project),
    systemc_thread_config.run.max_deltas,
#if defined(FSIM_HAS_LLVM)
    fsim::app::SimulationEngine::compiled};
#else
    fsim::app::SimulationEngine::interpreter};
#endif
const auto systemc_thread_result =
    systemc_thread_simulation.run();
assert(
    systemc_thread_result.status
    == fsim::runtime::RunStatus::stopped);
assert(systemc_thread_result.time == 4);
assert(
    systemc_thread_simulation
        .read_signal(*thread_count)
        .to_msb_string()
    == "00000010");
assert(
    systemc_thread_simulation
        .read_signal(*thread_timed)
        .to_msb_string()
    == "00000011");
assert(
    systemc_thread_simulation
        .read_signal(*thread_event_count)
        .to_msb_string()
    == "00000010");
#endif

auto systemc_method_config = hdl_systemc_config;
systemc_method_config.project.top =
    "sv:work.systemc_method_host";
systemc_method_config.bindings = {
    {"systemc_method_host.u_method",
     "systemc:models.method_bridge",
     std::nullopt},
};
fsim::diagnostic::Engine systemc_method_diagnostics;
auto systemc_method_reference = fsim::app::build_project(
    systemc_method_config, systemc_method_diagnostics);
auto systemc_method_compiled = fsim::app::build_project(
    systemc_method_config, systemc_method_diagnostics);
assert(systemc_method_reference);
assert(systemc_method_compiled);
assert(
    systemc_method_reference->design.systemc_processes().size()
    == 1);
const auto method_process_id =
    systemc_method_reference->design.systemc_processes().front().process;
const auto& method_process =
    systemc_method_reference->design.processes().at(
        method_process_id);
assert(
    method_process.name == "systemc_method_host.u_method.evaluate");
assert(method_process.initialize);
assert(method_process.static_sensitivity.size() == 1);
const auto run_systemc_method =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto inverted =
          project.design.find_signal("inverted");
      const auto observed =
          project.design.find_signal("observed");
      assert(inverted && observed);
      fsim::app::Simulation simulation{
          std::move(project),
          systemc_method_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::tuple{
          result,
          simulation.read_signal(*inverted).to_msb_string(),
          simulation.read_signal(*observed).to_msb_string()};
    };
const auto method_reference = run_systemc_method(
    std::move(*systemc_method_reference),
    fsim::app::SimulationEngine::interpreter);
const auto method_compiled = run_systemc_method(
    std::move(*systemc_method_compiled),
    fsim::app::SimulationEngine::compiled);
assert(std::get<0>(method_reference).status
       == fsim::runtime::RunStatus::stopped);
assert(std::get<0>(method_reference).time == 2);
assert(std::get<1>(method_reference) == "0");
assert(std::get<2>(method_reference) == "0");
assert(std::get<0>(method_reference).status
       == std::get<0>(method_compiled).status);
assert(std::get<0>(method_reference).time
       == std::get<0>(method_compiled).time);
assert(std::get<1>(method_reference)
       == std::get<1>(method_compiled));
assert(std::get<2>(method_reference)
       == std::get<2>(method_compiled));

auto vhdl_systemc_method_config = systemc_method_config;
vhdl_systemc_method_config.project.top =
    "vhdl:work.systemc_method_vhdl_host(rtl)";
vhdl_systemc_method_config.source_sets.front().language =
    fsim::project::Language::vhdl;
vhdl_systemc_method_config.source_sets.front().standard = "2008";
vhdl_systemc_method_config.source_sets.front().files = {
    systemc_method_vhdl_source};
vhdl_systemc_method_config.bindings = {
    {"systemc_method_vhdl_host.u_method",
     "systemc:models.method_bridge",
     std::nullopt},
};
fsim::diagnostic::Engine vhdl_systemc_method_diagnostics;
auto vhdl_systemc_method_project = fsim::app::build_project(
    vhdl_systemc_method_config,
    vhdl_systemc_method_diagnostics);
assert(vhdl_systemc_method_project);
const auto vhdl_method_output =
    vhdl_systemc_method_project->design.find_signal("inverted");
assert(vhdl_method_output);
fsim::app::Simulation vhdl_systemc_method_simulation{
    std::move(*vhdl_systemc_method_project),
    vhdl_systemc_method_config.run.max_deltas,
    fsim::app::SimulationEngine::compiled};
const auto vhdl_systemc_method_result =
    vhdl_systemc_method_simulation.run();
assert(
    vhdl_systemc_method_result.status
    == fsim::runtime::RunStatus::completed);
assert(
    vhdl_systemc_method_simulation
        .read_signal(*vhdl_method_output)
        .to_msb_string()
    == "1");

auto systemc_method_top_config = systemc_method_config;
systemc_method_top_config.project.top =
    "systemc:models.method_bridge";
systemc_method_top_config.bindings.clear();
fsim::diagnostic::Engine systemc_method_top_diagnostics;
auto systemc_method_top = fsim::app::build_project(
    systemc_method_top_config,
    systemc_method_top_diagnostics);
assert(systemc_method_top);
const auto method_top_output =
    systemc_method_top->design.find_signal(
        "method_bridge.inverted");
assert(method_top_output);
fsim::app::Simulation method_top_simulation{
    std::move(*systemc_method_top),
    systemc_method_top_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter};
const auto method_top_result = method_top_simulation.run();
assert(
    method_top_result.status
    == fsim::runtime::RunStatus::completed);
assert(
    method_top_simulation.read_signal(*method_top_output)
        .to_msb_string()
    == "Z");

auto noinit_config = systemc_method_top_config;
noinit_config.project.top =
    "systemc:models.noinit_bridge";
fsim::diagnostic::Engine noinit_diagnostics;
auto noinit_project = fsim::app::build_project(
    noinit_config, noinit_diagnostics);
assert(noinit_project);
const auto noinit_input =
    noinit_project->design.find_signal("noinit_bridge.value");
const auto noinit_output =
    noinit_project->design.find_signal(
        "noinit_bridge.inverted");
assert(noinit_input && noinit_output);
fsim::app::Simulation noinit_simulation{
    std::move(*noinit_project),
    noinit_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter};
const auto noinit_result = noinit_simulation.run();
assert(noinit_result.status == fsim::runtime::RunStatus::completed);
assert(
    noinit_simulation.read_signal(*noinit_output).to_msb_string()
    == "X");

noinit_diagnostics.clear();
auto awakened_noinit_project = fsim::app::build_project(
    noinit_config, noinit_diagnostics);
assert(awakened_noinit_project);
const auto awakened_input =
    awakened_noinit_project->design.find_signal(
        "noinit_bridge.value");
const auto awakened_output =
    awakened_noinit_project->design.find_signal(
        "noinit_bridge.inverted");
assert(awakened_input && awakened_output);
fsim::app::Simulation awakened_noinit_simulation{
    std::move(*awakened_noinit_project),
    noinit_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter};
awakened_noinit_simulation.deposit_signal(
    *awakened_input,
    fsim::runtime::PackedLogic4::from_msb_string("0"));
const auto awakened_noinit_result =
    awakened_noinit_simulation.run();
assert(
    awakened_noinit_result.status
    == fsim::runtime::RunStatus::completed);
assert(
    awakened_noinit_simulation
        .read_signal(*awakened_output)
        .to_msb_string()
    == "0");

auto edge_method_config = systemc_method_config;
edge_method_config.project.top =
    "sv:work.systemc_edge_host";
edge_method_config.bindings = {
    {"systemc_edge_host.u_counter",
     "systemc:models.edge_counter",
     std::nullopt},
};
fsim::diagnostic::Engine edge_method_diagnostics;
auto edge_method_project = fsim::app::build_project(
    edge_method_config, edge_method_diagnostics);
assert(edge_method_project);
const auto edge_count =
    edge_method_project->design.find_signal("count");
assert(edge_count);
fsim::app::Simulation edge_method_simulation{
    std::move(*edge_method_project),
    edge_method_config.run.max_deltas,
    fsim::app::SimulationEngine::compiled};
const auto edge_method_result = edge_method_simulation.run();
assert(
    edge_method_result.status
    == fsim::runtime::RunStatus::stopped);
assert(edge_method_result.time == 4);
assert(
    edge_method_simulation.read_signal(*edge_count).to_msb_string()
    == "00000010");

auto throwing_method_config = systemc_method_top_config;
throwing_method_config.project.top =
    "systemc:models.throwing_method";
fsim::diagnostic::Engine throwing_method_diagnostics;
auto throwing_method_project = fsim::app::build_project(
    throwing_method_config, throwing_method_diagnostics);
assert(throwing_method_project);
fsim::app::Simulation throwing_method_simulation{
    std::move(*throwing_method_project),
    throwing_method_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter};
bool caught_systemc_failure = false;
try {
  (void)throwing_method_simulation.run();
} catch (const std::runtime_error& error) {
  caught_systemc_failure =
      std::string_view{error.what()}.find(
          "intentional SystemC method failure")
      != std::string_view::npos;
}
assert(caught_systemc_failure);
assert(throwing_method_simulation.poisoned());

auto dynamic_event_config = systemc_method_top_config;
dynamic_event_config.project.top =
    "systemc:models.dynamic_events";
fsim::diagnostic::Engine dynamic_event_diagnostics;
auto dynamic_event_reference = fsim::app::build_project(
    dynamic_event_config, dynamic_event_diagnostics);
auto dynamic_event_compiled = fsim::app::build_project(
    dynamic_event_config, dynamic_event_diagnostics);
assert(dynamic_event_reference);
assert(dynamic_event_compiled);
assert(
    dynamic_event_reference->design.systemc_processes().size()
    == 2);
assert(
    dynamic_event_reference->design.systemc_instances().size()
    == 1);
assert(
    dynamic_event_reference->design.systemc_instances().front()
        .events.size()
    == 1);
const auto run_dynamic_events =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto count =
          project.design.find_signal("dynamic_events.count");
      assert(count);
      fsim::app::Simulation simulation{
          std::move(project),
          dynamic_event_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::pair{
          result,
          simulation.read_signal(*count).to_msb_string()};
    };
const auto dynamic_reference = run_dynamic_events(
    std::move(*dynamic_event_reference),
    fsim::app::SimulationEngine::interpreter);
const auto dynamic_compiled = run_dynamic_events(
    std::move(*dynamic_event_compiled),
    fsim::app::SimulationEngine::compiled);
assert(
    dynamic_reference.first.status
    == fsim::runtime::RunStatus::completed);
assert(dynamic_reference.first.time == 8);
assert(dynamic_reference.second == "00000100");
assert(dynamic_reference.first.status == dynamic_compiled.first.status);
assert(dynamic_reference.first.time == dynamic_compiled.first.time);
assert(dynamic_reference.second == dynamic_compiled.second);

auto event_list_config = systemc_method_top_config;
event_list_config.project.top =
    "systemc:models.event_lists";
fsim::diagnostic::Engine event_list_diagnostics;
auto event_list_reference = fsim::app::build_project(
    event_list_config, event_list_diagnostics);
auto event_list_compiled = fsim::app::build_project(
    event_list_config, event_list_diagnostics);
assert(event_list_reference);
assert(event_list_compiled);
assert(
    event_list_reference->design.systemc_instances().front()
        .events.size()
    == 3);
const auto run_event_lists =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto or_seen =
          project.design.find_signal("event_lists.or_seen");
      const auto and_seen =
          project.design.find_signal("event_lists.and_seen");
      assert(or_seen && and_seen);
      fsim::app::Simulation simulation{
          std::move(project),
          event_list_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::tuple{
          result,
          simulation.read_signal(*or_seen).to_msb_string(),
          simulation.read_signal(*and_seen).to_msb_string()};
    };
const auto event_lists_reference = run_event_lists(
    std::move(*event_list_reference),
    fsim::app::SimulationEngine::interpreter);
const auto event_lists_compiled = run_event_lists(
    std::move(*event_list_compiled),
    fsim::app::SimulationEngine::compiled);
assert(
    std::get<0>(event_lists_reference).status
    == fsim::runtime::RunStatus::completed);
assert(std::get<0>(event_lists_reference).time == 3);
assert(std::get<1>(event_lists_reference) == "00000001");
assert(std::get<2>(event_lists_reference) == "00000011");
assert(
    std::get<0>(event_lists_reference).status
    == std::get<0>(event_lists_compiled).status);
assert(
    std::get<0>(event_lists_reference).time
    == std::get<0>(event_lists_compiled).time);
assert(
    std::get<1>(event_lists_reference)
    == std::get<1>(event_lists_compiled));
assert(
    std::get<2>(event_lists_reference)
    == std::get<2>(event_lists_compiled));

auto kernel_channel_config = systemc_method_top_config;
kernel_channel_config.project.top =
    "systemc:models.kernel_channels";
fsim::diagnostic::Engine kernel_channel_diagnostics;
auto kernel_channel_reference = fsim::app::build_project(
    kernel_channel_config, kernel_channel_diagnostics);
auto kernel_channel_compiled = fsim::app::build_project(
    kernel_channel_config, kernel_channel_diagnostics);
assert(kernel_channel_reference);
assert(kernel_channel_compiled);
assert(
    kernel_channel_reference->design.systemc_instances()
        .front().primitive_channels.size()
    == 1);
const auto run_kernel_channels =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto value =
          project.design.find_signal("kernel_channels.value");
      const auto updates =
          project.design.find_signal("kernel_channels.updates");
      const auto event_count =
          project.design.find_signal(
              "kernel_channels.event_count");
      assert(value && updates && event_count);
      fsim::app::Simulation simulation{
          std::move(project),
          kernel_channel_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::tuple{
          result,
          simulation.read_signal(*value).to_msb_string(),
          simulation.read_signal(*updates).to_msb_string(),
          simulation.read_signal(*event_count).to_msb_string()};
    };
const auto kernel_reference = run_kernel_channels(
    std::move(*kernel_channel_reference),
    fsim::app::SimulationEngine::interpreter);
const auto kernel_compiled = run_kernel_channels(
    std::move(*kernel_channel_compiled),
    fsim::app::SimulationEngine::compiled);
assert(
    std::get<0>(kernel_reference).status
    == fsim::runtime::RunStatus::completed);
assert(std::get<0>(kernel_reference).time == 3);
assert(std::get<1>(kernel_reference) == "00000100");
assert(std::get<2>(kernel_reference) == "00000011");
assert(std::get<3>(kernel_reference) == "00000001");
assert(
    std::get<0>(kernel_reference).status
    == std::get<0>(kernel_compiled).status);
assert(
    std::get<0>(kernel_reference).time
    == std::get<0>(kernel_compiled).time);
assert(
    std::get<1>(kernel_reference)
    == std::get<1>(kernel_compiled));
assert(
    std::get<2>(kernel_reference)
    == std::get<2>(kernel_compiled));
assert(
    std::get<3>(kernel_reference)
    == std::get<3>(kernel_compiled));

auto internal_signal_config = systemc_method_top_config;
internal_signal_config.project.top =
    "systemc:models.internal_signals";
fsim::diagnostic::Engine internal_signal_diagnostics;
auto internal_signal_reference = fsim::app::build_project(
    internal_signal_config, internal_signal_diagnostics);
auto internal_signal_compiled = fsim::app::build_project(
    internal_signal_config, internal_signal_diagnostics);
assert(internal_signal_reference);
assert(internal_signal_compiled);
const auto& internal_instance =
    internal_signal_reference->design.systemc_instances().front();
assert(internal_instance.internal_signals.size() == 1);
assert(internal_instance.primitive_channels.size() == 1);
const auto run_internal_signals =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto internal =
          project.design.find_signal(
              "internal_signals.internal");
      const auto observed =
          project.design.find_signal(
              "internal_signals.observed");
      const auto event_count =
          project.design.find_signal(
              "internal_signals.event_count");
      const auto dynamic_count =
          project.design.find_signal(
              "internal_signals.dynamic_count");
      assert(
          internal && observed && event_count && dynamic_count);
      fsim::app::Simulation simulation{
          std::move(project),
          internal_signal_config.run.max_deltas,
          engine};
      assert(
          simulation.read_signal(*internal).to_msb_string()
          == "00000101");
      const auto result = simulation.run();
      return std::tuple{
          result,
          simulation.read_signal(*internal).to_msb_string(),
          simulation.read_signal(*observed).to_msb_string(),
          simulation.read_signal(*event_count).to_msb_string(),
          simulation.read_signal(*dynamic_count).to_msb_string()};
    };
const auto internal_reference = run_internal_signals(
    std::move(*internal_signal_reference),
    fsim::app::SimulationEngine::interpreter);
const auto internal_compiled = run_internal_signals(
    std::move(*internal_signal_compiled),
    fsim::app::SimulationEngine::compiled);
assert(
    std::get<0>(internal_reference).status
    == fsim::runtime::RunStatus::completed);
assert(std::get<0>(internal_reference).time == 1);
assert(std::get<1>(internal_reference) == "00000011");
assert(std::get<2>(internal_reference) == "00000011");
assert(std::get<3>(internal_reference) == "00000010");
assert(std::get<4>(internal_reference) == "00000010");
assert(
    std::get<0>(internal_reference).status
    == std::get<0>(internal_compiled).status);
assert(
    std::get<0>(internal_reference).time
    == std::get<0>(internal_compiled).time);
assert(
    std::get<1>(internal_reference)
    == std::get<1>(internal_compiled));
assert(
    std::get<2>(internal_reference)
    == std::get<2>(internal_compiled));
assert(
    std::get<3>(internal_reference)
    == std::get<3>(internal_compiled));
assert(
    std::get<4>(internal_reference)
    == std::get<4>(internal_compiled));

}

}  // namespace fsim::test
