// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_tlm1.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-TLM1-001"};
constexpr std::string_view kInvalidEndpoint{"FSIM-UVM-TLM1-002"};
constexpr std::string_view kInvalidConnection{"FSIM-UVM-TLM1-003"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-TLM1-004"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmTlm1Error{std::string{code}, std::string{message}};
}

[[nodiscard]] bool valid_endpoint_name(const std::string_view name) {
  if (name.empty()) return false;
  return std::ranges::none_of(name, [](const unsigned char character) {
    return character == '.' || character == '/' || character <= ' ';
  });
}

}  // namespace

SystemVerilogUvmTlm1Error::SystemVerilogUvmTlm1Error(
    std::string code,
    std::string message)
    : std::runtime_error{std::move(message)}, code_(std::move(code)) {}

SystemVerilogUvmTlm1Service::SystemVerilogUvmTlm1Service(
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmTlm1Limits limits)
    : components_(&components),
      limits_(limits),
      owner_(std::make_shared<unsigned char>(0)) {
  if (limits_.maximum_endpoints == 0
      || limits_.maximum_connections == 0
      || limits_.maximum_endpoints_per_component == 0
      || limits_.maximum_name_bytes == 0
      || limits_.maximum_profile_bytes == 0
      || limits_.maximum_fanout == 0
      || limits_.maximum_depth == 0
      || limits_.maximum_resolution_work == 0
      || limits_.maximum_mutations == 0
      || limits_.maximum_fifos == 0
      || limits_.maximum_fifo_capacity == 0
      || limits_.maximum_queued_payloads == 0
      || limits_.maximum_payload_bits == 0
      || limits_.maximum_payload_type_bytes == 0
      || limits_.maximum_pending_operations == 0
      || limits_.maximum_execution_operations == 0
      || limits_.maximum_analysis_callbacks_per_publication == 0
      || limits_.maximum_analysis_recursion_depth == 0
      || limits_.maximum_analysis_failures == 0
      || limits_.maximum_analysis_publications == 0) {
    fail(kResourceLimit, "UVM TLM1 limits must all be nonzero");
  }
}

SystemVerilogUvmTlm1Service::SystemVerilogUvmTlm1Service(
    SystemVerilogClassHeap& heap,
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmTlm1Limits limits)
    : SystemVerilogUvmTlm1Service(components, std::move(limits)) {
  heap_ = &heap;
}

SystemVerilogUvmTlm1Service::~SystemVerilogUvmTlm1Service() {
  cancel_all();
  if (phases_) phases_->clear_tlm1_service(*this);
}

SystemVerilogUvmTlm1EndpointHandle
SystemVerilogUvmTlm1Service::register_endpoint(
    SystemVerilogUvmTlm1EndpointDescriptor descriptor) {
  if (mutations_ >= limits_.maximum_mutations
      || endpoints_.size() >= limits_.maximum_endpoints) {
    fail(kResourceLimit, "UVM TLM1 endpoint or mutation ceiling exceeded");
  }
  if (descriptor.component == 0
      || !components_->contains(descriptor.component)) {
    fail(kInvalidHandle, "UVM TLM1 endpoint component is empty or stale");
  }
  if (!valid_endpoint_name(descriptor.name)
      || descriptor.name.size() > limits_.maximum_name_bytes) {
    fail(kInvalidEndpoint, "UVM TLM1 endpoint name is malformed or too long");
  }
  if (descriptor.profile.request_type.empty()) {
    fail(kInvalidEndpoint, "UVM TLM1 request nominal type must not be empty");
  }
  if (descriptor.profile.request_type.size()
          + descriptor.profile.response_type.size()
      > limits_.maximum_profile_bytes) {
    fail(kResourceLimit, "UVM TLM1 profile exceeds its byte ceiling");
  }
  if (descriptor.kind == SystemVerilogUvmTlm1EndpointKind::Implementation) {
    if (descriptor.minimum_connections != 0
        || descriptor.maximum_connections != 0) {
      fail(
          kInvalidEndpoint,
          "UVM TLM1 implementation endpoints cannot have outbound cardinality");
    }
  } else if (descriptor.minimum_connections > descriptor.maximum_connections
             || descriptor.maximum_connections == 0) {
    fail(kInvalidEndpoint, "UVM TLM1 endpoint cardinality is malformed");
  } else if (descriptor.maximum_connections > limits_.maximum_fanout) {
    fail(kResourceLimit, "UVM TLM1 endpoint fanout exceeds its ceiling");
  }

  std::size_t component_endpoints{};
  for (const auto& [slot, existing] : endpoints_) {
    (void)slot;
    if (existing.descriptor.component != descriptor.component) continue;
    ++component_endpoints;
    if (existing.descriptor.name == descriptor.name) {
      fail(kInvalidEndpoint, "duplicate UVM TLM1 endpoint name on component");
    }
  }
  if (component_endpoints >= limits_.maximum_endpoints_per_component) {
    fail(kResourceLimit, "UVM TLM1 component endpoint ceiling exceeded");
  }

  const auto component = components_->snapshot(descriptor.component);
  const auto slot = next_slot_;
  Endpoint value;
  value.slot = slot;
  value.root = component.root;
  value.component_name = component.full_name;
  value.full_name = component.full_name + "." + descriptor.name;
  value.debug_name = std::string{components_->root_identity(component.root)}
      + ":" + value.full_name;
  value.declaration_order = next_declaration_order_;
  value.descriptor = std::move(descriptor);
  endpoints_.emplace(slot, std::move(value));
  ++next_slot_;
  ++next_declaration_order_;
  ++mutations_;
  ++revision_;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Connection,
        SystemVerilogUvmActivityAction::Created,
        endpoints_.at(slot).debug_name,
        "tlm1-endpoint",
        endpoints_.at(slot).root});
  }
  return handle(slot);
}

