// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_tlm1.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-TLM1-001"};
constexpr std::string_view kInvalidEndpoint{"FSIM-UVM-TLM1-002"};
constexpr std::string_view kInvalidExecution{"FSIM-UVM-TLM1-005"};
constexpr std::string_view kExecutionLimit{"FSIM-UVM-TLM1-006"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmTlm1Error{std::string{code}, std::string{message}};
}

}  // namespace

void SystemVerilogUvmTlm1Service::set_phase_service(
    SystemVerilogUvmPhaseService& phases) noexcept {
  if (phases_ && phases_ != &phases) phases_->clear_tlm1_service(*this);
  phases_ = &phases;
  phases.set_tlm1_service(*this);
}

void SystemVerilogUvmTlm1Service::configure_fifo(
    const SystemVerilogUvmTlm1EndpointHandle implementation,
    const std::size_t capacity) {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
      != SystemVerilogUvmTlm1EndpointKind::Implementation) {
    fail(kInvalidExecution, "a UVM TLM1 FIFO requires an implementation endpoint");
  }
  if (capacity == 0 || capacity > limits_.maximum_fifo_capacity) {
    fail(kExecutionLimit, "UVM TLM1 FIFO capacity exceeds its ceiling");
  }
  if (fifos_.contains(implementation.slot_)) {
    fail(kInvalidEndpoint, "duplicate UVM TLM1 FIFO configuration");
  }
  if (fifos_.size() >= limits_.maximum_fifos) {
    fail(kExecutionLimit, "UVM TLM1 FIFO count ceiling exceeded");
  }
  Fifo selected_fifo;
  selected_fifo.capacity = capacity;
  fifos_.emplace(implementation.slot_, std::move(selected_fifo));
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Fifo,
        SystemVerilogUvmActivityAction::Created,
        selected.debug_name,
        "capacity",
        selected.root,
        capacity});
  }
}

void SystemVerilogUvmTlm1Service::set_transport_handler(
    const SystemVerilogUvmTlm1EndpointHandle implementation,
    TransportHandler handler) {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
          != SystemVerilogUvmTlm1EndpointKind::Implementation
      || !handler) {
    fail(kInvalidExecution, "invalid UVM TLM1 transport implementation");
  }
  transport_handlers_.insert_or_assign(
      implementation.slot_, std::move(handler));
}

SystemVerilogUvmTlm1FifoSnapshot
SystemVerilogUvmTlm1Service::fifo_snapshot(
    const SystemVerilogUvmTlm1EndpointHandle implementation) const {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  const auto& selected_fifo = fifo(implementation.slot_);
  return {
      implementation,
      selected_fifo.capacity,
      selected_fifo.values.size(),
      selected_fifo.puts.size(),
      selected_fifo.gets.size(),
      selected_fifo.peeks.size()};
}

std::vector<SystemVerilogUvmTlm1FifoSnapshot>
SystemVerilogUvmTlm1Service::fifo_snapshots() const {
  std::vector<SystemVerilogUvmTlm1FifoSnapshot> result;
  result.reserve(fifos_.size());
  for (const auto& [slot, selected] : fifos_) {
    (void)selected;
    result.push_back(fifo_snapshot(handle(slot)));
  }
  return result;
}

std::vector<SystemVerilogUvmTlm1Payload>
SystemVerilogUvmTlm1Service::fifo_values(
    const SystemVerilogUvmTlm1EndpointHandle implementation) const {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  const auto& selected_fifo = fifo(implementation.slot_);
  return {selected_fifo.values.begin(), selected_fifo.values.end()};
}

bool SystemVerilogUvmTlm1Service::implementation_can_write(
    const SystemVerilogUvmTlm1EndpointHandle implementation) const {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
      != SystemVerilogUvmTlm1EndpointKind::Implementation) {
    fail(kInvalidExecution, "provider FIFO access requires an implementation endpoint");
  }
  const auto& selected_fifo = fifo(implementation.slot_);
  return selected_fifo.values.size() < selected_fifo.capacity;
}

