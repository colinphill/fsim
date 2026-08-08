// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/class_heap.hpp"
#include "fsim/runtime/class_methods.hpp"
#include "fsim/runtime/class_static.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

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
  result.properties[0].random_kind = SystemVerilogClassRandomKind::Rand;
  result.properties[0].nominal_type = "bit[8:0]";
  result.properties[1].random_kind = SystemVerilogClassRandomKind::Randc;
  result.properties[1].signed_value = true;
  result.properties[1].nominal_type = "work::mode_t";
  result.properties[4].random_kind = SystemVerilogClassRandomKind::Rand;
  result.properties[4].nominal_type = "work::Base";
  return result;
}

}  // namespace

void test_systemverilog_class_heap() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap{{2, 266}};
  require(!heap.contains(0), "the null class handle must never be live");

  const auto stream_snapshot = [](const std::uint64_t seed,
                                  std::string root) {
    SystemVerilogClassHeap random_heap{{}, seed};
    auto first_descriptor = descriptor();
    first_descriptor.random_root_identity = std::move(root);
    const auto first_handle = random_heap.allocate(first_descriptor);
    const auto second_handle = random_heap.allocate(first_descriptor);
    auto first_call = random_heap.random_stream(
        first_handle, "work::Base::randomize@call-a");
    auto repeated_call = random_heap.random_stream(
        first_handle, "work::Base::randomize@call-a");
    auto other_call = random_heap.random_stream(
        first_handle, "work::Base::randomize@call-b");
    return std::tuple{
        random_heap.object(first_handle).random_root_seed,
        random_heap.object(first_handle).random_object_seed,
        random_heap.object(second_handle).random_object_seed,
        random_heap.random_state(first_handle, "two_state").stream_seed,
        first_call.call_seed,
        first_call.next_u64(),
        repeated_call.call_seed,
        other_call.call_seed};
  };
  const auto stream_first = stream_snapshot(42, "root_a");
  const auto stream_replay = stream_snapshot(42, "root_a");
  const auto stream_changed_seed = stream_snapshot(43, "root_a");
  const auto stream_changed_root = stream_snapshot(42, "root_b");
  require(
      stream_first == stream_replay
          && stream_first != stream_changed_seed
          && stream_first != stream_changed_root
          && std::get<1>(stream_first) != std::get<2>(stream_first)
          && std::get<4>(stream_first) != std::get<6>(stream_first)
          && std::get<4>(stream_first) != std::get<7>(stream_first),
      "simulation, root, object, property, call-site, and call-ordinal streams must be deterministic and independent");

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
          && heap.storage_bytes() == 133,
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
  require(
      heap.random_state(first, "two_state").kind
              == SystemVerilogClassRandomKind::Rand
          && heap.random_state(first, "four_state").kind
              == SystemVerilogClassRandomKind::Randc
          && heap.random_state(first, "four_state").signed_value
          && heap.random_state(first, "four_state").width == 9
          && heap.random_state(first, "four_state").nominal_type
              == "work::mode_t"
          && heap.random_state(first, "next").nominal_type == "work::Base",
      "per-object random state must retain exact kinds, widths, signedness, enums, and handle profiles");
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
  heap.random_state(first, "two_state").enabled = false;
  ++heap.random_state(first, "two_state").revision;
  require(
      heap.live_objects() == 2 && heap.storage_bytes() == 266
          && !heap.random_state(first, "two_state").enabled
          && heap.random_state(first, "two_state").revision == 1
          && heap.random_state(second, "two_state").enabled
          && heap.random_state(second, "two_state").revision == 0,
      "the heap must account every live object and isolate its random state");
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

  SystemVerilogClassHeap storage_limited{{10, 132}};
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

  const auto random_check = heap.allocate(descriptor());
  bool nonrandom_failed = false;
  try {
    (void)heap.random_state(random_check, "count");
  } catch (const std::invalid_argument&) {
    nonrandom_failed = true;
  }
  auto invalid_randc = descriptor();
  invalid_randc.properties[4].random_kind =
      SystemVerilogClassRandomKind::Randc;
  bool randc_profile_failed = false;
  try {
    (void)heap.allocate(invalid_randc);
  } catch (const std::invalid_argument&) {
    randc_profile_failed = true;
  }
  require(
      nonrandom_failed && randc_profile_failed,
      "nonrandom state access and nonintegral randc profiles must fail explicitly");

  auto mode_descriptor = descriptor();
  mode_descriptor.constraint_modes = {
      {"work::Base::base_rule", true},
      {"work::Derived::derived_rule", true}};
  SystemVerilogClassHeap mode_heap;
  const auto mode_first = mode_heap.allocate(mode_descriptor);
  const auto mode_second = mode_heap.allocate(mode_descriptor);
  mode_heap.set_random_mode(mode_first, "two_state", false);
  mode_heap.set_constraint_mode(mode_first, "derived_rule", false);
  require(
      !mode_heap.random_mode(mode_first, "two_state")
          && mode_heap.random_mode(mode_second, "two_state")
          && !mode_heap.constraint_mode(mode_first, "derived_rule")
          && mode_heap.constraint_mode(mode_second, "derived_rule")
          && mode_heap.constraint_mode(mode_first, "base_rule"),
      "property and constraint modes must be independently owned by each object");
  bool invalid_random_mode_failed = false;
  try {
    (void)mode_heap.random_mode(mode_first, "count");
  } catch (const std::invalid_argument&) {
    invalid_random_mode_failed = true;
  }
  bool missing_constraint_mode_failed = false;
  try {
    (void)mode_heap.constraint_mode(mode_first, "missing_rule");
  } catch (const std::out_of_range&) {
    missing_constraint_mode_failed = true;
  }
  require(
      invalid_random_mode_failed && missing_constraint_mode_failed,
      "mode access must reject nonrandom properties and missing constraint blocks");

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
          && container_heap.storage_bytes() == 229,
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
  auto method_descriptor = descriptor();
  method_descriptor.properties.push_back(
      {"wide", SystemVerilogClassPropertyKind::Logic4, 137});
  const auto instance = heap.allocate(method_descriptor);
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

  auto wide_value = PackedLogic4{137, Logic4::zero};
  wide_value.set(136, Logic4::one);
  wide_value.set(73, Logic4::one);
  wide_value.set(3, Logic4::one);
  auto wide_expected = wide_value;
  wide_expected.set(96, Logic4::one);
  SystemVerilogClassMethodDescriptor wide_method;
  wide_method.canonical_identity = "work::Base::wide";
  wide_method.owner_type = "work::Base";
  wide_method.arguments = {SystemVerilogClassArgumentMode::Inout};
  wide_method.automatic_value_count = 1;
  wide_method.entry = [](SystemVerilogClassMethodFrame& frame) {
    frame.local(0) = frame.argument(0);
    frame.local(0).packed.set(96, Logic4::one);
    frame.property("wide") = frame.local(0);
    frame.argument(0) = frame.local(0);
    return SystemVerilogClassMethodStatus::Completed;
  };
  methods.register_method(std::move(wide_method));
  std::vector<SystemVerilogClassMethodValue> wide_actuals(1);
  wide_actuals.front().packed = wide_value;
  const auto wide_result = methods.invoke(
      "work::Base::wide", instance, wide_actuals);
  require(
      wide_result.status == SystemVerilogClassMethodStatus::Completed
          && wide_actuals.front().packed == wide_expected
          && heap.property(instance, "wide").packed == wide_expected,
      "class method arguments, automatic locals, copy-out, and object properties must preserve arbitrary-width packed values");

  SystemVerilogClassMethodDescriptor task;
  task.canonical_identity = "work::Derived::delayed";
  task.owner_type = "work::Derived";
  task.arguments = {SystemVerilogClassArgumentMode::Output};
  task.automatic_value_count = 1;
  task.is_task = true;
  task.entry = [](SystemVerilogClassMethodFrame& frame) {
    if (frame.continuation_point() == 0) {
      frame.local(0).packed = PackedLogic4{137, Logic4::zero};
      frame.local(0).packed.set(136, Logic4::one);
      frame.local(0).packed.set(11, Logic4::one);
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
          && delayed.front().packed.width() == 137
          && delayed.front().packed.get(136) == Logic4::one
          && delayed.front().packed.get(11) == Logic4::one
          && methods.suspended_invocations() == 0,
      "suspended class tasks must preserve arbitrary-width automatic locals and copy-out");
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

  SystemVerilogClassMethodDescriptor bounded_recursive;
  bounded_recursive.canonical_identity = "work::Derived::bounded_recursive";
  bounded_recursive.owner_type = "work::Derived";
  bounded_recursive.arguments = {
      SystemVerilogClassArgumentMode::Inout,
      SystemVerilogClassArgumentMode::Input};
  bounded_recursive.entry = [](SystemVerilogClassMethodFrame& frame) {
    const auto remaining = frame.argument(1).packed.low_word().aval;
    if (remaining == 0) {
      frame.argument(0).packed.set(128, Logic4::one);
      return SystemVerilogClassMethodStatus::Completed;
    }
    std::vector<SystemVerilogClassMethodValue> nested{
        frame.argument(0), frame.argument(1)};
    nested[1].packed = PackedLogic4::from_aval_bval(
        32, remaining - 1, 0);
    (void)frame.call_nonvirtual(
        "work::Derived::bounded_recursive", nested);
    frame.argument(0) = nested[0];
    return SystemVerilogClassMethodStatus::Completed;
  };
  methods.register_method(std::move(bounded_recursive));
  std::vector<SystemVerilogClassMethodValue> recursive_actuals(2);
  recursive_actuals[0].packed = wide_value;
  recursive_actuals[1].packed = PackedLogic4::from_aval_bval(32, 2, 0);
  (void)methods.invoke(
      "work::Derived::bounded_recursive", instance, recursive_actuals);
  require(
      recursive_actuals[0].packed.width() == 137
          && recursive_actuals[0].packed.get(136) == Logic4::one
          && recursive_actuals[0].packed.get(128) == Logic4::one
          && recursive_actuals[0].packed.get(73) == Logic4::one,
      "bounded recursive method frames must preserve arbitrary-width values");

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

void test_systemverilog_uvm_object() {
  using namespace fsim::runtime;
  const auto make_descriptor = [] {
    SystemVerilogClassDescriptor result;
    result.declared_type = "uvm_pkg::uvm_object";
    result.dynamic_type = "work::uvm_item";
    result.specialization_identity = "work::uvm_item";
    result.assignable_declared_types = {
        "work::uvm_item", "uvm_pkg::uvm_object"};
    result.properties = {
        {"value", SystemVerilogClassPropertyKind::Logic4, 16},
        {"label", SystemVerilogClassPropertyKind::String, 0},
        {"child", SystemVerilogClassPropertyKind::ClassHandle, 64},
        {"alias", SystemVerilogClassPropertyKind::ClassHandle, 64},
        {"ignored", SystemVerilogClassPropertyKind::Logic4, 8}};
    return result;
  };

  SystemVerilogClassHeap heap{{64, 4096}};
  std::size_t copy_hooks{};
  std::size_t compare_hooks{};
  std::size_t print_hooks{};
  std::size_t record_hooks{};
  std::size_t print_sink_calls{};
  std::size_t record_sink_calls{};
  SystemVerilogUvmObjectService objects{
      heap,
      [&](const std::string_view specialization,
          const std::string_view declared,
          const std::string_view) {
        require(
            specialization == "work::uvm_item"
                && declared == "uvm_pkg::uvm_object",
            "UVM virtual create must retain specialization and declared view");
        return heap.allocate(make_descriptor());
      },
      {16, 64, 256, 4096}};
  SystemVerilogUvmObjectDescriptor type;
  type.specialization_identity = "work::uvm_item";
  type.type_name = "uvm_item";
  type.fields = {
      {"value", SystemVerilogUvmFieldFlag::None},
      {"label", SystemVerilogUvmFieldFlag::None},
      {"child", SystemVerilogUvmFieldFlag::None},
      {"alias", SystemVerilogUvmFieldFlag::Reference},
      {"ignored", SystemVerilogUvmFieldFlag::NoCopy
          | SystemVerilogUvmFieldFlag::NoCompare
          | SystemVerilogUvmFieldFlag::NoPrint
          | SystemVerilogUvmFieldFlag::NoRecord}};
  type.do_copy = [&](const auto, const auto) { ++copy_hooks; };
  type.do_compare = [&](const auto, const auto) {
    ++compare_hooks;
    return true;
  };
  type.do_print = [&](const auto handle, auto& entries) {
    ++print_hooks;
    entries.push_back({
        std::string{objects.name(handle)} + ".$do_print",
        "hook", "print", handle,
        SystemVerilogUvmObjectEntryKind::Custom, 1});
  };
  type.do_record = [&](const auto handle, auto& entries) {
    ++record_hooks;
    entries.push_back({
        std::string{objects.name(handle)} + ".$do_record",
        "hook", "record", handle,
        SystemVerilogUvmObjectEntryKind::Custom, 1});
  };
  objects.register_type(std::move(type));
  objects.set_print_hook([&](const auto entries) {
    ++print_sink_calls;
    require(!entries.empty(), "UVM print sink must receive a stable snapshot");
  });
  objects.set_record_hook([&](const auto entries) {
    ++record_sink_calls;
    require(!entries.empty(), "UVM record sink must receive a stable snapshot");
  });

  const auto root = heap.allocate(make_descriptor());
  const auto child = heap.allocate(make_descriptor());
  objects.initialize(root, "root");
  objects.initialize(child, "child");
  heap.property(root, "value").packed =
      PackedLogic4::from_aval_bval(16, 0x1234, 0);
  heap.property(root, "label").string = "root-label";
  heap.property(root, "child").handle = child;
  heap.property(root, "alias").handle = child;
  heap.property(root, "ignored").packed =
      PackedLogic4::from_aval_bval(8, 0xaa, 0);
  heap.property(child, "value").packed =
      PackedLogic4::from_aval_bval(16, 0x5678, 0);
  heap.property(child, "label").string = "child-label";
  heap.property(child, "child").handle = root;
  heap.property(child, "alias").handle = root;

  require(
      objects.instance_id(root) == 0 && objects.instance_id(child) == 1
          && objects.instance_count() == 2
          && objects.name(root) == "root"
          && objects.full_name(root) == "root"
          && objects.type_name(root) == "uvm_item",
      "UVM construction must retain deterministic names and type/instance identity");
  objects.set_name(root, "renamed");
  require(
      objects.name(root) == "renamed" && objects.full_name(root) == "renamed",
      "UVM set_name must update leaf and default full-name identity");

  const auto cloned = objects.clone(root);
  const auto cloned_child = heap.property(cloned, "child").handle;
  require(
      cloned != root && cloned_child != child && cloned_child != 0
          && heap.property(cloned_child, "child").handle == cloned
          && heap.property(cloned, "alias").handle == child
          && objects.name(cloned) == "renamed"
          && objects.name(cloned_child) == "child"
          && objects.instance_id(cloned) == 2
          && objects.instance_id(cloned_child) == 3
          && objects.compare(root, cloned),
      "UVM clone must deep-copy fields while preserving cycles and reference aliases");
  require(
      heap.property(cloned, "ignored").packed
              != heap.property(root, "ignored").packed
          && copy_hooks == 2 && compare_hooks == 2,
      "UVM field flags and virtual copy/compare hooks must execute per object");

  heap.property(cloned_child, "value").packed =
      PackedLogic4::from_aval_bval(16, 7, 0);
  require(
      !objects.compare(root, cloned),
      "UVM deep compare must detect a recursive field mismatch");
  objects.copy(cloned, root);
  const auto recopied_child = heap.property(cloned, "child").handle;
  require(
      recopied_child != cloned_child && recopied_child != child
          && heap.property(recopied_child, "child").handle == cloned
          && objects.compare(root, cloned),
      "UVM copy must publish a new deterministic recursive graph");

  const auto printed = objects.print(cloned);
  const auto recorded = objects.record(cloned);
  require(
      std::ranges::any_of(printed, [](const auto& entry) {
        return entry.kind == SystemVerilogUvmObjectEntryKind::Cycle
            && entry.path == "renamed.child.child"
            && entry.value == "renamed";
      })
          && std::ranges::none_of(printed, [](const auto& entry) {
               return entry.path.find("ignored") != std::string::npos;
             })
          && std::ranges::any_of(recorded, [](const auto& entry) {
               return entry.kind == SystemVerilogUvmObjectEntryKind::Custom
                   && entry.value == "record";
             })
          && print_hooks == 2 && record_hooks == 2
          && print_sink_calls == 1 && record_sink_calls == 1,
      "UVM print/record automation must expose deterministic cycles, flags, hooks, and snapshots");

  const auto live_before_failure = heap.live_objects();
  SystemVerilogUvmObjectService bounded{
      heap,
      [&](const std::string_view, const std::string_view,
          const std::string_view) {
        return heap.allocate(make_descriptor());
      },
      {16, 1, 256, 4096}};
  SystemVerilogUvmObjectDescriptor bounded_type;
  bounded_type.specialization_identity = "work::uvm_item";
  bounded_type.type_name = "uvm_item";
  bounded_type.fields = {
      {"value", SystemVerilogUvmFieldFlag::None},
      {"child", SystemVerilogUvmFieldFlag::None}};
  bounded.register_type(std::move(bounded_type));
  bounded.initialize(root, "bounded-root");
  bool bounded_failed = false;
  try {
    (void)bounded.clone(root);
  } catch (const std::length_error&) {
    bounded_failed = true;
  }
  require(
      bounded_failed && heap.live_objects() == live_before_failure
          && heap.property(root, "child").handle == child,
      "UVM recursive copy limits must roll back all newly allocated objects");

  bool duplicate_failed = false;
  try {
    SystemVerilogUvmObjectDescriptor duplicate;
    duplicate.specialization_identity = "work::uvm_item";
    duplicate.type_name = "duplicate";
    objects.register_type(std::move(duplicate));
  } catch (const std::invalid_argument&) {
    duplicate_failed = true;
  }
  bool null_copy_failed = false;
  try {
    objects.copy(root, 0);
  } catch (const std::invalid_argument&) {
    null_copy_failed = true;
  }
  require(
      duplicate_failed && null_copy_failed,
      "duplicate UVM types and null copy operands must reject explicitly");
}

}  // namespace fsim::tests::runtime
