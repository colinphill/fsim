// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_factory.hpp"

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
}  // namespace

}

void test_systemverilog_uvm_factory() {
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
  const std::vector<std::pair<std::string, std::string>> object_types{
      {"work::base", "base"},
      {"work::fast", "fast"},
      {"work::final", "final"},
      {"work::component", "component"},
      {"work::alt_component", "alt_component"}};
  for (const auto& [specialization, name] : object_types) {
    SystemVerilogUvmObjectDescriptor descriptor;
    descriptor.specialization_identity = specialization;
    descriptor.type_name = name;
    objects.register_type(std::move(descriptor));
  }
  SystemVerilogUvmComponentService components{
      heap, objects, {4, 64, 16, 16, 64, 256}};
  const auto root = components.create_root("factory");
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
                                  const SystemVerilogUvmRootHandle owner) {
    const auto result = heap.allocate(class_descriptor(
        std::string{specialization}, "uvm_pkg::uvm_component"));
    objects.initialize(result);
    components.initialize(result, std::string{name}, parent, owner);
    return result;
  };
  SystemVerilogUvmRegistryService registry{
      heap, objects, components, make_object, make_component, {16, 128, 64}};
  const auto register_type = [&](const std::string_view specialization,
                                 const std::string_view name,
                                 const SystemVerilogUvmRegisteredKind kind) {
    return registry.register_type({
        0,
        std::string{specialization},
        std::string{specialization},
        std::string{name},
        kind,
        false,
        0});
  };
  const auto base = register_type(
      "work::base", "base", SystemVerilogUvmRegisteredKind::Object);
  const auto fast = register_type(
      "work::fast", "fast", SystemVerilogUvmRegisteredKind::Object);
  const auto final_type = register_type(
      "work::final", "final", SystemVerilogUvmRegisteredKind::Object);
  const auto component = register_type(
      "work::component", "component",
      SystemVerilogUvmRegisteredKind::Component);
  const auto alt_component = register_type(
      "work::alt_component", "alt_component",
      SystemVerilogUvmRegisteredKind::Component);

  SystemVerilogUvmFactoryService replacement_factory{registry};
  require(
      replacement_factory.set_type_override_by_type(base, fast)
          && !replacement_factory.set_type_override_by_type(
              base, final_type, false)
          && replacement_factory.resolve_by_type(base).resolved == fast
          && replacement_factory.set_type_override_by_name(
              "base", "final", true)
          && replacement_factory.resolve_by_name("base").resolved == final_type
          && replacement_factory.type_overrides().size() == 1,
      "UVM type override replacement must preserve the existing entry when "
      "replace is false and update it deterministically when true");

  SystemVerilogUvmFactoryService factory{registry};
  require(
      factory.set_instance_override_by_type(base, fast, "top.special")
          && factory.set_instance_override_by_type(base, final_type, "top.*")
          && !factory.set_instance_override_by_type(
              base, fast, "top.special")
          && factory.set_type_override_by_type(fast, final_type),
      "UVM instance override registration must preserve source order and "
      "reject exact duplicates");
  const auto exact = factory.resolve_by_type(base, "top.special");
  const auto wildcard = factory.resolve_by_name("base", "top.other");
  require(
      exact.resolved == final_type && exact.steps.size() == 2
          && exact.steps[0].kind == SystemVerilogUvmOverrideKind::Instance
          && exact.steps[0].instance_pattern == "top.special"
          && exact.steps[1].kind == SystemVerilogUvmOverrideKind::Type
          && wildcard.resolved == final_type && wildcard.steps.size() == 1
          && wildcard.steps[0].instance_pattern == "top.*",
      "UVM instance overrides must take precedence in registration order, "
      "match wildcards, and recursively chain through type overrides");

  require(
      factory.set_instance_override_by_name("legacy*", "fast", "env.?")
          && factory.resolve_by_name("legacy_item", "env.a").resolved
              == final_type,
      "UVM name-based instance overrides must support deferred original "
      "names and bounded wildcard type/path matching");
  const auto uses_before_debug = factory.instance_overrides()[0].uses;
  const auto debug = factory.debug_resolve_by_type(base, "top.special");
  require(
      debug.resolved == final_type && debug.steps.size() == 2
          && factory.instance_overrides()[0].uses == uses_before_debug,
      "UVM factory debug resolution must report the selected chain without "
      "mutating use counts");

  const auto object = factory.create_object_by_type(base, "top", "special");
  SystemVerilogUvmFactoryService component_factory{registry};
  require(component_factory.set_type_override_by_type(
              component, alt_component),
          "UVM component type override must register");
  const auto top = component_factory.create_component_by_name(
      "component", "", "top", 0, root);
  require(
      heap.object(object).specialization_identity == "work::final"
          && objects.name(object) == "special"
          && heap.object(top).specialization_identity == "work::alt_component"
          && components.full_name(top) == "top",
      "UVM factory creation must instantiate the recursively resolved object "
      "or component wrapper with the requested name and hierarchy context");

  SystemVerilogUvmFactoryService loop_factory{registry};
  require(
      loop_factory.set_type_override_by_type(base, fast)
          && loop_factory.set_type_override_by_type(fast, base),
      "UVM recursive-loop evidence must configure both edges");
  bool loop_rejected{};
  try {
    (void)loop_factory.resolve_by_type(base);
  } catch (const std::invalid_argument&) {
    loop_rejected = true;
  }
  require(
      loop_rejected && loop_factory.type_overrides()[0].uses == 0
          && loop_factory.type_overrides()[1].uses == 0,
      "recursive UVM factory overrides must reject without partially "
      "publishing resolution use counts");

  SystemVerilogUvmFactoryService limited{
      registry, {1, 1, 16, 8, 2, 64}};
  require(limited.set_type_override_by_type(base, fast),
          "bounded UVM factory must accept its first type override");
  bool type_limit_rejected{};
  try {
    (void)limited.set_type_override_by_type(fast, final_type);
  } catch (const std::length_error&) {
    type_limit_rejected = true;
  }
  bool path_limit_rejected{};
  try {
    (void)limited.set_instance_override_by_type(
        base, fast, "too.long.path");
  } catch (const std::length_error&) {
    path_limit_rejected = true;
  }
  bool report_limit_rejected{};
  try {
    (void)limited.report(true);
  } catch (const std::length_error&) {
    report_limit_rejected = true;
  }
  require(
      type_limit_rejected && path_limit_rejected && report_limit_rejected,
      "UVM factory override, instance-path, and report budgets must reject "
      "before unbounded growth");

  const auto report = factory.report(true);
  require(
      report.find("Registered Types\n") != std::string::npos
          && report.find("Type Overrides\n") != std::string::npos
          && report.find("base @ top.special -> fast") != std::string::npos
          && report.find("uses=") != std::string::npos,
      "UVM factory reports must deterministically include registered types, "
      "override order, selected targets, paths, and use counts");

  objects.erase(object);
  require(heap.release(object),
          "factory-created objects must remain caller-owned");
  components.destroy_root(root);
  require(heap.live_objects() == 0,
          "factory-created components must follow root lifecycle ownership");
}

}
