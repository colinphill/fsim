// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <limits>
#include <sstream>

namespace fsim::app {

namespace {

constexpr std::string_view kInvalidSnapshot{"FSIM-UVM-DEBUG-001"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-DEBUG-002"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw UvmDebugError{std::string{code}, std::string{message}};
}

void add_payload_bytes(
    std::size_t& total,
    const runtime::SystemVerilogUvmTlm1Payload& payload,
    const std::size_t maximum) {
  const auto bytes = payload.value.width() != 0
      ? (payload.value.width() + 7U) / 8U
      : sizeof(payload.object);
  if (bytes > maximum - total) {
    fail(kResourceLimit, "UVM debug payload-byte ceiling exceeded");
  }
  total += bytes;
}

void add_payload_bytes(
    std::size_t& total,
    const runtime::SystemVerilogUvmTlm2GenericPayload& payload,
    const std::size_t maximum) {
  std::size_t bytes = payload.data.size();
  if (payload.byte_enables.size()
      > std::numeric_limits<std::size_t>::max() - bytes) {
    fail(kResourceLimit, "UVM debug payload-byte accounting overflow");
  }
  bytes += payload.byte_enables.size();
  for (const auto& [name, value] : payload.extensions) {
    if (name.size() > std::numeric_limits<std::size_t>::max() - bytes
        || value.size()
            > std::numeric_limits<std::size_t>::max() - bytes - name.size()) {
      fail(kResourceLimit, "UVM debug payload-byte accounting overflow");
    }
    bytes += name.size() + value.size();
  }
  if (bytes > maximum - total) {
    fail(kResourceLimit, "UVM debug payload-byte ceiling exceeded");
  }
  total += bytes;
}

}  // namespace

UvmDebugError::UvmDebugError(std::string code, std::string message)
    : std::runtime_error(std::move(message)), code_(std::move(code)) {}

UvmDebugSnapshot Simulation::uvm_debug_snapshot(
    const UvmDebugLimits limits) const {
  if (limits.maximum_records == 0
      || limits.maximum_payload_bytes == 0
      || limits.maximum_formatted_bytes == 0) {
    fail(kResourceLimit, "UVM debug limits must be nonzero");
  }
  const auto start_time = now();
  const auto start_delta = delta();
  const auto phase_revision = uvm_phases().mutation_count();
  const auto objection_revision = uvm_objections().mutation_count();
  const auto tlm1_revision = uvm_tlm1().mutation_count();
  const auto tlm2_revision = uvm_tlm2().mutation_count();
  UvmDebugSnapshot result;
  result.time = start_time;
  result.delta = start_delta;
  std::size_t records{};
  std::size_t payload_bytes{};
  const auto reserve = [&](const std::size_t count) {
    if (count > limits.maximum_records - records) {
      fail(kResourceLimit, "UVM debug record ceiling exceeded");
    }
    records += count;
  };
  try {
    for (const auto& domain : uvm_phases().domains()) {
      reserve(1);
      result.domains.push_back(uvm_phases().snapshot(domain));
      for (const auto& phase : uvm_phases().phases(domain)) {
        reserve(1);
        result.phases.push_back(uvm_phases().snapshot(phase));
      }
    }
    for (const auto& process : uvm_phases().processes()) {
      reserve(1);
      result.processes.push_back(uvm_phases().process_snapshot(process));
    }
    result.objections = uvm_objections().snapshots();
    reserve(result.objections.size());
    result.drains = uvm_objections().drain_snapshots();
    reserve(result.drains.size());
    for (const auto& endpoint : uvm_tlm1().endpoints()) {
      reserve(1);
      result.tlm1_endpoints.push_back(uvm_tlm1().snapshot(endpoint));
    }
    result.tlm1_fifos = uvm_tlm1().fifo_snapshots();
    reserve(result.tlm1_fifos.size());
    for (const auto& operation : uvm_tlm1().operations()) {
      reserve(1);
      auto snapshot = uvm_tlm1().operation_snapshot(operation);
      if (snapshot.request) {
        add_payload_bytes(
            payload_bytes, *snapshot.request, limits.maximum_payload_bytes);
      }
      if (snapshot.response) {
        add_payload_bytes(
            payload_bytes, *snapshot.response, limits.maximum_payload_bytes);
      }
      result.tlm1_operations.push_back(std::move(snapshot));
    }
    for (const auto& socket : uvm_tlm2().sockets()) {
      reserve(1);
      result.tlm2_sockets.push_back(uvm_tlm2().snapshot(socket));
    }
    for (const auto& transaction : uvm_tlm2().transactions()) {
      reserve(1);
      auto snapshot = uvm_tlm2().transaction_snapshot(transaction);
      add_payload_bytes(
          payload_bytes, snapshot.payload, limits.maximum_payload_bytes);
      result.tlm2_transactions.push_back(std::move(snapshot));
    }
  } catch (const UvmDebugError&) {
    throw;
  } catch (const std::exception& error) {
    throw UvmDebugError{
        std::string{kInvalidSnapshot},
        "UVM state changed while a debug snapshot was captured: "
            + std::string{error.what()}};
  }
  if (now() != start_time || delta() != start_delta
      || uvm_phases().mutation_count() != phase_revision
      || uvm_objections().mutation_count() != objection_revision
      || uvm_tlm1().mutation_count() != tlm1_revision
      || uvm_tlm2().mutation_count() != tlm2_revision) {
    fail(kInvalidSnapshot, "UVM state advanced during debug snapshot capture");
  }
  return result;
}

std::string format_uvm_debug_snapshot(
    const UvmDebugSnapshot& snapshot,
    const UvmDebugSection section,
    const std::size_t maximum_bytes) {
  if (maximum_bytes == 0) {
    fail(kResourceLimit, "UVM debug formatting ceiling must be nonzero");
  }
  std::string result;
  const auto append = [&](const auto&... values) {
    std::ostringstream line;
    (line << ... << values);
    line << '\n';
    auto text = std::move(line).str();
    if (text.size() > maximum_bytes - result.size()) {
      fail(kResourceLimit, "UVM debug formatting ceiling exceeded");
    }
    result += text;
  };
  const auto selected = [&](const UvmDebugSection wanted) {
    return section == UvmDebugSection::all || section == wanted;
  };
  if (selected(UvmDebugSection::summary)) {
    append(
        "uvm time ", snapshot.time, " delta ", snapshot.delta,
        " domains ", snapshot.domains.size(), " phases ",
        snapshot.phases.size(), " processes ", snapshot.processes.size(),
        " objections ", snapshot.objections.size(), " drains ",
        snapshot.drains.size(), " tlm1 ", snapshot.tlm1_endpoints.size(),
        "/", snapshot.tlm1_operations.size(), " tlm2 ",
        snapshot.tlm2_sockets.size(), "/",
        snapshot.tlm2_transactions.size());
  }
  if (selected(UvmDebugSection::phases)) {
    for (const auto& value : snapshot.domains) {
      append(
          "domain ", value.identity, " kind ",
          static_cast<unsigned>(value.kind), " roots ", value.roots.size());
    }
    for (const auto& value : snapshot.phases) {
      const auto domain = std::ranges::find(
          snapshot.domains, value.domain,
          &runtime::SystemVerilogUvmDomainSnapshot::handle);
      append(
          "phase ",
          domain == snapshot.domains.end() ? "<stale>" : domain->identity,
          ".", value.identity, " state ",
          static_cast<unsigned>(value.state), " roots ", value.roots.size());
    }
    for (const auto& value : snapshot.processes) {
      append(
          "process root ", value.root, " component ", value.component,
          " state ", static_cast<unsigned>(value.state), " depth ",
          value.depth, " scheduled ", value.scheduled);
    }
  }
  if (selected(UvmDebugSection::objections)) {
    for (const auto& value : snapshot.objections) {
      append(
          "objection phase ", value.phase_slot, " root ", value.root,
          " source ", value.source, " component ", value.component,
          " count ", value.count, " description ", value.description);
    }
    for (const auto& value : snapshot.drains) {
      append(
          "drain phase ", value.phase_slot, " root ", value.root,
          " source ", value.source, " delay ", value.delay, " pending ",
          value.pending, " due ", value.due);
    }
  }
  if (selected(UvmDebugSection::tlm1)) {
    for (const auto& value : snapshot.tlm1_endpoints) {
      append(
          value.debug_name, " root ", value.root, " connections ",
          value.outbound.size(), "/", value.inbound.size(), " resolved ",
          value.resolved.size());
    }
    for (const auto& value : snapshot.tlm1_fifos) {
      append(
          "tlm1 fifo size ", value.size, "/", value.capacity, " pending ",
          value.pending_puts, "/", value.pending_gets, "/",
          value.pending_peeks);
    }
    for (const auto& value : snapshot.tlm1_operations) {
      append(
          "tlm1 operation kind ", static_cast<unsigned>(value.kind),
          " state ", static_cast<unsigned>(value.state), " completion ",
          value.completion_time);
    }
  }
  if (selected(UvmDebugSection::tlm2)) {
    for (const auto& value : snapshot.tlm2_sockets) {
      append(
          value.debug_name, " root ", value.root, " connections ",
          value.outbound.size(), "/", value.inbound.size(), " targets ",
          value.resolved_targets.size());
    }
    for (const auto& value : snapshot.tlm2_transactions) {
      append(
          "tlm2 transaction ", value.payload.transaction_id, " state ",
          static_cast<unsigned>(value.state), " bytes ",
          value.payload.data.size(), " delay ", value.delay, " hops ",
          value.hops, " callbacks ", value.callbacks);
    }
  }
  return result;
}

}  // namespace fsim::app
