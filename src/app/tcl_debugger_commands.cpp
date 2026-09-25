// SPDX-License-Identifier: Apache-2.0
#include "tcl_command_catalog.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

namespace {

    Tcl_Obj* string_object(const std::string_view value)
    {
        return Tcl_NewStringObj(value.data(), tcl_size(value.size()));
    }

    Tcl_Obj* unsigned_object(const std::uint64_t value)
    {
        return string_object(std::to_string(value));
    }

    void dict_put(
        Tcl_Interp* interpreter,
        Tcl_Obj* dictionary,
        const std::string_view key,
        Tcl_Obj* value)
    {
        if (Tcl_DictObjPut(
                interpreter, dictionary, string_object(key), value)
            != TCL_OK) {
            throw std::runtime_error("failed to construct a Tcl dictionary");
        }
    }

    void list_append(
        Tcl_Interp* interpreter, Tcl_Obj* list, Tcl_Obj* value)
    {
        if (Tcl_ListObjAppendElement(interpreter, list, value) != TCL_OK) {
            throw std::runtime_error("failed to construct a Tcl list");
        }
    }

    std::string_view object_string(Tcl_Obj* object)
    {
        Tcl_Size length { };
        const char* value = Tcl_GetStringFromObj(object, &length);
        return { value, static_cast<std::size_t>(length) };
    }

    int structured_error(
        TclContext& context,
        Tcl_Interp* interpreter,
        const std::string_view code,
        const std::string_view message)
    {
        context.diagnostics.error(std::string { code }, std::string { message });
        return command_diagnostic_error(context, interpreter, message);
    }

    int structured_error(
        TclContext& context,
        Tcl_Interp* interpreter,
        const std::exception& exception)
    {
        return structured_error(
            context, interpreter, "FSIM-TCL-DEBUG-0001", exception.what());
    }

    bool ensure_debugger(TclContext& context, Tcl_Interp* interpreter)
    {
        if (!ensure_simulation(
                context, interpreter, SimulationEngine::debug)) {
            return false;
        }
        if (!context.debugger) {
            context.debug_output = std::make_unique<std::ostringstream>();
            context.debug_error = std::make_unique<std::ostringstream>();
            context.simulation->start();
            try {
                context.debugger = std::make_unique<DebuggerControl>(
                    *context.simulation,
                    *context.debug_output,
                    *context.debug_error,
                    context.config,
                    context.diagnostics);
            } catch (const std::exception& exception) {
                (void)structured_error(context, interpreter, exception);
                return false;
            }
        }
        return true;
    }

