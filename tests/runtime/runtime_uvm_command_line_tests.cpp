// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_command_line.hpp"

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
          && settings.resource_db_trace && settings.config_db_trace,
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
          && config.entries().size() == 3 && resources.size() == 3,
      "config integers must apply before bitstreams, repeated strings must "
      "remain source ordered, and both must publish through the resource pool");

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
}

}  // namespace fsim::tests::runtime
