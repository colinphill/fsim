// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_resource.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

using SystemVerilogUvmConfigWaiterToken = std::uint64_t;

enum class SystemVerilogUvmConfigPhase : std::uint8_t {
  Build,
  Runtime,
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

struct SystemVerilogUvmConfigDbLimits {
  std::size_t max_entries{65'536};
  std::size_t max_waiters{65'536};
  std::size_t max_context_bytes{16'384};
  std::size_t max_instance_pattern_bytes{16'384};
  std::size_t max_field_pattern_bytes{4'096};
  std::size_t max_context_depth{999};
  std::size_t max_regex_states{16'384};
  std::size_t max_wake_callbacks_per_set{65'536};
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

  SystemVerilogUvmResourcePoolService* resources_{};
  SystemVerilogUvmConfigDbLimits limits_;
  std::map<std::string, SystemVerilogUvmConfigEntry, std::less<>> entries_;
  std::map<SystemVerilogUvmConfigWaiterToken, Waiter> waiters_;
  SystemVerilogUvmConfigWaiterToken next_waiter_token_{1};
  std::uint64_t next_entry_order_{};
  std::uint64_t next_update_order_{};
  std::uint64_t next_waiter_order_{};
  std::uint64_t callback_failures_{};
};

}  // namespace fsim::runtime
