// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/class_methods.hpp"
#include "fsim/runtime/class_static.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace fsim::tests::runtime {

namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error(std::string{message});
}

fsim::runtime::SystemVerilogClassDescriptor descriptor() {
  using namespace fsim::runtime;
  SystemVerilogClassDescriptor result;
  result.declared_type = "work::Base";
  result.dynamic_type = "work::Derived";
  result.specialization_identity = "work::Derived<WIDTH=8>";
  result.assignable_declared_types = {"work::Derived", "work::Base"};
  result.properties = {
      {"two_state", SystemVerilogClassPropertyKind::Bit2, 9},
      {"four_state", SystemVerilogClassPropertyKind::Logic4, 9},
      {"count", SystemVerilogClassPropertyKind::Integer, 64},
      {"label", SystemVerilogClassPropertyKind::String, 0},
      {"next", SystemVerilogClassPropertyKind::ClassHandle, 64}};
  return result;
}

}  // namespace

void test_systemverilog_class_heap() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap{{2, 44}};
  require(!heap.contains(0), "the null class handle must never be live");

  std::vector<SystemVerilogClassHeap::ConstructorStep> constructors;
  constructors.push_back([](auto& runtime, const auto handle) {
    runtime.property(handle, "count").packed =
        PackedLogic4::from_aval_bval(64, 2, 0);
  });
  constructors.push_back([](auto& runtime, const auto handle) {
    runtime.property(handle, "count").packed =
        PackedLogic4::from_aval_bval(64, 3, 0);
  });
  const auto first = heap.construct(descriptor(), constructors);
  require(
      first != 0 && heap.contains(first)
          && heap.live_objects() == 1
          && heap.storage_bytes() == 22,
      "allocation must consume the exact storage-derived budget");
  const auto& object = heap.object(first);
  require(
      object.declared_type == "work::Base"
          && object.dynamic_type == "work::Derived"
          && object.specialization_identity == "work::Derived<WIDTH=8>"
          && object.properties.size() == 5,
      "class objects must retain declared, dynamic, and specialization types");
  require(
      object.properties[0].packed.to_msb_string() == "000000000"
          && object.properties[1].packed.to_msb_string() == "XXXXXXXXX"
          && object.properties[2].packed.low_word().aval == 3
          && object.properties[3].string.empty()
          && object.properties[4].handle == 0,
      "class properties must use deterministic language defaults");
  const auto alias = first;
  heap.property(alias, "next").handle = first;
  require(
      SystemVerilogClassHeap::equal(first, alias)
          && heap.property(first, "next").handle == alias
          && heap.checked_cast(first, "work::Base") == first
          && heap.checked_cast(0, "work::Base") == 0,
      "assignment, argument/return transfer, equality, property access, and upcasts must preserve aliases");
  bool downcast_failed = false;
  try {
    (void)heap.checked_cast(first, "work::Unrelated");
  } catch (const std::invalid_argument&) {
    downcast_failed = true;
  }
  bool property_failed = false;
  try {
    (void)heap.property(first, "missing");
  } catch (const std::out_of_range&) {
    property_failed = true;
  }
  require(
      downcast_failed && property_failed,
      "invalid downcasts and property selections must fail explicitly");

  const auto second = heap.allocate(descriptor());
  require(
      heap.live_objects() == 2 && heap.storage_bytes() == 44,
      "the heap must account every live object");
  bool live_budget_failed = false;
  try {
    (void)heap.allocate(descriptor());
  } catch (const std::length_error&) {
    live_budget_failed = true;
  }
  require(
      live_budget_failed && heap.live_objects() == 2,
      "a live-object budget failure must be transactional");

  require(heap.release(first), "a live class handle must release once");
  require(
      !heap.contains(first) && !heap.release(first),
      "released and repeated handles must be stale");
  const auto reused = heap.allocate(descriptor());
  require(
      reused != first && heap.contains(reused) && !heap.contains(first),
      "slot reuse must advance the encoded generation");

  heap.clear();
  require(
      heap.live_objects() == 0 && heap.storage_bytes() == 0
          && !heap.contains(second) && !heap.contains(reused),
      "heap cleanup must invalidate every live handle deterministically");
  const auto live_before_failed_constructor = heap.live_objects();
  std::vector<SystemVerilogClassHeap::ConstructorStep> failing_constructor;
  failing_constructor.push_back([](auto&, const auto) {
    throw std::runtime_error{"constructor failure"};
  });
  bool constructor_failed = false;
  try {
    (void)heap.construct(descriptor(), failing_constructor);
  } catch (const std::runtime_error&) {
    constructor_failed = true;
  }
  require(
      constructor_failed
          && heap.live_objects() == live_before_failed_constructor,
      "constructor failure must release the unpublished object transactionally");

  SystemVerilogClassHeap storage_limited{{10, 21}};
  bool storage_budget_failed = false;
  try {
    (void)storage_limited.allocate(descriptor());
  } catch (const std::length_error&) {
    storage_budget_failed = true;
  }
  require(
      storage_budget_failed
          && storage_limited.live_objects() == 0
          && storage_limited.storage_bytes() == 0,
      "a storage-budget failure must not publish a partial object");

  auto duplicate = descriptor();
  duplicate.properties.push_back(duplicate.properties.front());
  bool duplicate_failed = false;
  try {
    (void)heap.allocate(duplicate);
  } catch (const std::invalid_argument&) {
    duplicate_failed = true;
  }
  require(duplicate_failed, "duplicate property descriptors must be rejected");

  SystemVerilogClassHeap container_heap;
  const auto element = container_heap.allocate(descriptor());
  auto container_owner = descriptor();
  container_owner.properties.clear();
  const auto add_container = [&](
      std::string name,
      const SystemVerilogClassContainerKind kind,
      const std::size_t maximum,
      const std::size_t initial,
      std::vector<std::string> members = {}) {
    SystemVerilogClassPropertyDescriptor property;
    property.name = std::move(name);
    property.kind = SystemVerilogClassPropertyKind::Container;
    property.width = 0;
    property.handle_container = SystemVerilogClassHandleContainerDescriptor{
        kind, "work::Base", maximum, initial, std::move(members), false};
    container_owner.properties.push_back(std::move(property));
  };
  add_container(
      "fixed", SystemVerilogClassContainerKind::FixedArray, 2, 2);
  add_container(
      "dynamic", SystemVerilogClassContainerKind::DynamicArray, 3, 1);
  add_container("queue", SystemVerilogClassContainerKind::Queue, 3, 0);
  add_container(
      "associative",
      SystemVerilogClassContainerKind::AssociativeArray,
      2,
      0);
  add_container(
      "aggregate",
      SystemVerilogClassContainerKind::UnpackedAggregate,
      2,
      0,
      {"left", "right"});
  const auto owner = container_heap.allocate(container_owner);
  auto& fixed = *container_heap.property(owner, "fixed").handle_container;
  auto& dynamic = *container_heap.property(owner, "dynamic").handle_container;
  auto& queue = *container_heap.property(owner, "queue").handle_container;
  auto& associative =
      *container_heap.property(owner, "associative").handle_container;
  auto& aggregate =
      *container_heap.property(owner, "aggregate").handle_container;
  fixed.set(container_heap, 1, element);
  dynamic.resize(3);
  dynamic.set(container_heap, 2, element);
  queue.push_back(container_heap, element);
  queue.push_back(container_heap, element);
  queue.push_back(container_heap, element);
  associative.set(container_heap, "primary", element);
  aggregate.set(container_heap, "left", element);
  require(
      fixed.at(1) == element && dynamic.at(2) == element
          && queue.pop_front() == element
          && associative.at("primary") == element
          && associative.at("missing") == 0
          && aggregate.at("left") == element
          && container_heap.storage_bytes() == 118,
      "fixed, dynamic, queue, keyed, aggregate, and property containers must preserve aliases and exact budgets");
  queue.push_back(container_heap, element);
  bool queue_budget_failed = false;
  try {
    queue.push_back(container_heap, element);
  } catch (const std::length_error&) {
    queue_budget_failed = true;
  }
  bool packed_handle_failed = false;
  try {
    (void)SystemVerilogClassHandleContainer{
        {SystemVerilogClassContainerKind::UnpackedAggregate,
         "work::Base", 1, 0, {"member"}, true}};
  } catch (const std::invalid_argument&) {
    packed_handle_failed = true;
  }
  auto unrelated_descriptor = descriptor();
  unrelated_descriptor.declared_type = "work::Unrelated";
  unrelated_descriptor.dynamic_type = "work::Unrelated";
  unrelated_descriptor.specialization_identity = "work::Unrelated";
  unrelated_descriptor.assignable_declared_types = {"work::Unrelated"};
  const auto unrelated = container_heap.allocate(unrelated_descriptor);
  bool element_type_failed = false;
  try {
    fixed.set(container_heap, 0, unrelated);
  } catch (const std::invalid_argument&) {
    element_type_failed = true;
  }
  require(
      queue_budget_failed && packed_handle_failed && element_type_failed,
      "container budgets, packed legality, and handle element types must be checked");
}

