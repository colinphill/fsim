// SPDX-License-Identifier: Apache-2.0
#include "tcl_command_catalog.hpp"
#include "tcl_internal.hpp"

#include "fsim/project/configuration.hpp"
#include "fsim/support/path.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"
#include "tcl.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <cstddef>
#include <tcl.h>
#include <tclDecls.h>

namespace fsim::app::tcl_detail {

#if defined(FSIM_HAS_TCL)

namespace {

std::string_view text(Tcl_Obj* object)
{
    Tcl_Size length {};
    const char* bytes = Tcl_GetStringFromObj(object, &length);
    return { bytes, static_cast<std::size_t>(length) };
}

Tcl_Obj* text_object(const std::string_view value)
{
    return Tcl_NewStringObj(value.data(), tcl_size(value.size()));
}

int fail(Tcl_Interp* interpreter, const std::string_view message)
{
    Tcl_SetObjResult(interpreter, text_object(message));
    Tcl_SetErrorCode(interpreter, "FSIM", "CONFIG", nullptr);
    return TCL_ERROR;
}

void put(Tcl_Obj* dictionary, const std::string_view key, Tcl_Obj* value)
{
    Tcl_DictObjPut(nullptr, dictionary, text_object(key), value);
}

Tcl_Obj* list_object(const std::vector<std::string>& values)
{
    auto* list = Tcl_NewListObj(0, nullptr);
    for (const auto& value : values) {
        Tcl_ListObjAppendElement(nullptr, list, text_object(value));
    }
    return list;
}

std::optional<std::vector<std::string>> list_elements(
    Tcl_Interp* interpreter, Tcl_Obj* object)
{
    Tcl_Size count {};
    Tcl_Obj** elements {};
    if (Tcl_ListObjGetElements(interpreter, object, &count, &elements)
        != TCL_OK) {
        return std::nullopt;
    }
    std::vector<std::string> result;
    for (Tcl_Size index = 0; index < count; ++index) {
        result.emplace_back(text(elements[index]));
    }
    return result;
}

// The script line that made a setting: the frame of the configuration
// command that is running, else the script being sourced.
project::ConfigOrigin origin(
    Tcl_Interp* interpreter, const TclConfigurationState& state)
{
    project::ConfigOrigin result { state.layer, { }, 0U };
    auto* saved = Tcl_GetObjResult(interpreter);
    Tcl_IncrRefCount(saved);
    if (Tcl_EvalEx(interpreter, "::info frame -1", -1, 0) == TCL_OK) {
        auto* frame = Tcl_GetObjResult(interpreter);
        Tcl_Obj* file {};
        Tcl_Obj* line {};
        Tcl_DictObjGet(nullptr, frame, text_object("file"), &file);
        Tcl_DictObjGet(nullptr, frame, text_object("line"), &line);
        Tcl_WideInt number {};
        if (file != nullptr) {
            result.file = std::filesystem::path { std::string { text(file) } };
        }
        if (line != nullptr
            && Tcl_GetWideIntFromObj(nullptr, line, &number) == TCL_OK
            && number > 0) {
            result.line = static_cast<std::uint32_t>(number);
        }
    }
    Tcl_SetObjResult(interpreter, saved);
    Tcl_DecrRefCount(saved);
    if (result.file.empty() && state.layer != project::ConfigLayer::session) {
        result.file = state.script;
    }
    return result;
}

Tcl_Obj* origin_object(const project::ConfigOrigin& value)
{
    auto* result = Tcl_NewDictObj();
    put(result, "layer", text_object(project::to_string(value.layer)));
    put(result, "source", text_object(project::describe(value)));
    return result;
}

bool matches(const std::optional<std::string_view> pattern,
    const std::string_view value)
{
    if (!pattern) {
        return true;
    }
    const std::string subject { value };
    const std::string glob { *pattern };
    return Tcl_StringMatch(subject.c_str(), glob.c_str()) != 0;
}

// `-name VALUE` options after the fixed words; every option once.
struct Options {
    std::vector<std::pair<std::string, Tcl_Obj*>> values;
    std::vector<Tcl_Obj*> positional;

