// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_tlm2.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-TLM2-001"};
constexpr std::string_view kInvalidSocket{"FSIM-UVM-TLM2-002"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-TLM2-005"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmTlm2Error{std::string{code}, std::string{message}};
}

[[nodiscard]] bool valid_name(const std::string_view name) {
  return !name.empty()
      && std::ranges::none_of(name, [](const unsigned char character) {
           return character == '.' || character == '/'
               || character <= ' ';
         });
}

}  // namespace

SystemVerilogUvmTlm2Error::SystemVerilogUvmTlm2Error(
    std::string code,
    std::string message)
    : std::runtime_error{std::move(message)}, code_(std::move(code)) {}

SystemVerilogUvmTlm2Service::SystemVerilogUvmTlm2Service(
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmTlm2Limits limits)
    : components_(&components),
      limits_(limits),
      owner_(std::make_shared<unsigned char>()) {
  if (limits_.maximum_sockets == 0
      || limits_.maximum_connections == 0
      || limits_.maximum_sockets_per_component == 0
      || limits_.maximum_name_bytes == 0
      || limits_.maximum_profile_bytes == 0
      || limits_.maximum_fanout == 0
      || limits_.maximum_depth == 0
      || limits_.maximum_hops == 0
      || limits_.maximum_payload_bytes == 0
      || limits_.maximum_byte_enables == 0
      || limits_.maximum_streaming_width == 0
      || limits_.maximum_extensions == 0
      || limits_.maximum_extension_bytes == 0
      || limits_.maximum_callbacks == 0
      || limits_.maximum_outstanding_transactions == 0
      || limits_.maximum_dmi_bytes == 0
      || limits_.maximum_mutations == 0) {
    fail(kResourceLimit, "UVM TLM2 limits must all be nonzero");
  }
}

SystemVerilogUvmTlm2Service::SystemVerilogUvmTlm2Service(
    SystemVerilogUvmComponentService& components,
    Scheduler& scheduler,
    SystemVerilogUvmTlm2Limits limits)
    : SystemVerilogUvmTlm2Service(components, std::move(limits)) {
  scheduler_ = &scheduler;
}

SystemVerilogUvmTlm2Service::~SystemVerilogUvmTlm2Service() {
  cancel_all();
}

SystemVerilogUvmTlm2SocketHandle
SystemVerilogUvmTlm2Service::register_socket(
    SystemVerilogUvmTlm2SocketDescriptor descriptor) {
  if (mutations_ >= limits_.maximum_mutations
      || sockets_.size() >= limits_.maximum_sockets
      || next_socket_ == 0
      || next_declaration_order_
          == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM TLM2 socket or mutation ceiling exceeded");
  }
  if (descriptor.component == 0
      || !components_->contains(descriptor.component)) {
    fail(kInvalidHandle, "UVM TLM2 socket component is empty or stale");
  }
  if (!valid_name(descriptor.name)
      || descriptor.name.size() > limits_.maximum_name_bytes) {
    fail(kInvalidSocket, "UVM TLM2 socket name is malformed or too long");
  }
  if (descriptor.profile.payload_type.empty()
      || descriptor.profile.phase_type.empty()
      || descriptor.profile.bus_width_bytes == 0) {
    fail(kInvalidSocket, "UVM TLM2 socket profile is malformed");
  }
  if (descriptor.profile.payload_type.size()
          + descriptor.profile.phase_type.size()
      > limits_.maximum_profile_bytes) {
    fail(kResourceLimit, "UVM TLM2 profile exceeds its byte ceiling");
  }
  if (descriptor.profile.bus_width_bytes
      > limits_.maximum_streaming_width) {
    fail(kResourceLimit, "UVM TLM2 bus width exceeds its ceiling");
  }
  if (descriptor.kind == SystemVerilogUvmTlm2SocketKind::Target) {
    if (descriptor.minimum_connections != 0
        || descriptor.maximum_connections != 0) {
      fail(kInvalidSocket, "terminal UVM TLM2 targets have no outbound cardinality");
    }
  } else if (descriptor.minimum_connections > descriptor.maximum_connections
             || descriptor.maximum_connections == 0) {
    fail(kInvalidSocket, "UVM TLM2 socket cardinality is malformed");
  } else if (descriptor.maximum_connections > limits_.maximum_fanout) {
    fail(kResourceLimit, "UVM TLM2 socket fanout exceeds its ceiling");
  }

  std::size_t component_sockets{};
  for (const auto& [slot, existing] : sockets_) {
    (void)slot;
    if (existing.descriptor.component != descriptor.component) continue;
    ++component_sockets;
    if (existing.descriptor.name == descriptor.name) {
      fail(kInvalidSocket, "duplicate UVM TLM2 socket name on component");
    }
  }
  if (component_sockets >= limits_.maximum_sockets_per_component) {
    fail(kResourceLimit, "UVM TLM2 component socket ceiling exceeded");
  }

  const auto component = components_->snapshot(descriptor.component);
  const auto slot = next_socket_++;
  Socket value;
  value.descriptor = std::move(descriptor);
  value.slot = slot;
  value.root = component.root;
  value.full_name = component.full_name + "." + value.descriptor.name;
  value.debug_name = std::string{components_->root_identity(component.root)}
      + ":" + value.full_name;
  value.declaration_order = next_declaration_order_++;
  sockets_.emplace(slot, std::move(value));
  ++mutations_;
  ++revision_;
  bound_revision_ = 0;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Connection,
        SystemVerilogUvmActivityAction::Created,
        sockets_.at(slot).debug_name,
        "tlm2-socket",
        sockets_.at(slot).root});
  }
  return socket_handle(slot);
}