bool SystemVerilogUvmTlm1Service::implementation_try_write(
    const SystemVerilogUvmTlm1EndpointHandle implementation,
    SystemVerilogUvmTlm1Payload payload,
    const bool response) {
  auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
      != SystemVerilogUvmTlm1EndpointKind::Implementation) {
    fail(kInvalidExecution, "provider FIFO write requires an implementation endpoint");
  }
  auto& selected_fifo = fifo(implementation.slot_);
  if (selected_fifo.values.size() >= selected_fifo.capacity) return false;
  if (queued_payload_count() >= limits_.maximum_queued_payloads) {
    fail(kExecutionLimit, "UVM TLM1 queued-payload ceiling exceeded");
  }
  validate_payload(payload, selected, response);
  record_execution();
  selected_fifo.values.push_back(std::move(payload));
  publish_fifo_activity(implementation.slot_, "enqueue");
  pump_fifo(implementation.slot_);
  return true;
}

std::optional<SystemVerilogUvmTlm1Payload>
SystemVerilogUvmTlm1Service::implementation_try_read(
    const SystemVerilogUvmTlm1EndpointHandle implementation,
    const bool peek) {
  const auto& selected = endpoint(implementation);
  require_live(selected);
  if (selected.descriptor.kind
      != SystemVerilogUvmTlm1EndpointKind::Implementation) {
    fail(kInvalidExecution, "provider FIFO read requires an implementation endpoint");
  }
  auto& selected_fifo = fifo(implementation.slot_);
  if (selected_fifo.values.empty()) return std::nullopt;
  record_execution();
  auto result = selected_fifo.values.front();
  if (!peek) {
    selected_fifo.values.pop_front();
    publish_fifo_activity(implementation.slot_, "dequeue");
    pump_fifo(implementation.slot_);
  } else {
    publish_fifo_activity(implementation.slot_, "peek");
  }
  result.owner_endpoint = implementation;
  return result;
}

bool SystemVerilogUvmTlm1Service::can_put(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const std::size_t binding_index) const {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Put, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Put, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface has no can_put operation");
  }
  const auto& selected = fifo(implementation);
  return selected.puts.empty()
      && selected.values.size() < selected.capacity;
}

bool SystemVerilogUvmTlm1Service::try_put(
    const SystemVerilogUvmTlm1EndpointHandle source,
    SystemVerilogUvmTlm1Payload payload,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Put, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Put, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support try_put");
  }
  auto& selected = fifo(implementation);
  if (!selected.puts.empty() || selected.values.size() >= selected.capacity) {
    return false;
  }
  validate_payload(
      payload, endpoints_.at(implementation),
      operation_uses_response(
          endpoints_.at(implementation),
          SystemVerilogUvmTlm1OperationKind::Put));
  if (queued_payload_count() >= limits_.maximum_queued_payloads) {
    fail(kExecutionLimit, "UVM TLM1 queued-payload ceiling exceeded");
  }
  record_execution();
  selected.values.push_back(std::move(payload));
  publish_fifo_activity(implementation, "enqueue");
  pump_fifo(implementation);
  return true;
}

bool SystemVerilogUvmTlm1Service::can_get(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const std::size_t binding_index) const {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Get, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Get, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface has no can_get operation");
  }
  const auto& selected = fifo(implementation);
  return selected.gets.empty() && !selected.values.empty()
      && payload_type_matches(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Get));
}

std::optional<SystemVerilogUvmTlm1Payload>
SystemVerilogUvmTlm1Service::try_get(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Get, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Get, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support try_get");
  }
  auto& selected = fifo(implementation);
  if (!selected.gets.empty() || selected.values.empty()
      || !payload_type_matches(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Get))) return std::nullopt;
  record_execution();
  auto result = std::move(selected.values.front());
  selected.values.pop_front();
  publish_fifo_activity(implementation, "dequeue");
  result.owner_endpoint = source;
  pump_fifo(implementation);
  return result;
}

bool SystemVerilogUvmTlm1Service::can_peek(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const std::size_t binding_index) const {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Peek, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Peek, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface has no can_peek operation");
  }
  const auto& selected = fifo(implementation);
  return selected.peeks.empty() && !selected.values.empty()
      && payload_type_matches(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Peek));
}

