// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_sequence.hpp"

#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

constexpr std::string_view kSequencerType{"work::access_sequencer#(32)"};
constexpr std::string_view kSequenceType{"work::access_sequence#(32)"};
constexpr std::string_view kRequestType{"work::access_request#(32)"};
constexpr std::string_view kResponseType{"work::access_response#(32)"};

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

void require_error(
    const std::string_view code,
    const std::function<void()>& operation,
    const std::string_view message) {
  try {
    operation();
  } catch (const SystemVerilogUvmSequenceError& error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

SystemVerilogUvmSequenceProfile profile() {
  return {std::string{kRequestType}, std::string{kResponseType}};
}

SystemVerilogClassDescriptor class_descriptor(
    const std::string_view specialization) {
  SystemVerilogClassDescriptor result;
  result.dynamic_type = std::string{specialization};
  result.specialization_identity = std::string{specialization};
  if (specialization == kSequencerType) {
    result.declared_type = "uvm_pkg::uvm_sequencer";
    result.assignable_declared_types = {
        std::string{kSequencerType}, "uvm_pkg::uvm_sequencer",
        "uvm_pkg::uvm_component", "uvm_pkg::uvm_object"};
  } else if (specialization == kSequenceType) {
    result.declared_type = "uvm_pkg::uvm_sequence";
    result.assignable_declared_types = {
        std::string{kSequenceType}, "uvm_pkg::uvm_sequence",
        "uvm_pkg::uvm_sequence_item", "uvm_pkg::uvm_object"};
  } else {
    result.declared_type = "uvm_pkg::uvm_sequence_item";
    result.assignable_declared_types = {
        std::string{specialization}, "uvm_pkg::uvm_sequence_item",
        "uvm_pkg::uvm_object"};
  }
  return result;
}

struct AccessFixture {
  SystemVerilogClassHeap heap{{512, 65'536}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle root{};

  AccessFixture()
      : objects(
            heap,
            [this](const std::string_view specialization,
                   const std::string_view,
                   const std::string_view) {
              return heap.allocate(class_descriptor(specialization));
            }),
        components(heap, objects, {4, 64, 8, 16, 64, 256}) {
    for (const auto type : {
             kSequencerType, kSequenceType, kRequestType, kResponseType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      objects.register_type(std::move(descriptor));
    }
    root = components.create_root("access");
  }

  [[nodiscard]] SystemVerilogClassHandle make_object(
      const std::string_view type,
      std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle make_sequencer(std::string name) {
    const auto result = make_object(kSequencerType, name);
    components.initialize(result, std::move(name), 0, root);
    return result;
  }
};

SystemVerilogUvmSequencerDescriptor sequencer_descriptor(
    const SystemVerilogClassHandle component) {
  return {component, std::string{kSequencerType}, profile()};
}

SystemVerilogUvmSequenceDescriptor sequence_descriptor(
    const SystemVerilogClassHandle object,
    std::string name,
    const SystemVerilogUvmSequenceHandle parent,
    const SystemVerilogUvmSequencerHandle sequencer) {
  return {
      object, std::move(name), std::string{kSequenceType}, profile(), parent,
      sequencer, {}};
}

SystemVerilogUvmSequenceItemDescriptor response_descriptor(
    const SystemVerilogClassHandle object,
    std::string name,
    const SystemVerilogUvmSequenceHandle owner,
    const SystemVerilogUvmSequencerHandle sequencer) {
  return {
      object, std::move(name), std::string{kResponseType},
      SystemVerilogUvmSequenceItemRole::Response, owner, sequencer};
}

}  // namespace

void test_systemverilog_uvm_sequence_access_and_responses() {
  AccessFixture fixture;
  SystemVerilogUvmSequenceService service{
      fixture.heap, fixture.objects, fixture.components};
  const auto sequencer = service.register_sequencer(
      sequencer_descriptor(fixture.make_sequencer("sequencer")));
  const auto add_sequence = [&](std::string name,
                                const SystemVerilogUvmSequenceHandle parent =
                                    SystemVerilogUvmSequenceHandle{}) {
    const auto object_name = name + "_object";
    return service.register_sequence(sequence_descriptor(
        fixture.make_object(kSequenceType, object_name), std::move(name),
        parent, sequencer));
  };
  const auto owner = add_sequence("owner");
  const auto child = add_sequence("child", owner);
  const auto sibling = add_sequence("sibling");
  const auto grabber = add_sequence("grabber");

  const auto owner_lock = service.request_lock(owner);
  const auto child_grab = service.request_grab(child);
  const auto sibling_lock = service.request_lock(sibling);
  const auto competing_grab = service.request_grab(grabber);
  const auto access_order = service.access_requests(sequencer);
  require(
      service.access_snapshot(owner_lock).state
              == SystemVerilogUvmSequenceAccessState::Granted
          && service.access_snapshot(child_grab).state
              == SystemVerilogUvmSequenceAccessState::Granted
          && service.access_snapshot(sibling_lock).state
              == SystemVerilogUvmSequenceAccessState::Pending
          && service.access_snapshot(competing_grab).state
              == SystemVerilogUvmSequenceAccessState::Pending
          && access_order
              == std::vector<SystemVerilogUvmSequenceAccessHandle>{
                  owner_lock, child_grab, competing_grab, sibling_lock}
          && service.has_lock(owner) && service.has_lock(child)
          && !service.has_lock(sibling),
      "locks must grant nested children and queue competing grabs before locks");
  require_error(
      "FSIM-UVM-SEQ-010", [&] { service.unlock(owner); },
      "only the exact nested top owner and kind may release access");
  service.ungrab(child);
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.access_snapshot(child_grab); },
      "released nested access handles must become stale");
  service.unlock(owner);
  require(
      service.access_snapshot(competing_grab).state
          == SystemVerilogUvmSequenceAccessState::Granted,
      "a queued grab must win when the owning lock is released");
  service.ungrab(grabber);
  require(
      service.access_snapshot(sibling_lock).state
          == SystemVerilogUvmSequenceAccessState::Granted,
      "the next queued lock must grant after the grab releases");
  service.unlock(sibling);

  const auto pending_owner = service.request_lock(owner);
  const auto pending_sibling = service.request_lock(sibling);
  require_error(
      "FSIM-UVM-SEQ-010", [&] { (void)service.request_grab(sibling); },
      "one sequence must not publish duplicate pending access requests");
  service.cancel_access(pending_sibling);
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.access_snapshot(pending_sibling); },
      "cancelled pending access handles must become stale");
  service.unlock(owner);
  require(service.access_requests(sequencer).empty(),
          "access cancellation and release must leave no queue residue");
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.access_snapshot(pending_owner); },
      "released owner handles must become stale");
  const auto cancelled_grant = service.request_grab(owner);
  service.cancel_access(cancelled_grant);
  require(
      service.access_requests(sequencer).empty(),
      "explicit cancellation of a granted access must release the sequencer");
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.access_snapshot(cancelled_grant); },
      "cancelled granted access handles must become stale");

  SystemVerilogUvmSequenceService foreign{
      fixture.heap, fixture.objects, fixture.components};
  const auto foreign_sequencer = foreign.register_sequencer(
      sequencer_descriptor(fixture.make_sequencer("foreign_sequencer")));
  const auto foreign_sequence = foreign.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "foreign_object"), "foreign", {},
      foreign_sequencer));
  const auto foreign_access = foreign.request_lock(foreign_sequence);
  require_error(
      "FSIM-UVM-SEQ-002",
      [&] { (void)service.access_snapshot(foreign_access); },
      "access identities must reject use by another simulation service");
  const auto unbound = service.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "unbound_object"), "unbound", {},
      {}));
  require_error(
      "FSIM-UVM-SEQ-010", [&] { (void)service.request_lock(unbound); },
      "unbound sequences must not acquire sequencer access");

  const auto add_response = [&](std::string name,
                                const SystemVerilogUvmSequenceHandle target) {
    const auto object_name = name + "_object";
    return service.register_item(response_descriptor(
        fixture.make_object(kResponseType, object_name), std::move(name),
        target, sequencer));
  };
  service.configure_response_queue(
      owner, 1, SystemVerilogUvmResponseOverflowPolicy::Error);
  const auto error_first = add_response("error_first", owner);
  const auto error_second = add_response("error_second", owner);
  require(
      service.route_response(error_first).status
          == SystemVerilogUvmResponseRouteStatus::Queued,
      "the first bounded response must queue");
  require_error(
      "FSIM-UVM-SEQ-009", [&] { (void)service.route_response(error_second); },
      "error overflow policy must diagnose a full response queue");
  require(
      service.responses(owner)
              == std::vector<SystemVerilogUvmSequenceItemHandle>{error_first}
          && service.snapshot(error_second).state
              == SystemVerilogUvmSequenceItemState::Owned,
      "response overflow failure must preserve queue and item state");

  const auto drop_newest_owner = add_sequence("drop_newest");
  service.configure_response_queue(
      drop_newest_owner, 1,
      SystemVerilogUvmResponseOverflowPolicy::DropNewest);
  const auto newest_first = add_response("newest_first", drop_newest_owner);
  const auto newest_second = add_response("newest_second", drop_newest_owner);
  (void)service.route_response(newest_first);
  const auto newest_result = service.route_response(newest_second);
  require(
      newest_result.status == SystemVerilogUvmResponseRouteStatus::DroppedNewest
          && newest_result.dropped == newest_second
          && service.responses(drop_newest_owner)
              == std::vector<SystemVerilogUvmSequenceItemHandle>{newest_first}
          && service.snapshot(newest_second).state
              == SystemVerilogUvmSequenceItemState::Owned,
      "drop-newest policy must retain the old response and report the new one");

  const auto drop_oldest_owner = add_sequence("drop_oldest");
  service.configure_response_queue(
      drop_oldest_owner, 1,
      SystemVerilogUvmResponseOverflowPolicy::DropOldest);
  const auto oldest_first = add_response("oldest_first", drop_oldest_owner);
  const auto oldest_second = add_response("oldest_second", drop_oldest_owner);
  (void)service.route_response(oldest_first);
  const auto oldest_result = service.route_response(oldest_second);
  require(
      oldest_result.status == SystemVerilogUvmResponseRouteStatus::DroppedOldest
          && oldest_result.dropped == oldest_first
          && service.responses(drop_oldest_owner)
              == std::vector<SystemVerilogUvmSequenceItemHandle>{oldest_second}
          && service.snapshot(oldest_first).state
              == SystemVerilogUvmSequenceItemState::Owned
          && service.snapshot(oldest_second).state
              == SystemVerilogUvmSequenceItemState::Routed,
      "drop-oldest policy must replace the queue head transactionally");

  const auto quiet_error_owner = add_sequence("quiet_error");
  service.configure_response_queue(
      quiet_error_owner, 1, SystemVerilogUvmResponseOverflowPolicy::Error,
      false);
  const auto quiet_first = add_response("quiet_first", quiet_error_owner);
  const auto quiet_second = add_response("quiet_second", quiet_error_owner);
  (void)service.route_response(quiet_first);
  require(
      service.route_response(quiet_second).status
          == SystemVerilogUvmResponseRouteStatus::DroppedNewest,
      "disabled overflow errors must map error policy to drop-newest behavior");
  require_error(
      "FSIM-UVM-SEQ-009",
      [&] {
        service.configure_response_queue(
            owner, 0, SystemVerilogUvmResponseOverflowPolicy::Error);
      },
      "zero response depth must reject");
  require_error(
      "FSIM-UVM-SEQ-009",
      [&] {
        service.configure_response_queue(
            owner, service.limits().maximum_responses_per_sequence + 1U,
            SystemVerilogUvmResponseOverflowPolicy::Error);
      },
      "configured response depth must remain within the service ceiling");
  require_error(
      "FSIM-UVM-SEQ-009",
      [&] {
        service.configure_response_queue(
            owner, 1,
            static_cast<SystemVerilogUvmResponseOverflowPolicy>(255));
      },
      "unknown response policies must reject");
  const auto shrink_owner = add_sequence("shrink_owner");
  service.configure_response_queue(
      shrink_owner, 2, SystemVerilogUvmResponseOverflowPolicy::Error);
  const auto shrink_first = add_response("shrink_first", shrink_owner);
  const auto shrink_second = add_response("shrink_second", shrink_owner);
  (void)service.route_response(shrink_first);
  (void)service.route_response(shrink_second);
  require_error(
      "FSIM-UVM-SEQ-009",
      [&] {
        service.configure_response_queue(
            shrink_owner, 1, SystemVerilogUvmResponseOverflowPolicy::Error);
      },
      "response depth must not shrink below retained queue contents");
  require(
      service.responses(shrink_owner)
          == std::vector<SystemVerilogUvmSequenceItemHandle>{
              shrink_first, shrink_second},
      "rejected response reconfiguration must preserve queue order");

  SystemVerilogUvmSequenceLimits starvation_limits;
  starvation_limits.maximum_consecutive_grabs = 1;
  SystemVerilogUvmSequenceService starvation{
      fixture.heap, fixture.objects, fixture.components, starvation_limits};
  const auto starvation_sequencer = starvation.register_sequencer(
      sequencer_descriptor(fixture.make_sequencer("starvation_sequencer")));
  const auto starvation_sequence = [&](std::string name) {
    const auto object_name = name + "_object";
    return starvation.register_sequence(sequence_descriptor(
        fixture.make_object(kSequenceType, object_name), std::move(name), {},
        starvation_sequencer));
  };
  const auto held = starvation_sequence("held");
  const auto waiting_lock = starvation_sequence("waiting_lock");
  const auto first_grabber = starvation_sequence("first_grabber");
  const auto second_grabber = starvation_sequence("second_grabber");
  (void)starvation.request_lock(held);
  const auto starved_lock = starvation.request_lock(waiting_lock);
  const auto first_grab = starvation.request_grab(first_grabber);
  const auto second_grab = starvation.request_grab(second_grabber);
  starvation.unlock(held);
  require(
      starvation.access_snapshot(first_grab).state
          == SystemVerilogUvmSequenceAccessState::Granted,
      "the first queued grab must retain precedence");
  starvation.ungrab(first_grabber);
  require(
      starvation.access_snapshot(starved_lock).state
              == SystemVerilogUvmSequenceAccessState::Granted
          && starvation.access_snapshot(second_grab).state
              == SystemVerilogUvmSequenceAccessState::Pending,
      "the consecutive-grab ceiling must admit a waiting lock");
  starvation.unlock(waiting_lock);
  require(
      starvation.access_snapshot(second_grab).state
          == SystemVerilogUvmSequenceAccessState::Granted,
      "grab arbitration must resume after the starvation-preventing lock");
  starvation.ungrab(second_grabber);

  SystemVerilogUvmSequenceLimits access_limits;
  access_limits.maximum_access_requests = 2;
  access_limits.maximum_access_depth = 1;
  SystemVerilogUvmSequenceService bounded{
      fixture.heap, fixture.objects, fixture.components, access_limits};
  const auto bounded_sequencer = bounded.register_sequencer(
      sequencer_descriptor(fixture.make_sequencer("bounded_sequencer")));
  const auto bounded_owner = bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "bounded_owner_object"),
      "bounded_owner", {}, bounded_sequencer));
  const auto bounded_child = bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "bounded_child_object"),
      "bounded_child", bounded_owner, bounded_sequencer));
  (void)bounded.request_lock(bounded_owner);
  require_error(
      "FSIM-UVM-SEQ-006", [&] { (void)bounded.request_grab(bounded_child); },
      "access count and nesting ceilings must reject without publication");
  require(bounded.access_requests(bounded_sequencer).size() == 1,
          "access-depth rejection must preserve the held access only");

  SystemVerilogUvmSequenceLimits count_limits;
  count_limits.maximum_access_requests = 1;
  SystemVerilogUvmSequenceService count_bounded{
      fixture.heap, fixture.objects, fixture.components, count_limits};
  const auto count_sequencer = count_bounded.register_sequencer(
      sequencer_descriptor(fixture.make_sequencer("count_sequencer")));
  const auto count_owner = count_bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "count_owner_object"), "count_owner",
      {}, count_sequencer));
  const auto count_waiter = count_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "count_waiter_object"),
          "count_waiter", {}, count_sequencer));
  (void)count_bounded.request_lock(count_owner);
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)count_bounded.request_lock(count_waiter); },
      "global access-request count must remain resource bounded");
  require(
      count_bounded.access_requests(count_sequencer).size() == 1,
      "access-count rejection must not publish a pending identity");

  for (std::size_t invalid_limit = 0; invalid_limit != 3; ++invalid_limit) {
    auto invalid_limits = SystemVerilogUvmSequenceLimits{};
    if (invalid_limit == 0) invalid_limits.maximum_access_requests = 0;
    if (invalid_limit == 1) invalid_limits.maximum_access_depth = 0;
    if (invalid_limit == 2) invalid_limits.maximum_consecutive_grabs = 0;
    require_error(
        "FSIM-UVM-SEQ-006",
        [&] {
          SystemVerilogUvmSequenceService rejected{
              fixture.heap, fixture.objects, fixture.components,
              invalid_limits};
        },
        "every access-resource ceiling must be nonzero");
  }

  SystemVerilogUvmSequenceHandle stopping_sequence;
  SystemVerilogUvmSequenceAccessHandle stopping_access;
  SystemVerilogUvmSequenceDescriptor stopping_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "stopping_object"), "stopping", {},
      sequencer);
  stopping_descriptor.hooks.body = [&](const auto executing) {
    require(executing == stopping_sequence,
            "stop callback must receive the executing sequence");
    stopping_access = service.request_lock(executing);
    service.request_stop(executing);
  };
  stopping_sequence = service.register_sequence(std::move(stopping_descriptor));
  const auto stopped = service.start(stopping_sequence);
  require(stopped.stopped && service.access_requests(sequencer).empty(),
          "cooperative stop must cancel access owned by the sequence tree");
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.access_snapshot(stopping_access); },
      "stop-cancelled access identities must become stale");
}

}  // namespace fsim::tests::runtime
