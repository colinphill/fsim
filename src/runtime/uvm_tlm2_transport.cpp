// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_tlm2.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-TLM2-001"};
constexpr std::string_view kInvalidSocket{"FSIM-UVM-TLM2-002"};
constexpr std::string_view kInvalidPayload{"FSIM-UVM-TLM2-003"};
constexpr std::string_view kInvalidProtocol{"FSIM-UVM-TLM2-004"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-TLM2-005"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmTlm2Error{std::string{code}, std::string{message}};
}

[[nodiscard]] std::uint8_t phase_rank(
    const SystemVerilogUvmTlm2PhaseKind phase) noexcept {
  switch (phase) {
    case SystemVerilogUvmTlm2PhaseKind::BeginRequest: return 0;
    case SystemVerilogUvmTlm2PhaseKind::EndRequest: return 1;
    case SystemVerilogUvmTlm2PhaseKind::BeginResponse: return 2;
    case SystemVerilogUvmTlm2PhaseKind::EndResponse: return 3;
    case SystemVerilogUvmTlm2PhaseKind::Custom: return 4;
  }
  return 0;
}

}  // namespace

void SystemVerilogUvmTlm2Service::set_blocking_handler(
    const SystemVerilogUvmTlm2SocketHandle target,
    BlockingHandler handler) {
  const auto& selected = socket(target);
  require_live(selected);
  if (selected.descriptor.kind != SystemVerilogUvmTlm2SocketKind::Target
      || (selected.descriptor.profile.protocol
              != SystemVerilogUvmTlm2Protocol::Blocking
          && selected.descriptor.profile.protocol
              != SystemVerilogUvmTlm2Protocol::Combined)
      || !handler) {
    fail(kInvalidProtocol, "invalid UVM TLM2 blocking target handler");
  }
  blocking_handlers_.insert_or_assign(target.slot_, std::move(handler));
}

void SystemVerilogUvmTlm2Service::set_forward_handler(
    const SystemVerilogUvmTlm2SocketHandle target,
    NonblockingHandler handler) {
  const auto& selected = socket(target);
  require_live(selected);
  if (selected.descriptor.kind != SystemVerilogUvmTlm2SocketKind::Target
      || (selected.descriptor.profile.protocol
              != SystemVerilogUvmTlm2Protocol::Nonblocking
          && selected.descriptor.profile.protocol
              != SystemVerilogUvmTlm2Protocol::Combined)
      || !handler) {
    fail(kInvalidProtocol, "invalid UVM TLM2 nonblocking target handler");
  }
  forward_handlers_.insert_or_assign(target.slot_, std::move(handler));
}

void SystemVerilogUvmTlm2Service::set_backward_handler(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    NonblockingHandler handler) {
  const auto& selected = socket(initiator);
  require_live(selected);
  if (selected.descriptor.kind == SystemVerilogUvmTlm2SocketKind::Target
      || (selected.descriptor.profile.protocol
              != SystemVerilogUvmTlm2Protocol::Nonblocking
          && selected.descriptor.profile.protocol
              != SystemVerilogUvmTlm2Protocol::Combined)
      || !handler) {
    fail(kInvalidProtocol, "invalid UVM TLM2 backward initiator handler");
  }
  backward_handlers_.insert_or_assign(initiator.slot_, std::move(handler));
}

void SystemVerilogUvmTlm2Service::set_debug_handler(
    const SystemVerilogUvmTlm2SocketHandle target,
    DebugHandler handler) {
  const auto& selected = socket(target);
  require_live(selected);
  if (selected.descriptor.kind != SystemVerilogUvmTlm2SocketKind::Target
      || !handler) {
    fail(kInvalidProtocol, "invalid UVM TLM2 debug target handler");
  }
  debug_handlers_.insert_or_assign(target.slot_, std::move(handler));
}

void SystemVerilogUvmTlm2Service::set_dmi_handler(
    const SystemVerilogUvmTlm2SocketHandle target,
    DmiHandler handler) {
  const auto& selected = socket(target);
  require_live(selected);
  if (selected.descriptor.kind != SystemVerilogUvmTlm2SocketKind::Target
      || !handler) {
    fail(kInvalidProtocol, "invalid UVM TLM2 direct-memory target handler");
  }
  dmi_handlers_.insert_or_assign(target.slot_, std::move(handler));
}

