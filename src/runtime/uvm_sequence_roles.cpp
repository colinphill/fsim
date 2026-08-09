// SPDX-License-Identifier: Apache-2.0
#include "fsim/runtime/uvm_sequence.hpp"

#include <algorithm>
#include <limits>
#include <ranges>
#include <set>
#include <utility>

namespace fsim::runtime {
namespace {

constexpr std::string_view kInvalidHandle{"FSIM-UVM-SEQ-001"};
constexpr std::string_view kForeignHandle{"FSIM-UVM-SEQ-002"};
constexpr std::string_view kInvalidLifecycle{"FSIM-UVM-SEQ-005"};
constexpr std::string_view kResourceLimit{"FSIM-UVM-SEQ-006"};
constexpr std::string_view kInvalidRole{"FSIM-UVM-SEQ-012"};
constexpr std::string_view kAgentModeType{"uvm_pkg::uvm_active_passive_enum"};

[[noreturn]] void fail(const std::string_view code,
                       const std::string_view message) {
  throw SystemVerilogUvmSequenceError{std::string{code}, std::string{message}};
}

[[nodiscard]] bool
is_analysis_sink(const SystemVerilogUvmSequenceRoleKind kind) noexcept {
  return kind == SystemVerilogUvmSequenceRoleKind::Subscriber ||
         kind == SystemVerilogUvmSequenceRoleKind::Scoreboard;
}

[[nodiscard]] bool
is_run_role(const SystemVerilogUvmSequenceRoleKind kind) noexcept {
  return kind == SystemVerilogUvmSequenceRoleKind::Driver ||
         kind == SystemVerilogUvmSequenceRoleKind::Monitor;
}

} // namespace

SystemVerilogUvmSequenceService::~SystemVerilogUvmSequenceService() noexcept {
  std::set<SystemVerilogUvmRootHandle> roots;
  for (const auto &[slot, selected] : roles_) {
    (void)slot;
    if (selected.live)
      roots.insert(selected.value.root);
  }
  for (const auto root : roots) {
    try {
      teardown_roles(root);
    } catch (...) {
      // Destruction must remain safe if a peer service is already unwinding.
    }
  }
  roles_.clear();
  roles_by_component_.clear();
  live_roles_ = 0;
}

void SystemVerilogUvmSequenceService::set_role_services(
    SystemVerilogUvmTlm1Service &tlm1,
    SystemVerilogUvmConfigDbService &configuration) noexcept {
  tlm1_ = &tlm1;
  configuration_ = &configuration;
}

SystemVerilogUvmSequenceRoleHandle
SystemVerilogUvmSequenceService::role_handle(const std::uint64_t slot) const {
  const auto found = roles_.find(slot);
  if (found == roles_.end() || !found->second.live)
    return {};
  return {owner_, slot, found->second.generation};
}

SystemVerilogUvmSequenceService::Role &SystemVerilogUvmSequenceService::role(
    const SystemVerilogUvmSequenceRoleHandle handle) {
  if (!handle.valid())
    fail(kInvalidHandle, "UVM sequence role handle is empty");
  if (handle.owner_.get() != owner_.get()) {
    fail(kForeignHandle, "UVM sequence role belongs to another simulation");
  }
  const auto found = roles_.find(handle.slot_);
  if (found == roles_.end() || !found->second.live ||
      found->second.generation != handle.generation_) {
    fail(kInvalidHandle, "UVM sequence role handle is stale");
  }
  return found->second;
}

const SystemVerilogUvmSequenceService::Role &
SystemVerilogUvmSequenceService::role(
    const SystemVerilogUvmSequenceRoleHandle handle) const {
  return const_cast<SystemVerilogUvmSequenceService *>(this)->role(handle);
}

SystemVerilogUvmSequenceService::Role *
SystemVerilogUvmSequenceService::role_for_component_impl(
    const SystemVerilogClassHandle component) noexcept {
  const auto found = roles_by_component_.find(component);
  if (found == roles_by_component_.end())
    return nullptr;
  const auto selected = roles_.find(found->second);
  return selected == roles_.end() || !selected->second.live ? nullptr
                                                            : &selected->second;
}

const SystemVerilogUvmSequenceService::Role *
SystemVerilogUvmSequenceService::role_for_component_impl(
    const SystemVerilogClassHandle component) const noexcept {
  return const_cast<SystemVerilogUvmSequenceService *>(this)
      ->role_for_component_impl(component);
}

void SystemVerilogUvmSequenceService::validate_role_hierarchy(
    const SystemVerilogUvmSequenceRoleDescriptor &descriptor) const {
  if (descriptor.component == 0 ||
      !components_->contains(descriptor.component)) {
    fail(kInvalidHandle, "UVM sequence role component is empty or stale");
  }
  if (role_for_component_impl(descriptor.component) != nullptr) {
    fail(kInvalidLifecycle, "UVM component already owns a sequence role");
  }
  const auto component = components_->snapshot(descriptor.component);
  if (descriptor.kind == SystemVerilogUvmSequenceRoleKind::Agent) {
    if (descriptor.agent || descriptor.sequencer ||
        !descriptor.analysis_type.empty() || descriptor.analysis_dispatch) {
      fail(kInvalidRole, "UVM agent role has incompatible bindings");
    }
    if (descriptor.active_config_field.empty()) {
      fail(kInvalidRole, "UVM agent active/passive config field is empty");
    }
    return;
  }

  if (descriptor.agent) {
    const auto &agent = role(descriptor.agent);
    if (agent.value.kind != SystemVerilogUvmSequenceRoleKind::Agent ||
        agent.value.root != component.root) {
      fail(kInvalidRole, "UVM role agent binding has the wrong kind or root");
    }
    auto ancestor = components_->parent(descriptor.component);
    while (ancestor != 0 && ancestor != agent.value.component) {
      ancestor = components_->parent(ancestor);
    }
    if (ancestor != agent.value.component) {
      fail(kInvalidRole,
           "UVM driver or monitor is outside its agent hierarchy");
    }
  } else if (descriptor.kind == SystemVerilogUvmSequenceRoleKind::Driver ||
             descriptor.kind == SystemVerilogUvmSequenceRoleKind::Monitor) {
    fail(kInvalidRole, "UVM driver or monitor requires an owning agent");
  }

  if (descriptor.kind == SystemVerilogUvmSequenceRoleKind::Driver) {
    if (!descriptor.sequencer || !contains(descriptor.sequencer)) {
      fail(kInvalidHandle, "UVM driver sequencer binding is empty or stale");
    }
    if (snapshot(descriptor.sequencer).root != component.root ||
        !descriptor.analysis_type.empty() || descriptor.analysis_dispatch) {
      fail(kInvalidRole, "UVM driver bindings are incompatible or cross-root");
    }
  } else if (descriptor.kind == SystemVerilogUvmSequenceRoleKind::Monitor) {
    if (descriptor.sequencer || descriptor.analysis_type.empty() ||
        descriptor.analysis_dispatch) {
      fail(kInvalidRole, "UVM monitor analysis bindings are malformed");
    }
  } else if (is_analysis_sink(descriptor.kind)) {
    if (descriptor.sequencer || descriptor.analysis_type.empty() ||
        !descriptor.analysis_dispatch) {
      fail(kInvalidRole, "UVM subscriber or scoreboard binding is malformed");
    }
  }
  if (descriptor.automatic_objection && !is_run_role(descriptor.kind)) {
    fail(kInvalidRole, "automatic objections require a driver or monitor role");
  }
}

SystemVerilogUvmSequenceRoleHandle
SystemVerilogUvmSequenceService::register_role(
    SystemVerilogUvmSequenceRoleDescriptor descriptor) {
  if (tlm1_ == nullptr || configuration_ == nullptr || phases_ == nullptr ||
      objections_ == nullptr) {
    fail(kInvalidLifecycle, "UVM sequence role integration services are unset");
  }
  validate_role_hierarchy(descriptor);
  const auto component = components_->snapshot(descriptor.component);
  const auto root_roles = std::ranges::count_if(roles_, [&](const auto &entry) {
    return entry.second.live && entry.second.value.root == component.root;
  });
  if (live_roles_ >= limits_.maximum_roles ||
      static_cast<std::size_t>(root_roles) >= limits_.maximum_roles_per_root ||
      mutations_ >= limits_.maximum_mutations ||
      next_role_slot_ == std::numeric_limits<std::uint64_t>::max() ||
      next_role_order_ == std::numeric_limits<std::uint64_t>::max() ||
      role_events_.size() >= limits_.maximum_role_events ||
      descriptor.active_config_field.size() > limits_.maximum_name_bytes ||
      descriptor.analysis_type.size() > limits_.maximum_profile_bytes) {
    fail(kResourceLimit, "UVM sequence role registration ceiling exceeded");
  }

  const auto slot = next_role_slot_;
  const auto handle = SystemVerilogUvmSequenceRoleHandle{owner_, slot, 1};
  SystemVerilogUvmTlm1EndpointHandle endpoint;
  try {
    if (descriptor.kind == SystemVerilogUvmSequenceRoleKind::Monitor) {
      SystemVerilogUvmTlm1EndpointDescriptor analysis;
      analysis.kind = SystemVerilogUvmTlm1EndpointKind::Port;
      analysis.profile = {SystemVerilogUvmTlm1Interface::Analysis,
                          SystemVerilogUvmTlm1Direction::Forward,
                          descriptor.analysis_type,
                          {}};
      analysis.component = descriptor.component;
      analysis.name = "analysis_port";
      analysis.minimum_connections = 0;
      analysis.maximum_connections =
          limits_.maximum_analysis_connections_per_monitor;
      endpoint = tlm1_->register_endpoint(std::move(analysis));
    } else if (is_analysis_sink(descriptor.kind)) {
      const auto dispatch = descriptor.analysis_dispatch;
      endpoint = tlm1_->register_analysis_implementation(
          descriptor.component,
          descriptor.kind == SystemVerilogUvmSequenceRoleKind::Subscriber
              ? "analysis_imp"
              : "scoreboard_imp",
          descriptor.analysis_type,
          [this, slot, dispatch](SystemVerilogUvmTlm1Payload payload) {
            const auto selected = roles_.find(slot);
            if (selected == roles_.end() || !selected->second.live)
              return;
            dispatch(std::move(payload));
          });
    }

    Role selected;
    selected.value.handle = handle;
    selected.value.component = descriptor.component;
    selected.value.root = component.root;
    selected.value.full_name = component.full_name;
    selected.value.kind = descriptor.kind;
    selected.value.agent_mode = descriptor.default_agent_mode;
    selected.value.agent = descriptor.agent;
    selected.value.sequencer = descriptor.sequencer;
    selected.value.analysis_endpoint = endpoint;
    selected.value.analysis_type = std::move(descriptor.analysis_type);
    selected.value.registration_order = next_role_order_;
    selected.value.automatic_objection = descriptor.automatic_objection;
    selected.function_dispatch = std::move(descriptor.function_dispatch);
    selected.task_dispatch = std::move(descriptor.task_dispatch);
    selected.active_config_field = std::move(descriptor.active_config_field);
    selected.objection_source = objections_->bind_source(descriptor.component);
    roles_.emplace(slot, std::move(selected));
    roles_by_component_.emplace(descriptor.component, slot);
  } catch (...) {
    if (endpoint && tlm1_->contains(endpoint)) {
      try {
        tlm1_->release_endpoint(endpoint);
      } catch (...) {
      }
    }
    throw;
  }
  ++next_role_slot_;
  ++next_role_order_;
  ++live_roles_;
  ++mutations_;
  append_role_event(roles_.at(slot),
                    SystemVerilogUvmSequenceRoleEventKind::Registered);
  return handle;
}

void SystemVerilogUvmSequenceService::append_role_event(
    const Role &selected, const SystemVerilogUvmSequenceRoleEventKind kind,
    const SystemVerilogUvmPhaseHandle phase,
    const SystemVerilogUvmPhaseProcessHandle process,
    const SystemVerilogUvmTlm1EndpointHandle endpoint) {
  if (role_events_.size() >= limits_.maximum_role_events ||
      next_role_event_order_ == std::numeric_limits<std::uint64_t>::max()) {
    fail(kResourceLimit, "UVM sequence role event ceiling exceeded");
  }
  role_events_.push_back({next_role_event_order_++, selected.value.handle,
                          selected.value.root, kind, phase, process, endpoint});
}

void SystemVerilogUvmSequenceService::append_role_failure(
    const Role &selected, const SystemVerilogUvmPhaseHandle phase,
    std::string message) {
  if (role_failures_.size() >= limits_.maximum_role_failures) {
    fail(kResourceLimit, "UVM sequence role failure ceiling exceeded");
  }
  role_failures_.push_back({std::string{kInvalidRole}, selected.value.handle,
                            phase, std::move(message)});
}

void SystemVerilogUvmSequenceService::connect_analysis(
    const SystemVerilogUvmSequenceRoleHandle monitor_handle,
    const SystemVerilogUvmSequenceRoleHandle subscriber_handle) {
  Role &monitor = role(monitor_handle);
  Role &subscriber = role(subscriber_handle);
  if (monitor.value.kind != SystemVerilogUvmSequenceRoleKind::Monitor ||
      !is_analysis_sink(subscriber.value.kind) ||
      monitor.value.root != subscriber.value.root ||
      monitor.value.analysis_type != subscriber.value.analysis_type) {
    fail(kInvalidRole, "UVM monitor analysis connection is incompatible");
  }
  if (std::ranges::find(monitor.analysis_targets, subscriber_handle) !=
      monitor.analysis_targets.end()) {
    fail(kInvalidRole, "duplicate UVM monitor analysis connection");
  }
  if (monitor.analysis_targets.size() >=
          limits_.maximum_analysis_connections_per_monitor ||
      mutations_ >= limits_.maximum_mutations ||
      role_events_.size() >= limits_.maximum_role_events) {
    fail(kResourceLimit, "UVM monitor analysis connection ceiling exceeded");
  }
  monitor.analysis_targets.reserve(monitor.analysis_targets.size() + 1U);
  tlm1_->connect(monitor.value.analysis_endpoint,
                 subscriber.value.analysis_endpoint);
  monitor.analysis_targets.push_back(subscriber_handle);
  tlm1_->resolve_all();
  append_role_event(monitor,
                    SystemVerilogUvmSequenceRoleEventKind::AnalysisConnection,
                    {}, {}, subscriber.value.analysis_endpoint);
}

void SystemVerilogUvmSequenceService::resolve_agent_mode(Role &selected) {
  if (selected.value.kind != SystemVerilogUvmSequenceRoleKind::Agent)
    return;
  const auto component = components_->snapshot(selected.value.component);
  SystemVerilogUvmConfigContext context{
      std::string{components_->root_identity(selected.value.root)} + ":" +
          component.full_name,
      component.depth};
  const auto configured =
      configuration_->get(context, {}, selected.active_config_field,
                          kAgentModeType, selected.value.full_name);
  if (!configured)
    return;
  const auto packed = std::get_if<PackedLogic4>(&*configured);
  if (packed == nullptr || packed->width() == 0 ||
      packed->low_word().bval != 0 || packed->low_word().aval > 1) {
    fail(kInvalidRole, "UVM agent active/passive configuration is invalid");
  }
  selected.value.agent_mode = packed->low_word().aval == 0
                                  ? SystemVerilogUvmAgentMode::Passive
                                  : SystemVerilogUvmAgentMode::Active;
}

void SystemVerilogUvmSequenceService::dispatch_role_function(
    const SystemVerilogClassHandle component,
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmPhaseCallbackKind callback) {
  Role *const selected = role_for_component_impl(component);
  if (selected == nullptr)
    return;
  if (!components_->contains(component) || !phases_->contains(phase_handle)) {
    fail(kInvalidHandle, "UVM role phase dispatch uses stale state");
  }
  const auto phase = phases_->snapshot(phase_handle);
  if (std::ranges::find(phase.roots, selected->value.root) ==
      phase.roots.end()) {
    fail(kInvalidRole, "UVM role phase does not participate in its root");
  }
  if (role_dispatches_ >= limits_.maximum_role_dispatches ||
      mutations_ >= limits_.maximum_mutations ||
      role_events_.size() >= limits_.maximum_role_events) {
    fail(kResourceLimit, "UVM sequence role dispatch ceiling exceeded");
  }

  auto next_state = selected->value.state;
  auto next_mode = selected->value.agent_mode;
  if (callback == SystemVerilogUvmPhaseCallbackKind::Execute) {
    if (phase.kind == SystemVerilogUvmPhaseKind::Build) {
      if (selected->value.state !=
          SystemVerilogUvmSequenceRoleState::Constructed) {
        fail(kInvalidLifecycle, "UVM sequence role build is out of order");
      }
      if (selected->value.kind == SystemVerilogUvmSequenceRoleKind::Agent) {
        resolve_agent_mode(*selected);
        next_mode = selected->value.agent_mode;
      } else if (selected->value.agent) {
        const auto &agent = role(selected->value.agent);
        if (selected->value.kind == SystemVerilogUvmSequenceRoleKind::Driver &&
            agent.value.agent_mode == SystemVerilogUvmAgentMode::Passive) {
          fail(kInvalidRole, "passive UVM agent cannot own a driver");
        }
        if (agent.value.state != SystemVerilogUvmSequenceRoleState::Built) {
          fail(kInvalidLifecycle, "UVM sequence role built before its agent");
        }
      }
      next_state = SystemVerilogUvmSequenceRoleState::Built;
    } else if (phase.kind == SystemVerilogUvmPhaseKind::Connect) {
      if (selected->value.state != SystemVerilogUvmSequenceRoleState::Built) {
        fail(kInvalidLifecycle, "UVM sequence role connect is out of order");
      }
      if (selected->value.kind == SystemVerilogUvmSequenceRoleKind::Driver &&
          (!contains(selected->value.sequencer) ||
           snapshot(selected->value.sequencer).root != selected->value.root)) {
        fail(kInvalidRole, "UVM driver lost its sequencer binding");
      }
      next_state = SystemVerilogUvmSequenceRoleState::Connected;
    } else if (selected->value.state ==
               SystemVerilogUvmSequenceRoleState::Constructed) {
      fail(kInvalidLifecycle, "UVM sequence role phase precedes build");
    }
  }

  try {
    if (selected->function_dispatch) {
      selected->function_dispatch(selected->value.handle, phase_handle,
                                  callback);
    }
  } catch (const std::exception &error) {
    append_role_failure(*selected, phase_handle, error.what());
    fail(kInvalidRole, "UVM sequence role function dispatch failed");
  } catch (...) {
    append_role_failure(
        *selected, phase_handle,
        "UVM sequence role function dispatch threw a non-standard exception");
    fail(kInvalidRole, "UVM sequence role function dispatch failed");
  }
  selected->value.state = next_state;
  selected->value.agent_mode = next_mode;
  selected->value.phase = phase_handle;
  ++selected->value.function_dispatches;
  ++role_dispatches_;
  ++mutations_;
  append_role_event(*selected,
                    SystemVerilogUvmSequenceRoleEventKind::PhaseDispatch,
                    phase_handle);
}

std::optional<SystemVerilogUvmTaskPhaseStatus>
SystemVerilogUvmSequenceService::dispatch_role_task(
    const SystemVerilogClassHandle component,
    const SystemVerilogUvmPhaseHandle phase_handle,
    const SystemVerilogUvmPhaseProcessHandle process_handle) {
  Role *const selected = role_for_component_impl(component);
  if (selected == nullptr)
    return std::nullopt;
  const auto phase = phases_->snapshot(phase_handle);
  if (phase.kind != SystemVerilogUvmPhaseKind::Run ||
      !is_run_role(selected->value.kind)) {
    return std::nullopt;
  }
  if (selected->value.state != SystemVerilogUvmSequenceRoleState::Connected &&
      selected->value.state != SystemVerilogUvmSequenceRoleState::Stopped) {
    fail(kInvalidLifecycle, "UVM sequence role run is out of order");
  }
  const auto process = phases_->process_snapshot(process_handle);
  if (process.component != component || process.phase != phase_handle ||
      process.root != selected->value.root) {
    fail(kInvalidRole, "UVM sequence role process context mismatches");
  }
  const auto required_events = selected->value.automatic_objection ? 3U : 1U;
  if (role_dispatches_ >= limits_.maximum_role_dispatches ||
      mutations_ >= limits_.maximum_mutations ||
      required_events > limits_.maximum_role_events ||
      role_events_.size() > limits_.maximum_role_events - required_events) {
    fail(kResourceLimit, "UVM sequence role dispatch ceiling exceeded");
  }
  if (selected->value.agent) {
    const auto &agent = role(selected->value.agent);
    if (selected->value.kind == SystemVerilogUvmSequenceRoleKind::Driver &&
        agent.value.agent_mode == SystemVerilogUvmAgentMode::Passive) {
      fail(kInvalidRole, "passive UVM agent cannot dispatch a driver");
    }
  }

  if (selected->value.automatic_objection) {
    const auto raised = objections_->raise(
        phase_handle, selected->objection_source, "sequence-role-run", 1);
    if (!raised.success()) {
      fail(kInvalidRole, "UVM sequence role automatic objection failed");
    }
    selected->value.objection_raised = true;
    append_role_event(*selected,
                      SystemVerilogUvmSequenceRoleEventKind::ObjectionRaised,
                      phase_handle, process_handle);
  }

  SystemVerilogUvmTaskPhaseStatus status{
      SystemVerilogUvmTaskPhaseStatus::Completed};
  try {
    if (selected->task_dispatch) {
      status = selected->task_dispatch(selected->value.handle, phase_handle,
                                       process_handle);
    }
  } catch (const std::exception &error) {
    if (selected->value.objection_raised)
      drop_role_objection(*selected);
    append_role_failure(*selected, phase_handle, error.what());
    fail(kInvalidRole, "UVM sequence role task dispatch failed");
  } catch (...) {
    if (selected->value.objection_raised)
      drop_role_objection(*selected);
    append_role_failure(
        *selected, phase_handle,
        "UVM sequence role task dispatch threw a non-standard exception");
    fail(kInvalidRole, "UVM sequence role task dispatch failed");
  }

  selected->value.phase = phase_handle;
  selected->value.process = process_handle;
  selected->value.state = status == SystemVerilogUvmTaskPhaseStatus::Completed
                              ? SystemVerilogUvmSequenceRoleState::Stopped
                              : SystemVerilogUvmSequenceRoleState::Running;
  ++selected->value.task_dispatches;
  ++role_dispatches_;
  ++mutations_;
  append_role_event(*selected,
                    SystemVerilogUvmSequenceRoleEventKind::PhaseDispatch,
                    phase_handle, process_handle);
  if (status == SystemVerilogUvmTaskPhaseStatus::Completed &&
      selected->value.objection_raised) {
    drop_role_objection(*selected);
  }
  return status;
}

void SystemVerilogUvmSequenceService::drop_role_objection(Role &selected) {
  if (!selected.value.objection_raised || !selected.value.phase)
    return;
  if (role_events_.size() >= limits_.maximum_role_events) {
    fail(kResourceLimit, "UVM sequence role event ceiling exceeded");
  }
  const auto dropped = objections_->drop(
      *selected.value.phase, selected.objection_source, "sequence-role-run", 1);
  if (!dropped.success()) {
    fail(kInvalidRole, "UVM sequence role automatic objection drop failed");
  }
  selected.value.objection_raised = false;
  append_role_event(
      selected, SystemVerilogUvmSequenceRoleEventKind::ObjectionDropped,
      *selected.value.phase,
      selected.value.process.value_or(SystemVerilogUvmPhaseProcessHandle{}));
}

void SystemVerilogUvmSequenceService::complete_role_task(
    const SystemVerilogUvmSequenceRoleHandle role_handle,
    const SystemVerilogUvmPhaseProcessHandle process_handle) {
  Role &selected = role(role_handle);
  if (selected.value.state != SystemVerilogUvmSequenceRoleState::Running ||
      !selected.value.process || *selected.value.process != process_handle) {
    fail(kInvalidLifecycle, "UVM sequence role task completion mismatches");
  }
  drop_role_objection(selected);
  selected.value.state = SystemVerilogUvmSequenceRoleState::Stopped;
  ++mutations_;
}

SystemVerilogUvmSequenceAcquireResult
SystemVerilogUvmSequenceService::driver_get_next_item(
    const SystemVerilogUvmSequenceRoleHandle driver_handle,
    SystemVerilogUvmSequenceHandshakeContext context) {
  const Role &driver = role(driver_handle);
  if (driver.value.kind != SystemVerilogUvmSequenceRoleKind::Driver ||
      (driver.value.state != SystemVerilogUvmSequenceRoleState::Connected &&
       driver.value.state != SystemVerilogUvmSequenceRoleState::Running &&
       driver.value.state != SystemVerilogUvmSequenceRoleState::Stopped)) {
    fail(kInvalidRole, "UVM driver handshake uses an invalid role state");
  }
  if (!context.phase && driver.value.phase)
    context.phase = *driver.value.phase;
  if (!context.process && driver.value.process) {
    context.process = *driver.value.process;
  }
  return get_next_item(driver.value.sequencer, context);
}

void SystemVerilogUvmSequenceService::driver_item_done(
    const SystemVerilogUvmSequenceRoleHandle driver_handle,
    const SystemVerilogUvmSequenceTransactionHandle transaction_handle,
    const SystemVerilogUvmSequenceItemHandle response) {
  const Role &driver = role(driver_handle);
  if (driver.value.kind != SystemVerilogUvmSequenceRoleKind::Driver ||
      transaction_snapshot(transaction_handle).sequencer !=
          driver.value.sequencer) {
    fail(kInvalidRole, "UVM driver completed another sequencer transaction");
  }
  item_done(transaction_handle, response);
}

SystemVerilogUvmTlm1AnalysisResult
SystemVerilogUvmSequenceService::publish_monitor(
    const SystemVerilogUvmSequenceRoleHandle monitor_handle,
    SystemVerilogUvmTlm1Payload payload) {
  Role &monitor = role(monitor_handle);
  if (monitor.value.kind != SystemVerilogUvmSequenceRoleKind::Monitor ||
      (monitor.value.state != SystemVerilogUvmSequenceRoleState::Connected &&
       monitor.value.state != SystemVerilogUvmSequenceRoleState::Running &&
       monitor.value.state != SystemVerilogUvmSequenceRoleState::Stopped)) {
    fail(kInvalidRole, "UVM monitor publication uses an invalid role state");
  }
  if (role_publications_ >= limits_.maximum_role_publications ||
      mutations_ >= limits_.maximum_mutations ||
      role_events_.size() >= limits_.maximum_role_events) {
    fail(kResourceLimit, "UVM sequence role publication ceiling exceeded");
  }
  if (payload.nominal_type.empty())
    payload.nominal_type = monitor.value.analysis_type;
  const auto result = tlm1_->write_analysis(monitor.value.analysis_endpoint,
                                            std::move(payload));
  ++monitor.value.publications;
  ++role_publications_;
  ++mutations_;
  append_role_event(
      monitor, SystemVerilogUvmSequenceRoleEventKind::AnalysisPublication,
      monitor.value.phase.value_or(SystemVerilogUvmPhaseHandle{}),
      monitor.value.process.value_or(SystemVerilogUvmPhaseProcessHandle{}),
      monitor.value.analysis_endpoint);
  return result;
}

std::optional<SystemVerilogUvmSequenceRoleSnapshot>
SystemVerilogUvmSequenceService::role_for_component(
    const SystemVerilogClassHandle component) const {
  const Role *const selected = role_for_component_impl(component);
  return selected == nullptr
             ? std::nullopt
             : std::optional<SystemVerilogUvmSequenceRoleSnapshot>{
                   selected->value};
}

SystemVerilogUvmSequenceRoleSnapshot
SystemVerilogUvmSequenceService::role_snapshot(
    const SystemVerilogUvmSequenceRoleHandle role_handle) const {
  return role(role_handle).value;
}

std::vector<SystemVerilogUvmSequenceRoleHandle>
SystemVerilogUvmSequenceService::roles(
    const std::optional<SystemVerilogUvmRootHandle> root) const {
  std::vector<SystemVerilogUvmSequenceRoleHandle> result;
  result.reserve(live_roles_);
  for (const auto &[slot, selected] : roles_) {
    if (selected.live && (!root || selected.value.root == *root)) {
      result.push_back(role_handle(slot));
    }
  }
  return result;
}

void SystemVerilogUvmSequenceService::release_role_impl(Role &selected,
                                                        const bool teardown) {
  if (!teardown &&
      (selected.value.state == SystemVerilogUvmSequenceRoleState::Running ||
       selected.value.objection_raised)) {
    fail(kInvalidLifecycle, "active UVM sequence role cannot be released");
  }
  if (!teardown && std::ranges::any_of(roles_, [&](const auto &entry) {
        return entry.second.live &&
               entry.second.value.agent == selected.value.handle;
      })) {
    fail(kInvalidLifecycle, "UVM agent role still owns child roles");
  }
  if (selected.value.analysis_endpoint && tlm1_ != nullptr &&
      tlm1_->contains(selected.value.analysis_endpoint)) {
    tlm1_->release_endpoint(selected.value.analysis_endpoint);
  }
  for (auto &[slot, monitor] : roles_) {
    (void)slot;
    if (!monitor.live)
      continue;
    std::erase(monitor.analysis_targets, selected.value.handle);
  }
  if (role_events_.size() < limits_.maximum_role_events &&
      next_role_event_order_ != std::numeric_limits<std::uint64_t>::max()) {
    append_role_event(
        selected, teardown ? SystemVerilogUvmSequenceRoleEventKind::Teardown
                           : SystemVerilogUvmSequenceRoleEventKind::Released);
  }
  roles_by_component_.erase(selected.value.component);
  selected.live = false;
  ++selected.generation;
  --live_roles_;
  if (mutations_ < limits_.maximum_mutations)
    ++mutations_;
}

void SystemVerilogUvmSequenceService::release_role(
    const SystemVerilogUvmSequenceRoleHandle role_handle) {
  Role &selected = role(role_handle);
  release_role_impl(selected, false);
}

void SystemVerilogUvmSequenceService::teardown_roles(
    const SystemVerilogUvmRootHandle root) {
  std::vector<std::pair<std::size_t, SystemVerilogUvmSequenceRoleHandle>> order;
  for (const auto &role_handle : roles(root)) {
    const auto &selected = role(role_handle);
    const auto depth =
        components_->contains(selected.value.component)
            ? components_->snapshot(selected.value.component).depth
            : static_cast<std::size_t>(
                  std::ranges::count(selected.value.full_name, '.'));
    order.emplace_back(depth, role_handle);
  }
  std::ranges::sort(order, [&](const auto &left, const auto &right) {
    if (left.first != right.first)
      return left.first > right.first;
    return role(left.second).value.registration_order <
           role(right.second).value.registration_order;
  });
  if (objections_ != nullptr)
    objections_->cancel_root(root);
  for (const auto &[depth, role_handle] : order) {
    (void)depth;
    Role &selected = role(role_handle);
    selected.value.objection_raised = false;
    if (selected.value.kind == SystemVerilogUvmSequenceRoleKind::Driver &&
        contains(selected.value.sequencer)) {
      for (const auto &transaction_handle :
           transactions(selected.value.sequencer)) {
        const auto snapshot = transaction_snapshot(transaction_handle);
        if (snapshot.state !=
                SystemVerilogUvmSequenceTransactionState::Completed &&
            snapshot.state !=
                SystemVerilogUvmSequenceTransactionState::Cancelled) {
          cancel_transaction(
              transaction_handle,
              SystemVerilogUvmSequenceCancellationReason::PhaseEnded);
        }
      }
    }
    release_role_impl(selected, true);
  }
}

} // namespace fsim::runtime
