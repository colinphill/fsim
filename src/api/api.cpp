// SPDX-License-Identifier: Apache-2.0
#include "api_internal.hpp"
#include "fsim/support/path.hpp"


using namespace fsim::api::detail;

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
        session->seed_override = true;
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
        fsim::support::path_from_utf8(manifest_path), value.diagnostics);
    if (!loaded) {
      return FSIM_STATUS_COMPILE_ERROR;
    }
    value.max_deltas = loaded->run.max_deltas;
    if (value.seed_override) {
      loaded->project.seed = value.seed;
      loaded->project.random_seed = false;
    } else if (!loaded->project.random_seed) {
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
    const auto& design = value.simulation->design_ir();
    if (requested.empty()
        || (!has_synthetic_root(design)
            && is_design_root(design, requested))) {
      *out_object = root_handle(value);
      return FSIM_STATUS_OK;
    }
    for (std::size_t index = 0;
         index < value.systemc_design_object_by_object.size(); ++index) {
      const auto* design_object = design_systemc_object(value, index);
      const auto* process = design_systemc_process(value, index);
      if ((design_object != nullptr && design_object->path == requested)
          || (process != nullptr && process->name == requested)) {
        *out_object = debug_systemc_object_handle(value, index);
        return FSIM_STATUS_OK;
      }
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
    const auto visit_systemc_children =
        [&](const std::string_view parent_name) {
          for (std::size_t index = 0;
               index < value.systemc_design_object_by_object.size();
               ++index) {
            const auto* object = design_systemc_object(value, index);
            const auto* process = design_systemc_process(value, index);
            const auto path = object != nullptr
                ? std::string_view{object->path}
                : process != nullptr ? std::string_view{process->name}
                                     : std::string_view{};
            const auto separator = path.rfind('.');
            const auto object_parent = separator == std::string_view::npos
                ? std::string_view{}
                : path.substr(0, separator);
            if (object_parent != parent_name
                || (object != nullptr
                    && object->kind
                        == fsim::semantic::design::ObjectKind::systemc_module)) {
              continue;
            }
            CallbackGuard guard{value};
            if (callback(
                    value.handle,
                    debug_systemc_object_handle(value, index),
                    user_data)
                == 0) {
              return false;
            }
          }
          return true;
        };
    const auto systemc_replaces_signal =
        [&](const fsim::semantic::design::Object& signal,
            const std::string_view parent_name) {
          for (std::size_t index = 0;
               index < value.systemc_design_object_by_object.size();
               ++index) {
            const auto* object = design_systemc_object(value, index);
            if (object == nullptr || object->width == 0
                || object->runtime_index != signal.runtime_index
                || leaf_name(object->path) != leaf_name(signal.path)) {
              continue;
            }
            const auto separator = object->path.rfind('.');
            const auto object_parent = separator == std::string::npos
                ? std::string_view{}
                : std::string_view{object->path}.substr(0, separator);
            if (object_parent == parent_name) {
              return true;
            }
          }
          return false;
        };
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
      if (object_systemc(value, parent)) {
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
          if (!visit_systemc_children(parent_scope.full_name)) {
            return FSIM_STATUS_OK;
          }
          for (const auto& signal :
               value.simulation->design_ir().objects()) {
            if (signal.kind
                    != fsim::semantic::design::ObjectKind::signal
                || signal.parent_object
                || owning_design_scope(value, signal.path)
                    != scope_index
                || systemc_replaces_signal(
                    signal, parent_scope.full_name)) {
              continue;
            }
            CallbackGuard guard{value};
            if (callback(
                    value.handle,
                    signal_handle(
                        value,
                        static_cast<fsim::runtime::simir::SignalId>(
                            signal.runtime_index)),
                    user_data)
                == 0) {
              return FSIM_STATUS_OK;
            }
          }
          for (const auto& process :
               value.simulation->design_ir().processes()) {
            const auto index = static_cast<std::size_t>(
                process.runtime_index);
            if (owning_design_scope(value, process.name)
                    != scope_index
                || (index < value.systemc_object_by_process.size()
                    && value.systemc_object_by_process[index])) {
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
    if (has_synthetic_root(value.simulation->design_ir())) {
      return FSIM_STATUS_OK;
    }
    const auto& top = value.simulation->design_ir().top();
    if (!visit_systemc_children(top)) {
      return FSIM_STATUS_OK;
    }
    for (const auto& signal : value.simulation->design_ir().objects()) {
      if (signal.kind != fsim::semantic::design::ObjectKind::signal
          || signal.parent_object
          || owning_design_scope(value, signal.path)
          || systemc_replaces_signal(signal, top)) {
        continue;
      }
      CallbackGuard guard{value};
      if (callback(
              value.handle,
              signal_handle(
                  value,
                  static_cast<fsim::runtime::simir::SignalId>(
                      signal.runtime_index)),
              user_data)
          == 0) {
        return FSIM_STATUS_OK;
      }
    }
    for (const auto& process : value.simulation->design_ir().processes()) {
      const auto index = static_cast<std::size_t>(process.runtime_index);
      if (owning_design_scope(value, process.name)
          || (index < value.systemc_object_by_process.size()
              && value.systemc_object_by_process[index])) {
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
        [&](const std::string_view path,
            const std::uint32_t line,
            const std::uint32_t column) {
          if (path.empty()
              || (!write_source_path && !write_source_line
                  && !write_source_column)) {
            return;
          }
          if (write_source_path) {
            out_info->source_path = view(path);
          }
          if (write_source_line) {
            out_info->source_line = line;
          }
          if (write_source_column) {
            out_info->source_column = column;
          }
          out_info->flags |= FSIM_OBJECT_FLAG_HAS_SOURCE;
        };
    if (object == root_handle(value)) {
      const auto& design = value.simulation->design_ir();
      const auto name = has_synthetic_root(design)
          ? std::string_view{"$root"}
          : std::string_view{design.top()};
      out_info->parent = FSIM_INVALID_OBJECT;
      out_info->kind = FSIM_OBJECT_ROOT;
      out_info->width = 0;
      out_info->name = view(name);
      out_info->full_name = view(name);
      out_info->type_name = view("design");
      return FSIM_STATUS_OK;
    }
    if (const auto systemc = object_systemc(value, object)) {
      const auto* design_object = design_systemc_object(value, *systemc);
      const auto* process = design_systemc_process(value, *systemc);
      const auto path = design_object != nullptr
          ? std::string_view{design_object->path}
          : process != nullptr ? std::string_view{process->name}
                               : std::string_view{};
      if (path.empty()) {
        return FSIM_STATUS_INVALID_HANDLE;
      }
      out_info->parent = root_handle(value);
      const auto separator = path.rfind('.');
      const auto parent_path = separator == std::string_view::npos
          ? std::string_view{}
          : path.substr(0, separator);
      const auto& design = value.simulation->design_ir();
      if (!parent_path.empty()
          && (has_synthetic_root(design)
              || !is_design_root(design, parent_path))) {
        const auto parent = std::ranges::find_if(
            value.simulation->design_ir().objects(),
            [&](const fsim::semantic::design::Object& candidate) {
              return candidate.path == parent_path;
            });
        if (parent != value.simulation->design_ir().objects().end()) {
          if (const auto adapter =
                  systemc_adapter_for_object(value, parent->id)) {
            out_info->parent =
                debug_systemc_object_handle(value, *adapter);
          }
        } else {
          const auto scope_parent = std::find_if(
              value.scopes.begin(), value.scopes.end(),
              [&](const ScopeObject& candidate) {
                return candidate.full_name == parent_path;
              });
          if (scope_parent != value.scopes.end()) {
            out_info->parent = scope_handle(
                value,
                static_cast<std::size_t>(
                    std::distance(value.scopes.begin(), scope_parent)));
          }
        }
      }
      out_info->width = design_object == nullptr ? 0 : design_object->width;
      out_info->name = view(leaf_name(path));
      out_info->full_name = view(path);
      out_info->type_name = design_object == nullptr
          ? view("process")
          : view(design_object->external_type);
      if (process != nullptr) {
        out_info->kind = FSIM_OBJECT_PROCESS;
      } else {
        switch (design_object->kind) {
      case fsim::semantic::design::ObjectKind::systemc_module:
        out_info->kind = FSIM_OBJECT_SCOPE;
        break;
      case fsim::semantic::design::ObjectKind::systemc_port:
        out_info->kind = FSIM_OBJECT_PORT;
        break;
      case fsim::semantic::design::ObjectKind::systemc_event:
        out_info->kind = FSIM_OBJECT_EVENT;
        break;
      case fsim::semantic::design::ObjectKind::systemc_channel:
        out_info->kind = FSIM_OBJECT_CHANNEL;
        break;
      case fsim::semantic::design::ObjectKind::systemc_signal:
        out_info->kind = FSIM_OBJECT_SIGNAL;
        break;
      case fsim::semantic::design::ObjectKind::systemc_export:
        out_info->kind = FSIM_OBJECT_EXPORT;
        break;
      default:
        return FSIM_STATUS_INTERNAL_ERROR;
        }
      }
      if (design_object != nullptr && design_object->width != 0) {
        const auto signal_id =
            static_cast<fsim::runtime::simir::SignalId>(
                design_object->runtime_index);
        const auto& signal =
            value.simulation->runtime_adapter().signals().at(signal_id);
        if (value.simulation->signal_is_forced(signal_id)) {
          out_info->flags |= FSIM_OBJECT_FLAG_FORCED;
        }
        if (signal.resolution
            != fsim::runtime::simir::ResolutionKind::none) {
          out_info->flags |= FSIM_OBJECT_FLAG_RESOLVED;
        }
      }
      const auto source = design_source(
          value, design_object != nullptr ? design_object->source
                                          : process->source);
      set_source(source.path, source.line, source.column);
      return FSIM_STATUS_OK;
    }
    if (const auto signal = object_signal(value, object)) {
      const auto* design_object = design_signal(value, *signal);
      if (design_object == nullptr) {
        return FSIM_STATUS_INTERNAL_ERROR;
      }
      const auto& runtime_info =
          value.simulation->runtime_adapter().signals().at(*signal);
      const auto owner = owning_design_scope(value, design_object->path);
      out_info->parent =
          owner ? scope_handle(value, *owner) : root_handle(value);
      const auto is_port = std::ranges::any_of(
          value.simulation->design_ir().ports(), [&](const auto& port) {
            return port.object == design_object->id;
          });
      out_info->kind = is_port ? FSIM_OBJECT_PORT : FSIM_OBJECT_SIGNAL;
      out_info->width = design_object->width;
      out_info->name = view(leaf_name(design_object->path));
      out_info->full_name = view(design_object->path);
      out_info->type_name = design_object->type.spelling.empty()
          ? view("logic4")
          : view(design_object->type.spelling);
      out_info->flags =
          value.simulation->signal_is_forced(*signal)
          ? FSIM_OBJECT_FLAG_FORCED
          : 0U;
      if (runtime_info.resolution
          != fsim::runtime::simir::ResolutionKind::none) {
        out_info->flags |= FSIM_OBJECT_FLAG_RESOLVED;
      }
      const auto source = design_source(value, design_object->source);
      set_source(source.path, source.line, source.column);
      return FSIM_STATUS_OK;
    }
    if (const auto process = object_process(value, object)) {
      const auto* occurrence = design_process(value, *process);
      if (occurrence == nullptr) {
        return FSIM_STATUS_INTERNAL_ERROR;
      }
      const auto& public_name = value.process_names.at(*process);
      const auto owner = owning_design_scope(value, occurrence->name);
      out_info->parent =
          owner ? scope_handle(value, *owner) : root_handle(value);
      out_info->kind = FSIM_OBJECT_PROCESS;
      out_info->width = 0;
      out_info->name = view(leaf_name(public_name));
      out_info->full_name = view(public_name);
      out_info->type_name = view("process");
      const auto source = design_source(value, occurrence->source);
      set_source(source.path, source.line, source.column);
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
      set_source(
          info.source.path, info.source.line, info.source.column);
      return FSIM_STATUS_OK;
    }
    if (const auto variable = object_variable(value, object)) {
      const auto& reference = value.variables[*variable];
      const auto& process =
          value.simulation->runtime_adapter().processes().at(
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
      set_source(
          info.source.path, info.source.line, info.source.column);
      return FSIM_STATUS_OK;
    }
    if (const auto driver = object_driver(value, object)) {
      const auto& reference = value.drivers[*driver];
      const auto* signal = design_signal(value, reference.signal);
      const auto* process = design_process(value, reference.process);
      if (signal == nullptr || process == nullptr) {
        return FSIM_STATUS_INTERNAL_ERROR;
      }
      out_info->parent = signal_handle(value, reference.signal);
      out_info->kind = FSIM_OBJECT_DRIVER;
      out_info->width = signal->width;
      out_info->name = view(leaf_name(reference.full_name));
      out_info->full_name = view(reference.full_name);
      out_info->type_name = view("driver");
      const auto source = design_source(value, process->source);
      set_source(source.path, source.line, source.column);
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
      const auto& reference = value.drivers[*driver];
      packed = value.simulation->read_driver(
          static_cast<fsim::runtime::simir::ProcessId>(
              reference.process),
          reference.signal);
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
    const auto* info = design_signal(value, *signal);
    if (info == nullptr) {
      return FSIM_STATUS_INTERNAL_ERROR;
    }
    const auto text =
        input.data == nullptr ? std::string_view{}
                              : std::string_view{input.data, input.size};
    std::string error;
    auto parsed = fsim::app::parse_value(text, info->width, error);
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
    const auto* info = design_signal(value, *signal);
    if (info == nullptr) {
      return FSIM_STATUS_INTERNAL_ERROR;
    }
    const auto text =
        input.data == nullptr ? std::string_view{}
                              : std::string_view{input.data, input.size};
    std::string error;
    auto parsed = fsim::app::parse_value(text, info->width, error);
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
