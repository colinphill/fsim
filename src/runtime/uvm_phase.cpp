// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_phase.hpp"
#include "fsim/runtime/uvm_objection.hpp"
#include "fsim/runtime/uvm_tlm1.hpp"

#include <algorithm>
#include <exception>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-PHASE-001"};
constexpr std::string_view kInvalidIdentity{"FSIM-UVM-PHASE-002"};
constexpr std::string_view kInvalidGraph{"FSIM-UVM-PHASE-003"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-PHASE-004"};
constexpr std::string_view kInvalidRoot{"FSIM-UVM-PHASE-005"};
constexpr std::string_view kCallbackFailure{"FSIM-UVM-PHASE-006"};
constexpr std::string_view kInvalidControl{"FSIM-UVM-PHASE-007"};
constexpr std::string_view kQuiescenceFailure{"FSIM-UVM-PHASE-008"};

[[noreturn]] void fail(
    const std::string_view code,
    const std::string_view message) {
  throw SystemVerilogUvmPhaseError{
      std::string{code}, std::string{message}};
}

void consume_work(
    std::size_t& work,
    const std::size_t maximum) {
  if (work >= maximum) {
    fail(kResourceLimit, "UVM phase graph traversal budget exceeded");
  }
  ++work;
}

}  // namespace

struct SystemVerilogUvmPhaseService::Domain {
  SystemVerilogUvmDomainHandle handle;
  std::string identity;
  SystemVerilogUvmDomainKind kind{SystemVerilogUvmDomainKind::Custom};
  std::uint64_t registration_order{};
  std::vector<std::uint64_t> phases;
  std::map<std::string, std::uint64_t, std::less<>> phases_by_identity;
  std::vector<SystemVerilogUvmRootHandle> roots;
  std::optional<std::uint64_t> with_phase;
};

struct SystemVerilogUvmPhaseService::Phase {
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
  std::optional<std::uint64_t> parent;
  std::vector<std::uint64_t> predecessors;
  std::vector<std::uint64_t> successors;
  std::vector<std::uint64_t> synchronized;
  std::vector<std::uint64_t> processes;
  std::optional<SystemVerilogUvmPhaseExecutionResult> task_result;
  std::size_t ready_to_end_attempts{};
};

struct SystemVerilogUvmPhaseService::PhaseProcess {
  SystemVerilogUvmPhaseProcessHandle handle;
  SystemVerilogUvmPhaseHandle phase;
  SystemVerilogUvmRootHandle root{};
  SystemVerilogClassHandle component{};
  SystemVerilogUvmPhaseProcessState state{
      SystemVerilogUvmPhaseProcessState::Running};
  std::uint64_t registration_order{};
  std::optional<std::uint64_t> parent;
  std::vector<std::uint64_t> children;
  std::size_t depth{};
  ScheduledTaskHandle task;
};

std::string_view systemverilog_uvm_phase_identity(
    const SystemVerilogUvmPhaseKind kind) noexcept {
  switch (kind) {
    case SystemVerilogUvmPhaseKind::Build: return "build";
    case SystemVerilogUvmPhaseKind::Connect: return "connect";
    case SystemVerilogUvmPhaseKind::EndOfElaboration:
      return "end_of_elaboration";
    case SystemVerilogUvmPhaseKind::StartOfSimulation:
      return "start_of_simulation";
    case SystemVerilogUvmPhaseKind::Run: return "run";
    case SystemVerilogUvmPhaseKind::Extract: return "extract";
    case SystemVerilogUvmPhaseKind::Check: return "check";
    case SystemVerilogUvmPhaseKind::Report: return "report";
    case SystemVerilogUvmPhaseKind::Final: return "final";
    case SystemVerilogUvmPhaseKind::PreReset: return "pre_reset";
    case SystemVerilogUvmPhaseKind::Reset: return "reset";
    case SystemVerilogUvmPhaseKind::PostReset: return "post_reset";
    case SystemVerilogUvmPhaseKind::PreConfigure: return "pre_configure";
    case SystemVerilogUvmPhaseKind::Configure: return "configure";
    case SystemVerilogUvmPhaseKind::PostConfigure:
      return "post_configure";
    case SystemVerilogUvmPhaseKind::PreMain: return "pre_main";
    case SystemVerilogUvmPhaseKind::Main: return "main";
    case SystemVerilogUvmPhaseKind::PostMain: return "post_main";
    case SystemVerilogUvmPhaseKind::PreShutdown: return "pre_shutdown";
    case SystemVerilogUvmPhaseKind::Shutdown: return "shutdown";
    case SystemVerilogUvmPhaseKind::PostShutdown:
      return "post_shutdown";
    case SystemVerilogUvmPhaseKind::Custom: return {};
  }
  return {};
}

SystemVerilogUvmDomainKind systemverilog_uvm_phase_domain_kind(
    const SystemVerilogUvmPhaseKind kind) {
  switch (kind) {
    case SystemVerilogUvmPhaseKind::Build:
    case SystemVerilogUvmPhaseKind::Connect:
    case SystemVerilogUvmPhaseKind::EndOfElaboration:
    case SystemVerilogUvmPhaseKind::StartOfSimulation:
    case SystemVerilogUvmPhaseKind::Run:
    case SystemVerilogUvmPhaseKind::Extract:
    case SystemVerilogUvmPhaseKind::Check:
    case SystemVerilogUvmPhaseKind::Report:
    case SystemVerilogUvmPhaseKind::Final:
      return SystemVerilogUvmDomainKind::Common;
    case SystemVerilogUvmPhaseKind::PreReset:
    case SystemVerilogUvmPhaseKind::Reset:
    case SystemVerilogUvmPhaseKind::PostReset:
    case SystemVerilogUvmPhaseKind::PreConfigure:
    case SystemVerilogUvmPhaseKind::Configure:
    case SystemVerilogUvmPhaseKind::PostConfigure:
    case SystemVerilogUvmPhaseKind::PreMain:
    case SystemVerilogUvmPhaseKind::Main:
    case SystemVerilogUvmPhaseKind::PostMain:
    case SystemVerilogUvmPhaseKind::PreShutdown:
    case SystemVerilogUvmPhaseKind::Shutdown:
    case SystemVerilogUvmPhaseKind::PostShutdown:
      return SystemVerilogUvmDomainKind::Runtime;
    case SystemVerilogUvmPhaseKind::Custom:
      fail(kInvalidIdentity, "custom UVM phase has no standard domain");
  }
  fail(kInvalidIdentity, "invalid UVM phase kind");
}

SystemVerilogUvmPhaseExecutionKind
systemverilog_uvm_phase_execution_kind(
    const SystemVerilogUvmPhaseKind kind) {
  switch (kind) {
    case SystemVerilogUvmPhaseKind::Build:
    case SystemVerilogUvmPhaseKind::Connect:
    case SystemVerilogUvmPhaseKind::EndOfElaboration:
    case SystemVerilogUvmPhaseKind::StartOfSimulation:
    case SystemVerilogUvmPhaseKind::Extract:
    case SystemVerilogUvmPhaseKind::Check:
    case SystemVerilogUvmPhaseKind::Report:
    case SystemVerilogUvmPhaseKind::Final:
      return SystemVerilogUvmPhaseExecutionKind::Function;
    case SystemVerilogUvmPhaseKind::Run:
    case SystemVerilogUvmPhaseKind::PreReset:
    case SystemVerilogUvmPhaseKind::Reset:
    case SystemVerilogUvmPhaseKind::PostReset:
    case SystemVerilogUvmPhaseKind::PreConfigure:
    case SystemVerilogUvmPhaseKind::Configure:
    case SystemVerilogUvmPhaseKind::PostConfigure:
    case SystemVerilogUvmPhaseKind::PreMain:
    case SystemVerilogUvmPhaseKind::Main:
    case SystemVerilogUvmPhaseKind::PostMain:
    case SystemVerilogUvmPhaseKind::PreShutdown:
    case SystemVerilogUvmPhaseKind::Shutdown:
    case SystemVerilogUvmPhaseKind::PostShutdown:
      return SystemVerilogUvmPhaseExecutionKind::Task;
    case SystemVerilogUvmPhaseKind::Custom:
      fail(kInvalidIdentity, "custom UVM phase has no standard execution kind");
  }
  fail(kInvalidIdentity, "invalid UVM phase kind");
}

SystemVerilogUvmPhaseError::SystemVerilogUvmPhaseError(
    std::string code,
    std::string message)
    : std::runtime_error{std::move(message)}, code_{std::move(code)} {}

SystemVerilogUvmPhaseService::SystemVerilogUvmPhaseService(
    SystemVerilogUvmComponentService& components,
    SystemVerilogUvmPhaseLimits limits)
    : SystemVerilogUvmPhaseService(components, nullptr, limits) {}

SystemVerilogUvmPhaseService::SystemVerilogUvmPhaseService(
    SystemVerilogUvmComponentService& components,
    Scheduler& scheduler,
    SystemVerilogUvmPhaseLimits limits)
    : SystemVerilogUvmPhaseService(components, &scheduler, limits) {}

SystemVerilogUvmPhaseService::SystemVerilogUvmPhaseService(
    SystemVerilogUvmComponentService& components,
    Scheduler* scheduler,
    SystemVerilogUvmPhaseLimits limits)
    : components_{&components},
      scheduler_{scheduler},
      limits_{limits},
      owner_{std::make_shared<std::uint8_t>()} {}

SystemVerilogUvmPhaseService::~SystemVerilogUvmPhaseService() {
  if (!scheduler_) return;
  for (const auto& [ignored, process] : processes_) {
    (void)ignored;
    scheduler_->cancel(process.task);
  }
}

void SystemVerilogUvmPhaseService::set_scheduler(Scheduler& scheduler) {
  if (!processes_.empty() || (scheduler_ && scheduler_ != &scheduler)) {
    fail(kInvalidControl, "UVM phase scheduler cannot change with live state");
  }
  scheduler_ = &scheduler;
}