void test_systemverilog_class_methods() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap;
  const auto instance = heap.allocate(descriptor());
  heap.property(instance, "count").packed =
      PackedLogic4::from_aval_bval(64, 4, 0);
  SystemVerilogClassMethodRuntime methods{
      heap, {3, 4, 2}};

  SystemVerilogClassMethodDescriptor bump;
  bump.canonical_identity = "work::Base::bump";
  bump.owner_type = "work::Base";
  bump.arguments = {SystemVerilogClassArgumentMode::Inout};
  bump.automatic_value_count = 1;
  bump.entry = [](SystemVerilogClassMethodFrame& frame) {
    const auto current = frame.property("count").packed.low_word().aval;
    const auto amount = frame.argument(0).packed.low_word().aval;
    frame.local(0).packed =
        PackedLogic4::from_aval_bval(64, current + amount, 0);
    frame.property("count").packed = frame.local(0).packed;
    frame.argument(0).packed = frame.local(0).packed;
    return SystemVerilogClassMethodStatus::Completed;
  };
  methods.register_method(std::move(bump));

  std::vector<SystemVerilogClassMethodValue> amount(1);
  amount.front().packed = PackedLogic4::from_aval_bval(64, 3, 0);
  const auto bumped = methods.invoke("work::Base::bump", instance, amount);
  require(
      bumped.status == SystemVerilogClassMethodStatus::Completed
          && amount.front().packed.low_word().aval == 7
          && heap.property(instance, "count").packed.low_word().aval == 7,
      "this, implicit properties, automatic locals, base views, and copy-out must execute");

  SystemVerilogClassMethodDescriptor task;
  task.canonical_identity = "work::Derived::delayed";
  task.owner_type = "work::Derived";
  task.arguments = {SystemVerilogClassArgumentMode::Output};
  task.automatic_value_count = 1;
  task.is_task = true;
  task.entry = [](SystemVerilogClassMethodFrame& frame) {
    if (frame.continuation_point() == 0) {
      frame.local(0).packed = PackedLogic4::from_aval_bval(64, 11, 0);
      frame.suspend_at(1);
      return SystemVerilogClassMethodStatus::Suspended;
    }
    frame.argument(0) = frame.local(0);
    return SystemVerilogClassMethodStatus::Completed;
  };
  methods.register_method(std::move(task));
  std::vector<SystemVerilogClassMethodValue> delayed(1);
  const auto suspended = methods.invoke(
      "work::Derived::delayed", instance, delayed);
  require(
      suspended.status == SystemVerilogClassMethodStatus::Suspended
          && suspended.continuation != 0
          && methods.suspended_invocations() == 1,
      "class tasks must publish a checked continuation");
  const auto resumed = methods.resume(suspended.continuation, delayed);
  require(
      resumed.status == SystemVerilogClassMethodStatus::Completed
          && delayed.front().packed.low_word().aval == 11
          && methods.suspended_invocations() == 0,
      "suspended class tasks must preserve automatic locals and copy-out");
  bool stale_continuation_failed = false;
  try {
    (void)methods.resume(suspended.continuation, delayed);
  } catch (const std::out_of_range&) {
    stale_continuation_failed = true;
  }

  SystemVerilogClassMethodDescriptor base_virtual;
  base_virtual.canonical_identity = "work::Base::value";
  base_virtual.owner_type = "work::Base";
  base_virtual.arguments = {SystemVerilogClassArgumentMode::Output};
  base_virtual.virtual_slot = 0;
  base_virtual.entry = [](SystemVerilogClassMethodFrame& frame) {
    frame.argument(0).packed = PackedLogic4::from_aval_bval(32, 1, 0);
    return SystemVerilogClassMethodStatus::Completed;
  };
  methods.register_method(std::move(base_virtual));
  SystemVerilogClassMethodDescriptor derived_virtual;
  derived_virtual.canonical_identity = "work::Derived::value";
  derived_virtual.owner_type = "work::Derived";
  derived_virtual.arguments = {SystemVerilogClassArgumentMode::Output};
  derived_virtual.virtual_slot = 0;
  derived_virtual.entry = [](SystemVerilogClassMethodFrame& frame) {
    frame.argument(0).packed = PackedLogic4::from_aval_bval(32, 2, 0);
    return SystemVerilogClassMethodStatus::Completed;
  };
  methods.register_method(std::move(derived_virtual));
  std::vector<SystemVerilogClassMethodValue> virtual_result(1);
  (void)methods.invoke_virtual(0, instance, virtual_result);
  require(
      virtual_result.front().packed.low_word().aval == 2,
      "virtual dispatch must select the dynamic type's stable slot");
  (void)methods.invoke("work::Base::value", instance, virtual_result);
  require(
      virtual_result.front().packed.low_word().aval == 1,
      "an explicit base-qualified call must bypass virtual dispatch");

  SystemVerilogClassMethodDescriptor pure;
  pure.canonical_identity = "work::Derived::required";
  pure.owner_type = "work::Derived";
  pure.virtual_slot = 1;
  pure.is_pure = true;
  methods.register_method(std::move(pure));
  bool pure_failed = false;
  try {
    std::vector<SystemVerilogClassMethodValue> no_actuals;
    (void)methods.invoke_virtual(1, instance, no_actuals);
  } catch (const std::logic_error&) {
    pure_failed = true;
  }

  SystemVerilogClassMethodDescriptor recursive;
  recursive.canonical_identity = "work::Derived::recursive";
  recursive.owner_type = "work::Derived";
  recursive.entry = [](SystemVerilogClassMethodFrame& frame) {
    std::vector<SystemVerilogClassMethodValue> no_actuals;
    (void)frame.call_nonvirtual("work::Derived::recursive", no_actuals);
    return SystemVerilogClassMethodStatus::Completed;
  };
  methods.register_method(std::move(recursive));
  bool recursion_failed = false;
  try {
    std::vector<SystemVerilogClassMethodValue> no_actuals;
    (void)methods.invoke(
        "work::Derived::recursive", instance, no_actuals);
  } catch (const std::length_error&) {
    recursion_failed = true;
  }
  bool null_failed = false;
  try {
    std::vector<SystemVerilogClassMethodValue> no_actuals;
    (void)methods.invoke("work::Derived::recursive", 0, no_actuals);
  } catch (const std::out_of_range&) {
    null_failed = true;
  }
  require(
      stale_continuation_failed && recursion_failed && null_failed
          && pure_failed,
      "stale continuations, recursion overflow, and null this must fail explicitly");

  std::vector<std::string> initialization_order;
  SystemVerilogClassStaticStore static_store{{3, 16}};
  SystemVerilogClassStaticDescriptor base_static;
  base_static.specialization_identity = "work::Counter<WIDTH=8>";
  base_static.aliases = {
      "work::Counter", "root_a::Counter", "imported_pkg::Counter"};
  base_static.properties = {
      {"count", SystemVerilogClassPropertyKind::Integer, 64}};
  base_static.initializers.push_back(
      [&](auto& store, const std::string_view identity) {
        initialization_order.emplace_back("base");
        store.property(identity, "count").packed =
            PackedLogic4::from_aval_bval(64, 2, 0);
      });
  static_store.register_specialization(std::move(base_static));
  SystemVerilogClassStaticDescriptor derived_static;
  derived_static.specialization_identity = "work::DerivedCounter<WIDTH=8>";
  derived_static.base_specialization_identity = "work::Counter<WIDTH=8>";
  derived_static.aliases = {"work::DerivedCounter"};
  derived_static.properties = {
      {"derived_count", SystemVerilogClassPropertyKind::Integer, 64}};
  derived_static.initializers.push_back(
      [&](auto& store, const std::string_view identity) {
        initialization_order.emplace_back("derived");
        store.property(identity, "derived_count").packed =
            PackedLogic4::from_aval_bval(64, 5, 0);
      });
  static_store.register_specialization(std::move(derived_static));
  static_store.initialize("work::DerivedCounter");
  require(
      initialization_order == std::vector<std::string>{"base", "derived"}
          && static_store.property("work::DerivedCounter", "count")
                 .packed.low_word().aval == 2
          && static_store.property_count() == 2
          && static_store.storage_bytes() == 16,
      "class static initialization must run base-first with exact budgets");

  const auto make_increment = [] {
    SystemVerilogClassMethodDescriptor increment;
    increment.canonical_identity = "work::Counter::increment";
    increment.owner_type = "work::Counter";
    increment.is_static = true;
    increment.entry = [](SystemVerilogClassMethodFrame& frame) {
      auto& count = frame.static_property("count").packed;
      count = PackedLogic4::from_aval_bval(
          64, count.low_word().aval + 1, 0);
      return SystemVerilogClassMethodStatus::Completed;
    };
    return increment;
  };
  SystemVerilogClassMethodRuntime root_a_methods{
      heap, {}, &static_store};
  SystemVerilogClassMethodRuntime root_b_methods{
      heap, {}, &static_store};
  root_a_methods.register_method(make_increment());
  root_b_methods.register_method(make_increment());
  std::vector<SystemVerilogClassMethodValue> no_actuals;
  (void)root_a_methods.invoke(
      "work::Counter::increment", 0, no_actuals);
  (void)root_b_methods.invoke(
      "work::Counter::increment", 0, no_actuals);
  require(
      static_store.property("root_a::Counter", "count")
                  .packed.low_word().aval == 4
          && static_store.property("imported_pkg::Counter", "count")
                 .packed.low_word().aval == 4,
      "static methods, roots, and import aliases must share one specialization state");

  SystemVerilogClassStaticStore invalid_static_store;
  SystemVerilogClassStaticDescriptor duplicate_alias;
  duplicate_alias.specialization_identity = "work::Duplicate";
  duplicate_alias.aliases = {"alias", "alias"};
  bool duplicate_alias_failed = false;
  try {
    invalid_static_store.register_specialization(
        std::move(duplicate_alias));
  } catch (const std::invalid_argument&) {
    duplicate_alias_failed = true;
  }
  require(
      duplicate_alias_failed
          && invalid_static_store.property_count() == 0
          && invalid_static_store.storage_bytes() == 0,
      "duplicate static aliases must fail registration transactionally");
  SystemVerilogClassStaticDescriptor cycle_left;
  cycle_left.specialization_identity = "work::CycleLeft";
  cycle_left.base_specialization_identity = "work::CycleRight";
  invalid_static_store.register_specialization(std::move(cycle_left));
  SystemVerilogClassStaticDescriptor cycle_right;
  cycle_right.specialization_identity = "work::CycleRight";
  cycle_right.base_specialization_identity = "work::CycleLeft";
  invalid_static_store.register_specialization(std::move(cycle_right));
  bool static_cycle_failed = false;
  try {
    invalid_static_store.initialize("work::CycleLeft");
  } catch (const std::logic_error&) {
    static_cycle_failed = true;
  }
  require(
      static_cycle_failed,
      "cyclic static initialization must reject without publishing values");
}

}  // namespace fsim::tests::runtime
