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
    case Phase::observed:
        return FSIM_SCHEDULER_PHASE_OBSERVED;
    case Phase::reactive:
      return FSIM_SCHEDULER_PHASE_REACTIVE;
    case Phase::re_inactive:
        return FSIM_SCHEDULER_PHASE_RE_INACTIVE;
    case Phase::re_update:
        return FSIM_SCHEDULER_PHASE_RE_UPDATE;
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

fsim_object_t systemc_object_handle(
    const Session& session, const std::size_t object) {
  if (object > kSystemCObjectIndexMask) {
    return FSIM_INVALID_OBJECT;
  }
  return object_handle(
      session,
      kSystemCObjectPayload | static_cast<std::uint32_t>(object));
}

std::optional<std::size_t> object_systemc(
    const Session& session, const fsim_object_t object) {
  if (!session.simulation || !current_object(session, object)) {
    return std::nullopt;
  }
  const auto payload = object_payload(object);
  if ((payload & ~kSystemCObjectIndexMask) != kSystemCObjectPayload) {
    return std::nullopt;
  }
  const auto index =
      static_cast<std::size_t>(payload & kSystemCObjectIndexMask);
  if (index >= session.systemc_design_object_by_object.size()) {
    return std::nullopt;
  }
  return index;
}

const fsim::semantic::design::Object* design_systemc_object(
    const Session& session, const std::size_t object) noexcept {
  if (!session.simulation
      || object >= session.systemc_design_object_by_object.size()) {
    return nullptr;
  }
  const auto id = session.systemc_design_object_by_object[object];
  return id ? &session.simulation->design_ir().objects()[id->value()]
            : nullptr;
}

const fsim::semantic::design::ProcessOccurrence* design_systemc_process(
    const Session& session, const std::size_t object) noexcept {
  if (!session.simulation
      || object >= session.systemc_design_process_by_object.size()) {
    return nullptr;
  }
  const auto id = session.systemc_design_process_by_object[object];
  return id ? &session.simulation->design_ir().processes()[id->value()]
            : nullptr;
}

std::optional<std::size_t> systemc_adapter_for_object(
    const Session& session,
    const fsim::semantic::design::ObjectId object) noexcept {
  const auto found = std::ranges::find(
      session.systemc_design_object_by_object,
      std::optional<fsim::semantic::design::ObjectId>{object});
  return found == session.systemc_design_object_by_object.end()
      ? std::nullopt
      : std::optional<std::size_t>{static_cast<std::size_t>(
            std::distance(
                session.systemc_design_object_by_object.begin(), found))};
}

std::optional<std::size_t> systemc_adapter_for_process(
    const Session& session,
    const fsim::semantic::design::ProcessOccurrenceId process) noexcept {
  const auto found = std::ranges::find(
      session.systemc_design_process_by_object,
      std::optional<fsim::semantic::design::ProcessOccurrenceId>{process});
  return found == session.systemc_design_process_by_object.end()
      ? std::nullopt
      : std::optional<std::size_t>{static_cast<std::size_t>(
            std::distance(
                session.systemc_design_process_by_object.begin(), found))};
}

const fsim::semantic::design::Object* design_signal(
    const Session& session,
    const fsim::runtime::simir::SignalId signal) noexcept {
  if (!session.simulation) {
    return nullptr;
  }
  const auto& objects = session.simulation->design_ir().objects();
  const auto found = std::ranges::find_if(
      objects, [&](const fsim::semantic::design::Object& candidate) {
        return candidate.kind == fsim::semantic::design::ObjectKind::signal
            && !candidate.parent_object
            && candidate.runtime_index == signal;
      });
  return found == objects.end() ? nullptr : &*found;
}

const fsim::semantic::design::ProcessOccurrence* design_process(
    const Session& session, const std::size_t process) noexcept {
  if (!session.simulation) {
    return nullptr;
  }
  const auto& processes = session.simulation->design_ir().processes();
  const auto found = std::ranges::find_if(
      processes,
      [&](const fsim::semantic::design::ProcessOccurrence& candidate) {
        return candidate.runtime_index == process;
      });
  return found == processes.end() ? nullptr : &*found;
}

DesignSourceLocation design_source(
    const Session& session,
    const std::optional<fsim::semantic::SourceSpanId> source) {
  if (!session.simulation || !source) {
    return {};
  }
  const auto& semantics = session.simulation->semantics();
  if (source->value() >= semantics.source_spans().size()) {
    return {};
  }
  const auto& span = semantics.source_spans()[source->value()];
  if (span.file.value() >= semantics.source_files().size()) {
    return {};
  }
  return {
      span.logical_name.empty()
          ? semantics.source_files()[span.file.value()].physical_name
          : span.logical_name,
      span.begin.line,
      span.begin.column};
}