std::optional<SystemVerilogUvmTlm1Payload>
SystemVerilogUvmTlm1Service::try_peek(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Peek, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Peek, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support try_peek");
  }
  const auto& selected = fifo(implementation);
  if (!selected.peeks.empty() || selected.values.empty()
      || !payload_type_matches(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Peek))) return std::nullopt;
  record_execution();
  auto result = selected.values.front();
  publish_fifo_activity(implementation, "peek");
  result.owner_endpoint = source;
  return result;
}

bool SystemVerilogUvmTlm1Service::can_transport(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const std::size_t binding_index) const {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Transport, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Transport, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface has no can_transport operation");
  }
  return transport_handlers_.contains(implementation);
}

std::optional<SystemVerilogUvmTlm1Payload>
SystemVerilogUvmTlm1Service::try_transport(
    const SystemVerilogUvmTlm1EndpointHandle source,
    SystemVerilogUvmTlm1Payload request,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Transport, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Transport, false)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support try_transport");
  }
  const auto found = transport_handlers_.find(implementation);
  if (found == transport_handlers_.end()) return std::nullopt;
  validate_payload(request, endpoints_.at(implementation), false);
  record_execution();
  auto response = found->second(request);
  if (!response) return std::nullopt;
  validate_payload(*response, endpoints_.at(implementation), true);
  response->owner_endpoint = source;
  return response;
}

SystemVerilogUvmTlm1OperationHandle SystemVerilogUvmTlm1Service::put(
    const SystemVerilogUvmTlm1EndpointHandle source,
    SystemVerilogUvmTlm1Payload payload,
    const SystemVerilogUvmPhaseHandle phase,
    CompletionCallback completion,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Put, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Put, true)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support blocking put");
  }
  validate_payload(
      payload, endpoints_.at(implementation),
      operation_uses_response(
          endpoints_.at(implementation),
          SystemVerilogUvmTlm1OperationKind::Put));
  if (queued_payload_count() >= limits_.maximum_queued_payloads) {
    fail(kExecutionLimit, "UVM TLM1 queued-payload ceiling exceeded");
  }
  const auto result = create_operation(
      SystemVerilogUvmTlm1OperationKind::Put, source, implementation,
      phase, payload, std::move(completion));
  auto& selected = fifo(implementation);
  if (selected.puts.empty() && selected.values.size() < selected.capacity) {
    selected.values.push_back(std::move(payload));
    publish_fifo_activity(implementation, "enqueue");
    complete_operation(result.slot_);
  } else {
    selected.puts.push_back(result.slot_);
  }
  pump_fifo(implementation);
  return result;
}

SystemVerilogUvmTlm1OperationHandle SystemVerilogUvmTlm1Service::get(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const SystemVerilogUvmPhaseHandle phase,
    CompletionCallback completion,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Get, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Get, true)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support blocking get");
  }
  auto& selected = fifo(implementation);
  const bool immediately_available = !selected.values.empty()
      && payload_type_matches(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Get));
  const auto result = create_operation(
      SystemVerilogUvmTlm1OperationKind::Get, source, implementation,
      phase, std::nullopt, std::move(completion));
  if (selected.gets.empty() && immediately_available) {
    auto response = std::move(selected.values.front());
    selected.values.pop_front();
    publish_fifo_activity(implementation, "dequeue");
    response.owner_endpoint = source;
    complete_operation(result.slot_, std::move(response));
  } else {
    selected.gets.push_back(result.slot_);
  }
  pump_fifo(implementation);
  return result;
}

SystemVerilogUvmTlm1OperationHandle SystemVerilogUvmTlm1Service::peek(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const SystemVerilogUvmPhaseHandle phase,
    CompletionCallback completion,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Peek, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Peek, true)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support blocking peek");
  }
  auto& selected = fifo(implementation);
  const bool immediately_available = !selected.values.empty()
      && payload_type_matches(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Peek));
  const auto result = create_operation(
      SystemVerilogUvmTlm1OperationKind::Peek, source, implementation,
      phase, std::nullopt, std::move(completion));
  if (selected.peeks.empty() && immediately_available) {
    auto response = selected.values.front();
    publish_fifo_activity(implementation, "peek");
    response.owner_endpoint = source;
    complete_operation(result.slot_, std::move(response));
  } else {
    selected.peeks.push_back(result.slot_);
  }
  pump_fifo(implementation);
  return result;
}

