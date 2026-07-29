// SPDX-License-Identifier: Apache-2.0
#include "api_internal.hpp"

namespace fsim::api::detail {

std::mutex registry_mutex;
std::unordered_map<fsim_session_t, std::shared_ptr<Session>> sessions;
std::atomic_uint64_t next_session{1};

CallbackGuard::CallbackGuard(Session& value) : session(value) {
  session.callback_depth.fetch_add(1, std::memory_order_acq_rel);
}

CallbackGuard::~CallbackGuard() {
  session.callback_depth.fetch_sub(1, std::memory_order_acq_rel);
}

std::shared_ptr<Session> find_session(const fsim_session_t handle) {
  std::lock_guard lock(registry_mutex);
  const auto iterator = sessions.find(handle);
  return iterator == sessions.end() ? nullptr : iterator->second;
}

fsim_string_view_t view(const std::string_view value) noexcept {
  return {value.data(), value.size()};
}

fsim_severity_t convert_severity(
    const fsim::diagnostic::Severity severity) noexcept {
  switch (severity) {
    case fsim::diagnostic::Severity::note:
      return FSIM_SEVERITY_NOTE;
    case fsim::diagnostic::Severity::warning:
      return FSIM_SEVERITY_WARNING;
    case fsim::diagnostic::Severity::error:
      return FSIM_SEVERITY_ERROR;
    case fsim::diagnostic::Severity::fatal:
      return FSIM_SEVERITY_FATAL;
  }
  return FSIM_SEVERITY_ERROR;
}

fsim_safe_point_kind_t convert_safe_point_kind(
    const fsim::runtime::simir::ExecutionPointKind kind) noexcept {
  using Kind = fsim::runtime::simir::ExecutionPointKind;
  switch (kind) {
    case Kind::statement:
      return FSIM_SAFE_POINT_STATEMENT;
    case Kind::call:
      return FSIM_SAFE_POINT_CALL;
    case Kind::wait:
      return FSIM_SAFE_POINT_WAIT;
    case Kind::assertion:
      return FSIM_SAFE_POINT_ASSERTION;
    case Kind::process_entry:
      return FSIM_SAFE_POINT_PROCESS_ENTRY;
    case Kind::process_suspend:
      return FSIM_SAFE_POINT_PROCESS_SUSPEND;
  }
  return FSIM_SAFE_POINT_SCHEDULER;
}

fsim_scheduler_phase_t convert_scheduler_phase(
    const fsim::runtime::SchedulerPhase phase) noexcept {
  using Phase = fsim::runtime::SchedulerPhase;
  switch (phase) {
    case Phase::active:
      return FSIM_SCHEDULER_PHASE_ACTIVE;
    case Phase::inactive:
      return FSIM_SCHEDULER_PHASE_INACTIVE;
    case Phase::update:
      return FSIM_SCHEDULER_PHASE_UPDATE;
    case Phase::postponed:
      return FSIM_SCHEDULER_PHASE_POSTPONED;
  }
  return FSIM_SCHEDULER_PHASE_UNKNOWN;
}

bool valid_struct_header(
    const std::uint32_t struct_size,
    const std::uint32_t api_version,
    const std::size_t required_size) noexcept {
  return struct_size >= required_size && api_version == FSIM_API_VERSION;
}

bool struct_contains(
    const std::uint32_t struct_size,
    const std::size_t offset,
    const std::size_t field_size) noexcept {
  return offset <= struct_size && field_size <= struct_size - offset;
}

#define FSIM_STRUCT_CONTAINS(struct_size, type, member) \
  struct_contains(                                      \
      (struct_size), offsetof(type, member),            \
      sizeof(((type*)nullptr)->member))

const fsim::runtime::simir::SourceLocation* process_source(
    const fsim::runtime::simir::Process& process) noexcept {
  const fsim::runtime::simir::SourceLocation* first = nullptr;
  for (const auto& operation : process.operations) {
    const auto* point =
        std::get_if<fsim::runtime::simir::DebugPoint>(&operation);
    if (point == nullptr) {
      continue;
    }
    if (first == nullptr) {
      first = &point->source;
    }
    if (point->kind
        == fsim::runtime::simir::DebugPointKind::process_entry) {
      return &point->source;
    }
  }
  return first;
}

std::optional<fsim::runtime::simir::SignalId> output_signal(
    const fsim::runtime::simir::Operation& operation) {
  using namespace fsim::runtime::simir;
  if (const auto* value = std::get_if<WriteBlocking>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteUpdate>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteAfter>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteInertial>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteProjected>(&operation)) {
    return value->signal;
  }
  if (const auto* value =
          std::get_if<WriteProjectedWaveform>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteBlockingSlice>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteUpdateSlice>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteAfterSlice>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteInertialSlice>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteProjectedSlice>(&operation)) {
    return value->signal;
  }
  if (const auto* value =
          std::get_if<WriteProjectedWaveformSlice>(&operation)) {
    return value->signal;
  }
  return std::nullopt;
}

fsim_status_t with_session(
    const fsim_session_t handle,
    const std::function<fsim_status_t(Session&)>& function) noexcept {
  try {
    auto session = find_session(handle);
    if (!session) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    std::lock_guard lock(session->mutex);
    if (session->destroyed.load(std::memory_order_acquire)) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    return function(*session);
  } catch (const std::bad_alloc&) {
    return FSIM_STATUS_INTERNAL_ERROR;
  } catch (...) {
    return FSIM_STATUS_INTERNAL_ERROR;
  }
}

bool ready(Session& session, const std::string_view operation) {
  if (session.simulation) {
    return true;
  }
  session.diagnostics.error(
      "FSIM-API-0003",
      std::string(operation) + " requires a successfully built design");
  return false;
}

void advance_design_generation(Session& session) noexcept {
  ++session.design_generation;
  if (session.design_generation == 0) {
    ++session.design_generation;
  }
}

fsim_object_t object_handle(
    const Session& session,
    const std::uint32_t payload) noexcept {
  return (static_cast<fsim_object_t>(session.design_generation) << 32U)
      | payload;
}

bool current_object(
    const Session& session,
    const fsim_object_t object) noexcept {
  return static_cast<std::uint32_t>(object >> 32U)
      == session.design_generation;
}

std::uint32_t object_payload(const fsim_object_t object) noexcept {
  return static_cast<std::uint32_t>(object & UINT32_MAX);
}

fsim_object_t root_handle(const Session& session) noexcept {
  return object_handle(session, kRootPayload);
}

fsim_object_t signal_handle(
    const Session& session,
    const fsim::runtime::simir::SignalId signal) {
  if (signal > kObjectIndexMask) {
    return FSIM_INVALID_OBJECT;
  }
  return object_handle(session, kSignalPayload | signal);
}

std::optional<fsim::runtime::simir::SignalId> object_signal(
    const Session& session,
    const fsim_object_t object) {
  if (!session.simulation || !current_object(session, object)) {
    return std::nullopt;
  }
  const auto payload = object_payload(object);
  if ((payload & ~kObjectIndexMask) != kSignalPayload) {
    return std::nullopt;
  }
  const auto index = payload & kObjectIndexMask;
  if (index >= session.simulation->design().signals().size()
      || index > std::numeric_limits<fsim::runtime::simir::SignalId>::max()) {
    return std::nullopt;
  }
  return static_cast<fsim::runtime::simir::SignalId>(index);
}

fsim_object_t process_handle(const Session& session, const std::size_t process) {
  if (process > kObjectIndexMask) {
    return FSIM_INVALID_OBJECT;
  }
  return object_handle(
      session, kProcessPayload | static_cast<std::uint32_t>(process));
}

std::optional<std::size_t> object_process(
    const Session& session,
    const fsim_object_t object) {
  if (!session.simulation) {
    return std::nullopt;
  }
  if (!current_object(session, object)) {
    return std::nullopt;
  }
  const auto payload = object_payload(object);
  if ((payload & ~kObjectIndexMask) != kProcessPayload) {
    return std::nullopt;
  }
  const auto index = payload & kObjectIndexMask;
  if (index >= session.simulation->design().processes().size()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(index);
}

fsim_object_t variable_handle(
    const Session& session, const std::size_t variable) {
  if (variable > kObjectIndexMask) {
    return FSIM_INVALID_OBJECT;
  }
  return object_handle(
      session,
      kVariablePayload | static_cast<std::uint32_t>(variable));
}

std::optional<std::size_t> object_variable(
    const Session& session,
    const fsim_object_t object) {
  if (!session.simulation || !current_object(session, object)) {
    return std::nullopt;
  }
  const auto payload = object_payload(object);
  if ((payload & ~kObjectIndexMask) != kVariablePayload) {
    return std::nullopt;
  }
  const auto index =
      static_cast<std::size_t>(payload & kObjectIndexMask);
  if (index >= session.variables.size()) {
    return std::nullopt;
  }
  return index;
}

fsim_object_t scope_handle(
    const Session& session, const std::size_t scope) {
  if (scope > kScopeIndexMask - kScopePayloadBase) {
    return FSIM_INVALID_OBJECT;
  }
  return object_handle(
      session,
      kScopePayloadBase + static_cast<std::uint32_t>(scope));
}

std::optional<std::size_t> object_scope(
    const Session& session,
    const fsim_object_t object) {
  if (!session.simulation || !current_object(session, object)) {
    return std::nullopt;
  }
  const auto payload = object_payload(object);
  if ((payload & ~kScopeIndexMask) != 0
      || payload < kScopePayloadBase) {
    return std::nullopt;
  }
  const auto index =
      static_cast<std::size_t>(payload - kScopePayloadBase);
  if (index >= session.scopes.size()) {
    return std::nullopt;
  }
  return index;
}

fsim_object_t driver_handle(
    const Session& session, const std::size_t driver) {
  if (driver > kDriverIndexMask) {
    return FSIM_INVALID_OBJECT;
  }
  return object_handle(
      session,
      kDriverPayload | static_cast<std::uint32_t>(driver));
}

std::optional<std::size_t> object_driver(
    const Session& session,
    const fsim_object_t object) {
  if (!session.simulation || !current_object(session, object)) {
    return std::nullopt;
  }
  const auto payload = object_payload(object);
  if ((payload & ~kDriverIndexMask) != kDriverPayload) {
    return std::nullopt;
  }
  const auto index =
      static_cast<std::size_t>(payload & kDriverIndexMask);
  if (index >= session.drivers.size()) {
    return std::nullopt;
  }
  return index;
}

std::optional<std::size_t> ensure_scope(
    Session& session,
    const std::size_t process,
    const std::optional<std::size_t> parent_scope,
    const std::string& full_name,
    const fsim::runtime::simir::SourceLocation& source) {
  for (std::size_t index = 0; index < session.scopes.size(); ++index) {
    const auto& candidate = session.scopes[index];
    if (candidate.kind == ScopeObjectKind::lexical
        && candidate.process == process
        && candidate.full_name == full_name) {
      return index;
    }
  }
  if (session.scopes.size()
      > kScopeIndexMask - kScopePayloadBase) {
    return std::nullopt;
  }
  session.scopes.push_back(
      ScopeObject{
          ScopeObjectKind::lexical,
          process,
          parent_scope,
          full_name,
          "scope",
          source});
  return session.scopes.size() - 1;
}

bool path_is_within(
    const std::string_view path,
    const std::string_view scope) noexcept {
  return path.size() > scope.size()
      && path.starts_with(scope)
      && path[scope.size()] == '.';
}

std::optional<std::size_t> owning_design_scope(
    const Session& session, const std::string_view path) {
  std::optional<std::size_t> owner;
  std::size_t owner_length = 0;
  for (std::size_t index = 0; index < session.scopes.size(); ++index) {
    const auto& scope = session.scopes[index];
    if (scope.kind != ScopeObjectKind::lexical
        && (path == scope.full_name
            || path_is_within(path, scope.full_name))
        && scope.full_name.size() > owner_length) {
      owner = index;
      owner_length = scope.full_name.size();
    }
  }
  return owner;
}

std::optional<std::size_t> append_design_scope(
    Session& session,
    const ScopeObjectKind kind,
    const std::optional<std::size_t> parent_scope,
    std::string full_name,
    std::string type_name,
    const std::string& source) {
  for (std::size_t index = 0; index < session.scopes.size(); ++index) {
    const auto& scope = session.scopes[index];
    if (scope.kind != ScopeObjectKind::lexical
        && scope.full_name == full_name) {
      return index;
    }
  }
  if (session.scopes.size()
      > kScopeIndexMask - kScopePayloadBase) {
    return std::nullopt;
  }
  session.scopes.push_back(ScopeObject{
      kind,
      std::nullopt,
      parent_scope,
      std::move(full_name),
      std::move(type_name),
      fsim::runtime::simir::SourceLocation{source, 1, 1}});
  return session.scopes.size() - 1;
}

bool rebuild_debug_objects(Session& session) {
  session.scopes.clear();
  session.process_names.clear();
  session.variables.clear();
  session.drivers.clear();
  if (!session.simulation) {
    return true;
  }
  const auto& design = session.simulation->design();
  for (const auto& specialization : design.specializations()) {
    if (specialization.instance == design.top()) {
      continue;
    }

    std::string_view parent_path = design.top();
    for (const auto& candidate : design.specializations()) {
      if (candidate.instance != specialization.instance
          && candidate.instance.size() > parent_path.size()
          && path_is_within(
              specialization.instance, candidate.instance)) {
        parent_path = candidate.instance;
      }
    }
    auto parent_scope =
        owning_design_scope(session, specialization.instance);
    const auto relative = std::string_view{specialization.instance}.substr(
        parent_path.size() + 1);
    std::size_t segment_begin = 0;
    while (true) {
      const auto segment_end = relative.find('.', segment_begin);
      if (segment_end == std::string_view::npos) {
        break;
      }
      const auto region_name =
          std::string(parent_path) + "."
          + std::string(relative.substr(0, segment_end));
      parent_scope = append_design_scope(
          session,
          ScopeObjectKind::generate,
          parent_scope,
          region_name,
          "generate",
          specialization.source);
      if (!parent_scope) {
        session.scopes.clear();
        return false;
      }
      segment_begin = segment_end + 1;
    }
    if (!append_design_scope(
            session,
            ScopeObjectKind::instance,
            parent_scope,
            specialization.instance,
            specialization.unit,
            specialization.source)) {
      session.scopes.clear();
      return false;
    }
  }

  const auto& processes = design.processes();
  for (std::size_t process = 0; process < processes.size(); ++process) {
    const auto& program = processes[process];
    for (std::size_t local = 0;
         local < program.debug_locals.size(); ++local) {
      const auto& debug_local = program.debug_locals[local];
      std::optional<std::size_t> parent_scope;
      const auto leaf_separator = debug_local.name.find_last_of('.');
      if (leaf_separator != std::string::npos) {
        const auto scope_path =
            std::string_view{debug_local.name}.substr(0, leaf_separator);
        std::size_t segment_begin = 0;
        while (segment_begin < scope_path.size()) {
          const auto segment_end =
              scope_path.find('.', segment_begin);
          const auto prefix_end =
              segment_end == std::string_view::npos
              ? scope_path.size()
              : segment_end;
          const auto full_name =
              program.name + "."
              + std::string(scope_path.substr(0, prefix_end));
          parent_scope = ensure_scope(
              session,
              process,
              parent_scope,
              full_name,
              debug_local.source);
          if (!parent_scope) {
            session.scopes.clear();
            session.variables.clear();
            return false;
          }
          if (segment_end == std::string_view::npos) {
            break;
          }
          segment_begin = segment_end + 1;
        }
      }
      if (session.variables.size() > kObjectIndexMask) {
        session.scopes.clear();
        session.variables.clear();
        return false;
      }
      session.variables.push_back({
          process,
          local,
          parent_scope,
          program.name + "." + debug_local.name});
    }
  }
  session.process_names.reserve(processes.size());
  for (const auto& process : processes) {
    auto name = process.name;
    const auto collides_with_scope =
        std::any_of(
            session.scopes.begin(),
            session.scopes.end(),
            [&](const ScopeObject& scope) {
              return scope.full_name == name;
            });
    if (collides_with_scope) {
      name += ".$process";
    }
    session.process_names.push_back(std::move(name));
  }

  std::vector<std::size_t> driver_ordinals(
      design.signals().size(), 0);
  for (std::size_t process_index = 0;
       process_index < processes.size(); ++process_index) {
    std::vector<fsim::runtime::simir::SignalId> outputs;
    for (const auto& operation : processes[process_index].operations) {
      const auto signal = output_signal(operation);
      if (signal
          && std::find(outputs.begin(), outputs.end(), *signal)
              == outputs.end()) {
        outputs.push_back(*signal);
      }
    }
    for (const auto signal : outputs) {
      if (session.drivers.size() > kDriverIndexMask
          || signal >= design.signals().size()) {
        session.scopes.clear();
        session.process_names.clear();
        session.variables.clear();
        session.drivers.clear();
        return false;
      }
      const auto ordinal = driver_ordinals[signal]++;
      session.drivers.push_back(DriverObject{
          signal,
          process_index,
          design.signals()[signal].name + ".$driver["
              + std::to_string(ordinal) + "]"});
    }
  }
  return true;
}

std::string_view leaf_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return separator == std::string_view::npos ? name : name.substr(separator + 1);
}

bool variable_initialized(
    Session& session, const VariableObject& variable) {
  try {
    (void)session.simulation->read_process_local(
        static_cast<fsim::runtime::simir::ProcessId>(
            variable.process),
        variable.local);
    return true;
  } catch (const std::logic_error&) {
    return false;
  }
}

bool scope_entered(Session& session, const ScopeObject& scope) {
  if (scope.kind != ScopeObjectKind::lexical || !scope.process) {
    return false;
  }
  const auto prefix = scope.full_name + ".";
  for (const auto& variable : session.variables) {
    if (variable.process == *scope.process
        && variable.full_name.starts_with(prefix)
        && variable_initialized(session, variable)) {
      return true;
    }
  }
  return false;
}

void lifecycle(Session& session, const fsim_lifecycle_event_t event) {
  if (session.callbacks.lifecycle) {
    CallbackGuard guard{session};
    session.callbacks.lifecycle(
        session.handle, event, session.callbacks.user_data);
  }
}

void invoke_safe_point(
    Session& session,
    fsim::runtime::Scheduler& scheduler,
    const fsim_object_t process,
    const fsim::runtime::simir::ExecutionPoint* point,
    const std::optional<fsim::runtime::SchedulerPhase> phase) {
  if (session.stop_requested.exchange(false, std::memory_order_relaxed)) {
    session.external_stop_seen.store(true, std::memory_order_relaxed);
    scheduler.request_stop();
  }
  if (session.callbacks.safe_point) {
    CallbackGuard guard{session};
    session.callbacks.safe_point(
        session.handle,
        process,
        scheduler.now(),
        scheduler.delta(),
        session.callbacks.user_data);
  }
  if (session.callbacks.safe_point_info) {
    fsim_safe_point_info_t info{};
    info.struct_size = sizeof(info);
    info.api_version = FSIM_API_VERSION;
    info.process = process;
    info.time = scheduler.now();
    info.delta = scheduler.delta();
    info.instruction = UINT64_MAX;
    info.kind = FSIM_SAFE_POINT_SCHEDULER;
    info.phase = FSIM_SCHEDULER_PHASE_UNKNOWN;
    if (point != nullptr) {
      info.instruction = point->instruction;
      info.kind = convert_safe_point_kind(point->kind);
      info.source_path = view(point->source.path);
      info.source_line = point->source.line;
      info.source_column = point->source.column;
    } else if (phase) {
      info.phase = convert_scheduler_phase(*phase);
    }
    CallbackGuard guard{session};
    session.callbacks.safe_point_info(
        session.handle, &info, session.callbacks.user_data);
  }
}

void attach_callbacks(Session& session) {
  auto* state = &session;
  session.simulation->set_signal_change_hook(
      [state](
          const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4&,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        if (state->callbacks.value_change) {
          CallbackGuard guard{*state};
          state->callbacks.value_change(
              state->handle,
              signal_handle(*state, signal),
              time,
              delta,
              state->callbacks.user_data);
        }
      });
  session.simulation->set_safe_point_hook(
      [state](
          fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::SchedulerPhase phase) {
        invoke_safe_point(
            *state,
            scheduler,
            FSIM_INVALID_OBJECT,
            nullptr,
            phase);
      });
  session.simulation->set_report_hook(
      [state](
          const fsim::runtime::simir::ProcessId process,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity severity,
          const fsim::runtime::simir::SourceLocation& source,
          const fsim::runtime::SimulationTick,
          const std::uint64_t) {
        fsim::diagnostic::Severity diagnostic_severity =
            fsim::diagnostic::Severity::error;
        switch (severity) {
        case fsim::runtime::simir::AssertionSeverity::note:
          diagnostic_severity = fsim::diagnostic::Severity::note;
          break;
        case fsim::runtime::simir::AssertionSeverity::warning:
          diagnostic_severity = fsim::diagnostic::Severity::warning;
          break;
        case fsim::runtime::simir::AssertionSeverity::error:
          diagnostic_severity = fsim::diagnostic::Severity::error;
          break;
        case fsim::runtime::simir::AssertionSeverity::failure:
          diagnostic_severity = fsim::diagnostic::Severity::fatal;
          break;
        }
        fsim::diagnostic::SourceSpan span;
        span.path = source.path;
        span.begin.line = source.line;
        span.begin.column = source.column;
        span.end = span.begin;
        state->diagnostics.report(fsim::diagnostic::Diagnostic{
            diagnostic_severity,
            "FSIM-API-REPORT-0001",
            std::string{message},
            std::move(span),
            {}});
        if (!state->callbacks.assertion) {
          return;
        }
        const auto& stored =
            state->diagnostics.diagnostics().back();
        fsim_diagnostic_t diagnostic{};
        diagnostic.struct_size = sizeof(diagnostic);
        diagnostic.api_version = FSIM_API_VERSION;
        diagnostic.severity = convert_severity(stored.severity);
        diagnostic.code = view(stored.code);
        diagnostic.message = view(stored.message);
        diagnostic.path = view(stored.span.path);
        diagnostic.line = stored.span.begin.line;
        diagnostic.column = stored.span.begin.column;
        CallbackGuard guard{*state};
        state->callbacks.assertion(
            state->handle,
            process_handle(*state, process),
            &diagnostic,
            state->callbacks.user_data);
      });
  if (session.callbacks.safe_point
      || session.callbacks.safe_point_info) {
    session.simulation->set_execution_point_hook(
        [state](
            fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::simir::ExecutionPoint& point) {
          invoke_safe_point(
              *state,
              scheduler,
              process_handle(*state, point.process),
              &point);
        });
  } else {
    session.simulation->set_execution_point_hook({});
  }
}

fsim_status_t runtime_failure(Session& session, const std::exception& error) {
  session.finished = true;
  session.diagnostics.error("FSIM-API-RUN-0001", error.what());
  lifecycle(session, FSIM_LIFECYCLE_SIMULATION_STOPPED);
  return FSIM_STATUS_RUNTIME_ERROR;
}

fsim::diagnostic::Severity assertion_severity(
    const fsim::runtime::simir::AssertionSeverity severity) noexcept {
  switch (severity) {
    case fsim::runtime::simir::AssertionSeverity::note:
      return fsim::diagnostic::Severity::note;
    case fsim::runtime::simir::AssertionSeverity::warning:
      return fsim::diagnostic::Severity::warning;
    case fsim::runtime::simir::AssertionSeverity::error:
      return fsim::diagnostic::Severity::error;
    case fsim::runtime::simir::AssertionSeverity::failure:
      return fsim::diagnostic::Severity::fatal;
  }
  return fsim::diagnostic::Severity::error;
}

fsim_status_t assertion_failure(
    Session& session,
    const fsim::runtime::simir::AssertionError& error) {
  session.finished = true;
  if (error.reported()) {
    lifecycle(session, FSIM_LIFECYCLE_SIMULATION_STOPPED);
    return FSIM_STATUS_RUNTIME_ERROR;
  }
  const auto& location = error.source();
  fsim::diagnostic::SourceSpan span;
  span.path = location.path;
  span.begin.line = location.line;
  span.begin.column = location.column;
  span.end = span.begin;
  session.diagnostics.report(fsim::diagnostic::Diagnostic{
      assertion_severity(error.severity()),
      "FSIM-API-ASSERT-0001",
      error.what(),
      std::move(span),
      {}});
  if (session.callbacks.assertion) {
    const auto& source = session.diagnostics.diagnostics().back();
    fsim_diagnostic_t diagnostic{};
    diagnostic.struct_size = sizeof(diagnostic);
    diagnostic.api_version = FSIM_API_VERSION;
    diagnostic.severity = convert_severity(source.severity);
    diagnostic.code = view(source.code);
    diagnostic.message = view(source.message);
    diagnostic.path = view(source.span.path);
    diagnostic.line = source.span.begin.line;
    diagnostic.column = source.span.begin.column;
    CallbackGuard guard{session};
    session.callbacks.assertion(
        session.handle,
        process_handle(session, error.process()),
        &diagnostic,
        session.callbacks.user_data);
  }
  lifecycle(session, FSIM_LIFECYCLE_SIMULATION_STOPPED);
  return FSIM_STATUS_RUNTIME_ERROR;
}

bool mutation_forbidden(const Session& session) noexcept {
  return session.running.load(std::memory_order_acquire)
      || session.callback_depth.load(std::memory_order_acquire) != 0;
}

fsim_status_t run_session(
    Session& session,
    const std::optional<fsim::runtime::SimulationTick> until) {
  if (mutation_forbidden(session)) {
    return FSIM_STATUS_UNAVAILABLE;
  }
  if (!ready(session, "simulation")) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  if (session.finished) {
    return FSIM_STATUS_STOPPED;
  }
  struct RunningGuard {
    std::atomic_bool& running;
    ~RunningGuard() {
      running.store(false, std::memory_order_release);
    }
  };
  session.running.store(true, std::memory_order_release);
  RunningGuard guard{session.running};
  session.stop_requested.store(false, std::memory_order_relaxed);
  session.external_stop_seen.store(false, std::memory_order_relaxed);
  session.simulation->clear_stop();
  lifecycle(session, FSIM_LIFECYCLE_SIMULATION_STARTED);
  try {
    const auto result = session.simulation->run(until);
    if (result.status == fsim::runtime::RunStatus::stopped) {
      const auto external =
          session.external_stop_seen.load(std::memory_order_relaxed);
      // A step or asynchronous stop may be observed at the same safe point as
      // a terminal HDL stop. The simulator's terminal lifecycle takes
      // precedence; only a genuinely nonterminal external stop is resumable.
      session.finished = session.simulation->finished() || !external;
      lifecycle(
          session,
          session.finished ? FSIM_LIFECYCLE_SIMULATION_FINISHED
                           : FSIM_LIFECYCLE_SIMULATION_STOPPED);
      return FSIM_STATUS_STOPPED;
    }
    if (result.status == fsim::runtime::RunStatus::completed) {
      session.finished = true;
      lifecycle(session, FSIM_LIFECYCLE_SIMULATION_FINISHED);
    } else {
      lifecycle(session, FSIM_LIFECYCLE_SIMULATION_STOPPED);
    }
    return FSIM_STATUS_OK;
  } catch (const fsim::runtime::simir::AssertionError& error) {
    return assertion_failure(session, error);
  } catch (const std::exception& error) {
    return runtime_failure(session, error);
  }
}


} // namespace fsim::api::detail
