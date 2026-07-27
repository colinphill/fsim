// SPDX-License-Identifier: Apache-2.0
#include "fsim/api.h"

#include "fsim/app/application.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

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
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace {

constexpr std::uint32_t kRootPayload = 1;
constexpr std::uint32_t kSignalPayload = UINT32_C(0x40000000);
constexpr std::uint32_t kProcessPayload = UINT32_C(0x80000000);
constexpr std::uint32_t kObjectIndexMask = UINT32_C(0x3fffffff);

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

bool valid_output_header(
    const std::uint32_t struct_size,
    const std::uint32_t api_version,
    const std::size_t required_size) noexcept {
  return struct_size >= required_size && api_version == FSIM_API_VERSION;
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

std::string_view leaf_name(const std::string_view name) {
  const auto separator = name.find_last_of('.');
  return separator == std::string_view::npos ? name : name.substr(separator + 1);
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
    fsim::runtime::Scheduler& scheduler) {
  if (session.stop_requested.exchange(false, std::memory_order_relaxed)) {
    session.external_stop_seen.store(true, std::memory_order_relaxed);
    scheduler.request_stop();
  }
  if (session.callbacks.safe_point) {
    CallbackGuard guard{session};
    session.callbacks.safe_point(
        session.handle,
        FSIM_INVALID_OBJECT,
        scheduler.now(),
        scheduler.delta(),
        session.callbacks.user_data);
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
          fsim::runtime::SchedulerPhase) {
        invoke_safe_point(*state, scheduler);
      });
}

fsim_status_t runtime_failure(Session& session, const std::exception& error) {
  session.finished = true;
  session.diagnostics.error("FSIM-API-RUN-0001", error.what());
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
        FSIM_INVALID_OBJECT,
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
      && !valid_output_header(
          options->struct_size,
          options->api_version,
          sizeof(fsim_session_options_t))) {
    return FSIM_STATUS_INCOMPATIBLE_ABI;
  }
  try {
    auto session = std::make_shared<Session>();
    if (options != nullptr) {
      session->max_deltas =
          options->max_deltas == 0 ? std::uint64_t{100'000}
                                   : options->max_deltas;
      session->seed = options->seed;
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
    advance_design_generation(value);
    value.finished = false;
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
    advance_design_generation(value);
    value.finished = false;
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
    const auto& processes = value.simulation->design().processes();
    for (std::size_t index = 0; index < processes.size(); ++index) {
      if (processes[index].name == requested) {
        *out_object = process_handle(value, index);
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
      if (object_signal(value, parent) || object_process(value, parent)) {
        return FSIM_STATUS_OK;
      }
      return FSIM_STATUS_INVALID_HANDLE;
    }
    for (const auto& signal : value.simulation->design().signals()) {
      CallbackGuard guard{value};
      if (callback(
              value.handle, signal_handle(value, signal.id), user_data)
          == 0) {
        return FSIM_STATUS_OK;
      }
    }
    const auto& processes = value.simulation->design().processes();
    for (std::size_t index = 0; index < processes.size(); ++index) {
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
  if (!valid_output_header(
          out_info->struct_size,
          out_info->api_version,
          sizeof(fsim_object_info_t))) {
    return FSIM_STATUS_INCOMPATIBLE_ABI;
  }
  return with_session(session, [&](Session& value) {
    if (!ready(value, "object metadata")) {
      return FSIM_STATUS_INVALID_ARGUMENT;
    }
    out_info->handle = object;
    out_info->flags = 0;
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
      out_info->parent = root_handle(value);
      out_info->kind = info.is_port ? FSIM_OBJECT_PORT : FSIM_OBJECT_SIGNAL;
      out_info->width = info.width;
      out_info->name = view(leaf_name(info.name));
      out_info->full_name = view(info.name);
      out_info->type_name = view("logic4");
      out_info->flags =
          value.simulation->signal_is_forced(*signal) ? 1U : 0U;
      return FSIM_STATUS_OK;
    }
    if (const auto process = object_process(value, object)) {
      const auto& info = value.simulation->design().processes().at(*process);
      out_info->parent = root_handle(value);
      out_info->kind = FSIM_OBJECT_PROCESS;
      out_info->width = 0;
      out_info->name = view(info.name);
      out_info->full_name = view(info.name);
      out_info->type_name = view("process");
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
    const auto signal = object_signal(value, object);
    if (!signal) {
      return FSIM_STATUS_INVALID_HANDLE;
    }
    const auto encoded =
        value.simulation->read_signal(*signal).to_msb_string();
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
      value.diagnostics.error(
          "FSIM-API-STEP-0001",
          "statement and process stepping require debug source maps");
      return FSIM_STATUS_UNAVAILABLE;
    }
    const auto start_time = value.simulation->now();
    auto* state = &value;
    value.simulation->set_safe_point_hook(
        [state, start_time, kind](
            fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::SchedulerPhase phase) {
          invoke_safe_point(*state, scheduler);
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
      && !valid_output_header(
          callbacks->struct_size,
          callbacks->api_version,
          sizeof(fsim_callbacks_t))) {
    return FSIM_STATUS_INCOMPATIBLE_ABI;
  }
  return with_session(session, [&](Session& value) {
    if (mutation_forbidden(value)) {
      return FSIM_STATUS_UNAVAILABLE;
    }
    value.callbacks = callbacks == nullptr ? fsim_callbacks_t{} : *callbacks;
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
  if (!valid_output_header(
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