SystemVerilogUvmTlm1OperationHandle SystemVerilogUvmTlm1Service::transport(
    const SystemVerilogUvmTlm1EndpointHandle source,
    SystemVerilogUvmTlm1Payload request,
    const SystemVerilogUvmPhaseHandle phase,
    CompletionCallback completion,
    const std::size_t binding_index) {
  const auto implementation = implementation_slot(
      source, SystemVerilogUvmTlm1OperationKind::Transport, binding_index);
  if (!supports(
          endpoint(source).descriptor.profile.interface_kind,
          SystemVerilogUvmTlm1OperationKind::Transport, true)) {
    fail(kInvalidExecution, "UVM TLM1 interface does not support blocking transport");
  }
  validate_payload(request, endpoints_.at(implementation), false);
  const auto result = create_operation(
      SystemVerilogUvmTlm1OperationKind::Transport, source, implementation,
      phase, request, std::move(completion));
  const auto found = transport_handlers_.find(implementation);
  if (found != transport_handlers_.end()) {
    auto response = found->second(request);
    if (response) {
      validate_payload(*response, endpoints_.at(implementation), true);
      response->owner_endpoint = source;
      complete_operation(result.slot_, std::move(*response));
    }
  }
  return result;
}

void SystemVerilogUvmTlm1Service::complete_transport(
    const SystemVerilogUvmTlm1OperationHandle operation_handle,
    SystemVerilogUvmTlm1Payload response) {
  auto& selected = operation(operation_handle);
  if (selected.value.state != SystemVerilogUvmTlm1OperationState::Pending
      || selected.value.kind != SystemVerilogUvmTlm1OperationKind::Transport) {
    fail(kInvalidExecution, "UVM TLM1 transport operation is not pending");
  }
  const auto implementation = selected.value.implementation.slot_;
  validate_payload(response, endpoints_.at(implementation), true);
  response.owner_endpoint = selected.value.source;
  complete_operation(operation_handle.slot_, std::move(response));
}

SystemVerilogUvmTlm1OperationSnapshot
SystemVerilogUvmTlm1Service::operation_snapshot(
    const SystemVerilogUvmTlm1OperationHandle operation_handle) const {
  return operation(operation_handle).value;
}

std::vector<SystemVerilogUvmTlm1OperationHandle>
SystemVerilogUvmTlm1Service::operations() const {
  std::vector<SystemVerilogUvmTlm1OperationHandle> result;
  result.reserve(operations_.size());
  for (const auto& [slot, selected] : operations_) {
    (void)selected;
    result.push_back(operation_handle(slot));
  }
  return result;
}

void SystemVerilogUvmTlm1Service::cancel_operation(
    const SystemVerilogUvmTlm1OperationHandle operation_handle) {
  auto& selected = operation(operation_handle);
  if (selected.value.state != SystemVerilogUvmTlm1OperationState::Pending) {
    fail(kInvalidExecution, "UVM TLM1 operation is not pending");
  }
  erase_waiter(operation_handle.slot_);
  if (scheduler_) scheduler_->cancel(selected.callback_task);
  selected.value.state = SystemVerilogUvmTlm1OperationState::Cancelled;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Transaction,
        SystemVerilogUvmActivityAction::Cancelled,
        endpoint(selected.value.source).debug_name,
        "tlm1",
        endpoint(selected.value.source).root,
        selected.value.reservation_order});
  }
}

void SystemVerilogUvmTlm1Service::cancel_phase(
    const SystemVerilogUvmPhaseHandle phase) noexcept {
  for (auto& [slot, selected] : operations_) {
    if (selected.value.phase == phase) {
      if (scheduler_) scheduler_->cancel(selected.callback_task);
      if (selected.value.state == SystemVerilogUvmTlm1OperationState::Pending) {
        erase_waiter(slot);
        selected.value.state = SystemVerilogUvmTlm1OperationState::Cancelled;
      }
    }
  }
}

void SystemVerilogUvmTlm1Service::cancel_root(
    const SystemVerilogUvmRootHandle root) noexcept {
  for (auto& [slot, selected] : operations_) {
    const auto endpoint_found = endpoints_.find(
        selected.value.implementation.slot_);
    if (endpoint_found != endpoints_.end()
        && endpoint_found->second.root == root) {
      if (scheduler_) scheduler_->cancel(selected.callback_task);
      if (selected.value.state == SystemVerilogUvmTlm1OperationState::Pending) {
        erase_waiter(slot);
        selected.value.state = SystemVerilogUvmTlm1OperationState::Cancelled;
      }
    }
  }
}

