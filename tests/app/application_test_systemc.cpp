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
assert(first->design_ir.valid());
assert(first->design_ir.valid(first->semantics));
assert(std::ranges::none_of(
    first->design_ir.boundaries(), [](const auto& boundary) {
      return boundary.kind
          == fsim::semantic::design::BoundaryKind::systemc_instance;
    }));
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
if (!hdl_systemc_project) {
  for (const auto& diagnostic :
       hdl_systemc_diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": "
              << diagnostic.message << '\n';
  }
}
assert(hdl_systemc_project);
assert(hdl_systemc_project->design_ir.valid());
assert(hdl_systemc_project->design_ir.valid(
    hdl_systemc_project->semantics));
assert(std::ranges::any_of(
    hdl_systemc_project->design_ir.objects(), [](const auto& object) {
      return object.kind
          == fsim::semantic::design::ObjectKind::systemc_port;
    }));
assert(std::ranges::any_of(
    hdl_systemc_project->design_ir.boundaries(), [](const auto& boundary) {
      return boundary.kind
          == fsim::semantic::design::BoundaryKind::systemc_instance;
    }));
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
using SystemCObjectKind =
    fsim::elaboration::SystemCNamedObjectKind;
const auto& native_objects =
    native_hierarchy_reference->design.systemc_objects();
const auto find_native_object =
    [&](const std::string_view name,
        const SystemCObjectKind kind) {
      return std::find_if(
          native_objects.begin(), native_objects.end(),
          [&](const fsim::elaboration::SystemCNamedObjectInfo& object) {
            return object.name == name && object.kind == kind;
          });
    };
const auto native_module = find_native_object(
    "systemc_native_hierarchy_host.u_native",
    SystemCObjectKind::module);
const auto native_leaf_module = find_native_object(
    "systemc_native_hierarchy_host.u_native.leaf",
    SystemCObjectKind::module);
const auto native_value_channel = find_native_object(
    "systemc_native_hierarchy_host.u_native.value_channel",
    SystemCObjectKind::signal);
const auto native_leaf_value = find_native_object(
    "systemc_native_hierarchy_host.u_native.leaf.value",
    SystemCObjectKind::port);
const auto native_evaluate = find_native_object(
    "systemc_native_hierarchy_host.u_native.leaf.evaluate",
    SystemCObjectKind::process);
assert(native_module != native_objects.end());
assert(native_module->parent == "systemc_native_hierarchy_host");
assert(native_leaf_module != native_objects.end());
assert(
    native_leaf_module->parent
    == "systemc_native_hierarchy_host.u_native");
assert(native_value_channel != native_objects.end());
assert(native_leaf_value != native_objects.end());
assert(native_value_channel->signal == native_leaf_value->signal);
assert(native_evaluate != native_objects.end());
assert(native_evaluate->process);

auto native_hierarchy_debug = fsim::app::build_project(
    native_hierarchy_config, native_hierarchy_diagnostics);
assert(native_hierarchy_debug);
const auto named_trace = directory / "systemc-named.vcd";
auto native_debug_config = native_hierarchy_config;
native_debug_config.run.trace_file = named_trace;
std::ostringstream native_debug_output;
std::ostringstream native_debug_error;
{
  fsim::app::Simulation native_debug_simulation{
      std::move(*native_hierarchy_debug),
      native_debug_config.run.max_deltas,
      fsim::app::SimulationEngine::debug};
  fsim::diagnostic::Engine native_debug_diagnostics;
  fsim::app::DebuggerControl debugger{
      native_debug_simulation,
      native_debug_output,
      native_debug_error,
      native_debug_config,
      native_debug_diagnostics};
  debugger.execute({
      "scope", "systemc_native_hierarchy_host.u_native"});
  debugger.execute({"scopes"});
  debugger.execute({
      "signals",
      "systemc_native_hierarchy_host.u_native.leaf"});
  debugger.execute({"run"});
  assert(!native_debug_diagnostics.has_error());
}
assert(native_debug_error.str().empty());
assert(
    native_debug_output.str().find(
        "systemc_native_hierarchy_host.u_native.leaf")
    != std::string::npos);
std::ifstream named_trace_input(named_trace, std::ios::binary);
const std::string named_trace_text{
    std::istreambuf_iterator<char>{named_trace_input},
    std::istreambuf_iterator<char>{}};
