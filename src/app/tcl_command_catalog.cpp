// SPDX-License-Identifier: Apache-2.0
#include "tcl_command_catalog.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

namespace {

int invoke_existing(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size count,
    Tcl_Obj* const arguments[])
{
    return fsim_command(&context, interpreter, count, arguments);
}

int invoke_version(
    TclContext& context,
    Tcl_Interp* interpreter,
    const Tcl_Size count,
    Tcl_Obj* const arguments[])
{
    return version_command(&context, interpreter, count, arguments);
}

Tcl_Obj* text_object(const std::string_view text)
{
    return Tcl_NewStringObj(text.data(), static_cast<Tcl_Size>(text.size()));
}

void put_text(Tcl_Interp* interpreter, Tcl_Obj* dictionary,
    const std::string_view key, const std::string_view value)
{
    if (Tcl_DictObjPut(interpreter, dictionary, text_object(key),
            text_object(value)) != TCL_OK) {
        throw std::runtime_error { "failed to build command help" };
    }
}

std::string_view capability_name(const TclCommandCapability capability)
{
    switch (capability) {
    case TclCommandCapability::workspace: return "workspace";
    case TclCommandCapability::loaded_design: return "loaded_design";
    case TclCommandCapability::simulation: return "simulation";
    case TclCommandCapability::debugger: return "debugger";
    }
    return "workspace";
}

std::string_view result_shape_name(const TclResultShape shape)
{
    switch (shape) {
    case TclResultShape::scalar: return "scalar";
    case TclResultShape::list: return "list";
    case TclResultShape::dictionary: return "dictionary";
    case TclResultShape::reference: return "reference";
    case TclResultShape::variant: return "variant";
    }
    return "variant";
}

std::string_view completion_domain_name(const TclCompletionDomain domain)
{
    switch (domain) {
    case TclCompletionDomain::literal: return "literal";
    case TclCompletionDomain::source_path: return "source_path";
    case TclCompletionDomain::directory_path: return "directory_path";
    case TclCompletionDomain::library: return "library";
    case TclCompletionDomain::snapshot: return "snapshot";
    case TclCompletionDomain::compiled_definition: return "compiled_definition";
    case TclCompletionDomain::design_object: return "design_object";
    case TclCompletionDomain::debugger_entity: return "debugger_entity";
    }
    return "literal";
}

Tcl_Obj* describe_arguments(Tcl_Interp* interpreter,
    const std::span<const TclCommandArgument> arguments)
{
    Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
    for (const auto& argument : arguments) {
        Tcl_Obj* entry = Tcl_NewDictObj();
        put_text(interpreter, entry, "name", argument.name);
        put_text(interpreter, entry, "completion",
            completion_domain_name(argument.completion));
        if (Tcl_DictObjPut(interpreter, entry, text_object("optional"),
                Tcl_NewBooleanObj(argument.optional)) != TCL_OK
            || Tcl_DictObjPut(interpreter, entry, text_object("repeatable"),
                Tcl_NewBooleanObj(argument.repeatable)) != TCL_OK) {
            throw std::runtime_error { "failed to build command help" };
        }
        Tcl_Obj* choices = Tcl_NewListObj(0, nullptr);
        for (const auto choice : argument.choices) {
            if (Tcl_ListObjAppendElement(interpreter, choices,
                    text_object(choice)) != TCL_OK) {
                throw std::runtime_error { "failed to build command help" };
            }
        }
        if (Tcl_DictObjPut(interpreter, entry,
                text_object("choices"), choices) != TCL_OK
            || Tcl_ListObjAppendElement(interpreter, result, entry) != TCL_OK) {
            throw std::runtime_error { "failed to build command help" };
        }
    }
    return result;
}

Tcl_Obj* describe_command(
    Tcl_Interp* interpreter, const TclCommandSpec& spec)
{
    Tcl_Obj* dictionary = Tcl_NewDictObj();
    put_text(interpreter, dictionary, "name", spec.name);
    put_text(interpreter, dictionary, "usage", spec.usage);
    put_text(interpreter, dictionary, "help", spec.help);
    put_text(interpreter, dictionary, "capability",
        capability_name(spec.capability));
    put_text(interpreter, dictionary, "result_shape",
        result_shape_name(spec.result_shape));
    put_text(interpreter, dictionary, "diagnostic_domain",
        spec.diagnostic_domain);
    if (Tcl_DictObjPut(interpreter, dictionary,
            text_object("callback_safe"),
            Tcl_NewBooleanObj(spec.callback_safe)) != TCL_OK
        || Tcl_DictObjPut(interpreter, dictionary,
            text_object("arguments"),
            describe_arguments(interpreter, spec.arguments)) != TCL_OK) {
        throw std::runtime_error { "failed to build command help" };
    }
    Tcl_Obj* subcommands = Tcl_NewListObj(0, nullptr);
    for (const auto& subcommand : spec.subcommands) {
        Tcl_Obj* entry = Tcl_NewDictObj();
        put_text(interpreter, entry, "name", subcommand.name);
        put_text(interpreter, entry, "usage", subcommand.usage);
        put_text(interpreter, entry, "help", subcommand.help);
        put_text(interpreter, entry, "capability",
            capability_name(subcommand.capability));
        if (Tcl_DictObjPut(interpreter, entry,
                text_object("arguments"),
                describe_arguments(interpreter, subcommand.arguments)) != TCL_OK
            || Tcl_ListObjAppendElement(interpreter, subcommands, entry) != TCL_OK) {
            throw std::runtime_error { "failed to build command help" };
        }
    }
    if (Tcl_DictObjPut(interpreter, dictionary,
            text_object("subcommands"), subcommands) != TCL_OK) {
        throw std::runtime_error { "failed to build command help" };
    }
    return dictionary;
}

int help_command(
    TclContext&,
    Tcl_Interp* interpreter,
    const Tcl_Size count,
    Tcl_Obj* const arguments[])
{
    if (count > 2) {
        Tcl_WrongNumArgs(interpreter, 1, arguments, "?COMMAND?");
        return TCL_ERROR;
    }
    if (count == 2) {
        const std::string_view name { Tcl_GetString(arguments[1]) };
        const auto* spec = find_command_spec(name);
        if (spec == nullptr && name.find("::") == std::string_view::npos) {
            spec = find_command_spec("fsim::" + std::string { name });
        }
        if (spec == nullptr) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("unknown fsim Tcl command", -1));
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interpreter, describe_command(interpreter, *spec));
        return TCL_OK;
    }
    Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
    for (const auto& spec : command_specs()) {
        if (Tcl_ListObjAppendElement(interpreter, result,
                describe_command(interpreter, spec)) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    Tcl_SetObjResult(interpreter, result);
    return TCL_OK;
}

std::string_view normalized_name(const std::string_view name)
{
    return name.starts_with("::") ? name.substr(2) : name;
}

} // namespace

