// SPDX-License-Identifier: Apache-2.0
#include "tcl.hpp"

#include "fsim/api.h"
#include "fsim/app/application.hpp"
#include "fsim/support/environment.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(FSIM_HAS_TCL)
#include <tcl.h>
#endif

namespace fsim::app {
namespace {

constexpr int kSuccess = 0;
constexpr int kUserError = 1;
constexpr int kUnavailable = 3;

#if defined(FSIM_HAS_TCL)

static_assert(
    TCL_MAJOR_VERSION == 9 && TCL_MINOR_VERSION == 0,
    "fsim requires Tcl 9.0");

Tcl_Size tcl_size(const std::size_t value) {
  if (value > static_cast<std::size_t>(TCL_SIZE_MAX)) {
    throw std::length_error{"value exceeds Tcl_Size"};
  }
  return static_cast<Tcl_Size>(value);
}

Tcl_WideInt tcl_wide_size(const std::size_t value) {
  if (value
      > static_cast<std::size_t>(
          std::numeric_limits<Tcl_WideInt>::max())) {
    throw std::length_error{"value exceeds Tcl_WideInt"};
  }
  return static_cast<Tcl_WideInt>(value);
}

struct TclInterpreterDeleter {
  void operator()(Tcl_Interp* interpreter) const noexcept {
    if (interpreter != nullptr) {
      Tcl_DeleteInterp(interpreter);
    }
  }
};

using TclInterpreter = std::unique_ptr<Tcl_Interp, TclInterpreterDeleter>;

struct TclContext {
  const cli::Invocation& invocation;
  project::Config config;
  diagnostic::Engine& diagnostics;
  Tcl_Interp* interpreter;
  std::ostream& output;
  std::ostream& error;
  std::optional<BuiltProject> built;
  std::unique_ptr<Simulation> simulation;
  std::optional<SimulationEngine> simulation_engine;
  std::unique_ptr<std::ostringstream> debug_output;
  std::unique_ptr<std::ostringstream> debug_error;
  std::unique_ptr<DebuggerControl> debugger;
  std::array<std::vector<std::string>, 4> callbacks;
  std::uint64_t signal_callback_token{};
  std::uint64_t safe_point_callback_token{};
  std::size_t callback_depth{};
  bool callbacks_attached{};
  bool lifecycle_started{};
  std::optional<std::string> callback_error;
  bool exit_requested{};
  int exit_code{};
};

std::string_view assertion_severity_name(
    runtime::simir::AssertionSeverity severity);
diagnostic::Severity assertion_diagnostic_severity(
    runtime::simir::AssertionSeverity severity);

struct TclChannelState {
  std::istream* input{};
  std::ostream* output{};
};

int channel_close(void*, Tcl_Interp*, int) noexcept {
  return 0;
}

int channel_input(
    void* client_data,
    char* buffer,
    const int count,
    int* error_code) noexcept {
  auto& state = *static_cast<TclChannelState*>(client_data);
  if (state.input == nullptr) {
    if (error_code != nullptr) {
      *error_code = EINVAL;
    }
    return -1;
  }
  try {
    state.input->read(buffer, count);
    const auto bytes = state.input->gcount();
    if (state.input->bad()) {
      if (error_code != nullptr) {
        *error_code = EIO;
      }
      return -1;
    }
    return static_cast<int>(bytes);
  } catch (...) {
    if (error_code != nullptr) {
      *error_code = EIO;
    }
    return -1;
  }
}

int channel_output(
    void* client_data,
    const char* buffer,
    const int count,
    int* error_code) noexcept {
  auto& state = *static_cast<TclChannelState*>(client_data);
  if (state.output == nullptr) {
    if (error_code != nullptr) {
      *error_code = EINVAL;
    }
    return -1;
  }
  try {
    state.output->write(buffer, count);
    if (!*state.output) {
      if (error_code != nullptr) {
        *error_code = EIO;
      }
      return -1;
    }
    return count;
  } catch (...) {
    if (error_code != nullptr) {
      *error_code = EIO;
    }
    return -1;
  }
}

int channel_flush(void* client_data) noexcept {
  auto& state = *static_cast<TclChannelState*>(client_data);
  if (state.output == nullptr) {
    return 0;
  }
  try {
    state.output->flush();
    return *state.output ? 0 : EIO;
  } catch (...) {
    return EIO;
  }
}

void channel_watch(void*, int) noexcept {}

int channel_get_handle(void*, int, void**) noexcept {
  return TCL_ERROR;
}

const Tcl_ChannelType kStreamChannelType{
    "fsim-stream",
    TCL_CHANNEL_VERSION_5,
    nullptr,
    channel_input,
    channel_output,
    nullptr,
    nullptr,
    nullptr,
    channel_watch,
    channel_get_handle,
    channel_close,
    nullptr,
    channel_flush,
    nullptr,
    nullptr,
    nullptr,
    nullptr};

class TclStreamChannels final {
 public:
  TclStreamChannels(
      Tcl_Interp* interpreter,
      std::istream& input,
      std::ostream& output,
      std::ostream& error)
      : interpreter_(interpreter),
        input_state_{&input, nullptr},
        output_state_{nullptr, &output},
        error_state_{nullptr, &error},
        previous_input_(Tcl_GetStdChannel(TCL_STDIN)),
        previous_output_(Tcl_GetStdChannel(TCL_STDOUT)),
        previous_error_(Tcl_GetStdChannel(TCL_STDERR)) {
    input_ = Tcl_CreateChannel(
        &kStreamChannelType,
        "fsim-stdin",
        &input_state_,
        TCL_READABLE);
    output_ = Tcl_CreateChannel(
        &kStreamChannelType,
        "fsim-stdout",
        &output_state_,
        TCL_WRITABLE);
    error_ = Tcl_CreateChannel(
        &kStreamChannelType,
        "fsim-stderr",
        &error_state_,
        TCL_WRITABLE);
    if (input_ == nullptr || output_ == nullptr || error_ == nullptr) {
      cleanup();
      return;
    }
    Tcl_RegisterChannel(interpreter_, input_);
    Tcl_RegisterChannel(interpreter_, output_);
    Tcl_RegisterChannel(interpreter_, error_);
    Tcl_SetStdChannel(input_, TCL_STDIN);
    Tcl_SetStdChannel(output_, TCL_STDOUT);
    Tcl_SetStdChannel(error_, TCL_STDERR);
    valid_ = true;
  }