void SystemVerilogUvmTlm2Service::set_dmi_invalidation_handler(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    DmiInvalidationHandler handler) {
  const auto& selected = socket(initiator);
  require_live(selected);
  if (selected.descriptor.kind == SystemVerilogUvmTlm2SocketKind::Target
      || !handler) {
    fail(kInvalidProtocol, "invalid UVM TLM2 direct-memory invalidation handler");
  }
  dmi_invalidation_handlers_.insert_or_assign(
      initiator.slot_, std::move(handler));
}

SystemVerilogUvmTlm2BlockingResult
SystemVerilogUvmTlm2Service::b_transport(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    SystemVerilogUvmTlm2GenericPayload payload,
    SimulationTick delay,
    const std::size_t binding_index) {
  const auto target = target_slot(
      initiator, SystemVerilogUvmTlm2Protocol::Blocking, binding_index);
  const auto found = blocking_handlers_.find(target);
  if (found == blocking_handlers_.end()) {
    fail(kInvalidProtocol, "UVM TLM2 target has no blocking handler");
  }
  validate_payload(payload, socket(initiator), true);
  reserve_callback();
  try {
    found->second(payload, delay);
  } catch (const std::exception& error) {
    fail(kInvalidProtocol, error.what());
  } catch (...) {
    fail(kInvalidProtocol, "UVM TLM2 blocking handler threw");
  }
  validate_payload(payload, socket(initiator), false);
  if (payload.response_status
      == SystemVerilogUvmTlm2ResponseStatus::Incomplete) {
    fail(kInvalidProtocol, "UVM TLM2 blocking transport left response incomplete");
  }
  const auto now = scheduler_ ? scheduler_->now() : 0;
  if (delay > std::numeric_limits<SimulationTick>::max() - now) {
    fail(kResourceLimit, "UVM TLM2 blocking delay overflows simulation time");
  }
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Transaction,
        SystemVerilogUvmActivityAction::Completed,
        socket(initiator).debug_name,
        "tlm2-blocking",
        socket(initiator).root,
        payload.transaction_id});
  }
  return {socket_handle(target), std::move(payload), delay, now + delay};
}

SystemVerilogUvmTlm2NonblockingResult
SystemVerilogUvmTlm2Service::nb_transport_fw(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    SystemVerilogUvmTlm2GenericPayload payload,
    SystemVerilogUvmTlm2Phase phase,
    SimulationTick delay,
    const std::size_t binding_index) {
  const auto target = target_slot(
      initiator, SystemVerilogUvmTlm2Protocol::Nonblocking, binding_index);
  const auto found = forward_handlers_.find(target);
  if (found == forward_handlers_.end()) {
    fail(kInvalidProtocol, "UVM TLM2 target has no forward handler");
  }
  if (outstanding_count() >= limits_.maximum_outstanding_transactions
      || next_transaction_ == 0) {
    fail(kResourceLimit, "UVM TLM2 outstanding-transaction ceiling exceeded");
  }
  validate_phase(phase, socket(initiator));
  if (phase.kind != SystemVerilogUvmTlm2PhaseKind::BeginRequest
      && phase.kind != SystemVerilogUvmTlm2PhaseKind::Custom) {
    fail(kInvalidProtocol, "UVM TLM2 forward transport must begin a request");
  }
  validate_payload(payload, socket(initiator), true);
  const auto original_phase = phase;
  reserve_callback();
  SystemVerilogUvmTlm2Sync sync;
  try {
    sync = found->second(payload, phase, delay);
  } catch (const std::exception& error) {
    fail(kInvalidProtocol, error.what());
  } catch (...) {
    fail(kInvalidProtocol, "UVM TLM2 forward handler threw");
  }
  validate_phase(phase, socket(initiator));
  validate_payload(payload, socket(initiator), false);
  if (phase.kind != SystemVerilogUvmTlm2PhaseKind::Custom
      && original_phase.kind != SystemVerilogUvmTlm2PhaseKind::Custom
      && phase_rank(phase.kind) < phase_rank(original_phase.kind)) {
    fail(kInvalidProtocol, "UVM TLM2 forward phase moved backward");
  }
  if (sync == SystemVerilogUvmTlm2Sync::Accepted
      && phase != original_phase) {
    fail(kInvalidProtocol, "accepted UVM TLM2 transaction changed phase");
  }
  if (sync == SystemVerilogUvmTlm2Sync::Completed
      && payload.response_status
          == SystemVerilogUvmTlm2ResponseStatus::Incomplete) {
    fail(kInvalidProtocol, "completed UVM TLM2 transaction has no response");
  }
  const auto now = scheduler_ ? scheduler_->now() : 0;
  if (delay > std::numeric_limits<SimulationTick>::max() - now) {
    fail(kResourceLimit, "UVM TLM2 forward delay overflows simulation time");
  }
  const auto slot = next_transaction_++;
  const auto handle = SystemVerilogUvmTlm2TransactionHandle{owner_, slot, 1};
  SystemVerilogUvmTlm2TransactionSnapshot snapshot;
  snapshot.handle = handle;
  snapshot.state = sync == SystemVerilogUvmTlm2Sync::Completed
      ? SystemVerilogUvmTlm2TransactionState::Completed
      : SystemVerilogUvmTlm2TransactionState::Outstanding;
  snapshot.initiator = initiator;
  snapshot.target = socket_handle(target);
  snapshot.payload = payload;
  snapshot.phase = phase;
  snapshot.delay = delay;
  snapshot.last_update_time = now;
  snapshot.hops = hop_count(initiator.slot_, target);
  snapshot.callbacks = 1;
  Transaction transaction_value;
  transaction_value.value = std::move(snapshot);
  transactions_.emplace(slot, std::move(transaction_value));
  if (activity_) {
    const auto& initiator_value = socket(initiator);
    activity_->publish({
        SystemVerilogUvmActivityKind::Transaction,
        sync == SystemVerilogUvmTlm2Sync::Completed
            ? SystemVerilogUvmActivityAction::Completed
            : SystemVerilogUvmActivityAction::Started,
        initiator_value.debug_name,
        "tlm2-forward",
        initiator_value.root,
        transactions_.at(slot).value.payload.transaction_id});
  }
  return {sync, handle, std::move(payload), std::move(phase), delay};
}