std::span<const TclCommandSpec> existing_command_specs()
{
    static constexpr std::array load_arguments {
        TclCommandArgument { "snapshot", TclCompletionDomain::snapshot, true },
    };
    static constexpr std::array signal_arguments {
        TclCommandArgument { "signal", TclCompletionDomain::design_object },
    };
    static constexpr std::array signal_value_arguments {
        TclCommandArgument { "signal", TclCompletionDomain::design_object },
        TclCommandArgument { "value", TclCompletionDomain::literal },
    };
    static constexpr std::array run_arguments {
        TclCommandArgument { "duration", TclCompletionDomain::literal, true },
    };
    static constexpr std::array event_arguments {
        TclCommandArgument { "event", TclCompletionDomain::literal },
        TclCommandArgument { "script", TclCompletionDomain::literal },
    };
    static constexpr std::array optional_event_arguments {
        TclCommandArgument { "event", TclCompletionDomain::literal, true },
    };
    static constexpr std::array off_arguments {
        TclCommandArgument { "event", TclCompletionDomain::literal },
        TclCommandArgument { "script", TclCompletionDomain::literal, true },
    };
    static constexpr std::array transcript_actions {
        std::string_view { "start" }, std::string_view { "stop" },
        std::string_view { "status" },
    };
    static constexpr std::array transcript_arguments {
        TclCommandArgument { "action", TclCompletionDomain::literal,
            false, false, transcript_actions },
    };
    static constexpr std::array transcript_path_arguments {
        TclCommandArgument { "path", TclCompletionDomain::source_path, true },
    };
    static constexpr std::array transcript_subcommands {
        TclSubcommandSpec { "start", "start ?PATH?",
            "Append commands and output to a transcript file.",
            transcript_path_arguments },
        TclSubcommandSpec { "stop", "stop",
            "Stop recording the Tcl transcript.", { } },
        TclSubcommandSpec { "status", "status",
            "Return the active transcript path and status.", { } },
    };
    static constexpr std::array specs {
        TclCommandSpec { "fsim::get_workspace", "fsim::get_workspace",
            "Return the current workspace and its libraries.",
            TclCommandCapability::workspace, true, { }, invoke_existing },
        TclCommandSpec { "fsim::help", "fsim::help ?COMMAND?",
            "Describe the registered fsim Tcl commands.",
            TclCommandCapability::workspace, true, { }, help_command,
            TclResultShape::variant, "FSIM-TCL" },
        TclCommandSpec { "fsim::version", "fsim::version",
            "Return the fsim and C API versions.",
            TclCommandCapability::workspace, true, { }, invoke_version,
            TclResultShape::scalar, "FSIM-TCL" },
        TclCommandSpec { "fsim::load_snapshot", "fsim::load_snapshot ?SNAPSHOT?",
            "Load a managed snapshot into this Tcl session.",
            TclCommandCapability::workspace, false, load_arguments, invoke_existing },
        TclCommandSpec { "fsim::get_signals", "fsim::get_signals",
            "Return the paths of loaded signals.",
            TclCommandCapability::loaded_design, true, { }, invoke_existing },
        TclCommandSpec { "fsim::read_signal", "fsim::read_signal SIGNAL",
            "Read a signal's logic value.",
            TclCommandCapability::simulation, true, signal_arguments, invoke_existing },
        TclCommandSpec { "fsim::deposit_signal", "fsim::deposit_signal SIGNAL VALUE",
            "Deposit a signal value.",
            TclCommandCapability::simulation, false, signal_value_arguments, invoke_existing },
        TclCommandSpec { "fsim::force_signal", "fsim::force_signal SIGNAL VALUE",
            "Force a signal value.",
            TclCommandCapability::simulation, false, signal_value_arguments, invoke_existing },
        TclCommandSpec { "fsim::release_signal", "fsim::release_signal SIGNAL",
            "Release a forced signal.",
            TclCommandCapability::simulation, false, signal_arguments, invoke_existing },
        TclCommandSpec { "fsim::run", "fsim::run ?DURATION?",
            "Run the loaded simulation.",
            TclCommandCapability::simulation, false, run_arguments, invoke_existing },
        TclCommandSpec { "fsim::get_simulation", "fsim::get_simulation",
            "Return the loaded simulation state.",
            TclCommandCapability::workspace, true, { }, invoke_existing },
        TclCommandSpec { "fsim::get_diagnostics", "fsim::get_diagnostics",
            "Return structured diagnostics.",
            TclCommandCapability::workspace, true, { }, invoke_existing },
        TclCommandSpec { "fsim::clear_diagnostics", "fsim::clear_diagnostics",
            "Discard the structured diagnostics.",
            TclCommandCapability::workspace, true, { }, invoke_existing },
        TclCommandSpec { "fsim::transcript", "fsim::transcript start ?PATH? | stop | status",
            "Control the running Tcl command and output transcript.",
            TclCommandCapability::workspace, false, transcript_arguments,
            transcript_command, TclResultShape::dictionary, "FSIM-TCL",
            transcript_subcommands },
        TclCommandSpec { "fsim::sdf", "fsim::sdf SUBCOMMAND ?ARGS?",
            "Configure and inspect SDF annotation.",
            TclCommandCapability::workspace, true, { }, invoke_existing },
        TclCommandSpec { "fsim::add_callback", "fsim::add_callback EVENT SCRIPT",
            "Register a simulation callback.",
            TclCommandCapability::simulation, false, event_arguments, invoke_existing },
        TclCommandSpec { "fsim::remove_callback", "fsim::remove_callback EVENT ?SCRIPT?",
            "Remove a simulation callback.",
            TclCommandCapability::simulation, false, off_arguments, invoke_existing },
        TclCommandSpec { "fsim::get_callbacks", "fsim::get_callbacks ?EVENT?",
            "Return the registered simulation callbacks.",
            TclCommandCapability::simulation, false, optional_event_arguments, invoke_existing },
        TclCommandSpec { "fsim::stop", "fsim::stop",
            "Request simulation stop at a safe point.",
            TclCommandCapability::simulation, true, { }, invoke_existing },
        TclCommandSpec { "fsim::tracing", "fsim::tracing SUBCOMMAND ?ARGS?",
            "Configure and inspect simulation tracing.",
            TclCommandCapability::simulation, false, { }, invoke_existing },
    };
    return specs;
}

