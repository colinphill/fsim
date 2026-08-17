// SPDX-License-Identifier: Apache-2.0
#include "fsim/cli/driver.hpp"

#include "fsim/api.h"
#include "fsim/support/path.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fsim::cli {
namespace {

    constexpr int kSuccess = 0;
    constexpr int kUserError = 1;
    constexpr int kUsageError = 2;
    constexpr int kUnavailable = 3;

    std::string basename(std::string_view path)
    {
        const auto separator = path.find_last_of("/\\");
        return std::string(separator == std::string_view::npos ? path : path.substr(separator + 1));
    }

    std::string lowercase(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char character) {
            return static_cast<char>(std::tolower(character));
        });
        return result;
    }

    std::string_view command_name(const Command command)
    {
        switch (command) {
        case Command::check:
            return "check";
        case Command::build:
            return "build";
        case Command::run:
            return "run";
        case Command::debug:
            return "debug";
        case Command::tcl:
            return "tcl";
        case Command::compile:
            return "compile";
        case Command::elaborate:
            return "elaborate";
        case Command::simulate:
            return "simulate";
        case Command::systemc_compile:
            return "systemc compile";
        case Command::systemc_link:
            return "systemc link";
        }
        return "check";
    }

    std::optional<Command> parse_command(const std::string_view spelling)
    {
        if (spelling == "check") {
            return Command::check;
        }
        if (spelling == "build") {
            return Command::build;
        }
        if (spelling == "run") {
            return Command::run;
        }
        if (spelling == "debug") {
            return Command::debug;
        }
        if (spelling == "tcl") {
            return Command::tcl;
        }
        if (spelling == "compile") {
            return Command::compile;
        }
        if (spelling == "elaborate") {
            return Command::elaborate;
        }
        if (spelling == "simulate") {
            return Command::simulate;
        }
        return std::nullopt;
    }

    bool parse_unsigned(const std::string_view spelling, std::uint64_t& result)
    {
        if (spelling.empty() || spelling.front() == '-') {
            return false;
        }
        const auto [end, error] = std::from_chars(spelling.data(), spelling.data() + spelling.size(), result);
        return error == std::errc { } && end == spelling.data() + spelling.size();
    }

    void argument_error(
        diagnostic::Engine& diagnostics,
        const std::string& message)
    {
        diagnostics.error("FSIM-CLI-0001", message, { "<command-line>", { 1, 1, 0 }, { 1, 1, 0 } });
    }

    std::optional<std::string_view> option_value(
        const std::string_view argument,
        const std::string_view name)
    {
        if (argument.size() > name.size() && argument.starts_with(name) && argument[name.size()] == '=') {
            return argument.substr(name.size() + 1);
        }
        return std::nullopt;
    }

    std::optional<std::string_view> take_value(
        int& index,
        const int argc,
        const char* const* argv,
        const std::string_view argument,
        const std::string_view long_name,
        diagnostic::Engine& diagnostics)
    {
        if (const auto inline_value = option_value(argument, long_name)) {
            if (inline_value->empty()) {
                argument_error(
                    diagnostics, "option '" + std::string(long_name) + "' requires a value");
                return std::nullopt;
            }
            return inline_value;
        }
        if (index + 1 >= argc) {
            argument_error(
                diagnostics, "option '" + std::string(long_name) + "' requires a value");
            return std::nullopt;
        }
        ++index;
        return std::string_view(argv[index]);
    }

    bool is_option(
        const std::string_view argument,
        const std::string_view short_name,
        const std::string_view long_name)
    {
        return argument == short_name || argument == long_name || option_value(argument, long_name).has_value();
    }

    bool valid_top_alias(const std::string_view alias_name)
    {
        if (alias_name.empty()
            || (std::isalpha(static_cast<unsigned char>(alias_name.front())) == 0
                && alias_name.front() != '_')) {
            return false;
        }
        return std::ranges::all_of(
            alias_name,
            [](const unsigned char character) {
                return std::isalnum(character) != 0 || character == '_';
            });
    }

    project::ProjectSection::TopLevel parse_top_option(
        const std::string_view spelling)
    {
        const auto separator = spelling.find('=');
        if (separator == std::string_view::npos) {
            return { std::string { spelling }, { } };
        }
        return {
            std::string { spelling.substr(separator + 1) },
            std::string { spelling.substr(0, separator) }
        };
    }

    std::optional<project::LibraryMapping> parse_library_mapping(
        const std::string_view spelling)
    {
        const auto separator = spelling.find('=');
        if (separator == std::string_view::npos || separator == 0
            || separator + 1 == spelling.size()) {
            return std::nullopt;
        }
        return project::LibraryMapping {
            std::string { spelling.substr(0, separator) },
            fsim::support::path_from_utf8(spelling.substr(separator + 1))
        };
    }

    bool valid_library_name(const std::string_view value)
    {
        if (value.empty()
            || (std::isalpha(static_cast<unsigned char>(value.front())) == 0
                && value.front() != '_')) {
            return false;
        }
        return std::ranges::all_of(
            value,
            [](const unsigned char character) {
                return std::isalnum(character) != 0 || character == '_';
            });
    }

    void validate_effective_library_mappings(
        const project::Config& config,
        diagnostic::Engine& diagnostics)
    {
        std::vector<std::string> built_libraries;
        for (const auto& source_set : config.source_sets) {
            const auto normalized = lowercase(source_set.library);
            if (std::ranges::find(built_libraries, normalized)
                == built_libraries.end()) {
                built_libraries.push_back(normalized);
            }
        }
        std::vector<std::string> mapped_libraries;
        for (const auto& mapping : config.library_mappings) {
            const auto normalized = lowercase(mapping.library);
            if (!valid_library_name(mapping.library)) {
                argument_error(
                    diagnostics,
                    "mapped logical library '" + mapping.library
                        + "' is not a safe logical-library identifier");
            } else if (normalized == "std" || normalized == "ieee"
                || normalized == "fsim") {
                argument_error(
                    diagnostics,
                    "mapped logical library '" + mapping.library
                        + "' is reserved by fsim");
            } else if (std::ranges::find(mapped_libraries, normalized)
                != mapped_libraries.end()) {
                argument_error(
                    diagnostics,
                    "duplicate mapped logical library '" + mapping.library + "'");
            } else if (std::ranges::find(built_libraries, normalized)
                != built_libraries.end()) {
                argument_error(
                    diagnostics,
                    "mapped logical library '" + mapping.library
                        + "' collides with a project-built source library");
            } else {
                mapped_libraries.push_back(normalized);
            }
            if (mapping.path.empty()) {
                argument_error(
                    diagnostics,
                    "mapped logical library '" + mapping.library
                        + "' requires a non-empty directory path");
            }
        }
    }

    std::optional<project::Optimization> parse_optimization(
        const std::string_view spelling)
    {
        const auto normalized = lowercase(spelling);
        if (normalized == "o0" || normalized == "0") {
            return project::Optimization::o0;
        }
        if (normalized == "o1" || normalized == "1") {
            return project::Optimization::o1;
        }
        if (normalized == "o2" || normalized == "2") {
            return project::Optimization::o2;
        }
        if (normalized == "o3" || normalized == "3") {
            return project::Optimization::o3;
        }
        return std::nullopt;
    }

    std::optional<project::Language> infer_language(
        const std::filesystem::path& path)
    {
        const auto extension = lowercase(path.extension().string());
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

    std::filesystem::path absolute_normalized(const std::filesystem::path& path)
    {
        std::error_code error;
        auto result = std::filesystem::absolute(path, error);
        return (error ? path : result).lexically_normal();
    }

    std::optional<project::Config> make_direct_config(
        const Invocation& invocation,
        diagnostic::Engine& diagnostics)
    {
        project::Config config;
        config.manifest_path = "<command-line>";
        std::error_code error;
        config.base_directory = std::filesystem::current_path(error);
        if (error) {
            config.base_directory = ".";
        }
        config.project.name = "command-line";
        if (!invocation.tops.empty()) {
            config.project.tops = invocation.tops;
            config.project.top = invocation.tops.size() == 1
                ? invocation.tops.front().target
                : std::string { };
        } else if (invocation.top.has_value()) {
            config.project.top = *invocation.top;
        }
        for (const auto& input : invocation.files) {
            const auto language = invocation.language.has_value() ? invocation.language : infer_language(input);
            if (!language.has_value()) {
                argument_error(
                    diagnostics,
                    "cannot infer the language for '"
                        + fsim::support::path_to_utf8(input) + "'; pass --lang");
                continue;
            }
            const auto path = absolute_normalized(input);
            std::error_code file_error;
            if (!std::filesystem::is_regular_file(path, file_error)) {
                argument_error(
                    diagnostics,
                    "source file does not exist: "
                        + fsim::support::path_to_utf8(path));
                continue;
            }

            auto iterator = std::find_if(
                config.source_sets.begin(),
                config.source_sets.end(),
                [&](const project::SourceSet& set) { return set.language == *language; });
            if (iterator == config.source_sets.end()) {
                project::SourceSet source_set;
                source_set.language = *language;
                const auto requested_standard = invocation.standard.value_or(
                    std::string { project::default_standard(*language) });
                const auto canonical_standard = project::canonical_standard(
                    *language, requested_standard);
                if (!canonical_standard) {
                    argument_error(
                        diagnostics,
                        "unsupported standard '" + requested_standard + "' for "
                            + std::string(project::to_string(*language)));
                    continue;
                }
                source_set.standard = *canonical_standard;
                source_set.compatibility_switches = invocation.compatibility_switches;
                source_set.library = invocation.library;
                source_set.compilation_unit = invocation.compilation_unit.value_or(
                    invocation.command == Command::compile
                            && *language != project::Language::vhdl
                        ? "source-set"
                        : "file");
                source_set.uvm_release = invocation.uvm_release.value_or(
                    project::SystemVerilogUvmRelease::none);
                for (const auto& directory : invocation.include_directories) {
                    source_set.include_directories.push_back(absolute_normalized(directory));
                }
                source_set.defines = invocation.defines;
                config.source_sets.push_back(std::move(source_set));
                iterator = std::prev(config.source_sets.end());
            }
            iterator->file_patterns.push_back(path);
            iterator->files.push_back(path);
        }

        if (invocation.duration.has_value()) {
            config.run.duration = invocation.duration;
        }
        if (invocation.max_deltas.has_value()) {
            config.run.max_deltas = *invocation.max_deltas;
        }
        if (invocation.delay_mode.has_value()) {
            config.run.delay_mode = *invocation.delay_mode;
        }
        if (invocation.trace_file.has_value()) {
            config.run.trace_file = absolute_normalized(*invocation.trace_file);
        }
        if (invocation.trace_format.has_value()) {
            config.run.trace_format = *invocation.trace_format;
        }
        if (invocation.trace_compression.has_value()) {
            config.run.trace_compression = *invocation.trace_compression;
        }
        config.run.trace_filters = invocation.trace_filters;
        if (invocation.trace_report_limit.has_value()) {
            config.run.trace_report_limit = *invocation.trace_report_limit;
        }
        if (invocation.trace_enabled.has_value()) {
            config.run.trace_enabled = *invocation.trace_enabled;
        }
        if (invocation.seed.has_value()) {
            config.project.seed = *invocation.seed;
        }
        config.project.random_seed = invocation.random_seed;
        if (invocation.jobs.has_value()) {
            config.build.jobs = *invocation.jobs;
        }
        if (invocation.optimization.has_value()) {
            config.build.optimization = *invocation.optimization;
        }
        if (!invocation.search_libraries.empty()) {
            config.elaboration.search_libraries = invocation.search_libraries;
        }
        if (!invocation.library_mappings.empty()) {
            config.library_mappings = invocation.library_mappings;
            for (auto& mapping : config.library_mappings) {
                mapping.path = absolute_normalized(mapping.path);
            }
        }
        if (invocation.uvm_release.has_value()) {
            for (auto& source_set : config.source_sets) {
                if (source_set.language == project::Language::system_verilog) {
                    source_set.uvm_release = *invocation.uvm_release;
                }
            }
        }

        if (diagnostics.has_error()) {
            return std::nullopt;
        }
        return config;
    }

    void apply_overrides(const Invocation& invocation, project::Config& config)
    {
        if (!invocation.tops.empty()) {
            config.project.tops = invocation.tops;
            config.project.top = invocation.tops.size() == 1
                ? invocation.tops.front().target
                : std::string { };
        } else if (invocation.top.has_value()) {
            config.project.top = *invocation.top;
        }
        if (invocation.duration.has_value()) {
            config.run.duration = invocation.duration;
        }
        if (invocation.max_deltas.has_value()) {
            config.run.max_deltas = *invocation.max_deltas;
        }
        if (invocation.delay_mode.has_value()) {
            config.run.delay_mode = *invocation.delay_mode;
        }
        if (invocation.trace_file.has_value()) {
            config.run.trace_file = absolute_normalized(*invocation.trace_file);
        }
        if (invocation.trace_format.has_value()) {
            config.run.trace_format = *invocation.trace_format;
        }
        if (invocation.trace_compression.has_value()) {
            config.run.trace_compression = *invocation.trace_compression;
        }
        if (!invocation.trace_filters.empty()) {
            config.run.trace_filters = invocation.trace_filters;
        }
        if (invocation.trace_report_limit.has_value()) {
            config.run.trace_report_limit = *invocation.trace_report_limit;
        }
        if (invocation.trace_enabled.has_value()) {
            config.run.trace_enabled = *invocation.trace_enabled;
        }
        if (invocation.seed.has_value()) {
            config.project.seed = *invocation.seed;
            config.project.random_seed = false;
        } else if (invocation.random_seed) {
            config.project.random_seed = true;
        }
        if (invocation.jobs.has_value()) {
            config.build.jobs = *invocation.jobs;
        }
        if (invocation.optimization.has_value()) {
            config.build.optimization = *invocation.optimization;
        }
        if (!invocation.search_libraries.empty()) {
            config.elaboration.search_libraries = invocation.search_libraries;
        }
        if (!invocation.library_mappings.empty()) {
            config.library_mappings = invocation.library_mappings;
            for (auto& mapping : config.library_mappings) {
                mapping.path = absolute_normalized(mapping.path);
            }
        }
        if (invocation.systemc_compiler.has_value()) {
            config.systemc.compiler = *invocation.systemc_compiler;
        }
        config.systemc.include_directories = invocation.include_directories;
        config.systemc.defines = invocation.defines;
        config.systemc.compile_options = invocation.systemc_compile_options;
        config.systemc.link_options = invocation.systemc_link_options;
        config.systemc.libraries = invocation.systemc_libraries;
    }

    void print_help(std::ostream& output, const std::string_view program)
    {
        output
            << "Usage: " << program
            << " <check|build|run|debug|tcl|compile|elaborate|simulate>"
               " [options] [files...]\n"
            << "\n"
            << "Commands:\n"
            << "  check   Parse and analyze sources\n"
            << "  build   Elaborate and populate the native-code cache\n"
            << "  run     Build incrementally and simulate in optimized mode\n"
            << "  debug   Build incrementally and enter the interactive debugger\n"
            << "  tcl     Enter Tcl or evaluate a Tcl script/command batch\n"
            << "  compile Compile explicit HDL sources into a .fsimobj artifact\n"
            << "  elaborate Elaborate explicit .fsimobj inputs into .fsimdesign\n"
            << "  simulate Simulate an explicit .fsimdesign artifact\n"
            << "  systemc compile Compile one SystemC C++ translation unit into .fsimscobj\n"
            << "  systemc link Link .fsimscobj inputs into one .fsimscplugin\n"
            << "\n"
            << "Project and source options:\n"
            << "  -p, --project PATH       Project manifest (default: fsim.toml)\n"
            << "      --top [ALIAS=]NAME   Replace design tops; repeatable, aliases required for multiple\n"
            << "      --lang LANGUAGE      Language for every direct source file\n"
            << "      --standard VERSION   Standard for direct source files\n"
            << "                           VHDL: 87/1987, 93/1993, 00/2000, 02/2002, 08/2008\n"
            << "                           Verilog: 95/1995, 01/2001, 2001-noconfig, 05/2005\n"
            << "                           SystemVerilog: 05/2005, 09/2009, 12/2012, 17/2017\n"
            << "      --compatibility NAME Explicit compatibility switch; repeatable\n"
            << "                           keyword-profile, implicit-net, port-connection, sizing,\n"
            << "                           lifetime, scheduler-assertion, configuration\n"
            << "      --uvm-release VERSION\n"
            << "                           Governed SystemVerilog UVM release: 1.2 or 2020.3.1\n"
            << "      --compilation-unit file|source-set\n"
            << "                           Compile files separately or as one unit\n"
            << "      --library NAME       Library for direct source files (default: work)\n"
            << "      --search-library NAME\n"
            << "                           Replace the manifest elaboration search list; repeatable\n"
            << "      --map-library NAME=DIRECTORY\n"
            << "                           Replace manifest precompiled-library mappings; repeatable\n"
            << "      --export-library NAME=DIRECTORY\n"
            << "                           Publish a project library during build; repeatable\n"
            << "  -I, --include PATH       Add a direct-source include directory\n"
            << "  -D, --define NAME[=VAL]  Add a direct-source preprocessor definition\n"
            << "      --output PATH        Output for compile or elaborate\n"
            << "      --object PATH        Input object for elaborate; repeatable\n"
            << "      --systemc-plugin PATH\n"
            << "                           Linked SystemC input for elaborate; repeatable\n"
            << "      --compiler PATH      SystemC C++ compiler executable\n"
            << "      --compile-option ARG SystemC compiler option; repeatable\n"
            << "      --link-option ARG    SystemC linker option; repeatable\n"
            << "      --link-library ARG   SystemC link library/path; repeatable\n"
            << "      --design PATH        Input design for simulate\n"
            << "\n"
            << "Build and run options:\n"
            << "  -O, --optimization O0..O3\n"
            << "  -j, --jobs COUNT\n"
            << "      --duration TIME\n"
            << "      --max-deltas COUNT\n"
            << "      --delay-mode min|typ|max\n"
            << "      --sdf PATH           Add an SDF annotation input; repeatable\n"
            << "      --sdf-root NAME      Select the annotation root\n"
            << "      --sdf-cell GLOB      Select cells within the root (default: *)\n"
            << "      --sdf-report-limit COUNT\n"
            << "                           Bound detailed SDF report entries\n"
            << "      --trace PATH          Trace output path (--trace-output alias)\n"
            << "      --trace-format auto|vcd|fst\n"
            << "      --trace-compression auto|none|deterministic\n"
            << "      --trace-filter GLOB  Trace selection; repeatable (--trace-select alias)\n"
            << "      --trace-lifecycle configured|disabled\n"
            << "      --trace-report-limit COUNT\n"
            << "                           Bound detailed trace report entries\n"
            << "      --cache PATH         Native cache for standalone simulate\n"
            << "      --file-root PATH     File-I/O root for standalone simulate\n"
            << "      --engine interpreter|compiled|debug\n"
            << "      --seed COUNT|random\n"
            << "      --diagnostics text|json\n"
            << "\n"
            << "Tcl options:\n"
            << "  -c, --command SCRIPT    Evaluate Tcl text (repeatable)\n"
            << "  SCRIPT [ARG...]         Evaluate a Tcl file with argv/argc set\n"
            << "                          Omit both forms for interactive Tcl\n"
            << "  -h, --help\n"
            << "      --version\n";
    }

    void print_diagnostics(
        std::ostream& error,
        const diagnostic::Engine& diagnostics,
        const DiagnosticFormat format)
    {
        if (format == DiagnosticFormat::json) {
            diagnostic::print_json(error, diagnostics);
        } else {
            diagnostic::print_text(error, diagnostics);
        }
    }

    const Handler* select_handler(const Services& services, const Command command)
    {
        switch (command) {
        case Command::check:
            return &services.check;
        case Command::build:
            return &services.build;
        case Command::run:
            return &services.run;
        case Command::debug:
            return &services.debug;
        case Command::tcl:
            return &services.tcl;
        case Command::compile:
            return &services.compile;
        case Command::elaborate:
            return &services.elaborate;
        case Command::simulate:
            return &services.simulate;
        case Command::systemc_compile:
            return &services.systemc_compile;
        case Command::systemc_link:
            return &services.systemc_link;
        }
        return nullptr;
    }

    DiagnosticFormat requested_diagnostic_format(
        const int argc,
        const char* const* argv) noexcept
    {
        DiagnosticFormat result = DiagnosticFormat::text;
        if (argc <= 1 || argv == nullptr) {
            return result;
        }
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) {
                continue;
            }
            const std::string_view argument { argv[index] };
            std::string_view value;
            if (argument.starts_with("--diagnostics=")) {
                value = argument.substr(std::string_view { "--diagnostics=" }.size());
            } else if (
                argument == "--diagnostics" && index + 1 < argc
                && argv[index + 1] != nullptr) {
                value = argv[++index];
            } else {
                continue;
            }
            if (value == "json") {
                result = DiagnosticFormat::json;
            } else if (value == "text") {
                result = DiagnosticFormat::text;
            }
        }
        return result;
    }

} // namespace

