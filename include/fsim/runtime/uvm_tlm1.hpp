// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/packed_value.hpp"
#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/uvm_activity.hpp"
#include "fsim/runtime/uvm_phase.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::runtime {

class SystemVerilogUvmTlm1Service;
class SystemVerilogUvmPhaseService;

class SystemVerilogUvmTlm1EndpointHandle final {
 public:
  SystemVerilogUvmTlm1EndpointHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmTlm1EndpointHandle&,
      const SystemVerilogUvmTlm1EndpointHandle&) = default;

 private:
  friend class SystemVerilogUvmTlm1Service;
  SystemVerilogUvmTlm1EndpointHandle(
      std::shared_ptr<const void> owner,
      std::uint64_t slot,
      std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

enum class SystemVerilogUvmTlm1EndpointKind : std::uint8_t {
  Port,
  Export,
  Implementation,
};

enum class SystemVerilogUvmTlm1Interface : std::uint8_t {
  BlockingPut,
  NonblockingPut,
  Put,
  BlockingGet,
  NonblockingGet,
  Get,
  BlockingPeek,
  NonblockingPeek,
  Peek,
  BlockingTransport,
  NonblockingTransport,
  Transport,
  Master,
  Slave,
  Bidirectional,
  Analysis,
};

enum class SystemVerilogUvmTlm1Direction : std::uint8_t {
  Forward,
  Backward,
  Bidirectional,
};

struct SystemVerilogUvmTlm1Profile {
  SystemVerilogUvmTlm1Interface interface_kind{
      SystemVerilogUvmTlm1Interface::Put};
  SystemVerilogUvmTlm1Direction direction{
      SystemVerilogUvmTlm1Direction::Forward};
  std::string request_type;
  std::string response_type;
  friend bool operator==(
      const SystemVerilogUvmTlm1Profile&,
      const SystemVerilogUvmTlm1Profile&) = default;
};

struct SystemVerilogUvmTlm1EndpointDescriptor {
  SystemVerilogUvmTlm1EndpointKind kind{
      SystemVerilogUvmTlm1EndpointKind::Port};
  SystemVerilogUvmTlm1Profile profile;
  SystemVerilogClassHandle component{};
  std::string name;
  std::size_t minimum_connections{1};
  std::size_t maximum_connections{1};
};

struct SystemVerilogUvmTlm1Limits {
  std::size_t maximum_endpoints{65'536};
  std::size_t maximum_connections{262'144};
  std::size_t maximum_endpoints_per_component{4'096};
  std::size_t maximum_name_bytes{1'024};
  std::size_t maximum_profile_bytes{4'096};
  std::size_t maximum_fanout{65'536};
  std::size_t maximum_depth{256};
  std::size_t maximum_resolution_work{1U << 20U};
  std::size_t maximum_mutations{1U << 20U};
  std::size_t maximum_fifos{65'536};
  std::size_t maximum_fifo_capacity{1U << 20U};
  std::size_t maximum_queued_payloads{1U << 20U};
  std::size_t maximum_payload_bits{1U << 24U};
  std::size_t maximum_payload_type_bytes{4'096};
  std::size_t maximum_pending_operations{262'144};
  std::size_t maximum_execution_operations{1U << 22U};
  std::size_t maximum_analysis_callbacks_per_publication{65'536};
  std::size_t maximum_analysis_recursion_depth{64};
  std::size_t maximum_analysis_failures{65'536};
  std::size_t maximum_analysis_publications{1U << 22U};
};

class SystemVerilogUvmTlm1Error final : public std::runtime_error {
 public:
  SystemVerilogUvmTlm1Error(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

 private:
  std::string code_;
};

struct SystemVerilogUvmTlm1EndpointSnapshot {
  SystemVerilogUvmTlm1EndpointHandle handle;
  SystemVerilogUvmTlm1EndpointKind kind{
      SystemVerilogUvmTlm1EndpointKind::Port};
  SystemVerilogUvmTlm1Profile profile;
  SystemVerilogClassHandle component{};
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::string debug_name;
  std::size_t minimum_connections{};
  std::size_t maximum_connections{};
  std::uint64_t declaration_order{};
  std::vector<SystemVerilogUvmTlm1EndpointHandle> outbound;
  std::vector<SystemVerilogUvmTlm1EndpointHandle> inbound;
  std::vector<SystemVerilogUvmTlm1EndpointHandle> resolved;
};

class SystemVerilogUvmTlm1OperationHandle final {
 public:
  SystemVerilogUvmTlm1OperationHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmTlm1OperationHandle&,
      const SystemVerilogUvmTlm1OperationHandle&) = default;

 private:
  friend class SystemVerilogUvmTlm1Service;
  SystemVerilogUvmTlm1OperationHandle(
      std::shared_ptr<const void> owner,
      std::uint64_t slot,
      std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}
  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

enum class SystemVerilogUvmTlm1OperationKind : std::uint8_t {
  Put,
  Get,
  Peek,
  Transport,
};

enum class SystemVerilogUvmTlm1OperationState : std::uint8_t {
  Pending,
  Completed,
  Cancelled,
};

struct SystemVerilogUvmTlm1Payload {
  std::string nominal_type;
  PackedLogic4 value;
  SystemVerilogClassHandle object{};
  SystemVerilogUvmRootHandle owner_root{};
  SystemVerilogUvmTlm1EndpointHandle owner_endpoint;
  std::uint64_t sequence{};
  friend bool operator==(
      const SystemVerilogUvmTlm1Payload&,
      const SystemVerilogUvmTlm1Payload&) = default;
};

struct SystemVerilogUvmTlm1OperationSnapshot {
  SystemVerilogUvmTlm1OperationHandle handle;
  SystemVerilogUvmTlm1OperationKind kind{
      SystemVerilogUvmTlm1OperationKind::Put};
  SystemVerilogUvmTlm1OperationState state{
      SystemVerilogUvmTlm1OperationState::Pending};
  SystemVerilogUvmTlm1EndpointHandle source;
  SystemVerilogUvmTlm1EndpointHandle implementation;
  SystemVerilogUvmPhaseHandle phase;
  std::optional<SystemVerilogUvmTlm1Payload> request;
  std::optional<SystemVerilogUvmTlm1Payload> response;
  std::uint64_t reservation_order{};
  SimulationTick completion_time{};
};

struct SystemVerilogUvmTlm1FifoSnapshot {
  SystemVerilogUvmTlm1EndpointHandle implementation;
  std::size_t capacity{};
  std::size_t size{};
  std::size_t pending_puts{};
  std::size_t pending_gets{};
  std::size_t pending_peeks{};
};

struct SystemVerilogUvmTlm1AnalysisDelivery {
  SystemVerilogUvmTlm1EndpointHandle implementation;
  SystemVerilogUvmTlm1Payload payload;
  std::uint64_t delivery_order{};
};

struct SystemVerilogUvmTlm1AnalysisFailure {
  std::string diagnostic_code;
  SystemVerilogUvmTlm1EndpointHandle implementation;
  std::string message;
  std::uint64_t delivery_order{};
};

struct SystemVerilogUvmTlm1AnalysisResult {
  std::vector<SystemVerilogUvmTlm1AnalysisDelivery> deliveries;
  std::vector<SystemVerilogUvmTlm1AnalysisFailure> failures;
  std::uint64_t publication_sequence{};
  std::size_t recursion_depth{};
  [[nodiscard]] bool success() const noexcept { return failures.empty(); }
};

/// Simulation-owned structural TLM1 graph. Graph mutations invalidate the
/// prior binding; resolve_all publishes a complete stable binding atomically.
class SystemVerilogUvmTlm1Service final {
 public:
  using CompletionCallback = std::function<void(
      const SystemVerilogUvmTlm1OperationSnapshot&)>;
  using TransportHandler = std::function<std::optional<
      SystemVerilogUvmTlm1Payload>(const SystemVerilogUvmTlm1Payload&)>;
  using AnalysisSubscriber = std::function<void(
      SystemVerilogUvmTlm1Payload)>;

  explicit SystemVerilogUvmTlm1Service(
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmTlm1Limits limits = {});
  SystemVerilogUvmTlm1Service(
      SystemVerilogClassHeap& heap,
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmTlm1Limits limits = {});
  ~SystemVerilogUvmTlm1Service();
  SystemVerilogUvmTlm1Service(const SystemVerilogUvmTlm1Service&) = delete;
  SystemVerilogUvmTlm1Service& operator=(
      const SystemVerilogUvmTlm1Service&) = delete;

  [[nodiscard]] SystemVerilogUvmTlm1EndpointHandle register_endpoint(
      SystemVerilogUvmTlm1EndpointDescriptor descriptor);
  void connect(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmTlm1EndpointHandle target);
  void resolve_all();

  void set_scheduler(Scheduler& scheduler) noexcept { scheduler_ = &scheduler; }
  void set_activity_service(SystemVerilogUvmActivityService& activity)
      noexcept {
    activity_ = &activity;
  }
  void set_phase_service(SystemVerilogUvmPhaseService& phases) noexcept;
  void configure_fifo(
      SystemVerilogUvmTlm1EndpointHandle implementation,
      std::size_t capacity);
  void set_transport_handler(
      SystemVerilogUvmTlm1EndpointHandle implementation,
      TransportHandler handler);
  [[nodiscard]] SystemVerilogUvmTlm1FifoSnapshot fifo_snapshot(
      SystemVerilogUvmTlm1EndpointHandle implementation) const;
  [[nodiscard]] std::vector<SystemVerilogUvmTlm1FifoSnapshot>
  fifo_snapshots() const;
  [[nodiscard]] std::vector<SystemVerilogUvmTlm1Payload> fifo_values(
      SystemVerilogUvmTlm1EndpointHandle implementation) const;
  [[nodiscard]] bool implementation_can_write(
      SystemVerilogUvmTlm1EndpointHandle implementation) const;
  [[nodiscard]] bool implementation_try_write(
      SystemVerilogUvmTlm1EndpointHandle implementation,
      SystemVerilogUvmTlm1Payload payload,
      bool response = false);
  [[nodiscard]] std::optional<SystemVerilogUvmTlm1Payload>
  implementation_try_read(
      SystemVerilogUvmTlm1EndpointHandle implementation,
      bool peek = false);

  [[nodiscard]] bool can_put(
      SystemVerilogUvmTlm1EndpointHandle source,
      std::size_t binding_index = 0) const;
  [[nodiscard]] bool try_put(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmTlm1Payload payload,
      std::size_t binding_index = 0);
  [[nodiscard]] bool can_get(
      SystemVerilogUvmTlm1EndpointHandle source,
      std::size_t binding_index = 0) const;
  [[nodiscard]] std::optional<SystemVerilogUvmTlm1Payload> try_get(
      SystemVerilogUvmTlm1EndpointHandle source,
      std::size_t binding_index = 0);
  [[nodiscard]] bool can_peek(
      SystemVerilogUvmTlm1EndpointHandle source,
      std::size_t binding_index = 0) const;
  [[nodiscard]] std::optional<SystemVerilogUvmTlm1Payload> try_peek(
      SystemVerilogUvmTlm1EndpointHandle source,
      std::size_t binding_index = 0);
  [[nodiscard]] bool can_transport(
      SystemVerilogUvmTlm1EndpointHandle source,
      std::size_t binding_index = 0) const;
  [[nodiscard]] std::optional<SystemVerilogUvmTlm1Payload> try_transport(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmTlm1Payload request,
      std::size_t binding_index = 0);

  [[nodiscard]] SystemVerilogUvmTlm1OperationHandle put(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmTlm1Payload payload,
      SystemVerilogUvmPhaseHandle phase = {},
      CompletionCallback completion = {},
      std::size_t binding_index = 0);
  [[nodiscard]] SystemVerilogUvmTlm1OperationHandle get(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmPhaseHandle phase = {},
      CompletionCallback completion = {},
      std::size_t binding_index = 0);
  [[nodiscard]] SystemVerilogUvmTlm1OperationHandle peek(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmPhaseHandle phase = {},
      CompletionCallback completion = {},
      std::size_t binding_index = 0);
  [[nodiscard]] SystemVerilogUvmTlm1OperationHandle transport(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmTlm1Payload request,
      SystemVerilogUvmPhaseHandle phase = {},
      CompletionCallback completion = {},
      std::size_t binding_index = 0);
  void complete_transport(
      SystemVerilogUvmTlm1OperationHandle operation,
      SystemVerilogUvmTlm1Payload response);
  [[nodiscard]] SystemVerilogUvmTlm1OperationSnapshot operation_snapshot(
      SystemVerilogUvmTlm1OperationHandle operation) const;
  [[nodiscard]] std::vector<SystemVerilogUvmTlm1OperationHandle> operations()
      const;
  void cancel_operation(SystemVerilogUvmTlm1OperationHandle operation);
  void cancel_phase(SystemVerilogUvmPhaseHandle phase) noexcept;
  void cancel_root(SystemVerilogUvmRootHandle root) noexcept;
  void cancel_all() noexcept;
  [[nodiscard]] std::size_t pending_operation_count() const noexcept;

  void set_analysis_subscriber(
      SystemVerilogUvmTlm1EndpointHandle implementation,
      AnalysisSubscriber subscriber);
  void configure_analysis_fifo(
      SystemVerilogUvmTlm1EndpointHandle implementation,
      std::size_t capacity);
  [[nodiscard]] SystemVerilogUvmTlm1EndpointHandle
  register_analysis_implementation(
      SystemVerilogClassHandle component,
      std::string name,
      std::string nominal_type,
      AnalysisSubscriber subscriber);
  [[nodiscard]] SystemVerilogUvmTlm1AnalysisResult write_analysis(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmTlm1Payload payload);
  [[nodiscard]] std::optional<SystemVerilogUvmTlm1Payload>
  analysis_fifo_try_get(
      SystemVerilogUvmTlm1EndpointHandle implementation);

  [[nodiscard]] bool contains(
      SystemVerilogUvmTlm1EndpointHandle endpoint) const noexcept;
  [[nodiscard]] SystemVerilogUvmTlm1EndpointSnapshot snapshot(
      SystemVerilogUvmTlm1EndpointHandle endpoint) const;
  [[nodiscard]] std::vector<SystemVerilogUvmTlm1EndpointHandle> endpoints()
      const;
  [[nodiscard]] std::span<const SystemVerilogUvmTlm1EndpointHandle> resolved(
      SystemVerilogUvmTlm1EndpointHandle endpoint) const;
  [[nodiscard]] bool binding_valid() const noexcept {
    return bound_revision_ == revision_;
  }
  [[nodiscard]] std::size_t connection_count() const noexcept {
    return connections_;
  }
  [[nodiscard]] std::size_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] const SystemVerilogUvmTlm1Limits& limits() const noexcept {
    return limits_;
  }

 private:
  struct Endpoint {
    std::uint64_t slot{};
    SystemVerilogUvmTlm1EndpointDescriptor descriptor;
    SystemVerilogUvmRootHandle root{};
    std::string component_name;
    std::string full_name;
    std::string debug_name;
    std::uint64_t declaration_order{};
    std::uint64_t generation{1};
    std::vector<std::uint64_t> outbound;
    std::vector<std::uint64_t> inbound;
    std::vector<SystemVerilogUvmTlm1EndpointHandle> resolved;
  };
  struct Fifo {
    std::size_t capacity{};
    std::deque<SystemVerilogUvmTlm1Payload> values;
    std::deque<std::uint64_t> puts;
    std::deque<std::uint64_t> gets;
    std::deque<std::uint64_t> peeks;
  };
  struct Operation {
    SystemVerilogUvmTlm1OperationSnapshot value;
    CompletionCallback completion;
    ScheduledTaskHandle callback_task;
    std::uint64_t generation{1};
  };

  [[nodiscard]] Endpoint& endpoint(SystemVerilogUvmTlm1EndpointHandle handle);
  [[nodiscard]] const Endpoint& endpoint(
      SystemVerilogUvmTlm1EndpointHandle handle) const;
  [[nodiscard]] SystemVerilogUvmTlm1EndpointHandle handle(
      std::uint64_t slot) const;
  void require_live(const Endpoint& endpoint) const;
  [[nodiscard]] bool reaches(
      std::uint64_t from,
      std::uint64_t wanted,
      std::size_t& work) const;
  void collect_implementations(
      std::uint64_t slot,
      std::size_t depth,
      std::size_t& work,
      std::vector<SystemVerilogUvmTlm1EndpointHandle>& result,
      std::vector<std::uint64_t>& path) const;
  [[nodiscard]] std::uint64_t implementation_slot(
      SystemVerilogUvmTlm1EndpointHandle source,
      SystemVerilogUvmTlm1OperationKind operation,
      std::size_t binding_index) const;
  [[nodiscard]] bool supports(
      SystemVerilogUvmTlm1Interface interface_kind,
      SystemVerilogUvmTlm1OperationKind operation,
      bool blocking) const noexcept;
  [[nodiscard]] bool operation_uses_response(
      const Endpoint& implementation,
      SystemVerilogUvmTlm1OperationKind operation) const noexcept;
  void validate_payload(
      SystemVerilogUvmTlm1Payload& payload,
      const Endpoint& implementation,
      bool response);
  void require_payload_type(
      const SystemVerilogUvmTlm1Payload& payload,
      const Endpoint& implementation,
      bool response) const;
  [[nodiscard]] bool payload_type_matches(
      const SystemVerilogUvmTlm1Payload& payload,
      const Endpoint& implementation,
      bool response) const noexcept;
  [[nodiscard]] Fifo& fifo(std::uint64_t implementation);
  [[nodiscard]] const Fifo& fifo(std::uint64_t implementation) const;
  void publish_fifo_activity(
      std::uint64_t implementation,
      std::string_view detail);
  [[nodiscard]] SystemVerilogUvmTlm1OperationHandle create_operation(
      SystemVerilogUvmTlm1OperationKind kind,
      SystemVerilogUvmTlm1EndpointHandle source,
      std::uint64_t implementation,
      SystemVerilogUvmPhaseHandle phase,
      std::optional<SystemVerilogUvmTlm1Payload> request,
      CompletionCallback completion);
  [[nodiscard]] SystemVerilogUvmTlm1OperationHandle operation_handle(
      std::uint64_t slot) const;
  [[nodiscard]] Operation& operation(
      SystemVerilogUvmTlm1OperationHandle handle);
  [[nodiscard]] const Operation& operation(
      SystemVerilogUvmTlm1OperationHandle handle) const;
  void complete_operation(
      std::uint64_t slot,
      std::optional<SystemVerilogUvmTlm1Payload> response = std::nullopt);
  void pump_fifo(std::uint64_t implementation);
  void erase_waiter(std::uint64_t operation) noexcept;
  void record_execution();
  [[nodiscard]] std::size_t queued_payload_count() const noexcept;

  SystemVerilogClassHeap* heap_{};
  SystemVerilogUvmComponentService* components_{};
  SystemVerilogUvmPhaseService* phases_{};
  Scheduler* scheduler_{};
  SystemVerilogUvmActivityService* activity_{};
  SystemVerilogUvmTlm1Limits limits_;
  std::shared_ptr<const void> owner_;
  std::map<std::uint64_t, Endpoint> endpoints_;
  std::map<std::uint64_t, Fifo> fifos_;
  std::map<std::uint64_t, TransportHandler> transport_handlers_;
  std::map<std::uint64_t, AnalysisSubscriber> analysis_subscribers_;
  std::map<std::uint64_t, Operation> operations_;
  std::uint64_t next_slot_{1};
  std::uint64_t next_declaration_order_{};
  std::size_t connections_{};
  std::size_t mutations_{};
  std::uint64_t revision_{1};
  std::uint64_t bound_revision_{};
  std::uint64_t next_operation_{1};
  std::uint64_t next_reservation_order_{};
  std::uint64_t next_payload_sequence_{};
  std::size_t execution_operations_{};
  std::size_t analysis_depth_{};
  std::size_t analysis_callbacks_reserved_{};
  std::size_t analysis_publications_{};
  std::uint64_t next_analysis_publication_{};
  std::uint64_t next_analysis_delivery_{};
};

}  // namespace fsim::runtime
