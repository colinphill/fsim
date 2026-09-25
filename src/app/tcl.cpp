// SPDX-License-Identifier: Apache-2.0
#include "tcl.hpp"
#include "tcl_command_catalog.hpp"
#include "tcl_completion_adapter.hpp"
#include "tcl_console.hpp"
#include "tcl_internal.hpp"

#include "fsim/api.h"
#include "fsim/app/application.hpp"
#include "fsim/app/sdf_control.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/support/path.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
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
namespace tcl_detail {

    constexpr int kUnavailable = 3;

#if defined(FSIM_HAS_TCL)

    constexpr int kSuccess = 0;
    constexpr int kUserError = 1;

    static_assert(
        TCL_MAJOR_VERSION == 9 && TCL_MINOR_VERSION == 0,
        "fsim requires Tcl 9.0");

    Tcl_Size tcl_size(const std::size_t value)
    {
        if (value > static_cast<std::size_t>(TCL_SIZE_MAX)) {
            throw std::length_error { "value exceeds Tcl_Size" };
        }
        return static_cast<Tcl_Size>(value);
    }

    Tcl_WideInt tcl_wide_size(const std::size_t value)
    {
        if (value
            > static_cast<std::size_t>(
                std::numeric_limits<Tcl_WideInt>::max())) {
            throw std::length_error { "value exceeds Tcl_WideInt" };
        }
        return static_cast<Tcl_WideInt>(value);
    }

    struct TclInterpreterDeleter {
        void operator()(Tcl_Interp* interpreter) const noexcept
        {
            if (interpreter != nullptr) {
                Tcl_DeleteInterp(interpreter);
            }
        }
    };

    using TclInterpreter = std::unique_ptr<Tcl_Interp, TclInterpreterDeleter>;

    std::string_view assertion_severity_name(
        runtime::simir::AssertionSeverity severity);
    diagnostic::Severity assertion_diagnostic_severity(
        runtime::simir::AssertionSeverity severity);

    void catalog_changed(TclContext& context, const std::string_view library,
        const TclCatalogMutation mutation)
    {
        if (context.references) {
            context.references->catalog_mutation_completed(
                library, mutation, true);
        }
    }

    struct TclCdCommandState {
        TclContext* context { };
        Tcl_CmdProc* native_string_proc { };
        void* native_string_client_data { };
        Tcl_ObjCmdProc* native_object_proc { };
        void* native_object_client_data { };
#if TCL_MAJOR_VERSION > 8
        Tcl_ObjCmdProc2* native_object_proc2 { };
        void* native_object_client_data2 { };
#endif
    };

    int reject_callback_cd(Tcl_Interp* interpreter) noexcept
    {
        Tcl_SetObjResult(
            interpreter,
            Tcl_NewStringObj(
                "Tcl cd is not allowed inside a simulation callback", -1));
        Tcl_SetErrorCode(
            interpreter, "FSIM", "TCL", "WORKSPACE", "CALLBACK", nullptr);
        return TCL_ERROR;
    }

    bool callback_cd_is_allowed(
        const TclCdCommandState& state, Tcl_Interp* interpreter) noexcept
    {
        if (state.context != nullptr && state.context->callback_depth == 0) {
            return true;
        }
        if (state.context == nullptr) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("Tcl cd guard is not initialized", -1));
            return false;
        }
        (void)reject_callback_cd(interpreter);
        return false;
    }

    int synchronize_after_cd(
        TclCdCommandState& state,
        Tcl_Interp* interpreter,
        const int command_result) noexcept
    {
        if (command_result != TCL_OK) {
            return command_result;
        }
        try {
            return synchronize_workspace(*state.context, interpreter)
                ? TCL_OK
                : TCL_ERROR;
        } catch (const std::exception& exception) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj(exception.what(), -1));
        } catch (...) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("failed to synchronize the Tcl workspace", -1));
        }
        return TCL_ERROR;
    }

    int guarded_cd_string_command(
        void* client_data,
        Tcl_Interp* interpreter,
        const int argument_count,
        const char* arguments[]) noexcept
    {
        auto& state = *static_cast<TclCdCommandState*>(client_data);
        if (!callback_cd_is_allowed(state, interpreter)) {
            return TCL_ERROR;
        }
        const int result = state.native_string_proc(
            state.native_string_client_data,
            interpreter,
            argument_count,
            arguments);
        return synchronize_after_cd(state, interpreter, result);
    }

    int guarded_cd_object_command(
        void* client_data,
        Tcl_Interp* interpreter,
        const int argument_count,
        Tcl_Obj* const arguments[]) noexcept
    {
        auto& state = *static_cast<TclCdCommandState*>(client_data);
        if (!callback_cd_is_allowed(state, interpreter)) {
            return TCL_ERROR;
        }
        const int result = state.native_object_proc(
            state.native_object_client_data,
            interpreter,
            argument_count,
            arguments);
        return synchronize_after_cd(state, interpreter, result);
    }