assert(named_trace_text.find("$scope module u_native $end")
       != std::string::npos);
assert(named_trace_text.find("$scope module leaf $end")
       != std::string::npos);
assert(named_trace_text.find(" value $end")
       != std::string::npos);
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
auto lifecycle_resumable_project = fsim::app::build_project(
    lifecycle_config, lifecycle_diagnostics);
assert(lifecycle_reference_project);
assert(lifecycle_compiled_project);
assert(lifecycle_resumable_project);
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

const auto lifecycle_resume_ready =
    lifecycle_resumable_project->design.find_signal("ready");
assert(lifecycle_resume_ready);
fsim::app::Simulation lifecycle_resumable_simulation{
    std::move(*lifecycle_resumable_project),
    lifecycle_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter};
lifecycle_resumable_simulation.request_stop();
const auto lifecycle_paused = lifecycle_resumable_simulation.run();
assert(
    lifecycle_paused.status == fsim::runtime::RunStatus::stopped
    && lifecycle_paused.time == 0
    && !lifecycle_resumable_simulation.finished());
lifecycle_resumable_simulation.clear_stop();
const auto lifecycle_resumed = lifecycle_resumable_simulation.run();
assert(
    lifecycle_resumed.status == fsim::runtime::RunStatus::stopped
    && lifecycle_resumed.time == 2
    && lifecycle_resumable_simulation.finished());
assert(
    lifecycle_resumable_simulation
        .read_signal(*lifecycle_resume_ready)
        .to_msb_string()
    == "1");

auto natural_lifecycle_config = lifecycle_config;
natural_lifecycle_config.project.top =
    "systemc:models.lifecycle_module";
natural_lifecycle_config.bindings.clear();
fsim::diagnostic::Engine natural_lifecycle_diagnostics;
auto natural_lifecycle_project = fsim::app::build_project(
    natural_lifecycle_config, natural_lifecycle_diagnostics);
assert(natural_lifecycle_project);
fsim::app::Simulation natural_lifecycle_simulation{
    std::move(*natural_lifecycle_project),
    natural_lifecycle_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter};
const auto natural_lifecycle_result =
    natural_lifecycle_simulation.run();
assert(
    natural_lifecycle_result.status
        == fsim::runtime::RunStatus::completed
    && natural_lifecycle_result.time == 0
    && natural_lifecycle_simulation.finished());

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
            !parent->exports[0].writable
            && !parent->exports[1].writable
            && parent->exports[2].writable
            && parent->exports[3].writable);
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

auto custom_metadata_config = hdl_systemc_config;
custom_metadata_config.project.top =
    "systemc:models.custom_metadata";
custom_metadata_config.bindings.clear();
fsim::diagnostic::Engine custom_metadata_diagnostics;
auto custom_metadata_project = fsim::app::build_project(
    custom_metadata_config, custom_metadata_diagnostics);
assert(custom_metadata_project);
const auto& custom_objects =
    custom_metadata_project->design.systemc_objects();
const auto has_custom_object =
    [&](const std::string_view name,
        const fsim::elaboration::SystemCNamedObjectKind kind,
        const std::string_view type_name) {
      return std::any_of(
          custom_objects.begin(), custom_objects.end(),
          [&](const fsim::elaboration::SystemCNamedObjectInfo& object) {
            return object.name == name && object.kind == kind
                && object.type_name == type_name && !object.signal
                && !object.process;
          });
    };
assert(has_custom_object(
    "custom_metadata.endpoint",
    fsim::elaboration::SystemCNamedObjectKind::port,
    "models.metadata_interface"));
assert(has_custom_object(
    "custom_metadata.exposed",
    fsim::elaboration::SystemCNamedObjectKind::export_object,
    "models.metadata_interface"));
assert(has_custom_object(
    "custom_metadata.channel",
    fsim::elaboration::SystemCNamedObjectKind::primitive_channel,
    "models.metadata_channel"));

auto custom_binding_config = custom_metadata_config;
custom_binding_config.project.top =
    "systemc:models.custom_binding_failure";
fsim::diagnostic::Engine custom_binding_diagnostics;
assert(!fsim::app::build_project(
    custom_binding_config, custom_binding_diagnostics));