SystemVerilogUvmTlm2NonblockingResult
SystemVerilogUvmTlm2Service::nb_transport_bw(
    const SystemVerilogUvmTlm2TransactionHandle transaction_handle_value,
    SystemVerilogUvmTlm2Phase phase,
    SimulationTick delay) {
  auto& selected = transaction(transaction_handle_value);
  if (selected.value.state
      != SystemVerilogUvmTlm2TransactionState::Outstanding) {
    fail(kInvalidProtocol, "UVM TLM2 transaction is not outstanding");
  }
  validate_phase(phase, socket(selected.value.initiator));
  if (phase.kind != SystemVerilogUvmTlm2PhaseKind::Custom
      && selected.value.phase.kind != SystemVerilogUvmTlm2PhaseKind::Custom
      && phase_rank(phase.kind) < phase_rank(selected.value.phase.kind)) {
    fail(kInvalidProtocol, "UVM TLM2 backward phase moved backward");
  }
  const auto found = backward_handlers_.find(selected.value.initiator.slot_);
  if (found == backward_handlers_.end()) {
    fail(kInvalidProtocol, "UVM TLM2 initiator has no backward handler");
  }
  auto payload = selected.value.payload;
  const auto original_phase = phase;
  reserve_callback();
  SystemVerilogUvmTlm2Sync sync;
  try {
    sync = found->second(payload, phase, delay);
  } catch (const std::exception& error) {
    fail(kInvalidProtocol, error.what());
  } catch (...) {
    fail(kInvalidProtocol, "UVM TLM2 backward handler threw");
  }
  validate_phase(phase, socket(selected.value.initiator));
  validate_payload(payload, socket(selected.value.initiator), false);
  if (sync == SystemVerilogUvmTlm2Sync::Accepted
      && phase != original_phase) {
    fail(kInvalidProtocol, "accepted backward UVM TLM2 call changed phase");
  }
  if (phase.kind != SystemVerilogUvmTlm2PhaseKind::Custom
      && original_phase.kind != SystemVerilogUvmTlm2PhaseKind::Custom
      && phase_rank(phase.kind) < phase_rank(original_phase.kind)) {
    fail(kInvalidProtocol, "UVM TLM2 backward callback moved phase backward");
  }
  const auto now = scheduler_ ? scheduler_->now() : 0;
  if (delay > std::numeric_limits<SimulationTick>::max() - now) {
    fail(kResourceLimit, "UVM TLM2 backward delay overflows simulation time");
  }
  selected.value.payload = payload;
  selected.value.phase = phase;
  selected.value.delay = delay;
  selected.value.last_update_time = now;
  ++selected.value.callbacks;
  if (sync == SystemVerilogUvmTlm2Sync::Completed) {
    if (payload.response_status
        == SystemVerilogUvmTlm2ResponseStatus::Incomplete) {
      fail(kInvalidProtocol, "completed backward UVM TLM2 call has no response");
    }
    selected.value.state = SystemVerilogUvmTlm2TransactionState::Completed;
  }
  if (activity_) {
    const auto& initiator_value = socket(selected.value.initiator);
    activity_->publish({
        SystemVerilogUvmActivityKind::Transaction,
        sync == SystemVerilogUvmTlm2Sync::Completed
            ? SystemVerilogUvmActivityAction::Completed
            : SystemVerilogUvmActivityAction::Updated,
        initiator_value.debug_name,
        "tlm2-backward",
        initiator_value.root,
        selected.value.payload.transaction_id});
  }
  return {
      sync, transaction_handle_value, std::move(payload),
      std::move(phase), delay};
}