#if TCL_MAJOR_VERSION > 8
    int guarded_cd_object_command2(
        void* client_data,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[]) noexcept
    {
        auto& state = *static_cast<TclCdCommandState*>(client_data);
        if (!callback_cd_is_allowed(state, interpreter)) {
            return TCL_ERROR;
        }
        const int result = state.native_object_proc2(
            state.native_object_client_data2,
            interpreter,
            argument_count,
            arguments);
        return synchronize_after_cd(state, interpreter, result);
    }
#endif

    bool install_cd_callback_guard(
        Tcl_Interp* interpreter,
        TclContext& context,
        TclCdCommandState& state)
    {
        Tcl_CmdInfo command_info { };
        if (Tcl_GetCommandInfo(interpreter, "::cd", &command_info) == 0) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("Tcl's built-in cd command is unavailable", -1));
            return false;
        }
        state.context = &context;
        if (command_info.isNativeObjectProc == 1) {
            state.native_object_proc = command_info.objProc;
            state.native_object_client_data = command_info.objClientData;
            if (state.native_object_proc == nullptr) {
                Tcl_SetObjResult(interpreter,
                    Tcl_NewStringObj("Tcl's built-in cd object command is invalid", -1));
                return false;
            }
            command_info.objProc = guarded_cd_object_command;
            command_info.objClientData = &state;
        } else if (command_info.isNativeObjectProc == 0) {
            state.native_string_proc = command_info.proc;
            state.native_string_client_data = command_info.clientData;
            if (state.native_string_proc == nullptr) {
                Tcl_SetObjResult(interpreter,
                    Tcl_NewStringObj("Tcl's built-in cd string command is invalid", -1));
                return false;
            }
            command_info.proc = guarded_cd_string_command;
            command_info.clientData = &state;
#if TCL_MAJOR_VERSION > 8
        } else if (command_info.isNativeObjectProc == 2) {
            state.native_object_proc2 = command_info.objProc2;
            state.native_object_client_data2 = command_info.objClientData2;
            if (state.native_object_proc2 == nullptr) {
                Tcl_SetObjResult(interpreter,
                    Tcl_NewStringObj("Tcl's built-in cd object2 command is invalid", -1));
                return false;
            }
            command_info.objProc2 = guarded_cd_object_command2;
            command_info.objClientData2 = &state;
#endif
        } else {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("Tcl's built-in cd command type is unsupported", -1));
            return false;
        }
        if (Tcl_SetCommandInfo(interpreter, "::cd", &command_info) == 0) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("failed to install the Tcl cd guard", -1));
            return false;
        }
        return true;
    }

    struct TclChannelState {
        std::istream* input { };
        std::ostream* output { };
    };

    int channel_close(void*, Tcl_Interp*, int) noexcept
    {
        return 0;
    }

    int channel_input(
        void* client_data,
        char* buffer,
        const int count,
        int* error_code) noexcept
    {
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
        int* error_code) noexcept
    {
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

    int channel_flush(void* client_data) noexcept
    {
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

    void channel_watch(void*, int) noexcept { }

    int channel_get_handle(void*, int, void**) noexcept
    {
        return TCL_ERROR;
    }

    const Tcl_ChannelType kStreamChannelType {
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
        nullptr
    };

    class TclStreamChannels final {
    public:
        TclStreamChannels(
            Tcl_Interp* interpreter,
            std::istream& input,
            std::ostream& output,
            std::ostream& error)
            : interpreter_(interpreter)
            , input_state_ { &input, nullptr }
            , output_state_ { nullptr, &output }
            , error_state_ { nullptr, &error }
            , previous_input_(Tcl_GetStdChannel(TCL_STDIN))
            , previous_output_(Tcl_GetStdChannel(TCL_STDOUT))
            , previous_error_(Tcl_GetStdChannel(TCL_STDERR))
        {
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

        ~TclStreamChannels()
        {
            cleanup();
        }

        TclStreamChannels(const TclStreamChannels&) = delete;
        TclStreamChannels& operator=(const TclStreamChannels&) = delete;

        [[nodiscard]] bool valid() const noexcept
        {
            return valid_;
        }

    private:
        void cleanup() noexcept
        {
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

        Tcl_Interp* interpreter_ { };
        TclChannelState input_state_;
        TclChannelState output_state_;
        TclChannelState error_state_;
        Tcl_Channel previous_input_ { };
        Tcl_Channel previous_output_ { };
        Tcl_Channel previous_error_ { };
        Tcl_Channel input_ { };
        Tcl_Channel output_ { };
        Tcl_Channel error_ { };
        bool valid_ { };
    };

    void configure_tcl_library(Tcl_Interp* interpreter)
    {
        const auto override_path = fsim::support::environment_variable("FSIM_TCL_LIBRARY");
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
        const std::u8string executable_utf8 {
            reinterpret_cast<const char8_t*>(executable)
        };
        const std::filesystem::path executable_path { executable_utf8 };
#else
        const std::filesystem::path executable_path { executable };
#endif
        const auto candidate = (executable_path.parent_path()
            / FSIM_BUNDLED_TCL_LIBRARY_RELATIVE_PATH)
                                   .lexically_normal();
        std::error_code error;
        if (!std::filesystem::is_regular_file(
                candidate / "init.tcl", error)) {
            return;
        }
        const auto encoded = fsim::support::path_to_utf8(candidate);
        (void)Tcl_SetVar(
            interpreter,
            "tcl_library",
            encoded.c_str(),
            TCL_GLOBAL_ONLY);
#else
        (void)interpreter;
#endif
    }


    int exit_command(
        void* client_data,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[]) noexcept
    {
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
        Tcl_Obj* value)
    {
        return Tcl_SetVar2Ex(
                   interpreter, name, nullptr, value, TCL_GLOBAL_ONLY)
            != nullptr;
    }

    bool initialize_arguments(
        Tcl_Interp* interpreter,
        const cli::Invocation& invocation)
    {
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
        const bool success = set_global(
                                 interpreter,
                                 "argv0",
                                 Tcl_NewStringObj(
                                     invocation.tcl_script
                                         ? fsim::support::path_to_utf8(*invocation.tcl_script).c_str()
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

    std::string interpreter_result(Tcl_Interp* interpreter)
    {
        const char* text = Tcl_GetStringResult(interpreter);
        return text == nullptr ? std::string { } : std::string { text };
    }

    void report_evaluation_error(
        diagnostic::Engine& diagnostics,
        Tcl_Interp* interpreter,
        const std::string_view source)
    {
        std::string message { "Tcl evaluation failed" };
        const auto result = interpreter_result(interpreter);
        if (!result.empty()) {
            message += ": " + result;
        }
        const char* details = Tcl_GetVar(interpreter, "errorInfo", TCL_GLOBAL_ONLY);
        if (details != nullptr && result != details) {
            message += "\n";
            message += details;
        }
        diagnostics.error(
            "FSIM-TCL-0003",
            std::move(message),
            { std::string { source }, { 1, 1, 0 }, { 1, 1, 0 } });
    }

    int evaluate_batch(
        Tcl_Interp* interpreter,
        TclContext& context,
        diagnostic::Engine& diagnostics)
    {
        if (context.invocation.tcl_script) {
            const auto script = fsim::support::path_to_utf8(*context.invocation.tcl_script);
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
        std::istream& input)
    {
        bool had_error = false;
        TclConsole::Options options;
        options.input = &input;
        options.output = &context.output;
        options.error = &context.error;
        options.workspace_root = context.config.base_directory;
        options.color_mode = context.invocation.color_mode;
        options.command_complete = [](const std::string_view text) {
            const std::string command { text };
            return Tcl_CommandComplete(command.c_str()) != 0;
        };
        options.completion_query = [&context](const std::string_view text,
                                       const std::size_t cursor_byte) {
            if (!synchronize_workspace(context, context.interpreter)) {
                Tcl_ResetResult(context.interpreter);
                tcl_completion::CompletionResult failed;
                failed.status = tcl_completion::CompletionStatus::stale_snapshot;
                return failed;
            }
            return complete_tcl(context, text, cursor_byte);
        };
        options.live_generations = [&context] {
            if (!synchronize_workspace(context, context.interpreter)) {
                Tcl_ResetResult(context.interpreter);
                return tcl_completion::Generations { };
            }
            return tcl_completion_generations(context);
        };
        if (const char* history = std::getenv("FSIM_TCL_HISTORY")) {
            if (std::string_view { history } == "off")
                options.persist_history = false;
            else if (*history != '\0')
                options.history_path = history;
        }
        if (const char* limit = std::getenv("FSIM_TCL_HISTORY_LIMIT")) {
            std::size_t value { };
            const std::string_view text { limit };
            const auto parsed = std::from_chars(
                text.data(), text.data() + text.size(), value);
            if (parsed.ec == std::errc { }
                && parsed.ptr == text.data() + text.size())
                options.history_limit = std::min<std::size_t>(value, 10000U);
        }
        auto console_workspace = options.workspace_root;
        auto console = std::make_unique<TclConsole>(options);
        while (!context.exit_requested) {
            if (!synchronize_workspace(context, interpreter))
                return kUserError;
            if (context.config.base_directory != console_workspace) {
                console_workspace = context.config.base_directory;
                options.workspace_root = console_workspace;
                console = std::make_unique<TclConsole>(options);
            }
            auto command = console->read_command();
            if (command.status == TclConsoleReadStatus::cancelled
                || command.status == TclConsoleReadStatus::interrupted)
                continue;
            if (command.status == TclConsoleReadStatus::error) {
                diagnostics.error("FSIM-TCL-0004", "Tcl console input failed");
                return kUserError;
            }
            if (command.status == TclConsoleReadStatus::end_of_file) {
                if (!command.text.empty()) {
                    diagnostic::Diagnostic value { diagnostic::Severity::error,
                        "FSIM-TCL-0004",
                        "incomplete Tcl command at end of input",
                        { "<interactive>", { 1, 1, 0 }, { 1, 1, 0 } }, { } };
                    diagnostics.report(value);
                    had_error = true;
                }
                break;
            }
            const int result = Tcl_EvalEx(
                interpreter,
                command.text.data(),
                tcl_size(command.text.size()),
                TCL_EVAL_GLOBAL);
            if (context.exit_requested) {
                break;
            }
            if (result != TCL_OK) {
                const auto text = interpreter_result(interpreter);
                console->write_diagnostic({ diagnostic::Severity::error,
                    "FSIM-TCL-0005",
                    text.empty() ? "Tcl evaluation failed" : text,
                    { "<interactive>", { 1, 1, 0 }, { 1, 1, 0 } }, { } });
                had_error = true;
            } else {
                const auto text = interpreter_result(interpreter);
                if (!text.empty()) {
                    console->write_output(text + '\n');
                }
            }
            Tcl_ResetResult(interpreter);
        }
        if (context.exit_requested) {
            return context.exit_code;
        }
        return had_error ? kUserError : kSuccess;
    }

#endif

} // namespace tcl_detail

using namespace tcl_detail;

int handle_tcl(
    const cli::Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::istream& input,
    std::ostream& output,
    std::ostream& error)
{
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
    const auto executable = fsim::support::path_to_utf8(invocation.program_path);
    Tcl_FindExecutable(executable.c_str());
    TclCdCommandState cd_command_state;
    TclInterpreter interpreter { Tcl_CreateInterp() };
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

    TclStreamChannels channels {
        interpreter.get(), input, output, error
    };
    if (!channels.valid()) {
        diagnostics.error(
            "FSIM-TCL-0002",
            "failed to connect Tcl standard channels to the fsim command");
        return kUnavailable;
    }

    TclContext context {
        invocation,
        config,
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
        { },
        0,
        0,
        0,
        false,
        false,
        std::nullopt,
        false,
        0,
        { },
        nullptr,
        nullptr
    };
    context.references = std::make_unique<TclReferenceTable>();
    context.sdf_request.surface = SdfControlSurface::Tcl;
    context.sdf_request.phase = SdfControlPhase::Elaborate;
    if (invocation.delay_mode == project::DelayMode::minimum)
        context.sdf_request.selection = SdfDelaySelection::Minimum;
    else if (invocation.delay_mode == project::DelayMode::maximum)
        context.sdf_request.selection = SdfDelaySelection::Maximum;
    if (invocation.sdf_report_limit)
        context.sdf_request.report_limit = *invocation.sdf_report_limit;
    for (std::size_t index = 0; index < invocation.sdf_files.size(); ++index) {
        context.sdf_request.inputs.push_back(SdfControlInput {
            fsim::support::path_to_utf8(invocation.sdf_files[index]),
            invocation.sdf_root.value_or("*"),
            invocation.sdf_cell,
            static_cast<std::uint64_t>(index),
            0 });
    }
    if (!context.sdf_request.inputs.empty()) {
        auto configured = apply_sdf_control(context.sdf_request);
        if (!configured.ok()) {
            for (const auto& entry : configured.diagnostics)
                diagnostics.error(entry.code, entry.message);
            return kUserError;
        }
        context.sdf_request = configured.application->request();
        context.sdf_control = std::move(configured.application);
    }
    if (Tcl_CreateNamespace(
            interpreter.get(), "::fsim", nullptr, nullptr)
        == nullptr) {
        diagnostics.error(
            "FSIM-TCL-0002",
            "failed to create the fsim Tcl namespace: "
                + interpreter_result(interpreter.get()));
        return kUnavailable;
    }
    if (!install_cd_callback_guard(
            interpreter.get(), context, cd_command_state)) {
        diagnostics.error(
            "FSIM-TCL-0002",
            "failed to install the Tcl cd callback guard: "
                + interpreter_result(interpreter.get()));
        return kUnavailable;
    }
    const auto fsim_commands = command_specs();
    const auto duplicate = std::adjacent_find(
        fsim_commands.begin(), fsim_commands.end(),
        [](const auto& left, const auto& right) {
            return left.name == right.name;
        });
    if (duplicate != fsim_commands.end()) {
        diagnostics.error("FSIM-TCL-0002",
            "duplicate fsim Tcl command catalog entry: "
                + std::string { duplicate->name });
        return kUnavailable;
    }
    bool commands_ok = true;
    for (const auto& command : fsim_commands) {
        if (!command.name.starts_with("fsim::")) {
            diagnostics.error("FSIM-TCL-0002",
                "noncanonical fsim Tcl command catalog entry: "
                    + std::string { command.name });
            return kUnavailable;
        }
        std::string qualified_name { "::" };
        qualified_name.append(command.name);
        if (Tcl_CreateObjCommand2(
                interpreter.get(),
                qualified_name.c_str(),
                invoke_catalog_command,
                &context,
                nullptr)
            == nullptr) {
            commands_ok = false;
            break;
        }
    }
    if (Tcl_CreateObjCommand2(
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

    if (invocation.command == cli::Command::debug) {
        if (reload_snapshot(context, interpreter.get(), invocation.snapshot)
            != TCL_OK) {
            return kUserError;
        }
        Tcl_ResetResult(interpreter.get());
    }

    if (invocation.tcl_script || !invocation.tcl_commands.empty()) {
        return evaluate_batch(interpreter.get(), context, diagnostics);
    }
    return evaluate_interactive(
        interpreter.get(), context, diagnostics, input);
#endif
}

} // namespace fsim::app