assert(std::any_of(
    custom_binding_diagnostics.diagnostics().begin(),
    custom_binding_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SC-A004"
          && diagnostic.message.find(
              "custom SystemC interface binding is metadata-only")
              != std::string::npos;
    }));

const auto run_custom_failure =
    [&](const std::string_view target,
        const std::string_view expected) {
      auto failure_config = custom_metadata_config;
      failure_config.project.top = std::string{target};
      fsim::diagnostic::Engine failure_diagnostics;
      auto failure_project = fsim::app::build_project(
          failure_config, failure_diagnostics);
      assert(failure_project);
      fsim::app::Simulation failure_simulation{
          std::move(*failure_project),
          failure_config.run.max_deltas,
          fsim::app::SimulationEngine::interpreter};
      bool rejected = false;
      std::string actual_error;
      try {
        (void)failure_simulation.run();
      } catch (const std::runtime_error& error) {
        actual_error = error.what();
        rejected =
            std::string_view{error.what()}.find(expected)
            != std::string_view::npos;
      }
      if (!rejected) {
        throw std::runtime_error{
            "unexpected failure for " + std::string{target}
            + ": " + actual_error};
      }
      assert(failure_simulation.poisoned());
    };
run_custom_failure(
    "systemc:models.custom_update_failure",
    "custom SystemC primitive-channel updates are unsupported");
run_custom_failure(
    "systemc:models.custom_value_failure",
    "custom SystemC interface value access is unsupported");
run_custom_failure(
    "systemc:models.event_tick_failure",
    "SystemC time is not exactly representable");
run_custom_failure(
    "systemc:models.delayed_pending_failure",
    "notify_delayed requires an event with no pending notification");
run_custom_failure(
    "systemc:models.channel_update_failure",
    "intentional channel update failure");
run_custom_failure(
    "systemc:models.lifecycle_event_failure",
    "fsim SystemC host failed to notify event");
run_custom_failure(
    "systemc:models.lifecycle_suspend_failure",
    "next_trigger is only valid in SC_METHOD");

auto named_matrix_config = hdl_systemc_config;
named_matrix_config.project.top =
    "sv:work.systemc_named_object_matrix_host";
named_matrix_config.bindings = {{
    "systemc_named_object_matrix_host.u_matrix",
    "systemc:models.named_object_matrix",
    std::nullopt}};
named_matrix_config.build.optimization =
    fsim::project::Optimization::o2;
named_matrix_config.build.cache_path = directory / "named-matrix-o2";
fsim::diagnostic::Engine named_matrix_diagnostics;
auto named_matrix_reference = fsim::app::build_project(
    named_matrix_config, named_matrix_diagnostics);
assert(named_matrix_reference);
const auto& named_matrix_objects =
    named_matrix_reference->design.systemc_objects();
const auto matrix_object =
    [&](const std::string_view name) {
      return std::find_if(
          named_matrix_objects.begin(), named_matrix_objects.end(),
          [&](const fsim::elaboration::SystemCNamedObjectInfo& object) {
            return object.name == name;
          });
    };
const auto matrix_prefix =
    std::string{"systemc_named_object_matrix_host.u_matrix"};
const auto matrix_value = matrix_object(matrix_prefix + ".value");
const auto matrix_value_channel =
    matrix_object(matrix_prefix + ".value_channel");
const auto matrix_endpoint =
    matrix_object(matrix_prefix + ".value_endpoint");
const auto matrix_leaf_value =
    matrix_object(matrix_prefix + ".leaf.value");
const auto matrix_process =
    matrix_object(matrix_prefix + ".leaf.evaluate");
const auto matrix_event = matrix_object(matrix_prefix + ".pulse");
const auto matrix_custom_port =
    matrix_object(matrix_prefix + ".custom_port");
const auto matrix_custom_export =
    matrix_object(matrix_prefix + ".custom_export");
const auto matrix_custom_channel =
    matrix_object(matrix_prefix + ".custom_channel");
for (const auto object : {
         matrix_value,
         matrix_value_channel,
         matrix_endpoint,
         matrix_leaf_value,
         matrix_process,
         matrix_event,
         matrix_custom_port,
         matrix_custom_export,
         matrix_custom_channel}) {
  assert(object != named_matrix_objects.end());
}
assert(
    matrix_value->signal == matrix_value_channel->signal
    && matrix_value->signal == matrix_endpoint->signal
    && matrix_value->signal == matrix_leaf_value->signal);
