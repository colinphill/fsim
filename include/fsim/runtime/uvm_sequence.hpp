// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/runtime/class_randomize.hpp"
#include "fsim/runtime/uvm_component.hpp"
#include "fsim/runtime/uvm_config_db.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_phase.hpp"
#include "fsim/runtime/uvm_tlm1.hpp"

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

class SystemVerilogUvmSequenceService;

class SystemVerilogUvmSequenceRequestHandle final {
public:
  SystemVerilogUvmSequenceRequestHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool
  operator==(const SystemVerilogUvmSequenceRequestHandle &,
             const SystemVerilogUvmSequenceRequestHandle &) = default;

private:
  friend class SystemVerilogUvmSequenceService;
  SystemVerilogUvmSequenceRequestHandle(std::shared_ptr<const void> owner,
                                        std::uint64_t slot,
                                        std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmSequenceAccessHandle final {
public:
  SystemVerilogUvmSequenceAccessHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool
  operator==(const SystemVerilogUvmSequenceAccessHandle &,
             const SystemVerilogUvmSequenceAccessHandle &) = default;

private:
  friend class SystemVerilogUvmSequenceService;
  SystemVerilogUvmSequenceAccessHandle(std::shared_ptr<const void> owner,
                                       std::uint64_t slot,
                                       std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}
  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmSequenceTransactionHandle final {
public:
  SystemVerilogUvmSequenceTransactionHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool
  operator==(const SystemVerilogUvmSequenceTransactionHandle &,
             const SystemVerilogUvmSequenceTransactionHandle &) = default;

private:
  friend class SystemVerilogUvmSequenceService;
  SystemVerilogUvmSequenceTransactionHandle(std::shared_ptr<const void> owner,
                                            std::uint64_t slot,
                                            std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}
  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmSequenceRoleHandle final {
public:
  SystemVerilogUvmSequenceRoleHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const SystemVerilogUvmSequenceRoleHandle &,
                         const SystemVerilogUvmSequenceRoleHandle &) = default;

private:
  friend class SystemVerilogUvmSequenceService;
  SystemVerilogUvmSequenceRoleHandle(std::shared_ptr<const void> owner,
                                     std::uint64_t slot,
                                     std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}
  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmSequenceItemHandle final {
public:
  SystemVerilogUvmSequenceItemHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const SystemVerilogUvmSequenceItemHandle &,
                         const SystemVerilogUvmSequenceItemHandle &) = default;

private:
  friend class SystemVerilogUvmSequenceService;
  SystemVerilogUvmSequenceItemHandle(std::shared_ptr<const void> owner,
                                     std::uint64_t slot,
                                     std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmSequenceHandle final {
public:
  SystemVerilogUvmSequenceHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const SystemVerilogUvmSequenceHandle &,
                         const SystemVerilogUvmSequenceHandle &) = default;

private:
  friend class SystemVerilogUvmSequenceService;
  SystemVerilogUvmSequenceHandle(std::shared_ptr<const void> owner,
                                 std::uint64_t slot, std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

class SystemVerilogUvmSequencerHandle final {
public:
  SystemVerilogUvmSequencerHandle() = default;
  [[nodiscard]] bool valid() const noexcept {
    return owner_ && slot_ != 0 && generation_ != 0;
  }
  explicit operator bool() const noexcept { return valid(); }
  friend bool operator==(const SystemVerilogUvmSequencerHandle &,
                         const SystemVerilogUvmSequencerHandle &) = default;

private:
  friend class SystemVerilogUvmSequenceService;
  SystemVerilogUvmSequencerHandle(std::shared_ptr<const void> owner,
                                  std::uint64_t slot, std::uint64_t generation)
      : owner_(std::move(owner)), slot_(slot), generation_(generation) {}

  std::shared_ptr<const void> owner_;
  std::uint64_t slot_{};
  std::uint64_t generation_{};
};

enum class SystemVerilogUvmSequenceState : std::uint8_t {
  Created,
  PreStart,
  PreBody,
  Body,
  PostBody,
  PostStart,
  Ended,
  Stopped,
  Finished,
};

enum class SystemVerilogUvmSequenceItemState : std::uint8_t {
  Created,
  Owned,
  InFlight,
  Completed,
  Cancelled,
  Routed,
};

enum class SystemVerilogUvmSequencerState : std::uint8_t {
  Registered,
  Stopping,
  Stopped,
};

enum class SystemVerilogUvmSequenceItemRole : std::uint8_t {
  Request,
  Response,
};

enum class SystemVerilogUvmSequenceCallbackKind : std::uint8_t {
  StateTransition,
  PreStart,
  PreBody,
  Body,
  PostBody,
  PostStart,
};

enum class SystemVerilogUvmSequenceArbitrationMode : std::uint8_t {
  Fifo,
  Weighted,
  Random,
  StrictFifo,
  StrictRandom,
  User,
};

enum class SystemVerilogUvmSequenceSelectionStatus : std::uint8_t {
  Selected,
  Empty,
  WaitingForRelevant,
};

enum class SystemVerilogUvmResponseOverflowPolicy : std::uint8_t {
  Error,
  DropOldest,
  DropNewest,
};

enum class SystemVerilogUvmResponseRouteStatus : std::uint8_t {
  Queued,
  DroppedOldest,
  DroppedNewest,
};

enum class SystemVerilogUvmSequenceAccessKind : std::uint8_t {
  Lock,
  Grab,
};

enum class SystemVerilogUvmSequenceAccessState : std::uint8_t {
  Pending,
  Granted,
};

enum class SystemVerilogUvmSequenceHandshakeKind : std::uint8_t {
  GetNextItem,
  TryNextItem,
  Get,
  Peek,
  Push,
};

enum class SystemVerilogUvmSequenceTransactionState : std::uint8_t {
  Peeked,
  Reserved,
  Pushed,
  Completed,
  Cancelled,
};

enum class SystemVerilogUvmSequenceAcquireStatus : std::uint8_t {
  Acquired,
  Empty,
  WaitingForRequest,
  WaitingForRelevant,
  Backpressured,
};

enum class SystemVerilogUvmSequenceCancellationReason : std::uint8_t {
  None,
  Explicit,
  Timeout,
  PhaseEnded,
  PhaseJumped,
  ProcessCancelled,
  SequenceStopped,
  SequenceKilled,
  VirtualReset,
};

enum class SystemVerilogUvmVirtualSequenceAccess : std::uint8_t {
  None,
  Lock,
  Grab,
};

enum class SystemVerilogUvmVirtualSequenceEventKind : std::uint8_t {
  CoordinatedStart,
  RequestQueued,
  AccessAcquired,
  ChildStarted,
  ChildCompleted,
  RequestCancelled,
  AccessReleased,
  ResetRequested,
  Restarted,
  Completed,
  Stopped,
  Killed,
};

enum class SystemVerilogUvmSequenceRoleKind : std::uint8_t {
  Driver,
  Monitor,
  Agent,
  Subscriber,
  Scoreboard,
};

enum class SystemVerilogUvmAgentMode : std::uint8_t {
  Active,
  Passive,
};

enum class SystemVerilogUvmSequenceRoleState : std::uint8_t {
  Constructed,
  Built,
  Connected,
  Running,
  Stopped,
};

enum class SystemVerilogUvmSequenceRoleEventKind : std::uint8_t {
  Registered,
  PhaseDispatch,
  AnalysisConnection,
  AnalysisPublication,
  ObjectionRaised,
  ObjectionDropped,
  Released,
  Teardown,
};

struct SystemVerilogUvmSequenceProfile {
  std::string request_type;
  std::string response_type;
  friend bool operator==(const SystemVerilogUvmSequenceProfile &,
                         const SystemVerilogUvmSequenceProfile &) = default;
};

struct SystemVerilogUvmVirtualSequencerDomain {
  std::string name;
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmSequenceProfile profile;
};

struct SystemVerilogUvmVirtualSequenceStep {
  std::string domain;
  SystemVerilogUvmSequenceHandle sequence;
  std::uint32_t priority{100};
  SystemVerilogUvmVirtualSequenceAccess access{
      SystemVerilogUvmVirtualSequenceAccess::None};
  bool call_pre_post{true};
  bool automatic_phase_objection{};
};

struct SystemVerilogUvmVirtualSequenceOptions {
  bool restart_on_reset{true};
  bool stop_on_child_failure{true};
};

struct SystemVerilogUvmVirtualSequenceEvent {
  SystemVerilogUvmSequenceHandle virtual_sequence;
  SystemVerilogUvmSequenceHandle child_sequence;
  SystemVerilogUvmSequencerHandle child_sequencer;
  SystemVerilogUvmSequenceRequestHandle request;
  SystemVerilogUvmSequenceAccessHandle access;
  std::string domain;
  SystemVerilogUvmVirtualSequenceEventKind kind{
      SystemVerilogUvmVirtualSequenceEventKind::CoordinatedStart};
  std::uint64_t epoch{};
  std::uint64_t order{};
};

struct SystemVerilogUvmSequenceHooks {
  using Hook = std::function<void(SystemVerilogUvmSequenceHandle)>;
  Hook pre_start;
  Hook pre_body;
  Hook body;
  Hook post_body;
  Hook post_start;
};

struct SystemVerilogUvmSequenceStartOptions {
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseProcessHandle parent_process;
  bool call_pre_post{true};
  bool automatic_phase_objection{};
};

struct SystemVerilogUvmSequenceRequestDescriptor {
  using Relevance = std::function<bool(SystemVerilogUvmSequenceHandle)>;
  SystemVerilogUvmSequenceRequestDescriptor() = default;
  SystemVerilogUvmSequenceRequestDescriptor(
      const SystemVerilogUvmSequenceHandle sequence_value,
      const std::uint32_t priority_value, const bool relevant_value,
      Relevance relevance_value,
      const SystemVerilogUvmSequenceItemHandle item_value = {})
      : sequence(sequence_value), priority(priority_value),
        relevant(relevant_value), is_relevant(std::move(relevance_value)),
        item(item_value) {}
  SystemVerilogUvmSequenceHandle sequence;
  std::uint32_t priority{100};
  bool relevant{true};
  Relevance is_relevant;
  SystemVerilogUvmSequenceItemHandle item;
};

struct SystemVerilogUvmSequenceRequestSnapshot {
  SystemVerilogUvmSequenceRequestHandle handle;
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequencerHandle sequencer;
  std::uint32_t priority{100};
  bool relevant{true};
  std::uint64_t queue_order{};
  SystemVerilogUvmSequenceItemHandle item;
};

struct SystemVerilogUvmSequenceSelectionResult {
  SystemVerilogUvmSequenceSelectionStatus status{
      SystemVerilogUvmSequenceSelectionStatus::Empty};
  std::optional<SystemVerilogUvmSequenceRequestSnapshot> request;
};

struct SystemVerilogUvmResponseRouteResult {
  SystemVerilogUvmResponseRouteStatus status{
      SystemVerilogUvmResponseRouteStatus::Queued};
  std::optional<SystemVerilogUvmSequenceItemHandle> dropped;
};

struct SystemVerilogUvmSequenceAccessSnapshot {
  SystemVerilogUvmSequenceAccessHandle handle;
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmSequenceAccessKind kind{
      SystemVerilogUvmSequenceAccessKind::Lock};
  SystemVerilogUvmSequenceAccessState state{
      SystemVerilogUvmSequenceAccessState::Pending};
  std::uint64_t queue_order{};
};

struct SystemVerilogUvmSequenceHandshakeContext {
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseProcessHandle process;
  SimulationTick timeout_ticks{};
};

struct SystemVerilogUvmSequenceTransactionSnapshot {
  SystemVerilogUvmSequenceTransactionHandle handle;
  SystemVerilogUvmSequenceRequestSnapshot request;
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogClassHandle request_object{};
  SystemVerilogUvmSequenceItemHandle request_item;
  SystemVerilogUvmSequenceItemHandle response_item;
  SystemVerilogUvmSequenceHandshakeKind kind{
      SystemVerilogUvmSequenceHandshakeKind::GetNextItem};
  SystemVerilogUvmSequenceTransactionState state{
      SystemVerilogUvmSequenceTransactionState::Reserved};
  SystemVerilogUvmSequenceCancellationReason cancellation{
      SystemVerilogUvmSequenceCancellationReason::None};
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseProcessHandle process;
  SimulationTick started_at{};
  std::optional<SimulationTick> deadline;
  std::uint64_t completion_order{};
  std::size_t peek_count{};
};

struct SystemVerilogUvmSequenceAcquireResult {
  SystemVerilogUvmSequenceAcquireStatus status{
      SystemVerilogUvmSequenceAcquireStatus::Empty};
  std::optional<SystemVerilogUvmSequenceTransactionSnapshot> transaction;
};

struct SystemVerilogUvmSequenceRoleSnapshot {
  SystemVerilogUvmSequenceRoleHandle handle;
  SystemVerilogClassHandle component{};
  SystemVerilogUvmRootHandle root{};
  std::string full_name;
  SystemVerilogUvmSequenceRoleKind kind{
      SystemVerilogUvmSequenceRoleKind::Agent};
  SystemVerilogUvmAgentMode agent_mode{SystemVerilogUvmAgentMode::Active};
  SystemVerilogUvmSequenceRoleState state{
      SystemVerilogUvmSequenceRoleState::Constructed};
  SystemVerilogUvmSequenceRoleHandle agent;
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmTlm1EndpointHandle analysis_endpoint;
  std::string analysis_type;
  std::uint64_t registration_order{};
  std::uint64_t function_dispatches{};
  std::uint64_t task_dispatches{};
  std::uint64_t publications{};
  std::optional<SystemVerilogUvmPhaseHandle> phase;
  std::optional<SystemVerilogUvmPhaseProcessHandle> process;
  bool automatic_objection{};
  bool objection_raised{};
};

struct SystemVerilogUvmSequenceRoleEvent {
  std::uint64_t order{};
  SystemVerilogUvmSequenceRoleHandle role;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogUvmSequenceRoleEventKind kind{
      SystemVerilogUvmSequenceRoleEventKind::Registered};
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseProcessHandle process;
  SystemVerilogUvmTlm1EndpointHandle endpoint;
};

struct SystemVerilogUvmSequenceRoleFailure {
  std::string diagnostic_code;
  SystemVerilogUvmSequenceRoleHandle role;
  SystemVerilogUvmPhaseHandle phase;
  std::string message;
};

struct SystemVerilogUvmSequenceRoleDescriptor {
  using FunctionDispatch = std::function<void(
      SystemVerilogUvmSequenceRoleHandle, SystemVerilogUvmPhaseHandle,
      SystemVerilogUvmPhaseCallbackKind)>;
  using TaskDispatch = std::function<SystemVerilogUvmTaskPhaseStatus(
      SystemVerilogUvmSequenceRoleHandle, SystemVerilogUvmPhaseHandle,
      SystemVerilogUvmPhaseProcessHandle)>;

  SystemVerilogClassHandle component{};
  SystemVerilogUvmSequenceRoleKind kind{
      SystemVerilogUvmSequenceRoleKind::Agent};
  SystemVerilogUvmSequenceRoleHandle agent;
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmAgentMode default_agent_mode{
      SystemVerilogUvmAgentMode::Active};
  std::string active_config_field{"is_active"};
  std::string analysis_type;
  bool automatic_objection{};
  FunctionDispatch function_dispatch;
  TaskDispatch task_dispatch;
  SystemVerilogUvmTlm1Service::AnalysisSubscriber analysis_dispatch;
};

struct SystemVerilogUvmSequenceExecutionEvent {
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequenceState state{SystemVerilogUvmSequenceState::Created};
  SystemVerilogUvmSequenceCallbackKind callback{
      SystemVerilogUvmSequenceCallbackKind::StateTransition};
  std::uint64_t execution{};
  std::uint64_t order{};
};

struct SystemVerilogUvmSequenceExecutionFailure {
  std::string diagnostic_code;
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequenceCallbackKind callback{
      SystemVerilogUvmSequenceCallbackKind::StateTransition};
  std::string message;
};

struct SystemVerilogUvmSequenceExecutionResult {
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequenceState final_state{
      SystemVerilogUvmSequenceState::Created};
  std::vector<SystemVerilogUvmSequenceExecutionEvent> events;
  std::vector<SystemVerilogUvmSequenceExecutionFailure> failures;
  bool stopped{};
  bool killed{};

  [[nodiscard]] bool success() const noexcept {
    return failures.empty() && !stopped && !killed;
  }
};

struct SystemVerilogUvmVirtualSequenceChildResult {
  std::string domain;
  SystemVerilogUvmSequenceHandle sequence;
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmSequenceRequestHandle request;
  SystemVerilogUvmSequenceAccessHandle access;
  std::uint32_t priority{100};
  std::uint64_t epoch{};
  SystemVerilogUvmSequenceExecutionResult execution;
};

struct SystemVerilogUvmVirtualSequenceResult {
  SystemVerilogUvmSequenceHandle sequence;
  std::vector<SystemVerilogUvmVirtualSequenceChildResult> children;
  std::vector<SystemVerilogUvmVirtualSequenceEvent> events;
  std::uint64_t epochs{};
  std::uint64_t restarts{};
  bool completed{};
  bool stopped{};
  bool killed{};
  bool reset{};

  [[nodiscard]] bool success() const noexcept {
    return completed && !stopped && !killed && !reset;
  }
};

enum class SystemVerilogUvmSequenceMacroStatus : std::uint8_t {
  RandomizationFailed,
  Queued,
  Started,
};

struct SystemVerilogUvmSequenceMacroResult {
  SystemVerilogUvmSequenceMacroStatus status{
      SystemVerilogUvmSequenceMacroStatus::RandomizationFailed};
  SystemVerilogClassRandomizeResult randomization;
  std::optional<SystemVerilogUvmSequenceRequestHandle> request;
  std::optional<SystemVerilogUvmSequenceExecutionResult> execution;

  [[nodiscard]] bool success() const noexcept {
    return status != SystemVerilogUvmSequenceMacroStatus::RandomizationFailed;
  }
};

struct SystemVerilogUvmSequencerDescriptor {
  SystemVerilogClassHandle component{};
  std::string nominal_type;
  SystemVerilogUvmSequenceProfile profile;
};

struct SystemVerilogUvmVirtualSequencerDescriptor {
  SystemVerilogClassHandle component{};
  std::string nominal_type;
  SystemVerilogUvmSequenceProfile profile;
  std::vector<SystemVerilogUvmVirtualSequencerDomain> domains;
};

struct SystemVerilogUvmSequenceDescriptor {
  SystemVerilogClassHandle object{};
  std::string name;
  std::string nominal_type;
  SystemVerilogUvmSequenceProfile profile;
  SystemVerilogUvmSequenceHandle parent;
  SystemVerilogUvmSequencerHandle sequencer;
  SystemVerilogUvmSequenceHooks hooks;
};

struct SystemVerilogUvmSequenceItemDescriptor {
  SystemVerilogClassHandle object{};
  std::string name;
  std::string nominal_type;
  SystemVerilogUvmSequenceItemRole role{
      SystemVerilogUvmSequenceItemRole::Request};
  SystemVerilogUvmSequenceHandle owner_sequence;
  SystemVerilogUvmSequencerHandle sequencer;
};

struct SystemVerilogUvmSequencerSnapshot {
  SystemVerilogUvmSequencerHandle handle;
  SystemVerilogClassHandle component{};
  SystemVerilogUvmRootHandle root{};
  std::string nominal_type;
  SystemVerilogUvmSequenceProfile profile;
  std::string full_name;
  std::string debug_name;
  std::uint64_t declaration_order{};
  SystemVerilogUvmSequencerState state{
      SystemVerilogUvmSequencerState::Registered};
  SystemVerilogUvmSequenceArbitrationMode arbitration{
      SystemVerilogUvmSequenceArbitrationMode::Fifo};
  std::uint64_t random_seed{};
  std::uint64_t random_draws{};
  std::uint64_t relevance_waits{};
  std::vector<SystemVerilogUvmSequenceRequestHandle> requests;
  std::vector<SystemVerilogUvmSequenceAccessHandle> access_queue;
  std::vector<SystemVerilogUvmSequenceAccessHandle> access_stack;
  std::size_t consecutive_grabs{};
  std::size_t maximum_in_flight{1};
  std::size_t push_capacity{1};
  std::vector<SystemVerilogUvmSequenceTransactionHandle> transactions;
  std::vector<SystemVerilogUvmSequenceTransactionHandle> active_transactions;
  std::vector<SystemVerilogUvmSequenceTransactionHandle> push_queue;
  bool is_virtual{};
  SystemVerilogUvmSequencerHandle parent_virtual;
  std::vector<SystemVerilogUvmVirtualSequencerDomain> virtual_domains;
};

struct SystemVerilogUvmSequenceSnapshot {
  SystemVerilogUvmSequenceHandle handle;
  SystemVerilogClassHandle object{};
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::string debug_name;
  std::string nominal_type;
  SystemVerilogUvmSequenceProfile profile;
  SystemVerilogUvmSequenceHandle parent;
  SystemVerilogUvmSequencerHandle sequencer;
  std::vector<SystemVerilogUvmSequenceHandle> children;
  std::vector<SystemVerilogUvmSequenceItemHandle> items;
  std::vector<SystemVerilogUvmSequenceItemHandle> responses;
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmPhaseProcessHandle process;
  std::size_t depth{};
  std::uint64_t declaration_order{};
  std::uint64_t execution_count{};
  std::size_t response_queue_depth{65'536};
  SystemVerilogUvmResponseOverflowPolicy response_overflow_policy{
      SystemVerilogUvmResponseOverflowPolicy::Error};
  bool response_overflow_error{true};
  bool is_virtual{};
  std::uint64_t virtual_run_count{};
  std::uint64_t virtual_restart_count{};
  SystemVerilogUvmSequenceState state{SystemVerilogUvmSequenceState::Created};
};

struct SystemVerilogUvmSequenceItemSnapshot {
  SystemVerilogUvmSequenceItemHandle handle;
  SystemVerilogClassHandle object{};
  SystemVerilogUvmRootHandle root{};
  std::string name;
  std::string full_name;
  std::string debug_name;
  std::string nominal_type;
  SystemVerilogUvmSequenceItemRole role{
      SystemVerilogUvmSequenceItemRole::Request};
  SystemVerilogUvmSequenceHandle owner_sequence;
  SystemVerilogUvmSequencerHandle sequencer;
  std::uint64_t declaration_order{};
  SystemVerilogUvmSequenceItemState state{
      SystemVerilogUvmSequenceItemState::Created};
};

struct SystemVerilogUvmSequenceLimits {
  std::size_t maximum_items{262'144};
  std::size_t maximum_sequences{65'536};
  std::size_t maximum_sequencers{65'536};
  std::size_t maximum_registrations{1U << 20U};
  std::size_t maximum_mutations{1U << 22U};
  std::size_t maximum_name_bytes{1'024};
  std::size_t maximum_full_name_bytes{16'384};
  std::size_t maximum_nominal_type_bytes{4'096};
  std::size_t maximum_profile_bytes{8'192};
  std::size_t maximum_depth{256};
  std::size_t maximum_children_per_sequence{65'536};
  std::size_t maximum_items_per_sequence{262'144};
  std::size_t maximum_active_executions{65'536};
  std::size_t maximum_execution_events{1U << 22U};
  std::size_t maximum_execution_failures{65'536};
  std::size_t maximum_responses_per_sequence{65'536};
  std::size_t maximum_pending_requests{262'144};
  std::size_t maximum_requests_per_sequencer{65'536};
  std::size_t maximum_arbitration_work{262'144};
  std::size_t maximum_relevance_waits{65'536};
  std::uint32_t maximum_priority{0x7fff'ffffU};
  std::size_t maximum_access_requests{65'536};
  std::size_t maximum_access_depth{256};
  std::size_t maximum_consecutive_grabs{256};
  std::size_t maximum_transactions{262'144};
  std::size_t maximum_transactions_per_sequencer{65'536};
  std::size_t maximum_in_flight_per_sequencer{65'536};
  std::size_t maximum_push_capacity{65'536};
  SimulationTick maximum_transaction_timeout{1ULL << 48U};
  std::size_t maximum_roles{65'536};
  std::size_t maximum_roles_per_root{65'536};
  std::size_t maximum_role_dispatches{1U << 22U};
  std::size_t maximum_role_failures{65'536};
  std::size_t maximum_role_events{1U << 22U};
  std::size_t maximum_analysis_connections_per_monitor{65'536};
  std::size_t maximum_role_publications{1U << 22U};
  std::size_t maximum_virtual_domains_per_sequencer{256};
  std::size_t maximum_virtual_steps{65'536};
  std::size_t maximum_virtual_restarts{256};
  std::size_t maximum_virtual_events{1U << 22U};
};

class SystemVerilogUvmSequenceError final : public std::runtime_error {
public:
  SystemVerilogUvmSequenceError(std::string code, std::string message);
  [[nodiscard]] std::string_view diagnostic_code() const noexcept {
    return code_;
  }

private:
  std::string code_;
};

/// Simulation-owned UVM sequence metadata. Registration is source ordered and
/// transactional; typed handles retain explicit parent, item, sequencer, and
/// root ownership while lifecycle callbacks remain dormant until execution.
class SystemVerilogUvmSequenceService final {
public:
  SystemVerilogUvmSequenceService(SystemVerilogClassHeap &heap,
                                  SystemVerilogUvmObjectService &objects,
                                  SystemVerilogUvmComponentService &components,
                                  SystemVerilogUvmSequenceLimits limits = {});
  SystemVerilogUvmSequenceService(SystemVerilogClassHeap &heap,
                                  SystemVerilogUvmObjectService &objects,
                                  SystemVerilogUvmComponentService &components,
                                  SystemVerilogUvmPhaseService &phases,
                                  SystemVerilogUvmObjectionService &objections,
                                  SystemVerilogUvmSequenceLimits limits = {});
  ~SystemVerilogUvmSequenceService() noexcept;

  void set_activity_service(SystemVerilogUvmActivityService &activity) noexcept {
    activity_ = &activity;
  }

  [[nodiscard]] SystemVerilogUvmSequencerHandle
  register_sequencer(SystemVerilogUvmSequencerDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmSequenceHandle
  register_sequence(SystemVerilogUvmSequenceDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmSequenceItemHandle
  register_item(SystemVerilogUvmSequenceItemDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmSequencerHandle register_virtual_sequencer(
      SystemVerilogUvmVirtualSequencerDescriptor descriptor);
  [[nodiscard]] SystemVerilogUvmSequenceHandle
  register_virtual_sequence(SystemVerilogUvmSequenceDescriptor descriptor);

  [[nodiscard]] bool
  contains(SystemVerilogUvmSequencerHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmSequenceHandle handle) const noexcept;
  [[nodiscard]] bool
  contains(SystemVerilogUvmSequenceItemHandle handle) const noexcept;
  [[nodiscard]] SystemVerilogUvmSequencerSnapshot
  snapshot(SystemVerilogUvmSequencerHandle handle) const;
  [[nodiscard]] SystemVerilogUvmSequenceSnapshot
  snapshot(SystemVerilogUvmSequenceHandle handle) const;
  [[nodiscard]] SystemVerilogUvmSequenceItemSnapshot
  snapshot(SystemVerilogUvmSequenceItemHandle handle) const;
  [[nodiscard]] std::vector<SystemVerilogUvmSequencerHandle> sequencers() const;
  [[nodiscard]] std::vector<SystemVerilogUvmSequenceHandle> sequences() const;
  [[nodiscard]] std::vector<SystemVerilogUvmSequenceItemHandle> items() const;

  [[nodiscard]] SystemVerilogUvmSequenceExecutionResult
  start(SystemVerilogUvmSequenceHandle sequence,
        SystemVerilogUvmSequenceStartOptions options = {});
  void request_stop(SystemVerilogUvmSequenceHandle sequence);
  void kill(SystemVerilogUvmSequenceHandle sequence);
  [[nodiscard]] SystemVerilogUvmVirtualSequenceResult
  coordinate_virtual(SystemVerilogUvmSequenceHandle sequence,
                     std::span<const SystemVerilogUvmVirtualSequenceStep> steps,
                     SystemVerilogUvmVirtualSequenceOptions options = {});
  void request_virtual_reset(SystemVerilogUvmSequenceHandle sequence,
                             bool restart = true);
  [[nodiscard]] std::span<const SystemVerilogUvmVirtualSequenceEvent>
  virtual_events() const noexcept {
    return virtual_events_;
  }
  void configure_response_queue(SystemVerilogUvmSequenceHandle sequence,
                                std::size_t depth,
                                SystemVerilogUvmResponseOverflowPolicy policy,
                                bool error_on_overflow = true);
  SystemVerilogUvmResponseRouteResult
  route_response(SystemVerilogUvmSequenceItemHandle response);
  [[nodiscard]] std::optional<SystemVerilogUvmSequenceItemHandle>
  pop_response(SystemVerilogUvmSequenceHandle sequence);
  [[nodiscard]] std::vector<SystemVerilogUvmSequenceItemHandle>
  responses(SystemVerilogUvmSequenceHandle sequence) const;
  using UserArbitration = std::function<SystemVerilogUvmSequenceRequestHandle(
      std::span<const SystemVerilogUvmSequenceRequestSnapshot>)>;
  void configure_arbitration(SystemVerilogUvmSequencerHandle sequencer,
                             SystemVerilogUvmSequenceArbitrationMode mode,
                             std::uint64_t seed = 1);
  void set_user_arbitration(SystemVerilogUvmSequencerHandle sequencer,
                            UserArbitration callback);
  void reseed_arbitration(SystemVerilogUvmSequencerHandle sequencer,
                          std::uint64_t seed);
  [[nodiscard]] SystemVerilogUvmSequenceRequestHandle
  enqueue_request(SystemVerilogUvmSequenceRequestDescriptor descriptor);
  void set_request_relevant(SystemVerilogUvmSequenceRequestHandle request,
                            bool relevant);
  void cancel_request(SystemVerilogUvmSequenceRequestHandle request);
  [[nodiscard]] SystemVerilogUvmSequenceSelectionResult
  select_request(SystemVerilogUvmSequencerHandle sequencer);
  [[nodiscard]] SystemVerilogUvmSequenceRequestSnapshot
  request_snapshot(SystemVerilogUvmSequenceRequestHandle request) const;
  [[nodiscard]] std::vector<SystemVerilogUvmSequenceRequestHandle>
  requests(SystemVerilogUvmSequencerHandle sequencer) const;
  [[nodiscard]] SystemVerilogUvmSequenceAccessHandle
  request_lock(SystemVerilogUvmSequenceHandle sequence);
  [[nodiscard]] SystemVerilogUvmSequenceAccessHandle
  request_grab(SystemVerilogUvmSequenceHandle sequence);
  void unlock(SystemVerilogUvmSequenceHandle sequence);
  void ungrab(SystemVerilogUvmSequenceHandle sequence);
  void cancel_access(SystemVerilogUvmSequenceAccessHandle access);
  [[nodiscard]] bool has_lock(SystemVerilogUvmSequenceHandle sequence) const;
  [[nodiscard]] SystemVerilogUvmSequenceAccessSnapshot
  access_snapshot(SystemVerilogUvmSequenceAccessHandle access) const;
  [[nodiscard]] std::vector<SystemVerilogUvmSequenceAccessHandle>
  access_requests(SystemVerilogUvmSequencerHandle sequencer) const;
  void configure_handshake(SystemVerilogUvmSequencerHandle sequencer,
                           std::size_t maximum_in_flight,
                           std::size_t push_capacity);
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  get_next_item(SystemVerilogUvmSequencerHandle sequencer,
                SystemVerilogUvmSequenceHandshakeContext context = {});
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  try_next_item(SystemVerilogUvmSequencerHandle sequencer,
                SystemVerilogUvmSequenceHandshakeContext context = {});
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  get(SystemVerilogUvmSequencerHandle sequencer,
      SystemVerilogUvmSequenceHandshakeContext context = {});
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  peek(SystemVerilogUvmSequencerHandle sequencer,
       SystemVerilogUvmSequenceHandshakeContext context = {});
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  push_next_item(SystemVerilogUvmSequencerHandle sequencer,
                 SystemVerilogUvmSequenceHandshakeContext context = {});
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  try_pop_pushed_item(SystemVerilogUvmSequencerHandle sequencer);
  void item_done(SystemVerilogUvmSequenceTransactionHandle transaction,
                 SystemVerilogUvmSequenceItemHandle response = {});
  void put_response(SystemVerilogUvmSequenceTransactionHandle transaction,
                    SystemVerilogUvmSequenceItemHandle response);
  void
  cancel_transaction(SystemVerilogUvmSequenceTransactionHandle transaction,
                     SystemVerilogUvmSequenceCancellationReason reason =
                         SystemVerilogUvmSequenceCancellationReason::Explicit);
  void cancel_phase_transactions(
      SystemVerilogUvmPhaseHandle phase,
      SystemVerilogUvmSequenceCancellationReason reason =
          SystemVerilogUvmSequenceCancellationReason::PhaseJumped);
  [[nodiscard]] SystemVerilogUvmPhaseJumpResult
  jump_phase(SystemVerilogUvmPhaseHandle from,
             SystemVerilogUvmPhaseHandle target);
  void synchronize_phase_transactions();
  void advance_handshake_time(SimulationTick time);
  [[nodiscard]] SimulationTick handshake_time() const noexcept {
    return handshake_time_;
  }
  [[nodiscard]] SystemVerilogUvmSequenceTransactionSnapshot
  transaction_snapshot(
      SystemVerilogUvmSequenceTransactionHandle transaction) const;
  [[nodiscard]] std::vector<SystemVerilogUvmSequenceTransactionHandle>
  transactions(SystemVerilogUvmSequencerHandle sequencer) const;
  void
  release_transaction(SystemVerilogUvmSequenceTransactionHandle transaction);
  void
  set_role_services(SystemVerilogUvmTlm1Service &tlm1,
                    SystemVerilogUvmConfigDbService &configuration) noexcept;
  [[nodiscard]] SystemVerilogUvmSequenceRoleHandle
  register_role(SystemVerilogUvmSequenceRoleDescriptor descriptor);
  void connect_analysis(SystemVerilogUvmSequenceRoleHandle monitor,
                        SystemVerilogUvmSequenceRoleHandle subscriber);
  [[nodiscard]] std::optional<SystemVerilogUvmSequenceRoleSnapshot>
  role_for_component(SystemVerilogClassHandle component) const;
  [[nodiscard]] SystemVerilogUvmSequenceRoleSnapshot
  role_snapshot(SystemVerilogUvmSequenceRoleHandle role) const;
  [[nodiscard]] std::vector<SystemVerilogUvmSequenceRoleHandle>
  roles(std::optional<SystemVerilogUvmRootHandle> root = std::nullopt) const;
  void dispatch_role_function(SystemVerilogClassHandle component,
                              SystemVerilogUvmPhaseHandle phase,
                              SystemVerilogUvmPhaseCallbackKind callback);
  [[nodiscard]] std::optional<SystemVerilogUvmTaskPhaseStatus>
  dispatch_role_task(SystemVerilogClassHandle component,
                     SystemVerilogUvmPhaseHandle phase,
                     SystemVerilogUvmPhaseProcessHandle process);
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  driver_get_next_item(SystemVerilogUvmSequenceRoleHandle driver,
                       SystemVerilogUvmSequenceHandshakeContext context = {});
  void driver_item_done(SystemVerilogUvmSequenceRoleHandle driver,
                        SystemVerilogUvmSequenceTransactionHandle transaction,
                        SystemVerilogUvmSequenceItemHandle response = {});
  [[nodiscard]] SystemVerilogUvmTlm1AnalysisResult
  publish_monitor(SystemVerilogUvmSequenceRoleHandle monitor,
                  SystemVerilogUvmTlm1Payload payload);
  void complete_role_task(SystemVerilogUvmSequenceRoleHandle role,
                          SystemVerilogUvmPhaseProcessHandle process);
  void release_role(SystemVerilogUvmSequenceRoleHandle role);
  void teardown_roles(SystemVerilogUvmRootHandle root);
  [[nodiscard]] std::span<const SystemVerilogUvmSequenceRoleEvent>
  role_events() const noexcept {
    return role_events_;
  }
  [[nodiscard]] std::span<const SystemVerilogUvmSequenceRoleFailure>
  role_failures() const noexcept {
    return role_failures_;
  }
  [[nodiscard]] SystemVerilogUvmSequenceHandle
  macro_create(SystemVerilogUvmSequenceDescriptor prototype);
  [[nodiscard]] SystemVerilogUvmSequenceItemHandle
  macro_create(SystemVerilogUvmSequenceItemDescriptor prototype);
  [[nodiscard]] SystemVerilogUvmSequenceRequestHandle
  macro_send(SystemVerilogUvmSequenceHandle sequence,
             std::uint32_t priority = 100);
  [[nodiscard]] SystemVerilogUvmSequenceRequestHandle
  macro_send(SystemVerilogUvmSequenceItemHandle item,
             std::uint32_t priority = 100);
  [[nodiscard]] SystemVerilogUvmSequenceMacroResult
  macro_random_send(SystemVerilogUvmSequenceHandle sequence,
                    const SystemVerilogClassRandomizeRequest &randomization,
                    std::uint32_t priority = 100);
  [[nodiscard]] SystemVerilogUvmSequenceMacroResult
  macro_random_send(SystemVerilogUvmSequenceItemHandle item,
                    const SystemVerilogClassRandomizeRequest &randomization,
                    std::uint32_t priority = 100);
  [[nodiscard]] SystemVerilogUvmSequenceMacroResult
  macro_random_start(SystemVerilogUvmSequenceHandle sequence,
                     const SystemVerilogClassRandomizeRequest &randomization,
                     SystemVerilogUvmSequenceStartOptions options = {});
  [[nodiscard]] std::span<const SystemVerilogUvmSequenceExecutionEvent>
  execution_events() const noexcept {
    return execution_events_;
  }
  [[nodiscard]] std::span<const SystemVerilogUvmSequenceExecutionFailure>
  execution_failures() const noexcept {
    return execution_failures_;
  }
  void clear_execution_trace() noexcept {
    execution_events_.clear();
    execution_failures_.clear();
  }

  void release(SystemVerilogUvmSequenceItemHandle handle);
  void release(SystemVerilogUvmSequenceHandle handle);
  void release(SystemVerilogUvmSequencerHandle handle);

  [[nodiscard]] std::size_t item_count() const noexcept { return live_items_; }
  [[nodiscard]] std::size_t sequence_count() const noexcept {
    return live_sequences_;
  }
  [[nodiscard]] std::size_t sequencer_count() const noexcept {
    return live_sequencers_;
  }
  [[nodiscard]] std::size_t role_count() const noexcept { return live_roles_; }
  [[nodiscard]] std::size_t mutation_count() const noexcept {
    return mutations_;
  }
  [[nodiscard]] const SystemVerilogUvmSequenceLimits &limits() const noexcept {
    return limits_;
  }

private:
  struct Sequencer {
    SystemVerilogUvmSequencerSnapshot value;
    std::uint64_t generation{1};
    bool live{true};
    std::uint64_t random_state{1};
    UserArbitration user_arbitration;
  };
  struct Sequence {
    SystemVerilogUvmSequenceSnapshot value;
    SystemVerilogUvmSequenceHooks hooks;
    std::uint64_t generation{1};
    bool live{true};
    bool active{};
    bool stop_requested{};
    bool kill_requested{};
    std::size_t reserved_events{};
    std::vector<SystemVerilogUvmSequenceHandle> active_virtual_children;
    bool virtual_reset_requested{};
    bool virtual_restart_requested{};
    std::size_t reserved_virtual_events{};
  };
  struct Item {
    SystemVerilogUvmSequenceItemSnapshot value;
    std::uint64_t generation{1};
    bool live{true};
  };
  struct Request {
    SystemVerilogUvmSequenceRequestSnapshot value;
    SystemVerilogUvmSequenceRequestDescriptor::Relevance is_relevant;
    std::uint64_t generation{1};
    bool live{true};
  };
  struct Access {
    SystemVerilogUvmSequenceAccessSnapshot value;
    std::uint64_t generation{1};
    bool live{true};
  };
  struct Transaction {
    SystemVerilogUvmSequenceTransactionSnapshot value;
    std::uint64_t generation{1};
    bool live{true};
  };
  struct Role {
    SystemVerilogUvmSequenceRoleSnapshot value;
    SystemVerilogUvmSequenceRoleDescriptor::FunctionDispatch function_dispatch;
    SystemVerilogUvmSequenceRoleDescriptor::TaskDispatch task_dispatch;
    SystemVerilogUvmObjectionSourceHandle objection_source;
    std::string active_config_field;
    std::vector<SystemVerilogUvmSequenceRoleHandle> analysis_targets;
    std::uint64_t generation{1};
    bool live{true};
  };

  [[nodiscard]] Sequencer &sequencer(SystemVerilogUvmSequencerHandle handle);
  [[nodiscard]] const Sequencer &
  sequencer(SystemVerilogUvmSequencerHandle handle) const;
  [[nodiscard]] Sequence &sequence(SystemVerilogUvmSequenceHandle handle);
  [[nodiscard]] const Sequence &
  sequence(SystemVerilogUvmSequenceHandle handle) const;
  [[nodiscard]] Item &item(SystemVerilogUvmSequenceItemHandle handle);
  [[nodiscard]] const Item &
  item(SystemVerilogUvmSequenceItemHandle handle) const;
  [[nodiscard]] Request &request(SystemVerilogUvmSequenceRequestHandle handle);
  [[nodiscard]] const Request &
  request(SystemVerilogUvmSequenceRequestHandle handle) const;
  [[nodiscard]] Access &access(SystemVerilogUvmSequenceAccessHandle handle);
  [[nodiscard]] const Access &
  access(SystemVerilogUvmSequenceAccessHandle handle) const;
  [[nodiscard]] Transaction &
  transaction(SystemVerilogUvmSequenceTransactionHandle handle);
  [[nodiscard]] const Transaction &
  transaction(SystemVerilogUvmSequenceTransactionHandle handle) const;
  [[nodiscard]] Role &role(SystemVerilogUvmSequenceRoleHandle handle);
  [[nodiscard]] const Role &
  role(SystemVerilogUvmSequenceRoleHandle handle) const;
  [[nodiscard]] SystemVerilogUvmSequenceRoleHandle
  role_handle(std::uint64_t slot) const;
  [[nodiscard]] Role *
  role_for_component_impl(SystemVerilogClassHandle component) noexcept;
  [[nodiscard]] const Role *
  role_for_component_impl(SystemVerilogClassHandle component) const noexcept;
  void validate_role_hierarchy(
      const SystemVerilogUvmSequenceRoleDescriptor &descriptor) const;
  void resolve_agent_mode(Role &role);
  void append_role_event(const Role &role,
                         SystemVerilogUvmSequenceRoleEventKind kind,
                         SystemVerilogUvmPhaseHandle phase = {},
                         SystemVerilogUvmPhaseProcessHandle process = {},
                         SystemVerilogUvmTlm1EndpointHandle endpoint = {});
  void append_role_failure(const Role &role, SystemVerilogUvmPhaseHandle phase,
                           std::string message);
  void drop_role_objection(Role &role);
  void release_role_impl(Role &role, bool teardown);
  [[nodiscard]] bool
  access_related(SystemVerilogUvmSequenceHandle owner,
                 SystemVerilogUvmSequenceHandle candidate) const;
  void release_access(Sequencer &sequencer,
                      SystemVerilogUvmSequenceHandle sequence,
                      SystemVerilogUvmSequenceAccessKind kind);
  void grant_next_access(Sequencer &sequencer);
  void cancel_sequence_accesses(Sequence &sequence);
  [[nodiscard]] SystemVerilogUvmSequenceAcquireResult
  acquire_transaction(SystemVerilogUvmSequencerHandle sequencer,
                      SystemVerilogUvmSequenceHandshakeKind kind,
                      SystemVerilogUvmSequenceHandshakeContext context);
  void validate_handshake_context(
      const Sequencer &sequencer,
      const SystemVerilogUvmSequenceHandshakeContext &context) const;
  void complete_transaction(Transaction &transaction,
                            SystemVerilogUvmSequenceItemHandle response);
  void
  cancel_transaction_impl(Transaction &transaction,
                          SystemVerilogUvmSequenceCancellationReason reason);
  void cancel_sequence_transactions(
      Sequence &sequence, SystemVerilogUvmSequenceCancellationReason reason);
  [[nodiscard]] std::size_t
  active_transaction_count(const Sequencer &sequencer) const noexcept;
  void rollback_latest_request(
      SystemVerilogUvmSequenceRequestHandle request) noexcept;
  [[nodiscard]] SystemVerilogUvmSequenceAccessHandle
  request_access(SystemVerilogUvmSequenceHandle sequence,
                 SystemVerilogUvmSequenceAccessKind kind);
  [[nodiscard]] SystemVerilogUvmSequencerHandle
  sequencer_handle(std::uint64_t slot) const;
  [[nodiscard]] SystemVerilogUvmSequenceHandle
  sequence_handle(std::uint64_t slot) const;
  [[nodiscard]] SystemVerilogUvmSequenceItemHandle
  item_handle(std::uint64_t slot) const;
  void validate_registration_capacity(std::size_t live, std::size_t maximum);
  void validate_name(std::string_view name) const;
  void validate_nominal_type(std::string_view type) const;
  void validate_profile(const SystemVerilogUvmSequenceProfile &profile) const;
  void require_object_type(SystemVerilogClassHandle object,
                           std::string_view nominal_type) const;
  void require_live(const Sequencer &value) const;
  void require_live(const Sequence &value) const;
  void require_live(const Item &value) const;
  void set_execution_state(Sequence &value, SystemVerilogUvmSequenceState state,
                           SystemVerilogUvmSequenceCallbackKind callback,
                           SystemVerilogUvmSequenceExecutionResult &result);
  [[nodiscard]] bool
  invoke_sequence_hook(Sequence &value, SystemVerilogUvmSequenceState state,
                       SystemVerilogUvmSequenceCallbackKind callback,
                       const SystemVerilogUvmSequenceHooks::Hook &hook,
                       SystemVerilogUvmSequenceExecutionResult &result);
  void record_execution_failure(Sequence &value,
                                SystemVerilogUvmSequenceCallbackKind callback,
                                std::string message,
                                SystemVerilogUvmSequenceExecutionResult &result,
                                bool stop = true);
  void request_stop_tree(Sequence &value, bool killed);
  void append_virtual_event(
      Sequence &sequence, SystemVerilogUvmVirtualSequenceResult &result,
      SystemVerilogUvmVirtualSequenceEventKind kind, std::uint64_t epoch,
      const SystemVerilogUvmVirtualSequenceStep *step = nullptr,
      SystemVerilogUvmSequenceRequestHandle request = {},
      SystemVerilogUvmSequenceAccessHandle access = {});
  void cancel_virtual_requests(SystemVerilogUvmSequenceHandle sequence);
  void restore_virtual_responses(
      const std::vector<
          std::pair<SystemVerilogUvmSequenceHandle,
                    std::vector<SystemVerilogUvmSequenceItemHandle>>>
          &baseline);
  void publish_activity(SystemVerilogUvmActivityAction action,
                        std::string identity,
                        SystemVerilogUvmRootHandle root,
                        std::uint64_t value,
                        std::string detail = {}) noexcept;

  SystemVerilogClassHeap *heap_{};
  SystemVerilogUvmObjectService *objects_{};
  SystemVerilogUvmComponentService *components_{};
  SystemVerilogUvmPhaseService *phases_{};
  SystemVerilogUvmObjectionService *objections_{};
  SystemVerilogUvmTlm1Service *tlm1_{};
  SystemVerilogUvmConfigDbService *configuration_{};
  SystemVerilogUvmActivityService *activity_{};
  SystemVerilogUvmSequenceLimits limits_;
  std::shared_ptr<const void> owner_;
  std::map<std::uint64_t, Sequencer> sequencers_;
  std::map<std::uint64_t, Sequence> sequences_;
  std::map<std::uint64_t, Item> items_;
  std::map<std::uint64_t, Request> requests_;
  std::map<std::uint64_t, Access> accesses_;
  std::map<std::uint64_t, Transaction> transactions_;
  std::uint64_t next_slot_{1};
  std::uint64_t next_declaration_order_{};
  std::size_t registrations_{};
  std::size_t mutations_{};
  std::size_t live_items_{};
  std::size_t live_sequences_{};
  std::size_t live_sequencers_{};
  std::size_t active_executions_{};
  std::size_t reserved_execution_events_{};
  std::vector<SystemVerilogUvmSequenceExecutionEvent> execution_events_;
  std::vector<SystemVerilogUvmSequenceExecutionFailure> execution_failures_;
  std::uint64_t next_execution_order_{};
  std::uint64_t next_request_slot_{1};
  std::uint64_t next_request_order_{};
  std::size_t live_requests_{};
  bool arbitration_callback_active_{};
  std::uint64_t next_access_slot_{1};
  std::uint64_t next_access_order_{};
  std::size_t live_accesses_{};
  std::uint64_t next_transaction_slot_{1};
  std::uint64_t next_transaction_completion_order_{};
  std::size_t live_transactions_{};
  SimulationTick handshake_time_{};
  std::map<std::uint64_t, Role> roles_;
  std::map<SystemVerilogClassHandle, std::uint64_t> roles_by_component_;
  std::uint64_t next_role_slot_{1};
  std::uint64_t next_role_order_{};
  std::uint64_t next_role_event_order_{};
  std::size_t live_roles_{};
  std::size_t role_dispatches_{};
  std::size_t role_publications_{};
  std::vector<SystemVerilogUvmSequenceRoleEvent> role_events_;
  std::vector<SystemVerilogUvmSequenceRoleFailure> role_failures_;
  std::vector<SystemVerilogUvmVirtualSequenceEvent> virtual_events_;
  std::uint64_t next_virtual_event_order_{};
  std::size_t reserved_virtual_events_{};
};

} // namespace fsim::runtime
