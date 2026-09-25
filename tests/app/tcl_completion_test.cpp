// SPDX-License-Identifier: Apache-2.0
#include "tcl_completion.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace fsim::app::tcl_completion;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        throw std::runtime_error { std::string { message } };
    }
}

bool has_candidate(
    const CompletionResult& result,
    const std::string_view display,
    const CandidateKind kind)
{
    return std::ranges::any_of(result.candidates, [display, kind](const Candidate& candidate) {
        return candidate.display == display && candidate.kind == kind;
    });
}

bool has_span(const CompletionResult& result, const SpanKind kind)
{
    return std::ranges::any_of(result.spans, [kind](const Span& span) {
        return span.kind == kind;
    });
}

CompletionSnapshot make_snapshot(const Generations generations)
{
    static constexpr std::array debug_break_actions {
        CompletionChoice { "add", "Add a breakpoint" },
        CompletionChoice { "delete", "Delete a breakpoint" },
    };
    static constexpr std::array debug_break_kinds {
        CompletionChoice { "signal", "Break on a signal" },
        CompletionChoice { "time", "Break at a simulation time" },
    };
    static constexpr std::array load_arguments {
        CompletionArgument { ArgumentDomain::snapshot, false, false },
    };
    static constexpr std::array compile_arguments {
        CompletionArgument { ArgumentDomain::source_path, false, true },
    };
    static constexpr std::array language_choices {
        CompletionChoice { "vhdl", "VHDL source" },
        CompletionChoice { "systemverilog", "SystemVerilog source" },
    };
    static constexpr std::array compile_options {
        CompletionOption { "-lang", "Select the source language", ArgumentDomain::literal,
            language_choices },
        CompletionOption { "-library", "Select the target library", ArgumentDomain::library },
    };
    static constexpr std::array map_arguments {
        CompletionArgument { ArgumentDomain::library, false, false, "name" },
        CompletionArgument { ArgumentDomain::directory_path, false, false, "directory" },
    };
    static constexpr std::array library_subcommands {
        CompletionSubcommand { "list", "fsim::library list", "List workspace libraries", { }, { } },
        CompletionSubcommand { "map", "fsim::library map NAME DIRECTORY",
            "Map a logical library", map_arguments, { } },
    };
    static constexpr std::array break_arguments {
        CompletionArgument { ArgumentDomain::literal, false, false,
            "action", debug_break_actions },
        CompletionArgument { ArgumentDomain::literal, true, false,
            "kind", debug_break_kinds },
        CompletionArgument { ArgumentDomain::debugger_entity, true, false, "location" },
    };
    static constexpr std::array debug_subcommands {
        CompletionSubcommand { "break", "fsim::debug break ACTION ?LOCATION?",
            "Manage debugger breakpoints", break_arguments, { } },
    };
    static constexpr std::array object_reference_arguments {
        CompletionArgument { ArgumentDomain::design_object, false, false, "reference" },
    };
    static constexpr std::array definitions_arguments {
        CompletionArgument { ArgumentDomain::library, true, false, "library" },
    };
    static constexpr std::array definition_arguments {
        CompletionArgument { ArgumentDomain::library, false, false, "library" },
        CompletionArgument { ArgumentDomain::compiled_definition, false, false, "name" },
    };
    static constexpr std::array object_subcommands {
        CompletionSubcommand { "roots", "fsim::object roots", "List design roots",
            { }, { }, capability_mask(Capability::loaded_design) },
        CompletionSubcommand { "resolve", "fsim::object resolve PATH", "Resolve a path",
            object_reference_arguments, { }, capability_mask(Capability::loaded_design) },
        CompletionSubcommand { "info", "fsim::object info REF", "Inspect an object",
            object_reference_arguments, { }, capability_mask(Capability::loaded_design) },
        CompletionSubcommand { "definitions", "fsim::object definitions ?LIBRARY?",
            "List compiled definitions", definitions_arguments, { },
            capability_mask(Capability::workspace) },
        CompletionSubcommand { "definition", "fsim::object definition LIBRARY NAME",
            "Resolve a compiled package or class", definition_arguments, { },
            capability_mask(Capability::workspace) },
    };
    static constexpr std::array commands {
        CompletionCommand { "::fsim::compile", "fsim::compile SOURCE ...", "Compile source files",
            capability_mask(Capability::workspace), false, compile_arguments, compile_options },
        CompletionCommand { "::fsim::debug", "fsim::debug break ACTION ?LOCATION?", "Debug a simulation",
            capability_mask(Capability::debugger), true, { }, { }, debug_subcommands },
        CompletionCommand { "::fsim::load", "fsim::load ?SNAPSHOT?", "Load a workspace snapshot",
            capability_mask(Capability::workspace), false, load_arguments, { } },
        CompletionCommand { "::fsim::object", "fsim::object resolve PATH", "Resolve a loaded object",
            capability_mask(Capability::workspace), true, { }, { }, object_subcommands },
        CompletionCommand { "::fsim::library", "fsim::library map NAME DIRECTORY",
            "Manage workspace libraries", capability_mask(Capability::workspace), false,
            { }, { }, library_subcommands },
    };
    static constexpr std::array namespaces {
        CompletionItem { "::fsim", "The fsim command namespace", CandidateKind::namespace_name, 0U },
        CompletionItem { "::tcl", "The Tcl support namespace", CandidateKind::namespace_name, 0U },
    };
    static constexpr std::array procedures {
        CompletionItem { "::fsim::load_design", "Load a design helper procedure", CandidateKind::procedure, 0U },
    };
    static constexpr std::array variables {
        CompletionItem { "foo", "A Tcl variable", CandidateKind::variable, 0U },
        CompletionItem { "forest", "A second Tcl variable", CandidateKind::variable, 0U },
    };
    static constexpr std::array paths {
        CompletionItem { "src/hdl/δ_top.sv", "SystemVerilog source", CandidateKind::path, 0U },
        CompletionItem { "src/hdl", "HDL source directory", CandidateKind::directory, 0U },
    };
    static constexpr std::array libraries {
        CompletionItem { "work", "Default managed library", CandidateKind::library, 0U },
    };
    static constexpr std::array snapshots {
        CompletionItem { "smoke", "Smoke test snapshot", CandidateKind::snapshot, 0U },
    };
    static constexpr std::array objects {
        CompletionItem { "top.signal", "Loaded signal object", CandidateKind::design_object,
            capability_mask(Capability::loaded_design) },
        CompletionItem { "top.state", "Loaded state object", CandidateKind::design_object,
            capability_mask(Capability::loaded_design) },
    };
    static constexpr std::array debugger_entities {
        CompletionItem { "top.signal", "Signal available to the debugger",
            CandidateKind::debugger_entity, capability_mask(Capability::debugger) },
    };
    static constexpr std::array compiled_definitions {
        CompletionItem { "work::top", "Compiled module in work",
            CandidateKind::compiled_definition, capability_mask(Capability::workspace) },
    };
    static constexpr std::array packages {
        CompletionItem { "work::core_pkg", "Compiled package in work",
            CandidateKind::package, capability_mask(Capability::workspace) },
    };
    static constexpr std::array types {
        CompletionItem { "work::core_type", "Compiled package type in work",
            CandidateKind::type, capability_mask(Capability::workspace) },
    };

    CompletionSnapshot result;
    result.generations = generations;
    result.commands = commands;
    result.namespaces = namespaces;
    result.procedures = procedures;
    result.variables = variables;
    result.paths = paths;
    result.libraries = libraries;
    result.snapshots = snapshots;
    result.design_objects = objects;
    result.debugger_entities = debugger_entities;
    result.compiled_definitions = compiled_definitions;
    result.packages = packages;
    result.types = types;
    return result;
}