void SystemVerilogUvmTlm1Service::connect(
    const SystemVerilogUvmTlm1EndpointHandle source_handle,
    const SystemVerilogUvmTlm1EndpointHandle target_handle) {
  Endpoint& source = endpoint(source_handle);
  Endpoint& target = endpoint(target_handle);
  require_live(source);
  require_live(target);
  if (mutations_ >= limits_.maximum_mutations
      || connections_ >= limits_.maximum_connections) {
    fail(kResourceLimit, "UVM TLM1 connection or mutation ceiling exceeded");
  }
  if (source_handle == target_handle) {
    fail(kInvalidConnection, "UVM TLM1 endpoint cannot connect to itself");
  }
  if (source.descriptor.kind
          == SystemVerilogUvmTlm1EndpointKind::Implementation
      || target.descriptor.kind == SystemVerilogUvmTlm1EndpointKind::Port) {
    fail(kInvalidConnection, "UVM TLM1 endpoint kinds have invalid direction");
  }
  if (source.root != target.root) {
    fail(kInvalidConnection, "UVM TLM1 connection crosses root contexts");
  }
  if (source.descriptor.profile != target.descriptor.profile) {
    fail(
        kInvalidConnection,
        "UVM TLM1 connection interface, profile, or direction mismatches");
  }
  if (std::ranges::find(source.outbound, target_handle.slot_)
      != source.outbound.end()) {
    fail(kInvalidEndpoint, "duplicate UVM TLM1 connection");
  }
  if (source.outbound.size() >= source.descriptor.maximum_connections
      || source.outbound.size() >= limits_.maximum_fanout) {
    fail(kResourceLimit, "UVM TLM1 endpoint fanout ceiling exceeded");
  }
  std::size_t work{};
  if (reaches(target_handle.slot_, source_handle.slot_, work)) {
    fail(kInvalidConnection, "UVM TLM1 connection would create a cycle");
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

void SystemVerilogUvmTlm1Service::resolve_all() {
  std::map<std::uint64_t, std::vector<SystemVerilogUvmTlm1EndpointHandle>>
      candidate;
  std::size_t work{};
  for (const auto& [slot, value] : endpoints_) {
    require_live(value);
    if (value.descriptor.kind
        != SystemVerilogUvmTlm1EndpointKind::Implementation) {
      if (value.outbound.size() < value.descriptor.minimum_connections
          || value.outbound.size() > value.descriptor.maximum_connections) {
        fail(
            kInvalidEndpoint,
            "UVM TLM1 endpoint does not satisfy connection cardinality");
      }
    }
    std::vector<std::uint64_t> path;
    collect_implementations(slot, 0, work, candidate[slot], path);
    if (!value.outbound.empty() && candidate[slot].empty()) {
      fail(kInvalidEndpoint, "UVM TLM1 binding reaches no implementation");
    }
  }
  for (auto& [slot, binding] : candidate) {
    endpoints_.at(slot).resolved = std::move(binding);
  }
  bound_revision_ = revision_;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Connection,
        SystemVerilogUvmActivityAction::Bound,
        "tlm1",
        "resolved",
        0,
        connections_});
  }
}

bool SystemVerilogUvmTlm1Service::contains(
    const SystemVerilogUvmTlm1EndpointHandle endpoint_handle) const noexcept {
  if (!endpoint_handle.valid()
      || endpoint_handle.owner_.get() != owner_.get()) return false;
  const auto found = endpoints_.find(endpoint_handle.slot_);
  return found != endpoints_.end()
      && found->second.generation == endpoint_handle.generation_;
}

