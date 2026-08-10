// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_command_line.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_report.hpp"


#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

fsim::runtime::SystemVerilogClassDescriptor class_descriptor(
    const std::string& specialization,
    const std::string& declared) {
  fsim::runtime::SystemVerilogClassDescriptor result;
  result.declared_type = declared;
  result.dynamic_type = specialization;
  result.specialization_identity = specialization;
  result.assignable_declared_types = {
      specialization, declared, "uvm_pkg::uvm_object"};
  return result;
}

}  // namespace

void test_systemverilog_uvm_command_line() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap{{128, 16'384}};
  SystemVerilogUvmObjectService objects{
      heap,
      [&](const std::string_view specialization,
          const std::string_view declared,
          const std::string_view name) {
        const auto result = heap.allocate(class_descriptor(
            std::string{specialization}, std::string{declared}));
        objects.initialize(result, std::string{name});
        return result;
      }};
  for (const auto& [specialization, name] :
       std::vector<std::pair<std::string, std::string>>{
           {"work::base", "base"},
           {"work::fast", "fast"},
           {"work::final", "final"}}) {
    SystemVerilogUvmObjectDescriptor descriptor;
    descriptor.specialization_identity = specialization;
    descriptor.type_name = name;
    objects.register_type(std::move(descriptor));
  }
  SystemVerilogUvmComponentService components{heap, objects};
  const auto make_object = [&](const std::string_view specialization,
                               const std::string_view name) {
    const auto result = heap.allocate(class_descriptor(
        std::string{specialization}, "uvm_pkg::uvm_object"));
    objects.initialize(result, std::string{name});
    return result;
  };
  const auto make_component = [&](const std::string_view specialization,
                                  const std::string_view name,
                                  const SystemVerilogClassHandle parent,
                                  const SystemVerilogUvmRootHandle root) {
    const auto result = heap.allocate(class_descriptor(
        std::string{specialization}, "uvm_pkg::uvm_component"));
    objects.initialize(result);
    components.initialize(result, std::string{name}, parent, root);
    return result;
  };
  SystemVerilogUvmRegistryService registry{
      heap, objects, components, make_object, make_component};
  const auto register_object = [&](const std::string_view specialization,
                                   const std::string_view name) {
    return registry.register_type({
        0, std::string{specialization}, std::string{specialization},
        std::string{name}, SystemVerilogUvmRegisteredKind::Object, false, 0});
  };
  const auto base = register_object("work::base", "base");
  const auto fast = register_object("work::fast", "fast");
  const auto final_type = register_object("work::final", "final");
  SystemVerilogUvmFactoryService factory{registry};
  SystemVerilogUvmResourcePoolService resources{&heap};
  SystemVerilogUvmConfigDbService config{resources};
  SystemVerilogUvmCommandLineService command_line{
      factory, resources, config};
  const auto report_root = components.create_root("command-line");
  const auto report_top =
      make_component("work::base", "top", 0, report_root);
  const auto report_agent =
      make_component("work::fast", "agent", report_top, report_root);
  SystemVerilogUvmPhaseService phases{components};
  SystemVerilogUvmObjectionService objections{
      objects, components, phases};
  SystemVerilogUvmReportService reports{objects, components};

  const std::vector<std::string> arguments{
      "+uvm_set_type_override=base,fast,0",
      "+UVM_SET_INST_OVERRIDE=base,final,top.*",
      "+uvm_set_type_override=base,final,0",
      "+uvm_set_config_bitstream=top.*,count,'h2a",
      "+UVM_SET_CONFIG_INT=top.*,count,5",
      "+uvm_set_config_int=top.*,decimal,1_005",
      "+uvm_set_config_string=top.*,mode,first",
      "+uvm_set_config_string=top.*,mode,second",
      "+UVM_VERBOSITY=UVM_HIGH",
      "+UVM_VERBOSITY=UVM_LOW",
      "+uvm_set_verbosity=top.*,_ALL_,UVM_FULL,build",
      "+uvm_set_verbosity=top.*,_ALL_,UVM_HIGH,time,20",
      "+UVM_TIMEOUT=100,NO",
      "+UVM_TIMEOUT=200,YES",
      "+UVM_MAX_QUIT_COUNT=7,NO",
      "+UVM_MAX_QUIT_COUNT=9,YES",
      "+UVM_PHASE_TRACE",
      "+UVM_OBJECTION_TRACE",
      "+UVM_RESOURCE_DB_TRACE",
      "+UVM_CONFIG_DB_TRACE",
      "+USER_OPTION=retained",
      "+USER_FLAG"};
  command_line.apply(arguments);

  const auto& settings = command_line.settings();
  require(
      settings.arguments == arguments
          && settings.unknown_arguments
              == std::vector<std::string>{
                  "+USER_OPTION=retained", "+USER_FLAG"}
          && settings.factory_settings.size() == 3
          && settings.config_settings.size() == 5
          && settings.verbosity_settings.size() == 2
          && settings.initial_verbosity == 300
          && settings.initial_verbosity_argument_count == 2
          && settings.timeout && settings.timeout->ticks == 100
          && !settings.timeout->overridable
          && settings.timeout_argument_count == 2
          && settings.max_quit_count
          && settings.max_quit_count->count == 7
          && !settings.max_quit_count->overridable
          && settings.max_quit_count_argument_count == 2
          && settings.phase_trace && settings.objection_trace
          && settings.resource_db_trace && settings.config_db_trace
          && resources.trace_enabled() && config.trace_enabled(),
      "UVM command-line parsing must retain all and unknown arguments while "
      "using the first repeated global verbosity and timeout settings");
  require(
      settings.verbosity_settings[0].phase == "build"
          && !settings.verbosity_settings[0].time_offset
          && settings.verbosity_settings[1].phase == "time"
          && settings.verbosity_settings[1].time_offset == 20
          && settings.verbosity_settings[0].source_index
              < settings.verbosity_settings[1].source_index,
      "phase and timed verbosity settings must retain command-line order");
  require(
          command_line.get_args() == arguments &&
          command_line.get_plusargs() == arguments &&
          command_line.get_uvm_args().size() == 20 &&
          command_line.get_arg_matches(
              "+UVM_VERBOSITY=UVM_LOW", true) ==
              std::vector<std::string>{"+UVM_VERBOSITY=UVM_LOW"} &&
          command_line.get_arg_values("+UVM_VERBOSITY=") ==
              std::vector<std::string>{"UVM_HIGH", "UVM_LOW"} &&
          command_line.get_arg_value("+UVM_VERBOSITY=") == "UVM_HIGH" &&
          command_line.get_arg_matches(
              "+uvm_set_config_string=").size() == 2 &&
          command_line.get_tool_name() == "fsim" &&
          command_line.get_tool_version() == "v2",
      "UVM command-line processor queries must preserve ordered argv, plusarg "
      "and UVM subsets, exact/prefix matches, duplicate values, and tool "
      "identity");

  reports.set_verbosity_hier(report_top, 100);
  command_line.apply_initial_report_settings(reports, objections);
  const auto build_settings = command_line.apply_report_settings(
      components, reports, "build", 0);
  const auto early_time_settings = command_line.apply_report_settings(
      components, reports, "time", 19);
  const auto timed_settings = command_line.apply_report_settings(
      components, reports, "time", 20);
  const auto repeated_settings = command_line.apply_report_settings(
      components, reports, "time", 21);
  require(
      reports.default_verbosity() == 300
          && reports.server().max_quit_count() == 7
          && !reports.server().max_quit_overridable()
          && objections.trace_enabled()
          && build_settings.size() == 1
          && build_settings.front().component == report_agent
          && build_settings.front().verbosity == 400
          && early_time_settings.empty() && timed_settings.size() == 1
          && timed_settings.front().component == report_agent
          && timed_settings.front().verbosity == 300
          && repeated_settings.empty()
          && reports.enabled(
              report_agent, 300, SystemVerilogUvmReportSeverity::Info,
              "CONTROL")
          && !reports.enabled(
              report_agent, 400, SystemVerilogUvmReportSeverity::Info,
              "CONTROL"),
      "initial, phase, and timed report controls must apply in command order "
      "once while max-quit and objection tracing retain first precedence");

  SystemVerilogUvmCommandLineService isolated{
      factory, resources, config, {}, "fsim-isolated", "v2-test"};
  const std::vector<std::string> isolated_arguments{
      "fsim", "+SECOND=1", "-quiet"};
  isolated.apply(isolated_arguments);
  require(
      isolated.get_args() == isolated_arguments &&
          isolated.get_plusargs() == std::vector<std::string>{"+SECOND=1"} &&
          isolated.get_uvm_args().empty() &&
          isolated.get_tool_name() == "fsim-isolated" &&
          isolated.get_tool_version() == "v2-test" &&
          command_line.get_args() == arguments,
      "UVM command-line argv and tool identity must remain simulation-local");

  const auto instance_resolution = factory.debug_resolve_by_type(
      base, "top.agent");
  const auto type_resolution = factory.debug_resolve_by_type(base);
  require(
      instance_resolution.resolved == final_type
          && type_resolution.resolved == fast
          && factory.instance_overrides().size() == 1
          && factory.type_overrides().size() == 1
          && factory.instance_overrides().front().registration_order
              < factory.type_overrides().front().registration_order,
      "factory instance settings must apply before source-ordered type "
      "settings and replace=0 must preserve the first type override");

  const SystemVerilogUvmConfigContext root{"", 0};
  const auto packed_value = config.get(
      root, "top.agent", "count", kSystemVerilogUvmBitstreamConfigType);
  const auto string_value = config.get(
      root, "top.agent", "mode", kSystemVerilogUvmStringConfigType);
  const auto decimal_value = config.get(
      root, "top.agent", "decimal", kSystemVerilogUvmBitstreamConfigType);
  require(
      packed_value
          && std::get<PackedLogic4>(*packed_value).width()
              == kSystemVerilogUvmBitstreamWidth
          && std::get<PackedLogic4>(*packed_value).aval_words().front() == 0x2a
          && string_value && std::get<std::string>(*string_value) == "second"
          && decimal_value
          && std::get<PackedLogic4>(*decimal_value).aval_words().front()
              == 1'005
          && config.entries().size() == 3 && resources.size() == 3
          && config.trace_records().size() == 3
          && resources.trace_records().size() == 3
          && config.trace_text().find("UVM_CONFIG_DB_TRACE") == 0
          && resources.trace_text().find("UVM_RESOURCE_DB_TRACE") == 0,
      "config integers must apply before bitstreams, repeated strings must "
      "remain source ordered, publish through the resource pool, and honor "
      "both command-line trace switches");

  const auto config_size = config.entries().size();
  const auto resource_size = resources.size();
  const std::vector<std::string> malformed_arguments{
      "+uvm_set_config_string=top.*,late,unpublished",
      "+UVM_TIMEOUT=invalid"};
  bool malformed_rejected{};
  try {
    command_line.apply(malformed_arguments);
  } catch (const SystemVerilogUvmCommandLineError& error) {
    malformed_rejected = error.argument_index() == 1
        && error.argument() == "+UVM_TIMEOUT=invalid";
  }
  require(
      malformed_rejected && config.entries().size() == config_size
          && resources.size() == resource_size
          && !config.exists(
              root, "top.agent", "late", kSystemVerilogUvmStringConfigType),
      "all recognized plusargs must parse before malformed late settings can "
      "publish factory, config, or resource state");

  const std::vector<std::string> missing_value{"+UVM_TIMEOUT"};
  bool missing_equal_rejected{};
  try {
    command_line.apply(missing_value);
  } catch (const SystemVerilogUvmCommandLineError&) {
    missing_equal_rejected = true;
  }
  require(
      missing_equal_rejected,
      "recognized value plusargs must diagnose a missing equals delimiter");

  SystemVerilogUvmCommandLineLimits tiny_limits;
  tiny_limits.max_argument_bytes = 8;
  SystemVerilogUvmCommandLineService tiny{
      factory, resources, config, tiny_limits};
  const std::vector<std::string> oversized_unknown{
      "+USER_OPTION=too-long"};
  bool byte_limit_rejected{};
  try {
    tiny.apply(oversized_unknown);
  } catch (const SystemVerilogUvmCommandLineError&) {
    byte_limit_rejected = true;
  }
  require(
      byte_limit_rejected,
      "unknown plusargs must remain bounded before being retained for HDL");

  tiny_limits.max_argument_bytes = 64;
  tiny_limits.max_total_argument_bytes = 8;
  SystemVerilogUvmCommandLineService total_limited{
      factory, resources, config, tiny_limits};
  bool total_limit_rejected{};
  try {
    total_limited.apply(oversized_unknown);
  } catch (const SystemVerilogUvmCommandLineError&) {
    total_limit_rejected = true;
  }
  require(
      total_limit_rejected,
      "total retained plusarg bytes must reject without unsigned underflow");

  SystemVerilogUvmCommandLineLimits query_limits;
  query_limits.max_query_results = 1;
  SystemVerilogUvmCommandLineService query_limited{
      factory, resources, config, query_limits};
  const std::vector<std::string> duplicate_values{"+DUP=first", "+DUP=second"};
  query_limited.apply(duplicate_values);
  bool malformed_query{};
  bool bounded_query{};
  try {
    (void)query_limited.get_arg_matches("");
  } catch (const SystemVerilogUvmCommandLineError& error) {
    malformed_query = error.diagnostic_code() == "FSIM-UVM-CMD-001";
  }
  try {
    (void)query_limited.get_arg_values("+DUP=");
  } catch (const SystemVerilogUvmCommandLineError& error) {
    bounded_query = error.diagnostic_code() == "FSIM-UVM-CMD-002";
  }
  require(
      malformed_query && bounded_query,
      "UVM command-line queries must catalog malformed and retained-result "
      "resource failures");

  SystemVerilogUvmCommandLineLimits report_limits;
  report_limits.max_report_control_matches = 1;
  SystemVerilogUvmCommandLineService report_limited{
      factory, resources, config, report_limits};
  report_limited.apply(std::vector<std::string>{
      "+uvm_set_verbosity=top*,_ALL_,UVM_FULL,build"});
  SystemVerilogUvmReportService bounded_reports{objects, components};
  bool report_limit_rejected{};
  try {
    (void)report_limited.apply_report_settings(
        components, bounded_reports, "build", 0);
  } catch (const SystemVerilogUvmCommandLineError& error) {
    report_limit_rejected =
        error.diagnostic_code() == "FSIM-UVM-CMD-002";
  }
  require(
      report_limit_rejected && bounded_reports.handler_count() == 0,
      "bounded report-control matching must reject transactionally before "
      "publishing handler state");
}

}  // namespace fsim::tests::runtime