void SystemVerilogUvmTlm1Service::cancel_all() noexcept {
  for (auto& [slot, selected] : operations_) {
    (void)slot;
    if (scheduler_) scheduler_->cancel(selected.callback_task);
    if (selected.value.state == SystemVerilogUvmTlm1OperationState::Pending) {
      selected.value.state = SystemVerilogUvmTlm1OperationState::Cancelled;
    }
  }
  for (auto& [slot, selected] : fifos_) {
    (void)slot;
    selected.puts.clear();
    selected.gets.clear();
    selected.peeks.clear();
  }
}

std::size_t SystemVerilogUvmTlm1Service::pending_operation_count()
    const noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      operations_, [](const auto& entry) {
        return entry.second.value.state
            == SystemVerilogUvmTlm1OperationState::Pending;
      }));
}

std::uint64_t SystemVerilogUvmTlm1Service::implementation_slot(
    const SystemVerilogUvmTlm1EndpointHandle source,
    const SystemVerilogUvmTlm1OperationKind operation,
    const std::size_t binding_index) const {
  const auto& selected = endpoint(source);
  require_live(selected);
  if (!binding_valid()) {
    fail(kInvalidExecution, "UVM TLM1 execution requires a stable binding");
  }
  if (!supports(selected.descriptor.profile.interface_kind, operation, true)
      && !supports(selected.descriptor.profile.interface_kind, operation, false)) {
    fail(kInvalidExecution, "operation is unsupported by the UVM TLM1 interface");
  }
  if (binding_index >= selected.resolved.size()) {
    fail(kInvalidExecution, "UVM TLM1 binding index is out of range");
  }
  const auto implementation = selected.resolved[binding_index].slot_;
  require_live(endpoints_.at(implementation));
  return implementation;
}

bool SystemVerilogUvmTlm1Service::supports(
    const SystemVerilogUvmTlm1Interface interface_kind,
    const SystemVerilogUvmTlm1OperationKind operation,
    const bool blocking) const noexcept {
  if (interface_kind == SystemVerilogUvmTlm1Interface::Master
      || interface_kind == SystemVerilogUvmTlm1Interface::Slave
      || interface_kind == SystemVerilogUvmTlm1Interface::Bidirectional) {
    return true;
  }
  switch (operation) {
    case SystemVerilogUvmTlm1OperationKind::Put:
      return interface_kind == SystemVerilogUvmTlm1Interface::Put
          || (blocking
              ? interface_kind == SystemVerilogUvmTlm1Interface::BlockingPut
              : interface_kind
                  == SystemVerilogUvmTlm1Interface::NonblockingPut);
    case SystemVerilogUvmTlm1OperationKind::Get:
      return interface_kind == SystemVerilogUvmTlm1Interface::Get
          || (blocking
              ? interface_kind == SystemVerilogUvmTlm1Interface::BlockingGet
              : interface_kind
                  == SystemVerilogUvmTlm1Interface::NonblockingGet);
    case SystemVerilogUvmTlm1OperationKind::Peek:
      return interface_kind == SystemVerilogUvmTlm1Interface::Peek
          || (blocking
              ? interface_kind == SystemVerilogUvmTlm1Interface::BlockingPeek
              : interface_kind
                  == SystemVerilogUvmTlm1Interface::NonblockingPeek);
    case SystemVerilogUvmTlm1OperationKind::Transport:
      return interface_kind == SystemVerilogUvmTlm1Interface::Transport
          || (blocking
              ? interface_kind
                  == SystemVerilogUvmTlm1Interface::BlockingTransport
              : interface_kind
                  == SystemVerilogUvmTlm1Interface::NonblockingTransport);
  }
  return false;
}