  ~TclStreamChannels() {
    cleanup();
  }

  TclStreamChannels(const TclStreamChannels&) = delete;
  TclStreamChannels& operator=(const TclStreamChannels&) = delete;

  [[nodiscard]] bool valid() const noexcept {
    return valid_;
  }

 private:
  void cleanup() noexcept {
    if (valid_) {
      Tcl_SetStdChannel(previous_input_, TCL_STDIN);
      Tcl_SetStdChannel(previous_output_, TCL_STDOUT);
      Tcl_SetStdChannel(previous_error_, TCL_STDERR);
    }
    if (interpreter_ != nullptr) {
      if (input_ != nullptr) {
        (void)Tcl_UnregisterChannel(interpreter_, input_);
      }
      if (output_ != nullptr) {
        (void)Tcl_UnregisterChannel(interpreter_, output_);
      }
      if (error_ != nullptr) {
        (void)Tcl_UnregisterChannel(interpreter_, error_);
      }
    }
    input_ = nullptr;
    output_ = nullptr;
    error_ = nullptr;
    valid_ = false;
  }

  Tcl_Interp* interpreter_{};
  TclChannelState input_state_;
  TclChannelState output_state_;
  TclChannelState error_state_;
  Tcl_Channel previous_input_{};
  Tcl_Channel previous_output_{};
  Tcl_Channel previous_error_{};
  Tcl_Channel input_{};
  Tcl_Channel output_{};
  Tcl_Channel error_{};
  bool valid_{};
};

std::string path_utf8(const std::filesystem::path& path) {
#if defined(_WIN32)
  const auto encoded = path.generic_u8string();
  return {
      reinterpret_cast<const char*>(encoded.data()),
      encoded.size()};
#else
  return path.generic_string();
#endif
}

void configure_tcl_library(Tcl_Interp* interpreter) {
  const auto override_path =
      fsim::support::environment_variable("FSIM_TCL_LIBRARY");
  if (override_path && !override_path->empty()) {
    (void)Tcl_SetVar(
        interpreter,
        "tcl_library",
        override_path->c_str(),
        TCL_GLOBAL_ONLY);
    return;
  }

#if defined(FSIM_BUNDLED_TCL_LIBRARY_RELATIVE_PATH)
  const char* executable = Tcl_GetNameOfExecutable();
  if (executable == nullptr || executable[0] == '\0') {
    return;
  }
#if defined(_WIN32)
  const std::u8string executable_utf8{
      reinterpret_cast<const char8_t*>(executable)};
  const std::filesystem::path executable_path{executable_utf8};
#else
  const std::filesystem::path executable_path{executable};
#endif
  const auto candidate =
      (executable_path.parent_path()
       / FSIM_BUNDLED_TCL_LIBRARY_RELATIVE_PATH)
          .lexically_normal();
  std::error_code error;
  if (!std::filesystem::is_regular_file(
          candidate / "init.tcl", error)) {
    return;
  }
  const auto encoded = path_utf8(candidate);
  (void)Tcl_SetVar(
      interpreter,
      "tcl_library",
      encoded.c_str(),
      TCL_GLOBAL_ONLY);
#else
  (void)interpreter;
#endif
}

int set_result(Tcl_Interp* interpreter, const std::string_view value) {
  Tcl_SetObjResult(
      interpreter,
      Tcl_NewStringObj(value.data(), tcl_size(value.size())));
  return TCL_OK;
}

int version_command(
    void*,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) noexcept {
  try {
    if (argument_count != 1) {
      Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
      return TCL_ERROR;
    }
    return set_result(
        interpreter,
        "0.1.0-dev (C API " + std::to_string(FSIM_API_VERSION) + ")");
  } catch (...) {
    Tcl_SetObjResult(
        interpreter,
        Tcl_NewStringObj("fsim::version failed", -1));
    return TCL_ERROR;
  }
}

int command_error(
    Tcl_Interp* interpreter,
    const std::string_view message) {
  Tcl_SetObjResult(
      interpreter,
      Tcl_NewStringObj(message.data(), tcl_size(message.size())));
  return TCL_ERROR;
}

int command_diagnostic_error(
    TclContext& context,
    Tcl_Interp* interpreter,
    const std::string_view fallback) {
  const auto& diagnostics = context.diagnostics.diagnostics();
  if (!diagnostics.empty()) {
    return command_error(interpreter, diagnostics.back().message);
  }
  return command_error(interpreter, fallback);
}

void dict_put(
    Tcl_Interp* interpreter,
    Tcl_Obj* dictionary,
    const std::string_view key,
    Tcl_Obj* value) {
  if (Tcl_DictObjPut(
          interpreter,
          dictionary,
          Tcl_NewStringObj(key.data(), tcl_size(key.size())),
          value)
      != TCL_OK) {
    throw std::runtime_error{"failed to construct a Tcl dictionary"};
  }
}

Tcl_Obj* string_object(const std::string_view value) {
  return Tcl_NewStringObj(value.data(), tcl_size(value.size()));
}

Tcl_Obj* unsigned_object(const std::uint64_t value) {
  return string_object(std::to_string(value));
}

enum class TclCallback : std::size_t {
  safe_point,
  value_change,
  assertion,
  lifecycle,
};

std::optional<TclCallback> callback_kind(const std::string_view name) {
  if (name == "safe_point") {
    return TclCallback::safe_point;
  }
  if (name == "value_change") {
    return TclCallback::value_change;
  }
  if (name == "assertion") {
    return TclCallback::assertion;
  }
  if (name == "lifecycle") {
    return TclCallback::lifecycle;
  }
  return std::nullopt;
}

std::string_view callback_name(const TclCallback callback) {
  switch (callback) {
    case TclCallback::safe_point:
      return "safe_point";
    case TclCallback::value_change:
      return "value_change";
    case TclCallback::assertion:
      return "assertion";
    case TclCallback::lifecycle:
      return "lifecycle";
  }
  return "unknown";
}

bool invoke_callback(
    TclContext& context,
    const TclCallback callback,
    const std::vector<std::string>& arguments) noexcept {
  try {
    const auto& prefix =
        context.callbacks.at(static_cast<std::size_t>(callback));
    if (prefix.empty() || context.callback_error) {
      return !context.callback_error;
    }
    if (context.callback_depth != 0) {
      context.callback_error =
          "recursive Tcl simulation callbacks are not allowed";
      if (context.simulation) {
        context.simulation->request_stop();
      }
      return false;
    }
    std::vector<Tcl_Obj*> objects;
    objects.reserve(prefix.size() + arguments.size());
    for (const auto& word : prefix) {
      objects.push_back(string_object(word));
    }
    for (const auto& word : arguments) {
      objects.push_back(string_object(word));
    }
    for (auto* object : objects) {
      Tcl_IncrRefCount(object);
    }
    ++context.callback_depth;
    const int result = Tcl_EvalObjv(
        context.interpreter,
        tcl_size(objects.size()),
        objects.data(),
        TCL_EVAL_GLOBAL);
    --context.callback_depth;
    for (auto* object : objects) {
      Tcl_DecrRefCount(object);
    }
    if (result != TCL_OK) {
      context.callback_error =
          "Tcl " + std::string{callback_name(callback)}
          + " callback failed: "
          + std::string{Tcl_GetStringResult(context.interpreter)};
      if (context.simulation) {
        context.simulation->request_stop();
      }
    }
    return result == TCL_OK;
  } catch (const std::exception& error) {
    try {
      context.callback_error =
          "Tcl callback dispatch failed: " + std::string{error.what()};
    } catch (...) {
    }
    if (context.simulation) {
      context.simulation->request_stop();
    }
    return false;
  } catch (...) {
    try {
      context.callback_error = "unknown Tcl callback dispatch failure";
    } catch (...) {
    }
    if (context.simulation) {
      context.simulation->request_stop();
    }
    return false;
  }
}

void attach_callbacks(TclContext& context) {
  if (context.callbacks_attached || !context.simulation) {
    return;
  }
  auto* state = &context;
  context.signal_callback_token =
      context.simulation->add_signal_change_hook(
          [state](
              const runtime::simir::SignalId signal,
              const runtime::PackedLogic4& value,
              const runtime::SimulationTick time,
              const std::uint64_t delta) {
            const auto& objects = state->simulation->design_ir().objects();
            const auto object = std::ranges::find_if(
                objects, [&](const semantic::design::Object& candidate) {
                  return candidate.kind
                          == semantic::design::ObjectKind::signal
                      && !candidate.parent_object
                      && candidate.runtime_index == signal;
                });
            const auto name = object == objects.end()
                ? std::to_string(signal)
                : object->path;
            (void)invoke_callback(
                *state,
                TclCallback::value_change,
                {
                    name,
                    value.to_msb_string(),
                    std::to_string(time),
                    std::to_string(delta),
                });
          });
  context.safe_point_callback_token =
      context.simulation->add_safe_point_hook(
          [state](
              runtime::Scheduler& scheduler,
              const runtime::SchedulerPhase phase) {
            (void)invoke_callback(
                *state,
                TclCallback::safe_point,
                {
                    std::to_string(scheduler.now()),
                    std::to_string(scheduler.delta()),
                    runtime::phase_name(phase),
                });
          });
  context.callbacks_attached = true;
}

void reset_session(TclContext& context) {
  context.debugger.reset();
  context.debug_output.reset();
  context.debug_error.reset();
  context.simulation.reset();
  context.simulation_engine.reset();
  context.built.reset();
  context.signal_callback_token = 0;
  context.safe_point_callback_token = 0;
  context.callbacks_attached = false;
  context.lifecycle_started = false;
  context.callback_error.reset();
}

std::string simulation_state(const TclContext& context) {
  if (!context.simulation) {
    return context.built ? "built" : "unbuilt";
  }
  if (context.simulation->poisoned()) {
    return "poisoned";
  }
  if (context.simulation->finished()) {
    return "finished";
  }
  return "ready";
}

bool ensure_built(TclContext& context, Tcl_Interp* interpreter) {
  if (context.built || context.simulation) {
    return true;
  }
  auto built = build_project(context.config, context.diagnostics);
  if (!built) {
    (void)command_diagnostic_error(
        context, interpreter, "fsim project build failed");
    return false;
  }
  context.built = std::move(*built);
  return true;
}

bool ensure_simulation(
    TclContext& context,
    Tcl_Interp* interpreter,
    const std::optional<SimulationEngine> requested_engine = std::nullopt) {
  if (context.simulation) {
    if (requested_engine
        && context.simulation_engine != requested_engine) {
      (void)command_error(
          interpreter,
          "the simulation is already initialized in a different execution "
          "mode; rebuild before entering Tcl debug control");
      return false;
    }
    return true;
  }
  const auto engine =
      requested_engine.value_or(SimulationEngine::compiled);
  if (!ensure_built(context, interpreter)) {
    return false;
  }
  context.simulation = std::make_unique<Simulation>(
      std::move(*context.built),
      context.config.run.max_deltas,
      engine);
  context.simulation->set_output_hook(
      [&context](
          const runtime::simir::ProcessId,
          const std::string_view text,
          const bool newline,
          const runtime::SimulationTick,
          const std::uint64_t) {
        context.output << text;
        if (newline) {
          context.output << '\n';
        }
      });
  context.simulation->set_report_hook(
      [&context](
          const runtime::simir::ProcessId process_id,
          const std::string_view message,
          const runtime::simir::AssertionSeverity severity,
          const runtime::simir::SourceLocation& source,
          const runtime::SimulationTick,
          const std::uint64_t) {
        context.output << source.path << ':' << source.line << ':'
                       << source.column << ": "
                       << assertion_severity_name(severity)
                       << "[FSIM-HDL-REPORT]: " << message << '\n';
        diagnostic::SourceSpan span;
        span.path = source.path;
        span.begin.line = source.line;
        span.begin.column = source.column;
        span.end = span.begin;
        context.diagnostics.report(diagnostic::Diagnostic{
            assertion_diagnostic_severity(severity),
            "FSIM-TCL-REPORT-0001",
            std::string{message},
            std::move(span),
            {}});
        std::string process = std::to_string(process_id);
        const auto& processes = context.simulation->design_ir().processes();
        const auto process_occurrence = std::ranges::find_if(
            processes, [&](const semantic::design::ProcessOccurrence& item) {
              return item.runtime_index == process_id;
            });
        if (process_occurrence != processes.end()) {
          process = process_occurrence->name;
        }
        (void)invoke_callback(
            context,
            TclCallback::assertion,
            {
                std::move(process),
                std::string{assertion_severity_name(severity)},
                std::string{message},
                source.path,
                std::to_string(source.line),
                std::to_string(source.column),
            });
      });
  context.built.reset();
  context.simulation_engine = engine;
  attach_callbacks(context);
  return true;
}

const semantic::design::DesignIr* current_design_ir(
    const TclContext& context) {
  if (context.simulation) {
    return &context.simulation->design_ir();
  }
  if (context.built) {
    return &context.built->design_ir;
  }
  return nullptr;
}

int project_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count == 3
      && std::string_view{Tcl_GetString(arguments[1])} == "load") {
    diagnostic::Engine load_diagnostics;
    auto loaded = project::load(
        std::filesystem::path{Tcl_GetString(arguments[2])},
        load_diagnostics);
    if (!loaded) {
      context.diagnostics.clear();
      for (const auto& entry : load_diagnostics.diagnostics()) {
        context.diagnostics.report(entry);
      }
      return command_diagnostic_error(
          context, interpreter, "fsim project load failed");
    }
    reset_session(context);
    context.diagnostics.clear();
    context.config = std::move(*loaded);
  } else if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, "?load MANIFEST?");
    return TCL_ERROR;
  }
  Tcl_Obj* result = Tcl_NewDictObj();
  dict_put(
      interpreter,
      result,
      "name",
      string_object(context.config.project.name));
  dict_put(
      interpreter,
      result,
      "top",
      string_object(context.config.project.top));
  dict_put(
      interpreter,
      result,
      "manifest",
      string_object(path_utf8(context.config.manifest_path)));
  dict_put(
      interpreter,
      result,
      "time_resolution",
      string_object(context.config.project.time_resolution));
  dict_put(
      interpreter,
      result,
      "source_sets",
      unsigned_object(context.config.source_sets.size()));
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int check_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
    return TCL_ERROR;
  }
  auto checked = check_project(context.config, context.diagnostics);
  if (!checked) {
    return command_diagnostic_error(
        context, interpreter, "fsim project check failed");
  }
  Tcl_Obj* result = Tcl_NewDictObj();
  dict_put(
      interpreter,
      result,
      "sources",
      unsigned_object(checked->source_count));
  dict_put(
      interpreter,
      result,
      "units",
      unsigned_object(checked->parsed.units.size()));
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int build_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
    return TCL_ERROR;
  }
  auto built = build_project(context.config, context.diagnostics);
  if (!built) {
    return command_diagnostic_error(
        context, interpreter, "fsim project build failed");
  }
  reset_session(context);
  context.built = std::move(*built);
  Tcl_Obj* result = Tcl_NewDictObj();
  dict_put(
      interpreter,
      result,
      "top",
      string_object(context.built->design_ir.top()));
  dict_put(
      interpreter,
      result,
      "signals",
      unsigned_object(static_cast<std::uint64_t>(std::count_if(
          context.built->design_ir.objects().begin(),
          context.built->design_ir.objects().end(),
          [](const auto& object) {
            return object.kind == semantic::design::ObjectKind::signal
                && !object.parent_object;
          }))));
  dict_put(
      interpreter,
      result,
      "processes",
      unsigned_object(context.built->design_ir.processes().size()));
  dict_put(
      interpreter,
      result,
      "cache_hit",
      Tcl_NewBooleanObj(context.built->cache_hit));
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int signals_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
    return TCL_ERROR;
  }
  if (!ensure_built(context, interpreter)) {
    return TCL_ERROR;
  }
  Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
  std::vector<std::string_view> paths;
  for (const auto& object : current_design_ir(context)->objects()) {
    const auto signal_bearing_systemc_object =
        object.kind == semantic::design::ObjectKind::systemc_port
        || object.kind == semantic::design::ObjectKind::systemc_event
        || object.kind == semantic::design::ObjectKind::systemc_signal
        || object.kind == semantic::design::ObjectKind::systemc_export;
    if (object.kind == semantic::design::ObjectKind::signal
        || (signal_bearing_systemc_object && object.width != 0)) {
      paths.push_back(object.path);
    }
  }
  std::sort(paths.begin(), paths.end());
  paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
  for (const auto path : paths) {
    if (Tcl_ListObjAppendElement(
            interpreter, result, string_object(path))
        != TCL_OK) {
      return TCL_ERROR;
    }
  }
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