std::span<const TclCommandSpec> command_specs()
{
    static const auto specs = [] {
        std::vector<TclCommandSpec> result;
        for (const auto group : {
                 existing_command_specs(), workspace_command_specs(),
                 object_command_specs(), debugger_command_specs() }) {
            result.insert(result.end(), group.begin(), group.end());
        }
        std::ranges::sort(result, [](const auto& left, const auto& right) {
            return left.name < right.name;
        });
        return result;
    }();
    return specs;
}

const TclCommandSpec* find_command_spec(const std::string_view name)
{
    const auto normalized = normalized_name(name);
    const auto specs = command_specs();
    const auto entry = std::ranges::lower_bound(specs, normalized, { },
        &TclCommandSpec::name);
    return entry != specs.end() && entry->name == normalized ? &*entry : nullptr;
}

bool synchronize_workspace(TclContext& context, Tcl_Interp* interpreter)
{
    std::error_code error;
    const auto current = std::filesystem::current_path(error);
    if (error) {
        const auto message = "cannot determine the current Tcl workspace: "
            + error.message();
        context.diagnostics.error("FSIM-TCL-WORKSPACE-0001", message);
        Tcl_SetObjResult(interpreter, text_object(message));
        return false;
    }
    if (current == context.initial_config.base_directory)
        return true;

    context.initial_config.base_directory = current;
    context.initial_config.build.cache_path = current / ".fsim" / "cache";
    context.config.base_directory = current;
    context.config.build.cache_path = current / ".fsim" / "cache";
    if (context.references)
        context.references->workspace_changed();
    return true;
}

