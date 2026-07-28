// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"

#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::uint32_t kRootPayload = 1;
constexpr std::uint32_t kScopePayloadBase = 2;
constexpr std::uint32_t kScopeIndexMask = UINT32_C(0x1fffffff);
constexpr std::uint32_t kDriverPayload = UINT32_C(0x20000000);
constexpr std::uint32_t kDriverIndexMask = UINT32_C(0x1fffffff);
constexpr std::uint32_t kSignalPayload = UINT32_C(0x40000000);
constexpr std::uint32_t kProcessPayload = UINT32_C(0x80000000);
constexpr std::uint32_t kVariablePayload = UINT32_C(0xc0000000);
constexpr std::uint32_t kObjectIndexMask = UINT32_C(0x3fffffff);

struct VariableObject {
  std::size_t process{};
  std::size_t local{};
  std::optional<std::size_t> parent_scope;
  std::string full_name;
};

struct DriverObject {
  fsim::runtime::simir::SignalId signal{};
  std::size_t process{};
  std::string full_name;
};

enum class ScopeObjectKind : std::uint8_t {
  instance,
  generate,
  lexical,
};

struct ScopeObject {
  ScopeObjectKind kind{ScopeObjectKind::lexical};
  std::optional<std::size_t> process;
  std::optional<std::size_t> parent_scope;
  std::string full_name;
  std::string type_name;
  fsim::runtime::simir::SourceLocation source;
};

struct Session {
  // Callbacks execute synchronously on the simulation thread and may perform
  // read-only API queries, so same-thread re-entry must not deadlock.
  std::recursive_mutex mutex;
  fsim_session_t handle{FSIM_INVALID_SESSION};
  fsim::diagnostic::Engine diagnostics;
  std::optional<fsim::project::Config> project;
  std::unique_ptr<fsim::app::Simulation> simulation;
  fsim_callbacks_t callbacks{};
  std::atomic_bool stop_requested{false};
  std::atomic_bool external_stop_seen{false};
  std::atomic_bool running{false};
  std::atomic_bool destroyed{false};
  std::atomic_uint32_t callback_depth{0};
  std::uint32_t design_generation{1};
  std::uint64_t max_deltas{100'000};
  std::uint64_t seed{1};
  std::optional<fsim::runtime::simir::ProcessId>
      current_execution_process;
  std::vector<ScopeObject> scopes;
  std::vector<std::string> process_names;
  std::vector<VariableObject> variables;
  std::vector<DriverObject> drivers;
  bool finished{};
};

struct CallbackGuard {
  explicit CallbackGuard(Session& value) : session(value) {
    session.callback_depth.fetch_add(1, std::memory_order_acq_rel);
  }
  ~CallbackGuard() {
    session.callback_depth.fetch_sub(1, std::memory_order_acq_rel);
  }
  Session& session;
};

std::mutex registry_mutex;
std::unordered_map<fsim_session_t, std::shared_ptr<Session>> sessions;
std::atomic_uint64_t next_session{1};

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
  if (const auto* value = std::get_if<WriteBlockingSlice>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteUpdateSlice>(&operation)) {
    return value->signal;
  }
  if (const auto* value = std::get_if<WriteAfterSlice>(&operation)) {
    return value->signal;
  }
  return std::nullopt;
}

template <typename Function>
fsim_status_t with_session(
    const fsim_session_t handle,
    Function&& function) noexcept {
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
    const fsim_object_t process = FSIM_INVALID_OBJECT,
    const fsim::runtime::simir::ExecutionPoint* point = nullptr,
    const std::optional<fsim::runtime::SchedulerPhase> phase =
        std::nullopt) {
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

}  // namespace