void SystemVerilogUvmPhaseService::validate_identity(
    const std::string_view identity) const {
  if (identity.empty()) {
    fail(kInvalidIdentity, "UVM phase identity must not be empty");
  }
  if (identity.size() > limits_.maximum_identity_bytes) {
    fail(kResourceLimit, "UVM phase identity exceeds the byte limit");
  }
  if (std::ranges::any_of(identity, [](const unsigned char character) {
        return character < 0x21U || character == 0x7fU;
      })) {
    fail(kInvalidIdentity, "UVM phase identity contains whitespace or control");
  }
}

void SystemVerilogUvmPhaseService::require_mutation_budget(
    const std::size_t count) const {
  if (limits_.maximum_mutations -
          std::min(mutations_, limits_.maximum_mutations) < count) {
    fail(kResourceLimit, "UVM phase graph mutation budget exceeded");
  }
}

bool SystemVerilogUvmPhaseService::contains(
    const SystemVerilogUvmDomainHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_ != owner_) return false;
  const auto found = domains_.find(handle.slot_);
  return found != domains_.end()
      && found->second.handle.generation_ == handle.generation_;
}

bool SystemVerilogUvmPhaseService::contains(
    const SystemVerilogUvmPhaseProcessHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_.get() != owner_.get()) return false;
  const auto found = processes_.find(handle.slot_);
  return found != processes_.end()
      && found->second.handle.generation_ == handle.generation_;
}

bool SystemVerilogUvmPhaseService::contains(
    const SystemVerilogUvmPhaseHandle handle) const noexcept {
  if (!handle.valid() || handle.owner_ != owner_) return false;
  const auto found = phases_.find(handle.slot_);
  return found != phases_.end()
      && found->second.handle.generation_ == handle.generation_;
}

SystemVerilogUvmPhaseService::Domain&
SystemVerilogUvmPhaseService::domain(
    const SystemVerilogUvmDomainHandle handle) {
  if (!contains(handle)) {
    fail(kInvalidHandle, "stale or foreign UVM domain handle");
  }
  return domains_.at(handle.slot_);
}

const SystemVerilogUvmPhaseService::Domain&
SystemVerilogUvmPhaseService::domain(
    const SystemVerilogUvmDomainHandle handle) const {
  if (!contains(handle)) {
    fail(kInvalidHandle, "stale or foreign UVM domain handle");
  }
  return domains_.at(handle.slot_);
}

SystemVerilogUvmPhaseService::Phase&
SystemVerilogUvmPhaseService::phase(
    const SystemVerilogUvmPhaseHandle handle) {
  if (!contains(handle)) {
    fail(kInvalidHandle, "stale or foreign UVM phase handle");
  }
  return phases_.at(handle.slot_);
}

const SystemVerilogUvmPhaseService::Phase&
SystemVerilogUvmPhaseService::phase(
    const SystemVerilogUvmPhaseHandle handle) const {
  if (!contains(handle)) {
    fail(kInvalidHandle, "stale or foreign UVM phase handle");
  }
  return phases_.at(handle.slot_);
}

SystemVerilogUvmPhaseService::PhaseProcess&
SystemVerilogUvmPhaseService::process(
    const SystemVerilogUvmPhaseProcessHandle handle) {
  if (!handle.valid() || handle.owner_ != owner_) {
    fail(kInvalidHandle, "stale or foreign UVM phase-process handle");
  }
  const auto found = processes_.find(handle.slot_);
  if (found == processes_.end()
      || found->second.handle.generation_ != handle.generation_) {
    fail(kInvalidHandle, "stale or foreign UVM phase-process handle");
  }
  return found->second;
}

const SystemVerilogUvmPhaseService::PhaseProcess&
SystemVerilogUvmPhaseService::process(
    const SystemVerilogUvmPhaseProcessHandle handle) const {
  if (!handle.valid() || handle.owner_ != owner_) {
    fail(kInvalidHandle, "stale or foreign UVM phase-process handle");
  }
  const auto found = processes_.find(handle.slot_);
  if (found == processes_.end()
      || found->second.handle.generation_ != handle.generation_) {
    fail(kInvalidHandle, "stale or foreign UVM phase-process handle");
  }
  return found->second;
}

SystemVerilogUvmDomainHandle
SystemVerilogUvmPhaseService::create_domain(
    std::string identity,
    const SystemVerilogUvmDomainKind kind) {
  validate_identity(identity);
  switch (kind) {
    case SystemVerilogUvmDomainKind::Common:
    case SystemVerilogUvmDomainKind::Runtime:
    case SystemVerilogUvmDomainKind::Custom:
      break;
    default:
      fail(kInvalidIdentity, "invalid UVM domain kind");
  }
  if (domains_.size() >= limits_.maximum_domains) {
    fail(kResourceLimit, "UVM phase domain budget exceeded");
  }
  if (domains_by_identity_.contains(identity)) {
    fail(kInvalidIdentity, "duplicate UVM phase domain identity");
  }
  if (kind != SystemVerilogUvmDomainKind::Custom
      && std::ranges::any_of(domains_, [kind](const auto& entry) {
           return entry.second.kind == kind;
         })) {
    fail(kInvalidIdentity, "duplicate standard UVM phase domain kind");
  }
  if (next_domain_ == 0
      || next_registration_order_
          == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM phase domain identity overflow");
  }
  require_mutation_budget();

  const auto slot = next_domain_;
  const auto handle = SystemVerilogUvmDomainHandle{owner_, slot, 1};
  auto [inserted_domain, inserted] = domains_.emplace(
      slot,
      Domain{
          handle, std::move(identity), kind,
          next_registration_order_, {}, {}, {}, std::nullopt});
  if (!inserted) {
    fail(kInvalidIdentity, "duplicate UVM phase domain slot");
  }
  try {
    const auto [unused, indexed] = domains_by_identity_.emplace(
        inserted_domain->second.identity, slot);
    if (!indexed) {
      domains_.erase(inserted_domain);
      fail(kInvalidIdentity, "duplicate UVM phase domain identity");
    }
  } catch (...) {
    domains_.erase(slot);
    throw;
  }
  ++next_domain_;
  ++next_registration_order_;
  ++mutations_;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Graph,
        SystemVerilogUvmActivityAction::Created,
        inserted_domain->second.identity,
        "domain",
        0,
        inserted_domain->second.registration_order});
  }
  return handle;
}

std::size_t SystemVerilogUvmPhaseService::parent_depth(
    const std::optional<SystemVerilogUvmPhaseHandle> parent_handle,
    const SystemVerilogUvmDomainHandle domain_handle) const {
  std::size_t depth{};
  std::size_t work{};
  auto current = parent_handle;
  std::set<std::uint64_t> visited;
  while (current) {
    consume_work(work, limits_.maximum_traversal_work);
    const auto& current_phase = phase(*current);
    if (current_phase.domain != domain_handle) {
      fail(kInvalidGraph, "UVM phase parent belongs to another domain");
    }
    if (!visited.insert(current->slot_).second) {
      fail(kInvalidGraph, "UVM phase parent cycle detected");
    }
    ++depth;
    if (depth > limits_.maximum_depth) {
      fail(kInvalidGraph, "UVM phase parent depth exceeded");
    }
    current = current_phase.parent
        ? std::optional<SystemVerilogUvmPhaseHandle>{
              phases_.at(*current_phase.parent).handle}
        : std::nullopt;
  }
  return depth;
}

SystemVerilogUvmPhaseHandle
SystemVerilogUvmPhaseService::create_phase(
    const SystemVerilogUvmDomainHandle domain_handle,
    std::string identity,
    const SystemVerilogUvmPhaseKind kind,
    const SystemVerilogUvmPhaseExecutionKind execution,
    const std::optional<SystemVerilogUvmPhaseHandle> parent_handle) {
  validate_identity(identity);
  auto& owner = domain(domain_handle);
  (void)parent_depth(parent_handle, domain_handle);
  if (phases_.size() >= limits_.maximum_phases
      || owner.phases.size() >= limits_.maximum_phases_per_domain) {
    fail(kResourceLimit, "UVM phase node budget exceeded");
  }
  if (owner.phases_by_identity.contains(identity)) {
    fail(kInvalidIdentity, "duplicate UVM phase identity in domain");
  }
  if (next_phase_ == 0
      || next_registration_order_
          == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM phase node identity overflow");
  }
  require_mutation_budget();

  const auto slot = next_phase_;
  const auto handle = SystemVerilogUvmPhaseHandle{owner_, slot, 1};
  auto [inserted_phase, inserted] = phases_.emplace(
      slot,
      Phase{
          handle, domain_handle, kind, execution,
          kind == SystemVerilogUvmPhaseKind::Build
              ? SystemVerilogUvmPhaseTraversal::TopDown
              : SystemVerilogUvmPhaseTraversal::BottomUp,
          SystemVerilogUvmPhaseState::Dormant, std::move(identity),
          next_registration_order_,
          parent_handle
              ? std::optional<std::uint64_t>{parent_handle->slot_}
              : std::nullopt,
          {}, {}, {}, {}, std::nullopt});
  if (!inserted) {
    fail(kInvalidIdentity, "duplicate UVM phase node slot");
  }
  bool indexed{};
  try {
    const auto [unused, did_index] = owner.phases_by_identity.emplace(
        inserted_phase->second.identity, slot);
    indexed = did_index;
    if (!indexed) {
      phases_.erase(inserted_phase);
      fail(kInvalidIdentity, "duplicate UVM phase identity in domain");
    }
    owner.phases.push_back(slot);
  } catch (...) {
    if (indexed) {
      owner.phases_by_identity.erase(inserted_phase->second.identity);
    }
    phases_.erase(slot);
    throw;
  }
  ++next_phase_;
  ++next_registration_order_;
  ++mutations_;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Graph,
        SystemVerilogUvmActivityAction::Created,
        owner.identity + "." + inserted_phase->second.identity,
        "phase",
        0,
        inserted_phase->second.registration_order});
  }
  return handle;
}