assert(matrix_process->process && !matrix_process->signal);
assert(matrix_event->signal);
assert(
    !matrix_custom_port->signal && !matrix_custom_export->signal
    && !matrix_custom_channel->signal);

const auto run_named_matrix =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto output = project.design.find_signal("inverted");
      assert(output);
      fsim::app::Simulation simulation{
          std::move(project),
          named_matrix_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::tuple{
          result,
          simulation.read_signal(*output).to_msb_string(),
          simulation.native_cache_statistics(),
          simulation.compiled_process_count()};
    };
const auto matrix_reference_capture = run_named_matrix(
    std::move(*named_matrix_reference),
    fsim::app::SimulationEngine::interpreter);
assert(
    std::get<0>(matrix_reference_capture).status
    == fsim::runtime::RunStatus::stopped);
assert(std::get<0>(matrix_reference_capture).time == 2);
assert(std::get<1>(matrix_reference_capture) == "0");

const auto run_named_matrix_compiled_pair =
    [&](const fsim::project::Optimization optimization,
        const std::string_view cache_name) {
      auto compiled_config = named_matrix_config;
      compiled_config.build.optimization = optimization;
      compiled_config.build.cache_path =
          directory / std::string{cache_name};
      fsim::diagnostic::Engine compiled_diagnostics;
      auto cold = fsim::app::build_project(
          compiled_config, compiled_diagnostics);
      auto warm = fsim::app::build_project(
          compiled_config, compiled_diagnostics);
      assert(cold && warm);
      auto cold_capture = run_named_matrix(
          std::move(*cold), fsim::app::SimulationEngine::compiled);
      auto warm_capture = run_named_matrix(
          std::move(*warm), fsim::app::SimulationEngine::compiled);
      assert(
          std::get<0>(cold_capture).status
              == std::get<0>(matrix_reference_capture).status
          && std::get<0>(cold_capture).time
              == std::get<0>(matrix_reference_capture).time
          && std::get<1>(cold_capture)
              == std::get<1>(matrix_reference_capture));
      assert(
          std::get<0>(warm_capture).status
              == std::get<0>(cold_capture).status
          && std::get<0>(warm_capture).time
              == std::get<0>(cold_capture).time
          && std::get<1>(warm_capture) == std::get<1>(cold_capture));
#if defined(FSIM_HAS_LLVM)
      assert(std::get<3>(cold_capture) > 0);
      assert(std::get<2>(cold_capture).misses > 0);
      assert(std::get<2>(cold_capture).stores > 0);
      assert(std::get<2>(warm_capture).hits > 0);
      assert(std::get<2>(warm_capture).misses == 0);
#endif
      return cold_capture;
    };
(void)run_named_matrix_compiled_pair(
    fsim::project::Optimization::o0, "named-matrix-o0");
(void)run_named_matrix_compiled_pair(
    fsim::project::Optimization::o2, "named-matrix-o2-compiled");

const auto matrix_trace = directory / "named-matrix.vcd";
auto matrix_debug_config = named_matrix_config;
matrix_debug_config.build.optimization =
    fsim::project::Optimization::o0;
matrix_debug_config.build.cache_path = directory / "named-matrix-debug";
matrix_debug_config.run.trace_file = matrix_trace;
fsim::diagnostic::Engine matrix_debug_diagnostics;
auto matrix_debug_project = fsim::app::build_project(
    matrix_debug_config, matrix_debug_diagnostics);
assert(matrix_debug_project);
std::ostringstream matrix_debug_output;
std::ostringstream matrix_debug_error;
{
  fsim::app::Simulation matrix_debug_simulation{
      std::move(*matrix_debug_project),
      matrix_debug_config.run.max_deltas,
      fsim::app::SimulationEngine::debug};
  fsim::app::DebuggerControl debugger{
      matrix_debug_simulation,
      matrix_debug_output,
      matrix_debug_error,
      matrix_debug_config,
      matrix_debug_diagnostics};
  debugger.execute({"scope", matrix_prefix});
  debugger.execute({"scopes"});
  debugger.execute({"signals", matrix_prefix});
  debugger.execute({"run"});
}
assert(!matrix_debug_diagnostics.has_error());
assert(matrix_debug_error.str().empty());
assert(
    matrix_debug_output.str().find(matrix_prefix + ".leaf")
    != std::string::npos);
