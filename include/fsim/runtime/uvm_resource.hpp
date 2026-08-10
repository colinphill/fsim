// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_heap.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::runtime {

using SystemVerilogUvmResourceHandle = std::uint64_t;
using SystemVerilogUvmResourceCallbackToken = std::uint64_t;

enum class SystemVerilogUvmResourceValueKind : std::uint8_t {
  Packed,
  Real,
  String,
  Object,
};

enum class SystemVerilogUvmResourcePriority : std::uint8_t {
  High,
  Low,
};

enum class SystemVerilogUvmResourceCallbackEvent : std::uint8_t {
  PreRead,
  PostRead,
  PreWrite,
  PostWrite,
};

enum class SystemVerilogUvmResourceAuditAction : std::uint8_t {
  Read,
  Write,
  RejectedWrite,
  CallbackFailure,
};

enum class SystemVerilogUvmResourceTraceAction : std::uint8_t {
  LookupName,
  LookupType,
  Read,
  Write,
};

using SystemVerilogUvmResourceValue = std::variant<
    PackedLogic4,
    double,
    std::string,
    SystemVerilogClassHandle>;

struct SystemVerilogUvmResourceType {
  std::string identity;
  SystemVerilogUvmResourceValueKind kind{
      SystemVerilogUvmResourceValueKind::Packed};
  std::size_t packed_width{};
};

struct SystemVerilogUvmResourceDescriptor {
  std::string name;
  std::string scope_pattern{"*"};
  SystemVerilogUvmResourceType type;
  SystemVerilogUvmResourceValue value{PackedLogic4{}};
  std::int64_t precedence{};
  bool read_only{};
  bool auditing{true};
};

struct SystemVerilogUvmResource {
  SystemVerilogUvmResourceHandle handle{};
  std::string name;
  std::string scope_pattern;
  SystemVerilogUvmResourceType type;
  SystemVerilogUvmResourceValue value{PackedLogic4{}};
  std::int64_t precedence{};
  std::int64_t priority_order{};
  bool read_only{};
  bool auditing{true};
  std::uint64_t registration_order{};
  std::uint64_t revision{};
  std::uint64_t read_count{};
  std::uint64_t write_count{};
};

struct SystemVerilogUvmResourceAuditRecord {
  std::uint64_t sequence{};
  SystemVerilogUvmResourceHandle resource{};
  SystemVerilogUvmResourceAuditAction action{
      SystemVerilogUvmResourceAuditAction::Read};
  std::string accessor;
  bool success{};
  std::uint64_t revision{};
};

struct SystemVerilogUvmResourceTraceRecord {
  std::uint64_t sequence{};
  SystemVerilogUvmResourceTraceAction action{
      SystemVerilogUvmResourceTraceAction::LookupName};
  std::string scope;
  std::string name;
  std::string type_identity;
  std::string accessor;
  SystemVerilogUvmResourceHandle selected{};
  std::size_t match_count{};
  bool success{};
};