std::optional<runtime::simir::SignalId> command_signal(
    TclContext& context,
    Tcl_Interp* interpreter,
    Tcl_Obj* argument) {
  if (!ensure_simulation(context, interpreter)) {
    return std::nullopt;
  }
  const std::string path{Tcl_GetString(argument)};
  const auto signal = context.simulation->find_signal(path);
  if (!signal) {
    (void)command_error(
        interpreter, "unknown signal path '" + path + "'");
  }
  return signal;
}

int read_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 2) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, "signal");
    return TCL_ERROR;
  }
  const auto signal =
      command_signal(context, interpreter, arguments[1]);
  if (!signal) {
    return TCL_ERROR;
  }
  return set_result(
      interpreter,
      context.simulation->read_signal(*signal).to_msb_string());
}

int mutate_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[],
    const std::string_view operation) {
  const bool release = operation == "release";
  if (argument_count != (release ? 2 : 3)) {
    Tcl_WrongNumArgs(
        interpreter,
        1,
        arguments,
        release ? "signal" : "signal value");
    return TCL_ERROR;
  }
  const auto signal =
      command_signal(context, interpreter, arguments[1]);
  if (!signal) {
    return TCL_ERROR;
  }
  if (release) {
    context.simulation->release_signal(*signal);
    return set_result(
        interpreter,
        context.simulation->read_signal(*signal).to_msb_string());
  }
  const auto& design_objects = context.simulation->design_ir().objects();
  const auto info = std::ranges::find_if(
      design_objects, [&](const semantic::design::Object& object) {
        return object.kind == semantic::design::ObjectKind::signal
            && !object.parent_object && object.runtime_index == *signal;
      });
  if (info == design_objects.end()) {
    return command_error(
        interpreter, "signal lacks its DesignIR runtime adapter");
  }
  std::string parse_error;
  auto value = parse_value(
      Tcl_GetString(arguments[2]), info->width, parse_error);
  if (!value) {
    return command_error(interpreter, parse_error);
  }
  if (operation == "deposit") {
    context.simulation->deposit_signal(*signal, std::move(*value));
  } else {
    context.simulation->force_signal(*signal, std::move(*value));
  }
  return set_result(
      interpreter,
      context.simulation->read_signal(*signal).to_msb_string());
}