SystemVerilogUvmPhaseHandle
SystemVerilogUvmPhaseService::create_standard_phase(
    const SystemVerilogUvmDomainHandle domain_handle,
    const SystemVerilogUvmPhaseKind kind,
    const std::optional<SystemVerilogUvmPhaseHandle> parent_handle) {
  if (kind == SystemVerilogUvmPhaseKind::Custom
      || systemverilog_uvm_phase_identity(kind).empty()) {
    fail(kInvalidIdentity, "standard UVM phase kind is invalid");
  }
  if (domain(domain_handle).kind
      != systemverilog_uvm_phase_domain_kind(kind)) {
    fail(kInvalidGraph, "standard UVM phase belongs to the wrong domain kind");
  }
  return create_phase(
      domain_handle,
      std::string{systemverilog_uvm_phase_identity(kind)},
      kind,
      systemverilog_uvm_phase_execution_kind(kind),
      parent_handle);
}

SystemVerilogUvmPhaseHandle
SystemVerilogUvmPhaseService::create_custom_phase(
    const SystemVerilogUvmDomainHandle domain_handle,
    std::string identity,
    const SystemVerilogUvmPhaseExecutionKind execution,
    const std::optional<SystemVerilogUvmPhaseHandle> parent_handle) {
  for (std::uint8_t raw{};
       raw < static_cast<std::uint8_t>(SystemVerilogUvmPhaseKind::Custom);
       ++raw) {
    if (identity == systemverilog_uvm_phase_identity(
                        static_cast<SystemVerilogUvmPhaseKind>(raw))) {
      fail(kInvalidIdentity, "custom UVM phase uses a reserved identity");
    }
  }
  return create_phase(
      domain_handle, std::move(identity),
      SystemVerilogUvmPhaseKind::Custom, execution, parent_handle);
}

SystemVerilogUvmStandardSchedule
SystemVerilogUvmPhaseService::create_standard_schedule(
    const std::span<const SystemVerilogUvmRootHandle> roots) {
  if (standard_schedule_) {
    fail(kInvalidIdentity, "standard UVM phase schedule already exists");
  }
  auto domains_before = domains_;
  auto domain_index_before = domains_by_identity_;
  auto phases_before = phases_;
  const auto next_domain_before = next_domain_;
  const auto next_phase_before = next_phase_;
  const auto next_order_before = next_registration_order_;
  const auto edges_before = edges_;
  const auto mutations_before = mutations_;
  try {
    SystemVerilogUvmStandardSchedule schedule;
    schedule.common_domain = create_domain(
        "common", SystemVerilogUvmDomainKind::Common);
    schedule.runtime_domain = create_domain(
        "uvm", SystemVerilogUvmDomainKind::Runtime);
    constexpr std::array common_kinds{
        SystemVerilogUvmPhaseKind::Build,
        SystemVerilogUvmPhaseKind::Connect,
        SystemVerilogUvmPhaseKind::EndOfElaboration,
        SystemVerilogUvmPhaseKind::StartOfSimulation,
        SystemVerilogUvmPhaseKind::Run,
        SystemVerilogUvmPhaseKind::Extract,
        SystemVerilogUvmPhaseKind::Check,
        SystemVerilogUvmPhaseKind::Report,
        SystemVerilogUvmPhaseKind::Final};
    constexpr std::array runtime_kinds{
        SystemVerilogUvmPhaseKind::PreReset,
        SystemVerilogUvmPhaseKind::Reset,
        SystemVerilogUvmPhaseKind::PostReset,
        SystemVerilogUvmPhaseKind::PreConfigure,
        SystemVerilogUvmPhaseKind::Configure,
        SystemVerilogUvmPhaseKind::PostConfigure,
        SystemVerilogUvmPhaseKind::PreMain,
        SystemVerilogUvmPhaseKind::Main,
        SystemVerilogUvmPhaseKind::PostMain,
        SystemVerilogUvmPhaseKind::PreShutdown,
        SystemVerilogUvmPhaseKind::Shutdown,
        SystemVerilogUvmPhaseKind::PostShutdown};
    const auto construct_chain = [&](const auto domain_handle,
                                     const auto& kinds) {
      std::optional<SystemVerilogUvmPhaseHandle> previous;
      for (const auto kind : kinds) {
        const auto handle = create_standard_phase(domain_handle, kind);
        schedule.phases[static_cast<std::size_t>(kind)] = handle;
        if (previous) connect(*previous, handle);
        previous = handle;
      }
    };
    construct_chain(schedule.common_domain, common_kinds);
    construct_chain(schedule.runtime_domain, runtime_kinds);
    place_domain_with(
        schedule.runtime_domain,
        schedule.phase(SystemVerilogUvmPhaseKind::Run));
    for (const auto root : roots) {
      participate(schedule.common_domain, root);
      participate(schedule.runtime_domain, root);
    }
    standard_schedule_ = schedule;
    return schedule;
  } catch (...) {
    domains_.swap(domains_before);
    domains_by_identity_.swap(domain_index_before);
    phases_.swap(phases_before);
    next_domain_ = next_domain_before;
    next_phase_ = next_phase_before;
    next_registration_order_ = next_order_before;
    edges_ = edges_before;
    mutations_ = mutations_before;
    standard_schedule_.reset();
    throw;
  }
}

SystemVerilogUvmPhaseHandle
SystemVerilogUvmPhaseService::insert_custom_phase(
    const SystemVerilogUvmDomainHandle domain_handle,
    std::string identity,
    const SystemVerilogUvmPhaseExecutionKind execution,
    const SystemVerilogUvmPhasePlacement placement) {
  const auto& selected = domain(domain_handle);
  if (placement.with_phase
      && (placement.after_phase || placement.before_phase)) {
    fail(
        kInvalidGraph,
        "with-phase placement excludes before/after placement");
  }
  const auto validate_anchor = [&](const auto handle) {
    if (phase(handle).domain != domain_handle) {
      fail(kInvalidGraph, "UVM phase placement crosses domains");
    }
  };
  if (placement.with_phase) validate_anchor(*placement.with_phase);
  if (placement.after_phase) validate_anchor(*placement.after_phase);
  if (placement.before_phase) validate_anchor(*placement.before_phase);
  if (placement.after_phase && placement.before_phase) {
    if (*placement.after_phase == *placement.before_phase
        || !reachable(
            placement.after_phase->slot_,
            placement.before_phase->slot_)) {
      fail(kInvalidGraph, "UVM phase placement anchors are not ordered");
    }
  }

  std::vector<SystemVerilogUvmPhaseHandle> predecessors;
  std::vector<SystemVerilogUvmPhaseHandle> successors;
  if (placement.with_phase) {
    const auto& anchor = phase(*placement.with_phase);
    for (const auto slot : anchor.predecessors) {
      predecessors.push_back(phases_.at(slot).handle);
    }
    for (const auto slot : anchor.successors) {
      successors.push_back(phases_.at(slot).handle);
    }
  } else if (placement.before_phase && !placement.after_phase) {
    for (const auto slot : phase(*placement.before_phase).predecessors) {
      predecessors.push_back(phases_.at(slot).handle);
    }
  } else if (placement.after_phase && !placement.before_phase) {
    for (const auto slot : phase(*placement.after_phase).successors) {
      successors.push_back(phases_.at(slot).handle);
    }
  } else if (!placement.after_phase && !placement.before_phase) {
    for (const auto slot : selected.phases) {
      const auto& candidate = phases_.at(slot);
      if (candidate.successors.empty()) {
        predecessors.push_back(candidate.handle);
      }
    }
  }

  auto domains_before = domains_;
  auto phases_before = phases_;
  const auto next_phase_before = next_phase_;
  const auto next_order_before = next_registration_order_;
  const auto edges_before = edges_;
  const auto mutations_before = mutations_;
  try {
    const auto inserted = create_custom_phase(
        domain_handle, std::move(identity), execution, placement.parent);
    if (placement.with_phase) {
      for (const auto& predecessor : predecessors) {
        connect(predecessor, inserted);
      }
      for (const auto& successor : successors) {
        connect(inserted, successor);
      }
    } else if (placement.before_phase && !placement.after_phase) {
      for (const auto& predecessor : predecessors) {
        disconnect(predecessor, *placement.before_phase);
        connect(predecessor, inserted);
      }
      connect(inserted, *placement.before_phase);
    } else if (placement.after_phase && !placement.before_phase) {
      for (const auto& successor : successors) {
        disconnect(*placement.after_phase, successor);
        connect(inserted, successor);
      }
      connect(*placement.after_phase, inserted);
    } else if (placement.after_phase && placement.before_phase) {
      const auto& after = phase(*placement.after_phase);
      if (std::ranges::find(
              after.successors, placement.before_phase->slot_)
          != after.successors.end()) {
        disconnect(*placement.after_phase, *placement.before_phase);
      }
      connect(*placement.after_phase, inserted);
      connect(inserted, *placement.before_phase);
    } else {
      for (const auto& predecessor : predecessors) {
        connect(predecessor, inserted);
      }
    }
    return inserted;
  } catch (...) {
    domains_.swap(domains_before);
    phases_.swap(phases_before);
    next_phase_ = next_phase_before;
    next_registration_order_ = next_order_before;
    edges_ = edges_before;
    mutations_ = mutations_before;
    throw;
  }
}