std::optional<Invocation> parse_arguments(
    const int argc,
    const char* const* argv,
    diagnostic::Engine& diagnostics)
{
    if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
        argument_error(diagnostics, "missing program name");
        return std::nullopt;
    }

    Invocation invocation;
    invocation.program_path = fsim::support::path_from_utf8(argv[0]);
    invocation.program_name = basename(argv[0]);
    bool command_selected = false;
    const auto executable = lowercase(invocation.program_name);
    if (executable == "fsim-vhdl" || executable == "fsim-vhdl.exe") {
        invocation.command = Command::check;
        invocation.language = project::Language::vhdl;
        command_selected = true;
    } else if (executable == "fsim-sv" || executable == "fsim-sv.exe") {
        invocation.command = Command::check;
        invocation.language = project::Language::system_verilog;
        command_selected = true;
    } else if (executable == "fsim-elab" || executable == "fsim-elab.exe") {
        invocation.command = Command::build;
        command_selected = true;
    } else if (executable == "fsim-run" || executable == "fsim-run.exe") {
        invocation.command = Command::run;
        command_selected = true;
    }

    bool positional_only = false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            argument_error(diagnostics, "null command-line argument");
            return std::nullopt;
        }
        const std::string_view argument(argv[index]);
        if (!positional_only && argument == "--") {
            positional_only = true;
            continue;
        }
        if (!positional_only && (argument == "-h" || argument == "--help")) {
            invocation.help = true;
            continue;
        }
        if (!positional_only && argument == "--version") {
            invocation.version = true;
            continue;
        }
        if (!positional_only && argument.starts_with('-')) {
            if (is_option(argument, "-p", "--project")) {
                const auto value = take_value(index, argc, argv, argument, "--project", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.manifest = fsim::support::path_from_utf8(*value);
                invocation.manifest_explicit = true;
            } else if (is_option(argument, "", "--top")) {
                const auto value = take_value(index, argc, argv, argument, "--top", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.tops.push_back(parse_top_option(*value));
            } else if (is_option(argument, "", "--lang")) {
                const auto value = take_value(index, argc, argv, argument, "--lang", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.language = project::parse_language(*value);
                if (!invocation.language.has_value()) {
                    argument_error(diagnostics, "unknown language '" + std::string(*value) + "'");
                    return std::nullopt;
                }
            } else if (is_option(argument, "", "--standard")) {
                const auto value = take_value(index, argc, argv, argument, "--standard", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.standard = std::string(*value);
            } else if (is_option(argument, "", "--compatibility")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--compatibility", diagnostics);
                const auto canonical = value.has_value()
                    ? project::parse_compatibility_switch(*value)
                    : std::nullopt;
                if (!canonical) {
                    argument_error(
                        diagnostics,
                        "--compatibility requires a supported switch name");
                    return std::nullopt;
                }
                if (std::ranges::find(
                        invocation.compatibility_switches, *canonical)
                    != invocation.compatibility_switches.end()) {
                    argument_error(
                        diagnostics,
                        "duplicate --compatibility switch '"
                            + std::string { *canonical } + "'");
                    return std::nullopt;
                }
                invocation.compatibility_switches.emplace_back(*canonical);
            } else if (is_option(argument, "", "--uvm-release")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--uvm-release", diagnostics);
                invocation.uvm_release = value.has_value()
                    ? project::parse_systemverilog_uvm_release(*value)
                    : std::nullopt;
                if (!invocation.uvm_release
                    || *invocation.uvm_release
                        == project::SystemVerilogUvmRelease::none) {
                    argument_error(
                        diagnostics, "--uvm-release must be 1.2 or 2020.3.1");
                    return std::nullopt;
                }
            } else if (is_option(argument, "", "--compilation-unit")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--compilation-unit", diagnostics);
                if (!value.has_value()
                    || (*value != "file" && *value != "source-set")) {
                    argument_error(
                        diagnostics,
                        "--compilation-unit must be file or source-set");
                    return std::nullopt;
                }
                invocation.compilation_unit = std::string { *value };
            } else if (is_option(argument, "", "--library")) {
                const auto value = take_value(index, argc, argv, argument, "--library", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.library = std::string(*value);
            } else if (is_option(argument, "", "--search-library")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--search-library", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (value->empty()) {
                    argument_error(
                        diagnostics,
                        "--search-library requires a non-empty library name");
                    return std::nullopt;
                }
                invocation.search_libraries.emplace_back(*value);
            } else if (is_option(argument, "", "--map-library")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--map-library", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                const auto mapping = parse_library_mapping(*value);
                if (!mapping.has_value()) {
                    argument_error(
                        diagnostics,
                        "--map-library requires a LIBRARY=DIRECTORY value");
                    return std::nullopt;
                }
                invocation.library_mappings.push_back(*mapping);
            } else if (is_option(argument, "", "--export-library")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--export-library", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                const auto mapping = parse_library_mapping(*value);
                if (!mapping.has_value()) {
                    argument_error(
                        diagnostics,
                        "--export-library requires a LIBRARY=DIRECTORY value");
                    return std::nullopt;
                }
                invocation.library_exports.push_back(*mapping);
            } else if (is_option(argument, "-o", "--output")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--output", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--output requires a non-empty path");
                    return std::nullopt;
                }
                invocation.artifact_output = fsim::support::path_from_utf8(*value);
            } else if (is_option(argument, "", "--object")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--object", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--object requires a non-empty path");
                    return std::nullopt;
                }
                invocation.objects.emplace_back(
                    fsim::support::path_from_utf8(*value));
            } else if (is_option(argument, "", "--systemc-plugin")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--systemc-plugin", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(
                        diagnostics, "--systemc-plugin requires a non-empty path");
                    return std::nullopt;
                }
                invocation.systemc_plugins.emplace_back(
                    fsim::support::path_from_utf8(*value));
            } else if (is_option(argument, "", "--compiler")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--compiler", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--compiler requires a non-empty value");
                    return std::nullopt;
                }
                invocation.systemc_compiler = std::string { *value };
            } else if (is_option(argument, "", "--compile-option")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--compile-option", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(
                        diagnostics, "--compile-option requires a non-empty value");
                    return std::nullopt;
                }
                invocation.systemc_compile_options.emplace_back(*value);
            } else if (is_option(argument, "", "--link-option")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--link-option", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(
                        diagnostics, "--link-option requires a non-empty value");
                    return std::nullopt;
                }
                invocation.systemc_link_options.emplace_back(*value);
            } else if (is_option(argument, "", "--link-library")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--link-library", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(
                        diagnostics, "--link-library requires a non-empty value");
                    return std::nullopt;
                }
                invocation.systemc_libraries.emplace_back(*value);
            } else if (is_option(argument, "", "--design")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--design", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--design requires a non-empty path");
                    return std::nullopt;
                }
                invocation.design = fsim::support::path_from_utf8(*value);
            } else if (is_option(argument, "", "--cache")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--cache", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--cache requires a non-empty path");
                    return std::nullopt;
                }
                invocation.cache_directory = fsim::support::path_from_utf8(*value);
            } else if (is_option(argument, "", "--file-root")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--file-root", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--file-root requires a non-empty path");
                    return std::nullopt;
                }
                invocation.file_root = fsim::support::path_from_utf8(*value);
            } else if (
                argument == "-I" || is_option(argument, "", "--include") || (argument.size() > 2 && argument.starts_with("-I"))) {
                std::optional<std::string_view> value;
                if (argument.size() > 2 && argument.starts_with("-I")) {
                    value = argument.substr(2);
                } else {
                    value = take_value(index, argc, argv, argument, "--include", diagnostics);
                }
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.include_directories.emplace_back(
                    fsim::support::path_from_utf8(*value));
            } else if (
                argument == "-D" || is_option(argument, "", "--define") || (argument.size() > 2 && argument.starts_with("-D"))) {
                std::optional<std::string_view> value;
                if (argument.size() > 2 && argument.starts_with("-D")) {
                    value = argument.substr(2);
                } else {
                    value = take_value(index, argc, argv, argument, "--define", diagnostics);
                }
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.defines.emplace_back(*value);
            } else if (
                argument == "-O" || is_option(argument, "", "--optimization") || (argument.size() == 3 && argument.starts_with("-O"))) {
                std::optional<std::string_view> value;
                if (argument.size() == 3 && argument.starts_with("-O")) {
                    value = argument.substr(1);
                } else {
                    value = take_value(index, argc, argv, argument, "--optimization", diagnostics);
                }
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.optimization = parse_optimization(*value);
                if (!invocation.optimization.has_value()) {
                    argument_error(diagnostics, "optimization must be O0, O1, O2, or O3");
                    return std::nullopt;
                }
            } else if (
                argument == "-j" || is_option(argument, "", "--jobs") || (argument.size() > 2 && argument.starts_with("-j"))) {
                std::optional<std::string_view> value;
                if (argument.size() > 2 && argument.starts_with("-j")) {
                    value = argument.substr(2);
                } else {
                    value = take_value(index, argc, argv, argument, "--jobs", diagnostics);
                }
                std::uint64_t number = 0;
                if (!value.has_value() || !parse_unsigned(*value, number) || number > std::numeric_limits<std::uint32_t>::max()) {
                    argument_error(diagnostics, "jobs must be a non-negative 32-bit integer");
                    return std::nullopt;
                }
                invocation.jobs = static_cast<std::uint32_t>(number);
            } else if (is_option(argument, "", "--duration")) {
                const auto value = take_value(index, argc, argv, argument, "--duration", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.duration = std::string(*value);
            } else if (is_option(argument, "", "--max-deltas")) {
                const auto value = take_value(index, argc, argv, argument, "--max-deltas", diagnostics);
                std::uint64_t number = 0;
                if (!value.has_value() || !parse_unsigned(*value, number) || number == 0) {
                    argument_error(diagnostics, "max-deltas must be a positive integer");
                    return std::nullopt;
                }
                invocation.max_deltas = number;
            } else if (is_option(argument, "", "--delay-mode")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--delay-mode", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.delay_mode = project::parse_delay_mode(*value);
                if (!invocation.delay_mode) {
                    argument_error(
                        diagnostics, "delay-mode must be min, typ, or max");
                    return std::nullopt;
                }
            } else if (is_option(argument, "", "--sdf")) {
                const auto value = take_value(index, argc, argv, argument, "--sdf", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--sdf requires a non-empty path");
                    return std::nullopt;
                }
                invocation.sdf_files.push_back(fsim::support::path_from_utf8(*value));
            } else if (is_option(argument, "", "--sdf-root")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--sdf-root", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--sdf-root requires a non-empty name");
                    return std::nullopt;
                }
                invocation.sdf_root = std::string(*value);
            } else if (is_option(argument, "", "--sdf-cell")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--sdf-cell", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--sdf-cell requires a non-empty glob");
                    return std::nullopt;
                }
                invocation.sdf_cell = std::string(*value);
            } else if (is_option(argument, "", "--sdf-report-limit")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--sdf-report-limit", diagnostics);
                std::uint64_t number = 0;
                if (!value.has_value() || !parse_unsigned(*value, number)
                    || number == 0
                    || number > std::numeric_limits<std::size_t>::max()) {
                    argument_error(
                        diagnostics, "--sdf-report-limit must be a positive integer");
                    return std::nullopt;
                }
                invocation.sdf_report_limit = static_cast<std::size_t>(number);
            } else if (is_option(argument, "", "--trace")
                || is_option(argument, "", "--trace-output")) {
                const auto name = argument.starts_with("--trace-output")
                    ? "--trace-output"
                    : "--trace";
                const auto value = take_value(
                    index, argc, argv, argument, name, diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.trace_file = fsim::support::path_from_utf8(*value);
            } else if (is_option(argument, "", "--trace-format")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--trace-format", diagnostics);
                invocation.trace_format = value.has_value()
                    ? project::parse_trace_format(*value)
                    : std::nullopt;
                if (!invocation.trace_format) {
                    argument_error(
                        diagnostics, "--trace-format must be auto, vcd, or fst");
                    return std::nullopt;
                }
            } else if (is_option(argument, "", "--trace-compression")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--trace-compression", diagnostics);
                invocation.trace_compression = value.has_value()
                    ? project::parse_trace_compression(*value)
                    : std::nullopt;
                if (!invocation.trace_compression) {
                    argument_error(diagnostics,
                        "--trace-compression must be auto, none, or deterministic");
                    return std::nullopt;
                }
            } else if (is_option(argument, "", "--trace-filter")
                || is_option(argument, "", "--trace-select")) {
                const auto name = argument.starts_with("--trace-select")
                    ? "--trace-select"
                    : "--trace-filter";
                const auto value = take_value(
                    index, argc, argv, argument, name, diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(
                        diagnostics, std::string { name }
                            + " requires a non-empty glob");
                    return std::nullopt;
                }
                invocation.trace_filters.emplace_back(*value);
            } else if (is_option(argument, "", "--trace-lifecycle")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--trace-lifecycle", diagnostics);
                if (value.has_value()
                    && (*value == "configured" || *value == "enabled")) {
                    invocation.trace_enabled = true;
                } else if (value.has_value()
                    && (*value == "disabled" || *value == "off")) {
                    invocation.trace_enabled = false;
                } else {
                    argument_error(diagnostics,
                        "--trace-lifecycle must be configured or disabled");
                    return std::nullopt;
                }
            } else if (is_option(argument, "", "--trace-report-limit")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--trace-report-limit", diagnostics);
                std::uint64_t number = 0U;
                if (!value.has_value() || !parse_unsigned(*value, number)
                    || number == 0U
                    || number > std::numeric_limits<std::size_t>::max()) {
                    argument_error(diagnostics,
                        "--trace-report-limit must be a nonzero size");
                    return std::nullopt;
                }
                invocation.trace_report_limit
                    = static_cast<std::size_t>(number);
            } else if (is_option(argument, "", "--engine")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--engine", diagnostics);
                if (!value.has_value()
                    || (*value != "interpreter" && *value != "compiled"
                        && *value != "debug")) {
                    argument_error(
                        diagnostics,
                        "--engine must be interpreter, compiled, or debug");
                    return std::nullopt;
                }
                invocation.engine = std::string { *value };
            } else if (is_option(argument, "", "--seed")) {
                const auto value = take_value(index, argc, argv, argument, "--seed", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (lowercase(*value) == "random") {
                    invocation.random_seed = true;
                    invocation.seed.reset();
                } else {
                    std::uint64_t number = 0;
                    if (!parse_unsigned(*value, number)) {
                        argument_error(diagnostics, "seed must be an unsigned integer or 'random'");
                        return std::nullopt;
                    }
                    invocation.seed = number;
                    invocation.random_seed = false;
                }
            } else if (is_option(argument, "", "--diagnostics")) {
                const auto value = take_value(index, argc, argv, argument, "--diagnostics", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (*value == "text") {
                    invocation.diagnostic_format = DiagnosticFormat::text;
                } else if (*value == "json") {
                    invocation.diagnostic_format = DiagnosticFormat::json;
                } else {
                    argument_error(diagnostics, "diagnostics must be 'text' or 'json'");
                    return std::nullopt;
                }
            } else if (is_option(argument, "-c", "--command")) {
                const auto value = take_value(index, argc, argv, argument, "--command", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.tcl_commands.emplace_back(*value);
            } else {
                argument_error(diagnostics, "unknown option '" + std::string(argument) + "'");
                return std::nullopt;
            }
            continue;
        }

        if (!positional_only && argument.starts_with('+')) {
            invocation.plusargs.emplace_back(argument);
            continue;
        }

        if (!command_selected) {
            if (argument == "systemc") {
                if (index + 1 >= argc || argv[index + 1] == nullptr) {
                    argument_error(
                        diagnostics, "systemc requires a compile or link subcommand");
                    return std::nullopt;
                }
                const std::string_view subcommand { argv[++index] };
                if (subcommand == "compile") {
                    invocation.command = Command::systemc_compile;
                } else if (subcommand == "link") {
                    invocation.command = Command::systemc_link;
                } else {
                    argument_error(
                        diagnostics,
                        "unknown systemc subcommand '" + std::string { subcommand }
                            + "'; expected compile or link");
                    return std::nullopt;
                }
                command_selected = true;
                continue;
            }
            const auto command = parse_command(argument);
            if (!command.has_value()) {
                argument_error(
                    diagnostics,
                    "unknown command '" + std::string(argument) + "'; expected check, build, run, debug, tcl, compile, "
                                                                  "elaborate, or simulate");
                return std::nullopt;
            }
            invocation.command = *command;
            command_selected = true;
        } else if (invocation.command == Command::tcl) {
            if (!invocation.tcl_script.has_value()) {
                invocation.tcl_script = fsim::support::path_from_utf8(argument);
            } else {
                invocation.tcl_arguments.emplace_back(argument);
            }
        } else {
            invocation.files.emplace_back(fsim::support::path_from_utf8(argument));
        }
    }

    if (!command_selected && !invocation.help && !invocation.version) {
        argument_error(
            diagnostics,
            "missing command; expected check, build, run, debug, tcl, compile, "
            "elaborate, or simulate");
        return std::nullopt;
    }
    if (!invocation.tops.empty()) {
        std::vector<std::string> aliases;
        aliases.reserve(invocation.tops.size());
        for (auto& top : invocation.tops) {
            if (top.target.empty()) {
                argument_error(diagnostics, "--top requires a non-empty target");
                return std::nullopt;
            }
            if (top.alias.empty()) {
                if (invocation.tops.size() != 1) {
                    argument_error(
                        diagnostics,
                        "every repeated --top requires an ALIAS=NAME spelling");
                    return std::nullopt;
                }
            } else if (!valid_top_alias(top.alias)) {
                argument_error(
                    diagnostics,
                    "top alias '" + top.alias
                        + "' must start with a letter or underscore and contain "
                          "only letters, digits, and underscores");
                return std::nullopt;
            } else if (std::ranges::find(aliases, top.alias) != aliases.end()) {
                argument_error(diagnostics, "duplicate top alias '" + top.alias + "'");
                return std::nullopt;
            }
            if (!top.alias.empty()) {
                aliases.push_back(top.alias);
            }
        }
        invocation.top = invocation.tops.size() == 1
            ? std::optional<std::string> { invocation.tops.front().target }
            : std::nullopt;
    }
    std::vector<std::string> exported_libraries;
    for (auto& library_export : invocation.library_exports) {
        const auto normalized = lowercase(library_export.library);
        if (!valid_library_name(library_export.library)
            || normalized == "std" || normalized == "ieee"
            || normalized == "fsim") {
            argument_error(
                diagnostics,
                "--export-library requires a safe, non-reserved logical library");
            return std::nullopt;
        }
        if (std::ranges::find(exported_libraries, normalized)
            != exported_libraries.end()) {
            argument_error(
                diagnostics,
                "duplicate --export-library logical library '"
                    + library_export.library + "'");
            return std::nullopt;
        }
        exported_libraries.push_back(normalized);
        library_export.path = absolute_normalized(library_export.path);
    }
    if (invocation.command != Command::tcl
        && !invocation.tcl_commands.empty()) {
        argument_error(
            diagnostics, "--command is available only with the tcl command");
        return std::nullopt;
    }
    if (!invocation.plusargs.empty()
        && invocation.command != Command::run
        && invocation.command != Command::debug
        && invocation.command != Command::simulate) {
        argument_error(
            diagnostics,
            "plusargs are available only with run, debug, or simulate");
        return std::nullopt;
    }
    if (!invocation.library_exports.empty()
        && invocation.command != Command::build) {
        argument_error(
            diagnostics, "--export-library is available only with build");
        return std::nullopt;
    }
    if (invocation.command == Command::tcl
        && invocation.tcl_script.has_value()
        && !invocation.tcl_commands.empty()) {
        argument_error(
            diagnostics,
            "a Tcl script file cannot be combined with --command");
        return std::nullopt;
    }
    if (invocation.manifest_explicit && !invocation.files.empty()) {
        argument_error(diagnostics, "--project cannot be combined with direct source files");
        return std::nullopt;
    }
    const bool non_project_command = invocation.command == Command::compile
        || invocation.command == Command::elaborate
        || invocation.command == Command::simulate
        || invocation.command == Command::systemc_compile
        || invocation.command == Command::systemc_link;
    if (non_project_command && invocation.manifest_explicit) {
        argument_error(
            diagnostics,
            "--project is not available with manifest-free artifact commands");
        return std::nullopt;
    }
    if (invocation.artifact_output.has_value()) {
        invocation.artifact_output = absolute_normalized(*invocation.artifact_output);
    }
    for (auto& object : invocation.objects) {
        object = absolute_normalized(object);
    }
    for (auto& plugin : invocation.systemc_plugins) {
        plugin = absolute_normalized(plugin);
    }
    if (invocation.design.has_value()) {
        invocation.design = absolute_normalized(*invocation.design);
    }
    for (auto& sdf_file : invocation.sdf_files) {
        sdf_file = absolute_normalized(sdf_file);
    }
    if (invocation.cache_directory.has_value()) {
        invocation.cache_directory = absolute_normalized(*invocation.cache_directory);
    }
    if (invocation.file_root.has_value()) {
        invocation.file_root = absolute_normalized(*invocation.file_root);
    }
    const bool has_sdf_option = !invocation.sdf_files.empty()
        || invocation.sdf_root.has_value() || invocation.sdf_cell != "*"
        || invocation.sdf_report_limit.has_value();
    if (has_sdf_option && invocation.sdf_files.empty()) {
        argument_error(
            diagnostics,
            "--sdf-root, --sdf-cell, and --sdf-report-limit require --sdf");
        return std::nullopt;
    }
    const bool sdf_phase = invocation.command == Command::build
        || invocation.command == Command::run
        || invocation.command == Command::debug
        || invocation.command == Command::tcl
        || invocation.command == Command::elaborate
        || invocation.command == Command::simulate;
    if (has_sdf_option && !sdf_phase) {
        argument_error(
            diagnostics,
            "SDF annotation is available only during elaborate or simulate phases");
        return std::nullopt;
    }
    const bool has_trace_option = invocation.trace_file.has_value()
        || invocation.trace_format.has_value()
        || invocation.trace_compression.has_value()
        || !invocation.trace_filters.empty()
        || invocation.trace_report_limit.has_value()
        || invocation.trace_enabled.has_value();
    const bool trace_phase = invocation.command == Command::build
        || invocation.command == Command::run
        || invocation.command == Command::debug
        || invocation.command == Command::tcl
        || invocation.command == Command::compile
        || invocation.command == Command::elaborate
        || invocation.command == Command::simulate;
    if (has_trace_option && !trace_phase) {
        argument_error(diagnostics,
            "trace control is available only during compile, elaborate, or simulate phases");
        return std::nullopt;
    }
    for (std::size_t index = 0; index < invocation.trace_filters.size(); ++index) {
        if (std::ranges::find(invocation.trace_filters.begin(),
                invocation.trace_filters.begin()
                    + static_cast<std::ptrdiff_t>(index),
                invocation.trace_filters[index])
            != invocation.trace_filters.begin()
                + static_cast<std::ptrdiff_t>(index)) {
            argument_error(diagnostics,
                "trace selections must be unique within one invocation");
            return std::nullopt;
        }
    }
    if (!invocation.help && !invocation.version
        && invocation.command == Command::compile) {
        if (invocation.files.empty() || !invocation.language.has_value()
            || !invocation.standard.has_value()
            || !invocation.artifact_output.has_value()) {
            argument_error(
                diagnostics,
                "compile requires --lang, --standard, --output, and source files");
            return std::nullopt;
        }
        if (*invocation.language == project::Language::systemc) {
            argument_error(
                diagnostics,
                "SystemC source compilation is provided by the Batch 138 "
                "incremental SystemC compile/link commands");
            return std::nullopt;
        }
        if (!invocation.compatibility_switches.empty()
            && *invocation.language != project::Language::verilog
            && *invocation.language != project::Language::system_verilog) {
            argument_error(
                diagnostics,
                "--compatibility is available only for Verilog/SystemVerilog");
            return std::nullopt;
        }
        if (!valid_library_name(invocation.library)) {
            argument_error(
                diagnostics, "compile requires a safe logical-library name");
            return std::nullopt;
        }
        if (!invocation.tops.empty() || !invocation.search_libraries.empty()
            || !invocation.library_mappings.empty()
            || !invocation.library_exports.empty() || !invocation.objects.empty()
            || invocation.design.has_value() || invocation.duration.has_value()
            || invocation.max_deltas.has_value()
            || invocation.delay_mode.has_value()
            || invocation.seed.has_value() || invocation.random_seed
            || invocation.optimization.has_value() || invocation.engine.has_value()) {
            argument_error(
                diagnostics,
                "compile received an elaboration, simulation, mapping, or native "
                "build option");
            return std::nullopt;
        }
    }
    if (!invocation.help && !invocation.version
        && invocation.command == Command::systemc_compile) {
        if (invocation.files.size() != 1
            || !invocation.artifact_output.has_value()) {
            argument_error(
                diagnostics,
                "systemc compile requires exactly one source and --output");
            return std::nullopt;
        }
        if (!invocation.objects.empty() || !invocation.systemc_plugins.empty()
            || invocation.language.has_value() || invocation.standard.has_value()
            || !invocation.compatibility_switches.empty()
            || !invocation.tops.empty() || invocation.design.has_value()
            || !invocation.systemc_link_options.empty()
            || !invocation.systemc_libraries.empty()) {
            argument_error(
                diagnostics,
                "systemc compile received a link, HDL, elaboration, or simulation option");
            return std::nullopt;
        }
    }
    if (!invocation.help && !invocation.version
        && invocation.command == Command::systemc_link) {
        if (invocation.objects.empty() || !invocation.artifact_output.has_value()) {
            argument_error(
                diagnostics, "systemc link requires --object and --output");
            return std::nullopt;
        }
        if (!valid_library_name(invocation.library)) {
            argument_error(
                diagnostics, "systemc link requires a safe logical-library name");
            return std::nullopt;
        }
        if (!invocation.files.empty() || !invocation.systemc_plugins.empty()
            || invocation.language.has_value() || invocation.standard.has_value()
            || !invocation.compatibility_switches.empty()
            || !invocation.include_directories.empty() || !invocation.defines.empty()
            || !invocation.systemc_compile_options.empty()
            || !invocation.tops.empty() || invocation.design.has_value()) {
            argument_error(
                diagnostics,
                "systemc link received a compile, HDL, elaboration, or simulation option");
            return std::nullopt;
        }
    }
    if (!invocation.help && !invocation.version
        && invocation.command == Command::elaborate) {
        if ((invocation.objects.empty() && invocation.systemc_plugins.empty())
            || invocation.tops.empty()
            || !invocation.artifact_output.has_value()) {
            argument_error(
                diagnostics,
                "elaborate requires --object or --systemc-plugin, plus --top and --output");
            return std::nullopt;
        }
        if (!invocation.files.empty() || invocation.language.has_value()
            || invocation.standard.has_value()
            || !invocation.compatibility_switches.empty()
            || !invocation.include_directories.empty()
            || !invocation.defines.empty() || invocation.design.has_value()
            || !invocation.library_mappings.empty()
            || !invocation.library_exports.empty() || invocation.duration.has_value()
            || invocation.max_deltas.has_value()
            || invocation.engine.has_value()) {
            argument_error(
                diagnostics,
                "elaborate received a source, project-mapping, or simulation option");
            return std::nullopt;
        }
    }
    if (!invocation.help && !invocation.version
        && invocation.command == Command::simulate) {
        if (!invocation.design.has_value()) {
            argument_error(diagnostics, "simulate requires --design");
            return std::nullopt;
        }
        if (!invocation.files.empty() || invocation.artifact_output.has_value()
            || !invocation.objects.empty() || !invocation.systemc_plugins.empty()
            || !invocation.tops.empty()
            || invocation.language.has_value() || invocation.standard.has_value()
            || !invocation.compatibility_switches.empty()
            || !invocation.include_directories.empty()
            || !invocation.defines.empty() || !invocation.search_libraries.empty()
            || !invocation.library_mappings.empty()
            || !invocation.library_exports.empty()
            || invocation.jobs.has_value() || invocation.optimization.has_value()) {
            argument_error(
                diagnostics,
                "simulate received a source, compile, elaboration, or project option");
            return std::nullopt;
        }
    }
    if (invocation.command != Command::simulate
        && (invocation.cache_directory.has_value()
            || invocation.file_root.has_value())) {
        argument_error(
            diagnostics,
            "--cache and --file-root are available only with simulate");
        return std::nullopt;
    }
    if (!non_project_command
        && (invocation.artifact_output.has_value()
            || !invocation.objects.empty() || !invocation.systemc_plugins.empty()
            || invocation.design.has_value()
            || invocation.engine.has_value())) {
        argument_error(
            diagnostics,
            "--output, --object, --design, and --engine are "
            "available only with artifact-phase commands");
        return std::nullopt;
    }
    const bool systemc_phase = invocation.command == Command::systemc_compile
        || invocation.command == Command::systemc_link;
    if (!systemc_phase
        && (invocation.systemc_compiler.has_value()
            || !invocation.systemc_compile_options.empty()
            || !invocation.systemc_link_options.empty()
            || !invocation.systemc_libraries.empty())) {
        argument_error(
            diagnostics,
            "--compiler, --compile-option, --link-option, and --link-library are "
            "available only with systemc compile or systemc link");
        return std::nullopt;
    }
    return invocation;
}

int run(
    const int argc,
    const char* const* argv,
    const Services& services,
    std::ostream& output,
    std::ostream& error)
{
    diagnostic::Engine diagnostics;
    const auto requested_format = requested_diagnostic_format(argc, argv);
    try {
        auto invocation = parse_arguments(argc, argv, diagnostics);
        if (!invocation.has_value()) {
            print_diagnostics(error, diagnostics, requested_format);
            return kUsageError;
        }
        if (invocation->version) {
            output << "fsim " << fsim::version << " (C API "
                   << FSIM_API_VERSION << ")\n";
            return kSuccess;
        }
        if (invocation->help) {
            print_help(output, invocation->program_name);
            return kSuccess;
        }
        std::optional<project::Config> config;
        if (invocation->command == Command::tcl
            && !invocation->manifest_explicit) {
            project::Config tcl_config;
            tcl_config.manifest_path = "<tcl>";
            std::error_code current_error;
            tcl_config.base_directory = std::filesystem::current_path(current_error);
            if (current_error) {
                tcl_config.base_directory = ".";
            }
            tcl_config.project.name = "tcl";
            config = std::move(tcl_config);
            apply_overrides(*invocation, *config);
        } else if (invocation->command == Command::compile) {
            config = make_direct_config(*invocation, diagnostics);
        } else if (invocation->command == Command::elaborate
            || invocation->command == Command::simulate
            || invocation->command == Command::systemc_compile
            || invocation->command == Command::systemc_link) {
            project::Config phase_config;
            phase_config.manifest_path = "<non-project>";
            std::error_code current_error;
            phase_config.base_directory = std::filesystem::current_path(current_error);
            if (current_error) {
                phase_config.base_directory = ".";
            }
            phase_config.project.name = std::string { command_name(invocation->command) };
            config = std::move(phase_config);
            apply_overrides(*invocation, *config);
        } else if (invocation->files.empty()) {
            config = project::load(invocation->manifest, diagnostics);
            if (config.has_value()) {
                apply_overrides(*invocation, *config);
            }
        } else {
            config = make_direct_config(*invocation, diagnostics);
        }
        if (config.has_value()) {
            validate_effective_library_mappings(*config, diagnostics);
        }
        if (!config.has_value() || diagnostics.has_error()) {
            print_diagnostics(error, diagnostics, invocation->diagnostic_format);
            return kUserError;
        }

        const Handler* handler = select_handler(services, invocation->command);
        if (handler == nullptr || !*handler) {
            diagnostics.error(
                "FSIM-CLI-0002",
                "command '" + std::string(command_name(invocation->command)) + "' is not connected to a simulation engine in this build");
            print_diagnostics(error, diagnostics, invocation->diagnostic_format);
            return kUnavailable;
        }

        const int result = (*handler)(*invocation, *config, diagnostics, output, error);
        if (!diagnostics.empty()) {
            print_diagnostics(error, diagnostics, invocation->diagnostic_format);
        }
        return result;
    } catch (const std::exception& exception) {
        diagnostics.error(
            "FSIM-CLI-0003",
            "unhandled command failure: " + std::string { exception.what() });
    } catch (...) {
        diagnostics.error(
            "FSIM-CLI-0003", "unhandled unknown command failure");
    }
    print_diagnostics(error, diagnostics, requested_format);
    return kUserError;
}

int run(
    const int argc,
    const char* const* argv,
    const Services& services)
{
    return run(argc, argv, services, std::cout, std::cerr);
}

} // namespace fsim::cli