void SystemVerilogUvmTlm2Service::connect(
    const SystemVerilogUvmTlm2SocketHandle source_handle,
    const SystemVerilogUvmTlm2SocketHandle target_handle) {
  auto& source = socket(source_handle);
  auto& target = socket(target_handle);
  require_live(source);
  require_live(target);
  if (mutations_ >= limits_.maximum_mutations
      || connections_ >= limits_.maximum_connections) {
    fail(kResourceLimit, "UVM TLM2 connection or mutation ceiling exceeded");
  }
  if (source_handle == target_handle
      || source.descriptor.kind == SystemVerilogUvmTlm2SocketKind::Target
      || target.descriptor.kind == SystemVerilogUvmTlm2SocketKind::Initiator) {
    fail(kInvalidSocket, "UVM TLM2 socket kinds have invalid connection direction");
  }
  if (source.root != target.root
      || source.descriptor.profile != target.descriptor.profile) {
    fail(kInvalidSocket, "UVM TLM2 connection root or nominal profile mismatches");
  }
  if (std::ranges::find(source.outbound, target_handle.slot_)
      != source.outbound.end()) {
    fail(kInvalidSocket, "duplicate UVM TLM2 connection");
  }
  if (source.outbound.size() >= source.descriptor.maximum_connections
      || source.outbound.size() >= limits_.maximum_fanout) {
    fail(kResourceLimit, "UVM TLM2 socket fanout ceiling exceeded");
  }
  std::size_t work{};
  if (reaches(target_handle.slot_, source_handle.slot_, work)) {
    fail(kInvalidSocket, "UVM TLM2 connection would create a cycle");
  }
  source.outbound.push_back(target_handle.slot_);
  target.inbound.push_back(source_handle.slot_);
  ++connections_;
  ++mutations_;
  ++revision_;
  bound_revision_ = 0;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Connection,
        SystemVerilogUvmActivityAction::Connected,
        source.debug_name,
        target.debug_name,
        source.root,
        connections_});
  }
}

void SystemVerilogUvmTlm2Service::bind_all() {
  std::map<std::uint64_t, std::vector<SystemVerilogUvmTlm2SocketHandle>>
      candidate;
  std::size_t work{};
  for (const auto& [slot, selected] : sockets_) {
    require_live(selected);
    if (selected.descriptor.kind != SystemVerilogUvmTlm2SocketKind::Target
        && (selected.outbound.size()
                < selected.descriptor.minimum_connections
            || selected.outbound.size()
                > selected.descriptor.maximum_connections)) {
      fail(kInvalidSocket, "UVM TLM2 socket violates connection cardinality");
    }
    std::vector<std::uint64_t> path;
    collect_targets(slot, 0, work, candidate[slot], path);
    if (!selected.outbound.empty() && candidate[slot].empty()) {
      fail(kInvalidSocket, "UVM TLM2 socket chain reaches no target");
    }
  }
  for (auto& [slot, targets] : candidate) {
    sockets_.at(slot).resolved = std::move(targets);
  }
  bound_revision_ = revision_;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Connection,
        SystemVerilogUvmActivityAction::Bound,
        "tlm2",
        "resolved",
        0,
        connections_});
  }
}

bool SystemVerilogUvmTlm2Service::contains(
    const SystemVerilogUvmTlm2SocketHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_.get() != owner_.get()) return false;
  const auto found = sockets_.find(handle.slot_);
  return found != sockets_.end()
      && found->second.generation == handle.generation_;
}

SystemVerilogUvmTlm2SocketSnapshot SystemVerilogUvmTlm2Service::snapshot(
    const SystemVerilogUvmTlm2SocketHandle handle) const {
  const auto& selected = socket(handle);
  require_live(selected);
  SystemVerilogUvmTlm2SocketSnapshot result;
  result.handle = handle;
  result.kind = selected.descriptor.kind;
  result.profile = selected.descriptor.profile;
  result.component = selected.descriptor.component;
  result.root = selected.root;
  result.name = selected.descriptor.name;
  result.full_name = selected.full_name;
  result.debug_name = selected.debug_name;
  result.minimum_connections = selected.descriptor.minimum_connections;
  result.maximum_connections = selected.descriptor.maximum_connections;
  result.declaration_order = selected.declaration_order;
  for (const auto slot : selected.outbound) {
    result.outbound.push_back(socket_handle(slot));
  }
  for (const auto slot : selected.inbound) {
    result.inbound.push_back(socket_handle(slot));
  }
  if (binding_valid()) result.resolved_targets = selected.resolved;
  return result;
}

