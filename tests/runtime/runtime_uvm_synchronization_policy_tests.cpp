// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_synchronization.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

[[nodiscard]] fsim::runtime::SystemVerilogClassDescriptor descriptor() {
  using namespace fsim::runtime;
  SystemVerilogClassDescriptor result;
  result.declared_type = "uvm_pkg::uvm_object";
  result.dynamic_type = "work::sync_object";
  result.specialization_identity = "work::sync_object";
  result.assignable_declared_types = {
      "work::sync_object", "uvm_pkg::uvm_object"};
  return result;
}

[[nodiscard]] fsim::runtime::SystemVerilogUvmObjectDescriptor
object_descriptor() {
  fsim::runtime::SystemVerilogUvmObjectDescriptor result;
  result.specialization_identity = "work::sync_object";
  result.type_name = "sync_object";
  return result;
}

[[nodiscard]] fsim::runtime::SystemVerilogUvmPackItem value(
    std::string text) {
  using namespace fsim::runtime;
  SystemVerilogUvmPackItem result;
  result.kind = SystemVerilogUvmPackItemKind::String;
  result.type_name = "string";
  result.string_value = std::move(text);
  return result;
}

}  // namespace

void test_systemverilog_uvm_synchronization_policies() {
  using namespace fsim::runtime;
  SystemVerilogClassHeap heap{{16, 1U << 14U}};
  SystemVerilogUvmObjectService objects{
      heap,
      [&](const auto, const auto, const auto) { return heap.allocate(descriptor()); }};
  objects.register_type(object_descriptor());
  const auto first = heap.allocate(descriptor());
  const auto second = heap.allocate(descriptor());
  objects.initialize(first, "first");
  objects.initialize(second, "second");

  SystemVerilogUvmPolicyLimits limits;
  limits.maximum_events = 8;
  limits.maximum_barriers = 4;
  limits.maximum_waiters = 16;
  limits.maximum_callbacks = 4;
  limits.maximum_pools = 4;
  limits.maximum_pool_entries = 8;
  limits.maximum_queues = 4;
  limits.maximum_queue_entries = 8;
  limits.maximum_heartbeats = 4;
  limits.maximum_heartbeat_participants = 8;
  limits.maximum_callback_failures = 2;
  limits.maximum_spell_candidates = 2;
  limits.maximum_spell_distance = 3;
  limits.maximum_text_bytes = 64;
  limits.maximum_mutations = 128;
  SystemVerilogUvmSynchronizationService policies{objects, limits};

  const auto event = policies.event("phase_done");
  require(
      policies.event("phase_done") == event &&
          policies.find_event("phase_done") == event,
      "UVM event pools must return one stable identity per deterministic name");
  std::vector<std::string> order;
  SystemVerilogUvmEventCallbackToken self_removing{};
  self_removing = policies.add_event_callback(
      event, [&](const auto active, const auto data) {
        require(active == event && data == first,
                "UVM event callback must retain event and payload identity");
        order.push_back("callback");
        require(policies.remove_event_callback(event, self_removing),
                "UVM event callbacks must support removal during dispatch");
      });
  const auto failing_callback = policies.add_event_callback(
      event, [](const auto, const auto) {
        throw std::runtime_error{"contained event callback"};
      });
  const auto trigger_wait = policies.wait_event(
      event, 11, SystemVerilogUvmEventWaitKind::Trigger,
      [&](const auto outcome, const auto data) {
        require(outcome == SystemVerilogUvmWaitOutcome::Triggered &&
                    data == first,
                "UVM trigger wait must receive its object payload");
        order.push_back("waiter");
      });
  require(trigger_wait != 0,
          "future UVM event waits must own cancellable handles");
  policies.trigger_event(event, first);
  require(
      order == std::vector<std::string>{"callback", "waiter"} &&
          policies.event_snapshot(event).on &&
          policies.event_snapshot(event).trigger_count == 1 &&
          policies.event_snapshot(event).trigger_data == first &&
          policies.callback_failures().size() == 1,
      "UVM event trigger must dispatch callback snapshots before ordered "
      "waiters and contain callback failure");
  std::size_t persistent{};
  require(
      policies.wait_event(
          event, 13, SystemVerilogUvmEventWaitKind::PersistentTrigger,
          [&](const auto, const auto) { ++persistent; }) == 0 &&
          persistent == 1,
      "persistent UVM event waits must observe an already-triggered event");
  const auto off_wait = policies.wait_event(
      event, 12, SystemVerilogUvmEventWaitKind::Off,
      [&](const auto outcome, const auto) {
        require(outcome == SystemVerilogUvmWaitOutcome::Triggered,
                "UVM wait_off must resume when the event resets");
        order.push_back("off");
      });
  require(off_wait != 0,
          "wait_off on an active event must retain a cancellable waiter");
  policies.reset_event(event);
  require(order.back() == "off" && !policies.event_snapshot(event).on,
          "UVM event reset must deterministically release wait_off");
  const auto cancelled = policies.wait_event(
      event, 14, SystemVerilogUvmEventWaitKind::Trigger,
      [&](const auto outcome, const auto) {
        require(outcome == SystemVerilogUvmWaitOutcome::Cancelled,
                "cancelled UVM event wait must receive cancellation");
      });
  require(policies.cancel_wait(cancelled) && !policies.cancel_wait(cancelled),
          "UVM event wait cancellation must be exact and idempotent");
  bool referenced_event_rejected{};
  try {
    (void)policies.erase_event(event);
  } catch (const SystemVerilogUvmPolicyError& error) {
    referenced_event_rejected =
        error.diagnostic_code() == "FSIM-UVM-SYNC-001";
  }
  require(
      referenced_event_rejected &&
          policies.remove_event_callback(event, failing_callback) &&
          policies.erase_event(event) && !policies.find_event("phase_done"),
      "UVM event-pool teardown must reject references then erase clean state");

  std::vector<std::uint64_t> barrier_order;
  const auto barrier = policies.create_barrier("join", 2, true);
  const auto first_wait = policies.wait_barrier(
      barrier, 21, [&](const auto outcome, const auto) {
        require(outcome == SystemVerilogUvmWaitOutcome::BarrierReleased,
                "UVM barrier must report threshold release");
        barrier_order.push_back(21);
      });
  const auto second_wait = policies.wait_barrier(
      barrier, 22, [&](const auto, const auto) { barrier_order.push_back(22); });
  require(
      first_wait != 0 && second_wait != 0 &&
          barrier_order == std::vector<std::uint64_t>{21, 22} &&
          policies.barrier_snapshot(barrier).release_count == 1 &&
          policies.barrier_snapshot(barrier).waiter_count == 0,
      "UVM barrier threshold and auto-reset must release process waiters in "
      "registration order");
  (void)policies.wait_barrier(barrier, 23);
  policies.set_barrier_threshold(barrier, 1);
  require(policies.barrier_snapshot(barrier).release_count == 2,
          "lowering a UVM barrier threshold must release satisfied waiters");
  const auto reset_wait = policies.wait_barrier(
      barrier, 24, [&](const auto outcome, const auto) {
        require(outcome == SystemVerilogUvmWaitOutcome::Reset,
                "barrier reset wakeup must retain reset outcome");
      });
  require(reset_wait != 0, "barrier reset setup must retain its waiter");
  policies.reset_barrier(barrier, true);
  require(policies.erase_barrier(barrier),
          "quiescent UVM barriers must support deterministic teardown");

  const auto pool = policies.create_pool("objects");
  policies.pool_put(pool, "zeta", value("last"));
  policies.pool_put(pool, "alpha", value("first"));
  policies.pool_put(pool, "alpha", value("updated"));
  require(
      policies.pool_keys(pool) == std::vector<std::string>{"alpha", "zeta"} &&
          policies.pool_get(pool, "alpha")->string_value == "updated" &&
          policies.pool_erase(pool, "zeta") &&
          !policies.pool_get(pool, "missing"),
      "UVM pool keys, replacement, lookup, and erasure must be deterministic");

  const auto queue = policies.create_queue("ordered");
  policies.queue_push_back(queue, value("middle"));
  policies.queue_push_front(queue, value("front"));
  policies.queue_insert(queue, 2, value("back"));
  require(
      policies.queue_size(queue) == 3 &&
          policies.queue_get(queue, 1)->string_value == "middle" &&
          policies.queue_pop_front(queue)->string_value == "front" &&
          policies.queue_pop_back(queue)->string_value == "back" &&
          policies.queue_erase(queue, 0) && policies.queue_size(queue) == 0,
      "UVM queue front/back/insert/get/delete behavior must preserve order");

  const auto heartbeat_event = policies.event("heartbeat_tick");
  const auto heartbeat = policies.create_heartbeat(
      "all_live", heartbeat_event, SystemVerilogUvmHeartbeatMode::All);
  policies.heartbeat_add(heartbeat, first);
  policies.heartbeat_add(heartbeat, second);
  policies.heartbeat_start(heartbeat);
  policies.heartbeat_beat(heartbeat, first);
  policies.trigger_event(heartbeat_event);
  policies.heartbeat_beat(heartbeat, first);
  policies.heartbeat_beat(heartbeat, second);
  policies.trigger_event(heartbeat_event);
  const auto heartbeat_state = policies.heartbeat_snapshot(heartbeat);
  require(
      heartbeat_state.active && heartbeat_state.checks == 2 &&
          heartbeat_state.failures == 1 && heartbeat_state.observed_count == 0,
      "UVM heartbeat ALL mode must detect missing activity and reset each "
      "observation window");
  policies.heartbeat_stop(heartbeat);

  const auto any_event = policies.event("any_tick");
  const auto any_heartbeat = policies.create_heartbeat(
      "any_live", any_event, SystemVerilogUvmHeartbeatMode::Any);
  policies.heartbeat_add(any_heartbeat, first);
  policies.heartbeat_add(any_heartbeat, second);
  policies.heartbeat_start(any_heartbeat);
  policies.heartbeat_beat(any_heartbeat, first);
  policies.trigger_event(any_event);
  const auto one_event = policies.event("one_tick");
  const auto one_heartbeat = policies.create_heartbeat(
      "one_live", one_event, SystemVerilogUvmHeartbeatMode::One);
  policies.heartbeat_add(one_heartbeat, first);
  policies.heartbeat_add(one_heartbeat, second);
  policies.heartbeat_start(one_heartbeat);
  policies.heartbeat_beat(one_heartbeat, first);
  policies.heartbeat_beat(one_heartbeat, second);
  policies.trigger_event(one_event);
  policies.heartbeat_beat(one_heartbeat, first);
  policies.trigger_event(one_event);
  require(
      policies.heartbeat_snapshot(any_heartbeat).failures == 0 &&
          policies.heartbeat_snapshot(one_heartbeat).checks == 2 &&
          policies.heartbeat_snapshot(one_heartbeat).failures == 1,
      "UVM heartbeat ANY and ONE modes must enforce their exact activity "
      "cardinality contracts");

  require(
      policies.spell_challenge(
          "phase_dne", {"heartbeat_tick", "phase_done", "other"}, 2) ==
          std::vector<std::string>{"phase_done"},
      "UVM spell challenge must return bounded distance/name ordering");

  bool invalid{};
  bool resource{};
  try {
    (void)policies.event_snapshot(UINT64_C(0xffff));
  } catch (const SystemVerilogUvmPolicyError& error) {
    invalid = error.diagnostic_code() == "FSIM-UVM-SYNC-001";
  }
  try {
    SystemVerilogUvmPolicyLimits tiny;
    tiny.maximum_events = 1;
    SystemVerilogUvmSynchronizationService bounded{objects, tiny};
    (void)bounded.event("one");
    (void)bounded.event("two");
  } catch (const SystemVerilogUvmPolicyError& error) {
    resource = error.diagnostic_code() == "FSIM-UVM-SYNC-002";
  }
  require(invalid && resource,
          "UVM policy services must catalog stale and bounded failures");

  bool reset_cancelled{};
  const auto reset_event = policies.event("reset_lifecycle");
  (void)policies.wait_event(
      reset_event, 99, SystemVerilogUvmEventWaitKind::Trigger,
      [&](const auto outcome, const auto) {
        reset_cancelled = outcome == SystemVerilogUvmWaitOutcome::Cancelled;
      });
  policies.reset();
  require(reset_cancelled && !policies.find_event("reset_lifecycle"),
          "UVM policy reset must cancel waiters and clear owned lifecycle state");
}

}  // namespace fsim::tests::runtime