bool SystemVerilogUvmTlm1Service::operation_uses_response(
    const Endpoint& implementation,
    const SystemVerilogUvmTlm1OperationKind operation) const noexcept {
  const auto interface_kind = implementation.descriptor.profile.interface_kind;
  if (operation == SystemVerilogUvmTlm1OperationKind::Put) {
    return interface_kind == SystemVerilogUvmTlm1Interface::Slave;
  }
  if (operation == SystemVerilogUvmTlm1OperationKind::Get
      || operation == SystemVerilogUvmTlm1OperationKind::Peek) {
    return interface_kind == SystemVerilogUvmTlm1Interface::Master
        || interface_kind == SystemVerilogUvmTlm1Interface::Bidirectional;
  }
  return false;
}

void SystemVerilogUvmTlm1Service::validate_payload(
    SystemVerilogUvmTlm1Payload& payload,
    const Endpoint& implementation,
    const bool response) {
  require_payload_type(payload, implementation, response);
  const auto& profile = implementation.descriptor.profile;
  const std::string_view expected = response && !profile.response_type.empty()
      ? std::string_view{profile.response_type}
      : std::string_view{profile.request_type};
  if (payload.nominal_type.size() > limits_.maximum_payload_type_bytes
      || payload.value.width() > limits_.maximum_payload_bits) {
    fail(kExecutionLimit, "UVM TLM1 payload exceeds its storage ceiling");
  }
  if (payload.object != 0) {
    if (!heap_ || !heap_->contains(payload.object)) {
      fail(kInvalidHandle, "UVM TLM1 payload object is empty or stale");
    }
    try {
      (void)heap_->checked_cast(payload.object, expected);
    } catch (const std::exception&) {
      fail(kInvalidExecution, "UVM TLM1 payload object has the wrong nominal type");
    }
  }
  if (next_payload_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kExecutionLimit, "UVM TLM1 payload sequence exhausted");
  }
  payload.owner_root = implementation.root;
  payload.owner_endpoint = handle(implementation.slot);
  payload.sequence = next_payload_sequence_++;
}

void SystemVerilogUvmTlm1Service::require_payload_type(
    const SystemVerilogUvmTlm1Payload& payload,
    const Endpoint& implementation,
    const bool response) const {
  if (!payload_type_matches(payload, implementation, response)) {
    fail(kInvalidExecution, "UVM TLM1 payload nominal type mismatches its profile");
  }
  if (payload.nominal_type.size() > limits_.maximum_payload_type_bytes
      || payload.value.width() > limits_.maximum_payload_bits) {
    fail(kExecutionLimit, "UVM TLM1 payload exceeds its storage ceiling");
  }
  if (payload.object != 0) {
    if (!heap_ || !heap_->contains(payload.object)) {
      fail(kInvalidHandle, "UVM TLM1 payload object is empty or stale");
    }
    const auto& profile = implementation.descriptor.profile;
    const std::string_view expected = response && !profile.response_type.empty()
        ? std::string_view{profile.response_type}
        : std::string_view{profile.request_type};
    try {
      (void)heap_->checked_cast(payload.object, expected);
    } catch (const std::exception&) {
      fail(kInvalidExecution, "UVM TLM1 payload object has the wrong nominal type");
    }
  }
}

bool SystemVerilogUvmTlm1Service::payload_type_matches(
    const SystemVerilogUvmTlm1Payload& payload,
    const Endpoint& implementation,
    const bool response) const noexcept {
  const auto& profile = implementation.descriptor.profile;
  const std::string_view expected = response && !profile.response_type.empty()
      ? std::string_view{profile.response_type}
      : std::string_view{profile.request_type};
  return payload.nominal_type == expected;
}

SystemVerilogUvmTlm1Service::Fifo& SystemVerilogUvmTlm1Service::fifo(
    const std::uint64_t implementation) {
  return const_cast<Fifo&>(std::as_const(*this).fifo(implementation));
}

const SystemVerilogUvmTlm1Service::Fifo&
SystemVerilogUvmTlm1Service::fifo(const std::uint64_t implementation) const {
  const auto found = fifos_.find(implementation);
  if (found == fifos_.end()) {
    fail(kInvalidExecution, "UVM TLM1 implementation has no FIFO");
  }
  return found->second;
}

void SystemVerilogUvmTlm1Service::publish_fifo_activity(
    const std::uint64_t implementation,
    const std::string_view detail) {
  if (!activity_) return;
  const auto& selected = endpoints_.at(implementation);
  activity_->publish({
      SystemVerilogUvmActivityKind::Fifo,
      SystemVerilogUvmActivityAction::Updated,
      selected.debug_name,
      std::string{detail},
      selected.root,
      fifo(implementation).values.size()});
}

