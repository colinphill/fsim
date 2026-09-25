// SPDX-License-Identifier: Apache-2.0
#include "tcl_command_catalog.hpp"

#include "application_workspace.hpp"
#include "application_workspace_store.hpp"
#include "application_workspace_systemc.hpp"

#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <optional>
#include <sstream>
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

    Tcl_Obj* size_object(const std::size_t value)
    {
        return Tcl_NewWideIntObj(tcl_wide_size(value));
    }

    std::string object_string(Tcl_Obj* value)
    {
        Tcl_Size length { };
        const char* text = Tcl_GetStringFromObj(value, &length);
        return std::string { text, static_cast<std::size_t>(length) };
    }

    void dict_put(
        Tcl_Interp* interpreter,
        Tcl_Obj* dictionary,
        const std::string_view key,
        Tcl_Obj* value)
    {
        if (Tcl_DictObjPut(
                interpreter,
                dictionary,
                string_object(key),
                value)
            != TCL_OK) {
            throw std::runtime_error { "failed to construct a Tcl dictionary" };
        }
    }

    bool list_append(
        Tcl_Interp* interpreter, Tcl_Obj* list, Tcl_Obj* value)
    {
        return Tcl_ListObjAppendElement(interpreter, list, value) == TCL_OK;
    }

    bool put_messages(
        Tcl_Interp* interpreter,
        Tcl_Obj* dictionary,
        const std::string_view output)
    {
        Tcl_Obj* messages = Tcl_NewListObj(0, nullptr);
        std::istringstream lines { std::string { output } };
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (!line.empty()
                && !list_append(interpreter, messages, string_object(line))) {
                return false;
            }
        }
        dict_put(interpreter, dictionary, "messages", messages);
        return true;
    }

    Tcl_Obj* position_object(
        Tcl_Interp* interpreter, const diagnostic::SourcePosition& position)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "line", size_object(position.line));
        dict_put(interpreter, result, "column", size_object(position.column));
        dict_put(interpreter, result, "offset", size_object(static_cast<std::size_t>(position.offset)));
        return result;
    }

    Tcl_Obj* span_object(
        Tcl_Interp* interpreter, const diagnostic::SourceSpan& span)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "path", string_object(span.path));
        dict_put(interpreter, result, "begin", position_object(interpreter, span.begin));
        dict_put(interpreter, result, "end", position_object(interpreter, span.end));
        return result;
    }

    Tcl_Obj* diagnostic_object(
        Tcl_Interp* interpreter, const diagnostic::Diagnostic& diagnostic_entry)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "severity",
            string_object(diagnostic::to_string(diagnostic_entry.severity)));
        dict_put(interpreter, result, "code", string_object(diagnostic_entry.code));
        dict_put(interpreter, result, "message", string_object(diagnostic_entry.message));
        dict_put(interpreter, result, "span",
            span_object(interpreter, diagnostic_entry.span));
        Tcl_Obj* notes = Tcl_NewListObj(0, nullptr);
        for (const auto& note : diagnostic_entry.notes) {
            Tcl_Obj* entry = Tcl_NewDictObj();
            dict_put(interpreter, entry, "message", string_object(note.message));
            dict_put(interpreter, entry, "span", span_object(interpreter, note.span));
            if (!list_append(interpreter, notes, entry)) {
                throw std::runtime_error { "failed to construct diagnostic notes" };
            }
        }
        dict_put(interpreter, result, "notes", notes);
        return result;
    }

    Tcl_Obj* diagnostics_object(
        Tcl_Interp* interpreter, const diagnostic::Engine& diagnostics)
    {
        Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            if (!list_append(
                    interpreter, result, diagnostic_object(interpreter, diagnostic))) {
                throw std::runtime_error { "failed to construct diagnostics list" };
            }
        }
        return result;
    }

    void append_context_diagnostics(
        TclContext& context, const diagnostic::Engine& diagnostics)
    {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            context.diagnostics.report(diagnostic);
        }
    }

    int fail_with_diagnostics(
        TclContext& context,
        Tcl_Interp* interpreter,
        diagnostic::Engine& diagnostics,
        std::string_view fallback)
    {
        if (!diagnostics.has_error()) {
            diagnostics.error("FSIM-WS-TCL001", std::string { fallback });
        }
        append_context_diagnostics(context, diagnostics);
        const auto& entries = diagnostics.diagnostics();
        const auto failed = std::find_if(entries.rbegin(), entries.rend(),
            [](const auto& entry) {
                return entry.severity == diagnostic::Severity::error
                    || entry.severity == diagnostic::Severity::fatal;
            });
        const auto& diagnostic = failed == entries.rend() ? entries.back() : *failed;
        Tcl_SetObjResult(interpreter, string_object(diagnostic.message));
        Tcl_Obj* error_code = Tcl_NewListObj(0, nullptr);
        if (!list_append(interpreter, error_code, string_object("FSIM"))
            || !list_append(interpreter, error_code, string_object(diagnostic.code))) {
            return TCL_ERROR;
        }
        Tcl_SetObjErrorCode(interpreter, error_code);
        return TCL_ERROR;
    }

    int fail_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        std::string_view code,
        std::string message)
    {
        diagnostic::Engine diagnostics;
        diagnostics.error(std::string { code }, std::move(message));
        return fail_with_diagnostics(
            context, interpreter, diagnostics, "workspace command failed");
    }

    Tcl_Obj* artifact_kind_object(const workspace::ArtifactKind kind)
    {
        switch (kind) {
        case workspace::ArtifactKind::Hdl:
            return string_object("hdl");
        case workspace::ArtifactKind::SystemCObject:
            return string_object("systemc_object");
        case workspace::ArtifactKind::SystemCPlugin:
            return string_object("systemc_plugin");
        }
        return string_object("unknown");
    }

    Tcl_Obj* unit_object(
        Tcl_Interp* interpreter,
        const workspace::OwnedUnit& owned,
        const std::string_view artifact_id)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "artifact_id", string_object(artifact_id));
        dict_put(interpreter, result, "source", string_object(owned.source_key));
        dict_put(interpreter, result, "language", string_object(owned.unit.language));
        dict_put(interpreter, result, "kind", string_object(owned.unit.kind));
        dict_put(interpreter, result, "name", string_object(owned.unit.name));
        dict_put(interpreter, result, "primary_name",
            string_object(owned.unit.primary_name));
        dict_put(interpreter, result, "architecture",
            string_object(owned.unit.architecture));
        dict_put(interpreter, result, "artifact",
            string_object(fsim::support::path_to_utf8(owned.unit.artifact)));
        dict_put(interpreter, result, "checksum", string_object(owned.unit.checksum));
        dict_put(interpreter, result, "standard", string_object(owned.unit.standard));
        dict_put(interpreter, result, "compatibility_profile",
            string_object(owned.unit.compatibility_profile));
        return result;
    }

    Tcl_Obj* artifact_object(
        Tcl_Interp* interpreter, const workspace::ArtifactRecord& artifact)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "id", string_object(artifact.id));
        dict_put(interpreter, result, "kind", artifact_kind_object(artifact.kind));
        dict_put(interpreter, result, "path",
            string_object(fsim::support::path_to_utf8(artifact.path)));
        Tcl_Obj* sources = Tcl_NewListObj(0, nullptr);
        for (const auto& source : artifact.sources) {
            if (!list_append(interpreter, sources, string_object(source))) {
                throw std::runtime_error { "failed to construct source list" };
            }
        }
        dict_put(interpreter, result, "sources", sources);
        Tcl_Obj* units = Tcl_NewListObj(0, nullptr);
        for (const auto& unit : artifact.units) {
            if (!list_append(
                    interpreter, units, unit_object(interpreter, unit, artifact.id))) {
                throw std::runtime_error { "failed to construct owned-unit list" };
            }
        }
        dict_put(interpreter, result, "units", units);
        Tcl_Obj* dependencies = Tcl_NewListObj(0, nullptr);
        for (const auto& dependency : artifact.dependencies) {
            Tcl_Obj* entry = Tcl_NewDictObj();
            dict_put(interpreter, entry, "library", string_object(dependency.library));
            dict_put(interpreter, entry, "language",
                string_object(dependency.unit.language));
            dict_put(interpreter, entry, "kind", string_object(dependency.unit.kind));
            dict_put(interpreter, entry, "name", string_object(dependency.unit.name));
            dict_put(interpreter, entry, "primary_name",
                string_object(dependency.unit.primary_name));
            dict_put(interpreter, entry, "architecture",
                string_object(dependency.unit.architecture));
            dict_put(interpreter, entry, "fingerprint",
                string_object(dependency.fingerprint));
            if (!list_append(interpreter, dependencies, entry)) {
                throw std::runtime_error { "failed to construct dependency list" };
            }
        }
        dict_put(interpreter, result, "dependencies", dependencies);
        return result;
    }

    Tcl_Obj* library_object(
        Tcl_Interp* interpreter, const workspace::LibraryLocation& location)
    {
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "library", string_object(location.name));
        dict_put(interpreter, result, "path",
            string_object(fsim::support::path_to_utf8(location.directory)));
        dict_put(interpreter, result, "mapped", Tcl_NewBooleanObj(location.mapped));
        return result;
    }

    std::optional<workspace::LibraryLocation> library_location(
        const workspace::Store& store,
        const std::string_view name,
        std::string& error)
    {
        auto catalog = store.read_library(name, error);
        if (!catalog) {
            return std::nullopt;
        }
        return catalog->location;
    }

    cli::Verbosity verbosity_from_text(std::string_view value)
    {
        if (value == "quiet") {
            return cli::Verbosity::quiet;
        }
        if (value == "normal") {
            return cli::Verbosity::normal;
        }
        if (value == "verbose") {
            return cli::Verbosity::verbose;
        }
        throw std::invalid_argument { "verbosity must be quiet, normal, or verbose" };
    }

    std::optional<project::Language> language_from_extension(
        const std::filesystem::path& path)
    {
        auto extension = path.extension().string();
        std::ranges::transform(extension, extension.begin(), [](const unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (extension == ".vhd" || extension == ".vhdl") {
            return project::Language::vhdl;
        }
        if (extension == ".v") {
            return project::Language::verilog;
        }
        if (extension == ".sv" || extension == ".svh") {
            return project::Language::system_verilog;
        }
        if (extension == ".cpp" || extension == ".cc" || extension == ".cxx") {
            return project::Language::systemc;
        }
        return std::nullopt;
    }

    std::filesystem::path normalized_source_path(
        const project::Config& config, const std::filesystem::path& path)
    {
        const auto rooted = path.is_absolute() ? path : config.base_directory / path;
        return rooted.lexically_normal();
    }

    struct CompileOptions {
        std::optional<project::Language> language;
        std::string library { "work" };
        std::optional<std::string> standard;
        cli::Verbosity verbosity { cli::Verbosity::normal };
        std::vector<std::filesystem::path> sources;
    };

    std::optional<CompileOptions> parse_compile_options(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        CompileOptions result;
        result.verbosity = context.invocation.verbosity;
        bool language_seen { false };
        bool library_seen { false };
        bool standard_seen { false };
        bool verbosity_seen { false };
        Tcl_Size index = 1;
        for (; index < argument_count; ++index) {
            const auto option = object_string(arguments[index]);
            if (option == "--") {
                ++index;
                break;
            }
            if (option.empty() || !option.starts_with('-')) {
                break;
            }
            if (option != "-lang" && option != "-library"
                && option != "-standard" && option != "-verbosity") {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "unknown fsim::compile option '" + option + "'");
                return std::nullopt;
            }
            if (index + 1 >= argument_count) {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "fsim::compile option '" + option + "' requires a value");
                return std::nullopt;
            }
            const auto value = object_string(arguments[++index]);
            if (value.empty() || value.find('\0') != std::string::npos) {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "fsim::compile option '" + option + "' requires a non-empty value");
                return std::nullopt;
            }
            if (option == "-lang") {
                if (language_seen) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "fsim::compile accepts -lang only once");
                    return std::nullopt;
                }
                language_seen = true;
                result.language = project::parse_language(value);
                if (!result.language) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "unsupported source language '" + value + "'");
                    return std::nullopt;
                }
            } else if (option == "-library") {
                if (library_seen) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "fsim::compile accepts -library only once");
                    return std::nullopt;
                }
                library_seen = true;
                result.library = value;
            } else if (option == "-standard") {
                if (standard_seen) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "fsim::compile accepts -standard only once");
                    return std::nullopt;
                }
                standard_seen = true;
                result.standard = value;
            } else {
                if (verbosity_seen) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "fsim::compile accepts -verbosity only once");
                    return std::nullopt;
                }
                verbosity_seen = true;
                try {
                    result.verbosity = verbosity_from_text(value);
                } catch (const std::invalid_argument& error) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002", error.what());
                    return std::nullopt;
                }
            }
        }
        for (; index < argument_count; ++index) {
            auto source = object_string(arguments[index]);
            if (source.empty() || source.find('\0') != std::string::npos) {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "fsim::compile source paths must be non-empty and contain no NUL bytes");
                return std::nullopt;
            }
            result.sources.emplace_back(std::move(source));
        }
        if (result.sources.empty()) {
            fail_command(context, interpreter, "FSIM-WS-TCL002",
                "fsim::compile requires at least one source path");
            return std::nullopt;
        }
        if (!result.language) {
            for (const auto& source : result.sources) {
                const auto inferred = language_from_extension(source);
                if (!inferred) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "cannot infer the language for '"
                            + fsim::support::path_to_utf8(source)
                            + "'; pass -lang");
                    return std::nullopt;
                }
                if (result.language && *result.language != *inferred) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "source paths contain more than one language; compile each language separately");
                    return std::nullopt;
                }
                result.language = inferred;
            }
        }
        return result;
    }

    std::optional<std::vector<std::string>> source_identities(
        const project::Config& config,
        const std::vector<std::filesystem::path>& sources,
        std::string& error)
    {
        std::vector<std::string> result;
        result.reserve(sources.size());
        for (const auto& source : sources) {
            auto identity = workspace::source_identity(
                normalized_source_path(config, source), error);
            if (!identity) {
                return std::nullopt;
            }
            result.push_back(std::move(*identity));
        }
        return result;
    }

    bool source_matches(
        const workspace::ArtifactRecord& artifact,
        const std::vector<std::string>& source_keys)
    {
        return std::ranges::any_of(artifact.sources, [&](const auto& source) {
            return std::ranges::find(source_keys, source) != source_keys.end();
        });
    }

    std::optional<project::Config> compile_config(
        const TclContext& context,
        const CompileOptions& options,
        diagnostic::Engine& diagnostics)
    {
        auto config = context.initial_config;
        if (*options.language == project::Language::systemc) {
            if (options.standard) {
                diagnostics.error("FSIM-WS-TCL002",
                    "-standard is not applicable to SystemC compilation");
                return std::nullopt;
            }
            return config;
        }
        project::SourceSet source_set;
        const auto inherited = std::ranges::find_if(
            config.source_sets, [&](const auto& candidate) {
                return candidate.language == *options.language;
            });
        if (inherited != config.source_sets.end()) {
            source_set = *inherited;
        }
        source_set.language = *options.language;
        source_set.library = options.library;
        source_set.files.clear();
        source_set.file_patterns.clear();
        for (const auto& source : options.sources) {
            const auto path = normalized_source_path(config, source);
            source_set.files.push_back(path);
            source_set.file_patterns.push_back(path);
        }
        const auto requested_standard = options.standard
            ? std::string_view { *options.standard }
            : source_set.standard.empty()
            ? project::default_standard(*options.language)
            : std::string_view { source_set.standard };
        const auto standard = project::canonical_standard(
            *options.language, requested_standard);
        if (!standard) {
            diagnostics.error("FSIM-WS-TCL002",
                "unsupported standard '" + std::string { requested_standard }
                    + "' for " + std::string { project::to_string(*options.language) });
            return std::nullopt;
        }
        source_set.standard = std::string { *standard };
        if (inherited == config.source_sets.end()
            || config.manifest_path == "<workspace>") {
            source_set.compilation_unit = *options.language == project::Language::vhdl
                ? "file"
                : "source-set";
        }
        config.source_sets.assign(1, std::move(source_set));
        config.project.top.clear();
        config.project.tops.clear();
        return config;
    }

    int compile_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        auto options = parse_compile_options(
            context, interpreter, argument_count, arguments);
        if (!options) {
            return TCL_ERROR;
        }
        diagnostic::Engine operation_diagnostics;
        auto config = compile_config(context, *options, operation_diagnostics);
        if (!config) {
            return fail_with_diagnostics(
                context, interpreter, operation_diagnostics,
                "cannot configure Tcl workspace compilation");
        }

        cli::Invocation invocation = context.invocation;
        invocation.command = *options->language == project::Language::systemc
            ? cli::Command::systemc_compile
            : cli::Command::compile;
        invocation.verbosity = options->verbosity;
        invocation.library = options->library;
        invocation.files.clear();
        for (const auto& source : options->sources) {
            invocation.files.push_back(normalized_source_path(*config, source));
        }
        if (*options->language != project::Language::systemc) {
            invocation.language = options->language;
            invocation.standard = config->source_sets.front().standard;
        }
        std::ostringstream output;
        std::ostringstream error_output;
        const int status = *options->language == project::Language::systemc
            ? application_detail::handle_workspace_systemc_compile(
                  invocation, *config, operation_diagnostics, output, error_output)
            : application_detail::handle_workspace_compile(
                  invocation, *config, operation_diagnostics, output, error_output);
        if (status != 0 || operation_diagnostics.has_error()) {
            return fail_with_diagnostics(
                context, interpreter, operation_diagnostics,
                "workspace compilation failed");
        }

        catalog_changed(
            context, options->library, TclCatalogMutation::replaced);
        workspace::Store store(config->base_directory);
        std::string error;
        const auto catalog = store.read_library(options->library, error);
        if (!catalog) {
            operation_diagnostics.error("FSIM-WS-TCL001", std::move(error));
            return fail_with_diagnostics(
                context, interpreter, operation_diagnostics,
                "cannot read compiled library catalog");
        }
        const auto source_keys = source_identities(
            *config, invocation.files, error);
        if (!source_keys) {
            operation_diagnostics.error("FSIM-WS-TCL001", std::move(error));
            return fail_with_diagnostics(
                context, interpreter, operation_diagnostics,
                "cannot identify compiled sources");
        }

        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "library", string_object(options->library));
        dict_put(interpreter, result, "language",
            string_object(project::to_string(*options->language)));
        Tcl_Obj* sources = Tcl_NewListObj(0, nullptr);
        for (const auto& source : invocation.files) {
            if (!list_append(interpreter, sources,
                    string_object(fsim::support::path_to_utf8(source)))) {
                return TCL_ERROR;
            }
        }
        dict_put(interpreter, result, "sources", sources);
        Tcl_Obj* artifacts = Tcl_NewListObj(0, nullptr);
        Tcl_Obj* units = Tcl_NewListObj(0, nullptr);
        std::size_t artifact_count { };
        for (const auto& artifact : catalog->artifacts) {
            if (!source_matches(artifact, *source_keys)) {
                continue;
            }
            ++artifact_count;
            if (!list_append(interpreter, artifacts,
                    artifact_object(interpreter, artifact))) {
                return TCL_ERROR;
            }
            for (const auto& unit : artifact.units) {
                if (!list_append(interpreter, units,
                        unit_object(interpreter, unit, artifact.id))) {
                    return TCL_ERROR;
                }
            }
        }
        dict_put(interpreter, result, "object_count", size_object(artifact_count));
        dict_put(interpreter, result, "objects", artifacts);
        dict_put(interpreter, result, "owned_units", units);
        if (!put_messages(interpreter, result, output.str())) {
            return TCL_ERROR;
        }
        dict_put(interpreter, result, "diagnostics",
            diagnostics_object(interpreter, operation_diagnostics));
        append_context_diagnostics(context, operation_diagnostics);
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }

    struct ElaborateOptions {
        std::string snapshot;
        cli::Verbosity verbosity { cli::Verbosity::normal };
        std::vector<std::string> tops;
    };

    std::optional<ElaborateOptions> parse_elaborate_options(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        ElaborateOptions result;
        result.snapshot = context.invocation.snapshot;
        result.verbosity = context.invocation.verbosity;
        bool snapshot_seen { false };
        bool verbosity_seen { false };
        Tcl_Size index = 1;
        for (; index < argument_count; ++index) {
            const auto option = object_string(arguments[index]);
            if (option == "--") {
                ++index;
                break;
            }
            if (option.empty() || !option.starts_with('-')) {
                break;
            }
            if (option != "-snapshot" && option != "-verbosity") {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "unknown fsim::elaborate option '" + option + "'");
                return std::nullopt;
            }
            if (index + 1 >= argument_count) {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "fsim::elaborate option '" + option + "' requires a value");
                return std::nullopt;
            }
            const auto value = object_string(arguments[++index]);
            if (value.empty() || value.find('\0') != std::string::npos) {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "fsim::elaborate option '" + option + "' requires a non-empty value");
                return std::nullopt;
            }
            if (option == "-snapshot") {
                if (snapshot_seen) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "fsim::elaborate accepts -snapshot only once");
                    return std::nullopt;
                }
                snapshot_seen = true;
                result.snapshot = value;
            } else {
                if (verbosity_seen) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002",
                        "fsim::elaborate accepts -verbosity only once");
                    return std::nullopt;
                }
                verbosity_seen = true;
                try {
                    result.verbosity = verbosity_from_text(value);
                } catch (const std::invalid_argument& error) {
                    fail_command(context, interpreter, "FSIM-WS-TCL002", error.what());
                    return std::nullopt;
                }
            }
        }
        for (; index < argument_count; ++index) {
            const auto top = object_string(arguments[index]);
            if (top.empty() || top.find('\0') != std::string::npos) {
                fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "fsim::elaborate top names must be non-empty and contain no NUL bytes");
                return std::nullopt;
            }
            result.tops.push_back(top);
        }
        if (result.tops.empty()) {
            fail_command(context, interpreter, "FSIM-WS-TCL002",
                "fsim::elaborate requires at least one top name");
            return std::nullopt;
        }
        return result;
    }

    int elaborate_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        auto options = parse_elaborate_options(
            context, interpreter, argument_count, arguments);
        if (!options) {
            return TCL_ERROR;
        }
        auto config = context.initial_config;
        config.project.tops.clear();
        config.project.top.clear();
        for (const auto& top : options->tops) {
            config.project.tops.push_back({ top, { } });
        }
        config.project.top = options->tops.front();

        cli::Invocation invocation = context.invocation;
        invocation.command = cli::Command::elaborate;
        invocation.snapshot = options->snapshot;
        invocation.verbosity = options->verbosity;
        invocation.tops.clear();
        invocation.top.reset();
        for (const auto& top : options->tops) {
            invocation.tops.push_back({ top, { } });
        }
        invocation.top = options->tops.front();

        diagnostic::Engine operation_diagnostics;
        std::ostringstream output;
        std::ostringstream error_output;
        const int status = application_detail::handle_workspace_elaborate(
            invocation, config, operation_diagnostics, output, error_output);
        if (status != 0 || operation_diagnostics.has_error()) {
            return fail_with_diagnostics(
                context, interpreter, operation_diagnostics,
                "workspace elaboration failed");
        }
        auto built = load_workspace_snapshot(
            invocation, config, operation_diagnostics);
        if (!built || operation_diagnostics.has_error()) {
            return fail_with_diagnostics(
                context, interpreter, operation_diagnostics,
                "cannot inspect the elaborated snapshot");
        }

        const auto& design = built->design_ir;
        Tcl_Obj* result = Tcl_NewDictObj();
        dict_put(interpreter, result, "snapshot", string_object(options->snapshot));
        dict_put(interpreter, result, "top", string_object(design.top()));
        Tcl_Obj* roots = Tcl_NewListObj(0, nullptr);
        for (const auto root : design.roots()) {
            if (!list_append(interpreter, roots,
                    string_object(design.path(root)))) {
                return TCL_ERROR;
            }
        }
        dict_put(interpreter, result, "roots", roots);
        Tcl_Obj* selected_tops = Tcl_NewListObj(0, nullptr);
        for (const auto& top : options->tops) {
            if (!list_append(interpreter, selected_tops, string_object(top))) {
                return TCL_ERROR;
            }
        }
        dict_put(interpreter, result, "selected_tops", selected_tops);
        std::size_t signal_count { };
        for (const auto& object : design.objects()) {
            if (object.kind == semantic::design::ObjectKind::signal
                && !object.parent_object) {
                ++signal_count;
            }
        }
        Tcl_Obj* counts = Tcl_NewDictObj();
        dict_put(interpreter, counts, "roots", size_object(design.roots().size()));
        dict_put(interpreter, counts, "objects", size_object(design.objects().size()));
        dict_put(interpreter, counts, "signals", size_object(signal_count));
        dict_put(interpreter, counts, "processes", size_object(design.processes().size()));
        dict_put(interpreter, result, "counts", counts);
        if (!put_messages(interpreter, result, output.str())) {
            return TCL_ERROR;
        }
        dict_put(interpreter, result, "diagnostics",
            diagnostics_object(interpreter, operation_diagnostics));
        append_context_diagnostics(context, operation_diagnostics);
        Tcl_SetObjResult(interpreter, result);
        return TCL_OK;
    }

    int library_command(
        TclContext& context,
        Tcl_Interp* interpreter,
        const Tcl_Size argument_count,
        Tcl_Obj* const arguments[])
    {
        if (argument_count < 2) {
            return fail_command(context, interpreter, "FSIM-WS-TCL002",
                "fsim::library requires list, map, unmap, objects, delete-object, or delete");
        }
        const auto operation = object_string(arguments[1]);
        workspace::Store store(context.config.base_directory);
        std::string error;
        if (operation == "list") {
            if (argument_count != 2) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "usage: fsim::library list");
            }
            const auto locations = store.library_locations(error);
            if (!locations) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
            for (const auto& location : *locations) {
                if (!list_append(interpreter, result,
                        library_object(interpreter, location))) {
                    return TCL_ERROR;
                }
            }
            Tcl_SetObjResult(interpreter, result);
            return TCL_OK;
        }
        if (operation == "map") {
            if (argument_count != 4) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "usage: fsim::library map NAME DIRECTORY");
            }
            const auto name = object_string(arguments[2]);
            const auto directory_text = object_string(arguments[3]);
            if (name.empty() || directory_text.empty()
                || name.find('\0') != std::string::npos
                || directory_text.find('\0') != std::string::npos) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "library names and directory paths must be non-empty and contain no NUL bytes");
            }
            std::filesystem::path directory { directory_text };
            if (directory.is_relative()) {
                directory = context.config.base_directory / directory;
            }
            if (!store.map_library(name, directory.lexically_normal(), error)) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            catalog_changed(context, name, TclCatalogMutation::remapped);
            const auto location = library_location(store, name, error);
            if (!location) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            Tcl_SetObjResult(interpreter, library_object(interpreter, *location));
            return TCL_OK;
        }
        if (operation == "unmap") {
            if (argument_count != 3) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "usage: fsim::library unmap NAME");
            }
            const auto name = object_string(arguments[2]);
            if (name.empty() || name.find('\0') != std::string::npos) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "library name must be non-empty and contain no NUL bytes");
            }
            if (!store.unmap_library(name, error)) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            catalog_changed(context, name, TclCatalogMutation::remapped);
            const auto location = library_location(store, name, error);
            if (!location) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            Tcl_SetObjResult(interpreter, library_object(interpreter, *location));
            return TCL_OK;
        }
        if (operation == "objects") {
            if (argument_count != 3) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "usage: fsim::library objects NAME");
            }
            const auto name = object_string(arguments[2]);
            const auto catalog = store.read_library(name, error);
            if (!catalog) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            Tcl_Obj* result = Tcl_NewListObj(0, nullptr);
            for (const auto& artifact : catalog->artifacts) {
                if (!list_append(interpreter, result,
                        artifact_object(interpreter, artifact))) {
                    return TCL_ERROR;
                }
            }
            Tcl_SetObjResult(interpreter, result);
            return TCL_OK;
        }
        if (operation == "delete-object") {
            if (argument_count != 4) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "usage: fsim::library delete-object NAME ARTIFACT_ID");
            }
            const auto name = object_string(arguments[2]);
            const auto artifact_id = object_string(arguments[3]);
            if (!store.delete_artifact(name, artifact_id, error)) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            catalog_changed(context, name, TclCatalogMutation::object_deleted);
            Tcl_Obj* result = Tcl_NewDictObj();
            dict_put(interpreter, result, "library", string_object(name));
            dict_put(interpreter, result, "artifact_id", string_object(artifact_id));
            dict_put(interpreter, result, "deleted", Tcl_NewBooleanObj(true));
            Tcl_SetObjResult(interpreter, result);
            return TCL_OK;
        }
        if (operation == "delete") {
            if (argument_count != 3) {
                return fail_command(context, interpreter, "FSIM-WS-TCL002",
                    "usage: fsim::library delete NAME");
            }
            const auto name = object_string(arguments[2]);
            std::size_t old_artifact_count { };
            const auto old_catalog = store.read_library(name, error);
            if (old_catalog) {
                old_artifact_count = old_catalog->artifacts.size();
            }
            error.clear();
            if (!store.delete_library(name, error)) {
                return fail_command(context, interpreter, "FSIM-WS-TCL001",
                    std::move(error));
            }
            catalog_changed(context, name, TclCatalogMutation::library_deleted);
            Tcl_Obj* result = Tcl_NewDictObj();
            dict_put(interpreter, result, "library", string_object(name));
            dict_put(interpreter, result, "deleted", Tcl_NewBooleanObj(true));
            dict_put(interpreter, result, "artifact_count",
                size_object(old_artifact_count));
            Tcl_SetObjResult(interpreter, result);
            return TCL_OK;
        }
        return fail_command(context, interpreter, "FSIM-WS-TCL002",
            "unknown fsim::library operation '" + operation + "'");
    }

    constexpr std::array<std::string_view, 4> language_choices {
        "vhdl", "verilog", "systemverilog", "systemc"
    };
    constexpr std::array<std::string_view, 3> verbosity_choices {
        "quiet", "normal", "verbose"
    };
    constexpr std::array<std::string_view, 6> library_subcommand_choices {
        "list", "map", "unmap", "objects", "delete-object", "delete"
    };
    constexpr std::array compile_arguments {
        TclCommandArgument {
            "-lang", TclCompletionDomain::literal, true, false, language_choices },
        TclCommandArgument { "-library", TclCompletionDomain::library, true },
        TclCommandArgument { "-standard", TclCompletionDomain::literal, true },
        TclCommandArgument {
            "-verbosity", TclCompletionDomain::literal, true, false, verbosity_choices },
        TclCommandArgument { "source", TclCompletionDomain::source_path, false, true },
    };
    constexpr std::array elaborate_arguments {
        TclCommandArgument { "-snapshot", TclCompletionDomain::snapshot, true },
        TclCommandArgument {
            "-verbosity", TclCompletionDomain::literal, true, false, verbosity_choices },
        TclCommandArgument { "top", TclCompletionDomain::compiled_definition, false, true },
    };
    constexpr std::array library_arguments {
        TclCommandArgument {
            "subcommand", TclCompletionDomain::literal, false, false,
            library_subcommand_choices },
    };
    constexpr std::array map_arguments {
        TclCommandArgument { "name", TclCompletionDomain::library },
        TclCommandArgument { "directory", TclCompletionDomain::directory_path },
    };
    constexpr std::array unmap_arguments {
        TclCommandArgument { "name", TclCompletionDomain::library },
    };
    constexpr std::array objects_arguments {
        TclCommandArgument { "name", TclCompletionDomain::library },
    };
    constexpr std::array delete_object_arguments {
        TclCommandArgument { "name", TclCompletionDomain::library },
        TclCommandArgument { "artifact_id", TclCompletionDomain::literal },
    };
    constexpr std::array delete_library_arguments {
        TclCommandArgument { "name", TclCompletionDomain::library },
    };
    constexpr std::array library_subcommands {
        TclSubcommandSpec {
            "list", "fsim::library list", "List workspace libraries.", { } },
        TclSubcommandSpec {
            "map", "fsim::library map NAME DIRECTORY",
            "Map a logical library to a compiled library directory.", map_arguments },
        TclSubcommandSpec {
            "unmap", "fsim::library unmap NAME",
            "Remove an external mapping for a logical library.", unmap_arguments },
        TclSubcommandSpec {
            "objects", "fsim::library objects NAME",
            "List the artifacts and units owned by a library.", objects_arguments },
        TclSubcommandSpec {
            "delete-object", "fsim::library delete-object NAME ARTIFACT_ID",
            "Delete one managed library artifact.", delete_object_arguments },
        TclSubcommandSpec {
            "delete", "fsim::library delete NAME",
            "Delete a managed library and its artifact catalog.", delete_library_arguments },
    };

} // namespace