assert(
    matrix_debug_output.str().find(matrix_prefix + ".value_endpoint")
    != std::string::npos);
std::ifstream matrix_trace_input(matrix_trace, std::ios::binary);
const std::string matrix_trace_text{
    std::istreambuf_iterator<char>{matrix_trace_input},
    std::istreambuf_iterator<char>{}};
assert(matrix_trace_text.find("$scope module u_matrix $end")
       != std::string::npos);
assert(matrix_trace_text.find(" value_endpoint $end")
       != std::string::npos);
assert(matrix_trace_text.find(" custom_port $end")
       == std::string::npos);

{
  std::ofstream edited_source(systemc_source, std::ios::app);
  edited_source << "\n// named-object matrix cache edit\n";
}
fsim::diagnostic::Engine matrix_edit_diagnostics;
auto matrix_edited_project = fsim::app::build_project(
    named_matrix_config, matrix_edit_diagnostics);
assert(matrix_edited_project && !matrix_edited_project->cache_hit);
const auto matrix_edited_capture = run_named_matrix(
    std::move(*matrix_edited_project),
    fsim::app::SimulationEngine::compiled);
assert(
    std::get<0>(matrix_edited_capture).status
        == std::get<0>(matrix_reference_capture).status
    && std::get<0>(matrix_edited_capture).time
        == std::get<0>(matrix_reference_capture).time
    && std::get<1>(matrix_edited_capture)
        == std::get<1>(matrix_reference_capture));
#if defined(FSIM_HAS_LLVM)
assert(std::get<2>(matrix_edited_capture).misses > 0);
#endif

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

auto unbound_native_config = hdl_systemc_config;
unbound_native_config.project.top =
    "systemc:models.unbound_native_hierarchy";
unbound_native_config.bindings.clear();
fsim::diagnostic::Engine unbound_native_diagnostics;
assert(!fsim::app::build_project(
    unbound_native_config, unbound_native_diagnostics));
assert(std::count_if(
           unbound_native_diagnostics.diagnostics().begin(),
           unbound_native_diagnostics.diagnostics().end(),
           [](const fsim::diagnostic::Diagnostic& diagnostic) {
             return diagnostic.code == "FSIM-ELAB-BIND-058";
           })
       == 2);

auto direction_probe_config = hdl_systemc_config;
direction_probe_config.project.top =
    "systemc:models.port_direction_probe";
direction_probe_config.bindings.clear();
fsim::diagnostic::Engine direction_probe_diagnostics;
const auto direction_probe_project = fsim::app::build_project(
    direction_probe_config, direction_probe_diagnostics);
assert(direction_probe_project);
assert(!direction_probe_diagnostics.has_error());
assert(
    direction_probe_project->design.systemc_instances().size()
    == 2);

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
const auto thread_static_count =
    systemc_thread_project->design.find_signal("static_count");
const auto thread_named_count =
    systemc_thread_project->design.find_signal("named_count");
const auto thread_timeout_count =
    systemc_thread_project->design.find_signal("timeout_count");
assert(
    thread_count && thread_timed && thread_event_count
    && thread_static_count && thread_named_count
    && thread_timeout_count);
const auto& static_matrix_processes =
    systemc_thread_project->design.processes();
const auto static_matrix_process =
    [&](const std::string_view suffix) -> const fsim::runtime::simir::Process& {
      const auto found = std::find_if(
          static_matrix_processes.begin(), static_matrix_processes.end(),
          [&](const fsim::runtime::simir::Process& process) {
            return process.name.ends_with(suffix);
          });
      assert(found != static_matrix_processes.end());
      return *found;
    };
const auto& clocked_process = static_matrix_process(".clocked_run");
const auto& named_process = static_matrix_process(".named_run");
const auto& static_process = static_matrix_process(".static_run");
assert(
    clocked_process.static_sensitivity.size() == 1
    && clocked_process.static_sensitivity.front().edge
        == fsim::runtime::simir::EdgeKind::posedge);
