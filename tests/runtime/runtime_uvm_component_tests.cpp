// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_context.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

}  // namespace

void test_systemverilog_uvm_component() {
  using namespace fsim::runtime;
  static_assert(
      kSystemVerilogUvmOwnershipContract.type_registry
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.factory
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.resources
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.configuration
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.callbacks
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.command_line
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.reporting
          == SystemVerilogUvmStateScope::Simulation
      && kSystemVerilogUvmOwnershipContract.component_paths
          == SystemVerilogUvmStateScope::Root);
  const auto descriptor = [] {
    SystemVerilogClassDescriptor result;
    result.declared_type = "uvm_pkg::uvm_component";
    result.dynamic_type = "work::component";
    result.specialization_identity = "work::component";
    result.assignable_declared_types = {
        "work::component", "uvm_pkg::uvm_component",
        "uvm_pkg::uvm_object"};
    result.properties = {
        {"payload", SystemVerilogClassPropertyKind::Logic4, 32}};
    return result;
  };

  SystemVerilogClassHeap heap{{64, 16'384}};
  SystemVerilogUvmObjectService objects{
      heap,
      [&](const std::string_view, const std::string_view,
          const std::string_view) {
        return heap.allocate(descriptor());
      }};
  SystemVerilogUvmObjectDescriptor object_type;
  object_type.specialization_identity = "work::component";
  object_type.type_name = "component";
  object_type.fields = {
      {"payload", SystemVerilogUvmFieldFlag::None}};
  objects.register_type(std::move(object_type));
  SystemVerilogUvmComponentService components{
      heap, objects, {4, 32, 4, 8, 32, 128}};

  const auto first_root = components.create_root("first");
  const auto second_root = components.create_root("second");
  const auto make_component = [&](const std::string& name,
                                  const SystemVerilogClassHandle parent,
                                  const SystemVerilogUvmRootHandle root) {
    const auto result = heap.allocate(descriptor());
    objects.initialize(result);
    components.initialize(result, name, parent, root);
    return result;
  };
  const auto top = make_component("top", 0, first_root);
  const auto alpha = make_component("alpha", top, 0);
  const auto leaf = make_component("leaf", alpha, 0);
  const auto beta = make_component("beta", top, 0);
  const auto isolated_top = make_component("top", 0, second_root);

  require(
      components.roots()
              == std::vector<SystemVerilogUvmRootHandle>{
                  first_root, second_root}
          && components.top_components(first_root)
              == std::vector<SystemVerilogClassHandle>{top}
          && components.children(top)
              == std::vector<SystemVerilogClassHandle>{alpha, beta}
          && components.parent(leaf) == alpha
          && components.root_of(leaf) == first_root
          && components.snapshot(leaf).depth == 2
          && components.full_name(leaf) == "top.alpha.leaf"
          && objects.full_name(leaf) == "top.alpha.leaf",
      "UVM components must retain root ownership, creation order, parentage, "
      "depth, and canonical full names");
  require(
      components.lookup_root(first_root, "top.alpha.leaf") == leaf
          && components.lookup(alpha, "leaf") == leaf
          && components.lookup(beta, ".top.alpha.leaf") == leaf
          && components.find_child(top, "missing") == 0
          && components.lookup_root(second_root, "top") == isolated_top
          && components.lookup_root(second_root, "top.alpha") == 0,
      "UVM component lookup must support exact root-scoped, relative, "
      "absolute, missing, and multi-root-isolated paths");

  const auto rejected = heap.allocate(descriptor());
  objects.initialize(rejected);
  bool duplicate_rejected = false;
  try {
    components.initialize(rejected, "alpha", top);
  } catch (const std::invalid_argument&) {
    duplicate_rejected = true;
  }
  bool cross_root_rejected = false;
  try {
    components.initialize(rejected, "cross", top, second_root);
  } catch (const std::invalid_argument&) {
    cross_root_rejected = true;
  }
  bool dotted_rejected = false;
  try {
    components.initialize(rejected, "bad.name", 0, first_root);
  } catch (const std::invalid_argument&) {
    dotted_rejected = true;
  }
  const auto depth_parent = make_component("depth", leaf, 0);
  bool depth_rejected = false;
  try {
    components.initialize(rejected, "too_deep", depth_parent);
  } catch (const std::length_error&) {
    depth_rejected = true;
  }
  require(
      duplicate_rejected && cross_root_rejected && dotted_rejected
          && depth_rejected && !components.contains(rejected),
      "UVM component registration must reject duplicate, cross-root, "
      "malformed-name, and depth-excess cases transactionally");
  objects.erase(rejected);
  require(
      heap.release(rejected),
      "a rejected UVM component candidate must remain caller-owned");

  std::vector<std::string> pre_destroy;
  std::vector<std::string> post_destroy;
  components.set_pre_destroy_hook([&](const auto& value) {
    pre_destroy.push_back(value.full_name);
    require(
        value.state == SystemVerilogUvmComponentState::Destroying,
        "pre-destroy hooks must observe the destroying lifecycle state");
  });
  components.set_post_destroy_hook([&](const auto& value) {
    post_destroy.push_back(value.full_name);
  });
  const auto stale_top = top;
  components.release(top);
  const std::vector<std::string> expected_order{
      "top.alpha.leaf.depth", "top.alpha.leaf", "top.alpha",
      "top.beta", "top"};
  require(
      pre_destroy == expected_order && post_destroy == expected_order
          && components.component_count() == 1
          && components.top_components(first_root).empty()
          && components.contains(isolated_top)
          && !components.contains(stale_top)
          && !objects.contains(stale_top)
          && !heap.contains(stale_top),
      "UVM component teardown must be deterministic, leaf first, root "
      "isolated, and remove object plus heap ownership");

  const auto replacement = heap.allocate(descriptor());
  objects.initialize(replacement);
  components.initialize(replacement, "replacement", 0, first_root);
  bool stale_rejected = false;
  try {
    (void)components.parent(stale_top);
  } catch (const std::out_of_range&) {
    stale_rejected = true;
  }
  require(
      stale_rejected && replacement != stale_top,
      "UVM component generation-qualified stale handles must reject after "
      "heap-slot reuse");

  components.destroy_root(second_root);
  require(
      !components.contains_root(second_root)
          && components.contains_root(first_root)
          && components.component_count() == 1,
      "destroying one UVM root must not disturb another root");
  components.destroy_root(first_root);
  require(
      components.roots().empty() && components.component_count() == 0
          && heap.live_objects() == 0,
      "multi-root UVM teardown must release every owned component");
}

}  // namespace fsim::tests::runtime
