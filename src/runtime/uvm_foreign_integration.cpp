// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_foreign.hpp"

#include <stdexcept>
#include <utility>

namespace fsim::runtime {
namespace {

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

SystemVerilogUvmForeignRecord make_record(
    const std::uint32_t kind, std::string identity,
    const SystemVerilogUvmRootHandle root = 0) {
  SystemVerilogUvmForeignRecord result;
  result.value.kind = kind;
  result.value.root = root;
  result.identity = std::move(identity);
  return result;
}

} // namespace

std::vector<SystemVerilogUvmForeignRecord>
SystemVerilogUvmForeignService::integrated_records(
    const std::size_t maximum_records,
    const std::size_t maximum_text_bytes) const {
  std::vector<SystemVerilogUvmForeignRecord> result;
  std::size_t text_bytes{};
  const auto append = [&](SystemVerilogUvmForeignRecord record) {
    if (result.size() == maximum_records ||
        record.identity.size() > maximum_text_bytes - text_bytes ||
        record.detail.size() > maximum_text_bytes - text_bytes -
                                   record.identity.size()) {
      throw std::length_error{
          "UVM foreign integration snapshot resource ceiling exceeded"};
    }
    text_bytes += record.identity.size() + record.detail.size();
    result.push_back(std::move(record));
  };
  if (sequences_) {
    for (const auto &handle : sequences_->sequencers()) {
      const auto value = sequences_->snapshot(handle);
      auto record = make_record(FSIM_UVM_FOREIGN_SEQUENCER,
                                value.debug_name, value.root);
      record.value.state = static_cast<std::uint32_t>(value.state);
      record.value.value = value.declaration_order;
      record.value.auxiliary = value.requests.size();
      append_field(record.detail, "type", value.nominal_type);
      append_field(record.detail, "request", value.profile.request_type);
      append_field(record.detail, "response", value.profile.response_type);
      append_number(record.detail, "arbitration",
                    static_cast<std::uint32_t>(value.arbitration));
      append_number(record.detail, "seed", value.random_seed);
      append_number(record.detail, "draws", value.random_draws);
      append_number(record.detail, "transactions", value.transactions.size());
      append(std::move(record));
    }
    for (const auto &handle : sequences_->sequences()) {
      const auto value = sequences_->snapshot(handle);
      auto record = make_record(FSIM_UVM_FOREIGN_SEQUENCE,
                                value.full_name, value.root);
      record.value.state = static_cast<std::uint32_t>(value.state);
      record.value.value = value.execution_count;
      record.value.auxiliary = value.depth;
      append_field(record.detail, "type", value.nominal_type);
      append_field(record.detail, "request", value.profile.request_type);
      append_field(record.detail, "response", value.profile.response_type);
      append_number(record.detail, "order", value.declaration_order);
      append_number(record.detail, "children", value.children.size());
      append_number(record.detail, "items", value.items.size());
      append_number(record.detail, "responses", value.responses.size());
      append_number(record.detail, "virtual", value.is_virtual ? 1U : 0U);
      append(std::move(record));
    }
    for (const auto &handle : sequences_->items()) {
      const auto value = sequences_->snapshot(handle);
      auto record = make_record(FSIM_UVM_FOREIGN_SEQUENCE_ITEM,
                                value.full_name, value.root);
      record.value.state = static_cast<std::uint32_t>(value.state);
      record.value.value = value.declaration_order;
      record.value.auxiliary = static_cast<std::uint32_t>(value.role);
      append_field(record.detail, "type", value.nominal_type);
      append(std::move(record));
    }
  }
  if (uvm_callbacks_) {
    for (const auto &value : uvm_callbacks_->snapshots()) {
      auto record = make_record(
          FSIM_UVM_FOREIGN_CALLBACK,
          value.target_type + ":" + value.callback_type + ":" + value.name);
      record.value.state = static_cast<std::uint32_t>(value.scope);
      record.value.value = value.invocations;
      record.value.auxiliary = value.order;
      append_number(record.detail, "instance", value.instance);
      append_number(record.detail, "mask", value.mask);
      append(std::move(record));
    }
  }
  if (transactions_) {
    for (const auto &value : transactions_->snapshots()) {
      auto record = make_record(
          FSIM_UVM_FOREIGN_TRANSACTION,
          value.stream + ":" + value.name + ":" +
              std::to_string(value.identity),
          value.root);
      record.value.state = static_cast<std::uint32_t>(value.state);
      record.value.value = value.identity;
      record.value.auxiliary = value.attributes.size();
      append_field(record.detail, "kind", value.kind);
      append_number(record.detail, "parent", value.parent_identity.value_or(0));
      append_number(record.detail, "links", value.links.size());
      append_number(record.detail, "begin-time", value.begin_time);
      append_number(record.detail, "begin-delta", value.begin_delta);
      append_number(record.detail, "end-time", value.end_time);
      append_number(record.detail, "end-delta", value.end_delta);
      append(std::move(record));
    }
  }
  if (!register_model_) {
    return result;
  }
  for (const auto &value : register_model_->blocks()) {
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER_BLOCK,
                              value.full_name, value.root);
    record.value.state = static_cast<std::uint32_t>(value.state);
    record.value.value = value.identity;
    record.value.auxiliary = value.declaration_order;
    append_number(record.detail, "depth", value.depth);
    append(std::move(record));
  }
  for (const auto &value : register_model_->maps()) {
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER_MAP,
                              value.full_name, value.root);
    record.value.state = static_cast<std::uint32_t>(value.endianness);
    record.value.value = value.identity;
    record.value.auxiliary = value.base_offset;
    append_number(record.detail, "bus-width", value.bus_width_bytes);
    append_number(record.detail, "byte-addressing",
                  value.byte_addressing ? 1U : 0U);
    append_number(record.detail, "order", value.declaration_order);
    append(std::move(record));
  }
  for (const auto &value : register_model_->registers()) {
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER,
                              value.full_name, value.root);
    record.value.value = value.identity;
    record.value.auxiliary = value.offset;
    append_number(record.detail, "width", value.width_bits);
    append_number(record.detail, "order", value.declaration_order);
    const auto state = register_model_->value_snapshot(value.handle);
    append_field(record.detail, "desired", state.desired.to_msb_string());
    append_field(record.detail, "mirrored", state.mirrored.to_msb_string());
    append_number(record.detail, "needs-update", state.needs_update ? 1U : 0U);
    append(std::move(record));
  }
  for (const auto &value : register_model_->fields()) {
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER_FIELD,
                              value.full_name, value.root);
    record.value.state = static_cast<std::uint32_t>(value.access);
    record.value.value = value.identity;
    record.value.auxiliary = value.least_significant_bit;
    append_number(record.detail, "width", value.width_bits);
    append_number(record.detail, "volatile", value.is_volatile ? 1U : 0U);
    append_number(record.detail, "compare",
                  static_cast<std::uint32_t>(value.compare));
    const auto state = register_model_->value_snapshot(value.handle);
    append_field(record.detail, "desired", state.desired.to_msb_string());
    append_field(record.detail, "mirrored", state.mirrored.to_msb_string());
    for (const auto &[name, reset] : state.reset_values) {
      append_field(record.detail, "reset-kind", name);
      append_field(record.detail, "reset-value", reset.to_msb_string());
    }
    append(std::move(record));
  }
  for (const auto &value : register_model_->memories()) {
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER_MEMORY,
                              value.full_name, value.root);
    record.value.state = static_cast<std::uint32_t>(value.access);
    record.value.value = value.identity;
    record.value.auxiliary = value.word_count;
    append_number(record.detail, "width", value.word_width_bits);
    append_number(record.detail, "offset", value.offset);
    for (const auto extent : value.dimensions) {
      append_number(record.detail, "dimension", extent);
    }
    append(std::move(record));
  }
  for (const auto &value : register_model_->standard_sequences()) {
    const auto block = register_model_->snapshot(value.top);
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER_SEQUENCE,
                              block.full_name, block.root);
    record.value.state = value.success() ? 1U : 0U;
    record.value.value = value.execution_order;
    record.value.auxiliary = value.final_random_state;
    append_number(record.detail, "kind",
                  static_cast<std::uint32_t>(value.kind));
    append_field(record.detail, "reset", value.reset_kind);
    append_number(record.detail, "seed", value.seed);
    append_number(record.detail, "operations", value.operations);
    append_number(record.detail, "reads", value.reads);
    append_number(record.detail, "writes", value.writes);
    append_number(record.detail, "comparisons", value.comparisons);
    for (const auto &failure : value.failures) {
      append_field(record.detail, "failure-target", failure.target);
      append_field(record.detail, "failure-message", failure.message);
    }
    append(std::move(record));
  }
  for (const auto &value : register_model_->register_callbacks()) {
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER_CALLBACK, value.name);
    record.value.state = static_cast<std::uint32_t>(value.scope);
    record.value.value = value.invocations;
    record.value.auxiliary = value.failures;
    append_number(record.detail, "phases", value.phases);
    append_number(record.detail, "priority", value.priority);
    append_number(record.detail, "order", value.registration_order);
    append(std::move(record));
  }
  for (const auto &value : register_model_->coverage_models()) {
    const auto block = register_model_->snapshot(value.block);
    auto record = make_record(FSIM_UVM_FOREIGN_REGISTER_COVERAGE,
                              block.full_name + ":" + value.name, block.root);
    record.value.value = value.samples;
    record.value.auxiliary = value.bins.size();
    append_number(record.detail, "order", value.registration_order);
    for (const auto &[name, count] : value.bins) {
      append_field(record.detail, "bin", name);
      append_number(record.detail, "hits", count);
    }
    append(std::move(record));
  }
  return result;
}

} // namespace fsim::runtime