fsim_object_t debug_systemc_object_handle(
    const Session& session, const std::size_t object) {
  const auto* design_object = design_systemc_object(session, object);
  const auto* design_process_object = design_systemc_process(session, object);
  if (design_object == nullptr && design_process_object == nullptr) {
    return FSIM_INVALID_OBJECT;
  }
  if (design_object != nullptr
      && design_object->kind
          == fsim::semantic::design::ObjectKind::systemc_module) {
    const auto& design = session.simulation->design_ir();
    if (!has_synthetic_root(design)
        && is_design_root(design, design_object->path)) {
      return root_handle(session);
    }
    if (object < session.systemc_scope_by_object.size()
        && session.systemc_scope_by_object[object]) {
      return scope_handle(
          session, *session.systemc_scope_by_object[object]);
    }
  }
  return systemc_object_handle(session, object);
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
  if (const auto systemc = object_systemc(session, object)) {
    const auto* design_object = design_systemc_object(session, *systemc);
    if (design_object == nullptr || design_object->width == 0
        || design_object->runtime_index
            > std::numeric_limits<fsim::runtime::simir::SignalId>::max()) {
      return std::nullopt;
    }
    return static_cast<fsim::runtime::simir::SignalId>(
        design_object->runtime_index);
  }
  const auto payload = object_payload(object);
  if ((payload & ~kObjectIndexMask) != kSignalPayload) {
    return std::nullopt;
  }
  const auto index = payload & kObjectIndexMask;
  if (index > std::numeric_limits<fsim::runtime::simir::SignalId>::max()
      || design_signal(
             session,
             static_cast<fsim::runtime::simir::SignalId>(index)) == nullptr) {
    return std::nullopt;
  }
  return static_cast<fsim::runtime::simir::SignalId>(index);
}

fsim_object_t process_handle(const Session& session, const std::size_t process) {
  if (process < session.systemc_object_by_process.size()
      && session.systemc_object_by_process[process]) {
    return systemc_object_handle(
        session, *session.systemc_object_by_process[process]);
  }
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
  if (const auto systemc = object_systemc(session, object)) {
    const auto* process = design_systemc_process(session, *systemc);
    if (process == nullptr) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(process->runtime_index);
  }
  const auto payload = object_payload(object);
  if ((payload & ~kObjectIndexMask) != kProcessPayload) {
    return std::nullopt;
  }
  const auto index = payload & kObjectIndexMask;
  if (design_process(session, index) == nullptr) {
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
          source,
          std::nullopt});
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
    const std::string_view source) {
  for (std::size_t index = 0; index < session.scopes.size(); ++index) {
    auto& scope = session.scopes[index];
    if (scope.kind != ScopeObjectKind::lexical
        && scope.full_name == full_name) {
      if (!source.empty()) {
        scope.source =
            fsim::runtime::simir::SourceLocation{
                std::string{source}, 1, 1};
      }
      if (!type_name.empty() && type_name != "hdl_instance") {
        scope.type_name = std::move(type_name);
      }
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
      fsim::runtime::simir::SourceLocation{
          std::string{source}, 1, 1},
      std::nullopt});
  return session.scopes.size() - 1;
}