bool SystemVerilogUvmPhaseService::reachable(
    const std::uint64_t from,
    const std::uint64_t target) const {
  std::vector<std::uint64_t> pending{from};
  std::set<std::uint64_t> visited;
  std::size_t work{};
  while (!pending.empty()) {
    consume_work(work, limits_.maximum_traversal_work);
    const auto current = pending.back();
    pending.pop_back();
    if (current == target) return true;
    if (!visited.insert(current).second) continue;
    const auto found = phases_.find(current);
    if (found == phases_.end()) {
      fail(kInvalidGraph, "UVM phase graph references a missing node");
    }
    for (const auto successor : found->second.successors) {
      consume_work(work, limits_.maximum_traversal_work);
      pending.push_back(successor);
    }
  }
  return false;
}

void SystemVerilogUvmPhaseService::connect(
    const SystemVerilogUvmPhaseHandle predecessor_handle,
    const SystemVerilogUvmPhaseHandle successor_handle) {
  auto& predecessor = phase(predecessor_handle);
  auto& successor = phase(successor_handle);
  if (predecessor_handle == successor_handle) {
    fail(kInvalidGraph, "UVM phase graph self edge is invalid");
  }
  if (predecessor.domain != successor.domain) {
    fail(kInvalidGraph, "UVM phase graph edge crosses domains");
  }
  if (std::ranges::find(
          predecessor.successors, successor_handle.slot_)
      != predecessor.successors.end()) {
    fail(kInvalidIdentity, "duplicate UVM phase graph edge");
  }
  if (edges_ >= limits_.maximum_edges) {
    fail(kResourceLimit, "UVM phase graph edge budget exceeded");
  }
  require_mutation_budget();
  if (reachable(successor_handle.slot_, predecessor_handle.slot_)) {
    fail(kInvalidGraph, "UVM phase graph edge would create a cycle");
  }

  predecessor.successors.push_back(successor_handle.slot_);
  try {
    successor.predecessors.push_back(predecessor_handle.slot_);
  } catch (...) {
    predecessor.successors.pop_back();
    throw;
  }
  ++edges_;
  ++mutations_;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::Graph,
        SystemVerilogUvmActivityAction::Connected,
        domain(predecessor.domain).identity + "." + predecessor.identity,
        successor.identity});
  }
}

void SystemVerilogUvmPhaseService::disconnect(
    const SystemVerilogUvmPhaseHandle predecessor_handle,
    const SystemVerilogUvmPhaseHandle successor_handle) {
  auto& predecessor = phase(predecessor_handle);
  auto& successor = phase(successor_handle);
  if (predecessor.domain != successor.domain) {
    fail(kInvalidGraph, "UVM phase graph edge crosses domains");
  }
  const auto outgoing = std::ranges::find(
      predecessor.successors, successor_handle.slot_);
  const auto incoming = std::ranges::find(
      successor.predecessors, predecessor_handle.slot_);
  if (outgoing == predecessor.successors.end()
      || incoming == successor.predecessors.end()) {
    fail(kInvalidIdentity, "missing UVM phase graph edge");
  }
  require_mutation_budget();
  predecessor.successors.erase(outgoing);
  successor.predecessors.erase(incoming);
  --edges_;
  ++mutations_;
}

void SystemVerilogUvmPhaseService::participate(
    const SystemVerilogUvmDomainHandle domain_handle,
    const SystemVerilogUvmRootHandle root) {
  auto& selected = domain(domain_handle);
  if (!components_->contains_root(root)) {
    fail(kInvalidRoot, "UVM phase domain references a missing root");
  }
  if (std::ranges::find(selected.roots, root) != selected.roots.end()) {
    fail(kInvalidIdentity, "duplicate UVM phase root participation");
  }
  if (selected.roots.size() >= limits_.maximum_roots_per_domain) {
    fail(kResourceLimit, "UVM phase root participation budget exceeded");
  }
  require_mutation_budget();
  selected.roots.push_back(root);
  ++mutations_;
}

void SystemVerilogUvmPhaseService::unparticipate(
    const SystemVerilogUvmDomainHandle domain_handle,
    const SystemVerilogUvmRootHandle root) {
  auto& selected = domain(domain_handle);
  const auto found = std::ranges::find(selected.roots, root);
  if (found == selected.roots.end()) {
    fail(kInvalidIdentity, "missing UVM phase root participation");
  }
  require_mutation_budget();
  selected.roots.erase(found);
  ++mutations_;
}

void SystemVerilogUvmPhaseService::participate_standard_root(
    const SystemVerilogUvmRootHandle root) {
  if (!standard_schedule_) {
    fail(kInvalidGraph, "standard UVM phase schedule does not exist");
  }
  // Retain enough mutation capacity to undo both participation records if
  // component construction fails after creating an automatic root.
  require_mutation_budget(4U);
  auto common_roots = domain(
      standard_schedule_->common_domain).roots;
  auto runtime_roots = domain(
      standard_schedule_->runtime_domain).roots;
  const auto mutations_before = mutations_;
  try {
    participate(standard_schedule_->common_domain, root);
    participate(standard_schedule_->runtime_domain, root);
  } catch (...) {
    domain(standard_schedule_->common_domain).roots.swap(common_roots);
    domain(standard_schedule_->runtime_domain).roots.swap(runtime_roots);
    mutations_ = mutations_before;
    throw;
  }
}

void SystemVerilogUvmPhaseService::unparticipate_standard_root(
    const SystemVerilogUvmRootHandle root) {
  if (!standard_schedule_) {
    fail(kInvalidGraph, "standard UVM phase schedule does not exist");
  }
  auto& common = domain(standard_schedule_->common_domain);
  auto& runtime = domain(standard_schedule_->runtime_domain);
  if (std::ranges::find(common.roots, root) == common.roots.end()
      || std::ranges::find(runtime.roots, root) == runtime.roots.end()) {
    fail(kInvalidIdentity, "missing standard UVM root participation");
  }
  if (limits_.maximum_mutations -
          std::min(mutations_, limits_.maximum_mutations) < 2U) {
    fail(kResourceLimit, "UVM phase graph mutation budget exceeded");
  }
  unparticipate(standard_schedule_->common_domain, root);
  unparticipate(standard_schedule_->runtime_domain, root);
}

void SystemVerilogUvmPhaseService::place_domain_with(
    const SystemVerilogUvmDomainHandle domain_handle,
    const SystemVerilogUvmPhaseHandle phase_handle) {
  auto& selected = domain(domain_handle);
  const auto& anchor = phase(phase_handle);
  if (selected.handle == anchor.domain) {
    fail(kInvalidGraph, "UVM phase domain cannot be placed within itself");
  }
  auto ancestor = anchor.domain;
  std::set<std::uint64_t> visited;
  std::size_t depth{};
  std::size_t work{};
  while (ancestor) {
    consume_work(work, limits_.maximum_traversal_work);
    if (ancestor == domain_handle) {
      fail(kInvalidGraph, "UVM phase domain placement would create a cycle");
    }
    if (!visited.insert(ancestor.slot_).second) {
      fail(kInvalidGraph, "UVM phase domain placement contains a cycle");
    }
    ++depth;
    if (depth > limits_.maximum_depth) {
      fail(kInvalidGraph, "UVM phase domain placement depth exceeded");
    }
    const auto& current = domain(ancestor);
    ancestor = current.with_phase
        ? phases_.at(*current.with_phase).domain
        : SystemVerilogUvmDomainHandle{};
  }
  if (selected.with_phase) {
    fail(kInvalidIdentity, "UVM phase domain already has a placement");
  }
  if (edges_ >= limits_.maximum_edges) {
    fail(kResourceLimit, "UVM phase graph edge budget exceeded");
  }
  require_mutation_budget();
  selected.with_phase = phase_handle.slot_;
  ++edges_;
  ++mutations_;
}