    [[nodiscard]] Tcl_Obj* find(const std::string_view name) const
    {
        const auto found = std::ranges::find(values, name,
            &std::pair<std::string, Tcl_Obj*>::first);
        return found == values.end() ? nullptr : found->second;
    }
    [[nodiscard]] std::optional<std::string> string(
        const std::string_view name) const
    {
        auto* value = find(name);
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string { text(value) };
    }
};

std::optional<Options> parse_options(Tcl_Interp* interpreter,
    const Tcl_Size first, const Tcl_Size count, Tcl_Obj* const arguments[],
    const std::span<const std::string_view> allowed)
{
    Options result;
    for (Tcl_Size index = first; index < count; ++index) {
        const auto word = text(arguments[index]);
        if (!word.starts_with('-')) {
            result.positional.push_back(arguments[index]);
            continue;
        }
        if (std::ranges::find(allowed, word) == allowed.end()) {
            fail(interpreter, "unknown option '" + std::string { word } + "'");
            return std::nullopt;
        }
        if (index + 1 >= count) {
            fail(interpreter, "option '" + std::string { word }
                    + "' needs a value");
            return std::nullopt;
        }
        if (result.find(word) != nullptr) {
            fail(interpreter, "option '" + std::string { word }
                    + "' is given twice");
            return std::nullopt;
        }
        result.values.emplace_back(std::string { word }, arguments[++index]);
    }
    return result;
}

Tcl_Obj* setting_value(const project::ConfigKey& key,
    const std::vector<std::string>& values)
{
    return key.kind == project::ConfigValueKind::list
        ? list_object(values)
        : text_object(values.front());
}

int config_command(TclConfigurationState& state, Tcl_Interp* interpreter,
    const Tcl_Size count, Tcl_Obj* const arguments[])
{
    constexpr std::string_view usage {
        "fsim::config set KEY VALUE | get KEY | unset KEY | list ?PATTERN? | "
        "keys ?PATTERN?"
    };
    if (count < 2) {
        return fail(interpreter, "usage: " + std::string { usage });
    }
    auto& configuration = *state.configuration;
    const auto action = text(arguments[1]);
    const auto known_key = [&](const std::string_view key)
        -> const project::ConfigKey* {
        const auto* found = project::find_config_key(key);
        if (found == nullptr) {
            auto message = "unknown configuration key '" + std::string { key }
                + "'";
            if (const auto closest = project::closest_config_key(key)) {
                message += "; did you mean '" + std::string { *closest }
                    + "'?";
            }
            fail(interpreter, message);
        }
        return found;
    };
    if (action == "set" && count == 4) {
        const auto key = text(arguments[2]);
        const auto* found = known_key(key);
        if (found == nullptr) {
            return TCL_ERROR;
        }
        std::vector<std::string> values;
        if (found->kind == project::ConfigValueKind::list) {
            auto elements = list_elements(interpreter, arguments[3]);
            if (!elements) {
                return TCL_ERROR;
            }
            values = std::move(*elements);
        } else {
            values.emplace_back(text(arguments[3]));
        }
        if (auto error = configuration.set(
                key, std::move(values), origin(interpreter, state))) {
            return fail(interpreter, *error);
        }
        Tcl_ResetResult(interpreter);
        return TCL_OK;
    }
    if (action == "get" && count == 3) {
        const auto key = text(arguments[2]);
        const auto* found = known_key(key);
        if (found == nullptr) {
            return TCL_ERROR;
        }
        if (const auto* setting = configuration.find(key)) {
            Tcl_SetObjResult(interpreter, setting_value(*found, setting->values));
        } else if (found->kind == project::ConfigValueKind::list) {
            Tcl_SetObjResult(interpreter, Tcl_NewListObj(0, nullptr));
        } else {
            Tcl_SetObjResult(interpreter, text_object(found->default_value));
        }
        return TCL_OK;
    }
    if (action == "unset" && count == 3) {
        const auto key = text(arguments[2]);
        if (known_key(key) == nullptr) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(interpreter,
            Tcl_NewBooleanObj(configuration.unset(key, state.layer)));
        return TCL_OK;
    }
    const std::optional<std::string_view> pattern = count == 3
        ? std::optional { text(arguments[2]) }
        : std::nullopt;
    if (action == "list" && count <= 3) {
        auto* result = Tcl_NewDictObj();
        for (const auto& [key, setting] : configuration.effective()) {
            if (!matches(pattern, key)) {
                continue;
            }
            auto* entry = Tcl_NewDictObj();
            put(entry, "value",
                setting_value(*project::find_config_key(key), setting.values));
            put(entry, "origin", origin_object(setting.origin));
            put(result, key, entry);
        }
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }
    if (action == "keys" && count <= 3) {
        auto* result = Tcl_NewListObj(0, nullptr);
        for (const auto& key : project::config_keys()) {
            if (!matches(pattern, key.name)) {
                continue;
            }
            auto* entry = Tcl_NewDictObj();
            put(entry, "name", text_object(key.name));
            put(entry, "kind",
                text_object(key.kind == project::ConfigValueKind::list
                        ? "list"
                        : key.kind == project::ConfigValueKind::boolean
                        ? "boolean"
                        : key.kind == project::ConfigValueKind::integer
                        ? "integer"
                        : "text"));
            put(entry, "default", text_object(key.default_value));
            put(entry, "summary", text_object(key.summary));
            Tcl_ListObjAppendElement(nullptr, result, entry);
        }
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }
    return fail(interpreter, "usage: " + std::string { usage });
}

Tcl_Obj* fsm_hint_object(const project::ConfigFsmHint& hint)
{
    auto* result = Tcl_NewDictObj();
    put(result, "instance", text_object(hint.entry.instance));
    put(result, "current", text_object(hint.entry.current_state));
    if (hint.entry.next_state) {
        put(result, "next", text_object(*hint.entry.next_state));
    }
    if (hint.entry.legal_states) {
        put(result, "legal", list_object(*hint.entry.legal_states));
    }
    put(result, "origin", origin_object(hint.origin));
    return result;
}

Tcl_Obj* exclusion_object(const project::ConfigExclusion& exclusion)
{
    auto* result = Tcl_NewDictObj();
    put(result, "name", text_object(exclusion.id));
    if (exclusion.entry.source) {
        put(result, "source", text_object(*exclusion.entry.source));
    }
    if (exclusion.entry.hierarchy) {
        put(result, "hierarchy", text_object(*exclusion.entry.hierarchy));
    }
    if (exclusion.entry.object) {
        put(result, "object", text_object(*exclusion.entry.object));
    }
    put(result, "metric", text_object(exclusion.entry.metric));
    put(result, "reason", text_object(exclusion.entry.reason));
    put(result, "origin", origin_object(exclusion.origin));
    return result;
}

Tcl_Obj* binding_object(const project::ConfigBinding& binding)
{
    auto* result = Tcl_NewDictObj();
    put(result, "instance", text_object(binding.entry.instance));
    if (binding.entry.target) {
        put(result, "target", text_object(*binding.entry.target));
    }
    if (binding.entry.resolver) {
        put(result, "resolver", text_object(*binding.entry.resolver));
    }
    put(result, "origin", origin_object(binding.origin));
    return result;
}

int fsm_command(TclConfigurationState& state, Tcl_Interp* interpreter,
    const Tcl_Size count, Tcl_Obj* const arguments[])
{
    constexpr std::string_view usage {
        "fsim::coverage fsm add -instance PATH -current NAME ?-next NAME? "
        "?-legal LIST? | remove -instance PATH ?-current NAME? | "
        "show -instance PATH -current NAME | list ?PATTERN? | clear"
    };
    auto& configuration = *state.configuration;
    const auto action = count > 2 ? text(arguments[2]) : std::string_view { };
    static constexpr std::array add_options {
        std::string_view { "-instance" }, std::string_view { "-current" },
        std::string_view { "-next" }, std::string_view { "-legal" },
    };
    static constexpr std::array key_options {
        std::string_view { "-instance" }, std::string_view { "-current" },
    };
    if (action == "add") {
        auto options = parse_options(
            interpreter, 3, count, arguments, add_options);
        if (!options) {
            return TCL_ERROR;
        }
        if (!options->positional.empty()) {
            return fail(interpreter, "usage: " + std::string { usage });
        }
        project::CoverageFsmHintEntry hint;
        hint.instance = options->string("-instance").value_or("");
        hint.current_state = options->string("-current").value_or("");
        hint.next_state = options->string("-next");
        if (auto* legal = options->find("-legal")) {
            auto states = list_elements(interpreter, legal);
            if (!states) {
                return TCL_ERROR;
            }
            hint.legal_states = std::move(*states);
        }
        project::ConfigChange change {};
        if (auto error = configuration.put_fsm_hint(std::move(hint),
                origin(interpreter, state), change)) {
            return fail(interpreter, *error);
        }
        Tcl_SetObjResult(interpreter, text_object(project::to_string(change)));
        return TCL_OK;
    }
    if (action == "remove" || action == "show") {
        auto options = parse_options(
            interpreter, 3, count, arguments, key_options);
        if (!options) {
            return TCL_ERROR;
        }
        const auto instance = options->string("-instance");
        const auto current = options->string("-current");
        if (!options->positional.empty() || !instance
            || (action == "show" && !current)) {
            return fail(interpreter, "usage: " + std::string { usage });
        }
        if (action == "show") {
            const auto& hints = configuration.fsm_hints();
            const auto found = std::ranges::find_if(hints, [&](const auto& item) {
                return item.entry.instance == *instance
                    && item.entry.current_state == *current;
            });
            if (found == hints.end()) {
                return fail(interpreter, "no FSM hint for instance '"
                        + *instance + "' and current state '" + *current + "'");
            }
            Tcl_SetObjResult(interpreter, fsm_hint_object(*found));
            return TCL_OK;
        }
        const auto removed = configuration.remove_fsm_hints(*instance,
            current ? std::optional<std::string_view> { *current }
                    : std::nullopt);
        if (removed == 0U) {
            return fail(interpreter, "no FSM hint for instance '" + *instance
                    + (current ? "' and current state '" + *current : "")
                    + "'");
        }
        Tcl_SetObjResult(interpreter, Tcl_NewWideIntObj(tcl_wide_size(removed)));
        return TCL_OK;
    }
    if (action == "list" && count <= 4) {
        const std::optional<std::string_view> pattern = count == 4
            ? std::optional { text(arguments[3]) }
            : std::nullopt;
        auto* result = Tcl_NewListObj(0, nullptr);
        for (const auto& hint : configuration.fsm_hints()) {
            if (matches(pattern, hint.entry.instance)) {
                Tcl_ListObjAppendElement(nullptr, result, fsm_hint_object(hint));
            }
        }
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }
    if (action == "clear" && count == 3) {
        configuration.clear_fsm_hints();
        Tcl_ResetResult(interpreter);
        return TCL_OK;
    }
    return fail(interpreter, "usage: " + std::string { usage });
}

int exclude_command(TclConfigurationState& state, Tcl_Interp* interpreter,
    const Tcl_Size count, Tcl_Obj* const arguments[])
{
    constexpr std::string_view usage {
        "fsim::coverage exclude add ?-name NAME? ?-source GLOB? "
        "?-hierarchy GLOB? ?-object GLOB? -metric METRIC -reason TEXT | "
        "remove NAME ... | show NAME | list ?PATTERN? | clear"
    };
    auto& configuration = *state.configuration;
    const auto action = count > 2 ? text(arguments[2]) : std::string_view { };
    static constexpr std::array add_options {
        std::string_view { "-name" }, std::string_view { "-source" },
        std::string_view { "-hierarchy" }, std::string_view { "-object" },
        std::string_view { "-metric" }, std::string_view { "-reason" },
    };
    if (action == "add") {
        auto options = parse_options(
            interpreter, 3, count, arguments, add_options);
        if (!options) {
            return TCL_ERROR;
        }
        if (!options->positional.empty()) {
            return fail(interpreter, "usage: " + std::string { usage });
        }
        project::CoverageExclusionEntry exclusion;
        exclusion.source = options->string("-source");
        exclusion.hierarchy = options->string("-hierarchy");
        exclusion.object = options->string("-object");
        exclusion.metric = options->string("-metric").value_or("");
        exclusion.reason = options->string("-reason").value_or("");
        project::ConfigChange change {};
        std::string assigned;
        if (auto error = configuration.put_exclusion(options->string("-name"),
                std::move(exclusion), origin(interpreter, state), change,
                assigned)) {
            return fail(interpreter, *error);
        }
        auto* result = Tcl_NewDictObj();
        put(result, "name", text_object(assigned));
        put(result, "change", text_object(project::to_string(change)));
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }
    if (action == "remove" && count > 3) {
        // Check every name first, so a typo removes nothing.
        for (Tcl_Size index = 3; index < count; ++index) {
            const auto name = text(arguments[index]);
            if (std::ranges::find(configuration.exclusions(), name,
                    &project::ConfigExclusion::id)
                == configuration.exclusions().end()) {
                return fail(interpreter, "no coverage exclusion named '"
                        + std::string { name } + "'");
            }
        }
        for (Tcl_Size index = 3; index < count; ++index) {
            (void)configuration.remove_exclusion(text(arguments[index]));
        }
        Tcl_ResetResult(interpreter);
        return TCL_OK;
    }
    if (action == "show" && count == 4) {
        const auto name = text(arguments[3]);
        const auto& exclusions = configuration.exclusions();
        const auto found = std::ranges::find(
            exclusions, name, &project::ConfigExclusion::id);
        if (found == exclusions.end()) {
            return fail(interpreter, "no coverage exclusion named '"
                    + std::string { name } + "'");
        }
        Tcl_SetObjResult(interpreter, exclusion_object(*found));
        return TCL_OK;
    }
    if (action == "list" && count <= 4) {
        const std::optional<std::string_view> pattern = count == 4
            ? std::optional { text(arguments[3]) }
            : std::nullopt;
        auto* result = Tcl_NewListObj(0, nullptr);
        for (const auto& exclusion : configuration.exclusions()) {
            if (matches(pattern, exclusion.id)) {
                Tcl_ListObjAppendElement(
                    nullptr, result, exclusion_object(exclusion));
            }
        }
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }
    if (action == "clear" && count == 3) {
        configuration.clear_exclusions();
        Tcl_ResetResult(interpreter);
        return TCL_OK;
    }
    return fail(interpreter, "usage: " + std::string { usage });
}

} // namespace

int coverage_settings_command(TclConfigurationState& state,
    Tcl_Interp* interpreter, const Tcl_Size count, Tcl_Obj* const arguments[])
{
    const auto family = count > 1 ? text(arguments[1]) : std::string_view { };
    if (family == "fsm") {
        return fsm_command(state, interpreter, count, arguments);
    }
    if (family == "exclude") {
        return exclude_command(state, interpreter, count, arguments);
    }
    return fail(interpreter,
        "usage: fsim::coverage fsm SUBCOMMAND ... | exclude SUBCOMMAND ...");
}

int bind_settings_command(TclConfigurationState& state,
    Tcl_Interp* interpreter, const Tcl_Size count, Tcl_Obj* const arguments[])
{
    constexpr std::string_view usage {
        "fsim::bind add INSTANCE ?-target vhdl:|sv:|systemc:UNIT? "
        "?-resolver std_logic|sv_wire? | remove INSTANCE ... | show INSTANCE | "
        "list ?PATTERN? | clear"
    };
    auto& configuration = *state.configuration;
    const auto action = count > 1 ? text(arguments[1]) : std::string_view { };
    static constexpr std::array add_options {
        std::string_view { "-target" }, std::string_view { "-resolver" },
    };
    if (action == "add") {
        auto options = parse_options(
            interpreter, 2, count, arguments, add_options);
        if (!options) {
            return TCL_ERROR;
        }
        if (options->positional.size() != 1U) {
            return fail(interpreter, "usage: " + std::string { usage });
        }
        project::Binding binding;
        binding.instance = std::string { text(options->positional.front()) };
        binding.target = options->string("-target");
        binding.resolver = options->string("-resolver");
        project::ConfigChange change {};
        if (auto error = configuration.put_binding(std::move(binding),
                origin(interpreter, state), change)) {
            return fail(interpreter, *error);
        }
        Tcl_SetObjResult(interpreter, text_object(project::to_string(change)));
        return TCL_OK;
    }
    const auto find = [&](const std::string_view instance) {
        return std::ranges::find_if(configuration.bindings(),
            [&](const auto& item) { return item.entry.instance == instance; });
    };
    if (action == "remove" && count > 2) {
        for (Tcl_Size index = 2; index < count; ++index) {
            const auto instance = text(arguments[index]);
            if (find(instance) == configuration.bindings().end()) {
                return fail(interpreter, "no binding for instance '"
                        + std::string { instance } + "'");
            }
        }
        for (Tcl_Size index = 2; index < count; ++index) {
            (void)configuration.remove_binding(text(arguments[index]));
        }
        Tcl_ResetResult(interpreter);
        return TCL_OK;
    }
    if (action == "show" && count == 3) {
        const auto instance = text(arguments[2]);
        const auto found = find(instance);
        if (found == configuration.bindings().end()) {
            return fail(interpreter, "no binding for instance '"
                    + std::string { instance } + "'");
        }
        Tcl_SetObjResult(interpreter, binding_object(*found));
        return TCL_OK;
    }
    if (action == "list" && count <= 3) {
        const std::optional<std::string_view> pattern = count == 3
            ? std::optional { text(arguments[2]) }
            : std::nullopt;
        auto* result = Tcl_NewListObj(0, nullptr);
        for (const auto& binding : configuration.bindings()) {
            if (matches(pattern, binding.entry.instance)) {
                Tcl_ListObjAppendElement(nullptr, result, binding_object(binding));
            }
        }
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }
    if (action == "clear" && count == 2) {
        configuration.clear_bindings();
        Tcl_ResetResult(interpreter);
        return TCL_OK;
    }
    return fail(interpreter, "usage: " + std::string { usage });
}

int configuration_settings_command(TclConfigurationState& state,
    Tcl_Interp* interpreter, const Tcl_Size count, Tcl_Obj* const arguments[])
{
    return config_command(state, interpreter, count, arguments);
}

bool source_configuration_scripts(TclConfigurationState& state,
    Tcl_Interp* interpreter, const std::filesystem::path& workspace,
    diagnostic::Engine& diagnostics)
{
    const auto saved_layer = state.layer;
    for (const auto& [layer, script] : configuration_scripts(workspace)) {
        state.layer = layer;
        state.script = script;
        state.configuration->add_script(script);
        auto* path = text_object(fsim::support::path_to_utf8(script));
        Tcl_IncrRefCount(path);
        const auto status = Tcl_FSEvalFileEx(interpreter, path, "utf-8");
        Tcl_DecrRefCount(path);
        if (status != TCL_OK) {
            auto* options = Tcl_GetReturnOptions(interpreter, status);
            Tcl_IncrRefCount(options);
            Tcl_Obj* trace {};
            Tcl_DictObjGet(nullptr, options, text_object("-errorinfo"), &trace);
            diagnostics.error("FSIM-TCL-CFG-001",
                "configuration script '" + fsim::support::path_to_utf8(script)
                    + "' failed: "
                    + std::string { trace != nullptr
                            ? text(trace)
                            : text(Tcl_GetObjResult(interpreter)) });
            Tcl_DecrRefCount(options);
            state.layer = saved_layer;
            state.script.clear();
            return false;
        }
    }
    state.layer = saved_layer;
    state.script.clear();
    Tcl_ResetResult(interpreter);
    return true;
}

namespace {

template <int (*Command)(
    TclConfigurationState&, Tcl_Interp*, Tcl_Size, Tcl_Obj* const[])>
int standalone_command(void* client_data, Tcl_Interp* interpreter,
    const Tcl_Size count, Tcl_Obj* const arguments[]) noexcept
{
    try {
        return Command(*static_cast<TclConfigurationState*>(client_data),
            interpreter, count, arguments);
    } catch (const std::exception& exception) {
        return fail(interpreter, exception.what());
    } catch (...) {
        return fail(interpreter, "unknown failure in an fsim Tcl command");
    }
}

} // namespace

bool register_configuration_commands(
    Tcl_Interp* interpreter, TclConfigurationState& state)
{
    constexpr std::string_view global_command_path {
        "::namespace eval :: {::namespace path ::fsim}"
    };
    return Tcl_CreateNamespace(interpreter, "::fsim", nullptr, nullptr)
            != nullptr
        && Tcl_CreateObjCommand2(interpreter, "::fsim::config",
               standalone_command<configuration_settings_command>, &state,
               nullptr)
            != nullptr
        && Tcl_CreateObjCommand2(interpreter, "::fsim::coverage",
               standalone_command<coverage_settings_command>, &state, nullptr)
            != nullptr
        && Tcl_CreateObjCommand2(interpreter, "::fsim::bind",
               standalone_command<bind_settings_command>, &state, nullptr)
            != nullptr
        && Tcl_EvalEx(interpreter, global_command_path.data(),
               tcl_size(global_command_path.size()), TCL_EVAL_GLOBAL)
            == TCL_OK;
}

namespace {

int invoke_config(TclContext& context, Tcl_Interp* interpreter,
    const Tcl_Size count, Tcl_Obj* const arguments[])
{
    return configuration_settings_command(
        context.configuration, interpreter, count, arguments);
}

int invoke_coverage(TclContext& context, Tcl_Interp* interpreter,
    const Tcl_Size count, Tcl_Obj* const arguments[])
{
    return coverage_settings_command(
        context.configuration, interpreter, count, arguments);
}

int invoke_bind(TclContext& context, Tcl_Interp* interpreter,
    const Tcl_Size count, Tcl_Obj* const arguments[])
{
    return bind_settings_command(
        context.configuration, interpreter, count, arguments);
}

} // namespace

std::span<const TclCommandSpec> configuration_command_specs()
{
    static constexpr std::array config_actions {
        std::string_view { "set" }, std::string_view { "get" },
        std::string_view { "unset" }, std::string_view { "list" },
        std::string_view { "keys" },
    };
    static constexpr std::array coverage_families {
        std::string_view { "fsm" }, std::string_view { "exclude" },
    };
    static constexpr std::array entry_actions {
        std::string_view { "add" }, std::string_view { "remove" },
        std::string_view { "show" }, std::string_view { "list" },
        std::string_view { "clear" },
    };
    static constexpr std::array config_arguments {
        TclCommandArgument { "action", TclCompletionDomain::literal, false,
            false, config_actions },
        TclCommandArgument { "key", TclCompletionDomain::literal, true },
        TclCommandArgument { "value", TclCompletionDomain::literal, true },
    };
    static constexpr std::array coverage_arguments {
        TclCommandArgument { "family", TclCompletionDomain::literal, false,
            false, coverage_families },
        TclCommandArgument { "action", TclCompletionDomain::literal, false,
            false, entry_actions },
        TclCommandArgument { "option", TclCompletionDomain::literal, true, true },
    };
    static constexpr std::array bind_arguments {
        TclCommandArgument { "action", TclCompletionDomain::literal, false,
            false, entry_actions },
        TclCommandArgument { "instance", TclCompletionDomain::literal, true, true },
    };
    static constexpr std::array specs {
        TclCommandSpec {
            "fsim::bind",
            "fsim::bind add INSTANCE ?-target vhdl:|sv:|systemc:UNIT? "
            "?-resolver std_logic|sv_wire? | remove INSTANCE ... | "
            "show INSTANCE | list ?PATTERN? | clear",
            "Bind design instances to units or port resolvers at elaboration. "
            "Adding a binding for an instance that has one replaces it.",
            TclCommandCapability::workspace,
            true,
            bind_arguments,
            invoke_bind,
            TclResultShape::variant,
            "configuration",
        },
        TclCommandSpec {
            "fsim::config",
            "fsim::config set KEY VALUE | get KEY | unset KEY | "
            "list ?PATTERN? | keys ?PATTERN?",
            "Set, read, or remove the default options of fsim commands. "
            "An option given to a command overrides its default.",
            TclCommandCapability::workspace,
            true,
            config_arguments,
            invoke_config,
            TclResultShape::variant,
            "configuration",
        },
        TclCommandSpec {
            "fsim::coverage",
            "fsim::coverage fsm|exclude add ... | remove ... | show ... | "
            "list ?PATTERN? | clear",
            "Add, change, or remove coverage FSM hints and exclusions applied "
            "at elaboration. Adding an entry with an existing key replaces it.",
            TclCommandCapability::workspace,
            true,
            coverage_arguments,
            invoke_coverage,
            TclResultShape::variant,
            "configuration",
        },
    };
    return specs;
}

#endif

} // namespace fsim::app::tcl_detail
