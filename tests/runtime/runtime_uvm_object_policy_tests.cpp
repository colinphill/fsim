// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::tests::runtime {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition)
    throw std::runtime_error{std::string{message}};
}

[[nodiscard]] fsim::runtime::SystemVerilogClassDescriptor
node_descriptor(std::string specialization = "work::policy_node",
                std::string dynamic_type = "work::policy_node") {
  using namespace fsim::runtime;
  SystemVerilogClassDescriptor result;
  result.declared_type = "uvm_pkg::uvm_object";
  result.dynamic_type = std::move(dynamic_type);
  result.specialization_identity = std::move(specialization);
  result.assignable_declared_types = {result.dynamic_type,
                                      "uvm_pkg::uvm_object"};
  result.properties = {
      {"value", SystemVerilogClassPropertyKind::Logic4, 8},
      {"label", SystemVerilogClassPropertyKind::String, 0},
      {"child", SystemVerilogClassPropertyKind::ClassHandle, 64},
      {"children", SystemVerilogClassPropertyKind::Container, 0},
      {"physical", SystemVerilogClassPropertyKind::Logic4, 8},
      {"abstract", SystemVerilogClassPropertyKind::Logic4, 8},
      {"reference", SystemVerilogClassPropertyKind::ClassHandle, 64}};
  SystemVerilogClassHandleContainerDescriptor associative;
  associative.kind = SystemVerilogClassContainerKind::AssociativeArray;
  associative.declared_element_type = "uvm_pkg::uvm_object";
  associative.maximum_elements = 8;
  associative.reserve_maximum_storage = false;
  result.properties.emplace_back("associative",
                                 SystemVerilogClassPropertyKind::Container, 0,
                                 std::move(associative));
  return result;
}

[[nodiscard]] fsim::runtime::SystemVerilogUvmObjectDescriptor
object_descriptor(std::string specialization = "work::policy_node",
                  std::string type_name = "policy_node") {
  using namespace fsim::runtime;
  SystemVerilogUvmObjectDescriptor result;
  result.specialization_identity = std::move(specialization);
  result.type_name = std::move(type_name);
  result.fields = {{"value", SystemVerilogUvmFieldFlag::None},
                   {"label", SystemVerilogUvmFieldFlag::None},
                   {"child", SystemVerilogUvmFieldFlag::None},
                   {"children", SystemVerilogUvmFieldFlag::None},
                   {"associative", SystemVerilogUvmFieldFlag::None},
                   {"physical", SystemVerilogUvmFieldFlag::Physical},
                   {"abstract", SystemVerilogUvmFieldFlag::Abstract},
                   {"reference", SystemVerilogUvmFieldFlag::Reference}};
  return result;
}

void initialize_node(fsim::runtime::SystemVerilogClassHeap &heap,
                     const fsim::runtime::SystemVerilogClassHandle handle,
                     const std::uint64_t value) {
  using namespace fsim::runtime;
  heap.property(handle, "value").packed =
      PackedLogic4::from_aval_bval(8, value, 0);
  heap.property(handle, "label").string = "same\nlabel";
  heap.property(handle, "physical").packed =
      PackedLogic4::from_aval_bval(8, 11, 0);
  heap.property(handle, "abstract").packed =
      PackedLogic4::from_aval_bval(8, 22, 0);
}

} // namespace

