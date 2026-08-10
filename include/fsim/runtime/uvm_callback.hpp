// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/uvm_activity.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_object.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace fsim::runtime {

class SystemVerilogUvmCallbackService;
class SystemVerilogUvmTransactionRecorderService;

class SystemVerilogUvmCallbackHandle final {
public:
  SystemVerilogUvmCallbackHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const SystemVerilogUvmCallbackHandle &,
                         const SystemVerilogUvmCallbackHandle &) = default;

private:
  friend class SystemVerilogUvmCallbackService;
  SystemVerilogUvmCallbackHandle(std::shared_ptr<const void> owner,
                                 std::uint64_t slot, std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

enum class SystemVerilogUvmCallbackOrdering : std::uint8_t {
  Append,
  Prepend,
};

enum class SystemVerilogUvmCallbackScope : std::uint8_t {
  TypeWide,
  Instance,
};

using SystemVerilogUvmCallbackAttributes =
    std::map<std::string, std::string, std::less<>>;

struct SystemVerilogUvmCallbackInvocation {
  SystemVerilogClassHandle target{};
  SystemVerilogUvmRootHandle root{};
  std::string target_type;
  std::string callback_type;
  std::string operation;
  std::uint64_t mask{};
  std::size_t depth{};
  SystemVerilogUvmCallbackAttributes attributes;

  friend bool operator==(const SystemVerilogUvmCallbackInvocation &,
                         const SystemVerilogUvmCallbackInvocation &) = default;
};

struct SystemVerilogUvmCallbackDescriptor {
  std::string target_type;
  std::string callback_type;
  std::string name;
  SystemVerilogClassHandle instance{};
  std::uint64_t mask{~UINT64_C(0)};
  SystemVerilogUvmCallbackOrdering ordering{
      SystemVerilogUvmCallbackOrdering::Append};
  std::function<void(SystemVerilogUvmCallbackInvocation &)> callback;
};

struct SystemVerilogUvmCallbackSnapshot {
  SystemVerilogUvmCallbackHandle handle;
  SystemVerilogUvmCallbackScope scope{SystemVerilogUvmCallbackScope::TypeWide};
  std::string target_type;
  std::string callback_type;
  std::string name;
  SystemVerilogClassHandle instance{};
  std::uint64_t mask{};
  std::uint64_t order{};
  std::uint64_t invocations{};

  friend bool operator==(const SystemVerilogUvmCallbackSnapshot &,
                         const SystemVerilogUvmCallbackSnapshot &) = default;
};

struct SystemVerilogUvmCallbackFailure {
  std::string diagnostic_code;
  SystemVerilogUvmCallbackSnapshot callback;
  SystemVerilogUvmCallbackInvocation invocation;
  std::string message;

  friend bool operator==(const SystemVerilogUvmCallbackFailure &,
                         const SystemVerilogUvmCallbackFailure &) = default;
};

struct SystemVerilogUvmCallbackDispatchResult {
  SystemVerilogUvmCallbackInvocation invocation;
  std::vector<SystemVerilogUvmCallbackHandle> invoked;
  std::size_t contained_failures{};
};

struct SystemVerilogUvmCallbackLimits {
  std::size_t maximum_callbacks{65'536};
  std::size_t maximum_callbacks_per_dispatch{4'096};
  std::size_t maximum_reentry_depth{64};
  std::size_t maximum_failures{65'536};
  std::size_t maximum_type_bytes{4'096};
  std::size_t maximum_name_bytes{4'096};
  std::size_t maximum_operation_bytes{4'096};
  std::size_t maximum_attribute_count{1'024};
  std::size_t maximum_attribute_bytes{1U << 20U};
  std::size_t maximum_mutations{1U << 24U};
};

class SystemVerilogUvmCallbackError final : public std::runtime_error {
public:
  SystemVerilogUvmCallbackError(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

private:
  std::string code_;
};

class SystemVerilogUvmCallbackService final {
public:
  using Callback = std::function<void(SystemVerilogUvmCallbackInvocation &)>;

  SystemVerilogUvmCallbackService(SystemVerilogUvmObjectService &objects,
                                  SystemVerilogUvmComponentService &components,
                                  SystemVerilogUvmCallbackLimits limits = {});

  void
  set_activity_service(SystemVerilogUvmActivityService &activity) noexcept {
    activity_ = &activity;
  }
  [[nodiscard]] SystemVerilogUvmCallbackHandle
  add(SystemVerilogUvmCallbackDescriptor descriptor);
  void remove(SystemVerilogUvmCallbackHandle handle);
  void set_mask(SystemVerilogUvmCallbackHandle handle, std::uint64_t mask);
  [[nodiscard]] SystemVerilogUvmCallbackDispatchResult
  dispatch(SystemVerilogClassHandle target, std::string callback_type,
           std::uint64_t mask, std::string operation = {},
           SystemVerilogUvmCallbackAttributes attributes = {});

  [[nodiscard]] SystemVerilogUvmCallbackSnapshot
  snapshot(SystemVerilogUvmCallbackHandle handle) const;
  [[nodiscard]] std::vector<SystemVerilogUvmCallbackSnapshot> snapshots() const;
  [[nodiscard]] std::span<const SystemVerilogUvmCallbackFailure>
  failures() const noexcept {
    return failures_;
  }
  [[nodiscard]] std::uint64_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] const SystemVerilogUvmCallbackLimits &limits() const noexcept {
    return limits_;
  }

private:
  struct Entry {
    SystemVerilogUvmCallbackSnapshot snapshot;
    Callback callback;
  };

  [[nodiscard]] Entry &entry(SystemVerilogUvmCallbackHandle handle);
  [[nodiscard]] const Entry &entry(SystemVerilogUvmCallbackHandle handle) const;
  [[nodiscard]] SystemVerilogUvmRootHandle
  root_of(SystemVerilogClassHandle target) const;
  void validate_invocation(const SystemVerilogUvmCallbackInvocation &) const;
  void publish_activity(SystemVerilogUvmActivityAction action,
                        const SystemVerilogUvmCallbackSnapshot &callback,
                        std::string_view detail) noexcept;

  SystemVerilogUvmObjectService *objects_{};
  SystemVerilogUvmComponentService *components_{};
  SystemVerilogUvmActivityService *activity_{};
  SystemVerilogUvmCallbackLimits limits_;
  std::shared_ptr<const void> owner_{std::make_shared<std::uint8_t>(0)};
  std::map<std::uint64_t, Entry> callbacks_;
  std::vector<SystemVerilogUvmCallbackHandle> order_;
  std::vector<SystemVerilogUvmCallbackFailure> failures_;
  std::map<std::uint64_t, std::uint64_t> generations_;
  std::uint64_t next_slot_{1};
  std::uint64_t next_order_{1};
  std::uint64_t mutations_{};
  std::size_t dispatch_depth_{};
};

class SystemVerilogUvmTransactionHandle final {
public:
  SystemVerilogUvmTransactionHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const SystemVerilogUvmTransactionHandle &,
                         const SystemVerilogUvmTransactionHandle &) = default;

private:
  friend class SystemVerilogUvmTransactionRecorderService;
  SystemVerilogUvmTransactionHandle(std::shared_ptr<const void> owner,
                                    std::uint64_t slot,
                                    std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

using SystemVerilogUvmTransactionAttributeValue =
    std::variant<std::int64_t, std::uint64_t, double, bool, std::string>;

struct SystemVerilogUvmTransactionAttribute {
  std::string name;
  SystemVerilogUvmTransactionAttributeValue value;

  friend bool
  operator==(const SystemVerilogUvmTransactionAttribute &,
             const SystemVerilogUvmTransactionAttribute &) = default;
};

enum class SystemVerilogUvmTransactionState : std::uint8_t {
  Active,
  Completed,
  Cancelled,
  Failed,
};

enum class SystemVerilogUvmTransactionTraceKind : std::uint8_t {
  Begin,
  Attribute,
  Link,
  End,
};

struct SystemVerilogUvmTransactionLink {
  std::uint64_t target_identity{};
  std::string relation;

  friend bool operator==(const SystemVerilogUvmTransactionLink &,
                         const SystemVerilogUvmTransactionLink &) = default;
};

struct SystemVerilogUvmTransactionDescriptor {
  std::string name;
  std::string stream;
  std::string kind;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle object{};
  std::optional<SystemVerilogUvmTransactionHandle> parent;
  std::vector<SystemVerilogUvmTransactionAttribute> attributes;
  std::string callback_type{"uvm_transaction_callback"};
};

struct SystemVerilogUvmTransactionSnapshot {
  SystemVerilogUvmTransactionHandle handle;
  std::uint64_t identity{};
  std::string name;
  std::string stream;
  std::string kind;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle object{};
  std::optional<std::uint64_t> parent_identity;
  std::vector<SystemVerilogUvmTransactionLink> links;
  std::vector<SystemVerilogUvmTransactionAttribute> attributes;
  SystemVerilogUvmTransactionState state{
      SystemVerilogUvmTransactionState::Active};
  SimulationTick begin_time{};
  std::uint64_t begin_delta{};
  SimulationTick end_time{};
  std::uint64_t end_delta{};

  friend bool operator==(const SystemVerilogUvmTransactionSnapshot &,
                         const SystemVerilogUvmTransactionSnapshot &) = default;
};

struct SystemVerilogUvmTransactionTraceRecord {
  std::uint64_t sequence{};
  SystemVerilogUvmTransactionTraceKind kind{
      SystemVerilogUvmTransactionTraceKind::Begin};
  std::uint64_t transaction_identity{};
  std::optional<std::uint64_t> related_identity;
  SystemVerilogUvmRootHandle root{};
  SimulationTick time{};
  std::uint64_t delta{};
  std::string name;
  std::string value;

  friend bool
  operator==(const SystemVerilogUvmTransactionTraceRecord &,
             const SystemVerilogUvmTransactionTraceRecord &) = default;
};

struct SystemVerilogUvmTransactionLimits {
  std::size_t maximum_transactions{65'536};
  std::size_t maximum_active_transactions{4'096};
  std::size_t maximum_attributes_per_transaction{1'024};
  std::size_t maximum_links_per_transaction{1'024};
  std::size_t maximum_trace_records{1U << 20U};
  std::size_t maximum_text_bytes{16'384};
  std::size_t maximum_attribute_bytes{1U << 20U};
  std::size_t maximum_mutations{1U << 24U};
};

class SystemVerilogUvmTransactionError final : public std::runtime_error {
public:
  SystemVerilogUvmTransactionError(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

private:
  std::string code_;
};

class SystemVerilogUvmTransactionRecorderService final {
public:
  SystemVerilogUvmTransactionRecorderService(
      SystemVerilogUvmObjectService &objects,
      SystemVerilogUvmComponentService &components,
      SystemVerilogUvmCallbackService &callbacks,
      SystemVerilogUvmTransactionLimits limits = {});

  void set_scheduler(Scheduler &scheduler) noexcept { scheduler_ = &scheduler; }
  void
  set_activity_service(SystemVerilogUvmActivityService &activity) noexcept {
    activity_ = &activity;
  }
  [[nodiscard]] SystemVerilogUvmTransactionHandle
  begin(SystemVerilogUvmTransactionDescriptor descriptor);
  void record_attribute(SystemVerilogUvmTransactionHandle transaction,
                        SystemVerilogUvmTransactionAttribute attribute);
  void record_object(SystemVerilogUvmTransactionHandle transaction,
                     SystemVerilogClassHandle object,
                     std::string prefix = {});
  void add_link(SystemVerilogUvmTransactionHandle transaction,
                SystemVerilogUvmTransactionHandle related,
                std::string relation);
  void end(SystemVerilogUvmTransactionHandle transaction,
           SystemVerilogUvmTransactionState state =
               SystemVerilogUvmTransactionState::Completed);
  void release(SystemVerilogUvmTransactionHandle transaction);

  [[nodiscard]] SystemVerilogUvmTransactionSnapshot
  snapshot(SystemVerilogUvmTransactionHandle transaction) const;
  [[nodiscard]] std::vector<SystemVerilogUvmTransactionSnapshot>
  snapshots() const;
  [[nodiscard]] std::span<const SystemVerilogUvmTransactionTraceRecord>
  trace_records() const noexcept {
    return trace_records_;
  }
  [[nodiscard]] std::vector<SystemVerilogUvmTransactionSnapshot> replay_trace(
      std::span<const SystemVerilogUvmTransactionTraceRecord> records) const;
  [[nodiscard]] std::uint64_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] const SystemVerilogUvmTransactionLimits &
  limits() const noexcept {
    return limits_;
  }

private:
  struct Entry {
    SystemVerilogUvmTransactionSnapshot snapshot;
    std::string callback_type;
  };

  [[nodiscard]] Entry &entry(SystemVerilogUvmTransactionHandle transaction);
  [[nodiscard]] const Entry &
  entry(SystemVerilogUvmTransactionHandle transaction) const;
  [[nodiscard]] std::pair<SimulationTick, std::uint64_t>
  timestamp() const noexcept;
  void validate_text(std::string_view text) const;
  void validate_attribute(
      const SystemVerilogUvmTransactionAttribute &attribute) const;
  void append_trace(SystemVerilogUvmTransactionTraceRecord record);
  void publish_activity(SystemVerilogUvmActivityAction action,
                        const SystemVerilogUvmTransactionSnapshot &transaction,
                        std::string detail);

  SystemVerilogUvmObjectService *objects_{};
  SystemVerilogUvmComponentService *components_{};
  SystemVerilogUvmCallbackService *callbacks_{};
  Scheduler *scheduler_{};
  SystemVerilogUvmActivityService *activity_{};
  SystemVerilogUvmTransactionLimits limits_;
  std::shared_ptr<const void> owner_{std::make_shared<std::uint8_t>(0)};
  std::map<std::uint64_t, Entry> transactions_;
  std::vector<SystemVerilogUvmTransactionTraceRecord> trace_records_;
  std::map<std::uint64_t, std::uint64_t> generations_;
  std::uint64_t next_slot_{1};
  std::uint64_t next_identity_{1};
  std::uint64_t next_trace_sequence_{1};
  std::uint64_t mutations_{};
};

} // namespace fsim::runtime
