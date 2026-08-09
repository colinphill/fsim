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

constexpr std::string_view kSequencerType{"work::driver_sequencer#(32)"};
constexpr std::string_view kSequenceType{"work::driver_sequence#(32)"};
constexpr std::string_view kRequestType{"work::driver_request#(32)"};
constexpr std::string_view kResponseType{"work::driver_response#(32)"};

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

struct HandshakeFixture {
  SystemVerilogClassHeap heap{{512, 65'536}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmRootHandle first_root{};
  SystemVerilogUvmRootHandle second_root{};

  HandshakeFixture()
      : objects(
            heap,
            [this](const std::string_view specialization,
                   const std::string_view,
                   const std::string_view) {
              return heap.allocate(class_descriptor(specialization));
            }),
        components(heap, objects, {4, 128, 8, 32, 128, 512}) {
    for (const auto type : {
             kSequencerType, kSequenceType, kRequestType, kResponseType}) {
      SystemVerilogUvmObjectDescriptor descriptor;
      descriptor.specialization_identity = std::string{type};
      descriptor.type_name = std::string{type};
      objects.register_type(std::move(descriptor));
    }
    first_root = components.create_root("driver");
    second_root = components.create_root("peer");
  }

  [[nodiscard]] SystemVerilogClassHandle make_object(
      const std::string_view type,
      std::string name) {
    const auto result = heap.allocate(class_descriptor(type));
    objects.initialize(result, std::move(name));
    return result;
  }

  [[nodiscard]] SystemVerilogClassHandle make_sequencer(
      std::string name,
      const SystemVerilogUvmRootHandle root) {
    const auto result = make_object(kSequencerType, name);
    components.initialize(result, std::move(name), 0, root);
    return result;
  }
};

SystemVerilogUvmSequenceDescriptor sequence_descriptor(
    const SystemVerilogClassHandle object,
    std::string name,
    const SystemVerilogUvmSequencerHandle sequencer,
    SystemVerilogUvmSequenceHooks hooks = {}) {
  return {
      object, std::move(name), std::string{kSequenceType}, profile(), {},
      sequencer, std::move(hooks)};
}

SystemVerilogUvmSequenceItemDescriptor item_descriptor(
    const SystemVerilogClassHandle object,
    std::string name,
    const SystemVerilogUvmSequenceItemRole role,
    const SystemVerilogUvmSequenceHandle owner,
    const SystemVerilogUvmSequencerHandle sequencer) {
  return {
      object, std::move(name),
      role == SystemVerilogUvmSequenceItemRole::Request
          ? std::string{kRequestType}
          : std::string{kResponseType},
      role, owner, sequencer};
}

struct SequenceItemPair {
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequenceItemHandle item;
};

}  // namespace

void test_systemverilog_uvm_sequence_handshakes() {
  HandshakeFixture fixture;
  Scheduler scheduler;
  SystemVerilogUvmPhaseService phases{fixture.components, scheduler};
  const std::vector<SystemVerilogUvmRootHandle> roots{fixture.first_root};
  const auto schedule = phases.create_standard_schedule(roots);
  SystemVerilogUvmObjectionService objections{
      fixture.objects, fixture.components, phases, scheduler};
  phases.set_objection_service(objections);
  SystemVerilogUvmSequenceService service{
      fixture.heap, fixture.objects, fixture.components, phases, objections};
  const auto sequencer = service.register_sequencer(
      {fixture.make_sequencer("sequencer", fixture.first_root),
       std::string{kSequencerType}, profile()});
  const auto peer_sequencer = service.register_sequencer(
      {fixture.make_sequencer("peer_sequencer", fixture.second_root),
       std::string{kSequencerType}, profile()});
  service.configure_handshake(sequencer, 3, 1);

  std::size_t next_name{};
  const auto make_pair = [&](const std::string_view prefix,
                             const SystemVerilogUvmSequencerHandle
                                 selected_sequencer =
                                 SystemVerilogUvmSequencerHandle{}) {
    const auto suffix = std::to_string(next_name++);
    const auto target = selected_sequencer ? selected_sequencer : sequencer;
    const auto sequence = service.register_sequence(sequence_descriptor(
        fixture.make_object(kSequenceType, std::string{prefix} + suffix),
        std::string{prefix} + suffix, target));
    const auto item = service.register_item(item_descriptor(
        fixture.make_object(kRequestType, std::string{prefix} + "_item" + suffix),
        std::string{prefix} + "_item" + suffix,
        SystemVerilogUvmSequenceItemRole::Request, sequence, target));
    return SequenceItemPair{sequence, item};
  };
  const auto make_response = [&](const SequenceItemPair& pair,
                                 const std::string_view prefix) {
    const auto suffix = std::to_string(next_name++);
    return service.register_item(item_descriptor(
        fixture.make_object(
            kResponseType, std::string{prefix} + "_response" + suffix),
        std::string{prefix} + "_response" + suffix,
        SystemVerilogUvmSequenceItemRole::Response, pair.sequence, sequencer));
  };

  require(
      service.try_next_item(sequencer).status
          == SystemVerilogUvmSequenceAcquireStatus::Empty,
      "try-next-item must report an empty sequencer without publication");
  require(
      service.get_next_item(sequencer).status
              == SystemVerilogUvmSequenceAcquireStatus::WaitingForRequest
          && service.get(sequencer).status
              == SystemVerilogUvmSequenceAcquireStatus::WaitingForRequest
          && service.peek(sequencer).status
              == SystemVerilogUvmSequenceAcquireStatus::WaitingForRequest
          && service.push_next_item(sequencer).status
              == SystemVerilogUvmSequenceAcquireStatus::WaitingForRequest,
      "blocking pull and push forms must distinguish waiting from nonblocking empty");
  const auto irrelevant = make_pair("irrelevant");
  const auto irrelevant_request = service.enqueue_request(
      {irrelevant.sequence, 100, false, {}, irrelevant.item});
  require(
      service.try_next_item(sequencer).status
              == SystemVerilogUvmSequenceAcquireStatus::WaitingForRelevant
          && service.requests(sequencer)
              == std::vector<SystemVerilogUvmSequenceRequestHandle>{
                  irrelevant_request},
      "try-next-item must retain an all-irrelevant request queue");
  service.set_request_relevant(irrelevant_request, true);
  const auto first = service.get_next_item(sequencer);
  require(
      first.status == SystemVerilogUvmSequenceAcquireStatus::Acquired
          && first.transaction
          && first.transaction->state
              == SystemVerilogUvmSequenceTransactionState::Reserved
          && first.transaction->kind
              == SystemVerilogUvmSequenceHandshakeKind::GetNextItem
          && first.transaction->request.handle == irrelevant_request
          && first.transaction->request_item == irrelevant.item
          && first.transaction->request_object
              == service.snapshot(irrelevant.item).object
          && service.snapshot(irrelevant.item).state
              == SystemVerilogUvmSequenceItemState::InFlight,
      "get-next-item must retain exact request, item, object, and state identity");
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.request_snapshot(irrelevant_request); },
      "selected request handles must become stale while the transaction retains identity");

  const auto second_pair = make_pair("second");
  const auto third_pair = make_pair("third");
  const auto fourth_pair = make_pair("fourth");
  (void)service.macro_send(second_pair.item, 90);
  (void)service.macro_send(third_pair.item, 80);
  (void)service.macro_send(fourth_pair.item, 70);
  const auto second = service.try_next_item(sequencer);
  const auto third = service.get_next_item(sequencer);
  require(
      second.transaction && third.transaction
          && service.snapshot(sequencer).active_transactions.size() == 3
          && service.get_next_item(sequencer).status
              == SystemVerilogUvmSequenceAcquireStatus::Backpressured
          && service.requests(sequencer).size() == 1,
      "configured in-flight depth must pipeline three requests and backpressure the next");
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] { service.configure_handshake(sequencer, 2, 1); },
      "live reconfiguration must not strand an existing pipeline");

  const auto first_response = make_response(irrelevant, "first");
  service.item_done(first.transaction->handle, first_response);
  require(
      service.transaction_snapshot(first.transaction->handle).state
              == SystemVerilogUvmSequenceTransactionState::Completed
          && service.transaction_snapshot(first.transaction->handle)
                 .response_item == first_response
          && service.responses(irrelevant.sequence)
              == std::vector<SystemVerilogUvmSequenceItemHandle>{first_response}
          && service.snapshot(irrelevant.item).state
              == SystemVerilogUvmSequenceItemState::Completed,
      "item-done must complete and route an exact response identity");
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] { service.item_done(first.transaction->handle); },
      "item-done must reject duplicate completion");
  service.item_done(second.transaction->handle);
  const auto second_response = make_response(second_pair, "second");
  service.put_response(second.transaction->handle, second_response);
  require(
      service.transaction_snapshot(second.transaction->handle).response_item
          == second_response,
      "put-response must attach a later response to the completed request");
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] {
        service.put_response(second.transaction->handle, second_response);
      },
      "put-response must reject a duplicate response");
  service.item_done(third.transaction->handle);
  require(
      service.get_next_item(sequencer).transaction->request_item
          == fourth_pair.item,
      "completion must release one backpressure slot without reordering requests");
  const auto fourth_transaction =
      service.snapshot(sequencer).active_transactions.front();
  service.item_done(fourth_transaction);

  const auto peek_pair = make_pair("peek");
  const auto peek_request = service.macro_send(peek_pair.item);
  const auto peek_first = service.peek(sequencer);
  const auto peek_second = service.peek(sequencer);
  require(
      peek_first.transaction && peek_second.transaction
          && peek_first.transaction->handle == peek_second.transaction->handle
          && peek_second.transaction->peek_count == 2
          && peek_second.transaction->request.handle == peek_request
          && service.requests(sequencer).empty(),
      "repeated peek must retain one reserved request identity");
  const auto get_after_peek = service.get(sequencer);
  require(
      get_after_peek.transaction
          && get_after_peek.transaction->handle == peek_first.transaction->handle
          && get_after_peek.transaction->state
              == SystemVerilogUvmSequenceTransactionState::Completed,
      "get after peek must complete the same retained transaction");

  const auto immediate_pair = make_pair("immediate");
  (void)service.macro_send(immediate_pair.item);
  const auto immediate = service.get(sequencer);
  require(
      immediate.transaction
          && immediate.transaction->state
              == SystemVerilogUvmSequenceTransactionState::Completed
          && service.snapshot(sequencer).active_transactions.empty(),
      "get must consume and complete without requiring item-done");

  const auto sequence_only_pair = make_pair("sequence_only");
  const auto sequence_only_request =
      service.macro_send(sequence_only_pair.sequence);
  const auto sequence_only = service.get(sequencer);
  require(
      sequence_only.transaction
          && sequence_only.transaction->request.handle == sequence_only_request
          && !sequence_only.transaction->request_item
          && sequence_only.transaction->request_object
              == service.snapshot(sequence_only_pair.sequence).object,
      "sequence requests must retain their object identity without fabricating an item");

  const auto explicit_pair = make_pair("explicit_cancel");
  (void)service.macro_send(explicit_pair.item);
  const auto explicit_transaction = service.get_next_item(sequencer);
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] {
        service.release_transaction(explicit_transaction.transaction->handle);
      },
      "active transactions must not be released");
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] {
        service.cancel_transaction(
            explicit_transaction.transaction->handle,
            static_cast<SystemVerilogUvmSequenceCancellationReason>(255));
      },
      "unknown cancellation reasons must reject without changing state");
  require(
      service.transaction_snapshot(explicit_transaction.transaction->handle)
              .state == SystemVerilogUvmSequenceTransactionState::Reserved,
      "invalid cancellation must preserve a reserved transaction");
  service.cancel_transaction(explicit_transaction.transaction->handle);
  require(
      service.transaction_snapshot(explicit_transaction.transaction->handle)
                  .cancellation
              == SystemVerilogUvmSequenceCancellationReason::Explicit,
      "explicit cancellation must retain its exact reason");

  service.configure_handshake(sequencer, 3, 1);
  const auto push_first_pair = make_pair("push_first");
  const auto push_second_pair = make_pair("push_second");
  (void)service.macro_send(push_first_pair.item);
  (void)service.macro_send(push_second_pair.item);
  const auto pushed_first = service.push_next_item(sequencer);
  require(
      pushed_first.transaction
          && pushed_first.transaction->state
              == SystemVerilogUvmSequenceTransactionState::Pushed
          && service.push_next_item(sequencer).status
              == SystemVerilogUvmSequenceAcquireStatus::Backpressured
          && service.requests(sequencer).size() == 1,
      "a full push queue must backpressure before consuming another request");
  const auto popped_first = service.try_pop_pushed_item(sequencer);
  const auto pushed_second = service.push_next_item(sequencer);
  require(
      popped_first.transaction && pushed_second.transaction
          && popped_first.transaction->state
              == SystemVerilogUvmSequenceTransactionState::Reserved
          && pushed_second.transaction->state
              == SystemVerilogUvmSequenceTransactionState::Pushed
          && service.snapshot(sequencer).active_transactions.size() == 2,
      "push-pop must permit a reserved driver item and a pipelined pushed item");
  const auto popped_second = service.try_pop_pushed_item(sequencer);
  service.item_done(popped_first.transaction->handle);
  service.item_done(popped_second.transaction->handle);
  require(
      service.try_pop_pushed_item(sequencer).status
          == SystemVerilogUvmSequenceAcquireStatus::Empty,
      "the push queue must drain exactly after both completions");

  const auto wrong_pair = make_pair("wrong_response_owner");
  const auto response_target = make_pair("response_target");
  (void)service.macro_send(response_target.item);
  const auto response_transaction = service.get_next_item(sequencer);
  const auto wrong_response = make_response(wrong_pair, "wrong");
  require_error(
      "FSIM-UVM-SEQ-005",
      [&] {
        service.item_done(response_transaction.transaction->handle, wrong_response);
      },
      "item-done must reject a response owned by another sequence");
  require(
      service.transaction_snapshot(response_transaction.transaction->handle).state
          == SystemVerilogUvmSequenceTransactionState::Reserved,
      "invalid response ownership must leave the request reserved");
  service.item_done(response_transaction.transaction->handle);

  const auto timeout_pair = make_pair("timeout");
  (void)service.macro_send(timeout_pair.item);
  const auto timed = service.get_next_item(sequencer, {{}, {}, 5});
  service.advance_handshake_time(4);
  require(
      service.transaction_snapshot(timed.transaction->handle).state
          == SystemVerilogUvmSequenceTransactionState::Reserved,
      "a transaction must remain reserved before its deadline");
  service.advance_handshake_time(5);
  require(
      service.transaction_snapshot(timed.transaction->handle).state
              == SystemVerilogUvmSequenceTransactionState::Cancelled
          && service.transaction_snapshot(timed.transaction->handle).cancellation
              == SystemVerilogUvmSequenceCancellationReason::Timeout
          && service.snapshot(timeout_pair.item).state
              == SystemVerilogUvmSequenceItemState::Cancelled,
      "deadline arrival must retain a typed timeout cancellation snapshot");
  require_error(
      "FSIM-UVM-SEQ-011", [&] { service.advance_handshake_time(4); },
      "handshake time must reject backward movement");
  const auto oversized_timeout_pair = make_pair("oversized_timeout");
  const auto oversized_timeout_request =
      service.macro_send(oversized_timeout_pair.item);
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] {
        (void)service.get_next_item(
            sequencer,
            {{}, {}, service.limits().maximum_transaction_timeout + 1U});
      },
      "transaction timeout must remain within the configured resource ceiling");
  require(
      service.requests(sequencer)
          == std::vector<SystemVerilogUvmSequenceRequestHandle>{
              oversized_timeout_request},
      "timeout-limit rejection must not consume the pending request");
  service.cancel_request(oversized_timeout_request);

  SystemVerilogUvmPhaseProcessHandle run_process;
  const auto run = schedule.phase(SystemVerilogUvmPhaseKind::Run);
  const auto run_execution = phases.execute_task_phase(
      run,
      [](const auto, const auto, const auto) {},
      [&](const auto component, const auto, const auto process) {
        if (component == service.snapshot(sequencer).component) {
          run_process = process;
        }
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  require(run_process && !run_execution.processes.empty(),
          "phase cancellation proof requires one retained running process");
  const auto process_pair = make_pair("process_cancel");
  (void)service.macro_send(process_pair.item);
  const auto process_transaction = service.get_next_item(
      sequencer, {run, run_process, 0});
  phases.cancel_task_process(run_process);
  service.synchronize_phase_transactions();
  require(
      service.transaction_snapshot(process_transaction.transaction->handle)
                  .cancellation
              == SystemVerilogUvmSequenceCancellationReason::ProcessCancelled,
      "cancelled phase processes must cancel their driver transactions");

  const auto jump_pair = make_pair("jump_cancel");
  (void)service.macro_send(jump_pair.item);
  const auto jump_transaction =
      service.get_next_item(sequencer, {run, {}, 0});
  const auto final_phase = schedule.phase(SystemVerilogUvmPhaseKind::Final);
  const auto jump = service.jump_phase(run, final_phase);
  require(jump.from == run && jump.target == final_phase,
          "phase-jump proof must execute an actual forward jump");
  require(
      service.transaction_snapshot(jump_transaction.transaction->handle)
                  .cancellation
              == SystemVerilogUvmSequenceCancellationReason::PhaseJumped,
      "phase jump must retain an exact typed transaction cancellation");

  const auto peer_pair = make_pair("peer", peer_sequencer);
  (void)service.macro_send(peer_pair.item);
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] {
        (void)service.get_next_item(peer_sequencer, {run, {}, 0});
      },
      "a phase not enrolled in the sequencer root must reject transaction binding");
  service.cancel_request(service.requests(peer_sequencer).front());

  SystemVerilogUvmSequenceTransactionHandle stop_transaction;
  SystemVerilogUvmSequenceHandle stopping_sequence;
  auto stop_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "stopping_sequence"), "stopping",
      sequencer);
  stop_descriptor.hooks.body = [&](const auto executing) {
    const auto acquired = service.get_next_item(sequencer);
    require(acquired.transaction.has_value(),
            "stopping sequence body must acquire its queued request");
    stop_transaction = acquired.transaction->handle;
    service.request_stop(executing);
  };
  stopping_sequence = service.register_sequence(std::move(stop_descriptor));
  const auto stopping_item = service.register_item(item_descriptor(
      fixture.make_object(kRequestType, "stopping_item"), "stopping_item",
      SystemVerilogUvmSequenceItemRole::Request, stopping_sequence, sequencer));
  (void)service.macro_send(stopping_item);
  const auto stopped = service.start(stopping_sequence);
  require(
      stopped.stopped
          && service.transaction_snapshot(stop_transaction).cancellation
              == SystemVerilogUvmSequenceCancellationReason::SequenceStopped,
      "cooperative sequence stop must cancel and retain its active transaction");

  SystemVerilogUvmSequenceTransactionHandle kill_transaction;
  SystemVerilogUvmSequenceHandle killed_sequence;
  auto kill_descriptor = sequence_descriptor(
      fixture.make_object(kSequenceType, "killed_sequence"), "killed",
      sequencer);
  kill_descriptor.hooks.body = [&](const auto executing) {
    const auto acquired = service.get_next_item(sequencer);
    require(acquired.transaction.has_value(),
            "killed sequence body must acquire its queued request");
    kill_transaction = acquired.transaction->handle;
    service.kill(executing);
  };
  killed_sequence = service.register_sequence(std::move(kill_descriptor));
  const auto killed_item = service.register_item(item_descriptor(
      fixture.make_object(kRequestType, "killed_item"), "killed_item",
      SystemVerilogUvmSequenceItemRole::Request, killed_sequence, sequencer));
  (void)service.macro_send(killed_item);
  const auto killed = service.start(killed_sequence);
  require(
      killed.killed
          && service.transaction_snapshot(kill_transaction).cancellation
              == SystemVerilogUvmSequenceCancellationReason::SequenceKilled,
      "recursive sequence kill must retain a distinct transaction reason");

  const auto retained = service.transactions(sequencer);
  require(retained.size() >= 12,
          "completed and cancelled transactions must remain retained");
  const auto released = first.transaction->handle;
  require_error(
      "FSIM-UVM-SEQ-005",
      [&] { service.release(irrelevant.item); },
      "request items must remain live while a transaction snapshot retains them");
  service.release_transaction(released);
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { (void)service.transaction_snapshot(released); },
      "released transaction handles must become stale");
  require_error(
      "FSIM-UVM-SEQ-001",
      [&] { service.release_transaction(SystemVerilogUvmSequenceTransactionHandle{}); },
      "empty transaction release must reject through handle validation");

  SystemVerilogUvmSequenceService foreign{
      fixture.heap, fixture.objects, fixture.components};
  require_error(
      "FSIM-UVM-SEQ-002",
      [&] { (void)foreign.transaction_snapshot(second.transaction->handle); },
      "retained transaction handles must reject across simulations");

  SystemVerilogUvmSequenceLimits retained_limits;
  retained_limits.maximum_transactions = 1;
  retained_limits.maximum_transactions_per_sequencer = 2;
  SystemVerilogUvmSequenceService bounded{
      fixture.heap, fixture.objects, fixture.components, retained_limits};
  const auto bounded_sequencer = bounded.register_sequencer(
      {fixture.make_sequencer("bounded_sequencer", fixture.first_root),
       std::string{kSequencerType}, profile()});
  const auto bounded_sequence = bounded.register_sequence(sequence_descriptor(
      fixture.make_object(kSequenceType, "bounded_sequence"), "bounded",
      bounded_sequencer));
  const auto bounded_item = bounded.register_item(item_descriptor(
      fixture.make_object(kRequestType, "bounded_item"), "bounded_item",
      SystemVerilogUvmSequenceItemRole::Request, bounded_sequence,
      bounded_sequencer));
  (void)bounded.macro_send(bounded_item);
  const auto bounded_first = bounded.get(bounded_sequencer);
  (void)bounded.macro_send(bounded_item);
  const auto pending_before_limit = bounded.requests(bounded_sequencer);
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)bounded.get(bounded_sequencer); },
      "retained transaction count must remain bounded");
  require(
      bounded.requests(bounded_sequencer) == pending_before_limit,
      "transaction-limit failure must not consume the pending request");
  bounded.release_transaction(bounded_first.transaction->handle);

  SystemVerilogUvmSequenceLimits per_sequencer_limits;
  per_sequencer_limits.maximum_transactions = 2;
  per_sequencer_limits.maximum_transactions_per_sequencer = 1;
  SystemVerilogUvmSequenceService per_sequencer_bounded{
      fixture.heap, fixture.objects, fixture.components,
      per_sequencer_limits};
  const auto per_sequencer = per_sequencer_bounded.register_sequencer(
      {fixture.make_sequencer("per_sequencer", fixture.first_root),
       std::string{kSequencerType}, profile()});
  const auto per_sequence = per_sequencer_bounded.register_sequence(
      sequence_descriptor(
          fixture.make_object(kSequenceType, "per_sequence"), "per_sequence",
          per_sequencer));
  const auto per_item = per_sequencer_bounded.register_item(item_descriptor(
      fixture.make_object(kRequestType, "per_item"), "per_item",
      SystemVerilogUvmSequenceItemRole::Request, per_sequence, per_sequencer));
  (void)per_sequencer_bounded.macro_send(per_item);
  (void)per_sequencer_bounded.get(per_sequencer);
  (void)per_sequencer_bounded.macro_send(per_item);
  const auto per_pending = per_sequencer_bounded.requests(per_sequencer);
  require_error(
      "FSIM-UVM-SEQ-006",
      [&] { (void)per_sequencer_bounded.get(per_sequencer); },
      "retained transactions per sequencer must remain independently bounded");
  require(
      per_sequencer_bounded.requests(per_sequencer) == per_pending,
      "per-sequencer transaction-limit failure must preserve the request");

  for (std::size_t invalid_limit = 0; invalid_limit != 5; ++invalid_limit) {
    auto invalid = SystemVerilogUvmSequenceLimits{};
    if (invalid_limit == 0) invalid.maximum_transactions = 0;
    if (invalid_limit == 1) invalid.maximum_transactions_per_sequencer = 0;
    if (invalid_limit == 2) invalid.maximum_in_flight_per_sequencer = 0;
    if (invalid_limit == 3) invalid.maximum_push_capacity = 0;
    if (invalid_limit == 4) invalid.maximum_transaction_timeout = 0;
    require_error(
        "FSIM-UVM-SEQ-006",
        [&] {
          SystemVerilogUvmSequenceService rejected{
              fixture.heap, fixture.objects, fixture.components, invalid};
        },
        "every driver-handshake resource ceiling must be nonzero");
  }
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] { service.configure_handshake(sequencer, 0, 1); },
      "zero in-flight handshake depth must reject");
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] { service.configure_handshake(sequencer, 1, 0); },
      "zero push capacity must reject");
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] {
        service.configure_handshake(
            sequencer,
            service.limits().maximum_in_flight_per_sequencer + 1U, 1);
      },
      "configured in-flight depth must remain bounded");
  require_error(
      "FSIM-UVM-SEQ-011",
      [&] {
        service.configure_handshake(
            sequencer, 1, service.limits().maximum_push_capacity + 1U);
      },
      "configured push capacity must remain bounded");
}

}  // namespace fsim::tests::runtime
