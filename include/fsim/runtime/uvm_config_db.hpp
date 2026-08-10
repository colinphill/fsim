// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_resource.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::runtime {

using SystemVerilogUvmConfigWaiterToken = std::uint64_t;

enum class SystemVerilogUvmConfigPhase : std::uint8_t {
  Build,
  Runtime,
};

enum class SystemVerilogUvmConfigTraceAction : std::uint8_t {
  Set,
  Get,
  Exists,
};

struct SystemVerilogUvmConfigContext {
  std::string full_name;
  std::size_t depth{};
};

struct SystemVerilogUvmConfigEntry {
  SystemVerilogUvmResourceHandle resource{};
  std::string context_name;
  std::string instance_pattern;
  std::string field_pattern;
  std::string type_identity;
  std::int64_t precedence{};
  std::uint64_t creation_order{};
  std::uint64_t update_order{};
  SystemVerilogUvmConfigPhase phase{SystemVerilogUvmConfigPhase::Build};
};

struct SystemVerilogUvmConfigTraceRecord {
  std::uint64_t sequence{};
  SystemVerilogUvmConfigTraceAction action{
      SystemVerilogUvmConfigTraceAction::Set};
  std::string context_name;
  std::string instance_name;
  std::string field_name;
  std::string type_identity;
  SystemVerilogUvmResourceHandle resource{};
  bool success{};
};

struct SystemVerilogUvmConfigDbLimits {
  std::size_t max_entries{65'536};
  std::size_t max_waiters{65'536};
  std::size_t max_context_bytes{16'384};
  std::size_t max_instance_pattern_bytes{16'384};
  std::size_t max_field_pattern_bytes{4'096};
  std::size_t max_context_depth{999};
  std::size_t max_regex_states{16'384};
  std::size_t max_wake_callbacks_per_set{65'536};
  std::size_t max_report_bytes{16U * 1'024U * 1'024U};
  std::size_t max_trace_records{65'536};
  std::size_t max_trace_bytes{16U * 1'024U * 1'024U};
  std::int64_t default_precedence{1'000};
};

/// Bounded UVM config_db semantics layered over the simulation-owned resource
/// pool. Waiters are one-shot callbacks dispatched in registration order after
/// the matching value and precedence are fully published.
class SystemVerilogUvmConfigDbService final {
 public:
  using WaiterCallback = std::function<void(
      SystemVerilogUvmConfigWaiterToken,
      const SystemVerilogUvmConfigEntry&)>;
  using TraceCallback =
      std::function<void(const SystemVerilogUvmConfigTraceRecord&)>;

  explicit SystemVerilogUvmConfigDbService(
      SystemVerilogUvmResourcePoolService& resources,
      SystemVerilogUvmConfigDbLimits limits = {});

  [[nodiscard]] SystemVerilogUvmResourceHandle set(
      const SystemVerilogUvmConfigContext& context,
      std::string_view instance_name,
      std::string_view field_name,
      SystemVerilogUvmResourceType type,
      SystemVerilogUvmResourceValue value,
      SystemVerilogUvmConfigPhase phase);
  [[nodiscard]] std::optional<SystemVerilogUvmResourceValue> get(
      const SystemVerilogUvmConfigContext& context,
      std::string_view instance_name,
      std::string_view field_name,
      std::string_view type_identity,
      std::string_view accessor = {});
  [[nodiscard]] bool exists(
      const SystemVerilogUvmConfigContext& context,
      std::string_view instance_name,
      std::string_view field_name,
      std::string_view type_identity,
      bool spell_check = false,
      std::vector<std::string>* spelling = nullptr) const;

  [[nodiscard]] SystemVerilogUvmConfigWaiterToken wait_modified(
      const SystemVerilogUvmConfigContext& context,
      std::string_view instance_name,
      std::string_view field_name,
      WaiterCallback callback);
  [[nodiscard]] bool cancel_waiter(
      SystemVerilogUvmConfigWaiterToken token) noexcept;

  [[nodiscard]] std::vector<SystemVerilogUvmConfigEntry> entries() const;
  [[nodiscard]] std::string report(bool audit = false) const;
  void set_trace_enabled(bool enabled) noexcept { trace_enabled_ = enabled; }
  [[nodiscard]] bool trace_enabled() const noexcept { return trace_enabled_; }
  void set_trace_callback(TraceCallback callback) {
    trace_callback_ = std::move(callback);
  }
  [[nodiscard]] const std::deque<SystemVerilogUvmConfigTraceRecord>&
  trace_records() const noexcept { return trace_records_; }
  [[nodiscard]] std::string trace_text() const;
  [[nodiscard]] std::uint64_t dropped_trace_records() const noexcept {
    return dropped_trace_records_;
  }
  [[nodiscard]] std::uint64_t trace_callback_failures() const noexcept {
    return trace_callback_failures_;
  }
  [[nodiscard]] std::size_t waiter_count() const noexcept {
    return waiters_.size();
  }
  [[nodiscard]] std::uint64_t callback_failures() const noexcept {
    return callback_failures_;
  }
  [[nodiscard]] const SystemVerilogUvmConfigDbLimits& limits() const noexcept {
    return limits_;
  }

 private:
  struct Waiter {
    SystemVerilogUvmConfigWaiterToken token{};
    std::string instance_name;
    std::string field_name;
    std::uint64_t registration_order{};
    WaiterCallback callback;
  };

  [[nodiscard]] std::string full_instance_name(
      const SystemVerilogUvmConfigContext& context,
      std::string_view instance_name) const;
  [[nodiscard]] std::string entry_key(
      std::string_view context,
      std::string_view instance_pattern,
      std::string_view field_pattern,
      std::string_view type_identity) const;
  void validate_context(
      const SystemVerilogUvmConfigContext& context) const;
  void validate_pattern(
      std::string_view pattern,
      std::size_t byte_limit) const;
  [[nodiscard]] bool pattern_matches(
      std::string_view pattern,
      std::string_view value) const;
  [[nodiscard]] const SystemVerilogUvmConfigEntry* resolve(
      std::string_view instance_name,
      std::string_view field_name,
      std::string_view type_identity) const;
  void validate_wake_budget(
      std::string_view instance_pattern,
      std::string_view field_pattern) const;
  void trigger_waiters(const SystemVerilogUvmConfigEntry& entry);
  void append_trace(SystemVerilogUvmConfigTraceRecord record) const;

  SystemVerilogUvmResourcePoolService* resources_{};
  SystemVerilogUvmConfigDbLimits limits_;
  std::map<std::string, SystemVerilogUvmConfigEntry, std::less<>> entries_;
  std::map<SystemVerilogUvmConfigWaiterToken, Waiter> waiters_;
  SystemVerilogUvmConfigWaiterToken next_waiter_token_{1};
  std::uint64_t next_entry_order_{};
  std::uint64_t next_update_order_{};
  std::uint64_t next_waiter_order_{};
  std::uint64_t callback_failures_{};
  mutable bool trace_enabled_{};
  mutable TraceCallback trace_callback_;
  mutable std::deque<SystemVerilogUvmConfigTraceRecord> trace_records_;
  mutable std::uint64_t next_trace_sequence_{};
  mutable std::uint64_t dropped_trace_records_{};
  mutable std::uint64_t trace_callback_failures_{};
};

}  // namespace fsim::runtime