std::size_t SystemVerilogUvmTlm2Service::transport_dbg(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    SystemVerilogUvmTlm2GenericPayload& payload,
    const std::size_t binding_index) {
  const auto target = target_slot(
      initiator, SystemVerilogUvmTlm2Protocol::Combined, binding_index);
  const auto found = debug_handlers_.find(target);
  if (found == debug_handlers_.end()) {
    fail(kInvalidProtocol, "UVM TLM2 target has no debug handler");
  }
  validate_payload(payload, socket(initiator), true);
  reserve_callback();
  std::size_t transferred{};
  try {
    transferred = found->second(payload);
  } catch (const std::exception& error) {
    fail(kInvalidProtocol, error.what());
  } catch (...) {
    fail(kInvalidProtocol, "UVM TLM2 debug handler threw");
  }
  validate_payload(payload, socket(initiator), false);
  if (transferred > payload.data.size()
      || transferred > limits_.maximum_payload_bytes) {
    fail(kInvalidProtocol, "UVM TLM2 debug handler returned an invalid byte count");
  }
  return transferred;
}

std::optional<SystemVerilogUvmTlm2Dmi>
SystemVerilogUvmTlm2Service::get_direct_mem_ptr(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    SystemVerilogUvmTlm2GenericPayload& payload,
    const std::size_t binding_index) {
  const auto target = target_slot(
      initiator, SystemVerilogUvmTlm2Protocol::Combined, binding_index);
  const auto found = dmi_handlers_.find(target);
  if (found == dmi_handlers_.end()) return std::nullopt;
  validate_payload(payload, socket(initiator), true);
  reserve_callback();
  std::optional<SystemVerilogUvmTlm2Dmi> result;
  try {
    result = found->second(payload);
  } catch (const std::exception& error) {
    fail(kInvalidProtocol, error.what());
  } catch (...) {
    fail(kInvalidProtocol, "UVM TLM2 DMI handler threw");
  }
  validate_payload(payload, socket(initiator), false);
  if (result) {
    validate_dmi(*result);
    payload.dmi_allowed = true;
  }
  return result;
}

void SystemVerilogUvmTlm2Service::invalidate_direct_memory(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    const std::uint64_t start_address,
    const std::uint64_t end_address) {
  const auto& selected = socket(initiator);
  require_live(selected);
  if (selected.descriptor.kind == SystemVerilogUvmTlm2SocketKind::Target
      || start_address > end_address) {
    fail(kInvalidProtocol, "invalid UVM TLM2 direct-memory invalidation");
  }
  const auto found = dmi_invalidation_handlers_.find(initiator.slot_);
  if (found == dmi_invalidation_handlers_.end()) {
    fail(kInvalidProtocol, "UVM TLM2 initiator has no DMI invalidation handler");
  }
  reserve_callback();
  try {
    found->second(start_address, end_address);
  } catch (const std::exception& error) {
    fail(kInvalidProtocol, error.what());
  } catch (...) {
    fail(kInvalidProtocol, "UVM TLM2 DMI invalidation handler threw");
  }
}

