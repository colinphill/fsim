// SPDX-License-Identifier: Apache-2.0
#include "runtime_test_support.hpp"

#include "fsim/runtime/uvm_tlm1.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::tests::runtime {
namespace {

using namespace fsim::runtime;

void require(const bool condition, const std::string_view message) {
  if (!condition) throw std::runtime_error{std::string{message}};
}

template <typename Callback>
void require_error(
    const std::string_view code,
    Callback&& callback,
    const std::string_view message) {
  try {
    callback();
  } catch (const SystemVerilogUvmTlm1Error& error) {
    require(error.diagnostic_code() == code, message);
    return;
  }
  throw std::runtime_error{std::string{message}};
}

SystemVerilogClassDescriptor component_descriptor() {
  SystemVerilogClassDescriptor result;
  result.declared_type = "uvm_pkg::uvm_component";
  result.dynamic_type = "work::component";
  result.specialization_identity = "work::component";
  result.assignable_declared_types = {
      "work::component", "uvm_pkg::uvm_component",
      "uvm_pkg::uvm_object"};
  return result;
}

SystemVerilogClassDescriptor payload_descriptor() {
  SystemVerilogClassDescriptor result;
  result.declared_type = "work::payload";
  result.dynamic_type = "work::payload";
  result.specialization_identity = "work::payload";
  result.assignable_declared_types = {
      "work::payload", "uvm_pkg::uvm_object"};
  return result;
}

struct ExecutionFixture {
  Scheduler scheduler;
  SystemVerilogClassHeap heap{{128, 16'384}};
  SystemVerilogUvmObjectService objects;
  SystemVerilogUvmComponentService components;
  SystemVerilogUvmPhaseService phases;
  SystemVerilogUvmTlm1Service tlm1;
  SystemVerilogUvmRootHandle root;
  SystemVerilogUvmPhaseHandle run;

  ExecutionFixture()
      : objects(
            heap,
            [&](const std::string_view, const std::string_view,
                const std::string_view) {
              return heap.allocate(component_descriptor());
            }),
        components(heap, objects, {4, 64, 8, 16, 64, 256}),
        phases(components, scheduler),
        tlm1(heap, components),
        root(components.create_root("execution")) {
    SystemVerilogUvmObjectDescriptor component_type;
    component_type.specialization_identity = "work::component";
    component_type.type_name = "component";
    objects.register_type(std::move(component_type));
    const std::array roots{root};
    const auto schedule = phases.create_standard_schedule(roots);
    run = schedule.phase(SystemVerilogUvmPhaseKind::Run);
    tlm1.set_scheduler(scheduler);
    tlm1.set_phase_service(phases);
  }

  [[nodiscard]] SystemVerilogClassHandle component(
      std::string name,
      const SystemVerilogClassHandle parent = 0) {
    const auto result = heap.allocate(component_descriptor());
    objects.initialize(result);
    components.initialize(result, std::move(name), parent, root);
    return result;
  }

  void start_run() {
    const auto result = phases.execute_task_phase(
        run,
        [](const auto, const auto, const auto) {},
        [](const auto, const auto, const auto) {
          return SystemVerilogUvmTaskPhaseStatus::Suspended;
        });
    require(
        result.final_state == SystemVerilogUvmPhaseState::Executing,
        "TLM1 execution fixture run phase must remain active");
  }
};

SystemVerilogUvmTlm1Profile profile(
    const SystemVerilogUvmTlm1Interface interface_kind,
    std::string request = "work::payload",
    std::string response = {}) {
  return {
      interface_kind,
      SystemVerilogUvmTlm1Direction::Bidirectional,
      std::move(request),
      std::move(response)};
}

SystemVerilogUvmTlm1EndpointDescriptor endpoint(
    const SystemVerilogUvmTlm1EndpointKind kind,
    const SystemVerilogClassHandle component,
    std::string name,
    SystemVerilogUvmTlm1Profile endpoint_profile) {
  const bool implementation =
      kind == SystemVerilogUvmTlm1EndpointKind::Implementation;
  return {
      kind, std::move(endpoint_profile), component, std::move(name),
      implementation ? 0U : 1U, implementation ? 0U : 1U};
}

SystemVerilogUvmTlm1Payload payload(const std::uint64_t value) {
  return {
      "work::payload",
      PackedLogic4::from_aval_bval(32, value, 0),
      0, 0, {}, 0};
}

}  // namespace

void test_systemverilog_uvm_tlm1_execution() {
  ExecutionFixture fixture;
  const auto top = fixture.component("top");
  const auto producer = fixture.component("producer", top);
  const auto consumer = fixture.component("consumer", top);

  const auto fifo_profile = profile(SystemVerilogUvmTlm1Interface::Bidirectional);
  const auto fifo_port = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "fifo_port", fifo_profile));
  const auto fifo_imp = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "fifo_imp", fifo_profile));
  fixture.tlm1.connect(fifo_port, fifo_imp);

  const auto transport_profile = profile(
      SystemVerilogUvmTlm1Interface::Transport,
      "work::request", "work::response");
  const auto transport_port = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "transport_port", transport_profile));
  const auto transport_imp = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "transport_imp", transport_profile));
  fixture.tlm1.connect(transport_port, transport_imp);

  const auto blocking_profile = profile(
      SystemVerilogUvmTlm1Interface::BlockingPut);
  const auto blocking_port = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "blocking_port", blocking_profile));
  const auto blocking_imp = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "blocking_imp", blocking_profile));
  fixture.tlm1.connect(blocking_port, blocking_imp);
  const auto master_profile = profile(
      SystemVerilogUvmTlm1Interface::Master,
      "work::request", "work::response");
  const auto master_port = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "master_port", master_profile));
  const auto master_imp = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "master_imp", master_profile));
  fixture.tlm1.connect(master_port, master_imp);
  const auto slave_profile = profile(
      SystemVerilogUvmTlm1Interface::Slave,
      "work::request", "work::response");
  const auto slave_port = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "slave_port", slave_profile));
  const auto slave_imp = fixture.tlm1.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "slave_imp", slave_profile));
  fixture.tlm1.connect(slave_port, slave_imp);
  fixture.tlm1.resolve_all();
  fixture.tlm1.configure_fifo(fifo_imp, 1);
  fixture.tlm1.configure_fifo(blocking_imp, 1);
  fixture.tlm1.configure_fifo(master_imp, 2);
  fixture.tlm1.configure_fifo(slave_imp, 2);
  fixture.start_run();

  require(
      fixture.tlm1.can_put(fifo_port)
          && fixture.tlm1.try_put(fifo_port, payload(0x11))
          && !fixture.tlm1.can_put(fifo_port)
          && !fixture.tlm1.try_put(fifo_port, payload(0x22))
          && fixture.tlm1.can_get(fifo_port)
          && fixture.tlm1.can_peek(fifo_port),
      "nonblocking FIFO put and can state must be exact");
  const auto peeked = fixture.tlm1.try_peek(fifo_port);
  require(
      peeked && peeked->value.low_word().aval == 0x11
          && fixture.tlm1.fifo_snapshot(fifo_imp).size == 1,
      "try_peek must preserve the queued payload");

  std::vector<std::uint64_t> callback_order;
  const auto blocked_put = fixture.tlm1.put(
      fifo_port, payload(0x22), fixture.run,
      [&](const auto& operation) {
        callback_order.push_back(operation.reservation_order);
      });
  require(
      fixture.tlm1.operation_snapshot(blocked_put).state
          == SystemVerilogUvmTlm1OperationState::Pending
          && fixture.tlm1.fifo_snapshot(fifo_imp).pending_puts == 1,
      "blocking put must reserve the full FIFO");
  const auto first_get = fixture.tlm1.get(
      fifo_port, fixture.run,
      [&](const auto& operation) {
        callback_order.push_back(operation.reservation_order);
      });
  require(
      fixture.tlm1.operation_snapshot(first_get).response->value.low_word().aval
              == 0x11
          && fixture.tlm1.operation_snapshot(blocked_put).state
              == SystemVerilogUvmTlm1OperationState::Completed
          && fixture.tlm1.fifo_snapshot(fifo_imp).size == 1,
      "a get must free one slot and wake the oldest blocked put");
  (void)fixture.scheduler.run();
  require(
      callback_order
          == std::vector<std::uint64_t>{
              fixture.tlm1.operation_snapshot(blocked_put).reservation_order,
              fixture.tlm1.operation_snapshot(first_get).reservation_order},
      "scheduler wake callbacks must follow reservation order");

  const auto second = fixture.tlm1.try_get(fifo_port);
  require(
      second && second->value.low_word().aval == 0x22
          && second->nominal_type == "work::payload"
          && second->owner_root == fixture.root
          && second->owner_endpoint == fifo_port,
      "get must transfer exact nominal payload and endpoint ownership");

  const auto waiting_get = fixture.tlm1.get(fifo_port, fixture.run);
  const auto waiting_peek = fixture.tlm1.peek(fifo_port, fixture.run);
  require(
      fixture.tlm1.pending_operation_count() == 2,
      "empty FIFO get and peek must block independently");
  require(fixture.tlm1.try_put(fifo_port, payload(0x33)), "put must wake waiters");
  require(
      fixture.tlm1.operation_snapshot(waiting_get).state
              == SystemVerilogUvmTlm1OperationState::Completed
          && fixture.tlm1.operation_snapshot(waiting_get)
                 .response->value.low_word().aval == 0x33
          && fixture.tlm1.operation_snapshot(waiting_peek).state
              == SystemVerilogUvmTlm1OperationState::Pending,
      "reservation order must let consuming get precede a later peek");
  require(fixture.tlm1.try_put(fifo_port, payload(0x44)), "second put must wake peek");
  require(
      fixture.tlm1.operation_snapshot(waiting_peek)
              .response->value.low_word().aval == 0x44
          && fixture.tlm1.fifo_snapshot(fifo_imp).size == 1,
      "peek wake must not consume the payload");
  (void)fixture.tlm1.try_get(fifo_port);

  const auto payload_object = fixture.heap.allocate(payload_descriptor());
  auto object_payload = payload(0x55);
  object_payload.object = payload_object;
  require(
      fixture.tlm1.try_put(fifo_port, object_payload),
      "live nominally compatible object payload must be accepted");
  const auto object_result = fixture.tlm1.try_get(fifo_port);
  require(
      object_result && object_result->object == payload_object,
      "FIFO transfer must preserve object identity");
  require(fixture.heap.release(payload_object), "payload object release must succeed");
  require_error(
      "FSIM-UVM-TLM1-001",
      [&] { (void)fixture.tlm1.try_put(fifo_port, object_payload); },
      "stale payload objects must reject before FIFO mutation");

  require_error(
      "FSIM-UVM-TLM1-005",
      [&] { (void)fixture.tlm1.try_put(blocking_port, payload(1)); },
      "blocking-only interfaces must reject nonblocking calls");
  const auto blocking_put = fixture.tlm1.put(
      blocking_port, payload(0x66), fixture.run);
  require(
      fixture.tlm1.operation_snapshot(blocking_put).state
          == SystemVerilogUvmTlm1OperationState::Completed,
      "blocking-only put must execute");

  SystemVerilogUvmTlm1Payload master_request{
      "work::request", PackedLogic4::from_aval_bval(16, 0x1234, 0),
      0, 0, {}, 0};
  require(
      fixture.tlm1.try_put(master_port, master_request),
      "master put must publish a request");
  const auto master_get = fixture.tlm1.get(master_port, fixture.run);
  require(
      fixture.tlm1.operation_snapshot(master_get).state
          == SystemVerilogUvmTlm1OperationState::Pending,
      "master get must reserve a response rather than consume its request");
  const auto provider_request = fixture.tlm1.implementation_try_read(master_imp);
  require(
      provider_request && provider_request->nominal_type == "work::request"
          && provider_request->value.low_word().aval == 0x1234,
      "master provider must receive the exact request payload");
  require(fixture.tlm1.implementation_try_write(
      master_imp,
      {"work::response", PackedLogic4::from_aval_bval(16, 0x5678, 0),
       0, 0, {}, 0},
      true), "master provider response enqueue must succeed");
  require(
      fixture.tlm1.operation_snapshot(master_get).state
              == SystemVerilogUvmTlm1OperationState::Completed
          && fixture.tlm1.operation_snapshot(master_get)
                 .response->nominal_type == "work::response"
          && fixture.tlm1.operation_snapshot(master_get)
                 .response->value.low_word().aval == 0x5678,
      "master get must wake with the provider response");

  require(fixture.tlm1.implementation_try_write(
      slave_imp, master_request, false),
      "slave provider request enqueue must succeed");
  const auto slave_request = fixture.tlm1.try_get(slave_port);
  require(
      slave_request && slave_request->nominal_type == "work::request",
      "slave get must receive a provider request");
  require(fixture.tlm1.try_put(
      slave_port,
      {"work::response", PackedLogic4::from_aval_bval(8, 0xa5, 0),
       0, 0, {}, 0}),
      "slave response put must succeed");
  const auto slave_response = fixture.tlm1.implementation_try_read(slave_imp);
  require(
      slave_response && slave_response->nominal_type == "work::response"
          && slave_response->value.low_word().aval == 0xa5,
      "slave put must return an exact response to its provider");
  require_error(
      "FSIM-UVM-TLM1-005",
      [&] {
        (void)fixture.tlm1.try_put(
            fifo_port,
            {"work::other_payload", PackedLogic4(8), 0, 0, {}, 0});
      },
      "payload nominal mismatches must reject before FIFO mutation");

  SystemVerilogUvmTlm1Limits bounded_limits;
  bounded_limits.maximum_fifo_capacity = 1;
  bounded_limits.maximum_payload_bits = 8;
  bounded_limits.maximum_pending_operations = 1;
  SystemVerilogUvmTlm1Service bounded{
      fixture.heap, fixture.components, bounded_limits};
  const auto bounded_port = bounded.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Port,
      producer, "bounded_port", fifo_profile));
  const auto bounded_imp = bounded.register_endpoint(endpoint(
      SystemVerilogUvmTlm1EndpointKind::Implementation,
      consumer, "bounded_imp", fifo_profile));
  bounded.connect(bounded_port, bounded_imp);
  bounded.resolve_all();
  require_error(
      "FSIM-UVM-TLM1-006",
      [&] { bounded.configure_fifo(bounded_imp, 2); },
      "FIFO capacity ceilings must reject before configuration");
  bounded.configure_fifo(bounded_imp, 1);
  require_error(
      "FSIM-UVM-TLM1-006",
      [&] { (void)bounded.try_put(bounded_port, payload(1)); },
      "payload width ceilings must reject before enqueue");
  const auto bounded_wait = bounded.get(bounded_port);
  require_error(
      "FSIM-UVM-TLM1-006",
      [&] { (void)bounded.peek(bounded_port); },
      "pending-operation ceilings must reject without another reservation");
  bounded.cancel_operation(bounded_wait);

  fixture.tlm1.set_transport_handler(
      transport_imp,
      [](const auto& request) -> std::optional<SystemVerilogUvmTlm1Payload> {
        return SystemVerilogUvmTlm1Payload{
            "work::response", request.value, 0, 0, {}, 0};
      });
  SystemVerilogUvmTlm1Payload request{
      "work::request", PackedLogic4::from_aval_bval(16, 0xabcd, 0),
      0, 0, {}, 0};
  const auto response = fixture.tlm1.try_transport(transport_port, request);
  require(
      response && response->nominal_type == "work::response"
          && response->value.low_word().aval == 0xabcd
          && response->owner_endpoint == transport_port,
      "nonblocking transport must preserve request and response profiles");

  fixture.tlm1.set_transport_handler(
      transport_imp,
      [](const auto&) -> std::optional<SystemVerilogUvmTlm1Payload> {
        return std::nullopt;
      });
  std::optional<SystemVerilogUvmTlm1OperationHandle> delayed;
  delayed = fixture.tlm1.transport(transport_port, request, fixture.run);
  fixture.scheduler.schedule_after(
      3, SchedulerPhase::active, 0,
      [&](Scheduler&) {
        fixture.tlm1.complete_transport(
            *delayed,
            {"work::response", PackedLogic4::from_aval_bval(8, 0x5a, 0),
             0, 0, {}, 0});
      });
  const auto delayed_run = fixture.scheduler.run();
  require(
      delayed_run.time == 3
          && fixture.tlm1.operation_snapshot(*delayed).state
              == SystemVerilogUvmTlm1OperationState::Completed
          && fixture.tlm1.operation_snapshot(*delayed).completion_time == 3,
      "blocking transport must resume at exact simulated time");

  bool post_phase_callback{};
  const auto completed_before_phase_end = fixture.tlm1.put(
      fifo_port, payload(0x77), fixture.run,
      [&](const auto&) { post_phase_callback = true; });
  require(
      fixture.tlm1.operation_snapshot(completed_before_phase_end).state
          == SystemVerilogUvmTlm1OperationState::Completed,
      "available blocking put must complete immediately");
  (void)fixture.tlm1.try_get(fifo_port);
  const auto unphased = fixture.tlm1.get(fifo_port);
  const auto cancelled = fixture.tlm1.get(fifo_port, fixture.run);
  require(
      fixture.tlm1.operation_snapshot(cancelled).state
          == SystemVerilogUvmTlm1OperationState::Pending,
      "empty FIFO get must remain pending until phase cleanup");
  const auto cancelled_processes = fixture.phases.timeout_task_phase(fixture.run);
  require(
      !cancelled_processes.empty()
          && fixture.tlm1.operation_snapshot(cancelled).state
              == SystemVerilogUvmTlm1OperationState::Cancelled
          && fixture.tlm1.operation_snapshot(unphased).state
              == SystemVerilogUvmTlm1OperationState::Pending
          && fixture.tlm1.pending_operation_count() == 1,
      "phase timeout must cancel only phase-owned TLM1 waits");
  fixture.tlm1.cancel_operation(unphased);
  (void)fixture.scheduler.run();
  require(
      !post_phase_callback,
      "phase cleanup must prevent delayed completion callbacks");
}

}  // namespace fsim::tests::runtime
