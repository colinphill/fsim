// SPDX-License-Identifier: Apache-2.0
#include "tcl.hpp"

#include "fsim/api.h"
#include "fsim/app/application.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <limits>
#include <memory>
#include <optional>
#include <ostream>
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
  const project::Config& config;
  diagnostic::Engine& diagnostics;
  std::ostream& output;
  std::ostream& error;
  std::optional<BuiltProject> built;
  std::unique_ptr<Simulation> simulation;
  bool exit_requested{};
  int exit_code{};
};

struct TclChannelState {
  std::istream* input{};
  std::ostream* output{};
};

int channel_close(void*, Tcl_Interp*) noexcept {
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
    TCL_CHANNEL_VERSION_2,
    channel_close,
    channel_input,
    channel_output,
    nullptr,
    nullptr,
    nullptr,
    channel_watch,
    channel_get_handle,
    nullptr,
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

int set_result(Tcl_Interp* interpreter, const std::string_view value) {
  Tcl_SetObjResult(
      interpreter,
      Tcl_NewStringObj(
          value.data(), static_cast<int>(value.size())));
  return TCL_OK;
}

int version_command(
    void*,
    Tcl_Interp* interpreter,
    const int argument_count,
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
      Tcl_NewStringObj(
          message.data(), static_cast<int>(message.size())));
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
          Tcl_NewStringObj(
              key.data(), static_cast<int>(key.size())),
          value)
      != TCL_OK) {
    throw std::runtime_error{"failed to construct a Tcl dictionary"};
  }
}

Tcl_Obj* string_object(const std::string_view value) {
  return Tcl_NewStringObj(
      value.data(), static_cast<int>(value.size()));
}

Tcl_Obj* unsigned_object(const std::uint64_t value) {
  return string_object(std::to_string(value));
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
    Tcl_Interp* interpreter) {
  if (context.simulation) {
    return true;
  }
  if (!ensure_built(context, interpreter)) {
    return false;
  }
  context.simulation = std::make_unique<Simulation>(
      std::move(*context.built),
      context.config.run.max_deltas,
      SimulationEngine::compiled);
  context.built.reset();
  return true;
}

const elaboration::ElaboratedDesign* current_design(
    const TclContext& context) {
  if (context.simulation) {
    return &context.simulation->design();
  }
  if (context.built) {
    return &context.built->design;
  }
  return nullptr;
}

int project_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const int argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
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
    const int argument_count,
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
    const int argument_count,
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
  context.simulation.reset();
  context.built = std::move(*built);
  Tcl_Obj* result = Tcl_NewDictObj();
  dict_put(
      interpreter,
      result,
      "top",
      string_object(context.built->design.top()));
  dict_put(
      interpreter,
      result,
      "signals",
      unsigned_object(context.built->design.signals().size()));
  dict_put(
      interpreter,
      result,
      "processes",
      unsigned_object(context.built->design.processes().size()));
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
    const int argument_count,
    Tcl_Obj* const arguments[]) {
  if (argument_count != 1) {
    Tcl_WrongNumArgs(interpreter, 1, arguments, nullptr);
    return TCL_ERROR;
  }
  if (!ensure_built(context, interpreter)) {
    return TCL_ERROR;
  }
  Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
  for (const auto& [path, unused] : current_design(context)->signal_paths()) {
    (void)unused;
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
    const int argument_count,
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
    const int argument_count,
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
  const auto& info = context.simulation->design().signals().at(*signal);
  std::string parse_error;
  auto value = parse_value(
      Tcl_GetString(arguments[2]), info.width, parse_error);
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
    const int argument_count,
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

int run_command(
    TclContext& context,
    Tcl_Interp* interpreter,
    const int argument_count,
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
  const auto run = context.simulation->run(until);
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
  Tcl_SetObjResult(interpreter, result);
  return TCL_OK;
}

int fsim_command(
    void* client_data,
    Tcl_Interp* interpreter,
    const int argument_count,
    Tcl_Obj* const arguments[]) noexcept {
  auto& context = *static_cast<TclContext*>(client_data);
  try {
    const std::string_view command{Tcl_GetString(arguments[0])};
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
    const int argument_count,
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
                argument.data(), static_cast<int>(argument.size())))
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
          Tcl_NewIntObj(
              static_cast<int>(invocation.tcl_arguments.size())))
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
        static_cast<int>(command.size()),
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
        static_cast<int>(command.size()),
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
  Tcl_FindExecutable(invocation.program_name.c_str());
  TclInterpreter interpreter{Tcl_CreateInterp()};
  if (!interpreter) {
    diagnostics.error(
        "FSIM-TCL-0002", "failed to create the Tcl interpreter");
    return kUnavailable;
  }
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
      output,
      error,
      std::nullopt,
      nullptr,
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
      "::fsim::status"};
  bool commands_ok = true;
  for (const char* command : fsim_commands) {
    if (Tcl_CreateObjCommand(
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
  if (Tcl_CreateObjCommand(
          interpreter.get(),
          "::fsim::version",
          version_command,
          &context,
          nullptr)
          == nullptr
      || Tcl_CreateObjCommand(
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