void test_command_completion_and_stable_order()
{
    const Generations generations { 3U, 5U, 7U, 11U };
    const auto snapshot = make_snapshot(generations);
    constexpr std::string_view input = "::fsim::co";
    CompletionRequest request { input, input.size(), RequestContext::interactive_console,
        Capability::workspace | Capability::loaded_design, generations, 16U };
    const auto first = complete(request, snapshot);
    const auto second = complete(request, snapshot);
    require(first.status == CompletionStatus::ok && first.candidates.size() == 1U,
        "catalog command prefix must produce its matching command");
    require(first.candidates[0].display == "::fsim::compile"
            && first.candidates[0].kind == CandidateKind::fsim_command
            && first.candidates[0].help.find("Compile source files") != std::string::npos,
        "command candidates must include their kind and catalog help");
    require(first.candidates[0].text == second.candidates[0].text
            && first.candidates[0].display == second.candidates[0].display,
        "repeated completion over the same snapshot must be stable");
    require(first.replace_begin_byte == 0U && first.replace_end_byte == input.size(),
        "command completion must replace the complete Tcl word by byte range");
    require(has_span(first, SpanKind::command),
        "syntax output must mark Tcl command words");
}

void test_nested_and_multiple_statement_object_completion()
{
    const Generations generations { 1U, 2U, 9U, 4U };
    const auto snapshot = make_snapshot(generations);
    const std::string input = "set note \"雪\"; set value [::fsim::object resolve top.si]";
    const auto cursor = input.find("top.si") + std::string_view { "top.si" }.size();
    CompletionRequest request { input, cursor, RequestContext::interactive_console,
        Capability::workspace | Capability::loaded_design, generations, 16U };
    const auto result = complete(request, snapshot);
    require(result.status == CompletionStatus::ok
            && result.candidates.size() == 1U
            && result.candidates[0].display == "top.signal"
            && result.candidates[0].kind == CandidateKind::design_object,
        "nested command substitution must use the active Tcl command argument domain");
    require(result.replace_begin_byte == input.find("top.si")
            && result.replace_end_byte == cursor,
        "nested Unicode input must report byte-based token offsets");
    require(std::ranges::any_of(result.hints, [](const Hint& hint) {
        return hint.text.find("Resolve a loaded object") != std::string::npos;
    }),
        "known command arguments should include usage/help hints");
}