int status_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
    return TCL_ERROR;
  }
  Tcl_Obj* result = Tcl_NewDictObj();
  dict_put(
      interpreter,
      result,
      "state",
      string_object(simulation_state(context)));
  dict_put(
      interpreter,
      result,
      "time",
      unsigned_object(
          context.simulation ? context.simulation->now() : 0));
  dict_put(
      interpreter,
      result,
      "delta",
      unsigned_object(
          context.simulation ? context.simulation->delta() : 0));
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int diagnostics_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count == 2
      && std::string_view{Tcl_GetString(arguments[1])} == "clear") {
    context.diagnostics.clear();
    return set_result(interpreter, "0");
  }
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, "?clear?");
    return TCL_ERROR;
  }
  Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
  for (const auto& entry : context.diagnostics.diagnostics()) {
    Tcl_Obj* item = Tcl_NewDictObj();
    dict_put(
        interpreter,
        item,
        "severity",
        string_object(diagnostic::to_string(entry.severity)));
    dict_put(
        interpreter, item, "code", string_object(entry.code));
    dict_put(
        interpreter, item, "message", string_object(entry.message));
    dict_put(
        interpreter, item, "path", string_object(entry.span.path));
    dict_put(
        interpreter,
        item,
        "line",
        unsigned_object(entry.span.begin.line));
    dict_put(
        interpreter,
        item,
        "column",
        unsigned_object(entry.span.begin.column));
    if (Tcl_ListObjAppendElement(interpreter, result, item) != TCL_OK) {
      return TCL_ERROR;
    }
  }
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int on_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 3) {
    Tcl_WrongNumArgs(
        interpreter, 1, arguments, "EVENT COMMAND_PREFIX");
    return TCL_ERROR;
  }
  const std::string_view event{Tcl_GetString(arguments[1])};
  const auto kind = callback_kind(event);
  if (!kind) {
    return command_error(
        interpreter,
        "callback event must be safe_point, value_change, assertion, or "
        "lifecycle");
  }
  Tcl_Size word_count{};
  Tcl_Obj** words{};
  if (Tcl_ListObjGetElements(
          interpreter, arguments[2], &word_count, &words)
      != TCL_OK) {
    return TCL_ERROR;
  }
  if (word_count == 0) {
    return command_error(
        interpreter, "callback command prefix cannot be empty");
  }
  auto& callback =
      context.callbacks.at(static_cast<std::size_t>(*kind));
  callback.clear();
  callback.reserve(static_cast<std::size_t>(word_count));
  for (Tcl_Size index = 0; index < word_count; ++index) {
    callback.emplace_back(Tcl_GetString(words[index]));
  }
  attach_callbacks(context);
  return set_result(interpreter, event);
}