assert(
    named_process.static_sensitivity.size() == 1
    && named_process.static_sensitivity.front().edge
        == fsim::runtime::simir::EdgeKind::any);
assert(
    static_process.static_sensitivity.size() == 1
    && static_process.static_sensitivity.front().edge
        == fsim::runtime::simir::EdgeKind::negedge
    && !static_process.initialize);
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
assert(systemc_thread_result.time == 5);
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
assert(
    systemc_thread_simulation
        .read_signal(*thread_static_count)
        .to_msb_string()
    == "00000001");
assert(
    systemc_thread_simulation
        .read_signal(*thread_named_count)
        .to_msb_string()
    == "00000010");
assert(
    systemc_thread_simulation
        .read_signal(*thread_timeout_count)
        .to_msb_string()
    == "00000100");

auto vhdl_thread_config = systemc_thread_config;
vhdl_thread_config.project.top =
    "vhdl:work.systemc_thread_vhdl_host(rtl)";
vhdl_thread_config.source_sets.front().language =
    fsim::project::Language::vhdl;
vhdl_thread_config.source_sets.front().standard = "2008";
vhdl_thread_config.source_sets.front().files = {
    systemc_method_vhdl_source};
vhdl_thread_config.bindings = {
    {"systemc_thread_vhdl_host.u_threads",
     "systemc:models.fiber_threads",
     std::nullopt},
};
fsim::diagnostic::Engine vhdl_thread_diagnostics;
auto vhdl_thread_project = fsim::app::build_project(
    vhdl_thread_config, vhdl_thread_diagnostics);
assert(vhdl_thread_project);
const auto vhdl_thread_count =
    vhdl_thread_project->design.find_signal("count");
const auto vhdl_thread_timed =
    vhdl_thread_project->design.find_signal("timed");
const auto vhdl_thread_event =
    vhdl_thread_project->design.find_signal("event_count");
const auto vhdl_thread_static =
    vhdl_thread_project->design.find_signal("static_count");
const auto vhdl_thread_named =
    vhdl_thread_project->design.find_signal("named_count");
const auto vhdl_thread_timeout =
    vhdl_thread_project->design.find_signal("timeout_count");
assert(
    vhdl_thread_count && vhdl_thread_timed && vhdl_thread_event
    && vhdl_thread_static && vhdl_thread_named
    && vhdl_thread_timeout);
fsim::app::Simulation vhdl_thread_simulation{
    std::move(*vhdl_thread_project),
    vhdl_thread_config.run.max_deltas,
    fsim::app::SimulationEngine::compiled};
const auto vhdl_thread_result = vhdl_thread_simulation.run();
assert(
    vhdl_thread_result.status == fsim::runtime::RunStatus::completed
    && vhdl_thread_result.time == 5);
for (const auto& [signal, expected] : {
         std::pair{*vhdl_thread_count, "00000010"},
         std::pair{*vhdl_thread_timed, "00000011"},
         std::pair{*vhdl_thread_event, "00000010"},
         std::pair{*vhdl_thread_static, "00000001"},
         std::pair{*vhdl_thread_named, "00000010"},
         std::pair{*vhdl_thread_timeout, "00000100"}}) {
  assert(
      vhdl_thread_simulation.read_signal(signal).to_msb_string()
      == expected);
}
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
assert(systemc_method_reference->specialization_cache_keys.size() == 1);
assert(
    systemc_method_compiled->specialization_cache_keys
    == systemc_method_reference->specialization_cache_keys);
const auto method_runtime_identity =
    systemc_method_reference->specialization_cache_keys.front();

// Keep the HDL source and specialization unchanged while selecting a
// different compatible factory, then a factory with a nested native child.
// Native specialization provenance must follow the selected factory schema,
// typed construction tuple, and stable hierarchy rather than transient image
// addresses or handles.
auto alternate_factory_config = systemc_method_config;
alternate_factory_config.bindings.front().target =
    "systemc:models.bound_ports";
fsim::diagnostic::Engine alternate_factory_diagnostics;
auto alternate_factory_project = fsim::app::build_project(
    alternate_factory_config, alternate_factory_diagnostics);
assert(alternate_factory_project);
assert(alternate_factory_project->specialization_cache_keys.size() == 1);
assert(
    alternate_factory_project->specialization_cache_keys.front()
    != method_runtime_identity);
