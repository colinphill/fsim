// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_foreign.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

std::atomic<std::uint64_t> next_simulation_identity{1};

[[nodiscard]] std::string phase_slot_identity(const std::uint64_t slot) {
  return "phase-slot:" + std::to_string(slot);
}

void append_field(std::string &result, const std::string_view name,
                  const std::string_view value) {
  result.append(name);
  result.push_back('=');
  result.append(std::to_string(value.size()));
  result.push_back(':');
  result.append(value);
  result.push_back(';');
}

template <typename Value>
void append_number(std::string &result, const std::string_view name,
                   const Value value) {
  append_field(result, name, std::to_string(value));
}

void append_binary_u64(std::vector<std::uint8_t> &result,
                       const std::uint64_t value) {
  for (unsigned shift = 0; shift < 64; shift += 8) {
    result.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

void append_binary_string(std::vector<std::uint8_t> &result,
                          const std::string_view value) {
  append_binary_u64(result, value.size());
  result.insert(result.end(), value.begin(), value.end());
}

std::string hex_bytes(const std::span<const std::uint8_t> bytes) {
  std::ostringstream result;
  result << std::hex << std::setfill('0');
  for (const auto byte : bytes) {
    result << std::setw(2) << static_cast<unsigned>(byte);
  }
  return result.str();
}

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL host_capture(
    void *const context, fsim_uvm_foreign_snapshot_v1 *const snapshot) {
  if (!context || !snapshot)
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  return static_cast<SystemVerilogUvmForeignService *>(context)->capture(
      *snapshot);
}

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL host_copy_record(
    void *const context, const fsim_uvm_foreign_snapshot_v1 *const snapshot,
    const std::uint64_t index, fsim_uvm_foreign_record_v1 *const record,
    char *const identity, const std::uint64_t identity_capacity,
    char *const detail, const std::uint64_t detail_capacity,
    std::uint8_t *const payload, const std::uint64_t payload_capacity) {
  if (!context || !snapshot || !record ||
      (identity_capacity != 0 && !identity) ||
      (detail_capacity != 0 && !detail) ||
      (payload_capacity != 0 && !payload) ||
      identity_capacity > std::numeric_limits<std::size_t>::max() ||
      detail_capacity > std::numeric_limits<std::size_t>::max() ||
      payload_capacity > std::numeric_limits<std::size_t>::max()) {
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  }
  return static_cast<SystemVerilogUvmForeignService *>(context)->copy_record(
      *snapshot, static_cast<std::size_t>(index), *record,
      {identity, static_cast<std::size_t>(identity_capacity)},
      {detail, static_cast<std::size_t>(detail_capacity)},
      {payload, static_cast<std::size_t>(payload_capacity)});
}

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL host_release(
    void *const context, const fsim_uvm_foreign_snapshot_v1 *const snapshot) {
  if (!context || !snapshot)
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  return static_cast<SystemVerilogUvmForeignService *>(context)->release(
      *snapshot);
}

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL host_add_callback(
    void *const context, const fsim_uvm_foreign_activity_callback_v1 callback,
    void *const callback_context, std::uint64_t *const token) {
  if (!context || !token)
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  return static_cast<SystemVerilogUvmForeignService *>(context)->add_callback(
      callback, callback_context, *token);
}

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL
host_remove_callback(void *const context, const std::uint64_t token) {
  if (!context)
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  return static_cast<SystemVerilogUvmForeignService *>(context)
      ->remove_callback(token);
}

} // namespace

SystemVerilogUvmForeignService::SystemVerilogUvmForeignService(
    SystemVerilogUvmPhaseService &phases,
    SystemVerilogUvmObjectionService &objections,
    SystemVerilogUvmTlm1Service &tlm1, SystemVerilogUvmTlm2Service &tlm2,
    SystemVerilogUvmActivityService &activity,
    SystemVerilogUvmForeignLimits limits)
    : phases_(&phases), objections_(&objections), tlm1_(&tlm1), tlm2_(&tlm2),
      activity_(&activity), limits_(limits),
      simulation_identity_(next_simulation_identity.fetch_add(1)) {
  if (simulation_identity_ == 0 || limits_.maximum_snapshots == 0 ||
      limits_.maximum_records == 0 || limits_.maximum_text_bytes == 0 ||
      limits_.maximum_payload_bytes == 0 || limits_.maximum_callbacks == 0) {
    throw std::invalid_argument{
        "UVM foreign limits and identity must be nonzero"};
  }
}

SystemVerilogUvmForeignService::~SystemVerilogUvmForeignService() {
  for (const auto &[token, activity_token] : callbacks_) {
    (void)token;
    activity_->remove_observer(activity_token);
  }
}

void SystemVerilogUvmForeignService::set_integrated_services(
    SystemVerilogUvmSequenceService &sequences,
    SystemVerilogUvmCallbackService &callbacks,
    SystemVerilogUvmTransactionRecorderService &transactions,
    SystemVerilogUvmRegisterModelService &register_model) noexcept {
  sequences_ = &sequences;
  uvm_callbacks_ = &callbacks;
  transactions_ = &transactions;
  register_model_ = &register_model;
}

void SystemVerilogUvmForeignService::set_error(std::string code,
                                               std::string message) const {
  diagnostic_code_ = std::move(code);
  diagnostic_message_ = std::move(message);
}

fsim_uvm_foreign_status_v1 SystemVerilogUvmForeignService::capture(
    fsim_uvm_foreign_snapshot_v1 &result) noexcept {
  try {
    if (snapshots_.size() >= limits_.maximum_snapshots ||
        next_generation_ == std::numeric_limits<std::uint64_t>::max()) {
      set_error("FSIM-UVM-FOREIGN-002",
                "UVM foreign snapshot ceiling exceeded");
      return FSIM_UVM_FOREIGN_RESOURCE_LIMIT;
    }
    Snapshot captured;
    captured.value = {FSIM_UVM_FOREIGN_ABI_VERSION,
                      sizeof(fsim_uvm_foreign_snapshot_v1),
                      simulation_identity_,
                      next_generation_,
                      scheduler_ ? scheduler_->now() : 0,
                      scheduler_ ? scheduler_->delta() : 0,
                      0};
    std::size_t text_bytes{};
    std::size_t payload_bytes{};
    const auto append = [&](SystemVerilogUvmForeignRecord record) {
      if (captured.records.size() >= limits_.maximum_records ||
          record.identity.size() > limits_.maximum_text_bytes - text_bytes ||
          record.detail.size() > limits_.maximum_text_bytes - text_bytes -
                                     record.identity.size() ||
          record.payload.size() >
              limits_.maximum_payload_bytes - payload_bytes) {
        throw std::length_error{
            "UVM foreign snapshot resource ceiling exceeded"};
      }
      text_bytes += record.identity.size() + record.detail.size();
      payload_bytes += record.payload.size();
      record.value.struct_size = sizeof(fsim_uvm_foreign_record_v1);
      record.value.handle = captured.records.size() + 1U;
      record.value.identity_size = record.identity.size();
      record.value.detail_size = record.detail.size();
      record.value.payload_size = record.payload.size();
      captured.records.push_back(std::move(record));
    };
    const auto phase_identity = [&](const SystemVerilogUvmPhaseHandle phase) {
      const auto value = phases_->snapshot(phase);
      return phases_->snapshot(value.domain).identity + "." + value.identity;
    };
    const auto tlm1_identity =
        [&](const SystemVerilogUvmTlm1EndpointHandle endpoint) {
          return tlm1_->snapshot(endpoint).debug_name;
        };
    const auto tlm2_identity =
        [&](const SystemVerilogUvmTlm2SocketHandle socket) {
          return tlm2_->snapshot(socket).debug_name;
        };
    for (const auto &domain : phases_->domains()) {
      const auto domain_value = phases_->snapshot(domain);
      for (const auto &phase : phases_->phases(domain)) {
        const auto value = phases_->snapshot(phase);
        SystemVerilogUvmForeignRecord record;
        record.value.kind = FSIM_UVM_FOREIGN_PHASE;
        record.value.state = static_cast<std::uint32_t>(value.state);
        record.value.value = value.registration_order;
        record.value.auxiliary = value.roots.size();
        record.identity = domain_value.identity + "." + value.identity;
        append_field(record.detail, "execution",
                     value.execution == SystemVerilogUvmPhaseExecutionKind::Task
                         ? "task"
                         : "function");
        append_number(record.detail, "kind",
                      static_cast<std::uint32_t>(value.kind));
        append_number(record.detail, "traversal",
                      static_cast<std::uint32_t>(value.traversal));
        append_field(record.detail, "parent",
                     value.parent ? phase_identity(*value.parent)
                                  : std::string{});
        for (const auto &predecessor : value.predecessors) {
          append_field(record.detail, "predecessor",
                       phase_identity(predecessor));
        }
        for (const auto &successor : value.successors) {
          append_field(record.detail, "successor", phase_identity(successor));
        }
        for (const auto &synchronized : value.synchronized) {
          append_field(record.detail, "synchronized",
                       phase_identity(synchronized));
        }
        for (const auto root : value.roots) {
          append_number(record.detail, "root", root);
        }
        append(std::move(record));
      }
    }
    for (const auto &process : phases_->processes()) {
      const auto value = phases_->process_snapshot(process);
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_PHASE_PROCESS;
      record.value.state = static_cast<std::uint32_t>(value.state);
      record.value.root = value.root;
      record.value.value = value.component;
      record.value.auxiliary = value.depth;
      record.identity = phases_->snapshot(value.phase).identity;
      record.detail = value.scheduled ? "scheduled" : "direct";
      append(std::move(record));
    }
    for (const auto &value : objections_->snapshots()) {
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_OBJECTION;
      record.value.state = static_cast<std::uint32_t>(value.kind);
      record.value.root = value.root;
      record.value.value = value.count;
      record.value.auxiliary =
          value.source != 0 ? value.source : value.component;
      record.identity = phase_slot_identity(value.phase_slot);
      record.detail = value.description;
      append(std::move(record));
    }
    for (const auto &value : objections_->drain_snapshots()) {
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_DRAIN;
      record.value.state = value.pending ? 1U : 0U;
      record.value.root = value.root;
      record.value.value = value.delay;
      record.value.auxiliary = value.due;
      record.identity = phase_slot_identity(value.phase_slot);
      record.detail = std::to_string(value.source);
      append(std::move(record));
    }
    for (const auto &endpoint : tlm1_->endpoints()) {
      const auto value = tlm1_->snapshot(endpoint);
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_TLM1_ENDPOINT;
      record.value.state = static_cast<std::uint32_t>(value.kind);
      record.value.root = value.root;
      record.value.value = value.resolved.size();
      record.value.auxiliary = value.declaration_order;
      record.identity = value.debug_name;
      append_number(record.detail, "kind",
                    static_cast<std::uint32_t>(value.kind));
      append_number(record.detail, "interface",
                    static_cast<std::uint32_t>(value.profile.interface_kind));
      append_number(record.detail, "direction",
                    static_cast<std::uint32_t>(value.profile.direction));
      append_field(record.detail, "request", value.profile.request_type);
      append_field(record.detail, "response", value.profile.response_type);
      append_number(record.detail, "minimum", value.minimum_connections);
      append_number(record.detail, "maximum", value.maximum_connections);
      for (const auto &target : value.outbound) {
        append_field(record.detail, "outbound", tlm1_identity(target));
      }
      for (const auto &source : value.inbound) {
        append_field(record.detail, "inbound", tlm1_identity(source));
      }
      for (const auto &resolved : value.resolved) {
        append_field(record.detail, "resolved", tlm1_identity(resolved));
      }
      append(std::move(record));
    }
    for (const auto &value : tlm1_->fifo_snapshots()) {
      const auto endpoint = tlm1_->snapshot(value.implementation);
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_TLM1_FIFO;
      record.value.root = endpoint.root;
      record.value.value = value.size;
      record.value.auxiliary = value.capacity;
      record.identity = endpoint.debug_name;
      append_number(record.detail, "pending-put", value.pending_puts);
      append_number(record.detail, "pending-get", value.pending_gets);
      append_number(record.detail, "pending-peek", value.pending_peeks);
      const auto values = tlm1_->fifo_values(value.implementation);
      append_binary_u64(record.payload, values.size());
      for (const auto &payload : values) {
        append_binary_string(record.payload, payload.nominal_type);
        append_binary_string(record.payload, payload.value.to_msb_string());
        append_binary_u64(record.payload, payload.owner_root);
        append_binary_u64(record.payload, payload.sequence);
      }
      append(std::move(record));
    }
    for (const auto &operation : tlm1_->operations()) {
      const auto value = tlm1_->operation_snapshot(operation);
      const auto endpoint = tlm1_->snapshot(value.source);
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_TLM1_OPERATION;
      record.value.state = static_cast<std::uint32_t>(value.state);
      record.value.root = endpoint.root;
      record.value.value = value.reservation_order;
      record.value.auxiliary = value.completion_time;
      record.identity = endpoint.debug_name;
      append_number(record.detail, "kind",
                    static_cast<std::uint32_t>(value.kind));
      append_field(record.detail, "implementation",
                   tlm1_identity(value.implementation));
      append_field(record.detail, "phase",
                   value.phase ? phase_identity(value.phase) : std::string{});
      append_binary_u64(record.payload,
                        static_cast<std::uint64_t>(value.request.has_value()));
      append_binary_u64(record.payload,
                        static_cast<std::uint64_t>(value.response.has_value()));
      for (const auto *payload :
           {value.request ? &*value.request : nullptr,
            value.response ? &*value.response : nullptr}) {
        if (!payload)
          continue;
        append_binary_string(record.payload, payload->nominal_type);
        append_binary_string(record.payload, payload->value.to_msb_string());
        append_binary_u64(record.payload, payload->owner_root);
        append_binary_u64(record.payload, payload->sequence);
      }
      append(std::move(record));
    }
    for (const auto &socket : tlm2_->sockets()) {
      const auto value = tlm2_->snapshot(socket);
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_TLM2_SOCKET;
      record.value.state = static_cast<std::uint32_t>(value.kind);
      record.value.root = value.root;
      record.value.value = value.resolved_targets.size();
      record.value.auxiliary = value.declaration_order;
      record.identity = value.debug_name;
      append_number(record.detail, "kind",
                    static_cast<std::uint32_t>(value.kind));
      append_number(record.detail, "protocol",
                    static_cast<std::uint32_t>(value.profile.protocol));
      append_field(record.detail, "payload", value.profile.payload_type);
      append_field(record.detail, "phase", value.profile.phase_type);
      append_number(record.detail, "bus-width", value.profile.bus_width_bytes);
      append_number(record.detail, "minimum", value.minimum_connections);
      append_number(record.detail, "maximum", value.maximum_connections);
      for (const auto &target : value.outbound) {
        append_field(record.detail, "outbound", tlm2_identity(target));
      }
      for (const auto &source : value.inbound) {
        append_field(record.detail, "inbound", tlm2_identity(source));
      }
      for (const auto &resolved : value.resolved_targets) {
        append_field(record.detail, "resolved", tlm2_identity(resolved));
      }
      append(std::move(record));
    }
    for (const auto &transaction : tlm2_->transactions()) {
      const auto value = tlm2_->transaction_snapshot(transaction);
      const auto socket = tlm2_->snapshot(value.initiator);
      SystemVerilogUvmForeignRecord record;
      record.value.kind = FSIM_UVM_FOREIGN_TLM2_TRANSACTION;
      record.value.state = static_cast<std::uint32_t>(value.state);
      record.value.root = value.payload.owner_root;
      record.value.value = value.payload.transaction_id;
      record.value.auxiliary = value.delay;
      record.identity = socket.debug_name;
      append_field(record.detail, "payload", value.payload.nominal_type);
      append_number(record.detail, "command",
                    static_cast<std::uint32_t>(value.payload.command));
      append_number(record.detail, "address", value.payload.address);
      append_field(record.detail, "byte-enables",
                   hex_bytes(value.payload.byte_enables));
      append_number(record.detail, "streaming-width",
                    value.payload.streaming_width);
      append_number(record.detail, "response",
                    static_cast<std::uint32_t>(value.payload.response_status));
      append_number(record.detail, "dmi", value.payload.dmi_allowed ? 1U : 0U);
      append_field(record.detail, "target", tlm2_identity(value.target));
      append_field(record.detail, "phase", value.phase.identity);
      append_field(record.detail, "phase-type", value.phase.nominal_type);
      append_number(record.detail, "phase-kind",
                    static_cast<std::uint32_t>(value.phase.kind));
      append_number(record.detail, "last-update", value.last_update_time);
      append_number(record.detail, "hops", value.hops);
      append_number(record.detail, "callbacks", value.callbacks);
      for (const auto &[name, extension] : value.payload.extensions) {
        append_field(record.detail, "extension-name", name);
        append_field(record.detail, "extension-data", hex_bytes(extension));
      }
      record.payload = value.payload.data;
      append(std::move(record));
    }
    for (auto &record : integrated_records(
             limits_.maximum_records - captured.records.size(),
             limits_.maximum_text_bytes - text_bytes)) {
      append(std::move(record));
    }
    captured.value.record_count = captured.records.size();
    result = captured.value;
    snapshots_.emplace(next_generation_++, std::move(captured));
    set_error({}, {});
    return FSIM_UVM_FOREIGN_OK;
  } catch (const std::length_error &error) {
    set_error("FSIM-UVM-FOREIGN-002", error.what());
    return FSIM_UVM_FOREIGN_RESOURCE_LIMIT;
  } catch (const std::exception &error) {
    set_error("FSIM-UVM-FOREIGN-001", error.what());
    return FSIM_UVM_FOREIGN_INTERNAL_ERROR;
  } catch (...) {
    set_error("FSIM-UVM-FOREIGN-001", "unknown UVM foreign snapshot failure");
    return FSIM_UVM_FOREIGN_INTERNAL_ERROR;
  }
}

fsim_uvm_foreign_status_v1 SystemVerilogUvmForeignService::copy_record(
    const fsim_uvm_foreign_snapshot_v1 &snapshot, const std::size_t index,
    fsim_uvm_foreign_record_v1 &result, const std::span<char> identity,
    const std::span<char> detail,
    const std::span<std::uint8_t> payload) const noexcept {
  if (snapshot.abi_version != FSIM_UVM_FOREIGN_ABI_VERSION ||
      snapshot.struct_size < sizeof(fsim_uvm_foreign_snapshot_v1)) {
    set_error("FSIM-UVM-FOREIGN-001", "invalid UVM foreign snapshot layout");
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  }
  if (snapshot.simulation_identity != simulation_identity_) {
    set_error("FSIM-UVM-FOREIGN-001",
              "UVM foreign snapshot belongs to another simulation");
    return FSIM_UVM_FOREIGN_WRONG_SIMULATION;
  }
  const auto found = snapshots_.find(snapshot.generation);
  if (found == snapshots_.end()) {
    set_error("FSIM-UVM-FOREIGN-001", "UVM foreign snapshot handle is stale");
    return FSIM_UVM_FOREIGN_STALE_HANDLE;
  }
  if (index >= found->second.records.size()) {
    set_error("FSIM-UVM-FOREIGN-001",
              "UVM foreign record index is out of range");
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  }
  const auto &record = found->second.records[index];
  result = record.value;
  if (identity.size() < record.identity.size() ||
      detail.size() < record.detail.size() ||
      payload.size() < record.payload.size()) {
    set_error("FSIM-UVM-FOREIGN-002", "UVM foreign caller buffer is too small");
    return FSIM_UVM_FOREIGN_BUFFER_TOO_SMALL;
  }
  std::ranges::copy(record.identity, identity.begin());
  std::ranges::copy(record.detail, detail.begin());
  std::ranges::copy(record.payload, payload.begin());
  set_error({}, {});
  return FSIM_UVM_FOREIGN_OK;
}

fsim_uvm_foreign_status_v1 SystemVerilogUvmForeignService::release(
    const fsim_uvm_foreign_snapshot_v1 &snapshot) noexcept {
  if (snapshot.abi_version != FSIM_UVM_FOREIGN_ABI_VERSION ||
      snapshot.struct_size < sizeof(fsim_uvm_foreign_snapshot_v1)) {
    set_error("FSIM-UVM-FOREIGN-001", "invalid UVM foreign snapshot layout");
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  }
  if (snapshot.simulation_identity != simulation_identity_) {
    set_error("FSIM-UVM-FOREIGN-001",
              "UVM foreign snapshot belongs to another simulation");
    return FSIM_UVM_FOREIGN_WRONG_SIMULATION;
  }
  if (snapshots_.erase(snapshot.generation) != 1) {
    set_error("FSIM-UVM-FOREIGN-001", "UVM foreign snapshot handle is stale");
    return FSIM_UVM_FOREIGN_STALE_HANDLE;
  }
  set_error({}, {});
  return FSIM_UVM_FOREIGN_OK;
}

fsim_uvm_foreign_status_v1 SystemVerilogUvmForeignService::add_callback(
    const fsim_uvm_foreign_activity_callback_v1 callback, void *const context,
    std::uint64_t &token) noexcept {
  if (!callback)
    return FSIM_UVM_FOREIGN_INVALID_ARGUMENT;
  if (callbacks_.size() >= limits_.maximum_callbacks || next_callback_ == 0) {
    return FSIM_UVM_FOREIGN_RESOURCE_LIMIT;
  }
  try {
    const auto foreign_token = next_callback_++;
    const auto activity_token =
        activity_->add_observer([callback, context](const auto &source) {
          const fsim_uvm_foreign_activity_v1 event{
              sizeof(fsim_uvm_foreign_activity_v1),
              static_cast<std::uint32_t>(source.kind),
              static_cast<std::uint32_t>(source.action),
              0,
              source.root,
              source.value,
              source.time,
              source.delta,
              source.sequence,
              source.identity.size(),
              source.identity.data(),
              source.detail.size(),
              source.detail.data()};
          if (callback(context, &event) != FSIM_UVM_FOREIGN_OK) {
            throw std::runtime_error{"UVM foreign activity callback failed"};
          }
        });
    callbacks_.emplace(foreign_token, activity_token);
    token = foreign_token;
    return FSIM_UVM_FOREIGN_OK;
  } catch (...) {
    return FSIM_UVM_FOREIGN_RESOURCE_LIMIT;
  }
}

fsim_uvm_foreign_status_v1 SystemVerilogUvmForeignService::remove_callback(
    const std::uint64_t token) noexcept {
  const auto found = callbacks_.find(token);
  if (found == callbacks_.end())
    return FSIM_UVM_FOREIGN_STALE_HANDLE;
  activity_->remove_observer(found->second);
  callbacks_.erase(found);
  return FSIM_UVM_FOREIGN_OK;
}

fsim_uvm_foreign_host_v1 make_systemverilog_uvm_foreign_host(
    SystemVerilogUvmForeignService &service) noexcept {
  return {FSIM_UVM_FOREIGN_ABI_VERSION,
          sizeof(fsim_uvm_foreign_host_v1),
          service.simulation_identity(),
          &service,
          host_capture,
          host_copy_record,
          host_release,
          host_add_callback,
          host_remove_callback};
}

} // namespace fsim::runtime