extern "C" {

uint32_t fsim_get_api_version(void) {
  return FSIM_API_VERSION;
}

const char* fsim_status_string(const fsim_status_t status) {
  switch (status) {
    case FSIM_STATUS_OK:
      return "ok";
    case FSIM_STATUS_INVALID_ARGUMENT:
      return "invalid argument";
    case FSIM_STATUS_INVALID_HANDLE:
      return "invalid handle";
    case FSIM_STATUS_INCOMPATIBLE_ABI:
      return "incompatible ABI";
    case FSIM_STATUS_IO_ERROR:
      return "I/O error";
    case FSIM_STATUS_COMPILE_ERROR:
      return "compile error";
    case FSIM_STATUS_RUNTIME_ERROR:
      return "runtime error";
    case FSIM_STATUS_STOPPED:
      return "stopped";
    case FSIM_STATUS_UNAVAILABLE:
      return "unavailable";
    case FSIM_STATUS_INTERNAL_ERROR:
      return "internal error";
  }
  return "unknown status";
}

fsim_status_t fsim_session_create(
    const fsim_session_options_t* options,
    fsim_session_t* out_session) {
  if (out_session == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  *out_session = FSIM_INVALID_SESSION;
  if (options != nullptr
      && !valid_struct_header(
          options->struct_size,
          options->api_version,
          FSIM_STRUCT_HEADER_SIZE)) {
    return FSIM_STATUS_INCOMPATIBLE_ABI;
  }
  try {
    auto session = std::make_shared<Session>();
    if (options != nullptr) {
      if (FSIM_STRUCT_CONTAINS(
              options->struct_size,
              fsim_session_options_t,
              max_deltas)) {
        session->max_deltas =
            options->max_deltas == 0 ? std::uint64_t{100'000}
                                     : options->max_deltas;
      }
      if (FSIM_STRUCT_CONTAINS(
              options->struct_size, fsim_session_options_t, seed)) {
        session->seed = options->seed;
      }
    }
    fsim_session_t handle =
        next_session.fetch_add(1, std::memory_order_relaxed);
    if (handle == FSIM_INVALID_SESSION) {
      handle = next_session.fetch_add(1, std::memory_order_relaxed);
    }
    session->handle = handle;
    {
      std::lock_guard lock(registry_mutex);
      sessions.emplace(handle, session);
    }
    *out_session = handle;
    return FSIM_STATUS_OK;
  } catch (...) {
    return FSIM_STATUS_INTERNAL_ERROR;
  }
}

fsim_status_t fsim_session_destroy(const fsim_session_t session) {
  try {
    auto state = find_session(session);
    if (!state) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    std::lock_guard session_lock(state->mutex);
    if (state->destroyed.load(std::memory_order_acquire)) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    if (mutation_forbidden(*state)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    {
      std::lock_guard lock(registry_mutex);
      const auto found = sessions.find(session);
      if (found == sessions.end() || found->second.get() != state.get()) {
        return FSIM_STATUS_INVALID_HANDLE;
      }
      state->destroyed.store(true, std::memory_order_release);
      sessions.erase(found);
    }
    state->stop_requested.store(true, std::memory_order_relaxed);
    state->external_stop_seen.store(true, std::memory_order_relaxed);
    if (state->running.load(std::memory_order_acquire)
        && state->simulation) {
      state->simulation->request_stop();
    }
    return FSIM_STATUS_OK;
  } catch (...) {
    return FSIM_STATUS_INTERNAL_ERROR;
  }
}

fsim_status_t fsim_session_load_project(
    const fsim_session_t session,
    const char* manifest_path) {
  if (manifest_path == nullptr || *manifest_path == '\0') {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  return with_session(session, [&](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    value.diagnostics.clear();
    value.project.reset();
    value.simulation.reset();
    value.scopes.clear();
    value.process_names.clear();
    value.variables.clear();
    value.drivers.clear();
    advance_design_generation(value);
    value.finished = false;
    value.current_execution_process.reset();
    auto loaded = fsim::project::load(
        std::filesystem::path(manifest_path), value.diagnostics);
    if (!loaded) {
      return FSIM_STATUS_COMPILE_ERROR;
    }
    value.max_deltas = loaded->run.max_deltas;
    if (!loaded->project.random_seed) {
      value.seed = loaded->project.seed;
    }
    value.project = std::move(loaded);
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_check(const fsim_session_t session) {
  return with_session(session, [](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    value.diagnostics.clear();
    if (!value.project) {
      value.diagnostics.error(
          "FSIM-API-0002", "load a project before checking it");
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    return fsim::app::check_project(*value.project, value.diagnostics)
        ? FSIM_STATUS_OK
        : FSIM_STATUS_COMPILE_ERROR;
  });
}

fsim_status_t fsim_session_build(const fsim_session_t session) {
  return with_session(session, [](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    value.diagnostics.clear();
    value.simulation.reset();
    value.scopes.clear();
    value.process_names.clear();
    value.variables.clear();
    value.drivers.clear();
    advance_design_generation(value);
    value.finished = false;
    value.current_execution_process.reset();
    if (!value.project) {
      value.diagnostics.error(
          "FSIM-API-0002", "load a project before building it");
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    auto built = fsim::app::build_project(
        *value.project, value.diagnostics);
    if (!built) {
      return FSIM_STATUS_COMPILE_ERROR;
    }
    value.simulation = std::make_unique<fsim::app::Simulation>(
        std::move(*built), value.max_deltas);
    if (!rebuild_debug_objects(value)) {
      value.simulation.reset();
      value.diagnostics.error(
          "FSIM-API-0004",
          "the design contains too many debug-visible scopes, local "
          "variables, or drivers for the version-1 object handle encoding");
      return FSIM_STATUS_INTERNAL_ERROR;
    }
    const auto native_cache =
        value.simulation->native_cache_statistics();
    if (native_cache.load_failures != 0
        || native_cache.store_failures != 0) {
      value.diagnostics.warning(
          "FSIM-CACHE-0004",
          "native LLVM object cache reported "
              + std::to_string(native_cache.load_failures)
              + " load failure(s) and "
              + std::to_string(native_cache.store_failures)
              + " store failure(s); simulation remains valid, but cache "
                "reuse may be incomplete");
    }
    attach_callbacks(value);
    lifecycle(value, FSIM_LIFECYCLE_DESIGN_LOADED);
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_root(
    const fsim_session_t session,
    fsim_object_t* out_root) {
  if (out_root == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  *out_root = FSIM_INVALID_OBJECT;
  return with_session(session, [&](Session& value) {
    if (!ready(value, "hierarchy access")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    *out_root = root_handle(value);
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_find_object(
    const fsim_session_t session,
    const fsim_string_view_t path,
    fsim_object_t* out_object) {
  if ((path.data == nullptr && path.size != 0) || out_object == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  *out_object = FSIM_INVALID_OBJECT;
  return with_session(session, [&](Session& value) {
    if (!ready(value, "object lookup")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    const auto requested =
        path.data == nullptr ? std::string_view{}
                             : std::string_view{path.data, path.size};
    if (requested.empty() || requested == value.simulation->design().top()) {
      *out_object = root_handle(value);
      return FSIM_STATUS_OK;
    }
    if (const auto signal = value.simulation->find_signal(requested)) {
      *out_object = signal_handle(value, *signal);
      return FSIM_STATUS_OK;
    }
    for (std::size_t index = 0;
         index < value.scopes.size(); ++index) {
      if (value.scopes[index].full_name == requested) {
        *out_object = scope_handle(value, index);
        return FSIM_STATUS_OK;
      }
    }
    for (std::size_t index = 0;
         index < value.process_names.size(); ++index) {
      if (value.process_names[index] == requested) {
        *out_object = process_handle(value, index);
        return FSIM_STATUS_OK;
      }
    }
    for (std::size_t index = 0;
         index < value.variables.size(); ++index) {
      if (value.variables[index].full_name == requested) {
        *out_object = variable_handle(value, index);
        return FSIM_STATUS_OK;
      }
    }
    for (std::size_t index = 0;
         index < value.drivers.size(); ++index) {
      if (value.drivers[index].full_name == requested) {
        *out_object = driver_handle(value, index);
        return FSIM_STATUS_OK;
      }
    }
    return FSIM_STATUS_INVALID_HANDLE;
  });
}

fsim_status_t fsim_session_visit_children(
    const fsim_session_t session,
    const fsim_object_t parent,
    const fsim_visit_object_callback_t callback,
    void* user_data) {
  if (callback == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  return with_session(session, [&](Session& value) {
    if (!ready(value, "hierarchy enumeration")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    if (parent != root_handle(value)) {
      if (const auto signal = object_signal(value, parent)) {
        for (std::size_t index = 0;
             index < value.drivers.size(); ++index) {
          if (value.drivers[index].signal != *signal) {
            continue;
          }
          CallbackGuard guard{value};
          if (callback(
                  value.handle,
                  driver_handle(value, index),
                  user_data)
              == 0) {
            break;
          }
        }
        return FSIM_STATUS_OK;
      }
      if (object_variable(value, parent)
          || object_driver(value, parent)) {
        return FSIM_STATUS_OK;
      }
      if (const auto process = object_process(value, parent)) {
        for (std::size_t index = 0;
             index < value.scopes.size(); ++index) {
          const auto& scope = value.scopes[index];
          if (scope.kind != ScopeObjectKind::lexical
              || scope.process != *process || scope.parent_scope) {
            continue;
          }
          CallbackGuard guard{value};
          if (callback(
                  value.handle,
                  scope_handle(value, index),
                  user_data)
              == 0) {
            return FSIM_STATUS_OK;
          }
        }
        for (std::size_t index = 0;
             index < value.variables.size(); ++index) {
          if (value.variables[index].process != *process
              || value.variables[index].parent_scope) {
            continue;
          }
          CallbackGuard guard{value};
          if (callback(
                  value.handle,
                  variable_handle(value, index),
                  user_data)
              == 0) {
            break;
          }
        }
        return FSIM_STATUS_OK;
      }
      if (const auto scope_index = object_scope(value, parent)) {
        const auto& parent_scope = value.scopes[*scope_index];
        for (std::size_t index = 0;
             index < value.scopes.size(); ++index) {
          const auto& scope = value.scopes[index];
          if (scope.parent_scope != scope_index) {
            continue;
          }
          CallbackGuard guard{value};
          if (callback(
                  value.handle,
                  scope_handle(value, index),
                  user_data)
              == 0) {
            return FSIM_STATUS_OK;
          }
        }
        if (parent_scope.kind != ScopeObjectKind::lexical) {
          for (const auto& signal :
               value.simulation->design().signals()) {
            if (owning_design_scope(value, signal.name)
                != scope_index) {
              continue;
            }
            CallbackGuard guard{value};
            if (callback(
                    value.handle,
                    signal_handle(value, signal.id),
                    user_data)
                == 0) {
              return FSIM_STATUS_OK;
            }
          }
          const auto& processes =
              value.simulation->design().processes();
          for (std::size_t index = 0;
               index < processes.size(); ++index) {
            if (owning_design_scope(value, processes[index].name)
                != scope_index) {
              continue;
            }
            CallbackGuard guard{value};
            if (callback(
                    value.handle,
                    process_handle(value, index),
                    user_data)
                == 0) {
              return FSIM_STATUS_OK;
            }
          }
          return FSIM_STATUS_OK;
        }
        for (std::size_t index = 0;
             index < value.variables.size(); ++index) {
          if (value.variables[index].parent_scope != scope_index) {
            continue;
          }
          CallbackGuard guard{value};
          if (callback(
                  value.handle,
                  variable_handle(value, index),
                  user_data)
              == 0) {
            break;
          }
        }
        return FSIM_STATUS_OK;
      }
      return FSIM_STATUS_INVALID_HANDLE;
    }
    for (std::size_t index = 0;
         index < value.scopes.size(); ++index) {
      const auto& scope = value.scopes[index];
      if (scope.kind == ScopeObjectKind::lexical
          || scope.parent_scope) {
        continue;
      }
      CallbackGuard guard{value};
      if (callback(
              value.handle, scope_handle(value, index), user_data)
          == 0) {
        return FSIM_STATUS_OK;
      }
    }
    for (const auto& signal : value.simulation->design().signals()) {
      if (owning_design_scope(value, signal.name)) {
        continue;
      }
      CallbackGuard guard{value};
      if (callback(
              value.handle, signal_handle(value, signal.id), user_data)
          == 0) {
        return FSIM_STATUS_OK;
      }
    }
    const auto& processes = value.simulation->design().processes();
    for (std::size_t index = 0; index < processes.size(); ++index) {
      if (owning_design_scope(value, processes[index].name)) {
        continue;
      }
      CallbackGuard guard{value};
      if (callback(value.handle, process_handle(value, index), user_data) == 0) {
        break;
      }
    }
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_get_object_info(
    const fsim_session_t session,
    const fsim_object_t object,
    fsim_object_info_t* out_info) {
  if (out_info == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  if (!valid_struct_header(
          out_info->struct_size,
          out_info->api_version,
          FSIM_OBJECT_INFO_V1_SIZE)) {
    return FSIM_STATUS_INCOMPATIBLE_ABI;
  }
  return with_session(session, [&](Session& value) {
    if (!ready(value, "object metadata")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    out_info->handle = object;
    out_info->flags = 0;
    const auto write_source_path =
        FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, source_path);
    const auto write_source_line =
        FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, source_line);
    const auto write_source_column =
        FSIM_STRUCT_CONTAINS(
            out_info->struct_size, fsim_object_info_t, source_column);
    if (write_source_path) {
      out_info->source_path = view("");
    }
    if (write_source_line) {
      out_info->source_line = 0;
    }
    if (write_source_column) {
      out_info->source_column = 0;
    }
    const auto set_source =
        [&](const fsim::runtime::simir::SourceLocation& source) {
          if (source.path.empty()
              || (!write_source_path && !write_source_line
                  && !write_source_column)) {
            return;
          }
          if (write_source_path) {
            out_info->source_path = view(source.path);
          }
          if (write_source_line) {
            out_info->source_line = source.line;
          }
          if (write_source_column) {
            out_info->source_column = source.column;
          }
          out_info->flags |= FSIM_OBJECT_FLAG_HAS_SOURCE;
        };
    if (object == root_handle(value)) {
      const auto& top = value.simulation->design().top();
      out_info->parent = FSIM_INVALID_OBJECT;
      out_info->kind = FSIM_OBJECT_ROOT;
      out_info->width = 0;
      out_info->name = view(top);
      out_info->full_name = view(top);
      out_info->type_name = view("design");
      return FSIM_STATUS_OK;
    }
    if (const auto signal = object_signal(value, object)) {
      const auto& info = value.simulation->design().signals().at(*signal);
      const auto owner = owning_design_scope(value, info.name);
      out_info->parent =
          owner ? scope_handle(value, *owner) : root_handle(value);
      out_info->kind = info.is_port ? FSIM_OBJECT_PORT : FSIM_OBJECT_SIGNAL;
      out_info->width = info.width;
      out_info->name = view(leaf_name(info.name));
      out_info->full_name = view(info.name);
      out_info->type_name = view("logic4");
      out_info->flags =
          value.simulation->signal_is_forced(*signal)
          ? FSIM_OBJECT_FLAG_FORCED
          : 0U;
      if (!info.declaration_span.source_name.empty()) {
        set_source(fsim::runtime::simir::SourceLocation{
            info.declaration_span.source_name,
            static_cast<std::uint32_t>(
                info.declaration_span.begin.line),
            static_cast<std::uint32_t>(
                info.declaration_span.begin.column)});
      }
      return FSIM_STATUS_OK;
    }
    if (const auto process = object_process(value, object)) {
      const auto& info = value.simulation->design().processes().at(*process);
      const auto& public_name = value.process_names.at(*process);
      const auto owner = owning_design_scope(value, info.name);
      out_info->parent =
          owner ? scope_handle(value, *owner) : root_handle(value);
      out_info->kind = FSIM_OBJECT_PROCESS;
      out_info->width = 0;
      out_info->name = view(leaf_name(public_name));
      out_info->full_name = view(public_name);
      out_info->type_name = view("process");
      if (const auto* source = process_source(info)) {
        set_source(*source);
      }
      return FSIM_STATUS_OK;
    }
    if (const auto scope = object_scope(value, object)) {
      const auto& info = value.scopes[*scope];
      out_info->parent =
          info.parent_scope
          ? scope_handle(value, *info.parent_scope)
          : info.process
                ? process_handle(value, *info.process)
                : root_handle(value);
      out_info->kind = FSIM_OBJECT_SCOPE;
      out_info->width = 0;
      out_info->name = view(leaf_name(info.full_name));
      out_info->full_name = view(info.full_name);
      out_info->type_name = view(info.type_name);
      if (scope_entered(value, info)) {
        out_info->flags |= FSIM_OBJECT_FLAG_ENTERED;
      }
      set_source(info.source);
      return FSIM_STATUS_OK;
    }
    if (const auto variable = object_variable(value, object)) {
      const auto& reference = value.variables[*variable];
      const auto& process =
          value.simulation->design().processes().at(
              reference.process);
      const auto& info =
          process.debug_locals.at(reference.local);
      out_info->parent =
          reference.parent_scope
          ? scope_handle(value, *reference.parent_scope)
          : process_handle(value, reference.process);
      out_info->kind = FSIM_OBJECT_VARIABLE;
      out_info->width = info.width;
      out_info->name = view(leaf_name(info.name));
      out_info->full_name = view(reference.full_name);
      out_info->type_name = view(info.type_name);
      if (variable_initialized(value, reference)) {
        out_info->flags |= FSIM_OBJECT_FLAG_INITIALIZED;
      }
      set_source(info.source);
      return FSIM_STATUS_OK;
    }
    if (const auto driver = object_driver(value, object)) {
      const auto& reference = value.drivers[*driver];
      const auto& signal =
          value.simulation->design().signals().at(reference.signal);
      const auto& process =
          value.simulation->design().processes().at(reference.process);
      out_info->parent = signal_handle(value, reference.signal);
      out_info->kind = FSIM_OBJECT_DRIVER;
      out_info->width = signal.width;
      out_info->name = view(leaf_name(reference.full_name));
      out_info->full_name = view(reference.full_name);
      out_info->type_name = view("driver");
      if (const auto* source = process_source(process)) {
        set_source(*source);
      }
      return FSIM_STATUS_OK;
    }
    return FSIM_STATUS_INVALID_HANDLE;
  });
}

fsim_status_t fsim_session_read_value(
    const fsim_session_t session,
    const fsim_object_t object,
    char* buffer,
    const size_t buffer_size,
    size_t* out_required) {
  if (out_required == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  *out_required = 0;
  return with_session(session, [&](Session& value) {
    if (!ready(value, "value reads")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    std::optional<fsim::runtime::PackedLogic4> packed;
    if (const auto signal = object_signal(value, object)) {
      packed = value.simulation->read_signal(*signal);
    } else if (const auto driver = object_driver(value, object)) {
      packed = value.simulation->read_signal(
          value.drivers[*driver].signal);
    } else if (const auto variable =
                   object_variable(value, object)) {
      const auto& reference = value.variables[*variable];
      try {
        packed = value.simulation->read_process_local(
            static_cast<fsim::runtime::simir::ProcessId>(
                reference.process),
            reference.local);
      } catch (const std::logic_error&) {
        return FSIM_STATUS_UNAVAILABLE;
      }
    } else {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    const auto encoded = packed->to_msb_string();
    *out_required = encoded.size() + 1;
    if (buffer == nullptr || buffer_size < *out_required) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    std::memcpy(buffer, encoded.data(), encoded.size());
    buffer[encoded.size()] = '\0';
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_deposit(
    const fsim_session_t session,
    const fsim_object_t object,
    const fsim_string_view_t input) {
  if (input.data == nullptr && input.size != 0) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  return with_session(session, [&](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    if (!ready(value, "deposit")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    const auto signal = object_signal(value, object);
    if (!signal) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    const auto& info = value.simulation->design().signals().at(*signal);
    const auto text =
        input.data == nullptr ? std::string_view{}
                              : std::string_view{input.data, input.size};
    std::string error;
    auto parsed = fsim::app::parse_value(text, info.width, error);
    if (!parsed) {
      value.diagnostics.error("FSIM-API-VALUE-0001", error);
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    value.simulation->deposit_signal(*signal, std::move(*parsed));
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_force(
    const fsim_session_t session,
    const fsim_object_t object,
    const fsim_string_view_t input) {
  if (input.data == nullptr && input.size != 0) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  return with_session(session, [&](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    if (!ready(value, "force")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    const auto signal = object_signal(value, object);
    if (!signal) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    const auto& info = value.simulation->design().signals().at(*signal);
    const auto text =
        input.data == nullptr ? std::string_view{}
                              : std::string_view{input.data, input.size};
    std::string error;
    auto parsed = fsim::app::parse_value(text, info.width, error);
    if (!parsed) {
      value.diagnostics.error("FSIM-API-VALUE-0001", error);
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    value.simulation->force_signal(*signal, std::move(*parsed));
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_release(
    const fsim_session_t session,
    const fsim_object_t object) {
  return with_session(session, [&](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    if (!ready(value, "release")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    const auto signal = object_signal(value, object);
    if (!signal) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    value.simulation->release_signal(*signal);
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_run(
    const fsim_session_t session,
    const fsim_time_t until_time) {
  return with_session(session, [&](Session& value) {
    return run_session(value, until_time);
  });
}

fsim_status_t fsim_session_step(
    const fsim_session_t session,
    const fsim_step_kind_t kind) {
  if (kind < FSIM_STEP_STATEMENT || kind > FSIM_STEP_TIME) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  return with_session(session, [&](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    if (!ready(value, "simulation stepping")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    if (kind == FSIM_STEP_STATEMENT || kind == FSIM_STEP_PROCESS) {
      auto* state = &value;
      value.simulation->set_execution_point_hook(
          [state, kind](
              fsim::runtime::Scheduler& scheduler,
              const fsim::runtime::simir::ExecutionPoint& point) {
            invoke_safe_point(
                *state, scheduler,
                process_handle(*state, point.process),
                &point);
            bool stop = false;
            if (kind == FSIM_STEP_STATEMENT) {
              const auto statement =
                  point.kind
                      == fsim::runtime::simir::ExecutionPointKind::statement
                  || point.kind
                      == fsim::runtime::simir::ExecutionPointKind::call
                  || point.kind
                      == fsim::runtime::simir::ExecutionPointKind::wait
                  || point.kind
                      == fsim::runtime::simir::ExecutionPointKind::assertion;
              stop =
                  statement
                  && (!state->current_execution_process
                      || point.process
                          == *state->current_execution_process);
            } else if (!state->current_execution_process) {
              if (point.kind
                  == fsim::runtime::simir::ExecutionPointKind::
                      process_entry) {
                state->current_execution_process = point.process;
              }
            } else {
              stop =
                  point.process == *state->current_execution_process
                  && point.kind
                      == fsim::runtime::simir::ExecutionPointKind::
                          process_suspend;
            }
            if (stop) {
              state->current_execution_process = point.process;
              state->external_stop_seen.store(
                  true, std::memory_order_relaxed);
              scheduler.request_stop();
            }
          });
      const auto status = run_session(value, std::nullopt);
      attach_callbacks(value);
      return status == FSIM_STATUS_STOPPED ? FSIM_STATUS_OK : status;
    }
    const auto start_time = value.simulation->now();
    auto* state = &value;
    value.simulation->set_safe_point_hook(
        [state, start_time, kind](
            fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::SchedulerPhase phase) {
          invoke_safe_point(
              *state,
              scheduler,
              FSIM_INVALID_OBJECT,
              nullptr,
              phase);
          if ((kind == FSIM_STEP_DELTA
               && phase == fsim::runtime::SchedulerPhase::postponed)
              || (kind == FSIM_STEP_TIME
                  && scheduler.now() > start_time)) {
            state->external_stop_seen.store(true, std::memory_order_relaxed);
            scheduler.request_stop();
          }
        });
    const auto status = run_session(value, std::nullopt);
    attach_callbacks(value);
    return status == FSIM_STATUS_STOPPED ? FSIM_STATUS_OK : status;
  });
}

fsim_status_t fsim_session_request_stop(const fsim_session_t session) {
  try {
    auto value = find_session(session);
    if (!value || value->destroyed.load(std::memory_order_acquire)) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    value->stop_requested.store(true, std::memory_order_relaxed);
    value->external_stop_seen.store(true, std::memory_order_relaxed);
    if (value->running.load(std::memory_order_acquire)) {
      value->simulation->request_stop();
      return FSIM_STATUS_OK;
    }
    std::lock_guard lock(value->mutex);
    if (value->simulation) {
      value->simulation->request_stop();
    }
    return FSIM_STATUS_OK;
  } catch (...) {
    return FSIM_STATUS_INTERNAL_ERROR;
  }
}

fsim_status_t fsim_session_set_callbacks(
    const fsim_session_t session,
    const fsim_callbacks_t* callbacks) {
  if (callbacks != nullptr
      && !valid_struct_header(
          callbacks->struct_size,
          callbacks->api_version,
          FSIM_STRUCT_HEADER_SIZE)) {
    return FSIM_STATUS_INCOMPATIBLE_ABI;
  }
  return with_session(session, [&](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    value.callbacks = {};
    if (callbacks != nullptr) {
      value.callbacks.struct_size = sizeof(value.callbacks);
      value.callbacks.api_version = FSIM_API_VERSION;
      if (FSIM_STRUCT_CONTAINS(
              callbacks->struct_size, fsim_callbacks_t, user_data)) {
        value.callbacks.user_data = callbacks->user_data;
      }
      if (FSIM_STRUCT_CONTAINS(
              callbacks->struct_size, fsim_callbacks_t, safe_point)) {
        value.callbacks.safe_point = callbacks->safe_point;
      }
      if (FSIM_STRUCT_CONTAINS(
              callbacks->struct_size, fsim_callbacks_t, value_change)) {
        value.callbacks.value_change = callbacks->value_change;
      }
      if (FSIM_STRUCT_CONTAINS(
              callbacks->struct_size, fsim_callbacks_t, assertion)) {
        value.callbacks.assertion = callbacks->assertion;
      }
      if (FSIM_STRUCT_CONTAINS(
              callbacks->struct_size, fsim_callbacks_t, lifecycle)) {
        value.callbacks.lifecycle = callbacks->lifecycle;
      }
      if (FSIM_STRUCT_CONTAINS(
              callbacks->struct_size,
              fsim_callbacks_t,
              safe_point_info)) {
        value.callbacks.safe_point_info =
            callbacks->safe_point_info;
      }
    }
    if (value.simulation) {
      attach_callbacks(value);
    }
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_get_diagnostic(
    const fsim_session_t session,
    const size_t index,
    fsim_diagnostic_t* out_diagnostic) {
  if (out_diagnostic == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  if (!valid_struct_header(
          out_diagnostic->struct_size,
          out_diagnostic->api_version,
          sizeof(fsim_diagnostic_t))) {
    return FSIM_STATUS_INCOMPATIBLE_ABI;
  }
  return with_session(session, [&](Session& value) {
    const auto& diagnostics = value.diagnostics.diagnostics();
    if (index >= diagnostics.size()) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    const auto& diagnostic = diagnostics[index];
    out_diagnostic->severity = convert_severity(diagnostic.severity);
    out_diagnostic->code = view(diagnostic.code);
    out_diagnostic->message = view(diagnostic.message);
    out_diagnostic->path = view(diagnostic.span.path);
    out_diagnostic->line = diagnostic.span.begin.line;
    out_diagnostic->column = diagnostic.span.begin.column;
    return FSIM_STATUS_OK;
  });
}

fsim_status_t fsim_session_diagnostic_count(
    const fsim_session_t session,
    size_t* out_count) {
  if (out_count == nullptr) {
    return FSIM_STATUS_INVALID_ARGUMENT;
  }
  return with_session(session, [&](Session& value) {
    *out_count = value.diagnostics.diagnostics().size();
    return FSIM_STATUS_OK;
  });
}

}  // extern "C"
