// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_test_runner.hpp"
#include "fsim/runtime/uvm_registry.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] fsim::runtime::SystemVerilogClassDescriptor descriptor(
    std::string specialization,
    std::string declared = "uvm_pkg::uvm_component") {
  fsim::runtime::SystemVerilogClassDescriptor result;
  result.declared_type = std::move(declared);
  result.dynamic_type = specialization;
  result.specialization_identity = std::move(specialization);
  result.assignable_declared_types = {
      result.dynamic_type, "uvm_pkg::uvm_object", "uvm_pkg::uvm_component"};
  return result;
}

}  // namespace

void test_systemverilog_uvm_test_runner() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap{{64, 1U << 16U}};
  SystemVerilogUvmObjectService objects{
      heap,
      [&](const auto specialization, const auto declared, const auto name) {
        const auto object = heap.allocate(
            descriptor(std::string{specialization}, std::string{declared}));
        objects.initialize(object, std::string{name});
        return object;
      }};
  for (const auto& name : {std::string{"test_a"}, std::string{"test_b"}}) {
    SystemVerilogUvmObjectDescriptor type;
    type.specialization_identity = "work::" + name;
    type.type_name = name;
    objects.register_type(std::move(type));
  }
  SystemVerilogUvmComponentService components{heap, objects};
  SystemVerilogUvmRegistryService registry{
      heap, objects, components,
      [&](const auto specialization, const auto name) {
        const auto object = heap.allocate(descriptor(
            std::string{specialization}, "uvm_pkg::uvm_object"));
        objects.initialize(object, std::string{name});
        return object;
      },
      [&](const auto specialization, const auto name, const auto parent,
          const auto root) {
        const auto object = heap.allocate(descriptor(std::string{specialization}));
        objects.initialize(object);
        components.initialize(object, std::string{name}, parent, root);
        return object;
      }};
  for (const auto& name : {std::string{"test_a"}, std::string{"test_b"}}) {
    (void)registry.register_type(
        {0, "work::" + name, "work::" + name, name,
         SystemVerilogUvmRegisteredKind::Component, false, 0});
  }
  SystemVerilogUvmFactoryService factory{registry};
  SystemVerilogUvmResourcePoolService resources{&heap};
  SystemVerilogUvmConfigDbService config{resources};
  SystemVerilogUvmCommandLineService command_line{factory, resources, config};
  const std::vector<std::string> arguments{
      "+UVM_TESTNAME=test_a", "+UVM_TESTNAME=test_b",
      "+ntb_random_seed=17", "+UVM_TIMEOUT=10,YES"};
  command_line.apply(arguments);
  SystemVerilogUvmTestRunnerService runner{
      objects, components, factory, command_line};
  const auto root = components.create_root("runner");

  const auto first = runner.run_test(
      root, {}, [&](const auto test, const auto seed) {
        require(
            components.full_name(test) == "uvm_test_top" && seed == 17,
            "run_test must apply first command test/seed selection");
        return SystemVerilogUvmRunExecution{
            SystemVerilogUvmRunStatus::Completed, 9, "completed"};
      });
  require(
      first.success() && first.test_name == "test_a" && first.seed == 17 &&
          first.timeout == 10 && first.elapsed_ticks == 9 && first.cleaned &&
          first.topology == "UVM Topology\nuvm_test_top (test_a)\n" &&
          components.component_count() == 0,
      "run_test must print deterministic topology and clean completed runs");

  SystemVerilogUvmRunOptions explicit_options;
  explicit_options.test_name = "test_b";
  explicit_options.seed = 23;
  explicit_options.timeout = 5;
  const auto finished = runner.run_test(
      root, explicit_options, [](const auto, const auto) {
        return SystemVerilogUvmRunExecution{
            SystemVerilogUvmRunStatus::Finished, 5, "$finish"};
      });
  require(
      finished.success() && finished.test_name == "test_b" &&
          finished.seed == 23 && finished.status ==
              SystemVerilogUvmRunStatus::Finished && finished.cleaned,
      "explicit run_test options must override command selection and retain "
      "$finish as successful termination");

  const auto timed_out = runner.run_test(
      root, {}, [](const auto, const auto) {
        return SystemVerilogUvmRunExecution{
            SystemVerilogUvmRunStatus::Completed, 11, "late"};
      });
  const auto fatal = runner.run_test(
      root, explicit_options, [](const auto, const auto) {
        return SystemVerilogUvmRunExecution{
            SystemVerilogUvmRunStatus::Fatal, 1, "$fatal"};
      });
  const auto thrown = runner.run_test(
      root, explicit_options, [](const auto, const auto)
          -> SystemVerilogUvmRunExecution {
        throw std::runtime_error{"contained test exception"};
      });
  require(
      !timed_out.success() &&
          timed_out.status == SystemVerilogUvmRunStatus::TimedOut &&
          timed_out.diagnostic_code == "FSIM-UVM-RUN-002" &&
          fatal.diagnostic_code == "FSIM-UVM-RUN-003" &&
          thrown.diagnostic_code == "FSIM-UVM-RUN-003" &&
          components.component_count() == 0 && runner.run_count() == 5,
      "run_test must contain timeout, fatal, and exception termination while "
      "cleaning repeated runs");

  bool reentry_rejected{};
  const auto reentry = runner.run_test(
      root, explicit_options, [&](const auto, const auto) {
        try {
          (void)runner.run_test(root, explicit_options);
        } catch (const SystemVerilogUvmRunError& error) {
          reentry_rejected =
              error.diagnostic_code() == "FSIM-UVM-RUN-001";
        }
        return SystemVerilogUvmRunExecution{};
      });
  require(reentry.success() && reentry_rejected,
          "run_test must reject deterministic same-simulation reentry");

  SystemVerilogUvmCommandLineService empty_command{
      factory, resources, config};
  empty_command.apply(std::span<const std::string>{});
  SystemVerilogUvmTestRunnerService missing{
      objects, components, factory, empty_command};
  bool missing_rejected{};
  try {
    (void)missing.run_test(root);
  } catch (const SystemVerilogUvmRunError& error) {
    missing_rejected = error.diagnostic_code() == "FSIM-UVM-RUN-001";
  }
  SystemVerilogUvmRunLimits tiny_limits;
  tiny_limits.maximum_topology_bytes = 1;
  SystemVerilogUvmTestRunnerService bounded{
      objects, components, factory, command_line, tiny_limits};
  bool bounded_rejected{};
  try {
    (void)bounded.run_test(root);
  } catch (const SystemVerilogUvmRunError& error) {
    bounded_rejected = error.diagnostic_code() == "FSIM-UVM-RUN-002";
  }
  require(
      missing_rejected && bounded_rejected && components.component_count() == 0,
      "run_test must catalog missing selection and topology resource failures "
      "without leaking test components");
}

}  // namespace fsim::tests::runtime