SystemVerilogUvmTlm1OperationHandle
SystemVerilogUvmTlm1Service::create_operation(
    const SystemVerilogUvmTlm1OperationKind kind,
    const SystemVerilogUvmTlm1EndpointHandle source,
    const std::uint64_t implementation,
    const SystemVerilogUvmPhaseHandle phase,
    std::optional<SystemVerilogUvmTlm1Payload> request,
    CompletionCallback completion) {
  if (pending_operation_count() >= limits_.maximum_pending_operations
      || execution_operations_ >= limits_.maximum_execution_operations
      || next_operation_ == 0
      || next_reservation_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kExecutionLimit, "UVM TLM1 pending or execution-operation ceiling exceeded");
  }
  if (phase) {
    if (!phases_ || !phases_->contains(phase)) {
      fail(kInvalidHandle, "UVM TLM1 operation phase is stale or foreign");
    }
    const auto state = phases_->snapshot(phase).state;
    if (state != SystemVerilogUvmPhaseState::Executing
        && state != SystemVerilogUvmPhaseState::ReadyToEnd) {
      fail(kInvalidExecution, "blocking UVM TLM1 operation requires an active task phase");
    }
  }
  const auto slot = next_operation_++;
  const auto handle_value = SystemVerilogUvmTlm1OperationHandle{owner_, slot, 1};
  SystemVerilogUvmTlm1OperationSnapshot snapshot;
  snapshot.handle = handle_value;
  snapshot.kind = kind;
  snapshot.source = source;
  snapshot.implementation = handle(implementation);
  snapshot.phase = phase;
  snapshot.request = std::move(request);
  snapshot.reservation_order = next_reservation_order_++;
  Operation selected;
  selected.value = std::move(snapshot);
  selected.completion = std::move(completion);
  operations_.emplace(slot, std::move(selected));
  record_execution();
  if (activity_) {
    const auto& endpoint_value = endpoint(source);
    activity_->publish({
        SystemVerilogUvmActivityKind::Transaction,
        SystemVerilogUvmActivityAction::Started,
        endpoint_value.debug_name,
        "tlm1",
        endpoint_value.root,
        operations_.at(slot).value.reservation_order});
  }
  return handle_value;
}

SystemVerilogUvmTlm1OperationHandle
SystemVerilogUvmTlm1Service::operation_handle(const std::uint64_t slot) const {
  return SystemVerilogUvmTlm1OperationHandle{
      owner_, slot, operations_.at(slot).generation};
}

SystemVerilogUvmTlm1Service::Operation&
SystemVerilogUvmTlm1Service::operation(
    const SystemVerilogUvmTlm1OperationHandle operation_handle) {
  return const_cast<Operation&>(
      std::as_const(*this).operation(operation_handle));
}

const SystemVerilogUvmTlm1Service::Operation&
SystemVerilogUvmTlm1Service::operation(
    const SystemVerilogUvmTlm1OperationHandle operation_handle) const {
  if (!operation_handle.valid()
      || operation_handle.owner_.get() != owner_.get()) {
    fail(kInvalidHandle, "UVM TLM1 operation handle is empty or foreign");
  }
  const auto found = operations_.find(operation_handle.slot_);
  if (found == operations_.end()
      || found->second.generation != operation_handle.generation_) {
    fail(kInvalidHandle, "UVM TLM1 operation handle is stale");
  }
  return found->second;
}

