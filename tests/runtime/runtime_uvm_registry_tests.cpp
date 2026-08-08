// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_registry.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

}  // namespace

void test_systemverilog_uvm_registry() {
  using namespace fsim::runtime;
  const auto class_descriptor = [](const std::string& specialization,
                                   const std::string& declared) {
    SystemVerilogClassDescriptor result;
    result.declared_type = declared;
    result.dynamic_type = specialization;
    result.specialization_identity = specialization;
    result.assignable_declared_types = {
        specialization, declared, "uvm_pkg::uvm_object"};
    return result;
  };

  SystemVerilogClassHeap heap{{64, 16'384}};
  bool mismatch_object{};
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
           {"work::item", "item"},
           {"work::item#(WIDTH=8)", "item#(WIDTH=8)"},
           {"work::component", "component"}}) {
    SystemVerilogUvmObjectDescriptor descriptor;
    descriptor.specialization_identity = specialization;
    descriptor.type_name = name;
    objects.register_type(std::move(descriptor));
  }
  SystemVerilogUvmComponentService components{
      heap, objects, {4, 32, 8, 8, 64, 256}};
  const auto root = components.create_root("registry");

  const auto make_object = [&](const std::string_view specialization,
                               const std::string_view name) {
    const auto selected = mismatch_object
        ? std::string{"work::component"}
        : std::string{specialization};
    const auto result = heap.allocate(class_descriptor(
        selected,
        selected == "work::component"
            ? "uvm_pkg::uvm_component"
            : "uvm_pkg::uvm_object"));
    objects.initialize(result, std::string{name});
    if (selected == "work::component") {
      components.initialize(result, "mismatch", 0, root);
    }
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
      heap, objects, components, make_object, make_component, {8, 128, 64}};

  const auto item = registry.register_type({
      0, "work::item", "work::item", "item",
      SystemVerilogUvmRegisteredKind::Object, false, 0});
  const auto parameterized = registry.register_type({
      0, "work::item#(WIDTH=8)", "work::item", "item#(WIDTH=8)",
      SystemVerilogUvmRegisteredKind::Object, true, 0});
  const auto component = registry.register_type({
      0, "work::component", "work::component", "component",
      SystemVerilogUvmRegisteredKind::Component, false, 0});

  require(
      registry.types().size() == 3
          && registry.types()[0].wrapper == item
          && registry.types()[1].wrapper == parameterized
          && registry.types()[2].wrapper == component
          && registry.snapshot(parameterized).parameterized
          && registry.wrapper_by_specialization("work::item") == item
          && registry.wrapper_by_name("item#(WIDTH=8)") == parameterized,
      "UVM wrapper registration must preserve stable order, exact "
      "specialization lookup, and parameterized type names");

  bool ambiguous_declaration_rejected{};
  try {
    (void)registry.unique_wrapper_by_declaration("work::item");
  } catch (const std::invalid_argument&) {
    ambiguous_declaration_rejected = true;
  }
  bool duplicate_specialization_rejected{};
  try {
    (void)registry.register_type({
        0, "work::item", "work::other", "other",
        SystemVerilogUvmRegisteredKind::Object, false, 0});
  } catch (const std::invalid_argument&) {
    duplicate_specialization_rejected = true;
  }
  bool duplicate_name_rejected{};
  try {
    (void)registry.register_type({
        0, "work::other", "work::other", "item",
        SystemVerilogUvmRegisteredKind::Object, false, 0});
  } catch (const std::invalid_argument&) {
    duplicate_name_rejected = true;
  }
  require(
      ambiguous_declaration_rejected && duplicate_specialization_rejected
          && duplicate_name_rejected && registry.size() == 3,
      "UVM registration must reject ambiguous declaration lookup and "
      "duplicate specialization or name identities transactionally");

  const auto first = registry.create_object_by_type(item, "first");
  const auto second = registry.create_object_by_name(
      "item#(WIDTH=8)", "second");
  const auto top = registry.create_component_by_type(
      component, "top", 0, root);
  const auto child = registry.create_component_by_name(
      "component", "child", top, root);
  require(
      heap.object(first).specialization_identity == "work::item"
          && objects.name(first) == "first"
          && heap.object(second).specialization_identity
              == "work::item#(WIDTH=8)"
          && objects.name(second) == "second"
          && components.full_name(child) == "top.child",
      "UVM wrapper creation by type and name must publish exact objects and "
      "components");

  bool object_as_component_rejected{};
  try {
    (void)registry.create_component_by_type(item, "bad", 0, root);
  } catch (const std::invalid_argument&) {
    object_as_component_rejected = true;
  }
  bool component_as_object_rejected{};
  try {
    (void)registry.create_object_by_type(component, "bad");
  } catch (const std::invalid_argument&) {
    component_as_object_rejected = true;
  }
  const auto live_before_mismatch = heap.live_objects();
  mismatch_object = true;
  bool mismatch_rejected{};
  try {
    (void)registry.create_object_by_type(item, "wrong");
  } catch (const std::invalid_argument&) {
    mismatch_rejected = true;
  }
  mismatch_object = false;
  require(
      object_as_component_rejected && component_as_object_rejected
          && mismatch_rejected && heap.live_objects() == live_before_mismatch
          && components.top_components(root)
              == std::vector<SystemVerilogClassHandle>{top},
      "UVM wrapper kind and callback-specialization mismatches must reject "
      "without leaking heap or hierarchy ownership");

  objects.erase(first);
  objects.erase(second);
  require(heap.release(first) && heap.release(second),
          "registry-created objects must remain caller-owned");
  components.destroy_root(root);
  require(heap.live_objects() == 0,
          "registry-created components must follow root lifecycle ownership");
}

}  // namespace fsim::tests::runtime
