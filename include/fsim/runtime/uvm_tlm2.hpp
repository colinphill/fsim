// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/uvm_activity.hpp"
#include "fsim/runtime/uvm_component.hpp"

#include <compare>
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
#include <vector>

namespace fsim::runtime {

class SystemVerilogUvmTlm2Service;

class SystemVerilogUvmTlm2SocketHandle final {
 public:
  SystemVerilogUvmTlm2SocketHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmTlm2SocketHandle&,
      const SystemVerilogUvmTlm2SocketHandle&) = default;

 private:
  friend class SystemVerilogUvmTlm2Service;
  SystemVerilogUvmTlm2SocketHandle(
      std::shared_ptr<const void> owner,
      std::uint64_t slot,
      std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}
  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmTlm2TransactionHandle final {
 public:
  SystemVerilogUvmTlm2TransactionHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmTlm2TransactionHandle&,
      const SystemVerilogUvmTlm2TransactionHandle&) = default;

 private:
  friend class SystemVerilogUvmTlm2Service;
  SystemVerilogUvmTlm2TransactionHandle(
      std::shared_ptr<const void> owner,
      std::uint64_t slot,
      std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}
  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

enum class SystemVerilogUvmTlm2SocketKind : std::uint8_t {
  Initiator,
  Target,
  InitiatorPassthrough,
  TargetPassthrough,
};

enum class SystemVerilogUvmTlm2Protocol : std::uint8_t {
  Blocking,
  Nonblocking,
  Combined,
};

enum class SystemVerilogUvmTlm2Command : std::uint8_t {
  Read,
  Write,
  Ignore,
};

enum class SystemVerilogUvmTlm2ResponseStatus : std::uint8_t {
  Incomplete,
  Ok,
  GenericError,
  AddressError,
  CommandError,
  BurstError,
  ByteEnableError,
};

enum class SystemVerilogUvmTlm2PhaseKind : std::uint8_t {
  BeginRequest,
  EndRequest,
  BeginResponse,
  EndResponse,
  Custom,
};

enum class SystemVerilogUvmTlm2Sync : std::uint8_t {
  Accepted,
  Updated,
  Completed,
};

enum class SystemVerilogUvmTlm2TransactionState : std::uint8_t {
  Outstanding,
  Completed,
  Cancelled,
};

struct SystemVerilogUvmTlm2Profile {
  SystemVerilogUvmTlm2Protocol protocol{
      SystemVerilogUvmTlm2Protocol::Combined};
  std::string payload_type{"uvm_tlm2::uvm_tlm_generic_payload"};
  std::string phase_type{"uvm_tlm2::uvm_tlm_phase_e"};
  std::size_t bus_width_bytes{4};
  friend bool operator==(
      const SystemVerilogUvmTlm2Profile&,
      const SystemVerilogUvmTlm2Profile&) = default;
};

struct SystemVerilogUvmTlm2Phase {
  SystemVerilogUvmTlm2PhaseKind kind{
      SystemVerilogUvmTlm2PhaseKind::BeginRequest};
  std::string identity;
  std::string nominal_type{"uvm_tlm2::uvm_tlm_phase_e"};
  friend bool operator==(
      const SystemVerilogUvmTlm2Phase&,
      const SystemVerilogUvmTlm2Phase&) = default;
};

struct SystemVerilogUvmTlm2GenericPayload {
  std::string nominal_type{"uvm_tlm2::uvm_tlm_generic_payload"};
  SystemVerilogUvmTlm2Command command{SystemVerilogUvmTlm2Command::Ignore};
  std::uint64_t address{};
  std::vector<std::uint8_t> data;
  std::vector<std::uint8_t> byte_enables;
  std::size_t streaming_width{};
  std::map<std::string, std::vector<std::uint8_t>, std::less<>> extensions;
  SystemVerilogUvmTlm2ResponseStatus response_status{
      SystemVerilogUvmTlm2ResponseStatus::Incomplete};
  bool dmi_allowed{};
  SystemVerilogUvmRootHandle owner_root{};
  SystemVerilogUvmTlm2SocketHandle owner_socket;
  std::uint64_t transaction_id{};
  friend bool operator==(
      const SystemVerilogUvmTlm2GenericPayload&,
      const SystemVerilogUvmTlm2GenericPayload&) = default;
};

struct SystemVerilogUvmTlm2Dmi {
  std::uint64_t start_address{};
  std::uint64_t end_address{};
  bool read_allowed{};
  bool write_allowed{};
  SimulationTick read_latency{};
  SimulationTick write_latency{};
  std::vector<std::uint8_t> data;
};

struct SystemVerilogUvmTlm2SocketDescriptor {
  SystemVerilogUvmTlm2SocketKind kind{
      SystemVerilogUvmTlm2SocketKind::Initiator};
  SystemVerilogUvmTlm2Profile profile;
  SystemVerilogClassHandle component{};
  std::string name;
  std::size_t minimum_connections{1};
  std::size_t maximum_connections{1};
};

struct SystemVerilogUvmTlm2Limits {
  std::size_t maximum_sockets{65'536};
  std::size_t maximum_connections{262'144};
  std::size_t maximum_sockets_per_component{4'096};
  std::size_t maximum_name_bytes{1'024};
  std::size_t maximum_profile_bytes{4'096};
  std::size_t maximum_fanout{65'536};
  std::size_t maximum_depth{256};
  std::size_t maximum_hops{256};
  std::size_t maximum_payload_bytes{1U << 24U};
  std::size_t maximum_byte_enables{1U << 20U};
  std::size_t maximum_streaming_width{1U << 24U};
  std::size_t maximum_extensions{4'096};
  std::size_t maximum_extension_bytes{1U << 20U};
  std::size_t maximum_callbacks{1U << 20U};
  std::size_t maximum_outstanding_transactions{262'144};
  std::size_t maximum_dmi_bytes{1U << 26U};
  std::size_t maximum_mutations{1U << 20U};
};

class SystemVerilogUvmTlm2Error final : public std::runtime_error {
 public:
  SystemVerilogUvmTlm2Error(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }
 private:
  std::string code_;
};

struct SystemVerilogUvmTlm2SocketSnapshot {
  SystemVerilogUvmTlm2SocketHandle handle;
  SystemVerilogUvmTlm2SocketKind kind{
      SystemVerilogUvmTlm2SocketKind::Initiator};
  SystemVerilogUvmTlm2Profile profile;
  SystemVerilogClassHandle component{};
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::string debug_name;
  std::size_t minimum_connections{};
  std::size_t maximum_connections{};
  std::uint64_t declaration_order{};
  std::vector<SystemVerilogUvmTlm2SocketHandle> outbound;
  std::vector<SystemVerilogUvmTlm2SocketHandle> inbound;
  std::vector<SystemVerilogUvmTlm2SocketHandle> resolved_targets;
};

struct SystemVerilogUvmTlm2BlockingResult {
  SystemVerilogUvmTlm2SocketHandle target;
  SystemVerilogUvmTlm2GenericPayload payload;
  SimulationTick delay{};
  SimulationTick completion_time{};
};

struct SystemVerilogUvmTlm2NonblockingResult {
  SystemVerilogUvmTlm2Sync sync{SystemVerilogUvmTlm2Sync::Accepted};
  SystemVerilogUvmTlm2TransactionHandle transaction;
  SystemVerilogUvmTlm2GenericPayload payload;
  SystemVerilogUvmTlm2Phase phase;
  SimulationTick delay{};
};

struct SystemVerilogUvmTlm2TransactionSnapshot {
  SystemVerilogUvmTlm2TransactionHandle handle;
  SystemVerilogUvmTlm2TransactionState state{
      SystemVerilogUvmTlm2TransactionState::Outstanding};
  SystemVerilogUvmTlm2SocketHandle initiator;
  SystemVerilogUvmTlm2SocketHandle target;
  SystemVerilogUvmTlm2GenericPayload payload;
  SystemVerilogUvmTlm2Phase phase;
  SimulationTick delay{};
  SimulationTick last_update_time{};
  std::size_t hops{};
  std::size_t callbacks{};
};

class SystemVerilogUvmTlm2Service final {
 public:
  using BlockingHandler = std::function<void(
      SystemVerilogUvmTlm2GenericPayload&, SimulationTick&)>;
  using NonblockingHandler = std::function<SystemVerilogUvmTlm2Sync(
      SystemVerilogUvmTlm2GenericPayload&,
      SystemVerilogUvmTlm2Phase&,
      SimulationTick&)>;
  using DebugHandler = std::function<std::size_t(
      SystemVerilogUvmTlm2GenericPayload&)>;
  using DmiHandler = std::function<std::optional<SystemVerilogUvmTlm2Dmi>(
      SystemVerilogUvmTlm2GenericPayload&)>;
  using DmiInvalidationHandler = std::function<void(
      std::uint64_t, std::uint64_t)>;

  explicit SystemVerilogUvmTlm2Service(
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmTlm2Limits limits = {});
  SystemVerilogUvmTlm2Service(
      SystemVerilogUvmComponentService& components,
      Scheduler& scheduler,
      SystemVerilogUvmTlm2Limits limits = {});
  ~SystemVerilogUvmTlm2Service();
  SystemVerilogUvmTlm2Service(const SystemVerilogUvmTlm2Service&) = delete;
  SystemVerilogUvmTlm2Service& operator=(
      const SystemVerilogUvmTlm2Service&) = delete;

  void set_scheduler(Scheduler& scheduler) noexcept { scheduler_ = &scheduler; }
  void set_activity_service(SystemVerilogUvmActivityService& activity)
      noexcept {
    activity_ = &activity;
  }
  [[nodiscard]] SystemVerilogUvmTlm2SocketHandle register_socket(
      SystemVerilogUvmTlm2SocketDescriptor descriptor);
  void connect(
      SystemVerilogUvmTlm2SocketHandle source,
      SystemVerilogUvmTlm2SocketHandle target);
  void bind_all();
  [[nodiscard]] bool contains(
      SystemVerilogUvmTlm2SocketHandle socket) const noexcept;
  [[nodiscard]] SystemVerilogUvmTlm2SocketSnapshot snapshot(
      SystemVerilogUvmTlm2SocketHandle socket) const;
  [[nodiscard]] std::vector<SystemVerilogUvmTlm2SocketHandle> sockets() const;
  [[nodiscard]] bool binding_valid() const noexcept {
    return bound_revision_ == revision_;
  }

  void set_blocking_handler(
      SystemVerilogUvmTlm2SocketHandle target, BlockingHandler handler);
  void set_forward_handler(
      SystemVerilogUvmTlm2SocketHandle target, NonblockingHandler handler);
  void set_backward_handler(
      SystemVerilogUvmTlm2SocketHandle initiator, NonblockingHandler handler);
  void set_debug_handler(
      SystemVerilogUvmTlm2SocketHandle target, DebugHandler handler);
  void set_dmi_handler(
      SystemVerilogUvmTlm2SocketHandle target, DmiHandler handler);
  void set_dmi_invalidation_handler(
      SystemVerilogUvmTlm2SocketHandle initiator,
      DmiInvalidationHandler handler);

  [[nodiscard]] SystemVerilogUvmTlm2BlockingResult b_transport(
      SystemVerilogUvmTlm2SocketHandle initiator,
      SystemVerilogUvmTlm2GenericPayload payload,
      SimulationTick delay = 0,
      std::size_t binding_index = 0);
  [[nodiscard]] SystemVerilogUvmTlm2NonblockingResult nb_transport_fw(
      SystemVerilogUvmTlm2SocketHandle initiator,
      SystemVerilogUvmTlm2GenericPayload payload,
      SystemVerilogUvmTlm2Phase phase,
      SimulationTick delay = 0,
      std::size_t binding_index = 0);
  [[nodiscard]] SystemVerilogUvmTlm2NonblockingResult nb_transport_bw(
      SystemVerilogUvmTlm2TransactionHandle transaction,
      SystemVerilogUvmTlm2Phase phase,
      SimulationTick delay = 0);
  [[nodiscard]] std::size_t transport_dbg(
      SystemVerilogUvmTlm2SocketHandle initiator,
      SystemVerilogUvmTlm2GenericPayload& payload,
      std::size_t binding_index = 0);
  [[nodiscard]] std::optional<SystemVerilogUvmTlm2Dmi> get_direct_mem_ptr(
      SystemVerilogUvmTlm2SocketHandle initiator,
      SystemVerilogUvmTlm2GenericPayload& payload,
      std::size_t binding_index = 0);
  void invalidate_direct_memory(
      SystemVerilogUvmTlm2SocketHandle initiator,
      std::uint64_t start_address,
      std::uint64_t end_address);

  [[nodiscard]] SystemVerilogUvmTlm2TransactionSnapshot transaction_snapshot(
      SystemVerilogUvmTlm2TransactionHandle transaction) const;
  [[nodiscard]] std::vector<SystemVerilogUvmTlm2TransactionHandle>
  transactions() const;
  void cancel_transaction(SystemVerilogUvmTlm2TransactionHandle transaction);
  void cancel_root(SystemVerilogUvmRootHandle root) noexcept;
  void cancel_all() noexcept;
  [[nodiscard]] std::size_t outstanding_count() const noexcept;
  [[nodiscard]] std::size_t callback_count() const noexcept {
    return callbacks_;
  }
  [[nodiscard]] std::size_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] const SystemVerilogUvmTlm2Limits& limits() const noexcept {
    return limits_;
  }

 private:
  struct Socket {
    SystemVerilogUvmTlm2SocketDescriptor descriptor;
    std::uint64_t slot{};
    std::uint64_t generation{1};
    SystemVerilogUvmRootHandle root{};
    std::string full_name;
    std::string debug_name;
    std::uint64_t declaration_order{};
    std::vector<std::uint64_t> outbound;
    std::vector<std::uint64_t> inbound;
    std::vector<SystemVerilogUvmTlm2SocketHandle> resolved;
  };
  struct Transaction {
    SystemVerilogUvmTlm2TransactionSnapshot value;
    std::uint64_t generation{1};
  };

  [[nodiscard]] Socket& socket(SystemVerilogUvmTlm2SocketHandle handle);
  [[nodiscard]] const Socket& socket(
      SystemVerilogUvmTlm2SocketHandle handle) const;
  [[nodiscard]] SystemVerilogUvmTlm2SocketHandle socket_handle(
      std::uint64_t slot) const;
  void require_live(const Socket& socket) const;
  [[nodiscard]] bool reaches(
      std::uint64_t from, std::uint64_t wanted,
      std::size_t& work) const;
  void collect_targets(
      std::uint64_t slot, std::size_t depth, std::size_t& work,
      std::vector<SystemVerilogUvmTlm2SocketHandle>& result,
      std::vector<std::uint64_t>& path) const;
  [[nodiscard]] std::uint64_t target_slot(
      SystemVerilogUvmTlm2SocketHandle initiator,
      SystemVerilogUvmTlm2Protocol required,
      std::size_t binding_index) const;
  [[nodiscard]] std::size_t hop_count(
      std::uint64_t source, std::uint64_t target) const;
  void validate_payload(
      SystemVerilogUvmTlm2GenericPayload& payload,
      const Socket& owner,
      bool assign_transaction);
  void validate_phase(
      const SystemVerilogUvmTlm2Phase& phase,
      const Socket& owner) const;
  void validate_dmi(const SystemVerilogUvmTlm2Dmi& dmi) const;
  void reserve_callback();
  [[nodiscard]] SystemVerilogUvmTlm2TransactionHandle transaction_handle(
      std::uint64_t slot) const;
  [[nodiscard]] Transaction& transaction(
      SystemVerilogUvmTlm2TransactionHandle handle);
  [[nodiscard]] const Transaction& transaction(
      SystemVerilogUvmTlm2TransactionHandle handle) const;

  SystemVerilogUvmComponentService* components_{};
  Scheduler* scheduler_{};
  SystemVerilogUvmActivityService* activity_{};
  SystemVerilogUvmTlm2Limits limits_;
  std::shared_ptr<const void> owner_;
  std::map<std::uint64_t, Socket> sockets_;
  std::map<std::uint64_t, Transaction> transactions_;
  std::map<std::uint64_t, BlockingHandler> blocking_handlers_;
  std::map<std::uint64_t, NonblockingHandler> forward_handlers_;
  std::map<std::uint64_t, NonblockingHandler> backward_handlers_;
  std::map<std::uint64_t, DebugHandler> debug_handlers_;
  std::map<std::uint64_t, DmiHandler> dmi_handlers_;
  std::map<std::uint64_t, DmiInvalidationHandler> dmi_invalidation_handlers_;
  std::uint64_t next_socket_{1};
  std::uint64_t next_declaration_order_{};
  std::uint64_t next_transaction_{1};
  std::uint64_t next_payload_transaction_id_{1};
  std::size_t connections_{};
  std::size_t mutations_{};
  std::size_t callbacks_{};
  std::uint64_t revision_{1};
  std::uint64_t bound_revision_{};
};

}  // namespace fsim::runtime