int off_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 2) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, "EVENT");
    return TCL_ERROR;
  }
  const std::string_view event{Tcl_GetString(arguments[1])};
  const auto kind = callback_kind(event);
  if (!kind) {
    return command_error(
        interpreter,
        "callback event must be safe_point, value_change, assertion, or "
        "lifecycle");
  }
  context.callbacks.at(static_cast<std::size_t>(*kind)).clear();
  return set_result(interpreter, event);
}

int callbacks_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
    return TCL_ERROR;
  }
  Tcl_Obj* result = Tcl_NewDictObj();
  for (const auto kind : {
           TclCallback::safe_point,
           TclCallback::value_change,
           TclCallback::assertion,
           TclCallback::lifecycle,
       }) {
    Tcl_Obj* prefix = Tcl_NewListObj(0, nullptr);
    for (const auto& word :
         context.callbacks.at(static_cast<std::size_t>(kind))) {
      if (Tcl_ListObjAppendElement(
              interpreter, prefix, string_object(word))
          != TCL_OK) {
        return TCL_ERROR;
      }
    }
    dict_put(interpreter, result, callback_name(kind), prefix);
  }
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int stop_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
    return TCL_ERROR;
  }
  if (!ensure_simulation(context, interpreter)) {
    return TCL_ERROR;
  }
  context.simulation->request_stop();
  return set_result(interpreter, "stop_requested");
}

std::string_view assertion_severity_name(
    const runtime::simir::AssertionSeverity severity) {
  switch (severity) {
    case runtime::simir::AssertionSeverity::note:
      return "note";
    case runtime::simir::AssertionSeverity::warning:
      return "warning";
    case runtime::simir::AssertionSeverity::error:
      return "error";
    case runtime::simir::AssertionSeverity::failure:
      return "failure";
  }
  return "error";
}

diagnostic::Severity assertion_diagnostic_severity(
    const runtime::simir::AssertionSeverity severity) {
  switch (severity) {
    case runtime::simir::AssertionSeverity::note:
      return diagnostic::Severity::note;
    case runtime::simir::AssertionSeverity::warning:
      return diagnostic::Severity::warning;
    case runtime::simir::AssertionSeverity::error:
      return diagnostic::Severity::error;
    case runtime::simir::AssertionSeverity::failure:
      return diagnostic::Severity::fatal;
  }
  return diagnostic::Severity::error;
}

int report_assertion(
    TclContext& context,
    Tcl_Interp* interpreter,
    const runtime::simir::AssertionError& error) {
  const auto& source = error.source();
  if (!error.reported()) {
    diagnostic::SourceSpan span;
    span.path = source.path;
    span.begin.line = source.line;
    span.begin.column = source.column;
    span.end = span.begin;
    context.diagnostics.report(diagnostic::Diagnostic{
        assertion_diagnostic_severity(error.severity()),
        "FSIM-TCL-ASSERT-0001",
        error.what(),
        std::move(span),
        {}});
    std::string process = std::to_string(error.process());
    const auto& processes = context.simulation->design_ir().processes();
    const auto process_occurrence = std::ranges::find_if(
        processes, [&](const semantic::design::ProcessOccurrence& item) {
          return item.runtime_index == error.process();
        });
    if (process_occurrence != processes.end()) {
      process = process_occurrence->name;
    }
    (void)invoke_callback(
        context,
        TclCallback::assertion,
        {
            std::move(process),
            std::string{
                assertion_severity_name(error.severity())},
            error.what(),
            source.path,
            std::to_string(source.line),
            std::to_string(source.column),
        });
  }
  (void)invoke_callback(
      context, TclCallback::lifecycle, {"stopped"});
  if (context.callback_error) {
    auto callback_error = std::move(*context.callback_error);
    context.callback_error.reset();
    return command_error(interpreter, callback_error);
  }
  return command_error(interpreter, error.what());
}

