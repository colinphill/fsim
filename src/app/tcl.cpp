// SPDX-License-Identifier: Apache-2.0
#include "tcl.hpp"
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
                        { "<interactive>", { 1, 1, 0 }, { 1, 1, 0 } });
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
        nullptr
    };
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
    constexpr std::array fsim_commands {
        "::fsim::project",
        "::fsim::check",
        "::fsim::build",
        "::fsim::signals",
        "::fsim::provenance",
        "::fsim::read",
        "::fsim::deposit",
        "::fsim::force",
        "::fsim::release",
        "::fsim::run",
        "::fsim::status",
        "::fsim::diagnostics",
        "::fsim::sdf",
        "::fsim::on",
        "::fsim::off",
        "::fsim::callbacks",
        "::fsim::stop",
        "::fsim::trace",
        "::fsim::debug"
    };
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

} // namespace fsim::app