void SystemVerilogUvmTlm1Service::complete_operation(
    const std::uint64_t slot,
    std::optional<SystemVerilogUvmTlm1Payload> response) {
  auto& selected = operations_.at(slot);
  if (selected.value.state != SystemVerilogUvmTlm1OperationState::Pending) {
    return;
  }
  selected.value.response = std::move(response);
  selected.value.state = SystemVerilogUvmTlm1OperationState::Completed;
  selected.value.completion_time = scheduler_ ? scheduler_->now() : 0;
  if (activity_) {
    const auto& endpoint_value = endpoint(selected.value.source);
    activity_->publish({
        SystemVerilogUvmActivityKind::Transaction,
        SystemVerilogUvmActivityAction::Completed,
        endpoint_value.debug_name,
        "tlm1",
        endpoint_value.root,
        selected.value.reservation_order});
  }
  if (!selected.completion) return;
  const auto operation_handle_value = operation_handle(slot);
  if (!scheduler_) {
    try {
      selected.completion(selected.value);
    } catch (...) {
    }
    return;
  }
  selected.callback_task = scheduler_->schedule_after_cancelable(
      0, SchedulerPhase::reactive, selected.value.reservation_order,
      [this, operation_handle_value](Scheduler&) {
        if (operation_handle_value.owner_.get() != owner_.get()) return;
        const auto found = operations_.find(operation_handle_value.slot_);
        if (found == operations_.end()
            || found->second.value.state
                != SystemVerilogUvmTlm1OperationState::Completed
            || !found->second.completion) return;
        try {
          found->second.completion(found->second.value);
        } catch (...) {
        }
      });
}

void SystemVerilogUvmTlm1Service::pump_fifo(
    const std::uint64_t implementation) {
  auto& selected = fifo(implementation);
  for (;;) {
    std::uint64_t candidate{};
    const auto consider = [&](const std::deque<std::uint64_t>& queue,
                              const bool feasible) {
      if (!feasible || queue.empty()) return;
      const auto slot = queue.front();
      if (candidate == 0
          || operations_.at(slot).value.reservation_order
              < operations_.at(candidate).value.reservation_order) {
        candidate = slot;
      }
    };
    const auto waiter_ready = [&](const std::deque<std::uint64_t>& queue) {
      if (queue.empty() || selected.values.empty()) return false;
      const auto kind = operations_.at(queue.front()).value.kind;
      return payload_type_matches(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(endpoints_.at(implementation), kind));
    };
    consider(selected.gets, waiter_ready(selected.gets));
    consider(selected.peeks, waiter_ready(selected.peeks));
    consider(selected.puts, selected.values.size() < selected.capacity);
    if (candidate == 0) break;
    auto& operation_value = operations_.at(candidate).value;
    if (operation_value.kind == SystemVerilogUvmTlm1OperationKind::Put) {
      selected.puts.pop_front();
      selected.values.push_back(*operation_value.request);
      publish_fifo_activity(implementation, "enqueue");
      complete_operation(candidate);
    } else if (operation_value.kind
               == SystemVerilogUvmTlm1OperationKind::Get) {
      require_payload_type(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Get));
      selected.gets.pop_front();
      auto response = std::move(selected.values.front());
      selected.values.pop_front();
      publish_fifo_activity(implementation, "dequeue");
      response.owner_endpoint = operation_value.source;
      complete_operation(candidate, std::move(response));
    } else {
      require_payload_type(
          selected.values.front(), endpoints_.at(implementation),
          operation_uses_response(
              endpoints_.at(implementation),
              SystemVerilogUvmTlm1OperationKind::Peek));
      selected.peeks.pop_front();
      auto response = selected.values.front();
      publish_fifo_activity(implementation, "peek");
      response.owner_endpoint = operation_value.source;
      complete_operation(candidate, std::move(response));
    }
  }
}

void SystemVerilogUvmTlm1Service::erase_waiter(
    const std::uint64_t operation_slot) noexcept {
  for (auto& [implementation, selected] : fifos_) {
    (void)implementation;
    const auto erase = [&](std::deque<std::uint64_t>& queue) {
      const auto found = std::ranges::find(queue, operation_slot);
      if (found != queue.end()) queue.erase(found);
    };
    erase(selected.puts);
    erase(selected.gets);
    erase(selected.peeks);
  }
}

void SystemVerilogUvmTlm1Service::record_execution() {
  if (execution_operations_ >= limits_.maximum_execution_operations) {
    fail(kExecutionLimit, "UVM TLM1 execution-operation ceiling exceeded");
  }
  ++execution_operations_;
}

std::size_t SystemVerilogUvmTlm1Service::queued_payload_count()
    const noexcept {
  std::size_t result{};
  for (const auto& [slot, selected] : fifos_) {
    (void)slot;
    result += selected.values.size() + selected.puts.size();
  }
  return result;
}

}  // namespace fsim::runtime