int run_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count < 1 || argument_count > 2) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, "?until?");
    return TCL_ERROR;
  }
  if (!ensure_simulation(context, interpreter)) {
    return TCL_ERROR;
  }
  std::optional<runtime::SimulationTick> until;
  if (argument_count == 2) {
    std::string parse_error;
    until = parse_time(
        Tcl_GetString(arguments[1]),
        context.simulation->time_resolution(),
        parse_error);
    if (!until) {
      return command_error(interpreter, parse_error);
    }
  }
  if (context.simulation->finished()) {
    return command_error(interpreter, "simulation has finished");
  }
  context.simulation->clear_stop();
  if (!context.lifecycle_started) {
    context.lifecycle_started = true;
    if (!invoke_callback(
            context, TclCallback::lifecycle, {"started"})) {
      auto error = std::move(*context.callback_error);
      context.callback_error.reset();
      return command_error(interpreter, error);
    }
  }
  runtime::RunResult run;
  try {
    run = context.simulation->run(until);
  } catch (const runtime::simir::AssertionError& error) {
    return report_assertion(context, interpreter, error);
  }
  Tcl_Obj* result = Tcl_NewDictObj();
  std::string_view status = "stopped";
  if (run.status == runtime::RunStatus::completed) {
    status = "completed";
  } else if (run.status == runtime::RunStatus::time_limit) {
    status = "time_limit";
  }
  dict_put(interpreter, result, "status", string_object(status));
  dict_put(interpreter, result, "time", unsigned_object(run.time));
  dict_put(interpreter, result, "delta", unsigned_object(run.delta));
  dict_put(
      interpreter,
      result,
      "callbacks",
      unsigned_object(run.callbacks_executed));
  const std::string lifecycle_event =
      context.simulation->finished()
      ? "finished"
      : run.status == runtime::RunStatus::time_limit ? "time_limit"
                                                    : "stopped";
  (void)invoke_callback(
      context, TclCallback::lifecycle, {lifecycle_event});
  if (context.callback_error) {
    auto error = std::move(*context.callback_error);
    context.callback_error.reset();
    return command_error(interpreter, error);
  }
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int execute_debug_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const std::vector<std::string>& command) {
  if (!ensure_simulation(
          context, interpreter, SimulationEngine::debug)) {
    return TCL_ERROR;
  }
  if (!context.debugger) {
    context.debug_output =
        std::make_unique<std::ostringstream>();
    context.debug_error =
        std::make_unique<std::ostringstream>();
    context.simulation->start();
    context.debugger = std::make_unique<DebuggerControl>(
        *context.simulation,
        *context.debug_output,
        *context.debug_error,
        context.config,
        context.diagnostics);
  }

  context.debug_output->str({});
  context.debug_output->clear();
  context.debug_error->str({});
  context.debug_error->clear();
  context.debugger->execute(command);
  if (context.callback_error) {
    auto callback_error = std::move(*context.callback_error);
    context.callback_error.reset();
    return command_error(interpreter, callback_error);
  }
  const auto error = context.debug_error->str();
  if (!error.empty()) {
    return command_error(interpreter, error);
  }
  auto output = context.debug_output->str();
  while (!output.empty()
         && (output.back() == '\n' || output.back() == '\r')) {
    output.pop_back();
  }
  return set_result(interpreter, output);
}

int debug_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count < 2) {
    Tcl_WrongNumArgs(
        interpreter, 1, arguments, "command ?argument ...?");
    return TCL_ERROR;
  }
  std::vector<std::string> command;
  command.reserve(static_cast<std::size_t>(argument_count - 1));
  for (Tcl_Size index = 1; index < argument_count; ++index) {
    command.emplace_back(Tcl_GetString(arguments[index]));
  }
  return execute_debug_command(context, interpreter, command);
}

int trace_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count < 2) {
    Tcl_WrongNumArgs(
        interpreter, 1, arguments, "add|remove SIGNAL | all|clear|list");
    return TCL_ERROR;
  }
  const std::string_view operation{Tcl_GetString(arguments[1])};
  if (operation == "configure") {
    if (argument_count < 3) {
      Tcl_WrongNumArgs(
          interpreter, 2, arguments, "FILE ?FILTER ...?");
      return TCL_ERROR;
    }
    if (context.simulation) {
      return command_error(
          interpreter,
          "trace output must be configured before simulation starts");
    }
    std::filesystem::path path{Tcl_GetString(arguments[2])};
    if (path.is_relative()) {
      path = context.config.base_directory / path;
    }
    context.config.run.trace_file = path.lexically_normal();
    context.config.run.trace_filters.clear();
    for (Tcl_Size index = 3; index < argument_count; ++index) {
      context.config.run.trace_filters.emplace_back(
          Tcl_GetString(arguments[index]));
    }
    return set_result(
        interpreter,
        path_utf8(*context.config.run.trace_file));
  }
  if (operation == "disable" && argument_count == 2) {
    if (context.simulation) {
      return command_error(
          interpreter,
          "trace output must be disabled before simulation starts");
    }
    context.config.run.trace_file.reset();
    context.config.run.trace_filters.clear();
    return set_result(interpreter, "");
  }
  if (operation == "status" && argument_count == 2) {
    Tcl_Obj* result = Tcl_NewDictObj();
    dict_put(
        interpreter,
        result,
        "file",
        string_object(
            context.config.run.trace_file
                ? path_utf8(*context.config.run.trace_file)
                : std::string{}));
    Tcl_Obj* filters = Tcl_NewListObj(0, nullptr);
    for (const auto& filter : context.config.run.trace_filters) {
      if (Tcl_ListObjAppendElement(
              interpreter, filters, string_object(filter))
          != TCL_OK) {
        return TCL_ERROR;
      }
    }
    dict_put(interpreter, result, "filters", filters);
    Tcl_SetObjResult(interpreter, result);
    return TCL_OK;
  }
  std::vector<std::string> command{"trace"};
  command.reserve(static_cast<std::size_t>(argument_count));
  for (Tcl_Size index = 1; index < argument_count; ++index) {
    command.emplace_back(Tcl_GetString(arguments[index]));
  }
  return execute_debug_command(context, interpreter, command);
}