void test_path_options_and_incomplete_quote()
{
    const Generations generations { 2U, 3U, 4U, 5U };
    const auto snapshot = make_snapshot(generations);
    constexpr std::string_view option_input = "::fsim::compile -";
    const auto option_result = complete(
        CompletionRequest { option_input, option_input.size(), RequestContext::interactive_console,
            capability_mask(Capability::workspace), generations, 16U },
        snapshot);
    require(has_candidate(option_result, "-lang", CandidateKind::option)
            && has_candidate(option_result, "-library", CandidateKind::option),
        "command options must be available from immutable descriptors");

    constexpr std::string_view option_value_input = "::fsim::compile -lang systemverilog -library wo";
    const auto option_value_result = complete(
        CompletionRequest { option_value_input, option_value_input.size(),
            RequestContext::interactive_console, capability_mask(Capability::workspace),
            generations, 16U },
        snapshot);
    require(has_candidate(option_value_result, "work", CandidateKind::library),
        "an option value domain must still complete after earlier valued options");

    constexpr std::string_view path_input = "::fsim::compile \"src/hdl/δ";
    const auto path_cursor = path_input.size();
    const auto path_result = complete(
        CompletionRequest { path_input, path_cursor, RequestContext::interactive_console,
            capability_mask(Capability::workspace), generations, 16U },
        snapshot);
    require(path_result.status == CompletionStatus::ok
            && has_candidate(path_result, "src/hdl/δ_top.sv", CandidateKind::path),
        "incomplete quoted Tcl input must complete a Unicode path without evaluation");
    require(path_result.replace_begin_byte == path_input.find('"')
            && path_result.replace_end_byte == path_input.size()
            && has_span(path_result, SpanKind::quoted_string)
            && has_span(path_result, SpanKind::diagnostic),
        "quoted completion and incomplete syntax spans must use UTF-8 byte ranges");
}