SystemVerilogUvmTlm2TransactionSnapshot
SystemVerilogUvmTlm2Service::transaction_snapshot(
    const SystemVerilogUvmTlm2TransactionHandle handle) const {
  return transaction(handle).value;
}

std::vector<SystemVerilogUvmTlm2TransactionHandle>
SystemVerilogUvmTlm2Service::transactions() const {
  std::vector<SystemVerilogUvmTlm2TransactionHandle> result;
  result.reserve(transactions_.size());
  for (const auto& [slot, selected] : transactions_) {
    (void)selected;
    result.push_back(transaction_handle(slot));
  }
  return result;
}

void SystemVerilogUvmTlm2Service::cancel_transaction(
    const SystemVerilogUvmTlm2TransactionHandle handle) {
  auto& selected = transaction(handle);
  if (selected.value.state
      != SystemVerilogUvmTlm2TransactionState::Outstanding) {
    fail(kInvalidProtocol, "UVM TLM2 transaction is not outstanding");
  }
  selected.value.state = SystemVerilogUvmTlm2TransactionState::Cancelled;
  if (activity_) {
    const auto& initiator = socket(selected.value.initiator);
    activity_->publish({
        SystemVerilogUvmActivityKind::Transaction,
        SystemVerilogUvmActivityAction::Cancelled,
        initiator.debug_name,
        "tlm2",
        initiator.root,
        selected.value.payload.transaction_id});
  }
}

void SystemVerilogUvmTlm2Service::cancel_root(
    const SystemVerilogUvmRootHandle root) noexcept {
  for (auto& [slot, selected] : transactions_) {
    (void)slot;
    const auto found = sockets_.find(selected.value.initiator.slot_);
    if (selected.value.state
            == SystemVerilogUvmTlm2TransactionState::Outstanding
        && found != sockets_.end() && found->second.root == root) {
      selected.value.state = SystemVerilogUvmTlm2TransactionState::Cancelled;
    }
  }
}

void SystemVerilogUvmTlm2Service::cancel_all() noexcept {
  for (auto& [slot, selected] : transactions_) {
    (void)slot;
    if (selected.value.state
        == SystemVerilogUvmTlm2TransactionState::Outstanding) {
      selected.value.state = SystemVerilogUvmTlm2TransactionState::Cancelled;
    }
  }
}

std::size_t SystemVerilogUvmTlm2Service::outstanding_count() const noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      transactions_, [](const auto& entry) {
        return entry.second.value.state
            == SystemVerilogUvmTlm2TransactionState::Outstanding;
      }));
}

void SystemVerilogUvmTlm2Service::validate_payload(
    SystemVerilogUvmTlm2GenericPayload& payload,
    const Socket& owner,
    const bool assign_transaction) {
  if (payload.nominal_type != owner.descriptor.profile.payload_type) {
    fail(kInvalidPayload, "UVM TLM2 payload nominal type mismatches socket profile");
  }
  if (payload.data.size() > limits_.maximum_payload_bytes
      || payload.byte_enables.size() > limits_.maximum_byte_enables
      || payload.streaming_width > limits_.maximum_streaming_width
      || payload.extensions.size() > limits_.maximum_extensions) {
    fail(kResourceLimit, "UVM TLM2 payload exceeds a configured ceiling");
  }
  if ((payload.command == SystemVerilogUvmTlm2Command::Read
       || payload.command == SystemVerilogUvmTlm2Command::Write)
      && (payload.data.empty() || payload.streaming_width == 0)) {
    fail(kInvalidPayload, "UVM TLM2 read/write payload has invalid data or streaming width");
  }
  if (std::ranges::any_of(payload.byte_enables, [](const auto value) {
        return value != 0x00U && value != 0xffU;
      })) {
    fail(kInvalidPayload, "UVM TLM2 byte enable is neither disabled nor enabled");
  }
  std::size_t extension_bytes{};
  for (const auto& [name, value] : payload.extensions) {
    if (name.empty()
        || name.size() > limits_.maximum_profile_bytes
        || value.size() > limits_.maximum_extension_bytes
        || name.size() > limits_.maximum_extension_bytes - extension_bytes
        || value.size()
            > limits_.maximum_extension_bytes
                - extension_bytes - name.size()
        || extension_bytes
            > limits_.maximum_extension_bytes) {
      fail(kResourceLimit, "UVM TLM2 extension state exceeds its ceiling");
    }
    extension_bytes += name.size() + value.size();
  }
  if (assign_transaction) {
    if (next_payload_transaction_id_ == 0) {
      fail(kResourceLimit, "UVM TLM2 payload transaction identity exhausted");
    }
    payload.transaction_id = next_payload_transaction_id_++;
  }
  payload.owner_root = owner.root;
  payload.owner_socket = socket_handle(owner.slot);
}