int fsim_command(
    void* client_data,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) noexcept {
  auto& context = *static_cast<TclContext*>(client_data);
  try {
    const std::string_view command{Tcl_GetString(arguments[0])};
    if (context.callback_depth != 0
        && command != "::fsim::project"
        && command != "fsim::project"
        && command != "::fsim::signals"
        && command != "fsim::signals"
        && command != "::fsim::read"
        && command != "fsim::read"
        && command != "::fsim::status"
        && command != "fsim::status"
        && command != "::fsim::diagnostics"
        && command != "fsim::diagnostics"
        && command != "::fsim::stop"
        && command != "fsim::stop") {
      return command_error(
          interpreter,
          "this fsim command is not safe inside a simulation callback");
    }
    if (command == "::fsim::project" || command == "fsim::project") {
      return project_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::check" || command == "fsim::check") {
      return check_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::build" || command == "fsim::build") {
      return build_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::signals" || command == "fsim::signals") {
      return signals_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::read" || command == "fsim::read") {
      return read_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::deposit" || command == "fsim::deposit") {
      return mutate_command(
          context,
          interpreter,
          argument_count,
          arguments,
          "deposit");
    }
    if (command == "::fsim::force" || command == "fsim::force") {
      return mutate_command(
          context,
          interpreter,
          argument_count,
          arguments,
          "force");
    }
    if (command == "::fsim::release" || command == "fsim::release") {
      return mutate_command(
          context,
          interpreter,
          argument_count,
          arguments,
          "release");
    }
    if (command == "::fsim::run" || command == "fsim::run") {
      return run_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::status" || command == "fsim::status") {
      return status_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::diagnostics"
        || command == "fsim::diagnostics") {
      return diagnostics_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::on" || command == "fsim::on") {
      return on_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::off" || command == "fsim::off") {
      return off_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::callbacks"
        || command == "fsim::callbacks") {
      return callbacks_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::stop" || command == "fsim::stop") {
      return stop_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::trace" || command == "fsim::trace") {
      return trace_command(
          context, interpreter, argument_count, arguments);
    }
    if (command == "::fsim::debug" || command == "fsim::debug") {
      return debug_command(
          context, interpreter, argument_count, arguments);
    }
    return command_error(interpreter, "unknown fsim Tcl command");
  } catch (const std::exception& exception) {
    return command_error(interpreter, exception.what());
  } catch (...) {
    return command_error(
        interpreter, "unknown failure in an fsim Tcl command");
  }
}

int exit_command(
    void* client_data,
    Tcl_Interp* interpreter,
    const Tcl_Size argument_count,
    Tcl_Obj* const arguments[]) noexcept {
  auto& context = *static_cast<TclContext*>(client_data);
  try {
    if (argument_count > 2) {
      Tcl_WrongNumArgs(interpreter, 1, arguments, "?returnCode?");
      return TCL_ERROR;
    }
    int code = 0;
    if (argument_count == 2
        && Tcl_GetIntFromObj(interpreter, arguments[1], &code) != TCL_OK) {
      return TCL_ERROR;
    }
    if (code < 0 || code > 255) {
      Tcl_SetObjResult(
          interpreter,
          Tcl_NewStringObj("exit code must be between 0 and 255", -1));
      return TCL_ERROR;
    }
    context.exit_requested = true;
    context.exit_code = code;
    return TCL_RETURN;
  } catch (...) {
    Tcl_SetObjResult(
        interpreter,
        Tcl_NewStringObj("Tcl exit handling failed", -1));
    return TCL_ERROR;
  }
}

bool set_global(
    Tcl_Interp* interpreter,
    const char* name,
    Tcl_Obj* value) {
  return Tcl_SetVar2Ex(
             interpreter, name, nullptr, value, TCL_GLOBAL_ONLY)
      != nullptr;
}

bool initialize_arguments(
    Tcl_Interp* interpreter,
    const cli::Invocation& invocation) {
  Tcl_Obj* arguments = Tcl_NewListObj(0, nullptr);
  Tcl_IncrRefCount(arguments);
  for (const auto& argument : invocation.tcl_arguments) {
    if (Tcl_ListObjAppendElement(
            interpreter,
            arguments,
            Tcl_NewStringObj(
                argument.data(), tcl_size(argument.size())))
        != TCL_OK) {
      Tcl_DecrRefCount(arguments);
      return false;
    }
  }
  const bool success =
      set_global(
          interpreter,
          "argv0",
          Tcl_NewStringObj(
              invocation.tcl_script
                  ? path_utf8(*invocation.tcl_script).c_str()
                  : invocation.program_name.c_str(),
              -1))
      && set_global(interpreter, "argv", arguments)
      && set_global(
          interpreter,
          "argc",
          Tcl_NewWideIntObj(
              tcl_wide_size(invocation.tcl_arguments.size())))
      && set_global(
          interpreter,
          "tcl_interactive",
          Tcl_NewBooleanObj(
              !invocation.tcl_script
              && invocation.tcl_commands.empty()));
  Tcl_DecrRefCount(arguments);
  return success;
}

std::string interpreter_result(Tcl_Interp* interpreter) {
  const char* text = Tcl_GetStringResult(interpreter);
  return text == nullptr ? std::string{} : std::string{text};
}

void report_evaluation_error(
    diagnostic::Engine& diagnostics,
    Tcl_Interp* interpreter,
    const std::string_view source) {
  std::string message{"Tcl evaluation failed"};
  const auto result = interpreter_result(interpreter);
  if (!result.empty()) {
    message += ": " + result;
  }
  const char* details =
      Tcl_GetVar(interpreter, "errorInfo", TCL_GLOBAL_ONLY);
  if (details != nullptr && result != details) {
    message += "\n";
    message += details;
  }
  diagnostics.error(
      "FSIM-TCL-0003",
      std::move(message),
      {std::string{source}, {1, 1, 0}, {1, 1, 0}});
}