void test_subcommand_and_nested_argument_completion()
{
    const Generations generations { 12U, 13U, 14U, 15U };
    const auto snapshot = make_snapshot(generations);
    constexpr std::string_view library_input = "::fsim::library map work src/hdl";
    const auto library_result = complete(
        CompletionRequest { library_input, library_input.size(),
            RequestContext::interactive_console, capability_mask(Capability::workspace),
            generations, 16U },
        snapshot);
    require(has_candidate(library_result, "src/hdl", CandidateKind::directory),
        "library map must complete its directory argument after the library name");

    constexpr std::string_view object_input = "::fsim::object info top.si";
    const auto object_result = complete(
        CompletionRequest { object_input, object_input.size(),
            RequestContext::interactive_console,
            capability_mask(Capability::loaded_design), generations, 16U },
        snapshot);
    require(has_candidate(object_result, "top.signal", CandidateKind::design_object),
        "object info must complete its reference argument after a literal subcommand");

    constexpr std::string_view object_subcommand_input = "::fsim::object ";
    const auto workspace_only_result = complete(
        CompletionRequest { object_subcommand_input, object_subcommand_input.size(),
            RequestContext::interactive_console, capability_mask(Capability::workspace),
            generations, 16U },
        snapshot);
    require(has_candidate(workspace_only_result, "definitions", CandidateKind::literal)
            && !has_candidate(workspace_only_result, "info", CandidateKind::literal),
        "subcommand capabilities must filter loaded-design operations independently");

    constexpr std::string_view definition_input = "::fsim::object definition work work::core";
    const auto definition_result = complete(
        CompletionRequest { definition_input, definition_input.size(),
            RequestContext::interactive_console, capability_mask(Capability::workspace),
            generations, 16U },
        snapshot);
    require(has_candidate(definition_result, "work::core_pkg", CandidateKind::package)
            && has_candidate(definition_result, "work::core_type", CandidateKind::type)
            && !has_candidate(
                definition_result, "work::top", CandidateKind::compiled_definition),
        "compiled-definition domains must reach package and type snapshot providers");

    constexpr std::string_view break_action_input = "::fsim::debug break ";
    const auto break_action_result = complete(
        CompletionRequest { break_action_input, break_action_input.size(),
            RequestContext::interactive_console, capability_mask(Capability::debugger),
            generations, 16U },
        snapshot);
    require(has_candidate(break_action_result, "add", CandidateKind::literal)
            && has_candidate(break_action_result, "delete", CandidateKind::literal),
        "fixed action choices must be exposed after selecting a debugger subcommand");

    constexpr std::string_view debugger_input = "::fsim::debug break add signal top.si";
    const auto debugger_result = complete(
        CompletionRequest { debugger_input, debugger_input.size(),
            RequestContext::interactive_console, capability_mask(Capability::debugger),
            generations, 16U },
        snapshot);
    require(has_candidate(debugger_result, "top.signal", CandidateKind::debugger_entity),
        "debug break add must complete its debugger entity argument");

    constexpr std::string_view debugger_action_input = "::fsim::debug break ";
    const auto debugger_action_result = complete(
        CompletionRequest { debugger_action_input, debugger_action_input.size(),
            RequestContext::interactive_console, capability_mask(Capability::debugger),
            generations, 16U },
        snapshot);
    require(has_candidate(debugger_action_result, "add", CandidateKind::literal),
        "debug break must expose fixed mutation actions before later arguments");
}

