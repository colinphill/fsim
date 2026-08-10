// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_callback.hpp"

#include <algorithm>
#include <charconv>
#include <map>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidTransaction{"FSIM-UVM-TR-001"};
constexpr std::string_view kTransactionResource{"FSIM-UVM-TR-002"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmTransactionError{std::string{code},
                                         std::string{message}};
}

[[nodiscard]] std::string recorded_value(
    const SystemVerilogUvmObjectEntry& entry) {
  return std::to_string(static_cast<std::uint8_t>(entry.kind)) + ":" +
      std::to_string(entry.size) + ":" + entry.type_name + ":" + entry.value;
}

}  // namespace

void SystemVerilogUvmTransactionRecorderService::record_object(
    const SystemVerilogUvmTransactionHandle transaction,
    const SystemVerilogClassHandle object,
    std::string prefix) {
  validate_text(prefix);
  auto& selected = entry(transaction);
  if (selected.snapshot.state != SystemVerilogUvmTransactionState::Active) {
    fail(kInvalidTransaction,
         "objects may only be recorded on an active UVM transaction");
  }
  if (!objects_->contains(object)) {
    fail(kInvalidTransaction, "UVM transaction recorder object is stale");
  }
  const auto entries = objects_->record(object);
  if (entries.size() > limits_.maximum_attributes_per_transaction
                           - selected.snapshot.attributes.size()
      || entries.size() > limits_.maximum_trace_records - trace_records_.size()
      || entries.size() > limits_.maximum_mutations - mutations_) {
    fail(kTransactionResource,
         "UVM object recording exceeds transaction retention ceilings");
  }
  const auto saved_snapshot = selected.snapshot;
  const auto saved_trace_size = trace_records_.size();
  const auto saved_trace_sequence = next_trace_sequence_;
  const auto saved_mutations = mutations_;
  try {
    for (const auto& recorded : entries) {
      auto name = prefix.empty() ? recorded.path : prefix + "." + recorded.path;
      record_attribute(
          transaction, {std::move(name), recorded_value(recorded)});
    }
  } catch (...) {
    entry(transaction).snapshot = saved_snapshot;
    trace_records_.resize(saved_trace_size);
    next_trace_sequence_ = saved_trace_sequence;
    mutations_ = saved_mutations;
    throw;
  }
}

std::vector<SystemVerilogUvmTransactionSnapshot>
SystemVerilogUvmTransactionRecorderService::replay_trace(
    const std::span<const SystemVerilogUvmTransactionTraceRecord> records) const {
  if (records.size() > limits_.maximum_trace_records) {
    fail(kTransactionResource, "UVM transaction replay exceeds trace ceiling");
  }
  std::map<std::uint64_t, SystemVerilogUvmTransactionSnapshot> replayed;
  std::uint64_t previous_sequence{};
  for (const auto& record : records) {
    validate_text(record.name);
    validate_text(record.value);
    if (record.sequence == 0 || record.sequence <= previous_sequence) {
      fail(kInvalidTransaction,
           "UVM transaction replay sequence is not strictly increasing");
    }
    previous_sequence = record.sequence;
    if (record.kind == SystemVerilogUvmTransactionTraceKind::Begin) {
      if (record.transaction_identity == 0
          || replayed.contains(record.transaction_identity)) {
        fail(kInvalidTransaction,
             "UVM transaction replay has a duplicate or empty begin");
      }
      const auto separator = record.value.find(':');
      if (record.name.empty() || separator == std::string::npos
          || separator == 0 || separator + 1U == record.value.size()) {
        fail(kInvalidTransaction, "UVM transaction replay begin is malformed");
      }
      SystemVerilogUvmTransactionSnapshot snapshot;
      snapshot.identity = record.transaction_identity;
      snapshot.name = record.name;
      snapshot.stream = record.value.substr(0, separator);
      snapshot.kind = record.value.substr(separator + 1U);
      snapshot.root = record.root;
      snapshot.parent_identity = record.related_identity;
      snapshot.begin_time = record.time;
      snapshot.begin_delta = record.delta;
      replayed.emplace(snapshot.identity, std::move(snapshot));
      continue;
    }
    const auto found = replayed.find(record.transaction_identity);
    if (found == replayed.end()
        || found->second.state != SystemVerilogUvmTransactionState::Active
        || found->second.root != record.root) {
      fail(kInvalidTransaction,
           "UVM transaction replay references inactive or missing state");
    }
    auto& snapshot = found->second;
    if (record.kind == SystemVerilogUvmTransactionTraceKind::Attribute) {
      if (record.name.empty()) {
        fail(kInvalidTransaction, "UVM replay attribute name is empty");
      }
      auto attribute = std::ranges::find(
          snapshot.attributes, record.name,
          &SystemVerilogUvmTransactionAttribute::name);
      if (attribute == snapshot.attributes.end()) {
        if (snapshot.attributes.size()
            >= limits_.maximum_attributes_per_transaction) {
          fail(kTransactionResource,
               "UVM transaction replay attribute ceiling exceeded");
        }
        snapshot.attributes.push_back({record.name, record.value});
      } else {
        attribute->value = record.value;
      }
      continue;
    }
    if (record.kind == SystemVerilogUvmTransactionTraceKind::Link) {
      if (!record.related_identity
          || !replayed.contains(*record.related_identity)
          || snapshot.links.size() >= limits_.maximum_links_per_transaction
          || std::ranges::any_of(snapshot.links, [&](const auto& link) {
               return link.target_identity == *record.related_identity
                   && link.relation == record.name;
             })) {
        fail(kInvalidTransaction, "UVM transaction replay link is malformed");
      }
      snapshot.links.push_back({*record.related_identity, record.name});
      continue;
    }
    if (record.kind != SystemVerilogUvmTransactionTraceKind::End) {
      fail(kInvalidTransaction, "unknown UVM transaction replay trace kind");
    }
    unsigned state_value{};
    const auto parsed = std::from_chars(
        record.value.data(), record.value.data() + record.value.size(),
        state_value);
    if (parsed.ec != std::errc{} || parsed.ptr != record.value.data() +
                                                  record.value.size()
        || state_value == 0
        || state_value > static_cast<unsigned>(
               SystemVerilogUvmTransactionState::Failed)) {
      fail(kInvalidTransaction, "UVM transaction replay end state is invalid");
    }
    snapshot.state = static_cast<SystemVerilogUvmTransactionState>(state_value);
    snapshot.end_time = record.time;
    snapshot.end_delta = record.delta;
  }
  std::vector<SystemVerilogUvmTransactionSnapshot> result;
  result.reserve(replayed.size());
  for (auto& [identity, snapshot] : replayed) {
    (void)identity;
    result.push_back(std::move(snapshot));
  }
  return result;
}

}  // namespace fsim::runtime