std::span<const TclCommandSpec> workspace_command_specs()
{
    static constexpr std::array specs {
        TclCommandSpec {
            "fsim::compile",
            "fsim::compile ?-lang LANGUAGE? ?-library LIBRARY? ?-standard STANDARD? ?-verbosity LEVEL? SOURCE ...",
            "Compile source files into a managed workspace library and return structured "
            "results with progress messages and diagnostics.",
            TclCommandCapability::workspace,
            false,
            compile_arguments,
            compile_command,
            TclResultShape::dictionary,
            "workspace",
        },
        TclCommandSpec {
            "fsim::elaborate",
            "fsim::elaborate ?-snapshot NAME? ?-verbosity LEVEL? TOP ...",
            "Elaborate workspace library units into a managed snapshot and return "
            "progress messages with structured results.",
            TclCommandCapability::workspace,
            false,
            elaborate_arguments,
            elaborate_command,
            TclResultShape::dictionary,
            "workspace",
        },
        TclCommandSpec {
            "fsim::library",
            "fsim::library list | map NAME DIRECTORY | unmap NAME | objects NAME | delete-object NAME ARTIFACT_ID | delete NAME",
            "List, map, inspect, or delete managed workspace libraries and artifacts.",
            TclCommandCapability::workspace,
            false,
            library_arguments,
            library_command,
            TclResultShape::variant,
            "workspace",
            library_subcommands,
        },
    };
    return specs;
}

#endif

} // namespace fsim::app::tcl_detail