void SystemVerilogUvmPhaseService::clear_domain_placement(
    const SystemVerilogUvmDomainHandle domain_handle) {
  auto& selected = domain(domain_handle);
  if (!selected.with_phase) {
    fail(kInvalidIdentity, "UVM phase domain has no placement");
  }
  require_mutation_budget();
  selected.with_phase.reset();
  --edges_;
  ++mutations_;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>>
SystemVerilogUvmPhaseService::synchronization_pairs(
    const SystemVerilogUvmDomainHandle source_handle,
    const SystemVerilogUvmDomainHandle target_handle,
    const std::optional<SystemVerilogUvmPhaseHandle> phase_handle,
    const std::optional<SystemVerilogUvmPhaseHandle> with_phase_handle) const {
  const auto& source = domain(source_handle);
  const auto& target = domain(target_handle);
  if (source_handle == target_handle) {
    fail(kInvalidGraph, "UVM phase domain cannot synchronize with itself");
  }
  if (!phase_handle && with_phase_handle) {
    fail(kInvalidGraph, "UVM with-phase requires a source phase");
  }
  std::vector<std::pair<std::uint64_t, std::uint64_t>> result;
  if (phase_handle) {
    const auto& from = phase(*phase_handle);
    if (from.domain != source_handle) {
      fail(kInvalidGraph, "UVM synchronization source phase is in another domain");
    }
    std::uint64_t to_slot{};
    if (with_phase_handle) {
      const auto& to = phase(*with_phase_handle);
      if (to.domain != target_handle) {
        fail(kInvalidGraph, "UVM synchronization target phase is in another domain");
      }
      to_slot = with_phase_handle->slot_;
    } else {
      const auto found = target.phases_by_identity.find(from.identity);
      if (found == target.phases_by_identity.end()) {
        fail(kInvalidGraph, "matching UVM synchronization phase was not found");
      }
      to_slot = found->second;
    }
    result.emplace_back(phase_handle->slot_, to_slot);
    return result;
  }
  result.reserve(std::min(source.phases.size(), target.phases.size()));
  for (const auto from_slot : source.phases) {
    const auto& from = phases_.at(from_slot);
    const auto found = target.phases_by_identity.find(from.identity);
    if (found != target.phases_by_identity.end()) {
      result.emplace_back(from_slot, found->second);
    }
  }
  if (result.empty()) {
    fail(kInvalidGraph, "UVM phase domains have no matching phases to synchronize");
  }
  return result;
}

void SystemVerilogUvmPhaseService::synchronize_domains(
    const SystemVerilogUvmDomainHandle source,
    const SystemVerilogUvmDomainHandle target,
    const std::optional<SystemVerilogUvmPhaseHandle> phase_handle,
    const std::optional<SystemVerilogUvmPhaseHandle> with_phase_handle) {
  const auto pairs = synchronization_pairs(
      source, target, phase_handle, with_phase_handle);
  for (const auto& [from, to] : pairs) {
    const auto& from_sync = phases_.at(from).synchronized;
    const auto& to_sync = phases_.at(to).synchronized;
    const auto forward = std::ranges::find(from_sync, to);
    const auto reverse = std::ranges::find(to_sync, from);
    if ((forward == from_sync.end()) != (reverse == to_sync.end())) {
      fail(kInvalidGraph, "UVM phase synchronization edge is inconsistent");
    }
    if (forward != from_sync.end()) {
      fail(kInvalidIdentity, "duplicate UVM phase synchronization edge");
    }
  }
  if (pairs.size() > limits_.maximum_edges -
          std::min(edges_, limits_.maximum_edges)) {
    fail(kResourceLimit, "UVM phase graph edge budget exceeded");
  }
  require_mutation_budget();
  auto phases_before = phases_;
  try {
    for (const auto& [from, to] : pairs) {
      phases_.at(from).synchronized.push_back(to);
      phases_.at(to).synchronized.push_back(from);
    }
  } catch (...) {
    phases_.swap(phases_before);
    throw;
  }
  edges_ += pairs.size();
  ++mutations_;
}

void SystemVerilogUvmPhaseService::unsynchronize_domains(
    const SystemVerilogUvmDomainHandle source,
    const SystemVerilogUvmDomainHandle target,
    const std::optional<SystemVerilogUvmPhaseHandle> phase_handle,
    const std::optional<SystemVerilogUvmPhaseHandle> with_phase_handle) {
  const auto pairs = synchronization_pairs(
      source, target, phase_handle, with_phase_handle);
  for (const auto& [from, to] : pairs) {
    const auto& from_sync = phases_.at(from).synchronized;
    const auto& to_sync = phases_.at(to).synchronized;
    if (std::ranges::find(from_sync, to) == from_sync.end()
        || std::ranges::find(to_sync, from) == to_sync.end()) {
      fail(kInvalidIdentity, "missing UVM phase synchronization edge");
    }
  }
  require_mutation_budget();
  for (const auto& [from, to] : pairs) {
    auto& from_sync = phases_.at(from).synchronized;
    auto& to_sync = phases_.at(to).synchronized;
    from_sync.erase(std::ranges::find(from_sync, to));
    to_sync.erase(std::ranges::find(to_sync, from));
  }
  edges_ -= pairs.size();
  ++mutations_;
}

SystemVerilogUvmPhaseExecutionResult
SystemVerilogUvmPhaseService::execute_function_phase(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const FunctionPhaseCallback& callback) {
  auto& selected = phase(phase_handle);
  if (selected.execution != SystemVerilogUvmPhaseExecutionKind::Function) {
    fail(kInvalidGraph, "task UVM phase cannot use function-phase execution");
  }
  if (selected.state != SystemVerilogUvmPhaseState::Dormant) {
    fail(kInvalidIdentity, "UVM function phase has already executed");
  }
  if (!callback) {
    fail(kInvalidIdentity, "UVM function phase callback must not be empty");
  }

  const auto roots = domain(selected.domain).roots;
  for (const auto root : roots) {
    if (!components_->contains_root(root)) {
      fail(kInvalidRoot, "UVM phase domain contains a stale component root");
    }
  }

  SystemVerilogUvmPhaseExecutionResult result;
  result.phase = phase_handle;
  set_state(selected, SystemVerilogUvmPhaseState::Scheduled);

  auto invoke = [&](const SystemVerilogUvmRootHandle root,
                    const SystemVerilogClassHandle component,
                    const SystemVerilogUvmPhaseCallbackKind kind,
                    const bool permit_build_child) {
    result.events.push_back(SystemVerilogUvmPhaseExecutionEvent{
        phase_handle, root, component, kind, selected.state,
        static_cast<std::uint64_t>(result.events.size())});

    components_->begin_phase_callback(
        permit_build_child
            ? std::optional<SystemVerilogClassHandle>{component}
            : std::nullopt);
    std::exception_ptr callback_error;
    try {
      callback(component, phase_handle, kind);
    } catch (...) {
      callback_error = std::current_exception();
    }
    components_->end_phase_callback();

    if (!callback_error) return;
    std::string message{"unknown exception from UVM phase callback"};
    try {
      std::rethrow_exception(callback_error);
    } catch (const std::exception& error) {
      message = error.what();
    } catch (...) {
    }
    result.failures.push_back(SystemVerilogUvmPhaseExecutionFailure{
        std::string{kCallbackFailure}, phase_handle, root, component, kind,
        std::move(message)});
  };

  auto traverse = [&](const SystemVerilogUvmPhaseCallbackKind kind,
                      const SystemVerilogUvmPhaseState state,
                      const bool permit_build_children) {
    selected.state = state;
    std::size_t work{};
    for (const auto root : roots) {
      const auto tops = components_->top_components(root);
      if (selected.traversal == SystemVerilogUvmPhaseTraversal::TopDown) {
        std::vector<SystemVerilogClassHandle> pending;
        pending.reserve(components_->component_count());
        for (auto current = tops.rbegin(); current != tops.rend(); ++current) {
          pending.push_back(*current);
        }
        while (!pending.empty()) {
          consume_work(work, limits_.maximum_traversal_work);
          const auto component = pending.back();
          pending.pop_back();
          invoke(root, component, kind, permit_build_children);
          const auto children = components_->children(component);
          for (auto current = children.rbegin();
               current != children.rend(); ++current) {
            pending.push_back(*current);
          }
        }
        continue;
      }

      for (const auto top : tops) {
        std::vector<std::pair<SystemVerilogClassHandle, bool>> pending;
        pending.emplace_back(top, false);
        while (!pending.empty()) {
          const auto [component, visited] = pending.back();
          pending.pop_back();
          if (visited) {
            consume_work(work, limits_.maximum_traversal_work);
            invoke(root, component, kind, false);
            continue;
          }
          pending.emplace_back(component, true);
          const auto children = components_->children(component);
          for (auto current = children.rbegin();
               current != children.rend(); ++current) {
            pending.emplace_back(*current, false);
          }
        }
      }
    }
  };

  try {
    traverse(
        SystemVerilogUvmPhaseCallbackKind::PhaseStarted,
        SystemVerilogUvmPhaseState::Started,
        false);
    traverse(
        SystemVerilogUvmPhaseCallbackKind::Execute,
        SystemVerilogUvmPhaseState::Executing,
        selected.kind == SystemVerilogUvmPhaseKind::Build);
    traverse(
        SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd,
        SystemVerilogUvmPhaseState::ReadyToEnd,
        false);
    traverse(
        SystemVerilogUvmPhaseCallbackKind::PhaseEnded,
        SystemVerilogUvmPhaseState::Ended,
        false);
    set_state(selected, SystemVerilogUvmPhaseState::Cleanup);
    set_state(selected, SystemVerilogUvmPhaseState::Done);
    result.final_state = selected.state;
    return result;
  } catch (...) {
    components_->end_phase_callback();
    set_state(selected, SystemVerilogUvmPhaseState::Cleanup);
    set_state(selected, SystemVerilogUvmPhaseState::Done);
    result.final_state = selected.state;
    reclaim_processes(selected);
    selected.task_result.reset();
    throw;
  }
}

SystemVerilogUvmPhaseExecutionResult
SystemVerilogUvmPhaseService::execute_task_phase(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const FunctionPhaseCallback& hook_callback,
    const TaskPhaseCallback& task_callback) {
  auto& selected = phase(phase_handle);
  if (selected.execution != SystemVerilogUvmPhaseExecutionKind::Task) {
    fail(kInvalidGraph, "function UVM phase cannot use task-phase execution");
  }
  if (selected.state != SystemVerilogUvmPhaseState::Dormant) {
    fail(kInvalidControl, "UVM task phase is not dormant");
  }
  if (!hook_callback || !task_callback) {
    fail(kInvalidIdentity, "UVM task-phase callbacks must not be empty");
  }
  const auto roots = domain(selected.domain).roots;
  for (const auto root : roots) {
    if (!components_->contains_root(root)) {
      fail(kInvalidRoot, "UVM phase domain contains a stale component root");
    }
  }
  if (processes_.size() > limits_.maximum_phase_processes
      || limits_.maximum_phase_processes - processes_.size()
          < components_->component_count()) {
    fail(kResourceLimit, "UVM phase-process budget exceeded");
  }

  selected.ready_to_end_attempts = 0;
  selected.task_result.emplace();
  auto& result = *selected.task_result;
  result.phase = phase_handle;
  set_state(selected, SystemVerilogUvmPhaseState::Scheduled);

  auto component_order = [&] {
    std::vector<std::pair<SystemVerilogUvmRootHandle,
                          SystemVerilogClassHandle>> order;
    std::size_t work{};
    for (const auto root : roots) {
      const auto tops = components_->top_components(root);
      std::vector<SystemVerilogClassHandle> pending;
      for (auto current = tops.rbegin(); current != tops.rend(); ++current) {
        pending.push_back(*current);
      }
      while (!pending.empty()) {
        consume_work(work, limits_.maximum_traversal_work);
        const auto component = pending.back();
        pending.pop_back();
        order.emplace_back(root, component);
        const auto children = components_->children(component);
        for (auto current = children.rbegin();
             current != children.rend(); ++current) {
          pending.push_back(*current);
        }
      }
    }
    return order;
  };
  const auto order = component_order();

  auto record_failure = [&](const SystemVerilogUvmRootHandle root,
                            const SystemVerilogClassHandle component,
                            const SystemVerilogUvmPhaseCallbackKind kind,
                            const std::exception_ptr error) {
    std::string message{"unknown exception from UVM phase callback"};
    try {
      std::rethrow_exception(error);
    } catch (const std::exception& exception) {
      message = exception.what();
    } catch (...) {
    }
    result.failures.push_back(SystemVerilogUvmPhaseExecutionFailure{
        std::string{kCallbackFailure}, phase_handle, root, component, kind,
        std::move(message)});
  };
  auto invoke_hook = [&](const SystemVerilogUvmRootHandle root,
                         const SystemVerilogClassHandle component,
                         const SystemVerilogUvmPhaseCallbackKind kind) {
    result.events.push_back(SystemVerilogUvmPhaseExecutionEvent{
        phase_handle, root, component, kind, selected.state,
        static_cast<std::uint64_t>(result.events.size())});
    components_->begin_phase_callback(std::nullopt);
    std::exception_ptr error;
    try {
      hook_callback(component, phase_handle, kind);
    } catch (...) {
      error = std::current_exception();
    }
    components_->end_phase_callback();
    if (error) record_failure(root, component, kind, error);
  };

  try {
    set_state(selected, SystemVerilogUvmPhaseState::Started);
    for (const auto& [root, component] : order) {
      invoke_hook(
          root, component,
          SystemVerilogUvmPhaseCallbackKind::PhaseStarted);
    }

    set_state(selected, SystemVerilogUvmPhaseState::Executing);
    for (const auto& [root, component] : order) {
      if (next_process_ == 0
          || next_process_registration_order_
              == std::numeric_limits<std::uint64_t>::max()) {
        fail(kResourceLimit, "UVM phase-process identity overflow");
      }
      const auto process_handle = SystemVerilogUvmPhaseProcessHandle{
          owner_, next_process_, 1};
      const auto [inserted, did_insert] = processes_.emplace(
          next_process_,
          PhaseProcess{
              process_handle, phase_handle, root, component,
              SystemVerilogUvmPhaseProcessState::Running,
              next_process_registration_order_, std::nullopt, {}, 0, {}});
      if (!did_insert) {
        fail(kInvalidControl, "duplicate UVM phase-process slot");
      }
      selected.processes.push_back(next_process_);
      result.processes.push_back(process_handle);
      ++next_process_;
      ++next_process_registration_order_;
      result.events.push_back(SystemVerilogUvmPhaseExecutionEvent{
          phase_handle, root, component,
          SystemVerilogUvmPhaseCallbackKind::Execute, selected.state,
          static_cast<std::uint64_t>(result.events.size())});

      components_->begin_phase_callback(std::nullopt);
      std::exception_ptr error;
      SystemVerilogUvmTaskPhaseStatus status{
          SystemVerilogUvmTaskPhaseStatus::Completed};
      try {
        status = task_callback(component, phase_handle, process_handle);
      } catch (...) {
        error = std::current_exception();
      }
      components_->end_phase_callback();
      if (error) {
        inserted->second.state =
            SystemVerilogUvmPhaseProcessState::Completed;
        record_failure(
            root, component,
            SystemVerilogUvmPhaseCallbackKind::Execute, error);
      } else if (status == SystemVerilogUvmTaskPhaseStatus::Completed) {
        inserted->second.state =
            SystemVerilogUvmPhaseProcessState::Completed;
      }
    }
  } catch (...) {
    components_->end_phase_callback();
    for (const auto slot : selected.processes) {
      auto& owned = processes_.at(slot);
      if (owned.state == SystemVerilogUvmPhaseProcessState::Running) {
        owned.state = SystemVerilogUvmPhaseProcessState::Cancelled;
      }
    }
    set_state(selected, SystemVerilogUvmPhaseState::Cleanup);
    set_state(selected, SystemVerilogUvmPhaseState::Done);
    result.final_state = selected.state;
    throw;
  }

  const auto running = std::ranges::any_of(
      selected.processes, [&](const auto slot) {
        return processes_.at(slot).state
            == SystemVerilogUvmPhaseProcessState::Running;
      });
  if (!running) return complete_task_phase(phase_handle, hook_callback);
  result.final_state = selected.state;
  return result;
}

std::vector<SystemVerilogUvmPhaseExecutionResult>
SystemVerilogUvmPhaseService::execute_synchronized_task_phases(
    const std::span<const SystemVerilogUvmPhaseHandle> phase_handles,
    const FunctionPhaseCallback& hook_callback,
    const TaskPhaseCallback& task_callback) {
  if (phase_handles.size() < 2U) {
    fail(kInvalidControl, "synchronized UVM execution requires two phases");
  }
  std::set<std::uint64_t> selected;
  for (const auto& handle : phase_handles) {
    const auto& value = phase(handle);
    if (value.execution != SystemVerilogUvmPhaseExecutionKind::Task
        || value.state != SystemVerilogUvmPhaseState::Dormant
        || !selected.insert(handle.slot_).second) {
      fail(kInvalidControl, "invalid synchronized UVM task-phase set");
    }
  }
  std::set<std::uint64_t> reached{phase_handles.front().slot_};
  std::vector<std::uint64_t> pending{phase_handles.front().slot_};
  while (!pending.empty()) {
    const auto slot = pending.back();
    pending.pop_back();
    for (const auto peer : phases_.at(slot).synchronized) {
      if (selected.contains(peer) && reached.insert(peer).second) {
        pending.push_back(peer);
      }
    }
  }
  if (reached.size() != selected.size()) {
    fail(kInvalidGraph, "UVM task phases do not form one synchronized group");
  }

  std::vector<SystemVerilogUvmPhaseExecutionResult> result;
  result.reserve(phase_handles.size());
  for (const auto& handle : phase_handles) {
    result.push_back(execute_task_phase(
        handle, hook_callback, task_callback));
  }
  return result;
}

void SystemVerilogUvmPhaseService::complete_task_process(
    const SystemVerilogUvmPhaseProcessHandle handle) {
  auto& selected = process(handle);
  if (selected.state != SystemVerilogUvmPhaseProcessState::Running) {
    fail(kInvalidControl, "UVM phase process is not running");
  }
  if (has_running_child(selected)) {
    fail(kInvalidControl, "UVM phase process still owns running children");
  }
  if (scheduler_) scheduler_->cancel(selected.task);
  selected.state = SystemVerilogUvmPhaseProcessState::Completed;
}

void SystemVerilogUvmPhaseService::cancel_task_process(
    const SystemVerilogUvmPhaseProcessHandle handle) {
  auto& selected = process(handle);
  if (selected.state != SystemVerilogUvmPhaseProcessState::Running) {
    fail(kInvalidControl, "UVM phase process is not running");
  }
  cancel_process_tree(handle.slot_);
}

SystemVerilogUvmPhaseProcessHandle
SystemVerilogUvmPhaseService::begin_child_process(
    const SystemVerilogUvmPhaseProcessHandle parent_handle) {
  auto& parent = process(parent_handle);
  auto& selected_phase = phase(parent.phase);
  if (parent.state != SystemVerilogUvmPhaseProcessState::Running
      || selected_phase.state != SystemVerilogUvmPhaseState::Executing) {
    fail(kInvalidControl, "UVM child process cannot begin here");
  }
  if (parent.depth >= limits_.maximum_process_depth) {
    fail(kResourceLimit, "UVM phase-process tree depth exceeded");
  }
  if (processes_.size() >= limits_.maximum_phase_processes
      || next_process_ == 0
      || next_process_registration_order_
          == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM phase-process identity ceiling exceeded");
  }

  const auto slot = next_process_;
  const auto handle = SystemVerilogUvmPhaseProcessHandle{owner_, slot, 1};
  auto parent_children = parent.children;
  auto phase_processes = selected_phase.processes;
  parent_children.push_back(slot);
  phase_processes.push_back(slot);
  const auto [inserted, did_insert] = processes_.emplace(
      slot,
      PhaseProcess{
          handle,
          parent.phase,
          parent.root,
          parent.component,
          SystemVerilogUvmPhaseProcessState::Running,
          next_process_registration_order_,
          parent_handle.slot_,
          {},
          parent.depth + 1U,
          {}});
  if (!did_insert) {
    fail(kInvalidControl, "duplicate UVM child-process slot");
  }
  (void)inserted;
  parent.children.swap(parent_children);
  selected_phase.processes.swap(phase_processes);
  ++next_process_;
  ++next_process_registration_order_;
  return handle;
}

bool SystemVerilogUvmPhaseService::has_running_child(
    const PhaseProcess& selected) const {
  return std::ranges::any_of(selected.children, [&](const auto slot) {
    const auto found = processes_.find(slot);
    return found != processes_.end()
        && (found->second.state == SystemVerilogUvmPhaseProcessState::Running
            || has_running_child(found->second));
  });
}

void SystemVerilogUvmPhaseService::cancel_process_tree(
    const std::uint64_t slot,
    std::vector<SystemVerilogUvmPhaseProcessHandle>* cancelled) noexcept {
  const auto found = processes_.find(slot);
  if (found == processes_.end()) return;
  const auto children = found->second.children;
  for (const auto child : children) {
    cancel_process_tree(child, cancelled);
  }
  auto& selected = found->second;
  if (selected.state != SystemVerilogUvmPhaseProcessState::Running) return;
  if (scheduler_) scheduler_->cancel(selected.task);
  selected.state = SystemVerilogUvmPhaseProcessState::Cancelled;
  if (cancelled) cancelled->push_back(selected.handle);
}

SystemVerilogUvmPhaseProcessHandle
SystemVerilogUvmPhaseService::schedule_child_process(
    const SystemVerilogUvmPhaseProcessHandle parent_handle,
    const SimulationTick delay,
    ScheduledProcessCallback callback) {
  auto& parent = process(parent_handle);
  auto& selected_phase = phase(parent.phase);
  if (!scheduler_ || !callback
      || parent.state != SystemVerilogUvmPhaseProcessState::Running
      || selected_phase.state != SystemVerilogUvmPhaseState::Executing) {
    fail(kInvalidControl, "UVM child process cannot be scheduled here");
  }
  if (parent.depth >= limits_.maximum_process_depth) {
    fail(kResourceLimit, "UVM phase-process tree depth exceeded");
  }
  if (processes_.size() >= limits_.maximum_phase_processes) {
    fail(kResourceLimit, "UVM phase-process ceiling exceeded");
  }
  const auto scheduled_count = static_cast<std::size_t>(std::ranges::count_if(
      processes_, [](const auto& entry) {
        return static_cast<bool>(entry.second.task);
      }));
  if (scheduled_count >= limits_.maximum_scheduled_processes
      || next_process_ == 0
      || next_process_registration_order_
          == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM scheduled-process ceiling exceeded");
  }

  const auto slot = next_process_;
  const auto handle = SystemVerilogUvmPhaseProcessHandle{owner_, slot, 1};
  auto task = scheduler_->schedule_after_cancelable(
      delay,
      SchedulerPhase::reactive,
      next_process_registration_order_,
      [this, handle, callback = std::move(callback)](Scheduler&) {
        if (!contains(handle)) return;
        auto& scheduled = process(handle);
        if (scheduled.state != SystemVerilogUvmPhaseProcessState::Running) {
          return;
        }
        try {
          callback(handle);
          scheduled.state = SystemVerilogUvmPhaseProcessState::Completed;
        } catch (...) {
          auto& owning_phase = phase(scheduled.phase);
          if (owning_phase.task_result) {
            std::string message{"unknown exception from UVM scheduled process"};
            try {
              throw;
            } catch (const std::exception& error) {
              message = error.what();
            } catch (...) {
            }
            owning_phase.task_result->failures.push_back(
                SystemVerilogUvmPhaseExecutionFailure{
                    std::string{kCallbackFailure},
                    scheduled.phase,
                    scheduled.root,
                    scheduled.component,
                    SystemVerilogUvmPhaseCallbackKind::Execute,
                    std::move(message)});
          }
          cancel_process_tree(handle.slot_);
        }
      });
  try {
    const auto [inserted, did_insert] = processes_.emplace(
        slot,
        PhaseProcess{
            handle,
            parent.phase,
            parent.root,
            parent.component,
            SystemVerilogUvmPhaseProcessState::Running,
            next_process_registration_order_,
            parent_handle.slot_,
            {},
            parent.depth + 1U,
            task});
    if (!did_insert) {
      scheduler_->cancel(task);
      fail(kInvalidControl, "duplicate UVM child-process slot");
    }
    (void)inserted;
    parent.children.push_back(slot);
    selected_phase.processes.push_back(slot);
    ++next_process_;
    ++next_process_registration_order_;
    return handle;
  } catch (...) {
    scheduler_->cancel(task);
    processes_.erase(slot);
    throw;
  }
}

std::vector<SystemVerilogUvmPhaseProcessHandle>
SystemVerilogUvmPhaseService::timeout_task_phase(
    const SystemVerilogUvmPhaseHandle phase_handle) {
  auto& selected = phase(phase_handle);
  if (selected.execution != SystemVerilogUvmPhaseExecutionKind::Task
      || selected.state != SystemVerilogUvmPhaseState::Executing) {
    fail(kInvalidControl, "UVM task-phase timeout is not legal here");
  }
  std::vector<SystemVerilogUvmPhaseProcessHandle> cancelled;
  for (const auto slot : selected.processes) {
    const auto found = processes_.find(slot);
    if (found != processes_.end() && !found->second.parent) {
      cancel_process_tree(slot, &cancelled);
    }
  }
  set_state(selected, SystemVerilogUvmPhaseState::Cleanup);
  set_state(selected, SystemVerilogUvmPhaseState::Done);
  if (selected.task_result) selected.task_result->final_state = selected.state;
  if (objections_) objections_->cancel_phase(phase_handle);
  if (tlm1_) tlm1_->cancel_phase(phase_handle);
  reclaim_processes(selected);
  selected.task_result.reset();
  return cancelled;
}

std::vector<SystemVerilogUvmPhaseProcessHandle>
SystemVerilogUvmPhaseService::teardown_root(
    const SystemVerilogUvmRootHandle root) {
  if (!components_->contains_root(root)) {
    fail(kInvalidRoot, "UVM process teardown root is stale");
  }
  std::vector<SystemVerilogUvmPhaseProcessHandle> cancelled;
  for (auto& [ignored, selected] : phases_) {
    (void)ignored;
    std::vector<std::uint64_t> retained;
    retained.reserve(selected.processes.size());
    for (const auto slot : selected.processes) {
      const auto found = processes_.find(slot);
      if (found == processes_.end()) continue;
      if (found->second.root != root) {
        retained.push_back(slot);
        continue;
      }
      if (!found->second.parent) cancel_process_tree(slot, &cancelled);
    }
    for (const auto slot : selected.processes) {
      const auto found = processes_.find(slot);
      if (found != processes_.end() && found->second.root == root) {
        processes_.erase(found);
      }
    }
    selected.processes = std::move(retained);
  }
  if (objections_) objections_->cancel_root(root);
  if (tlm1_) tlm1_->cancel_root(root);
  return cancelled;
}

SystemVerilogUvmPhaseExecutionResult
SystemVerilogUvmPhaseService::complete_task_phase(
    const SystemVerilogUvmPhaseHandle phase_handle,
    const FunctionPhaseCallback& hook_callback) {
  auto& selected = phase(phase_handle);
  if (selected.execution != SystemVerilogUvmPhaseExecutionKind::Task
      || selected.state != SystemVerilogUvmPhaseState::Executing
      || !selected.task_result || !hook_callback) {
    fail(kInvalidControl, "UVM task phase is not ready for completion");
  }
  if (std::ranges::any_of(selected.processes, [&](const auto slot) {
        return processes_.at(slot).state
            == SystemVerilogUvmPhaseProcessState::Running;
      })) {
    fail(kInvalidControl, "UVM task phase still owns running processes");
  }
  if (objections_ && !objections_->phase_quiescent(phase_handle)) {
    fail(kInvalidControl, "UVM task phase still owns objections or drains");
  }
  auto& result = *selected.task_result;
  const auto roots = domain(selected.domain).roots;
  auto invoke_stage = [&](const SystemVerilogUvmPhaseCallbackKind kind,
                          const SystemVerilogUvmPhaseState state) {
    selected.state = state;
    std::size_t work{};
    for (const auto root : roots) {
      const auto tops = components_->top_components(root);
      std::vector<SystemVerilogClassHandle> pending;
      for (auto current = tops.rbegin(); current != tops.rend(); ++current) {
        pending.push_back(*current);
      }
      while (!pending.empty()) {
        consume_work(work, limits_.maximum_traversal_work);
        const auto component = pending.back();
        pending.pop_back();
        result.events.push_back(SystemVerilogUvmPhaseExecutionEvent{
            phase_handle, root, component, kind, selected.state,
            static_cast<std::uint64_t>(result.events.size())});
        components_->begin_phase_callback(std::nullopt);
        std::exception_ptr error;
        try {
          hook_callback(component, phase_handle, kind);
        } catch (...) {
          error = std::current_exception();
        }
        components_->end_phase_callback();
        if (error) {
          std::string message{"unknown exception from UVM phase callback"};
          try {
            std::rethrow_exception(error);
          } catch (const std::exception& exception) {
            message = exception.what();
          } catch (...) {
          }
          result.failures.push_back(SystemVerilogUvmPhaseExecutionFailure{
              std::string{kCallbackFailure}, phase_handle, root, component,
              kind, std::move(message)});
        }
        const auto children = components_->children(component);
        for (auto current = children.rbegin();
             current != children.rend(); ++current) {
          pending.push_back(*current);
        }
      }
    }
  };
  try {
    if (selected.ready_to_end_attempts
        >= limits_.maximum_ready_to_end_reentries) {
      fail(kQuiescenceFailure, "UVM ready-to-end re-entry ceiling exceeded");
    }
    ++selected.ready_to_end_attempts;
    invoke_stage(
        SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd,
        SystemVerilogUvmPhaseState::ReadyToEnd);
    if (objections_ && !objections_->phase_quiescent(phase_handle)) {
      set_state(selected, SystemVerilogUvmPhaseState::Executing);
      result.final_state = selected.state;
      return result;
    }
    invoke_stage(
        SystemVerilogUvmPhaseCallbackKind::PhaseEnded,
        SystemVerilogUvmPhaseState::Ended);
    set_state(selected, SystemVerilogUvmPhaseState::Cleanup);
    set_state(selected, SystemVerilogUvmPhaseState::Done);
    result.final_state = selected.state;
    result.final_processes.clear();
    result.final_processes.reserve(selected.processes.size());
    for (const auto slot : selected.processes) {
      result.final_processes.push_back(
          process_snapshot(processes_.at(slot).handle));
    }
    auto completed = result;
    reclaim_processes(selected);
    selected.task_result.reset();
    if (objections_) objections_->cancel_phase(phase_handle);
    if (tlm1_) tlm1_->cancel_phase(phase_handle);
    return completed;
  } catch (...) {
    components_->end_phase_callback();
    set_state(selected, SystemVerilogUvmPhaseState::Cleanup);
    set_state(selected, SystemVerilogUvmPhaseState::Done);
    result.final_state = selected.state;
    reclaim_processes(selected);
    selected.task_result.reset();
    if (objections_) objections_->cancel_phase(phase_handle);
    if (tlm1_) tlm1_->cancel_phase(phase_handle);
    throw;
  }
}

#include "uvm_phase_quiescence.tpp"

void SystemVerilogUvmPhaseService::reclaim_processes(
    Phase& selected) noexcept {
  for (const auto slot : selected.processes) {
    const auto found = processes_.find(slot);
    if (found != processes_.end() && scheduler_) {
      scheduler_->cancel(found->second.task);
    }
    processes_.erase(slot);
  }
  selected.processes.clear();
}

SystemVerilogUvmPhaseProcessSnapshot
SystemVerilogUvmPhaseService::process_snapshot(
    const SystemVerilogUvmPhaseProcessHandle handle) const {
  const auto& value = process(handle);
  return {
      value.handle, value.phase, value.root, value.component, value.state,
      value.registration_order,
      value.parent
          ? std::optional<SystemVerilogUvmPhaseProcessHandle>{
                processes_.at(*value.parent).handle}
          : std::nullopt,
      value.depth,
      static_cast<bool>(value.task)};
}

std::vector<SystemVerilogUvmPhaseProcessHandle>
SystemVerilogUvmPhaseService::phase_processes(
    const SystemVerilogUvmPhaseHandle handle) const {
  const auto& selected = phase(handle);
  std::vector<SystemVerilogUvmPhaseProcessHandle> result;
  result.reserve(selected.processes.size());
  for (const auto slot : selected.processes) {
    result.push_back(processes_.at(slot).handle);
  }
  return result;
}

std::vector<SystemVerilogUvmPhaseProcessHandle>
SystemVerilogUvmPhaseService::processes() const {
  std::vector<SystemVerilogUvmPhaseProcessHandle> result;
  result.reserve(processes_.size());
  for (const auto& [slot, selected] : processes_) {
    (void)slot;
    result.push_back(selected.handle);
  }
  return result;
}

void SystemVerilogUvmPhaseService::set_state(
    Phase& selected,
    const SystemVerilogUvmPhaseState state) {
  selected.state = state;
  if (activity_) {
    activity_->publish({
        SystemVerilogUvmActivityKind::PhaseState,
        state == SystemVerilogUvmPhaseState::Done
            ? SystemVerilogUvmActivityAction::Completed
            : state == SystemVerilogUvmPhaseState::Cleanup
                ? SystemVerilogUvmActivityAction::Cancelled
                : SystemVerilogUvmActivityAction::Updated,
        domain(selected.domain).identity + "." + selected.identity,
        std::to_string(static_cast<unsigned>(state)),
        0,
        static_cast<std::uint64_t>(state)});
  }
}

SystemVerilogUvmPhaseJumpResult SystemVerilogUvmPhaseService::jump(
    const SystemVerilogUvmPhaseHandle from_handle,
    const SystemVerilogUvmPhaseHandle target_handle) {
  auto& from = phase(from_handle);
  auto& target = phase(target_handle);
  if (from.domain != target.domain || from_handle == target_handle
      || from.state == SystemVerilogUvmPhaseState::Dormant) {
    fail(kInvalidControl, "invalid UVM phase jump endpoints");
  }
  const auto order = topological_order(from.domain);
  const auto from_at = std::ranges::find(order, from_handle);
  const auto target_at = std::ranges::find(order, target_handle);
  if (from_at == order.end() || target_at == order.end()) {
    fail(kInvalidGraph, "UVM phase jump endpoint is outside domain order");
  }
  const auto from_index = static_cast<std::size_t>(
      std::distance(order.begin(), from_at));
  const auto target_index = static_cast<std::size_t>(
      std::distance(order.begin(), target_at));
  SystemVerilogUvmPhaseJumpResult result;
  result.kind = target_index < from_index
      ? SystemVerilogUvmPhaseJumpKind::Backward
      : SystemVerilogUvmPhaseJumpKind::Forward;
  result.from = from_handle;
  result.target = target_handle;
  set_state(from, SystemVerilogUvmPhaseState::Jumping);

  const auto cancel_owned = [&](Phase& selected) {
    for (const auto slot : selected.processes) {
      auto& owned = processes_.at(slot);
      if (owned.state == SystemVerilogUvmPhaseProcessState::Running) {
        owned.state = SystemVerilogUvmPhaseProcessState::Cancelled;
        result.cancelled.push_back(owned.handle);
      }
    }
    reclaim_processes(selected);
    selected.task_result.reset();
    if (objections_) objections_->cancel_phase(selected.handle);
    if (tlm1_) tlm1_->cancel_phase(selected.handle);
  };
  if (result.kind == SystemVerilogUvmPhaseJumpKind::Forward) {
    cancel_owned(from);
    set_state(from, SystemVerilogUvmPhaseState::Done);
    for (std::size_t index = from_index + 1U; index < target_index; ++index) {
      auto& skipped = phase(order[index]);
      cancel_owned(skipped);
      set_state(skipped, SystemVerilogUvmPhaseState::Done);
      if (skipped.task_result) {
        skipped.task_result->final_state = skipped.state;
      }
      result.skipped.push_back(order[index]);
    }
    cancel_owned(target);
    set_state(target, SystemVerilogUvmPhaseState::Dormant);
    target.ready_to_end_attempts = 0;
    return result;
  }

  for (std::size_t index = target_index; index <= from_index; ++index) {
    auto& reset = phase(order[index]);
    cancel_owned(reset);
    set_state(reset, SystemVerilogUvmPhaseState::Dormant);
    reset.ready_to_end_attempts = 0;
    result.reset.push_back(order[index]);
  }
  return result;
}

SystemVerilogUvmDomainSnapshot
SystemVerilogUvmPhaseService::snapshot(
    const SystemVerilogUvmDomainHandle handle) const {
  const auto& value = domain(handle);
  SystemVerilogUvmDomainSnapshot result;
  result.handle = value.handle;
  result.identity = value.identity;
  result.kind = value.kind;
  result.registration_order = value.registration_order;
  result.roots = value.roots;
  if (value.with_phase) {
    result.with_phase = phases_.at(*value.with_phase).handle;
  }
  result.phases.reserve(value.phases.size());
  for (const auto slot : value.phases) {
    result.phases.push_back(phases_.at(slot).handle);
  }
  return result;
}

SystemVerilogUvmPhaseSnapshot
SystemVerilogUvmPhaseService::snapshot(
    const SystemVerilogUvmPhaseHandle handle) const {
  const auto& value = phase(handle);
  const auto& owner = domain(value.domain);
  SystemVerilogUvmPhaseSnapshot result;
  result.handle = value.handle;
  result.domain = value.domain;
  result.kind = value.kind;
  result.execution = value.execution;
  result.traversal = value.traversal;
  result.state = value.state;
  result.identity = value.identity;
  result.registration_order = value.registration_order;
  if (value.parent) {
    result.parent = phases_.at(*value.parent).handle;
  }
  result.predecessors.reserve(value.predecessors.size());
  for (const auto slot : value.predecessors) {
    result.predecessors.push_back(phases_.at(slot).handle);
  }
  result.successors.reserve(value.successors.size());
  for (const auto slot : value.successors) {
    result.successors.push_back(phases_.at(slot).handle);
  }
  result.synchronized.reserve(value.synchronized.size());
  for (const auto slot : value.synchronized) {
    result.synchronized.push_back(phases_.at(slot).handle);
  }
  result.roots = owner.roots;
  return result;
}

std::vector<SystemVerilogUvmDomainHandle>
SystemVerilogUvmPhaseService::domains() const {
  std::vector<SystemVerilogUvmDomainHandle> result;
  result.reserve(domains_.size());
  for (const auto& [unused, value] : domains_) {
    result.push_back(value.handle);
  }
  return result;
}

std::vector<SystemVerilogUvmPhaseHandle>
SystemVerilogUvmPhaseService::phases(
    const SystemVerilogUvmDomainHandle handle) const {
  const auto& owner = domain(handle);
  std::vector<SystemVerilogUvmPhaseHandle> result;
  result.reserve(owner.phases.size());
  for (const auto slot : owner.phases) {
    result.push_back(phases_.at(slot).handle);
  }
  return result;
}

std::vector<SystemVerilogUvmPhaseHandle>
SystemVerilogUvmPhaseService::topological_order(
    const SystemVerilogUvmDomainHandle handle) const {
  const auto& owner = domain(handle);
  std::map<std::uint64_t, std::size_t> indegrees;
  std::set<std::pair<std::uint64_t, std::uint64_t>> ready;
  std::size_t work{};
  for (const auto slot : owner.phases) {
    consume_work(work, limits_.maximum_traversal_work);
    const auto& node = phases_.at(slot);
    indegrees.emplace(slot, node.predecessors.size());
    if (node.predecessors.empty()) {
      ready.emplace(node.registration_order, slot);
    }
  }

  std::vector<SystemVerilogUvmPhaseHandle> result;
  result.reserve(owner.phases.size());
  while (!ready.empty()) {
    consume_work(work, limits_.maximum_traversal_work);
    const auto slot = ready.begin()->second;
    ready.erase(ready.begin());
    const auto& node = phases_.at(slot);
    result.push_back(node.handle);
    for (const auto successor : node.successors) {
      consume_work(work, limits_.maximum_traversal_work);
      const auto found = indegrees.find(successor);
      if (found == indegrees.end() || found->second == 0) {
        fail(kInvalidGraph, "UVM phase graph indegree is inconsistent");
      }
      --found->second;
      if (found->second == 0) {
        const auto& successor_node = phases_.at(successor);
        ready.emplace(successor_node.registration_order, successor);
      }
    }
  }
  if (result.size() != owner.phases.size()) {
    fail(kInvalidGraph, "UVM phase graph contains a cycle");
  }
  return result;
}

}  // namespace fsim::runtime