SystemVerilogUvmTlm1EndpointSnapshot
SystemVerilogUvmTlm1Service::snapshot(
    const SystemVerilogUvmTlm1EndpointHandle endpoint_handle) const {
  const auto& value = endpoint(endpoint_handle);
  require_live(value);
  SystemVerilogUvmTlm1EndpointSnapshot result;
  result.handle = endpoint_handle;
  result.kind = value.descriptor.kind;
  result.profile = value.descriptor.profile;
  result.component = value.descriptor.component;
  result.root = value.root;
  result.name = value.descriptor.name;
  result.full_name = value.full_name;
  result.debug_name = value.debug_name;
  result.minimum_connections = value.descriptor.minimum_connections;
  result.maximum_connections = value.descriptor.maximum_connections;
  result.declaration_order = value.declaration_order;
  for (const auto slot : value.outbound) result.outbound.push_back(handle(slot));
  for (const auto slot : value.inbound) result.inbound.push_back(handle(slot));
  if (binding_valid()) result.resolved = value.resolved;
  return result;
}

std::vector<SystemVerilogUvmTlm1EndpointHandle>
SystemVerilogUvmTlm1Service::endpoints() const {
  std::vector<SystemVerilogUvmTlm1EndpointHandle> result;
  result.reserve(endpoints_.size());
  for (const auto& [slot, value] : endpoints_) {
    (void)value;
    result.push_back(handle(slot));
  }
  return result;
}

std::span<const SystemVerilogUvmTlm1EndpointHandle>
SystemVerilogUvmTlm1Service::resolved(
    const SystemVerilogUvmTlm1EndpointHandle endpoint_handle) const {
  const auto& value = endpoint(endpoint_handle);
  require_live(value);
  if (!binding_valid()) {
    fail(kInvalidEndpoint, "UVM TLM1 graph has not been stably bound");
  }
  return value.resolved;
}

SystemVerilogUvmTlm1Service::Endpoint&
SystemVerilogUvmTlm1Service::endpoint(
    const SystemVerilogUvmTlm1EndpointHandle endpoint_handle) {
  return const_cast<Endpoint&>(std::as_const(*this).endpoint(endpoint_handle));
}

const SystemVerilogUvmTlm1Service::Endpoint&
SystemVerilogUvmTlm1Service::endpoint(
    const SystemVerilogUvmTlm1EndpointHandle endpoint_handle) const {
  if (!contains(endpoint_handle)) {
    fail(
        kInvalidHandle,
        "UVM TLM1 endpoint is empty, stale, or owned by another simulation");
  }
  return endpoints_.at(endpoint_handle.slot_);
}

SystemVerilogUvmTlm1EndpointHandle
SystemVerilogUvmTlm1Service::handle(const std::uint64_t slot) const {
  return SystemVerilogUvmTlm1EndpointHandle{
      owner_, slot, endpoints_.at(slot).generation};
}

void SystemVerilogUvmTlm1Service::require_live(
    const Endpoint& value) const {
  if (!components_->contains(value.descriptor.component)
      || !components_->contains_root(value.root)
      || components_->root_of(value.descriptor.component) != value.root) {
    fail(kInvalidHandle, "UVM TLM1 endpoint component or root is stale");
  }
}

bool SystemVerilogUvmTlm1Service::reaches(
    const std::uint64_t from,
    const std::uint64_t wanted,
    std::size_t& work) const {
  std::vector<std::pair<std::uint64_t, std::size_t>> pending{{from, 0}};
  std::set<std::uint64_t> visited;
  while (!pending.empty()) {
    const auto [slot, depth] = pending.back();
    pending.pop_back();
    if (++work > limits_.maximum_resolution_work) {
      fail(kResourceLimit, "UVM TLM1 graph traversal work ceiling exceeded");
    }
    if (depth > limits_.maximum_depth) {
      fail(kResourceLimit, "UVM TLM1 graph depth ceiling exceeded");
    }
    if (slot == wanted) return true;
    if (!visited.insert(slot).second) continue;
    const auto& outbound = endpoints_.at(slot).outbound;
    for (auto iterator = outbound.rbegin(); iterator != outbound.rend();
         ++iterator) {
      pending.emplace_back(*iterator, depth + 1);
    }
  }
  return false;
}

void SystemVerilogUvmTlm1Service::collect_implementations(
    const std::uint64_t slot,
    const std::size_t depth,
    std::size_t& work,
    std::vector<SystemVerilogUvmTlm1EndpointHandle>& result,
    std::vector<std::uint64_t>& path) const {
  if (++work > limits_.maximum_resolution_work) {
    fail(kResourceLimit, "UVM TLM1 binding work ceiling exceeded");
  }
  if (depth > limits_.maximum_depth) {
    fail(kResourceLimit, "UVM TLM1 binding depth ceiling exceeded");
  }
  if (std::ranges::find(path, slot) != path.end()) {
    fail(kInvalidConnection, "UVM TLM1 binding graph contains a cycle");
  }
  const auto& value = endpoints_.at(slot);
  if (value.descriptor.kind
      == SystemVerilogUvmTlm1EndpointKind::Implementation) {
    const auto implementation = handle(slot);
    if (std::ranges::find(result, implementation) == result.end()) {
      result.push_back(implementation);
    }
    return;
  }
  path.push_back(slot);
  for (const auto target : value.outbound) {
    collect_implementations(target, depth + 1, work, result, path);
  }
  path.pop_back();
}

}  // namespace fsim::runtime