    Tcl_Obj* status_object(
        Tcl_Interp* interpreter, const DebuggerStatus& status)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "time", unsigned_object(status.time));
        dict_put(interpreter, result, "delta", unsigned_object(status.delta));
        dict_put(interpreter, result, "scope", string_object(status.scope));
        dict_put(interpreter, result, "finished", Tcl_NewBooleanObj(status.finished));
        dict_put(interpreter, result, "poisoned", Tcl_NewBooleanObj(status.poisoned));
        if (status.breakpoint_id) {
            dict_put(
                interpreter, result, "breakpoint_id",
                unsigned_object(*status.breakpoint_id));
        }
        if (!status.stop_reason.empty()) {
            dict_put(
                interpreter, result, "stop_reason",
                string_object(status.stop_reason));
        }
        return result;
    }

    Tcl_Obj* breakpoint_object(
        Tcl_Interp* interpreter, const DebuggerBreakpointInfo& breakpoint)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "id", unsigned_object(breakpoint.id));
        dict_put(interpreter, result, "kind", string_object(breakpoint.kind));
        dict_put(interpreter, result, "path", string_object(breakpoint.path));
        if (breakpoint.time) {
            dict_put(interpreter, result, "time", unsigned_object(*breakpoint.time));
        }
        if (breakpoint.line) {
            dict_put(interpreter, result, "line", unsigned_object(*breakpoint.line));
        }
        if (!breakpoint.comparison.empty()) {
            dict_put(
                interpreter, result, "comparison",
                string_object(breakpoint.comparison));
            dict_put(
                interpreter, result, "value", string_object(breakpoint.value));
        }
        return result;
    }

    Tcl_Obj* breakpoints_object(
        Tcl_Interp* interpreter,
        const std::vector<DebuggerBreakpointInfo>& breakpoints)
    {
        Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
        for (const auto& breakpoint : breakpoints) {
            list_append(interpreter, result, breakpoint_object(interpreter, breakpoint));
        }
        return result;
    }

    Tcl_Obj* frame_object(Tcl_Interp* interpreter, const DebuggerFrameInfo& frame)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "index", unsigned_object(frame.index));
        dict_put(
            interpreter, result, "process_id",
            unsigned_object(frame.process_id));
        dict_put(interpreter, result, "process", string_object(frame.process));
        dict_put(interpreter, result, "scope", string_object(frame.scope));
        dict_put(
            interpreter, result, "source_path",
            string_object(frame.source_path));
        dict_put(
            interpreter, result, "source_line",
            unsigned_object(frame.source_line));
        dict_put(
            interpreter, result, "source_column",
            unsigned_object(frame.source_column));
        dict_put(
            interpreter, result, "point_kind", string_object(frame.point_kind));
        Tcl_Obj* locals = Tcl_NewListObj(0, nullptr);
        for (const auto& local : frame.locals) {
            Tcl_Obj* item = Tcl_NewDictObj();
            dict_put(interpreter, item, "name", string_object(local.name));
            dict_put(interpreter, item, "type", string_object(local.type));
            dict_put(interpreter, item, "value", string_object(local.value));
            list_append(interpreter, locals, item);
        }
        dict_put(interpreter, result, "locals", locals);
        return result;
    }

    Tcl_Obj* scope_object(Tcl_Interp* interpreter, const DebuggerScopeInfo& scope)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "path", string_object(scope.path));
        Tcl_Obj* children = Tcl_NewListObj(0, nullptr);
        for (const auto& child : scope.children) {
            list_append(interpreter, children, string_object(child));
        }
        dict_put(interpreter, result, "children", children);
        return result;
    }

    Tcl_Obj* inspection_object(
        Tcl_Interp* interpreter, const DebuggerInspection& inspection)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "path", string_object(inspection.path));
        dict_put(interpreter, result, "name", string_object(inspection.name));
        dict_put(interpreter, result, "kind", string_object(inspection.kind));
        dict_put(interpreter, result, "type", string_object(inspection.type));
        dict_put(interpreter, result, "width", unsigned_object(inspection.width));
        dict_put(interpreter, result, "value", string_object(inspection.value));
        dict_put(interpreter, result, "forced", Tcl_NewBooleanObj(inspection.forced));
        Tcl_Obj* children = Tcl_NewListObj(0, nullptr);
        for (const auto& child : inspection.children) {
            list_append(interpreter, children, string_object(child));
        }
        dict_put(interpreter, result, "children", children);
        return result;
    }

    Tcl_Obj* provenance_object(
        Tcl_Interp* interpreter, const DebuggerSourceProvenance& provenance)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "path", string_object(provenance.path));
        dict_put(interpreter, result, "language", string_object(provenance.language));
        dict_put(interpreter, result, "library", string_object(provenance.library));
        dict_put(interpreter, result, "unit", string_object(provenance.unit));
        if (provenance.unit_id) {
            dict_put(
                interpreter, result, "unit_id",
                unsigned_object(*provenance.unit_id));
        }
        dict_put(
            interpreter, result, "source_path",
            string_object(provenance.source_path));
        dict_put(
            interpreter, result, "source_identity",
            string_object(provenance.source_identity));
        if (provenance.source_id) {
            dict_put(
                interpreter, result, "source_id",
                unsigned_object(*provenance.source_id));
        }
        dict_put(
            interpreter, result, "source_line",
            unsigned_object(provenance.source_line));
        dict_put(
            interpreter, result, "source_column",
            unsigned_object(provenance.source_column));
        dict_put(
            interpreter, result, "standard", string_object(provenance.standard));
        dict_put(
            interpreter, result, "compatibility_profile",
            string_object(provenance.compatibility_profile));
        dict_put(
            interpreter, result, "instance", string_object(provenance.instance));
        dict_put(
            interpreter, result, "plugin_identity",
            string_object(provenance.plugin_identity));
        dict_put(
            interpreter, result, "plugin_input_digest",
            string_object(provenance.plugin_input_digest));
        dict_put(
            interpreter, result, "plugin_link_digest",
            string_object(provenance.plugin_link_digest));
        dict_put(
            interpreter, result, "plugin_compiler_fingerprint",
            string_object(provenance.plugin_compiler_fingerprint));
        dict_put(
            interpreter, result, "plugin_library_checksum",
            string_object(provenance.plugin_library_checksum));
        return result;
    }

    Tcl_Obj* provenance_list(
        Tcl_Interp* interpreter,
        const std::vector<DebuggerSourceProvenance>& provenance)
    {
        Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
        for (const auto& item : provenance) {
            list_append(interpreter, result, provenance_object(interpreter, item));
        }
        return result;
    }

    std::optional<std::uint64_t> unsigned_argument(
        Tcl_Interp* interpreter, Tcl_Obj* object)
    {
        Tcl_WideInt parsed { };
        if (Tcl_GetWideIntFromObj(interpreter, object, &parsed) != TCL_OK
            || parsed < 0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(parsed);
    }

    int resolve_inspection_path(
        TclContext& context,
        Tcl_Interp* interpreter,
        const std::string_view token,
        std::string& path,
        std::shared_ptr<const void>& lifetime_guard)
    {
        constexpr std::string_view reference_prefix { "@fsim/ref/" };
        if (!token.starts_with(reference_prefix)) {
            path.assign(token);
            return TCL_OK;
        }
        if (!context.references) {
            return structured_error(
                context, interpreter, "FSIM-TCL-DEBUG-REF-0001",
                "loaded object reference table is unavailable");
        }
        const auto resolved = context.references->resolve(
            token, TclReferenceKind::loaded_object);
        if (!resolved) {
            const auto message = resolved.error == TclReferenceError::wrong_kind
                ? "debug inspect requires a loaded design object reference"
                : resolved.error == TclReferenceError::stale
                ? "debug inspect received a stale object reference"
                : "debug inspect received an invalid object reference";
            return structured_error(
                context, interpreter, "FSIM-TCL-DEBUG-REF-0001", message);
        }
        lifetime_guard = resolved.lifetime_guard;
        const std::string_view identity { resolved.reference->identity };
        constexpr std::string_view object_prefix { "object:" };
        constexpr std::string_view instance_prefix { "instance:" };
        if (identity.starts_with(object_prefix)) {
            const auto object_number = identity.substr(object_prefix.size());
            std::uint64_t object_id { };
            const auto parsed = std::from_chars(
                object_number.data(), object_number.data() + object_number.size(),
                object_id, 10);
            const auto* design_ir = current_design_ir(context);
            if (parsed.ec != std::errc { }
                || parsed.ptr != object_number.data() + object_number.size()
                || !design_ir) {
                return structured_error(
                    context, interpreter, "FSIM-TCL-DEBUG-REF-0001",
                    "debug inspect cannot resolve the loaded object reference");
            }
            const auto object = std::ranges::find_if(
                design_ir->objects(), [object_id](const auto& candidate) {
                    return candidate.id.value() == object_id;
                });
            if (object == design_ir->objects().end()) {
                return structured_error(
                    context, interpreter, "FSIM-TCL-DEBUG-REF-0001",
                    "debug inspect cannot resolve the loaded object reference");
            }
            path.assign(design_ir->path(object->path));
            return TCL_OK;
        }
        if (identity.starts_with(instance_prefix)) {
            path.assign(identity.substr(instance_prefix.size()));
            return TCL_OK;
        }
        return structured_error(
            context, interpreter, "FSIM-TCL-DEBUG-REF-0001",
            "debug inspect requires an inspectable loaded design object reference");
    }

    int debug_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count < 2) {
            Tcl_WrongNumArgs(
                interpreter, 1, arguments,
                "status|step|continue|break|watch|frames|frame|scope|inspect|restart|provenance ?ARG ...?");
            return TCL_ERROR;
        }
        const auto subcommand = object_string(arguments[1]);
        if (subcommand == "provenance") {
            if (argument_count > 3) {
                Tcl_WrongNumArgs(interpreter, 2, arguments, "?PATH?");
                return TCL_ERROR;
            }
            try {
                const auto requested = argument_count == 3
                    ? std::optional<std::string_view> { object_string(arguments[2]) }
                    : std::nullopt;
                if (!context.simulation && !ensure_built(context, interpreter)) {
                    return TCL_ERROR;
                }
                auto records = context.simulation
                    ? context.simulation->structured_provenance(requested)
                    : fsim::app::structured_provenance(*context.built, requested);
                Tcl_SetObjResult(interpreter, provenance_list(interpreter, records));
                return TCL_OK;
            } catch (const std::exception& exception) {
                return structured_error(context, interpreter, exception);
            }
        }

        if (subcommand == "restart") {
            if (argument_count > 3) {
                Tcl_WrongNumArgs(interpreter, 2, arguments, "?SNAPSHOT?");
                return TCL_ERROR;
            }
            const auto snapshot = argument_count == 3
                ? object_string(arguments[2])
                : std::string_view { context.invocation.snapshot };
            const auto reloaded = reload_snapshot(context, interpreter, snapshot);
            if (reloaded != TCL_OK) {
                return reloaded;
            }
            if (!ensure_debugger(context, interpreter)) {
                return TCL_ERROR;
            }
            Tcl_Obj* result = status_object(interpreter, context.debugger->status());
            dict_put(
                interpreter, result, "snapshot",
                string_object(context.invocation.snapshot));
            Tcl_SetObjResult(interpreter, result);
            return TCL_OK;
        }

        if (!ensure_debugger(context, interpreter)) {
            return TCL_ERROR;
        }
        try {
            if (subcommand == "status") {
                if (argument_count != 2) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, nullptr);
                    return TCL_ERROR;
                }
                Tcl_SetObjResult(
                    interpreter, status_object(interpreter, context.debugger->status()));
                return TCL_OK;
            }
            if (subcommand == "step") {
                if (argument_count > 3) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, "?KIND?");
                    return TCL_ERROR;
                }
                context.debugger->step(argument_count == 3
                        ? object_string(arguments[2])
                        : std::string_view { "statement" });
                if (context.callback_error) {
                    throw std::runtime_error(*context.callback_error);
                }
                Tcl_SetObjResult(
                    interpreter, status_object(interpreter, context.debugger->status()));
                return TCL_OK;
            }
            if (subcommand == "continue") {
                if (argument_count > 3) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, "?DURATION?");
                    return TCL_ERROR;
                }
                context.debugger->continue_run(argument_count == 3
                        ? std::optional<std::string_view> { object_string(arguments[2]) }
                        : std::nullopt);
                if (context.callback_error) {
                    throw std::runtime_error(*context.callback_error);
                }
                Tcl_SetObjResult(
                    interpreter, status_object(interpreter, context.debugger->status()));
                return TCL_OK;
            }
            if (subcommand == "break" || subcommand == "watch") {
                if (argument_count < 3) {
                    Tcl_WrongNumArgs(
                        interpreter, 2, arguments,
                        "add|list|delete|clear ?ARG ...?");
                    return TCL_ERROR;
                }
                const auto operation = object_string(arguments[2]);
                const bool watch = subcommand == "watch";
                if (operation == "list") {
                    if (argument_count != 3) {
                        Tcl_WrongNumArgs(interpreter, 3, arguments, nullptr);
                        return TCL_ERROR;
                    }
                    Tcl_SetObjResult(interpreter, breakpoints_object(interpreter, watch ? context.debugger->watches() : context.debugger->breakpoints()));
                    return TCL_OK;
                }
                if (operation == "clear") {
                    if (argument_count != 3) {
                        Tcl_WrongNumArgs(interpreter, 3, arguments, nullptr);
                        return TCL_ERROR;
                    }
                    if (watch) {
                        for (const auto& item : context.debugger->watches()) {
                            context.debugger->delete_watch(item.id);
                        }
                    } else {
                        context.debugger->clear_breakpoints();
                    }
                    Tcl_SetObjResult(interpreter, breakpoints_object(interpreter, watch ? context.debugger->watches() : context.debugger->breakpoints()));
                    return TCL_OK;
                }
                if (operation == "delete") {
                    if (argument_count != 4) {
                        Tcl_WrongNumArgs(interpreter, 3, arguments, "ID");
                        return TCL_ERROR;
                    }
                    const auto id = unsigned_argument(interpreter, arguments[3]);
                    if (!id) {
                        return structured_error(
                            context, interpreter, "FSIM-TCL-DEBUG-0002",
                            "debugger ID must be an unsigned integer");
                    }
                    if (watch) {
                        context.debugger->delete_watch(*id);
                    } else {
                        context.debugger->delete_breakpoint(*id);
                    }
                    Tcl_SetObjResult(interpreter, breakpoints_object(interpreter, watch ? context.debugger->watches() : context.debugger->breakpoints()));
                    return TCL_OK;
                }
                if (operation != "add") {
                    return structured_error(
                        context, interpreter, "FSIM-TCL-DEBUG-0002",
                        "debugger subcommand must be add, list, delete, or clear");
                }
                const auto minimum = watch ? 4 : 5;
                if (argument_count < minimum || argument_count > minimum + 2
                    || argument_count == minimum + 1) {
                    Tcl_WrongNumArgs(
                        interpreter, 3, arguments,
                        watch ? "SIGNAL ?COMPARATOR VALUE?"
                              : "KIND LOCATION ?COMPARATOR VALUE?");
                    return TCL_ERROR;
                }
                const auto comparison = argument_count == minimum + 2
                    ? std::optional<std::string_view> {
                          object_string(arguments[minimum])
                      }
                    : std::nullopt;
                const auto value = argument_count == minimum + 2
                    ? std::optional<std::string_view> {
                          object_string(arguments[minimum + 1])
                      }
                    : std::nullopt;
                const auto result = watch
                    ? context.debugger->add_watch(
                          object_string(arguments[3]), comparison, value)
                    : context.debugger->add_breakpoint(
                          object_string(arguments[3]), object_string(arguments[4]),
                          comparison, value);
                Tcl_SetObjResult(interpreter, breakpoint_object(interpreter, result));
                return TCL_OK;
            }
            if (subcommand == "frames") {
                if (argument_count != 2) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, nullptr);
                    return TCL_ERROR;
                }
                Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
                for (const auto& frame : context.debugger->frames()) {
                    list_append(interpreter, result, frame_object(interpreter, frame));
                }
                Tcl_SetObjResult(interpreter, result);
                return TCL_OK;
            }
            if (subcommand == "frame") {
                if (argument_count > 3) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, "?INDEX?");
                    return TCL_ERROR;
                }
                const auto index = argument_count == 3
                    ? unsigned_argument(interpreter, arguments[2])
                    : std::optional<std::uint64_t> { 0U };
                if (!index || *index > std::numeric_limits<std::size_t>::max()) {
                    return structured_error(
                        context, interpreter, "FSIM-TCL-DEBUG-0002",
                        "debug frame index must be an unsigned integer");
                }
                Tcl_SetObjResult(interpreter, frame_object(interpreter, context.debugger->frame(static_cast<std::size_t>(*index))));
                return TCL_OK;
            }
            if (subcommand == "scope") {
                if (argument_count > 3) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, "?PATH?");
                    return TCL_ERROR;
                }
                const auto path = argument_count == 3
                    ? std::optional<std::string_view> { object_string(arguments[2]) }
                    : std::nullopt;
                Tcl_SetObjResult(
                    interpreter, scope_object(interpreter, context.debugger->scope(path)));
                return TCL_OK;
            }
            if (subcommand == "inspect") {
                if (argument_count != 3) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, "PATH_OR_REFERENCE");
                    return TCL_ERROR;
                }
                std::string path;
                std::shared_ptr<const void> lifetime_guard;
                const auto resolved = resolve_inspection_path(
                    context, interpreter, object_string(arguments[2]), path,
                    lifetime_guard);
                if (resolved != TCL_OK) {
                    return resolved;
                }
                Tcl_SetObjResult(interpreter, inspection_object(interpreter, context.debugger->inspect(path)));
                return TCL_OK;
            }
            if (subcommand == "provenance") {
                if (argument_count > 3) {
                    Tcl_WrongNumArgs(interpreter, 2, arguments, "?PATH?");
                    return TCL_ERROR;
                }
                const auto path = argument_count == 3
                    ? std::optional<std::string_view> { object_string(arguments[2]) }
                    : std::nullopt;
                Tcl_SetObjResult(
                    interpreter, provenance_list(interpreter, context.debugger->provenance(path)));
                return TCL_OK;
            }
            return structured_error(
                context, interpreter, "FSIM-TCL-DEBUG-0002",
                "unknown fsim::debug subcommand");
        } catch (const std::exception& exception) {
            if (context.callback_error) {
                auto callback_error = std::move(*context.callback_error);
                context.callback_error.reset();
                return structured_error(
                    context, interpreter, "FSIM-TCL-DEBUG-CALLBACK-0001",
                    callback_error);
            }
            return structured_error(context, interpreter, exception);
        }
    }

    int provenance_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count > 2) {
            Tcl_WrongNumArgs(interpreter, 1, arguments, "?PATH?");
            return TCL_ERROR;
        }
        try {
            if (!context.simulation && !ensure_built(context, interpreter)) {
                return TCL_ERROR;
            }
            const auto path = argument_count == 2
                ? std::optional<std::string_view> { object_string(arguments[1]) }
                : std::nullopt;
            const auto records = context.simulation
                ? context.simulation->structured_provenance(path)
                : fsim::app::structured_provenance(*context.built, path);
            Tcl_SetObjResult(interpreter, provenance_list(interpreter, records));
            return TCL_OK;
        } catch (const std::exception& exception) {
            return structured_error(context, interpreter, exception);
        }
    }

    int debug_handler(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[]) noexcept
    {
        try {
            return debug_command(context, interpreter, argument_count, arguments);
        } catch (const std::exception& exception) {
            return structured_error(context, interpreter, exception);
        } catch (...) {
            return structured_error(
                context, interpreter, "FSIM-TCL-DEBUG-0001",
                "fsim::debug failed with an unknown exception");
        }
    }

    int provenance_handler(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[]) noexcept
    {
        try {
            return provenance_command(
                context, interpreter, argument_count, arguments);
        } catch (const std::exception& exception) {
            return structured_error(context, interpreter, exception);
        } catch (...) {
            return structured_error(
                context, interpreter, "FSIM-TCL-DEBUG-0001",
                "fsim::provenance failed with an unknown exception");
        }
    }

    constexpr std::array<std::string_view, 11> kDebugSubcommandChoices {
        "status",
        "step",
        "continue",
        "break",
        "watch",
        "frames",
        "frame",
        "scope",
        "inspect",
        "restart",
        "provenance",
    };
    constexpr std::array<std::string_view, 5> kStepChoices {
        "statement",
        "process",
        "phase",
        "delta",
        "time",
    };
    constexpr std::array<std::string_view, 4> kMutationChoices {
        "add",
        "list",
        "delete",
        "clear",
    };
    constexpr std::array<std::string_view, 5> kBreakpointKindChoices {
        "time",
        "signal",
        "source",
        "phase",
        "uvm",
    };
    constexpr std::array<std::string_view, 2> kComparisonChoices {
        "==",
        "!=",
    };
    constexpr std::array<TclCommandArgument, 2> kDebugArguments { {
        { "subcommand", TclCompletionDomain::literal, false, false,
            kDebugSubcommandChoices },
        { "argument", TclCompletionDomain::debugger_entity, true, true },
    } };
    constexpr std::array<TclCommandArgument, 1> kStepArguments { {
        { "kind", TclCompletionDomain::literal, true, false, kStepChoices },
    } };
    constexpr std::array<TclCommandArgument, 1> kContinueArguments { {
        { "duration", TclCompletionDomain::literal, true, false },
    } };
    constexpr std::array<TclCommandArgument, 5> kBreakArguments { {
        { "operation", TclCompletionDomain::literal, false, false,
            kMutationChoices },
        { "kind_or_id", TclCompletionDomain::literal, true, false,
            kBreakpointKindChoices },
        { "location", TclCompletionDomain::debugger_entity, true, false },
        { "comparison", TclCompletionDomain::literal, true, false,
            kComparisonChoices },
        { "value", TclCompletionDomain::literal, true, false },
    } };
    constexpr std::array<TclCommandArgument, 4> kWatchArguments { {
        { "operation", TclCompletionDomain::literal, false, false,
            kMutationChoices },
        { "signal_or_id", TclCompletionDomain::design_object, true, false },
        { "comparison", TclCompletionDomain::literal, true, false,
            kComparisonChoices },
        { "value", TclCompletionDomain::literal, true, false },
    } };
    constexpr std::array<TclCommandArgument, 1> kFrameArguments { {
        { "index", TclCompletionDomain::literal, true, false },
    } };
    constexpr std::array<TclCommandArgument, 1> kScopeArguments { {
        { "path", TclCompletionDomain::design_object, true, false },
    } };
    constexpr std::array<TclCommandArgument, 1> kInspectArguments { {
        { "path_or_reference", TclCompletionDomain::design_object, false, false },
    } };
    constexpr std::array<TclCommandArgument, 1> kRestartArguments { {
        { "snapshot", TclCompletionDomain::snapshot, true, false },
    } };
    constexpr std::array<TclCommandArgument, 1> kDebugProvenanceArguments { {
        { "path", TclCompletionDomain::design_object, true, false },
    } };
    const std::array<TclSubcommandSpec, 11> kDebugSubcommands { {
        { "status", "fsim::debug status",
            "Return current time, delta, scope, and stop state.", { },
            TclCommandCapability::debugger },
        { "step", "fsim::debug step ?statement|process|phase|delta|time?",
            "Advance one debugger execution unit.", kStepArguments,
            TclCommandCapability::debugger },
        { "continue", "fsim::debug continue ?DURATION?",
            "Run until a stop point or duration limit.", kContinueArguments,
            TclCommandCapability::debugger },
        { "break", "fsim::debug break add|list|delete|clear ?ARG ...?",
            "Add, list, remove, or clear execution breakpoints.", kBreakArguments,
            TclCommandCapability::debugger },
        { "watch", "fsim::debug watch add|list|delete|clear ?ARG ...?",
            "Add, list, remove, or clear signal watches.", kWatchArguments,
            TclCommandCapability::debugger },
        { "frames", "fsim::debug frames",
            "Return the current process frames.", { },
            TclCommandCapability::debugger },
        { "frame", "fsim::debug frame ?INDEX?",
            "Return one current process frame.", kFrameArguments,
            TclCommandCapability::debugger },
        { "scope", "fsim::debug scope ?PATH?",
            "Return the selected scope and its child scopes.", kScopeArguments,
            TclCommandCapability::debugger },
        { "inspect", "fsim::debug inspect PATH_OR_REFERENCE",
            "Inspect a signal, scope, string, or container.", kInspectArguments,
            TclCommandCapability::debugger },
        { "restart", "fsim::debug restart ?SNAPSHOT?",
            "Reload a snapshot and reset the debugger session.", kRestartArguments,
            TclCommandCapability::debugger },
        { "provenance", "fsim::debug provenance ?PATH?",
            "Return structured source provenance.", kDebugProvenanceArguments,
            TclCommandCapability::debugger },
    } };
    constexpr std::array<TclCommandArgument, 1> kProvenanceArguments { {
        { "path", TclCompletionDomain::design_object, true, false },
    } };
    const std::array<TclCommandSpec, 2> kDebuggerCommands { {
        {
            "fsim::debug",
            "fsim::debug status|step|continue|break|watch|frames|frame|scope|inspect|restart|provenance ?ARG ...?",
            "Control and inspect the loaded simulation through structured debugger results.",
            TclCommandCapability::debugger,
            false,
            kDebugArguments,
            debug_handler,
            TclResultShape::variant,
            "FSIM-TCL-DEBUG",
            kDebugSubcommands,
        },
        {
            "fsim::provenance",
            "fsim::provenance ?PATH?",
            "Return structured source provenance for a loaded design object or all design roots.",
            TclCommandCapability::loaded_design,
            true,
            kProvenanceArguments,
            provenance_handler,
            TclResultShape::list,
            "FSIM-TCL-DEBUG",
        },
    } };

} // namespace

std::span<const TclCommandSpec> debugger_command_specs()
{
    return kDebuggerCommands;
}

#endif

} // namespace fsim::app::tcl_detail