void SystemVerilogUvmTlm2Service::validate_phase(
    const SystemVerilogUvmTlm2Phase& phase,
    const Socket& owner) const {
  if (phase.nominal_type != owner.descriptor.profile.phase_type) {
    fail(kInvalidProtocol, "UVM TLM2 phase nominal type mismatches socket profile");
  }
  if (phase.kind == SystemVerilogUvmTlm2PhaseKind::Custom) {
    if (phase.identity.empty()
        || phase.identity.size() > limits_.maximum_profile_bytes) {
      fail(kInvalidProtocol, "custom UVM TLM2 phase identity is malformed");
    }
  } else if (!phase.identity.empty()) {
    fail(kInvalidProtocol, "standard UVM TLM2 phase cannot carry a custom identity");
  }
}

void SystemVerilogUvmTlm2Service::validate_dmi(
    const SystemVerilogUvmTlm2Dmi& dmi) const {
  const auto range = dmi.end_address >= dmi.start_address
      ? dmi.end_address - dmi.start_address
      : std::numeric_limits<std::uint64_t>::max();
  if (dmi.start_address > dmi.end_address
      || (!dmi.read_allowed && !dmi.write_allowed)
      || dmi.data.empty() || dmi.data.size() > limits_.maximum_dmi_bytes
      || range < dmi.data.size() - 1U) {
    fail(kInvalidPayload, "UVM TLM2 direct-memory descriptor is malformed");
  }
}

void SystemVerilogUvmTlm2Service::reserve_callback() {
  if (callbacks_ >= limits_.maximum_callbacks) {
    fail(kResourceLimit, "UVM TLM2 callback ceiling exceeded");
  }
  ++callbacks_;
}

SystemVerilogUvmTlm2TransactionHandle
SystemVerilogUvmTlm2Service::transaction_handle(
    const std::uint64_t slot) const {
  return SystemVerilogUvmTlm2TransactionHandle{
      owner_, slot, transactions_.at(slot).generation};
}

SystemVerilogUvmTlm2Service::Transaction&
SystemVerilogUvmTlm2Service::transaction(
    const SystemVerilogUvmTlm2TransactionHandle handle) {
  return const_cast<Transaction&>(std::as_const(*this).transaction(handle));
}

const SystemVerilogUvmTlm2Service::Transaction&
SystemVerilogUvmTlm2Service::transaction(
    const SystemVerilogUvmTlm2TransactionHandle handle) const {
  if (!handle.valid() || handle.owner_.get() != owner_.get()) {
    fail(kInvalidHandle, "UVM TLM2 transaction handle is empty or foreign");
  }
  const auto found = transactions_.find(handle.slot_);
  if (found == transactions_.end()
      || found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM TLM2 transaction handle is stale");
  }
  return found->second;
}

std::size_t SystemVerilogUvmTlm2Service::hop_count(
    const std::uint64_t source,
    const std::uint64_t target) const {
  std::vector<std::pair<std::uint64_t, std::size_t>> pending{{source, 0}};
  std::set<std::uint64_t> visited;
  while (!pending.empty()) {
    const auto [slot, hops] = pending.front();
    pending.erase(pending.begin());
    if (hops > limits_.maximum_hops) {
      fail(kResourceLimit, "UVM TLM2 transaction hop ceiling exceeded");
    }
    if (slot == target) return hops;
    if (!visited.insert(slot).second) continue;
    for (const auto next : sockets_.at(slot).outbound) {
      pending.emplace_back(next, hops + 1);
    }
  }
  fail(kInvalidSocket, "bound UVM TLM2 target is unreachable");
}

}  // namespace fsim::runtime