void test_variable_completion_and_tcl_quoting()
{
    const Generations generations { 6U, 1U, 8U, 2U };
    const auto snapshot = make_snapshot(generations);
    constexpr std::string_view input = "puts \"$fo\"";
    const auto cursor = input.find("fo") + 2U;
    const auto result = complete(
        CompletionRequest { input, cursor, RequestContext::interactive_console,
            capability_mask(Capability::workspace), generations, 16U },
        snapshot);
    require(result.status == CompletionStatus::ok
            && result.replace_begin_byte == input.find("fo")
            && result.replace_end_byte == input.find('"', input.find("fo"))
            && has_candidate(result, "foo", CandidateKind::variable)
            && has_candidate(result, "forest", CandidateKind::variable),
        "variable completion must replace only the variable name and preserve Tcl syntax");

    constexpr std::string_view spaced = "::fsim::load smoke";
    const auto snapshot_result = complete(
        CompletionRequest { spaced, spaced.size(), RequestContext::interactive_console,
            capability_mask(Capability::workspace), generations, 16U },
        snapshot);
    require(has_candidate(snapshot_result, "smoke", CandidateKind::snapshot),
        "argument-domain providers must complete snapshot names");
}

void test_callback_capability_staleness_and_cursor_validation()
{
    const Generations generations { 10U, 20U, 30U, 40U };
    const auto snapshot = make_snapshot(generations);
    constexpr std::string_view command_prefix = "::fsim::";
    const auto callback_result = complete(
        CompletionRequest { command_prefix, command_prefix.size(), RequestContext::callback,
            capability_mask(Capability::workspace) | Capability::loaded_design
                | Capability::debugger,
            generations, 32U },
        snapshot);
    require(!has_candidate(callback_result, "::fsim::compile", CandidateKind::fsim_command)
            && has_candidate(callback_result, "::fsim::object", CandidateKind::fsim_command),
        "callback context must filter unsafe catalog commands by capability metadata");

    auto stale_snapshot = snapshot;
    stale_snapshot.generations.session++;
    const auto stale = complete(
        CompletionRequest { command_prefix, command_prefix.size(), RequestContext::interactive_console,
            capability_mask(Capability::workspace), generations, 8U },
        stale_snapshot);
    require(stale.status == CompletionStatus::stale_snapshot
            && !is_current(stale, generations),
        "generation-mismatched snapshots must be rejected before suggestions are returned");
    const auto live = complete(
        CompletionRequest { command_prefix, command_prefix.size(), RequestContext::interactive_console,
            capability_mask(Capability::workspace), generations, 8U },
        snapshot);
    auto changed = generations;
    changed.session++;
    require(is_current(live, generations) && !is_current(live, changed),
        "cached results must reject application after a session generation changes");

    constexpr std::string_view unicode = "set label 雪";
    const auto unicode_start = unicode.find("雪");
    const auto split_cursor = unicode_start + 1U;
    const auto invalid_cursor = complete(
        CompletionRequest { unicode, split_cursor, RequestContext::interactive_console,
            capability_mask(Capability::workspace), generations, 8U },
        snapshot);
    require(invalid_cursor.status == CompletionStatus::invalid_cursor,
        "byte cursors inside a UTF-8 codepoint must reject without splitting text");
}

void test_bounded_sorted_results()
{
    const Generations generations { };
    const auto snapshot = make_snapshot(generations);
    constexpr std::string_view input = "::fsim::";
    const auto result = complete(
        CompletionRequest { input, input.size(), RequestContext::interactive_console,
            capability_mask(Capability::workspace) | Capability::loaded_design,
            generations, 2U },
        snapshot);
    require(result.candidates.size() == 2U && result.candidates_truncated,
        "completion limits must bound and report the result set");
    require(result.candidates[0].display < result.candidates[1].display,
        "bounded candidates must remain deterministically sorted");
}

} // namespace

int main()
{
    try {
        test_command_completion_and_stable_order();
        test_nested_and_multiple_statement_object_completion();
        test_path_options_and_incomplete_quote();
        test_subcommand_and_nested_argument_completion();
        test_variable_completion_and_tcl_quoting();
        test_callback_capability_staleness_and_cursor_validation();
        test_bounded_sorted_results();
        std::cout << "Tcl completion tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