bool rebuild_debug_objects(Session& session) {
  session.scopes.clear();
  session.process_names.clear();
  session.variables.clear();
  session.drivers.clear();
  session.systemc_scope_by_object.clear();
  session.systemc_object_by_process.clear();
  session.systemc_design_object_by_object.clear();
  session.systemc_design_process_by_object.clear();
  if (!session.simulation) {
    return true;
  }
  const auto& runtime_design = session.simulation->runtime_adapter();
  const auto& design = session.simulation->design_ir();
  const auto& systemc_objects = runtime_design.systemc_objects();
  if (systemc_objects.size() > kSystemCObjectIndexMask) {
    return false;
  }
  session.systemc_scope_by_object.resize(systemc_objects.size());
  session.systemc_object_by_process.resize(runtime_design.processes().size());
  session.systemc_design_object_by_object.resize(systemc_objects.size());
  session.systemc_design_process_by_object.resize(systemc_objects.size());

  using SystemCKind = fsim::elaboration::SystemCNamedObjectKind;
  std::vector<std::size_t> systemc_scopes;
  for (std::size_t index = 0; index < systemc_objects.size(); ++index) {
    const auto& adapter = systemc_objects[index];
    if (adapter.kind == SystemCKind::process) {
      const auto boundary = std::ranges::find_if(
          design.boundaries(), [&](const auto& candidate) {
            return candidate.kind
                    == fsim::semantic::design::BoundaryKind::systemc_process
                && candidate.path == adapter.name && candidate.process;
          });
      if (boundary == design.boundaries().end()) {
        return false;
      }
      session.systemc_design_process_by_object[index] = *boundary->process;
      const auto process = static_cast<std::size_t>(
          design.processes()[boundary->process->value()].runtime_index);
      if (process >= session.systemc_object_by_process.size()) {
        return false;
      }
      session.systemc_object_by_process[process] = index;
      continue;
    }
    const auto expected_kind = [&] {
      using ObjectKind = fsim::semantic::design::ObjectKind;
      switch (adapter.kind) {
        case SystemCKind::module:
        case SystemCKind::foreign_child:
          return ObjectKind::systemc_module;
        case SystemCKind::port:
          return ObjectKind::systemc_port;
        case SystemCKind::event:
          return ObjectKind::systemc_event;
        case SystemCKind::primitive_channel:
          return ObjectKind::systemc_channel;
        case SystemCKind::signal:
          return ObjectKind::systemc_signal;
        case SystemCKind::export_object:
          return ObjectKind::systemc_export;
        case SystemCKind::process:
          break;
      }
      return ObjectKind::systemc_module;
    }();
    const auto object = std::ranges::find_if(
        design.objects(), [&](const auto& candidate) {
          return candidate.kind == expected_kind
              && candidate.path == adapter.name;
        });
    if (object == design.objects().end()) {
      return false;
    }
    session.systemc_design_object_by_object[index] = object->id;
    if (object->kind
        == fsim::semantic::design::ObjectKind::systemc_module) {
      systemc_scopes.push_back(index);
    }
  }
  std::stable_sort(
      systemc_scopes.begin(), systemc_scopes.end(),
      [&](const std::size_t left, const std::size_t right) {
        const auto& left_path = design_systemc_object(session, left)->path;
        const auto& right_path = design_systemc_object(session, right)->path;
        const auto left_depth = static_cast<std::size_t>(std::count(
            left_path.begin(), left_path.end(), '.'));
        const auto right_depth = static_cast<std::size_t>(std::count(
            right_path.begin(), right_path.end(), '.'));
        return left_depth < right_depth;
      });
  for (const auto index : systemc_scopes) {
    const auto& object = *design_systemc_object(session, index);
    if (!has_synthetic_root(design)
        && is_design_root(design, object.path)) {
      continue;
    }
    const auto separator = object.path.rfind('.');
    const auto parent_path = separator == std::string::npos
        ? std::string_view{}
        : std::string_view{object.path}.substr(0, separator);
    const auto parent_scope = owning_design_scope(session, parent_path);
    const auto source = design_source(session, object.source);
    const auto scope = append_design_scope(
        session,
        ScopeObjectKind::instance,
        parent_scope,
        object.path,
        object.external_type,
        source.path);
    if (!scope) {
      session.scopes.clear();
      return false;
    }
    session.scopes[*scope].source = {
        std::string{source.path}, source.line, source.column};
    session.scopes[*scope].systemc_object = index;
    session.systemc_scope_by_object[index] = *scope;
  }

  for (const auto& instance : design.instances()) {
    const auto& specialization =
        design.specializations()[instance.specialization.value()];
    if (specialization.language == fsim::semantic::Language::systemc
        || (!has_synthetic_root(design)
            && is_design_root(design, instance.path))) {
      continue;
    }

    const auto owning_root = std::ranges::find_if(
        design.roots(),
        [&](const auto& root) {
          return instance.path == root
              || (instance.path.size() > root.size()
                  && instance.path.starts_with(root)
                  && instance.path[root.size()] == '.');
        });
    const auto parent_path = instance.parent
        ? std::string_view{
              design.instances()[instance.parent->value()].path}
        : owning_root != design.roots().end()
                  && instance.path != *owning_root
            ? std::string_view{*owning_root}
            : std::string_view{};
    auto parent_scope = owning_design_scope(session, instance.path);
    const auto relative = instance.path.size() > parent_path.size()
            && instance.path[parent_path.size()] == '.'
        ? std::string_view{instance.path}.substr(parent_path.size() + 1)
        : std::string_view{instance.path};
    const auto instance_source = design_source(session, instance.source);
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
          instance_source.path);
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
            instance.path,
            instance.target,
            instance_source.path)) {
      session.scopes.clear();
      return false;
    }
  }

  const auto& runtime_processes = runtime_design.processes();
  session.process_names.resize(runtime_processes.size());
  for (const auto& occurrence : design.processes()) {
    const auto process = static_cast<std::size_t>(occurrence.runtime_index);
    if (process >= runtime_processes.size()) {
      return false;
    }
    const auto& program = runtime_processes[process];
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
              occurrence.name + "."
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
          occurrence.name + "." + debug_local.name});
    }
    auto name = occurrence.name;
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
    session.process_names[process] = std::move(name);
  }

  std::vector<std::pair<std::size_t, fsim::runtime::simir::SignalId>>
      driver_pairs;
  std::map<fsim::runtime::simir::SignalId, std::size_t> driver_ordinals;
  for (const auto& driver : design.drivers()) {
    const auto& object = design.objects()[driver.object.value()];
    const auto& process = design.processes()[driver.process.value()];
    if (object.runtime_index
            > std::numeric_limits<fsim::runtime::simir::SignalId>::max()
        || process.runtime_index >= runtime_processes.size()) {
      return false;
    }
    const auto signal = static_cast<fsim::runtime::simir::SignalId>(
        object.runtime_index);
    const auto pair = std::pair<std::size_t, fsim::runtime::simir::SignalId>{
        process.runtime_index, signal};
    if (std::ranges::find(driver_pairs, pair) != driver_pairs.end()) {
      continue;
    }
    if (session.drivers.size() > kDriverIndexMask) {
      return false;
    }
    driver_pairs.push_back(pair);
    const auto ordinal = driver_ordinals[signal]++;
    session.drivers.push_back(DriverObject{
        signal,
        process.runtime_index,
        object.path + ".$driver[" + std::to_string(ordinal) + "]"});
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
    info.provenance_unit_id = UINT32_MAX;
    info.provenance_source_id = UINT32_MAX;
    std::optional<fsim::app::VerilogScopeProvenance> provenance;
    if (point != nullptr) {
      info.instruction = point->instruction;
      info.kind = convert_safe_point_kind(point->kind);
      info.source_path = view(point->source.path);
      info.source_line = point->source.line;
      info.source_column = point->source.column;
      const auto occurrence = std::ranges::find_if(
          session.simulation->design_ir().processes(),
          [&](const auto& candidate) {
            return candidate.runtime_index == point->process;
          });
      if (occurrence != session.simulation->design_ir().processes().end()) {
        provenance = session.simulation->verilog_scope_provenance(
            occurrence->name);
      }
      if (!provenance && point->process < session.process_names.size()) {
        provenance = session.simulation->verilog_scope_provenance(
            session.process_names.at(point->process));
      }
      if (!provenance && !point->source.path.empty()) {
        const auto candidates
            = session.simulation->verilog_scope_provenance();
        const auto source_owner = std::ranges::find_if(
            candidates, [&](const auto& candidate) {
              return candidate.source_path == point->source.path
                  || std::filesystem::path(candidate.source_path).filename()
                      == std::filesystem::path(point->source.path).filename();
            });
        if (source_owner != candidates.end()) {
          provenance = *source_owner;
        }
      }
    } else if (phase) {
      info.phase = convert_scheduler_phase(*phase);
    }
    if (provenance) {
      info.provenance_unit = view(provenance->semantic_unit);
      info.provenance_source_path = view(provenance->source_path);
      info.provenance_language = view(
          provenance->language == fsim::semantic::Language::verilog
              ? "verilog" : "systemverilog");
      info.provenance_standard = view(provenance->standard);
      info.provenance_compatibility_profile
          = view(provenance->compatibility_profile);
      info.provenance_unit_id = provenance->unit.value();
      info.provenance_source_id = provenance->source.value();
      info.provenance_source_line = provenance->source_line;
      info.provenance_source_column = provenance->source_column;
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
      const auto trace_ok = !session.finished || !session.trace_runtime
          || session.trace_runtime->close(session.diagnostics);
      lifecycle(
          session,
          session.finished ? FSIM_LIFECYCLE_SIMULATION_FINISHED
                           : FSIM_LIFECYCLE_SIMULATION_STOPPED);
      if (!trace_ok) {
        return FSIM_STATUS_RUNTIME_ERROR;
      }
      return FSIM_STATUS_STOPPED;
    }
    if (result.status == fsim::runtime::RunStatus::completed) {
      session.finished = true;
      const auto trace_ok = !session.trace_runtime
          || session.trace_runtime->close(session.diagnostics);
      lifecycle(session, FSIM_LIFECYCLE_SIMULATION_FINISHED);
      if (!trace_ok) {
        return FSIM_STATUS_RUNTIME_ERROR;
      }
    } else {
      lifecycle(session, FSIM_LIFECYCLE_SIMULATION_STOPPED);
    }
    return FSIM_STATUS_OK;
  } catch (const fsim::runtime::simir::AssertionError& error) {
    if (session.trace_runtime) {
      session.trace_runtime->fail(session.diagnostics, error.what());
    }
    return assertion_failure(session, error);
  } catch (const std::exception& error) {
    if (session.trace_runtime) {
      session.trace_runtime->fail(session.diagnostics, error.what());
    }
    return runtime_failure(session, error);
  }
}


} // namespace fsim::api::detail