void test_systemverilog_uvm_object_policies() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap{{64, 1U << 18U}};
  SystemVerilogUvmObjectService objects{
      heap,
      [&](const std::string_view specialization, const std::string_view,
          const std::string_view) {
        return heap.allocate(node_descriptor(
            std::string{specialization}, std::string{specialization}));
      },
      {32, 64, 512, 1U << 14U}};
  std::size_t copy_hooks{};
  bool fail_copy_hook{};
  auto policy_type = object_descriptor();
  policy_type.do_copy = [&](const auto, const auto) {
    ++copy_hooks;
    if (fail_copy_hook) {
      throw std::runtime_error{"injected do_copy failure"};
    }
  };
  objects.register_type(std::move(policy_type));

  const auto left = heap.allocate(node_descriptor());
  const auto left_child = heap.allocate(node_descriptor());
  const auto right = heap.allocate(node_descriptor());
  const auto right_child = heap.allocate(node_descriptor());
  const auto shared_reference = heap.allocate(node_descriptor());
  for (const auto &[handle, name] :
       {std::pair{left, "left"}, std::pair{left_child, "left_child"},
        std::pair{right, "right"}, std::pair{right_child, "right_child"},
        std::pair{shared_reference, "shared"}}) {
    objects.initialize(handle, name);
  }
  initialize_node(heap, left, 1);
  initialize_node(heap, left_child, 2);
  initialize_node(heap, right, 1);
  initialize_node(heap, right_child, 2);
  initialize_node(heap, shared_reference, 9);
  heap.property(left, "child").handle = left_child;
  heap.property(left_child, "child").handle = left;
  heap.property(right, "child").handle = right_child;
  heap.property(right_child, "child").handle = right;
  heap.property(left, "children").handles = {left_child, left};
  heap.property(right, "children").handles = {right_child, right};
  heap.property(left, "associative")
      .handle_container->set(heap, "kid", left_child);
  heap.property(right, "associative")
      .handle_container->set(heap, "kid", right_child);
  heap.property(left, "associative")
      .handle_container->set(heap, "quote\"slash\\", left_child);
  heap.property(right, "associative")
      .handle_container->set(heap, "quote\"slash\\", right_child);
  heap.property(left, "reference").handle = shared_reference;
  heap.property(right, "reference").handle = shared_reference;

  heap.property(left, "value").packed =
      PackedLogic4::from_aval_bval(4, 9, 0);

  const auto left_owned = objects.object_handle(left);
  const auto deep_copy = objects.clone_detailed(left_owned);
  const auto deep_root = deep_copy.destination.object();
  const auto deep_child = heap.property(deep_root, "child").handle;
  require(
      objects.contains(deep_copy.destination) && deep_root != left &&
          deep_child != left_child && deep_child != 0 &&
          heap.property(deep_child, "child").handle == deep_root &&
          heap.property(deep_root, "children").handles[0] == deep_child &&
          heap.property(deep_root, "children").handles[1] == deep_root &&
          heap.property(deep_root, "associative")
                  .handle_container->at("kid") == deep_child &&
          heap.property(deep_root, "value").packed.width() == 4 &&
          heap.property(deep_root, "reference").handle == shared_reference &&
          deep_copy.copied_objects == 2 && deep_copy.created_objects == 2 &&
          copy_hooks == 2,
      "deep UVM copier must preserve cycles, aliases, arrays, keyed fields, "
      "runtime value widths, references, ownership, and per-object automation hooks");

  SystemVerilogUvmCopierPolicy shallow_copy_policy;
  shallow_copy_policy.recursion = SystemVerilogUvmRecursionPolicy::Shallow;
  const auto shallow_copy =
      objects.clone_detailed(left_owned, shallow_copy_policy);
  const auto shallow_root = shallow_copy.destination.object();
  const auto shallow_child = heap.property(shallow_root, "child").handle;
  require(
      shallow_child != left_child &&
          heap.property(shallow_child, "child").handle == left &&
          heap.property(shallow_root, "children").handles[0] == shallow_child &&
          shallow_copy.copied_objects == 2,
      "shallow UVM copier must copy one object level and retain nested handles");

  SystemVerilogUvmCopierPolicy reference_copy_policy;
  reference_copy_policy.recursion = SystemVerilogUvmRecursionPolicy::Reference;
  const auto reference_copy =
      objects.clone_detailed(left_owned, reference_copy_policy);
  const auto reference_root = reference_copy.destination.object();
  require(
      heap.property(reference_root, "child").handle == left_child &&
          heap.property(reference_root, "children").handles[1] == left &&
          reference_copy.copied_objects == 1 &&
          reference_copy.created_objects == 1,
      "reference UVM copier must retain object and array handles");

  SystemVerilogUvmCopierPolicy physical_copy_policy;
  physical_copy_policy.copy_abstract = false;
  const auto physical_copy =
      objects.clone_detailed(left_owned, physical_copy_policy);
  require(
      heap.property(physical_copy.destination.object(), "physical").packed ==
              heap.property(left, "physical").packed &&
          heap.property(physical_copy.destination.object(), "abstract").packed !=
              heap.property(left, "abstract").packed,
      "UVM copier abstraction knobs must filter field automation");
  const auto self_copy = objects.copy_detailed(left_owned, left_owned);
  require(
      self_copy.copied_objects == 0 && self_copy.created_objects == 0,
      "self-copy must be a deterministic no-op");

  const auto live_before_callback_failure = heap.live_objects();
  const auto right_value_before_failure = heap.property(right, "value").packed;
  const auto right_child_before_failure = heap.property(right, "child").handle;
  fail_copy_hook = true;
  bool callback_failed{};
  try {
    (void)objects.copy_detailed(objects.object_handle(right), left_owned);
  } catch (const SystemVerilogUvmCopyError& error) {
    callback_failed = error.diagnostic_code() == "FSIM-UVM-COPY-005";
  }
  fail_copy_hook = false;
  require(
      callback_failed && heap.live_objects() == live_before_callback_failure &&
          heap.property(right, "value").packed == right_value_before_failure &&
          heap.property(right, "child").handle == right_child_before_failure,
      "UVM copier hook failure must roll back destination state and creations");

  auto override_type = object_descriptor("work::copy_override", "copy_override");
  override_type.fields = {
      {"value", SystemVerilogUvmFieldFlag::NoCopy},
      {"child", SystemVerilogUvmFieldFlag::Deep},
      {"reference", SystemVerilogUvmFieldFlag::Shallow},
      {"children", SystemVerilogUvmFieldFlag::None}};
  objects.register_type(std::move(override_type));
  const auto override_root =
      heap.allocate(node_descriptor("work::copy_override", "work::copy_override"));
  const auto override_deep =
      heap.allocate(node_descriptor("work::copy_override", "work::copy_override"));
  const auto override_shallow =
      heap.allocate(node_descriptor("work::copy_override", "work::copy_override"));
  const auto override_leaf =
      heap.allocate(node_descriptor("work::copy_override", "work::copy_override"));
  for (const auto handle :
       {override_root, override_deep, override_shallow, override_leaf}) {
    objects.initialize(handle, "override");
    initialize_node(heap, handle, 19);
  }
  heap.property(override_root, "child").handle = override_deep;
  heap.property(override_deep, "child").handle = override_leaf;
  heap.property(override_root, "reference").handle = override_shallow;
  heap.property(override_shallow, "children").handles = {override_leaf};
  const auto override_copy = objects.clone_detailed(
      objects.object_handle(override_root), reference_copy_policy);
  const auto override_copy_root = override_copy.destination.object();
  const auto override_copy_deep =
      heap.property(override_copy_root, "child").handle;
  const auto override_copy_shallow =
      heap.property(override_copy_root, "reference").handle;
  require(
      override_copy_deep != override_deep &&
          heap.property(override_copy_deep, "child").handle != override_leaf &&
          override_copy_shallow != override_shallow &&
          heap.property(override_copy_shallow, "children").handles[0] ==
              override_leaf &&
          heap.property(override_copy_root, "value").packed !=
              heap.property(override_root, "value").packed,
      "field automation must override global copier recursion and honor no-copy");

  heap.property(left, "value").packed =
      PackedLogic4::from_aval_bval(8, 1, 0);

  const auto deep = objects.compare_detailed(left, right);
  require(
      deep.equal() && deep.compared_objects == 2 && deep.compared_fields > 16,
      "deep UVM comparer must traverse fields, arrays, aliases, and cycles");
  SystemVerilogUvmComparerPolicy shallow_policy;
  shallow_policy.recursion = SystemVerilogUvmRecursionPolicy::Shallow;
  const auto shallow = objects.compare_detailed(left, right, shallow_policy);
  require(!shallow.equal() && shallow.mismatches.front().kind ==
                                  SystemVerilogUvmMismatchKind::Handle,
          "shallow UVM comparer must compare the next object level and retain "
          "nested handles");
  SystemVerilogUvmComparerPolicy reference_policy;
  reference_policy.recursion = SystemVerilogUvmRecursionPolicy::Reference;
  const auto reference =
      objects.compare_detailed(left, right, reference_policy);
  require(
      !reference.equal() && reference.mismatch_count >= 3,
      "reference UVM comparer must compare object and array handles directly");

  heap.property(right_child, "value").packed =
      PackedLogic4::from_aval_bval(8, 3, 0);
  heap.property(right, "physical").packed =
      PackedLogic4::from_aval_bval(8, 12, 0);
  heap.property(right, "abstract").packed =
      PackedLogic4::from_aval_bval(8, 23, 0);
  SystemVerilogUvmComparerPolicy bounded_mismatches;
  bounded_mismatches.show_max = 1;
  const auto mismatch =
      objects.compare_detailed(left, right, bounded_mismatches);
  require(
      !mismatch.equal() && mismatch.mismatch_count == 3 &&
          mismatch.mismatches.size() == 1,
      "UVM comparer must count every mismatch while bounding retained detail");
  SystemVerilogUvmComparerPolicy abstract_only;
  abstract_only.compare_physical = false;
  const auto abstract_result =
      objects.compare_detailed(left, right, abstract_only);
  require(abstract_result.mismatch_count == 1,
          "UVM comparer physical knob must isolate abstract field mismatches");
  SystemVerilogUvmComparerPolicy physical_only;
  physical_only.compare_abstract = false;
  const auto physical_result =
      objects.compare_detailed(left, right, physical_only);
  require(physical_result.mismatch_count == 2,
          "UVM comparer abstract knob must retain physical and recursive "
          "mismatches");
  heap.property(right_child, "value").packed =
      PackedLogic4::from_aval_bval(8, 2, 0);
  heap.property(right, "physical").packed =
      PackedLogic4::from_aval_bval(8, 11, 0);
  heap.property(right, "abstract").packed =
      PackedLogic4::from_aval_bval(8, 22, 0);
  require(objects.compare(left, right),
          "legacy UVM compare must delegate to the deterministic deep policy");

  const auto line = objects.print_formatted(left);
  SystemVerilogUvmPrinterPolicy tree_policy;
  tree_policy.kind = SystemVerilogUvmPrinterKind::Tree;
  const auto tree = objects.print_formatted(left, tree_policy);
  SystemVerilogUvmPrinterPolicy table_policy;
  table_policy.kind = SystemVerilogUvmPrinterKind::Table;
  const auto table = objects.print_formatted(left, table_policy);
  require(line.text == objects.print_formatted(left).text &&
              line.text.find("same\\nlabel") != std::string::npos &&
              tree.text.find("  left.value") != std::string::npos &&
              table.text.starts_with("Name") &&
              std::ranges::any_of(
                  line.entries,
                  [](const auto &entry) {
                    return entry.path == "left.child.child" &&
                           entry.kind == SystemVerilogUvmObjectEntryKind::Cycle;
                  }) &&
              std::ranges::any_of(
                  line.entries,
                  [](const auto &entry) {
                    return entry.path == "left.children[0]" &&
                           entry.kind ==
                               SystemVerilogUvmObjectEntryKind::Reference;
                  }) &&
              std::ranges::any_of(line.entries,
                                  [](const auto &entry) {
                                    return entry.path ==
                                           "left.associative[\"kid\"]";
                                  }) &&
              std::ranges::any_of(
                  line.entries,
                  [](const auto &entry) {
                    return entry.path ==
                           "left.associative[\"quote\\\"slash\\\\\"]";
                  }),
          "line/tree/table UVM printers must be stable and traverse object "
          "arrays and keyed containers");

  objects.register_type(object_descriptor("work::alpha", "alpha"));
  objects.register_type(object_descriptor("work::beta", "beta"));
  const auto alpha =
      heap.allocate(node_descriptor("work::alpha", "work::alpha"));
  const auto beta = heap.allocate(node_descriptor("work::beta", "work::beta"));
  objects.initialize(alpha, "alpha");
  objects.initialize(beta, "beta");
  initialize_node(heap, alpha, 7);
  initialize_node(heap, beta, 7);
  require(!objects.compare_detailed(alpha, beta).equal(),
          "UVM comparer must check dynamic type by default");
  SystemVerilogUvmComparerPolicy unchecked_type;
  unchecked_type.check_type = false;
  require(objects.compare_detailed(alpha, beta, unchecked_type).equal(),
          "UVM comparer check_type knob must permit shape-compatible types");
  bool incompatible_copy{};
  try {
    (void)objects.copy_detailed(
        objects.object_handle(alpha), objects.object_handle(beta));
  } catch (const SystemVerilogUvmCopyError& error) {
    incompatible_copy = error.diagnostic_code() == "FSIM-UVM-COPY-003";
  }

  const auto stale_object = heap.allocate(node_descriptor());
  objects.initialize(stale_object, "stale");
  const auto stale_handle = objects.object_handle(stale_object);
  objects.erase(stale_object);
  require(heap.release(stale_object), "stale UVM copier fixture release");
  bool stale_copy{};
  try {
    (void)objects.clone_detailed(stale_handle);
  } catch (const SystemVerilogUvmCopyError& error) {
    stale_copy = error.diagnostic_code() == "FSIM-UVM-COPY-001";
  }

  SystemVerilogClassHeap foreign_heap{{8, 4096}};
  SystemVerilogUvmObjectService foreign_objects{
      foreign_heap,
      [&](const std::string_view, const std::string_view,
          const std::string_view) {
        return foreign_heap.allocate(node_descriptor());
      },
      {8, 8, 64, 4096}};
  foreign_objects.register_type(object_descriptor());
  const auto foreign_object = foreign_heap.allocate(node_descriptor());
  foreign_objects.initialize(foreign_object, "foreign");
  bool foreign_copy{};
  try {
    (void)objects.clone_detailed(
        foreign_objects.object_handle(foreign_object));
  } catch (const SystemVerilogUvmCopyError& error) {
    foreign_copy = error.diagnostic_code() == "FSIM-UVM-COPY-002";
  }
  require(
      incompatible_copy && stale_copy && foreign_copy,
      "UVM copier must reject incompatible, stale, and cross-owner operands");

  std::size_t policy_errors{};
  const auto expect_policy_error = [&](const auto &operation) {
    try {
      operation();
    } catch (const SystemVerilogUvmObjectPolicyError &error) {
      require(error.diagnostic_code() == "FSIM-UVM-POLICY-001",
              "malformed UVM policy must retain its cataloged diagnostic");
      ++policy_errors;
    }
  };
  expect_policy_error([&] {
    auto invalid = SystemVerilogUvmPrinterPolicy{};
    invalid.kind = static_cast<SystemVerilogUvmPrinterKind>(255);
    (void)objects.print_formatted(left, invalid);
  });
  expect_policy_error([&] {
    auto invalid = SystemVerilogUvmPrinterPolicy{};
    invalid.separator = "\n";
    (void)objects.print_formatted(left, invalid);
  });
  expect_policy_error([&] {
    auto invalid = SystemVerilogUvmComparerPolicy{};
    invalid.recursion = static_cast<SystemVerilogUvmRecursionPolicy>(255);
    (void)objects.compare_detailed(left, right, invalid);
  });
  expect_policy_error([&] {
    auto invalid = SystemVerilogUvmCopierPolicy{};
    invalid.recursion = static_cast<SystemVerilogUvmRecursionPolicy>(255);
    (void)objects.copy_detailed(left_owned, objects.object_handle(right), invalid);
  });
  expect_policy_error([&] {
    auto invalid = object_descriptor("work::bad_policy", "bad_policy");
    invalid.fields.front().flags =
        SystemVerilogUvmFieldFlag::Reference | SystemVerilogUvmFieldFlag::Deep;
    objects.register_type(std::move(invalid));
  });
  bool output_limited{};
  try {
    auto oversized = table_policy;
    oversized.name_width = 4096;
    oversized.type_width = 4096;
    oversized.size_width = 4096;
    oversized.value_width = 4096;
    (void)objects.print_formatted(left, oversized);
  } catch (const std::length_error &) {
    output_limited = true;
  }
  require(policy_errors == 5 && output_limited,
          "UVM policy diagnostics and formatted-output ceilings must reject "
          "deterministically");
}

} // namespace fsim::tests::runtime