assert(
    alternate_factory_project->design.systemc_instances().front().target
    == "systemc:models.bound_ports");

auto alternate_hierarchy_config = systemc_method_config;
alternate_hierarchy_config.bindings.front().target =
    "systemc:models.native_hierarchy";
fsim::diagnostic::Engine alternate_hierarchy_diagnostics;
auto alternate_hierarchy_project = fsim::app::build_project(
    alternate_hierarchy_config, alternate_hierarchy_diagnostics);
assert(alternate_hierarchy_project);
assert(alternate_hierarchy_project->specialization_cache_keys.size() == 1);
assert(
    alternate_hierarchy_project->specialization_cache_keys.front()
    != method_runtime_identity);
assert(
    alternate_hierarchy_project->specialization_cache_keys.front()
    != alternate_factory_project->specialization_cache_keys.front());
assert(alternate_hierarchy_project->design.systemc_instances().size() == 2);
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

auto timed_method_config = systemc_method_top_config;
timed_method_config.project.top =
    "systemc:models.timed_method_triggers";
fsim::diagnostic::Engine timed_method_diagnostics;
auto timed_method_reference = fsim::app::build_project(
    timed_method_config, timed_method_diagnostics);
auto timed_method_compiled = fsim::app::build_project(
    timed_method_config, timed_method_diagnostics);
assert(timed_method_reference && timed_method_compiled);
const auto run_timed_methods =
    [&](fsim::app::BuiltProject project,
        const fsim::app::SimulationEngine engine) {
      const auto single =
          project.design.find_signal("timed_method_triggers.single_seen");
      const auto or_seen =
          project.design.find_signal("timed_method_triggers.or_seen");
      const auto and_seen =
          project.design.find_signal("timed_method_triggers.and_seen");
      assert(single && or_seen && and_seen);
      fsim::app::Simulation simulation{
          std::move(project),
          timed_method_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::tuple{
          result,
          simulation.read_signal(*single).to_msb_string(),
          simulation.read_signal(*or_seen).to_msb_string(),
          simulation.read_signal(*and_seen).to_msb_string()};
    };
const auto timed_method_reference_capture = run_timed_methods(
    std::move(*timed_method_reference),
    fsim::app::SimulationEngine::interpreter);
const auto timed_method_compiled_capture = run_timed_methods(
    std::move(*timed_method_compiled),
    fsim::app::SimulationEngine::compiled);
assert(
    std::get<0>(timed_method_reference_capture).status
    == fsim::runtime::RunStatus::completed);
assert(std::get<0>(timed_method_reference_capture).time == 4);
assert(std::get<1>(timed_method_reference_capture) == "00010111");
assert(std::get<2>(timed_method_reference_capture) == "00010111");
assert(std::get<3>(timed_method_reference_capture) == "00011000");
assert(
    std::get<0>(timed_method_reference_capture).status
        == std::get<0>(timed_method_compiled_capture).status
    && std::get<0>(timed_method_reference_capture).time
        == std::get<0>(timed_method_compiled_capture).time
    && std::get<1>(timed_method_reference_capture)
        == std::get<1>(timed_method_compiled_capture)
    && std::get<2>(timed_method_reference_capture)
        == std::get<2>(timed_method_compiled_capture)
    && std::get<3>(timed_method_reference_capture)
        == std::get<3>(timed_method_compiled_capture));

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
    == 2);
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
      const auto cross_updates =
          project.design.find_signal("kernel_channels.cross_updates");
      assert(value && updates && event_count && cross_updates);
      fsim::app::Simulation simulation{
          std::move(project),
          kernel_channel_config.run.max_deltas,
          engine};
      const auto result = simulation.run();
      return std::tuple{
          result,
          simulation.read_signal(*value).to_msb_string(),
          simulation.read_signal(*updates).to_msb_string(),
          simulation.read_signal(*event_count).to_msb_string(),
          simulation.read_signal(*cross_updates).to_msb_string()};
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
assert(std::get<2>(kernel_reference) == "00000100");
assert(std::get<3>(kernel_reference) == "00000001");
assert(std::get<4>(kernel_reference) == "00000001");
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
assert(
    std::get<4>(kernel_reference)
    == std::get<4>(kernel_compiled));

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