namespace {

    // Global code reaches fsim commands through the ::fsim namespace path, so
    // the invoked word may be unqualified; resolve it as Tcl just did.
    const TclCommandSpec* invoked_command_spec(
        Tcl_Interp* interpreter, Tcl_Obj* word)
    {
        if (const auto* spec = find_command_spec(Tcl_GetString(word))) {
            return spec;
        }
        const auto command = Tcl_GetCommandFromObj(interpreter, word);
        if (command == nullptr) {
            return nullptr;
        }
        Tcl_Obj* full_name = Tcl_NewObj();
        Tcl_IncrRefCount(full_name);
        Tcl_GetCommandFullName(interpreter, command, full_name);
        const auto* spec = find_command_spec(Tcl_GetString(full_name));
        Tcl_DecrRefCount(full_name);
        return spec;
    }

} // namespace

int invoke_catalog_command(
    void* client_data,
    Tcl_Interp* interpreter,
    const Tcl_Size count,
    Tcl_Obj* const arguments[]) noexcept
{
    auto& context = *static_cast<TclContext*>(client_data);
    try {
        if (!synchronize_workspace(context, interpreter))
            return TCL_ERROR;
        if (count < 1) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("missing fsim Tcl command", -1));
            return TCL_ERROR;
        }
        const auto* spec = invoked_command_spec(interpreter, arguments[0]);
        if (spec == nullptr || spec->handler == nullptr) {
            Tcl_SetObjResult(interpreter,
                Tcl_NewStringObj("unknown fsim Tcl command", -1));
            return TCL_ERROR;
        }
        if (context.callback_depth != 0 && !spec->callback_safe) {
            Tcl_SetObjResult(interpreter, Tcl_NewStringObj(
                "this fsim command is not safe inside a simulation callback", -1));
            return TCL_ERROR;
        }
        if (normalized_name(Tcl_GetString(arguments[0])) == spec->name) {
            return spec->handler(context, interpreter, count, arguments);
        }
        // Handlers dispatch on and report the canonical fsim:: spelling.
        std::vector<Tcl_Obj*> canonical(arguments, arguments + count);
        canonical.front() = Tcl_NewStringObj(
            spec->name.data(), tcl_size(spec->name.size()));
        Tcl_IncrRefCount(canonical.front());
        struct Release {
            Tcl_Obj* object;
            ~Release() { Tcl_DecrRefCount(object); }
        } release { canonical.front() };
        return spec->handler(context, interpreter, count, canonical.data());
    } catch (const std::exception& exception) {
        Tcl_SetObjResult(interpreter,
            Tcl_NewStringObj(exception.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        Tcl_SetObjResult(interpreter,
            Tcl_NewStringObj("unknown failure in an fsim Tcl command", -1));
        return TCL_ERROR;
    }
}

#endif

} // namespace fsim::app::tcl_detail
