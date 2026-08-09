// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/scheduler.hpp"
#include "fsim/runtime/uvm_activity.hpp"
#include "fsim/runtime/uvm_component.hpp"

#include <array>
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
#include <vector>

namespace fsim::runtime {

class SystemVerilogUvmPhaseService;
class SystemVerilogUvmObjectionService;
class SystemVerilogUvmTlm1Service;

class SystemVerilogUvmDomainHandle final {
 public:
  SystemVerilogUvmDomainHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmDomainHandle&,
      const SystemVerilogUvmDomainHandle&) = default;

 private:
  friend class SystemVerilogUvmPhaseService;
  SystemVerilogUvmDomainHandle(
      std::shared_ptr<const void> owner,
      const std::uint64_t slot,
      const std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmPhaseHandle final {
 public:
  SystemVerilogUvmPhaseHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmPhaseHandle&,
      const SystemVerilogUvmPhaseHandle&) = default;

 private:
  friend class SystemVerilogUvmPhaseService;
  friend class SystemVerilogUvmObjectionService;
  SystemVerilogUvmPhaseHandle(
      std::shared_ptr<const void> owner,
      const std::uint64_t slot,
      const std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmPhaseProcessHandle final {
 public:
  SystemVerilogUvmPhaseProcessHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(
      const SystemVerilogUvmPhaseProcessHandle&,
      const SystemVerilogUvmPhaseProcessHandle&) = default;

 private:
  friend class SystemVerilogUvmPhaseService;
  SystemVerilogUvmPhaseProcessHandle(
      std::shared_ptr<const void> owner,
      const std::uint64_t slot,
      const std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

enum class SystemVerilogUvmDomainKind : std::uint8_t {
  Common,
  Runtime,
  Custom,
};

enum class SystemVerilogUvmPhaseKind : std::uint8_t {
  Build,
  Connect,
  EndOfElaboration,
  StartOfSimulation,
  Run,
  Extract,
  Check,
  Report,
  Final,
  PreReset,
  Reset,
  PostReset,
  PreConfigure,
  Configure,
  PostConfigure,
  PreMain,
  Main,
  PostMain,
  PreShutdown,
  Shutdown,
  PostShutdown,
  Custom,
};

enum class SystemVerilogUvmPhaseExecutionKind : std::uint8_t {
  Function,
  Task,
};

enum class SystemVerilogUvmPhaseState : std::uint16_t {
  Dormant = 1U,
  Scheduled = 2U,
  Syncing = 4U,
  Started = 8U,
  Executing = 16U,
  ReadyToEnd = 32U,
  Ended = 64U,
  Cleanup = 128U,
  Done = 256U,
  Jumping = 512U,
};

enum class SystemVerilogUvmPhaseTraversal : std::uint8_t {
  TopDown,
  BottomUp,
};

enum class SystemVerilogUvmPhaseCallbackKind : std::uint8_t {
  PhaseStarted,
  Execute,
  PhaseReadyToEnd,
  PhaseEnded,
};

enum class SystemVerilogUvmTaskPhaseStatus : std::uint8_t {
  Completed,
  Suspended,
};

enum class SystemVerilogUvmPhaseProcessState : std::uint8_t {
  Running,
  Completed,
  Cancelled,
};

enum class SystemVerilogUvmPhaseJumpKind : std::uint8_t {
  Forward,
  Backward,
};

enum class SystemVerilogUvmQuiescenceStatus : std::uint8_t {
  Completed,
  Stopped,
  TimedOut,
};

[[nodiscard]] std::string_view systemverilog_uvm_phase_identity(
    SystemVerilogUvmPhaseKind kind) noexcept;
[[nodiscard]] SystemVerilogUvmDomainKind systemverilog_uvm_phase_domain_kind(
    SystemVerilogUvmPhaseKind kind);
[[nodiscard]] SystemVerilogUvmPhaseExecutionKind
systemverilog_uvm_phase_execution_kind(SystemVerilogUvmPhaseKind kind);

struct SystemVerilogUvmPhaseLimits {
  std::size_t maximum_domains{64};
  std::size_t maximum_phases{4'096};
  std::size_t maximum_phases_per_domain{512};
  std::size_t maximum_edges{16'384};
  std::size_t maximum_roots_per_domain{64};
  std::size_t maximum_depth{256};
  std::size_t maximum_identity_bytes{1'024};
  std::size_t maximum_traversal_work{65'536};
  std::size_t maximum_mutations{65'536};
  std::size_t maximum_phase_processes{65'536};
  std::size_t maximum_process_depth{256};
  std::size_t maximum_scheduled_processes{65'536};
  std::size_t maximum_ready_to_end_reentries{256};
  std::size_t maximum_quiescence_iterations{4'096};
  std::size_t maximum_quiescence_callbacks{1U << 20U};
  std::size_t maximum_zero_time_iterations{256};
};

class SystemVerilogUvmPhaseError final : public std::runtime_error {
 public:
  SystemVerilogUvmPhaseError(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

 private:
  std::string code_;
};

struct SystemVerilogUvmDomainSnapshot {
  SystemVerilogUvmDomainHandle handle;
  std::string identity;
  SystemVerilogUvmDomainKind kind{SystemVerilogUvmDomainKind::Custom};
  std::uint64_t registration_order{};
  std::vector<SystemVerilogUvmPhaseHandle> phases;
  std::vector<SystemVerilogUvmRootHandle> roots;
  std::optional<SystemVerilogUvmPhaseHandle> with_phase;
};

struct SystemVerilogUvmPhaseSnapshot {
  SystemVerilogUvmPhaseHandle handle;
  SystemVerilogUvmDomainHandle domain;
  SystemVerilogUvmPhaseKind kind{SystemVerilogUvmPhaseKind::Custom};
  SystemVerilogUvmPhaseExecutionKind execution{
      SystemVerilogUvmPhaseExecutionKind::Function};
  SystemVerilogUvmPhaseTraversal traversal{
      SystemVerilogUvmPhaseTraversal::BottomUp};
  SystemVerilogUvmPhaseState state{SystemVerilogUvmPhaseState::Dormant};
  std::string identity;
  std::uint64_t registration_order{};
  std::optional<SystemVerilogUvmPhaseHandle> parent;
  std::vector<SystemVerilogUvmPhaseHandle> predecessors;
  std::vector<SystemVerilogUvmPhaseHandle> successors;
  std::vector<SystemVerilogUvmPhaseHandle> synchronized;
  std::vector<SystemVerilogUvmRootHandle> roots;
};

struct SystemVerilogUvmPhaseExecutionEvent {
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle component{};
  SystemVerilogUvmPhaseCallbackKind callback{
      SystemVerilogUvmPhaseCallbackKind::Execute};
  SystemVerilogUvmPhaseState state{SystemVerilogUvmPhaseState::Dormant};
  std::uint64_t sequence{};
};

struct SystemVerilogUvmPhaseExecutionFailure {
  std::string diagnostic_code;
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle component{};
  SystemVerilogUvmPhaseCallbackKind callback{
      SystemVerilogUvmPhaseCallbackKind::Execute};
  std::string message;
};

struct SystemVerilogUvmPhaseProcessSnapshot {
  SystemVerilogUvmPhaseProcessHandle handle;
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle component{};
  SystemVerilogUvmPhaseProcessState state{
      SystemVerilogUvmPhaseProcessState::Running};
  std::uint64_t registration_order{};
  std::optional<SystemVerilogUvmPhaseProcessHandle> parent;
  std::size_t depth{};
  bool scheduled{};
};

struct SystemVerilogUvmPhaseExecutionResult {
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseState final_state{
      SystemVerilogUvmPhaseState::Dormant};
  std::vector<SystemVerilogUvmPhaseExecutionEvent> events;
  std::vector<SystemVerilogUvmPhaseExecutionFailure> failures;
  std::vector<SystemVerilogUvmPhaseProcessHandle> processes;
  std::vector<SystemVerilogUvmPhaseProcessSnapshot> final_processes;

  [[nodiscard]] bool success() const noexcept { return failures.empty(); }
};

struct SystemVerilogUvmPhaseJumpResult {
  SystemVerilogUvmPhaseJumpKind kind{
      SystemVerilogUvmPhaseJumpKind::Forward};
  SystemVerilogUvmPhaseHandle from;
  SystemVerilogUvmPhaseHandle target;
  std::vector<SystemVerilogUvmPhaseHandle> reset;
  std::vector<SystemVerilogUvmPhaseHandle> skipped;
  std::vector<SystemVerilogUvmPhaseProcessHandle> cancelled;
};

struct SystemVerilogUvmQuiescenceResult {
  SystemVerilogUvmQuiescenceStatus status{
      SystemVerilogUvmQuiescenceStatus::Completed};
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseState final_state{
      SystemVerilogUvmPhaseState::Dormant};
  SimulationTick time{};
  std::size_t iterations{};
  std::uint64_t callbacks{};
  std::vector<SystemVerilogUvmPhaseProcessHandle> cancelled;
  std::optional<SystemVerilogUvmPhaseExecutionResult> execution;
};

inline constexpr std::size_t kSystemVerilogUvmStandardPhaseCount =
    static_cast<std::size_t>(SystemVerilogUvmPhaseKind::Custom);

struct SystemVerilogUvmStandardSchedule {
  SystemVerilogUvmDomainHandle common_domain;
  SystemVerilogUvmDomainHandle runtime_domain;
  std::array<
      SystemVerilogUvmPhaseHandle,
      kSystemVerilogUvmStandardPhaseCount> phases;

  friend bool operator==(
      const SystemVerilogUvmStandardSchedule&,
      const SystemVerilogUvmStandardSchedule&) = default;

  [[nodiscard]] SystemVerilogUvmPhaseHandle phase(
      const SystemVerilogUvmPhaseKind kind) const noexcept {
    const auto index = static_cast<std::size_t>(kind);
    return index < phases.size()
        ? phases[index]
        : SystemVerilogUvmPhaseHandle{};
  }
};

struct SystemVerilogUvmPhasePlacement {
  std::optional<SystemVerilogUvmPhaseHandle> with_phase;
  std::optional<SystemVerilogUvmPhaseHandle> after_phase;
  std::optional<SystemVerilogUvmPhaseHandle> before_phase;
  std::optional<SystemVerilogUvmPhaseHandle> parent;

  [[nodiscard]] static SystemVerilogUvmPhasePlacement parallel_with(
      const SystemVerilogUvmPhaseHandle phase) {
    SystemVerilogUvmPhasePlacement result;
    result.with_phase = phase;
    return result;
  }
  [[nodiscard]] static SystemVerilogUvmPhasePlacement after(
      const SystemVerilogUvmPhaseHandle phase) {
    SystemVerilogUvmPhasePlacement result;
    result.after_phase = phase;
    return result;
  }
  [[nodiscard]] static SystemVerilogUvmPhasePlacement before(
      const SystemVerilogUvmPhaseHandle phase) {
    SystemVerilogUvmPhasePlacement result;
    result.before_phase = phase;
    return result;
  }
  [[nodiscard]] static SystemVerilogUvmPhasePlacement between(
      const SystemVerilogUvmPhaseHandle after,
      const SystemVerilogUvmPhaseHandle before) {
    SystemVerilogUvmPhasePlacement result;
    result.after_phase = after;
    result.before_phase = before;
    return result;
  }
};

/// Simulation-owned UVM phase graph. Handles retain an owner token and slot
/// generation, mutations are transactional, and every traversal is bounded.
/// This service defines graph identity only; phase scheduling begins in the
/// later Batch 160 execution changes.
class SystemVerilogUvmPhaseService final {
 public:
  using FunctionPhaseCallback = std::function<void(
      SystemVerilogClassHandle,
      SystemVerilogUvmPhaseHandle,
      SystemVerilogUvmPhaseCallbackKind)>;
  using TaskPhaseCallback = std::function<SystemVerilogUvmTaskPhaseStatus(
      SystemVerilogClassHandle,
      SystemVerilogUvmPhaseHandle,
      SystemVerilogUvmPhaseProcessHandle)>;
  using ScheduledProcessCallback = std::function<void(
      SystemVerilogUvmPhaseProcessHandle)>;

  explicit SystemVerilogUvmPhaseService(
      SystemVerilogUvmComponentService& components,
      SystemVerilogUvmPhaseLimits limits = {});
  SystemVerilogUvmPhaseService(
      SystemVerilogUvmComponentService& components,
      Scheduler& scheduler,
      SystemVerilogUvmPhaseLimits limits = {});
  ~SystemVerilogUvmPhaseService();
  SystemVerilogUvmPhaseService(const SystemVerilogUvmPhaseService&) = delete;
  SystemVerilogUvmPhaseService& operator=(
      const SystemVerilogUvmPhaseService&) = delete;
  SystemVerilogUvmPhaseService(SystemVerilogUvmPhaseService&&) = delete;
  SystemVerilogUvmPhaseService& operator=(
      SystemVerilogUvmPhaseService&&) = delete;

  [[nodiscard]] SystemVerilogUvmDomainHandle create_domain(
      std::string identity,
      SystemVerilogUvmDomainKind kind);
  [[nodiscard]] SystemVerilogUvmPhaseHandle create_standard_phase(
      SystemVerilogUvmDomainHandle domain,
      SystemVerilogUvmPhaseKind kind,
      std::optional<SystemVerilogUvmPhaseHandle> parent = std::nullopt);
  [[nodiscard]] SystemVerilogUvmPhaseHandle create_custom_phase(
      SystemVerilogUvmDomainHandle domain,
      std::string identity,
      SystemVerilogUvmPhaseExecutionKind execution,
      std::optional<SystemVerilogUvmPhaseHandle> parent = std::nullopt);
  [[nodiscard]] SystemVerilogUvmStandardSchedule create_standard_schedule(
      std::span<const SystemVerilogUvmRootHandle> roots = {});
  [[nodiscard]] SystemVerilogUvmPhaseHandle insert_custom_phase(
      SystemVerilogUvmDomainHandle domain,
      std::string identity,
      SystemVerilogUvmPhaseExecutionKind execution,
      SystemVerilogUvmPhasePlacement placement = {});

  void connect(
      SystemVerilogUvmPhaseHandle predecessor,
      SystemVerilogUvmPhaseHandle successor);
  void disconnect(
      SystemVerilogUvmPhaseHandle predecessor,
      SystemVerilogUvmPhaseHandle successor);
  void participate(
      SystemVerilogUvmDomainHandle domain,
      SystemVerilogUvmRootHandle root);
  void unparticipate(
      SystemVerilogUvmDomainHandle domain,
      SystemVerilogUvmRootHandle root);
  void participate_standard_root(SystemVerilogUvmRootHandle root);
  void unparticipate_standard_root(SystemVerilogUvmRootHandle root);
  void place_domain_with(
      SystemVerilogUvmDomainHandle domain,
      SystemVerilogUvmPhaseHandle phase);
  void clear_domain_placement(SystemVerilogUvmDomainHandle domain);
  void synchronize_domains(
      SystemVerilogUvmDomainHandle source,
      SystemVerilogUvmDomainHandle target,
      std::optional<SystemVerilogUvmPhaseHandle> phase = std::nullopt,
      std::optional<SystemVerilogUvmPhaseHandle> with_phase = std::nullopt);
  void unsynchronize_domains(
      SystemVerilogUvmDomainHandle source,
      SystemVerilogUvmDomainHandle target,
      std::optional<SystemVerilogUvmPhaseHandle> phase = std::nullopt,
      std::optional<SystemVerilogUvmPhaseHandle> with_phase = std::nullopt);
  [[nodiscard]] SystemVerilogUvmPhaseExecutionResult execute_function_phase(
      SystemVerilogUvmPhaseHandle phase,
      const FunctionPhaseCallback& callback);
  [[nodiscard]] SystemVerilogUvmPhaseExecutionResult execute_task_phase(
      SystemVerilogUvmPhaseHandle phase,
      const FunctionPhaseCallback& hook_callback,
      const TaskPhaseCallback& task_callback);
  [[nodiscard]] std::vector<SystemVerilogUvmPhaseExecutionResult>
  execute_synchronized_task_phases(
      std::span<const SystemVerilogUvmPhaseHandle> phases,
      const FunctionPhaseCallback& hook_callback,
      const TaskPhaseCallback& task_callback);
  void complete_task_process(SystemVerilogUvmPhaseProcessHandle process);
  void cancel_task_process(SystemVerilogUvmPhaseProcessHandle process);
  [[nodiscard]] SystemVerilogUvmPhaseProcessHandle begin_child_process(
      SystemVerilogUvmPhaseProcessHandle parent);
  [[nodiscard]] SystemVerilogUvmPhaseProcessHandle schedule_child_process(
      SystemVerilogUvmPhaseProcessHandle parent,
      SimulationTick delay,
      ScheduledProcessCallback callback);
  [[nodiscard]] std::vector<SystemVerilogUvmPhaseProcessHandle>
  timeout_task_phase(SystemVerilogUvmPhaseHandle phase);
  [[nodiscard]] std::vector<SystemVerilogUvmPhaseProcessHandle>
  teardown_root(SystemVerilogUvmRootHandle root);
  [[nodiscard]] SystemVerilogUvmQuiescenceResult settle_task_phase(
      SystemVerilogUvmPhaseHandle phase,
      const FunctionPhaseCallback& hook_callback,
      std::optional<SimulationTick> deadline = std::nullopt);
  [[nodiscard]] SystemVerilogUvmPhaseExecutionResult complete_task_phase(
      SystemVerilogUvmPhaseHandle phase,
      const FunctionPhaseCallback& hook_callback);
  [[nodiscard]] SystemVerilogUvmPhaseProcessSnapshot process_snapshot(
      SystemVerilogUvmPhaseProcessHandle process) const;
  [[nodiscard]] std::vector<SystemVerilogUvmPhaseProcessHandle>
  phase_processes(SystemVerilogUvmPhaseHandle phase) const;
  [[nodiscard]] std::vector<SystemVerilogUvmPhaseProcessHandle> processes()
      const;
  [[nodiscard]] SystemVerilogUvmPhaseJumpResult jump(
      SystemVerilogUvmPhaseHandle from,
      SystemVerilogUvmPhaseHandle target);
  void set_objection_service(SystemVerilogUvmObjectionService& objections) {
    objections_ = &objections;
  }
  void set_activity_service(SystemVerilogUvmActivityService& activity)
      noexcept {
    activity_ = &activity;
  }
  void set_tlm1_service(SystemVerilogUvmTlm1Service& tlm1) {
    tlm1_ = &tlm1;
  }
  void clear_tlm1_service(const SystemVerilogUvmTlm1Service& tlm1) noexcept {
    if (tlm1_ == &tlm1) tlm1_ = nullptr;
  }
  void set_scheduler(Scheduler& scheduler);

  [[nodiscard]] bool contains(
      SystemVerilogUvmDomainHandle domain) const noexcept;
  [[nodiscard]] bool contains(
      SystemVerilogUvmPhaseHandle phase) const noexcept;
  [[nodiscard]] bool contains(
      SystemVerilogUvmPhaseProcessHandle process) const noexcept;
  [[nodiscard]] SystemVerilogUvmDomainSnapshot snapshot(
      SystemVerilogUvmDomainHandle domain) const;
  [[nodiscard]] SystemVerilogUvmPhaseSnapshot snapshot(
      SystemVerilogUvmPhaseHandle phase) const;
  [[nodiscard]] std::vector<SystemVerilogUvmDomainHandle> domains() const;
  [[nodiscard]] std::vector<SystemVerilogUvmPhaseHandle> phases(
      SystemVerilogUvmDomainHandle domain) const;
  [[nodiscard]] std::vector<SystemVerilogUvmPhaseHandle> topological_order(
      SystemVerilogUvmDomainHandle domain) const;
  [[nodiscard]] const std::optional<SystemVerilogUvmStandardSchedule>&
  standard_schedule() const noexcept {
    return standard_schedule_;
  }

  [[nodiscard]] std::size_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] std::size_t edge_count() const noexcept {
    return edges_;
  }
  [[nodiscard]] std::size_t process_count() const noexcept {
    return processes_.size();
  }
  [[nodiscard]] const SystemVerilogUvmPhaseLimits& limits() const noexcept {
    return limits_;
  }

 private:
  SystemVerilogUvmPhaseService(
      SystemVerilogUvmComponentService& components,
      Scheduler* scheduler,
      SystemVerilogUvmPhaseLimits limits);
  struct Domain;
  struct Phase;
  struct PhaseProcess;

  [[nodiscard]] Domain& domain(SystemVerilogUvmDomainHandle handle);
  [[nodiscard]] const Domain& domain(
      SystemVerilogUvmDomainHandle handle) const;
  [[nodiscard]] Phase& phase(SystemVerilogUvmPhaseHandle handle);
  [[nodiscard]] const Phase& phase(
      SystemVerilogUvmPhaseHandle handle) const;
  [[nodiscard]] PhaseProcess& process(
      SystemVerilogUvmPhaseProcessHandle handle);
  [[nodiscard]] const PhaseProcess& process(
      SystemVerilogUvmPhaseProcessHandle handle) const;
  [[nodiscard]] SystemVerilogUvmPhaseHandle create_phase(
      SystemVerilogUvmDomainHandle domain,
      std::string identity,
      SystemVerilogUvmPhaseKind kind,
      SystemVerilogUvmPhaseExecutionKind execution,
      std::optional<SystemVerilogUvmPhaseHandle> parent);
  [[nodiscard]] bool reachable(
      std::uint64_t from,
      std::uint64_t target) const;
  [[nodiscard]] std::size_t parent_depth(
      std::optional<SystemVerilogUvmPhaseHandle> parent,
      SystemVerilogUvmDomainHandle domain) const;
  [[nodiscard]] std::vector<std::pair<std::uint64_t, std::uint64_t>>
  synchronization_pairs(
      SystemVerilogUvmDomainHandle source,
      SystemVerilogUvmDomainHandle target,
      std::optional<SystemVerilogUvmPhaseHandle> phase,
      std::optional<SystemVerilogUvmPhaseHandle> with_phase) const;
  void validate_identity(std::string_view identity) const;
  void require_mutation_budget(std::size_t count = 1U) const;
  void reclaim_processes(Phase& phase) noexcept;
  void cancel_process_tree(
      std::uint64_t slot,
      std::vector<SystemVerilogUvmPhaseProcessHandle>* cancelled = nullptr)
      noexcept;
  [[nodiscard]] bool has_running_child(const PhaseProcess& process) const;
  void set_state(Phase& phase, SystemVerilogUvmPhaseState state);

  SystemVerilogUvmComponentService* components_{};
  Scheduler* scheduler_{};
  SystemVerilogUvmObjectionService* objections_{};
  SystemVerilogUvmTlm1Service* tlm1_{};
  SystemVerilogUvmActivityService* activity_{};
  SystemVerilogUvmPhaseLimits limits_;
  std::shared_ptr<const void> owner_;
  std::map<std::uint64_t, Domain> domains_;
  std::map<std::string, std::uint64_t, std::less<>> domains_by_identity_;
  std::map<std::uint64_t, Phase> phases_;
  std::map<std::uint64_t, PhaseProcess> processes_;
  std::uint64_t next_domain_{1};
  std::uint64_t next_phase_{1};
  std::uint64_t next_registration_order_{};
  std::uint64_t next_process_{1};
  std::uint64_t next_process_registration_order_{};
  std::size_t edges_{};
  std::size_t mutations_{};
  std::optional<SystemVerilogUvmStandardSchedule> standard_schedule_;
};

}  // namespace fsim::runtime
