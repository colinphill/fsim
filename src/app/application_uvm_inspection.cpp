// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <limits>
#include <sstream>
#include <type_traits>

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

void add_text_bytes(
    std::size_t& total,
    const std::string_view text,
    const std::size_t maximum) {
  if (text.size() > maximum - total) {
    fail(kResourceLimit, "UVM debug payload-byte ceiling exceeded");
  }
  total += text.size();
}

void add_attribute_bytes(
    std::size_t& total,
    const runtime::SystemVerilogUvmTransactionAttribute& attribute,
    const std::size_t maximum) {
  add_text_bytes(total, attribute.name, maximum);
  std::visit(
      [&](const auto& value) {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::is_same_v<Value, std::string>) {
          add_text_bytes(total, value, maximum);
        } else {
          if (sizeof(value) > maximum - total) {
            fail(kResourceLimit, "UVM debug payload-byte ceiling exceeded");
          }
          total += sizeof(value);
        }
      },
      attribute.value);
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
  const auto callback_revision = uvm_callbacks().mutation_count();
  const auto transaction_revision = uvm_transactions().mutation_count();
  const auto sequence_revision = uvm_sequences().mutation_count();
  const auto register_revision = uvm_register_model().mutation_count();
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
    result.callbacks = uvm_callbacks().snapshots();
    reserve(result.callbacks.size());
    for (const auto& callback : result.callbacks) {
      add_text_bytes(payload_bytes, callback.target_type,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, callback.callback_type,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, callback.name,
                     limits.maximum_payload_bytes);
    }
    result.callback_failures.assign(
        uvm_callbacks().failures().begin(), uvm_callbacks().failures().end());
    reserve(result.callback_failures.size());
    for (const auto& failure : result.callback_failures) {
      add_text_bytes(payload_bytes, failure.diagnostic_code,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, failure.message,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, failure.callback.target_type,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, failure.callback.callback_type,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, failure.callback.name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, failure.invocation.target_type,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, failure.invocation.callback_type,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, failure.invocation.operation,
                     limits.maximum_payload_bytes);
      for (const auto& [name, value] : failure.invocation.attributes) {
        add_text_bytes(payload_bytes, name, limits.maximum_payload_bytes);
        add_text_bytes(payload_bytes, value, limits.maximum_payload_bytes);
      }
    }
    result.transactions = uvm_transactions().snapshots();
    reserve(result.transactions.size());
    for (const auto& transaction : result.transactions) {
      add_text_bytes(payload_bytes, transaction.name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, transaction.stream,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, transaction.kind,
                     limits.maximum_payload_bytes);
      for (const auto& attribute : transaction.attributes) {
        add_attribute_bytes(
            payload_bytes, attribute, limits.maximum_payload_bytes);
      }
      for (const auto& link : transaction.links) {
        add_text_bytes(payload_bytes, link.relation,
                       limits.maximum_payload_bytes);
      }
    }
    result.transaction_trace_records.assign(
        uvm_transactions().trace_records().begin(),
        uvm_transactions().trace_records().end());
    reserve(result.transaction_trace_records.size());
    for (const auto& record : result.transaction_trace_records) {
      add_text_bytes(payload_bytes, record.name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.value,
                     limits.maximum_payload_bytes);
    }
    for (const auto& sequencer : uvm_sequences().sequencers()) {
      reserve(1);
      auto value = uvm_sequences().snapshot(sequencer);
      add_text_bytes(payload_bytes, value.debug_name,
                     limits.maximum_payload_bytes);
      result.sequencers.push_back(std::move(value));
    }
    for (const auto& sequence : uvm_sequences().sequences()) {
      reserve(1);
      auto value = uvm_sequences().snapshot(sequence);
      add_text_bytes(payload_bytes, value.full_name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, value.nominal_type,
                     limits.maximum_payload_bytes);
      result.sequences.push_back(std::move(value));
    }
    for (const auto& item : uvm_sequences().items()) {
      reserve(1);
      auto value = uvm_sequences().snapshot(item);
      add_text_bytes(payload_bytes, value.full_name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, value.nominal_type,
                     limits.maximum_payload_bytes);
      result.sequence_items.push_back(std::move(value));
    }
    result.factory_trace_records.assign(
        uvm_factory().trace_records().begin(),
        uvm_factory().trace_records().end());
    reserve(result.factory_trace_records.size());
    for (const auto& record : result.factory_trace_records) {
      add_text_bytes(payload_bytes, record.resolution.requested_name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.resolution.full_instance_path,
                     limits.maximum_payload_bytes);
      for (const auto& step : record.resolution.steps) {
        add_text_bytes(payload_bytes, step.original_name,
                       limits.maximum_payload_bytes);
        add_text_bytes(payload_bytes, step.instance_pattern,
                       limits.maximum_payload_bytes);
      }
    }
    result.resource_trace_records.assign(
        uvm_resources().trace_records().begin(),
        uvm_resources().trace_records().end());
    reserve(result.resource_trace_records.size());
    for (const auto& record : result.resource_trace_records) {
      add_text_bytes(payload_bytes, record.scope,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.type_identity,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.accessor,
                     limits.maximum_payload_bytes);
    }
    result.config_trace_records.assign(
        uvm_config_db().trace_records().begin(),
        uvm_config_db().trace_records().end());
    reserve(result.config_trace_records.size());
    for (const auto& record : result.config_trace_records) {
      add_text_bytes(payload_bytes, record.context_name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.instance_name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.field_name,
                     limits.maximum_payload_bytes);
      add_text_bytes(payload_bytes, record.type_identity,
                     limits.maximum_payload_bytes);
    }
    result.register_blocks = uvm_register_model().blocks();
    reserve(result.register_blocks.size());
    result.register_maps = uvm_register_model().maps();
    reserve(result.register_maps.size());
    result.registers = uvm_register_model().registers();
    reserve(result.registers.size());
    result.register_fields = uvm_register_model().fields();
    reserve(result.register_fields.size());
    result.register_memories = uvm_register_model().memories();
    reserve(result.register_memories.size());
    result.register_sequences = uvm_register_model().standard_sequences();
    reserve(result.register_sequences.size());
    result.register_callbacks = uvm_register_model().register_callbacks();
    reserve(result.register_callbacks.size());
    result.register_coverage = uvm_register_model().coverage_models();
    reserve(result.register_coverage.size());
    for (const auto& value : result.register_blocks) {
      add_text_bytes(payload_bytes, value.full_name,
                     limits.maximum_payload_bytes);
    }
    for (const auto& value : result.register_maps) {
      add_text_bytes(payload_bytes, value.full_name,
                     limits.maximum_payload_bytes);
    }
    for (const auto& value : result.registers) {
      add_text_bytes(payload_bytes, value.full_name,
                     limits.maximum_payload_bytes);
    }
    for (const auto& value : result.register_fields) {
      add_text_bytes(payload_bytes, value.full_name,
                     limits.maximum_payload_bytes);
    }
    for (const auto& value : result.register_memories) {
      add_text_bytes(payload_bytes, value.full_name,
                     limits.maximum_payload_bytes);
    }
    for (const auto& value : result.register_sequences) {
      add_text_bytes(payload_bytes, value.reset_kind,
                     limits.maximum_payload_bytes);
      for (const auto& failure : value.failures) {
        add_text_bytes(payload_bytes, failure.target,
                       limits.maximum_payload_bytes);
        add_text_bytes(payload_bytes, failure.message,
                       limits.maximum_payload_bytes);
      }
    }
    for (const auto& value : result.register_callbacks) {
      add_text_bytes(payload_bytes, value.name,
                     limits.maximum_payload_bytes);
    }
    for (const auto& value : result.register_coverage) {
      add_text_bytes(payload_bytes, value.name,
                     limits.maximum_payload_bytes);
      for (const auto& [name, count] : value.bins) {
        (void)count;
        add_text_bytes(payload_bytes, name, limits.maximum_payload_bytes);
      }
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
      || uvm_tlm2().mutation_count() != tlm2_revision
      || uvm_callbacks().mutation_count() != callback_revision
      || uvm_transactions().mutation_count() != transaction_revision
      || uvm_sequences().mutation_count() != sequence_revision
      || uvm_register_model().mutation_count() != register_revision) {
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
        snapshot.tlm2_transactions.size(), " callbacks ",
        snapshot.callbacks.size(), "/", snapshot.callback_failures.size(),
        " transactions ", snapshot.transactions.size(), "/",
        snapshot.transaction_trace_records.size(), " sequences ",
        snapshot.sequencers.size(), "/", snapshot.sequences.size(), "/",
        snapshot.sequence_items.size(), " configuration ",
        snapshot.factory_trace_records.size(), "/",
        snapshot.resource_trace_records.size(), "/",
        snapshot.config_trace_records.size(), " register-model ",
        snapshot.register_blocks.size(), "/", snapshot.registers.size(), "/",
        snapshot.register_fields.size(), "/",
        snapshot.register_memories.size());
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
  if (selected(UvmDebugSection::callbacks)) {
    for (const auto& value : snapshot.callbacks) {
      append(
          "callback ", value.target_type, ":", value.callback_type, ":",
          value.name, " scope ", static_cast<unsigned>(value.scope),
          " instance ", value.instance, " mask ", value.mask,
          " invocations ", value.invocations);
    }
    for (const auto& value : snapshot.callback_failures) {
      append(
          "callback failure ", value.callback.name, " code ",
          value.diagnostic_code, " operation ", value.invocation.operation,
          " message ", value.message);
    }
  }
  if (selected(UvmDebugSection::transactions)) {
    for (const auto& value : snapshot.transactions) {
      append(
          "transaction ", value.identity, " stream ", value.stream,
          " name ", value.name, " state ",
          static_cast<unsigned>(value.state), " parent ",
          value.parent_identity.value_or(0), " attributes ",
          value.attributes.size(), " links ", value.links.size(), " time ",
          value.begin_time, ":", value.begin_delta, "-", value.end_time,
          ":", value.end_delta);
    }
    for (const auto& value : snapshot.transaction_trace_records) {
      append(
          "transaction trace ", value.sequence, " kind ",
          static_cast<unsigned>(value.kind), " transaction ",
          value.transaction_identity, " related ",
          value.related_identity.value_or(0), " time ", value.time, ":",
          value.delta, " name ", value.name, " value ", value.value);
    }
  }
  if (selected(UvmDebugSection::sequences)) {
    for (const auto& value : snapshot.sequencers) {
      append("sequencer ", value.debug_name, " state ",
             static_cast<unsigned>(value.state), " arbitration ",
             static_cast<unsigned>(value.arbitration), " requests ",
             value.requests.size(), " transactions ",
             value.transactions.size());
    }
    for (const auto& value : snapshot.sequences) {
      append("sequence ", value.full_name, " state ",
             static_cast<unsigned>(value.state), " executions ",
             value.execution_count, " children ", value.children.size(),
             " items ", value.items.size());
    }
    for (const auto& value : snapshot.sequence_items) {
      append("sequence item ", value.full_name, " state ",
             static_cast<unsigned>(value.state), " role ",
             static_cast<unsigned>(value.role), " type ",
             value.nominal_type);
    }
  }
  if (selected(UvmDebugSection::configuration)) {
    for (const auto& value : snapshot.factory_trace_records) {
      append("factory trace ", value.sequence, " mode ",
             value.debug ? "debug" : "create", " requested ",
             value.resolution.requested_name, " path ",
             value.resolution.full_instance_path, " resolved ",
             value.resolution.resolved, " steps ",
             value.resolution.steps.size());
    }
    for (const auto& value : snapshot.resource_trace_records) {
      append("resource trace ", value.sequence, " action ",
             static_cast<unsigned>(value.action), " scope ", value.scope,
             " name ", value.name, " type ", value.type_identity,
             " selected ", value.selected, " matches ", value.match_count,
             " success ", value.success);
    }
    for (const auto& value : snapshot.config_trace_records) {
      append("config trace ", value.sequence, " action ",
             static_cast<unsigned>(value.action), " context ",
             value.context_name, " instance ", value.instance_name,
             " field ", value.field_name, " type ", value.type_identity,
             " resource ", value.resource, " success ", value.success);
    }
  }
  if (selected(UvmDebugSection::register_model)) {
    for (const auto& value : snapshot.register_blocks) {
      append("register block ", value.full_name, " state ",
             static_cast<unsigned>(value.state), " depth ", value.depth);
    }
    for (const auto& value : snapshot.register_maps) {
      append("register map ", value.full_name, " base ", value.base_offset,
             " bus-width ", value.bus_width_bytes, " endian ",
             static_cast<unsigned>(value.endianness));
    }
    for (const auto& value : snapshot.registers) {
      append("register ", value.full_name, " width ", value.width_bits,
             " offset ", value.offset);
    }
    for (const auto& value : snapshot.register_fields) {
      append("register field ", value.full_name, " width ",
             value.width_bits, " lsb ", value.least_significant_bit,
             " access ", static_cast<unsigned>(value.access));
    }
    for (const auto& value : snapshot.register_memories) {
      append("register memory ", value.full_name, " width ",
             value.word_width_bits, " words ", value.word_count);
    }
    for (const auto& value : snapshot.register_sequences) {
      append("register sequence kind ", static_cast<unsigned>(value.kind),
             " order ", value.execution_order, " operations ",
             value.operations, " failures ", value.failures.size());
    }
    for (const auto& value : snapshot.register_callbacks) {
      append("register callback ", value.name, " scope ",
             static_cast<unsigned>(value.scope), " invocations ",
             value.invocations, " failures ", value.failures);
    }
    for (const auto& value : snapshot.register_coverage) {
      append("register coverage ", value.name, " samples ", value.samples,
             " bins ", value.bins.size());
    }
  }
  return result;
}

}  // namespace fsim::app