struct SystemVerilogUvmResourceLimits {
  std::size_t max_resources{65'536};
  std::size_t max_type_identity_bytes{4'096};
  std::size_t max_name_bytes{4'096};
  std::size_t max_scope_bytes{16'384};
  std::size_t max_string_value_bytes{16U * 1'024U * 1'024U};
  std::size_t max_packed_width{16U * 1'024U * 1'024U};
  std::size_t max_lookup_results{65'536};
  std::size_t max_audit_records{65'536};
  std::size_t max_callbacks_per_resource{1'024};
  std::size_t max_spell_candidates{256};
  std::size_t max_spell_distance{64};
  std::size_t max_accessor_bytes{4'096};
  std::size_t max_report_bytes{16U * 1'024U * 1'024U};
  std::size_t max_trace_records{65'536};
  std::size_t max_trace_bytes{16U * 1'024U * 1'024U};
};

/// Scheduler-owned UVM resource metadata. Class-valued entries are non-owning
/// generation-safe heap handles; erasing a resource never destroys its value.
class SystemVerilogUvmResourcePoolService final {
 public:
  using Callback = std::function<void(
      SystemVerilogUvmResourceCallbackEvent,
      const SystemVerilogUvmResource&)>;
  using TraceCallback =
      std::function<void(const SystemVerilogUvmResourceTraceRecord&)>;

  explicit SystemVerilogUvmResourcePoolService(
      SystemVerilogClassHeap* heap = nullptr,
      SystemVerilogUvmResourceLimits limits = {});

  [[nodiscard]] SystemVerilogUvmResourceHandle insert(
      SystemVerilogUvmResourceDescriptor descriptor);
  [[nodiscard]] bool erase(
      SystemVerilogUvmResourceHandle handle) noexcept;
  [[nodiscard]] bool contains(
      SystemVerilogUvmResourceHandle handle) const noexcept;
  [[nodiscard]] SystemVerilogUvmResource snapshot(
      SystemVerilogUvmResourceHandle handle) const;

  void set_scope(
      SystemVerilogUvmResourceHandle handle,
      std::string scope_pattern);
  void set_precedence(
      SystemVerilogUvmResourceHandle handle,
      std::int64_t precedence);
  void set_priority(
      SystemVerilogUvmResourceHandle handle,
      SystemVerilogUvmResourcePriority priority);
  void set_read_only(
      SystemVerilogUvmResourceHandle handle,
      bool read_only);
  void set_auditing(
      SystemVerilogUvmResourceHandle handle,
      bool auditing);

  [[nodiscard]] std::vector<SystemVerilogUvmResourceHandle> lookup_name(
      std::string_view scope,
      std::string_view name,
      std::optional<std::string_view> type_identity = std::nullopt) const;
  [[nodiscard]] SystemVerilogUvmResourceHandle get_by_name(
      std::string_view scope,
      std::string_view name,
      std::optional<std::string_view> type_identity = std::nullopt) const;
  [[nodiscard]] std::vector<SystemVerilogUvmResourceHandle> lookup_type(
      std::string_view scope,
      std::string_view type_identity) const;
  [[nodiscard]] SystemVerilogUvmResourceHandle get_by_type(
      std::string_view scope,
      std::string_view type_identity) const;

  [[nodiscard]] SystemVerilogUvmResourceValue read(
      SystemVerilogUvmResourceHandle handle,
      std::string_view accessor = {});
  [[nodiscard]] bool write(
      SystemVerilogUvmResourceHandle handle,
      SystemVerilogUvmResourceValue value,
      std::string_view accessor = {});

  [[nodiscard]] SystemVerilogUvmResourceCallbackToken add_callback(
      SystemVerilogUvmResourceHandle handle,
      Callback callback);
  [[nodiscard]] bool remove_callback(
      SystemVerilogUvmResourceHandle handle,
      SystemVerilogUvmResourceCallbackToken token) noexcept;

  [[nodiscard]] std::vector<std::string> spell_check(
      std::string_view name,
      std::size_t maximum_distance) const;
  [[nodiscard]] std::string report() const;
  void set_trace_enabled(bool enabled) noexcept { trace_enabled_ = enabled; }
  [[nodiscard]] bool trace_enabled() const noexcept { return trace_enabled_; }
  void set_trace_callback(TraceCallback callback) {
    trace_callback_ = std::move(callback);
  }
  [[nodiscard]] const std::deque<SystemVerilogUvmResourceTraceRecord>&
  trace_records() const noexcept { return trace_records_; }
  [[nodiscard]] std::string trace_text() const;
  [[nodiscard]] std::uint64_t dropped_trace_records() const noexcept {
    return dropped_trace_records_;
  }
  [[nodiscard]] std::uint64_t trace_callback_failures() const noexcept {
    return trace_callback_failures_;
  }

  [[nodiscard]] const std::deque<SystemVerilogUvmResourceAuditRecord>&
  audit_records() const noexcept { return audit_records_; }
  [[nodiscard]] std::uint64_t dropped_audit_records() const noexcept {
    return dropped_audit_records_;
  }
  [[nodiscard]] std::size_t size() const noexcept {
    return resources_.size();
  }
  [[nodiscard]] const SystemVerilogUvmResourceLimits& limits() const noexcept {
    return limits_;
  }

 private:
  struct StoredResource {
    SystemVerilogUvmResource resource;
    std::vector<std::pair<
        SystemVerilogUvmResourceCallbackToken, Callback>> callbacks;
  };

  [[nodiscard]] StoredResource& stored(
      SystemVerilogUvmResourceHandle handle);
  [[nodiscard]] const StoredResource& stored(
      SystemVerilogUvmResourceHandle handle) const;
  void validate_descriptor(
      const SystemVerilogUvmResourceDescriptor& descriptor) const;
  void validate_value(
      const SystemVerilogUvmResourceType& type,
      const SystemVerilogUvmResourceValue& value) const;
  void validate_lookup(
      std::string_view scope,
      std::string_view name) const;
  void append_audit(
      const SystemVerilogUvmResource& resource,
      SystemVerilogUvmResourceAuditAction action,
      std::string_view accessor,
      bool success);
  void notify(
      SystemVerilogUvmResourceHandle handle,
      SystemVerilogUvmResourceCallbackEvent event,
      std::string_view accessor);
  void append_trace(SystemVerilogUvmResourceTraceRecord record) const;

  SystemVerilogClassHeap* heap_{};
  SystemVerilogUvmResourceLimits limits_;
  std::map<SystemVerilogUvmResourceHandle, StoredResource> resources_;
  std::deque<SystemVerilogUvmResourceAuditRecord> audit_records_;
  SystemVerilogUvmResourceHandle next_handle_{1};
  SystemVerilogUvmResourceCallbackToken next_callback_token_{1};
  std::uint64_t next_registration_order_{};
  std::uint64_t next_audit_sequence_{};
  std::uint64_t dropped_audit_records_{};
  std::int64_t next_high_priority_{};
  std::int64_t next_low_priority_{};
  mutable bool trace_enabled_{};
  mutable TraceCallback trace_callback_;
  mutable std::deque<SystemVerilogUvmResourceTraceRecord> trace_records_;
  mutable std::uint64_t next_trace_sequence_{};
  mutable std::uint64_t dropped_trace_records_{};
  mutable std::uint64_t trace_callback_failures_{};
};

}  // namespace fsim::runtime