int evaluate_batch(
    Tcl_Interp* interpreter,
    TclContext& context,
    diagnostic::Engine& diagnostics) {
  if (context.invocation.tcl_script) {
    const auto script = path_utf8(*context.invocation.tcl_script);
    const int result = Tcl_EvalFile(interpreter, script.c_str());
    if (context.exit_requested) {
      return context.exit_code;
    }
    if (result != TCL_OK) {
      report_evaluation_error(diagnostics, interpreter, script);
      return kUserError;
    }
    return kSuccess;
  }

  for (std::size_t index = 0;
       index < context.invocation.tcl_commands.size();
       ++index) {
    const auto& command = context.invocation.tcl_commands[index];
    const int result = Tcl_EvalEx(
        interpreter,
        command.data(),
        tcl_size(command.size()),
        TCL_EVAL_GLOBAL);
    if (context.exit_requested) {
      return context.exit_code;
    }
    if (result != TCL_OK) {
      report_evaluation_error(
          diagnostics,
          interpreter,
          "<command:" + std::to_string(index + 1) + ">");
      return kUserError;
    }
  }
  return kSuccess;
}

int evaluate_interactive(
    Tcl_Interp* interpreter,
    TclContext& context,
    diagnostic::Engine& diagnostics,
    std::istream& input) {
  bool had_error = false;
  std::string command;
  std::string line;
  while (!context.exit_requested) {
    context.output << (command.empty() ? "(fsim:tcl) " : "... ");
    context.output.flush();
    if (!std::getline(input, line)) {
      if (!command.empty()) {
        diagnostics.error(
            "FSIM-TCL-0004",
            "incomplete Tcl command at end of input",
            {"<interactive>", {1, 1, 0}, {1, 1, 0}});
        had_error = true;
      }
      break;
    }
    command += line;
    command.push_back('\n');
    if (Tcl_CommandComplete(command.c_str()) == 0) {
      continue;
    }
    const int result = Tcl_EvalEx(
        interpreter,
        command.data(),
        tcl_size(command.size()),
        TCL_EVAL_GLOBAL);
    if (context.exit_requested) {
      break;
    }
    if (result != TCL_OK) {
      const auto text = interpreter_result(interpreter);
      context.error << (text.empty() ? "Tcl evaluation failed" : text)
                    << '\n';
      had_error = true;
    } else {
      const auto text = interpreter_result(interpreter);
      if (!text.empty()) {
        context.output << text << '\n';
      }
    }
    Tcl_ResetResult(interpreter);
    command.clear();
  }
  if (context.exit_requested) {
    return context.exit_code;
  }
  return had_error ? kUserError : kSuccess;
}

#endif

}  // namespace

int handle_tcl(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::istream& input,
    std::ostream& output,
    std::ostream& error) {
#if !defined(FSIM_HAS_TCL)
  (void)invocation;
  (void)config;
  (void)input;
  (void)output;
  (void)error;
  diagnostics.error(
      "FSIM-TCL-0001",
      "the Tcl interface is unavailable in this build; configure with "
      "-DFSIM_TCL_MODE=ON to discover or download Tcl");
  return kUnavailable;
#else
  const auto executable = path_utf8(invocation.program_path);
  Tcl_FindExecutable(executable.c_str());
  TclInterpreter interpreter{Tcl_CreateInterp()};
  if (!interpreter) {
    diagnostics.error(
        "FSIM-TCL-0002", "failed to create the Tcl interpreter");
    return kUnavailable;
  }
  configure_tcl_library(interpreter.get());
  if (Tcl_Init(interpreter.get()) != TCL_OK) {
    diagnostics.error(
        "FSIM-TCL-0002",
        "failed to initialize the Tcl standard library: "
            + interpreter_result(interpreter.get()));
    return kUnavailable;
  }

  TclStreamChannels channels{
      interpreter.get(), input, output, error};
  if (!channels.valid()) {
    diagnostics.error(
        "FSIM-TCL-0002",
        "failed to connect Tcl standard channels to the fsim command");
    return kUnavailable;
  }

  TclContext context{
      invocation,
      config,
      diagnostics,
      interpreter.get(),
      output,
      error,
      std::nullopt,
      nullptr,
      std::nullopt,
      nullptr,
      nullptr,
      nullptr,
      {},
      0,
      0,
      0,
      false,
      false,
      std::nullopt,
      false,
      0};
  if (Tcl_CreateNamespace(
          interpreter.get(), "::fsim", nullptr, nullptr)
      == nullptr) {
    diagnostics.error(
        "FSIM-TCL-0002",
        "failed to create the fsim Tcl namespace: "
            + interpreter_result(interpreter.get()));
    return kUnavailable;
  }
  constexpr std::array fsim_commands{
      "::fsim::project",
      "::fsim::check",
      "::fsim::build",
      "::fsim::signals",
      "::fsim::read",
      "::fsim::deposit",
      "::fsim::force",
      "::fsim::release",
      "::fsim::run",
      "::fsim::status",
      "::fsim::diagnostics",
      "::fsim::on",
      "::fsim::off",
      "::fsim::callbacks",
      "::fsim::stop",
      "::fsim::trace",
      "::fsim::debug"};
  bool commands_ok = true;
  for (const char* command : fsim_commands) {
    if (Tcl_CreateObjCommand2(
            interpreter.get(),
            command,
            fsim_command,
            &context,
            nullptr)
        == nullptr) {
      commands_ok = false;
      break;
    }
  }
  if (Tcl_CreateObjCommand2(
          interpreter.get(),
          "::fsim::version",
          version_command,
          &context,
          nullptr)
          == nullptr
      || Tcl_CreateObjCommand2(
             interpreter.get(),
             "exit",
             exit_command,
             &context,
             nullptr)
          == nullptr
      || !commands_ok
      || !initialize_arguments(interpreter.get(), invocation)) {
    diagnostics.error(
        "FSIM-TCL-0002",
        "failed to initialize fsim Tcl commands and variables: "
            + interpreter_result(interpreter.get()));
    return kUnavailable;
  }

  if (invocation.tcl_script || !invocation.tcl_commands.empty()) {
    return evaluate_batch(interpreter.get(), context, diagnostics);
  }
  return evaluate_interactive(
      interpreter.get(), context, diagnostics, input);
#endif
}

}  // namespace fsim::app