std::vector<SystemVerilogUvmTlm2SocketHandle>
SystemVerilogUvmTlm2Service::sockets() const {
  std::vector<SystemVerilogUvmTlm2SocketHandle> result;
  result.reserve(sockets_.size());
  for (const auto& [slot, selected] : sockets_) {
    (void)selected;
    result.push_back(socket_handle(slot));
  }
  return result;
}

SystemVerilogUvmTlm2Service::Socket& SystemVerilogUvmTlm2Service::socket(
    const SystemVerilogUvmTlm2SocketHandle handle) {
  return const_cast<Socket&>(std::as_const(*this).socket(handle));
}

const SystemVerilogUvmTlm2Service::Socket&
SystemVerilogUvmTlm2Service::socket(
    const SystemVerilogUvmTlm2SocketHandle handle) const {
  if (!contains(handle)) {
    fail(kInvalidHandle, "UVM TLM2 socket is empty, stale, or foreign");
  }
  return sockets_.at(handle.slot_);
}

SystemVerilogUvmTlm2SocketHandle
SystemVerilogUvmTlm2Service::socket_handle(const std::uint64_t slot) const {
  return SystemVerilogUvmTlm2SocketHandle{
      owner_, slot, sockets_.at(slot).generation};
}

void SystemVerilogUvmTlm2Service::require_live(
    const Socket& selected) const {
  if (!components_->contains(selected.descriptor.component)
      || !components_->contains_root(selected.root)
      || components_->root_of(selected.descriptor.component) != selected.root) {
    fail(kInvalidHandle, "UVM TLM2 socket component or root is stale");
  }
}

bool SystemVerilogUvmTlm2Service::reaches(
    const std::uint64_t from,
    const std::uint64_t wanted,
    std::size_t& work) const {
  std::vector<std::pair<std::uint64_t, std::size_t>> pending{{from, 0}};
  std::set<std::uint64_t> visited;
  while (!pending.empty()) {
    const auto [slot, depth] = pending.back();
    pending.pop_back();
    if (++work > limits_.maximum_hops || depth > limits_.maximum_depth) {
      fail(kResourceLimit, "UVM TLM2 graph traversal ceiling exceeded");
    }
    if (slot == wanted) return true;
    if (!visited.insert(slot).second) continue;
    for (const auto target : sockets_.at(slot).outbound) {
      pending.emplace_back(target, depth + 1);
    }
  }
  return false;
}

void SystemVerilogUvmTlm2Service::collect_targets(
    const std::uint64_t slot,
    const std::size_t depth,
    std::size_t& work,
    std::vector<SystemVerilogUvmTlm2SocketHandle>& result,
    std::vector<std::uint64_t>& path) const {
  if (++work > limits_.maximum_hops || depth > limits_.maximum_depth) {
    fail(kResourceLimit, "UVM TLM2 binding hop or depth ceiling exceeded");
  }
  if (std::ranges::find(path, slot) != path.end()) {
    fail(kInvalidSocket, "UVM TLM2 socket graph contains a cycle");
  }
  const auto& selected = sockets_.at(slot);
  if (selected.descriptor.kind == SystemVerilogUvmTlm2SocketKind::Target) {
    const auto target = socket_handle(slot);
    if (std::ranges::find(result, target) == result.end()) {
      result.push_back(target);
    }
    return;
  }
  path.push_back(slot);
  for (const auto target : selected.outbound) {
    collect_targets(target, depth + 1, work, result, path);
  }
  path.pop_back();
}

std::uint64_t SystemVerilogUvmTlm2Service::target_slot(
    const SystemVerilogUvmTlm2SocketHandle initiator,
    const SystemVerilogUvmTlm2Protocol required,
    const std::size_t binding_index) const {
  const auto& selected = socket(initiator);
  require_live(selected);
  if (!binding_valid()) {
    fail(kInvalidSocket, "UVM TLM2 execution requires a stable binding");
  }
  if (selected.descriptor.kind == SystemVerilogUvmTlm2SocketKind::Target) {
    fail(kInvalidSocket, "terminal UVM TLM2 target cannot initiate transport");
  }
  const auto protocol = selected.descriptor.profile.protocol;
  if (required != SystemVerilogUvmTlm2Protocol::Combined
      && protocol != SystemVerilogUvmTlm2Protocol::Combined
      && protocol != required) {
    fail(kInvalidSocket, "UVM TLM2 socket protocol does not support transport");
  }
  if (binding_index >= selected.resolved.size()) {
    fail(kInvalidSocket, "UVM TLM2 binding index is out of range");
  }
  const auto result = selected.resolved[binding_index].slot_;
  require_live(sockets_.at(result));
  return result;
}

}  // namespace fsim::runtime
