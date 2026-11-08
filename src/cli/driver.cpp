// SPDX-License-Identifier: Apache-2.0
#include "fsim/cli/driver.hpp"

#include "fsim/api.h"
#include "fsim/support/environment.hpp"
#include "fsim/support/native_filesystem.hpp"
#include "fsim/support/path.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

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
        case Command::coverage_merge:
            return "coverage merge";
        case Command::coverage_report:
            return "coverage report";
        case Command::library_map:
            return "library map";
        case Command::library_list:
            return "library list";
        case Command::library_unmap:
            return "library unmap";
        case Command::library_objects:
            return "library objects";
        case Command::library_delete_object:
            return "library delete-object";
        case Command::library_delete:
            return "library delete";
        case Command::snapshot_info:
            return "snapshot info";
        }
        return "check";
    }

    std::optional<Command> parse_command(const std::string_view spelling)
    {
        if (spelling == "check") {
            return Command::check;
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

    std::optional<CoverageThreshold> parse_coverage_threshold(
        const std::string_view spelling)
    {
        const auto separator = spelling.find('=');
        if (separator == 0U || separator == std::string_view::npos
            || separator + 1U == spelling.size()
            || spelling.find('=', separator + 1U) != std::string_view::npos) {
            return std::nullopt;
        }
        std::uint64_t percent { };
        if (!parse_unsigned(spelling.substr(separator + 1U), percent)
            || percent > 100U) {
            return std::nullopt;
        }
        return CoverageThreshold { lowercase(spelling.substr(0U, separator)),
            static_cast<std::uint32_t>(percent) };
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
        if (index + 1 >= argc || argv[index + 1] == nullptr) {
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

    bool valid_snapshot_name(const std::string_view value)
    {
        if (value.empty() || value.size() > 128U) {
            return false;
        }
        const auto letter = [](const unsigned char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z') || character == '_';
        };
        if (!letter(static_cast<unsigned char>(value.front()))
            || !std::ranges::all_of(value, [&](const unsigned char character) {
                   return letter(character)
                       || (character >= '0' && character <= '9')
                       || character == '-';
               })) {
            return false;
        }
        const auto normalized = lowercase(value);
        if (normalized == "con" || normalized == "prn"
            || normalized == "aux" || normalized == "nul") {
            return false;
        }
        return !(normalized.size() == 4U
            && (normalized.starts_with("com") || normalized.starts_with("lpt"))
            && normalized.back() >= '1' && normalized.back() <= '9');
    }

    bool valid_object_id(const std::string_view value)
    {
        return !value.empty() && value.size() <= 128U
            && std::ranges::all_of(value, [](const unsigned char character) {
                   return (character >= 'a' && character <= 'z')
                       || (character >= 'A' && character <= 'Z')
                       || (character >= '0' && character <= '9')
                       || character == '_' || character == '-';
               });
    }

    std::string inferred_top_alias(std::string_view target)
    {
        target = target.substr(0, target.find('('));
        // An escaped identifier (`\t.has.dots`) may contain the separators
        // (IEEE 1800-2023 5.6.1).
        if (const auto escaped = target.find('\\');
            escaped != std::string_view::npos) {
            target.remove_prefix(escaped + 1U);
        } else if (const auto separator = target.find_last_of(".:");
            separator != std::string_view::npos) {
            target.remove_prefix(separator + 1U);
        }
        std::string alias { target };
        for (auto& character : alias) {
            if (std::isalnum(static_cast<unsigned char>(character)) == 0
                && character != '_') {
                character = '_';
            }
        }
        if (alias.empty()) {
            return "top";
        }
        if (std::isdigit(static_cast<unsigned char>(alias.front())) != 0) {
            alias.insert(alias.begin(), '_');
        }
        return alias;
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

    std::optional<project::Config> make_workspace_config(
        const Invocation& invocation,
        diagnostic::Engine& diagnostics)
    {
        project::Config config;
        std::error_code error;
        config.base_directory = std::filesystem::current_path(error);
        if (error) {
            argument_error(diagnostics,
                "cannot determine the current workspace directory: "
                    + error.message());
            return std::nullopt;
        }
        config.project.name = std::string { command_name(invocation.command) };
        config.build.cache_path = config.base_directory / ".fsim" / "cache";
        return config;
    }

    std::optional<project::Config> make_direct_config(
        const Invocation& invocation,
        diagnostic::Engine& diagnostics)
    {
        auto workspace = make_workspace_config(invocation, diagnostics);
        if (!workspace) {
            return std::nullopt;
        }
        auto config = std::move(*workspace);
        if (!invocation.generics.empty()) {
            config.project.generics = invocation.generics;
        }
        if (!invocation.tops.empty()) {
            config.project.tops = invocation.tops;
            config.project.top = invocation.tops.size() == 1
                ? invocation.tops.front().target
                : std::string { };
        } else if (invocation.top.has_value()) {
            config.project.top = *invocation.top;
        }
        for (const auto& input : invocation.files) {
            const auto language = invocation.command == Command::systemc_compile
                ? std::optional { project::Language::systemc }
                : invocation.language.has_value()
                ? invocation.language : infer_language(input);
            if (!language.has_value()) {
                argument_error(
                    diagnostics,
                    "cannot infer the language for '"
                        + fsim::support::path_to_utf8(input) + "'; pass --lang");
                continue;
            }
            if (invocation.command == Command::compile
                && *language == project::Language::systemc) {
                argument_error(diagnostics,
                    "use systemc compile for SystemC translation unit '"
                        + support::path_to_utf8(input) + "'");
                continue;
            }
            if (!invocation.compatibility_switches.empty()
                && *language != project::Language::verilog
                && *language != project::Language::system_verilog) {
                argument_error(diagnostics,
                    "--compatibility is available only for Verilog/SystemVerilog");
                continue;
            }
            if (invocation.vhdl_compatibility.has_value()
                && *language != project::Language::vhdl) {
                argument_error(diagnostics,
                    "--vhdl-compatibility is available only for VHDL");
                continue;
            }
            const auto path = absolute_normalized(input);
            std::error_code file_error;
            if (!support::native_fs::is_regular_file(path, file_error)) {
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
                auto source_set = make_source_set(invocation, *language, diagnostics);
                if (!source_set) {
                    continue;
                }
                config.source_sets.push_back(std::move(*source_set));
                iterator = std::prev(config.source_sets.end());
            }
            iterator->file_patterns.push_back(path);
            iterator->files.push_back(path);
        }

        if (invocation.duration.has_value()) {
            config.run.duration = invocation.duration;
        }
        if (invocation.save_at.has_value()) {
            config.run.save_at = invocation.save_at;
        }
        if (invocation.save_file.has_value()) {
            config.run.save_file = absolute_normalized(*invocation.save_file);
        }
        if (invocation.restart_file.has_value()) {
            config.run.restart_file
                = absolute_normalized(*invocation.restart_file);
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
        if (invocation.foreign_probes.has_value()) {
            config.run.foreign_probes = *invocation.foreign_probes;
        }
        if (invocation.vhpi_end_of_step_timers.has_value()) {
            config.run.vhpi_end_of_step_timers
                = *invocation.vhpi_end_of_step_timers;
        }
        if (invocation.coverage_output.has_value()) {
            config.run.coverage_output
                = absolute_normalized(*invocation.coverage_output);
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
        if (invocation.code_coverage.has_value()) {
            config.coverage.enabled = *invocation.code_coverage;
        }
        if (!invocation.search_libraries.empty()) {
            config.elaboration.search_libraries = invocation.search_libraries;
        }
        config.elaboration.vpi_modules = invocation.vpi_modules;
        config.run.vpi_modules = invocation.vpi_modules;
        config.run.sv_libs = invocation.sv_libs;
        config.run.vhpi_modules = invocation.vhpi_modules;
        config.run.vhpi_registries = invocation.vhpi_registries;
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
        if (invocation.time_resolution.has_value()) {
            config.project.time_resolution = *invocation.time_resolution;
        }
        if (!invocation.generics.empty()) {
            config.project.generics = invocation.generics;
        }
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
        if (invocation.save_at.has_value()) {
            config.run.save_at = invocation.save_at;
        }
        if (invocation.save_file.has_value()) {
            config.run.save_file = absolute_normalized(*invocation.save_file);
        }
        if (invocation.restart_file.has_value()) {
            config.run.restart_file
                = absolute_normalized(*invocation.restart_file);
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
        if (invocation.foreign_probes.has_value()) {
            config.run.foreign_probes = *invocation.foreign_probes;
        }
        if (invocation.vhpi_end_of_step_timers.has_value()) {
            config.run.vhpi_end_of_step_timers
                = *invocation.vhpi_end_of_step_timers;
        }
        if (invocation.coverage_output.has_value()) {
            config.run.coverage_output
                = absolute_normalized(*invocation.coverage_output);
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
        if (invocation.code_coverage.has_value()) {
            config.coverage.enabled = *invocation.code_coverage;
        }
        if (!invocation.search_libraries.empty()) {
            config.elaboration.search_libraries = invocation.search_libraries;
        }
        config.elaboration.vpi_modules = invocation.vpi_modules;
        config.run.vpi_modules = invocation.vpi_modules;
        config.run.sv_libs = invocation.sv_libs;
        config.run.vhpi_modules = invocation.vhpi_modules;
        config.run.vhpi_registries = invocation.vhpi_registries;
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
            << " <check|compile|elaborate|simulate|debug|tcl|systemc|library|coverage>"
               " [options] [files...]\n"
            << "\n"
            << "Commands:\n"
            << "  check FILE...          Parse and analyze explicit sources\n"
            << "  compile FILE...        Compile sources into a workspace library\n"
            << "  elaborate TOP...       Elaborate library units into a snapshot\n"
            << "  simulate               Simulate a workspace snapshot\n"
            << "  debug                  Open the Tcl debugger on a workspace snapshot\n"
            << "  tcl                    Open the Tcl console or evaluate a script\n"
            << "  systemc compile FILE... Compile SystemC translation units into a library\n"
            << "  systemc link           Link the managed SystemC library objects\n"
            << "  library map NAME PATH  Map a library directory or an exported .fsimlib\n"
            << "  library list           List local and mapped libraries\n"
            << "  library unmap NAME     Remove an external library mapping\n"
            << "  library objects [NAME] List managed object IDs and units (default: work)\n"
            << "  library delete-object NAME OBJECT_ID\n"
            << "                         Delete an object from a library\n"
            << "  library delete NAME    Delete a managed library\n"
            << "  snapshot info [NAME]   Show what a snapshot was elaborated with\n"
            << "  coverage merge Merge .fsimcov databases (strict by default)\n"
            << "  coverage report Render or enforce a .fsimcov report\n"
            << "  cocotb makefile|pythonpath|libs\n"
            << "                         Print the cocotb Makefile.fsim, the fsim_cocotb\n"
            << "                         Python path, or the GPI implementations used\n"
            << "\n"
            << "The workspace is the current directory. fsim manages libraries in\n"
            << ".fsim/libraries, snapshots in .fsim/snapshots, and mappings in\n"
            << ".fsim/libraries.toml. Recompilation and re-elaboration replace managed data.\n"
            << "\n"
            << "Workspace and source options:\n"
            << "      --snapshot NAME     Snapshot for elaborate/simulate/debug/tcl (default: default)\n"
            << "      --top [ALIAS=]NAME   Add an elaboration top; aliases disambiguate root names\n"
            << "                           Use NAME or LIBRARY.NAME; add LANGUAGE: only if ambiguous\n"
            << "                           Example: vhdl:work.tb, systemverilog:work.tb, systemc:tb\n"
            << "      --lang LANGUAGE      Override the language inferred from source extensions\n"
            << "      --standard VERSION   Override the language's default standard\n"
            << "                           VHDL: 87/1987, 93/1993, 00/2000, 02/2002, 08/2008, 19/2019\n"
            << "                           Verilog: 95/1995, 01/2001, 2001-noconfig, 05/2005\n"
            << "                           SystemVerilog: 05/2005, 09/2009, 12/2012, 17/2017, 23/2023\n"
            << "      --compatibility NAME Explicit compatibility switch; repeatable\n"
            << "                           keyword-profile, implicit-net, port-connection, sizing,\n"
            << "                           lifetime, scheduler-assertion, configuration\n"
            << "      --vhdl-compatibility PROFILE\n"
            << "                           VHDL: legacy-unprotected-shared-variable\n"
            << "      --uvm-release VERSION\n"
            << "                           Governed SystemVerilog UVM release: 1.2 or 2020.3.1\n"
            << "      --compilation-unit file|source-set\n"
            << "                           Compile files separately or as one unit\n"
            << "      --library NAME       Library for direct source files (default: work)\n"
            << "      --search-library NAME\n"
            << "                           Add a library to compile/elaborate searches; repeatable\n"
            << "  -I, --include PATH       Add a direct-source include directory\n"
            << "  -D, --define NAME[=VAL]  Add a direct-source preprocessor definition\n"
            << "      --timescale UNIT/PRECISION\n"
            << "                           Verilog/SystemVerilog timescale of design\n"
            << "                           elements no `timescale applies to (1ns/1ps)\n"
            << "      --preprocess-only    compile: run the Verilog preprocessor and stop\n"
            << "      --dpi-header PATH    compile: write the DPI-C import/export C prototypes\n"
            << "      --verbosity quiet|normal|verbose\n"
            << "                           Compile/elaborate/library detail (default: normal)\n"
            << "  -q, --quiet              Suppress command progress\n"
            << "  -v, --verbose            Show detailed progress and library objects\n"
            << "      --compiler PATH      SystemC C++ compiler executable\n"
            << "      --compile-option ARG SystemC compiler option; repeatable\n"
            << "      --link-option ARG    SystemC linker option; repeatable\n"
            << "      --link-library ARG   SystemC link library/path; repeatable\n"
            << "      --aot                Populate the v4 kernel code cache after elaborate\n"
            << "      --no-aot             Disable post-elaboration native compilation\n"
            << "      --aot-scope selected|all|kernel\n"
            << "                           Accepted for compatibility; ignored\n"
            << "\n"
            << "Elaboration and simulation options:\n"
            << "      --generic NAME=VALUE Set a top-level generic or parameter; repeatable\n"
            << "      --time-resolution auto|TIME\n"
            << "                           elaborate: simulation time resolution (1fs, 1ps)\n"
            << "  -O, --optimization O0..O3\n"
            << "  -j, --jobs COUNT\n"
            << "      --code-coverage      Enable statement, branch, condition, toggle,\n"
            << "                           and FSM coverage\n"
            << "      --coverage-output PATH\n"
            << "                           Write the coverage database when simulation ends\n"
            << "      --duration TIME\n"
            << "      --save-at TIME       Save the run state at the end of the time slot\n"
            << "                           at TIME (needs --save-file)\n"
            << "      --save-file FILE     File that --save-at writes\n"
            << "      --restart FILE       Start from a saved run state; repeat the command\n"
            << "                           line of the save\n"
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
            << "      --cache PATH         Override the managed .fsim/cache native cache\n"
            << "      --file-root PATH     File-I/O root for snapshot simulation\n"
            << "      --engine interpreter|compiled|debug|v4\n"
            << "                           v4 (default; compiled is an alias of v4), the\n"
            << "                           reference interpreter, or debug (reference\n"
            << "                           with debugger hooks)\n"
            << "      --compiled-processes auto|selected|all\n"
            << "                           Accepted for compatibility; ignored\n"
            << "      --seed COUNT|random\n"
            << "      --vpi LIB[:BOOTSTRAP]\n"
            << "                           Load a VPI module; repeatable (elaborate, simulate)\n"
            << "      --sv-lib LIB         Load a DPI-C library; repeatable\n"
            << "      --vhpi LIB[:BOOTSTRAP]\n"
            << "                           Load a VHPI module; repeatable (simulate)\n"
            << "      --vhpi-registry FILE Read a VHPI tabular registry; repeatable\n"
            << "                           (simulate)\n"
            << "      --foreign-probes     Give VPI/VHPI libraries statement, call and\n"
            << "                           suspend callbacks (cbStmt, vhpiCbStmt, ...)\n"
            << "      --vhpi-end-of-step-timers\n"
            << "                           Accept VHPI timers registered at the end of a\n"
            << "                           time step; they run from the next step\n"
            << "      --diagnostics text|json\n"
            << "      --color auto|always|never\n"
            << "\n"
            << "Coverage options:\n"
            << "  -o, --output PATH        Coverage merge database or report output\n"
            << "      --partial            Merge unchanged point identities explicitly\n"
            << "      --format FORMAT      text, html, json, lcov, or cobertura\n"
            << "      --threshold METRIC=PERCENT\n"
            << "                           Require a per-family combined score; repeatable\n"
            << "                           Unmet thresholds return CI exit status 4\n"
            << "\n"
            << "Tcl options:\n"
            << "  -c, --command SCRIPT    Evaluate Tcl text (repeatable)\n"
            << "  SCRIPT [ARG...]         Evaluate a Tcl file with argv/argc set\n"
            << "                          Available with tcl or debug; omit both for the Tcl console\n"
            << "\n"
            << "Configuration:\n"
            << "      --no-config          Run no configuration script (also FSIM_NO_CONFIG=1)\n"
            << "  Every command first runs the per-user fsim.tcl ($XDG_CONFIG_HOME/fsim,\n"
            << "  ~/.config/fsim, or %APPDATA%\\fsim) and then the workspace's fsim.tcl.\n"
            << "  They set option defaults with fsim::config and elaboration settings\n"
            << "  with fsim::coverage and fsim::bind; an option given here wins.\n"
            << "\n"
            << "  -h, --help\n"
            << "      --version\n";
    }

    void print_diagnostics(
        std::ostream& error,
        const diagnostic::Engine& diagnostics,
        const DiagnosticFormat format,
        const diagnostic::ColorMode color_mode,
        const bool error_is_terminal,
        const bool no_color)
    {
        if (format == DiagnosticFormat::json) {
            diagnostic::print_json(error, diagnostics);
        } else {
            diagnostic::print_text(
                error,
                diagnostics,
                diagnostic::color_enabled(
                    color_mode, error_is_terminal, no_color));
        }
    }

    bool standard_error_is_terminal() noexcept
    {
#if defined(_WIN32)
        return _isatty(_fileno(stderr)) != 0;
#else
        return ::isatty(STDERR_FILENO) != 0;
#endif
    }

    bool standard_output_is_terminal() noexcept
    {
#if defined(_WIN32)
        return _isatty(_fileno(stdout)) != 0;
#else
        return ::isatty(STDOUT_FILENO) != 0;
#endif
    }

    bool no_color_requested() noexcept
    {
        const char* value = std::getenv("NO_COLOR");
        return value != nullptr && value[0] != '\0';
    }

    const Handler* select_handler(const Services& services, const Command command,
        const bool builtin_stdio)
    {
        switch (command) {
        case Command::check:
            return &services.check;
        case Command::build:
            return &services.build;
        case Command::run:
            return builtin_stdio && services.stdio_run
                ? &services.stdio_run : &services.run;
        case Command::debug:
            return &services.debug;
        case Command::tcl:
            return &services.tcl;
        case Command::compile:
            return &services.compile;
        case Command::elaborate:
            return &services.elaborate;
        case Command::simulate:
            return builtin_stdio && services.stdio_simulate
                ? &services.stdio_simulate : &services.simulate;
        case Command::systemc_compile:
            return &services.systemc_compile;
        case Command::systemc_link:
            return &services.systemc_link;
        case Command::coverage_merge:
            return &services.coverage_merge;
        case Command::coverage_report:
            return &services.coverage_report;
        case Command::library_map:
            return &services.library_map;
        case Command::library_list:
            return &services.library_list;
        case Command::library_unmap:
            return &services.library_unmap;
        case Command::library_objects:
            return &services.library_objects;
        case Command::library_delete_object:
            return &services.library_delete_object;
        case Command::library_delete:
            return &services.library_delete;
        case Command::snapshot_info:
            return &services.snapshot_info;
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
            if (argument == "--") {
                break;
            }
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

    diagnostic::ColorMode requested_color_mode(
        const int argc,
        const char* const* argv) noexcept
    {
        auto result = diagnostic::ColorMode::automatic;
        if (argc <= 1 || argv == nullptr) {
            return result;
        }
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) {
                continue;
            }
            const std::string_view argument { argv[index] };
            if (argument == "--") {
                break;
            }
            std::string_view value;
            if (argument.starts_with("--color=")) {
                value = argument.substr(std::string_view { "--color=" }.size());
            } else if (
                argument == "--color" && index + 1 < argc
                && argv[index + 1] != nullptr) {
                value = argv[++index];
            } else {
                continue;
            }
            if (value == "always") {
                result = diagnostic::ColorMode::always;
            } else if (value == "never") {
                result = diagnostic::ColorMode::never;
            } else if (value == "auto") {
                result = diagnostic::ColorMode::automatic;
            }
        }
        return result;
    }

    enum class OptionScan {
        unrecognized,
        flag,
        separate_value,
        inline_value,
    };

    OptionScan scan_cli_option(const std::string_view argument) noexcept
    {
        static constexpr std::string_view value_options[] = {
            "--snapshot",
            "--verbosity",
            "--top",
            "--generic",
            "--lang",
            "--standard",
            "--compatibility",
            "--vhdl-compatibility",
            "--uvm-release",
            "--compilation-unit",
            "--library",
            "--search-library",
            "-o",
            "--output",
            "--compiler",
            "--compile-option",
            "--link-option",
            "--link-library",
            "--vpi",
            "--sv-lib",
            "--vhpi",
            "--vhpi-registry",
            "--cache",
            "--aot-scope",
            "--compiled-processes",
            "--file-root",
            "-I",
            "--include",
            "-D",
            "--define",
            "--timescale",
            "--time-resolution",
            "-O",
            "--optimization",
            "-j",
            "--jobs",
            "--format",
            "--threshold",
            "--duration",
            "--save-at",
            "--save-file",
            "--restart",
            "--max-deltas",
            "--delay-mode",
            "--sdf",
            "--sdf-root",
            "--sdf-cell",
            "--sdf-report-limit",
            "--dpi-header",
            "--trace",
            "--trace-output",
            "--coverage-output",
            "--trace-format",
            "--trace-compression",
            "--trace-filter",
            "--trace-select",
            "--trace-lifecycle",
            "--engine",
            "--seed",
            "--diagnostics",
            "--color",
            "-c",
            "--command",
        };
        for (const auto option : value_options) {
            if (argument == option) {
                return OptionScan::separate_value;
            }
            if (option.starts_with("--")
                && argument.size() > option.size()
                && argument.starts_with(option)
                && argument[option.size()] == '=') {
                return OptionScan::inline_value;
            }
        }

        static constexpr std::string_view flags[] = {
            "-h",
            "--help",
            "--version",
            "-q",
            "--quiet",
            "-v",
            "--verbose",
            "--aot",
            "--no-aot",
            "--code-coverage",
            "--foreign-probes",
            "--vhpi-end-of-step-timers",
            "--partial",
            "--preprocess-only",
            "--no-config",
        };
        for (const auto flag : flags) {
            if (argument == flag) {
                return OptionScan::flag;
            }
        }

        if ((argument.starts_with("-I") && argument.size() > 2)
            || (argument.starts_with("-D") && argument.size() > 2)
            || (argument.starts_with("-j") && argument.size() > 2)
            || (argument.starts_with("-O") && argument.size() == 3)) {
            return OptionScan::inline_value;
        }
        return OptionScan::unrecognized;
    }

    std::optional<Command> entry_command_for_color_policy(
        const int argc,
        const char* const* argv) noexcept
    {
        if (argc <= 1 || argv == nullptr) {
            return std::nullopt;
        }

        bool positional_only = false;
        for (int index = 1; index < argc; ++index) {
            if (argv[index] == nullptr) {
                return std::nullopt;
            }
            const std::string_view argument { argv[index] };
            if (!positional_only && argument == "--") {
                positional_only = true;
                continue;
            }
            if (!positional_only && argument.starts_with('-')) {
                switch (scan_cli_option(argument)) {
                case OptionScan::separate_value:
                    if (index + 1 >= argc || argv[index + 1] == nullptr) {
                        return std::nullopt;
                    }
                    ++index;
                    continue;
                case OptionScan::flag:
                case OptionScan::inline_value:
                    continue;
                case OptionScan::unrecognized:
                    return std::nullopt;
                }
            }

            const auto command = parse_command(argument);
            if (command == Command::tcl || command == Command::debug) {
                return command;
            }
            return std::nullopt;
        }
        return std::nullopt;
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
    bool snapshot_explicit = false;
    bool verbosity_explicit = false;
    bool library_explicit = false;
    bool standard_explicit = false;

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
            if (is_option(argument, "", "--snapshot")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--snapshot", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.snapshot = std::string { *value };
                snapshot_explicit = true;
            } else if (is_option(argument, "", "--verbosity")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--verbosity", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (*value == "quiet") {
                    invocation.verbosity = Verbosity::quiet;
                } else if (*value == "normal") {
                    invocation.verbosity = Verbosity::normal;
                } else if (*value == "verbose") {
                    invocation.verbosity = Verbosity::verbose;
                } else {
                    argument_error(diagnostics,
                        "--verbosity must be quiet, normal, or verbose");
                    return std::nullopt;
                }
                verbosity_explicit = true;
            } else if (argument == "--no-config") {
                invocation.no_config = true;
            } else if (argument == "-q" || argument == "--quiet") {
                invocation.verbosity = Verbosity::quiet;
                verbosity_explicit = true;
            } else if (argument == "-v" || argument == "--verbose") {
                invocation.verbosity = Verbosity::verbose;
                verbosity_explicit = true;
            } else if (is_option(argument, "", "--top")) {
                const auto value = take_value(index, argc, argv, argument, "--top", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.tops.push_back(parse_top_option(*value));
            } else if (is_option(argument, "", "--generic")) {
                const auto value = take_value(index, argc, argv, argument, "--generic", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                const auto separator = value->find('=');
                if (separator == std::string_view::npos || separator == 0U) {
                    argument_error(diagnostics,
                        "--generic requires NAME=VALUE, received '" + std::string(*value) + "'");
                    return std::nullopt;
                }
                invocation.generics.emplace_back(
                    std::string(value->substr(0U, separator)),
                    std::string(value->substr(separator + 1U)));
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
                if (!standard_explicit) {
                    const auto standard = project::canonical_standard(
                        *invocation.language, *value);
                    if (standard) {
                        invocation.standard = std::string { *standard };
                    } else {
                        invocation.standard.reset();
                    }
                }
            } else if (is_option(argument, "", "--standard")) {
                const auto value = take_value(index, argc, argv, argument, "--standard", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.standard = std::string(*value);
                standard_explicit = true;
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
            } else if (is_option(argument, "", "--vhdl-compatibility")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--vhdl-compatibility", diagnostics);
                const auto canonical = value.has_value()
                    ? project::parse_vhdl_compatibility(*value)
                    : std::nullopt;
                if (!canonical || canonical->empty()) {
                    argument_error(
                        diagnostics,
                        "--vhdl-compatibility requires "
                        "legacy-unprotected-shared-variable");
                    return std::nullopt;
                }
                if (invocation.vhdl_compatibility.has_value()) {
                    argument_error(
                        diagnostics,
                        "--vhdl-compatibility may be specified only once");
                    return std::nullopt;
                }
                invocation.vhdl_compatibility = std::string { *canonical };
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
                library_explicit = true;
                invocation.library_explicit = true;
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
            } else if (is_option(argument, "-o", "--output")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--output", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--output requires a non-empty path");
                    return std::nullopt;
                }
                invocation.artifact_output = fsim::support::path_from_utf8(*value);
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
            } else if (is_option(argument, "", "--vpi")
                || is_option(argument, "", "--sv-lib")
                || is_option(argument, "", "--vhpi")) {
                const bool vpi = is_option(argument, "", "--vpi");
                const bool vhpi = is_option(argument, "", "--vhpi");
                const auto name = vpi ? "--vpi" : vhpi ? "--vhpi" : "--sv-lib";
                const auto value
                    = take_value(index, argc, argv, argument, name, diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics,
                        std::string { name } + " requires a library path");
                    return std::nullopt;
                }
                (vpi       ? invocation.vpi_modules
                        : vhpi ? invocation.vhpi_modules
                               : invocation.sv_libs)
                    .emplace_back(*value);
            } else if (is_option(argument, "", "--vhpi-registry")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--vhpi-registry", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(
                        diagnostics, "--vhpi-registry requires a file path");
                    return std::nullopt;
                }
                invocation.vhpi_registries.emplace_back(*value);
            } else if (is_option(argument, "", "--link-library")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--link-library", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(
                        diagnostics, "--link-library requires a non-empty value");
                    return std::nullopt;
                }
                invocation.systemc_libraries.emplace_back(*value);
            } else if (is_option(argument, "", "--cache")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--cache", diagnostics);
                if (!value.has_value() || value->empty()) {
                    argument_error(diagnostics, "--cache requires a non-empty path");
                    return std::nullopt;
                }
                invocation.cache_directory = fsim::support::path_from_utf8(*value);
            } else if (argument == "--aot") {
                invocation.aot = true;
            } else if (argument == "--no-aot") {
                invocation.aot = false;
            } else if (is_option(argument, "", "--aot-scope")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--aot-scope", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                const auto normalized = lowercase(*value);
                if (normalized == "selected") {
                    invocation.aot_scope = AotScope::selected;
                } else if (normalized == "all") {
                    invocation.aot_scope = AotScope::all;
                } else if (normalized == "kernel") {
                    invocation.aot_scope = AotScope::kernel;
                } else {
                    argument_error(diagnostics,
                        "--aot-scope must be selected, all, or kernel");
                    return std::nullopt;
                }
            } else if (is_option(argument, "", "--compiled-processes")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--compiled-processes", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                const auto normalized = lowercase(*value);
                if (normalized == "auto") {
                    invocation.compiled_processes
                        = CompiledProcessPolicy::automatic;
                } else if (normalized == "selected") {
                    invocation.compiled_processes
                        = CompiledProcessPolicy::selected;
                } else if (normalized == "all") {
                    invocation.compiled_processes
                        = CompiledProcessPolicy::all;
                } else {
                    argument_error(diagnostics,
                        "--compiled-processes must be auto, selected, or all");
                    return std::nullopt;
                }
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
            } else if (is_option(argument, "", "--timescale")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--timescale", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (const auto problem
                    = project::validate_default_timescale(*value)) {
                    argument_error(diagnostics, "--timescale: " + *problem);
                    return std::nullopt;
                }
                invocation.default_timescale = std::string { *value };
            } else if (is_option(argument, "", "--time-resolution")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--time-resolution", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (const auto problem
                    = project::validate_time_resolution(*value)) {
                    argument_error(diagnostics, "--time-resolution: " + *problem);
                    return std::nullopt;
                }
                invocation.time_resolution = std::string { *value };
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
                invocation.optimization = project::parse_optimization(*value);
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
            } else if (argument == "--foreign-probes") {
                invocation.foreign_probes = true;
            } else if (argument == "--vhpi-end-of-step-timers") {
                invocation.vhpi_end_of_step_timers = true;
            } else if (argument == "--code-coverage") {
                invocation.code_coverage = true;
            } else if (argument == "--partial") {
                invocation.coverage_partial_merge = true;
            } else if (argument == "--preprocess-only") {
                invocation.preprocess_only = true;
            } else if (is_option(argument, "", "--dpi-header")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--dpi-header", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.dpi_header = fsim::support::path_from_utf8(*value);
            } else if (is_option(argument, "", "--format")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--format", diagnostics);
                if (!value.has_value()
                    || (*value != "text" && *value != "html"
                        && *value != "json" && *value != "lcov"
                        && *value != "cobertura")) {
                    argument_error(diagnostics,
                        "--format must be text, html, json, lcov, or cobertura");
                    return std::nullopt;
                }
                invocation.coverage_report_format = std::string { *value };
            } else if (is_option(argument, "", "--threshold")) {
                const auto value = take_value(
                    index, argc, argv, argument, "--threshold", diagnostics);
                const auto threshold = value.has_value()
                    ? parse_coverage_threshold(*value)
                    : std::nullopt;
                if (!threshold.has_value()) {
                    argument_error(diagnostics,
                        "--threshold requires METRIC=PERCENT with an integer from 0 to 100");
                    return std::nullopt;
                }
                if (std::ranges::find(invocation.coverage_thresholds,
                        threshold->metric, &CoverageThreshold::metric)
                    != invocation.coverage_thresholds.end()) {
                    argument_error(diagnostics,
                        "coverage thresholds must name each metric once");
                    return std::nullopt;
                }
                invocation.coverage_thresholds.push_back(*threshold);
            } else if (is_option(argument, "", "--duration")) {
                const auto value = take_value(index, argc, argv, argument, "--duration", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.duration = std::string(*value);
            } else if (is_option(argument, "", "--save-at")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--save-at", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.save_at = std::string(*value);
            } else if (is_option(argument, "", "--save-file")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--save-file", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.save_file = fsim::support::path_from_utf8(*value);
            } else if (is_option(argument, "", "--restart")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--restart", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.restart_file = fsim::support::path_from_utf8(*value);
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
            } else if (is_option(argument, "", "--coverage-output")) {
                const auto value = take_value(index, argc, argv, argument,
                    "--coverage-output", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                invocation.coverage_output = fsim::support::path_from_utf8(*value);
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
                        && *value != "debug" && *value != "v4")) {
                    argument_error(
                        diagnostics,
                        "--engine must be interpreter, compiled, debug, or v4");
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
            } else if (is_option(argument, "", "--color")) {
                const auto value
                    = take_value(index, argc, argv, argument, "--color", diagnostics);
                if (!value.has_value()) {
                    return std::nullopt;
                }
                if (*value == "auto") {
                    invocation.color_mode = diagnostic::ColorMode::automatic;
                } else if (*value == "always") {
                    invocation.color_mode = diagnostic::ColorMode::always;
                } else if (*value == "never") {
                    invocation.color_mode = diagnostic::ColorMode::never;
                } else {
                    argument_error(diagnostics,
                        "color must be 'auto', 'always', or 'never'");
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
            if (argument == "library") {
                if (index + 1 >= argc || argv[index + 1] == nullptr) {
                    argument_error(diagnostics,
                        "library requires map, list, unmap, objects, "
                        "delete-object, or delete");
                    return std::nullopt;
                }
                const std::string_view subcommand { argv[++index] };
                if (subcommand == "map") {
                    invocation.command = Command::library_map;
                } else if (subcommand == "list") {
                    invocation.command = Command::library_list;
                } else if (subcommand == "unmap") {
                    invocation.command = Command::library_unmap;
                } else if (subcommand == "objects") {
                    invocation.command = Command::library_objects;
                } else if (subcommand == "delete-object") {
                    invocation.command = Command::library_delete_object;
                } else if (subcommand == "delete") {
                    invocation.command = Command::library_delete;
                } else {
                    argument_error(diagnostics,
                        "unknown library subcommand '"
                            + std::string { subcommand }
                            + "'; expected map, list, unmap, objects, "
                              "delete-object, or delete");
                    return std::nullopt;
                }
                command_selected = true;
                continue;
            }
            if (argument == "coverage") {
                if (index + 1 >= argc || argv[index + 1] == nullptr) {
                    argument_error(
                        diagnostics, "coverage requires a merge or report subcommand");
                    return std::nullopt;
                }
                const std::string_view subcommand { argv[++index] };
                if (subcommand == "merge") {
                    invocation.command = Command::coverage_merge;
                } else if (subcommand == "report") {
                    invocation.command = Command::coverage_report;
                } else {
                    argument_error(diagnostics,
                        "unknown coverage subcommand '"
                            + std::string { subcommand }
                            + "'; expected merge or report");
                    return std::nullopt;
                }
                command_selected = true;
                continue;
            }
            if (argument == "cocotb") {
                const std::string_view subcommand
                    = index + 1 < argc && argv[index + 1] != nullptr
                    ? std::string_view { argv[index + 1] }
                    : std::string_view { };
                if (subcommand != "makefile" && subcommand != "pythonpath"
                    && subcommand != "libs") {
                    argument_error(diagnostics,
                        "cocotb requires a makefile, pythonpath, or libs "
                        "subcommand");
                    return std::nullopt;
                }
                if (index + 2 < argc) {
                    argument_error(diagnostics,
                        "cocotb " + std::string { subcommand }
                            + " takes no arguments");
                    return std::nullopt;
                }
                invocation.cocotb_query = std::string { subcommand };
                return invocation;
            }
            if (argument == "snapshot") {
                if (index + 1 >= argc || argv[index + 1] == nullptr
                    || std::string_view { argv[index + 1] } != "info") {
                    argument_error(diagnostics, "snapshot requires the info subcommand");
                    return std::nullopt;
                }
                ++index;
                invocation.command = Command::snapshot_info;
                command_selected = true;
                continue;
            }
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
                    "unknown command '" + std::string(argument)
                        + "'; expected check, compile, elaborate, simulate, "
                          "debug, tcl, systemc, library, snapshot, coverage, "
                          "or cocotb");
                return std::nullopt;
            }
            invocation.command = *command;
            command_selected = true;
        } else if (invocation.command == Command::snapshot_info) {
            if (snapshot_explicit) {
                argument_error(diagnostics, "snapshot info takes one snapshot name");
                return std::nullopt;
            }
            invocation.snapshot = std::string { argument };
            snapshot_explicit = true;
        } else if (invocation.command == Command::library_map
            || invocation.command == Command::library_unmap
            || invocation.command == Command::library_objects
            || invocation.command == Command::library_delete_object
            || invocation.command == Command::library_delete) {
            if (!invocation.library_name) {
                invocation.library_name = std::string { argument };
            } else if (invocation.command == Command::library_map
                && !invocation.library_mapping_path) {
                invocation.library_mapping_path
                    = fsim::support::path_from_utf8(argument);
            } else if (invocation.command == Command::library_delete_object
                && !invocation.library_object_id) {
                invocation.library_object_id = std::string { argument };
            } else {
                argument_error(diagnostics,
                    "too many arguments for "
                        + std::string { command_name(invocation.command) });
                return std::nullopt;
            }
        } else if (invocation.command == Command::elaborate) {
            invocation.tops.push_back(parse_top_option(argument));
        } else if (invocation.command == Command::tcl
            || invocation.command == Command::debug) {
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
            "missing command; expected check, compile, elaborate, simulate, "
            "debug, tcl, systemc, library, snapshot, or coverage");
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
                if (invocation.tops.size() > 1) {
                    top.alias = inferred_top_alias(top.target);
                }
            }
            if (!top.alias.empty() && !valid_top_alias(top.alias)) {
                argument_error(
                    diagnostics,
                    "top alias '" + top.alias
                        + "' must start with a letter or underscore and contain "
                          "only letters, digits, and underscores");
                return std::nullopt;
            } else if (!top.alias.empty()
                && std::ranges::find(aliases, top.alias) != aliases.end()) {
                argument_error(diagnostics,
                    "duplicate top alias '" + top.alias
                        + "'; use ALIAS=NAME to disambiguate the roots");
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
    const bool source_command = invocation.command == Command::check
        || invocation.command == Command::compile;
    const bool systemc_compile = invocation.command == Command::systemc_compile;
    const bool systemc_phase = systemc_compile
        || invocation.command == Command::systemc_link;
    const bool elaborate = invocation.command == Command::elaborate;
    const bool snapshot_consumer = invocation.command == Command::simulate
        || invocation.command == Command::debug
        || invocation.command == Command::tcl;
    const bool coverage_utility = invocation.command == Command::coverage_merge
        || invocation.command == Command::coverage_report;
    const bool library_utility = invocation.command == Command::library_map
        || invocation.command == Command::library_list
        || invocation.command == Command::library_unmap
        || invocation.command == Command::library_objects
        || invocation.command == Command::library_delete_object
        || invocation.command == Command::library_delete;
    const bool compilation = invocation.command == Command::compile
        || systemc_phase;

    if (!invocation.help && !invocation.version) {
        if ((source_command || systemc_compile) && invocation.files.empty()) {
            argument_error(diagnostics,
                std::string { command_name(invocation.command) }
                    + " requires explicit source files");
            return std::nullopt;
        }
        if (elaborate && invocation.tops.empty()) {
            argument_error(diagnostics,
                "elaborate requires at least one top level");
            return std::nullopt;
        }
        if (!source_command && !systemc_compile && !coverage_utility
            && !invocation.files.empty()) {
            argument_error(diagnostics,
                std::string { command_name(invocation.command) }
                    + " does not accept source files or artifact paths");
            return std::nullopt;
        }
        if (invocation.command == Command::library_map
            && (!invocation.library_name
                || !invocation.library_mapping_path
                || invocation.library_mapping_path->empty())) {
            argument_error(diagnostics, "library map requires NAME DIRECTORY");
            return std::nullopt;
        }
        if ((invocation.command == Command::library_unmap
                || invocation.command == Command::library_delete)
            && !invocation.library_name) {
            argument_error(diagnostics,
                std::string { command_name(invocation.command) }
                    + " requires NAME");
            return std::nullopt;
        }
        if (invocation.command == Command::library_delete_object
            && (!invocation.library_name || !invocation.library_object_id
                || invocation.library_object_id->empty())) {
            argument_error(diagnostics,
                "library delete-object requires NAME OBJECT_ID");
            return std::nullopt;
        }
    }
    if (invocation.library_name) {
        const auto& name = *invocation.library_name;
        const auto normalized = lowercase(name);
        if (!project::valid_library_name(name) || normalized == "std"
            || normalized == "ieee" || normalized == "fsim") {
            argument_error(diagnostics,
                "library commands require a safe, non-reserved library name");
            return std::nullopt;
        }
    }
    if (invocation.library_object_id
        && !valid_object_id(*invocation.library_object_id)) {
        argument_error(diagnostics,
            "library delete-object requires an object ID, not a path");
        return std::nullopt;
    }
    if (!project::valid_library_name(invocation.library)) {
        argument_error(diagnostics, "--library requires a safe library name");
        return std::nullopt;
    }
    if (library_explicit && !source_command && !systemc_phase) {
        argument_error(diagnostics,
            "--library is available only with source compilation or checking");
        return std::nullopt;
    }
    if (!valid_snapshot_name(invocation.snapshot)) {
        argument_error(diagnostics,
            "--snapshot requires a portable name of at most 128 characters, "
            "starting with a letter or '_' and containing letters, digits, "
            "'_', or '-'");
        return std::nullopt;
    }
    if (snapshot_explicit && !elaborate && !snapshot_consumer
        && invocation.command != Command::snapshot_info) {
        argument_error(diagnostics,
            "--snapshot requires elaborate, simulate, debug, or tcl");
        return std::nullopt;
    }
    if (verbosity_explicit && !compilation && !elaborate && !library_utility) {
        argument_error(diagnostics,
            "--verbosity requires compile, elaborate, or library commands");
        return std::nullopt;
    }
    if (!elaborate && !invocation.tops.empty()) {
        argument_error(diagnostics, "--top is available only with elaborate");
        return std::nullopt;
    }
    const bool hdl_option = invocation.language.has_value()
        || invocation.standard.has_value()
        || !invocation.compatibility_switches.empty()
        || invocation.vhdl_compatibility.has_value()
        || invocation.compilation_unit.has_value()
        || invocation.uvm_release.has_value();
    if (hdl_option && !source_command) {
        argument_error(diagnostics,
            "HDL language options require compile or check");
        return std::nullopt;
    }
    if (invocation.command == Command::compile
        && invocation.language == project::Language::systemc) {
        argument_error(diagnostics,
            "use systemc compile for SystemC translation units");
        return std::nullopt;
    }
    if (!invocation.compatibility_switches.empty()
        && invocation.language.has_value()
        && *invocation.language != project::Language::verilog
        && *invocation.language != project::Language::system_verilog) {
        argument_error(diagnostics,
            "--compatibility is available only for Verilog/SystemVerilog");
        return std::nullopt;
    }
    if (invocation.vhdl_compatibility.has_value()
        && invocation.language.has_value()
        && *invocation.language != project::Language::vhdl) {
        argument_error(diagnostics,
            "--vhdl-compatibility is available only for VHDL");
        return std::nullopt;
    }
    if ((!invocation.include_directories.empty() || !invocation.defines.empty())
        && !source_command && !systemc_compile) {
        argument_error(diagnostics,
            "--include and --define require source compilation or checking");
        return std::nullopt;
    }
    if (invocation.default_timescale.has_value() && !source_command) {
        argument_error(diagnostics,
            "--timescale requires source compilation or checking");
        return std::nullopt;
    }
    if (invocation.time_resolution.has_value() && !elaborate) {
        argument_error(diagnostics, "--time-resolution requires elaborate");
        return std::nullopt;
    }
    if (!invocation.search_libraries.empty()
        && invocation.command != Command::compile && !elaborate) {
        argument_error(diagnostics,
            "--search-library requires compile or elaborate");
        return std::nullopt;
    }
    for (const auto& library : invocation.search_libraries) {
        if (!project::valid_library_name(library)) {
            argument_error(diagnostics,
                "--search-library requires a safe library name");
            return std::nullopt;
        }
    }
    if (!systemc_phase
        && (invocation.systemc_compiler.has_value()
            || !invocation.systemc_compile_options.empty()
            || !invocation.systemc_link_options.empty()
            || !invocation.systemc_libraries.empty())) {
        argument_error(diagnostics,
            "--compiler, --compile-option, --link-option, and --link-library "
            "require systemc compile or systemc link");
        return std::nullopt;
    }
    if (systemc_compile && (!invocation.systemc_link_options.empty()
                              || !invocation.systemc_libraries.empty())) {
        argument_error(diagnostics,
            "--link-option and --link-library require systemc link");
        return std::nullopt;
    }
    if (invocation.command == Command::systemc_link
        && !invocation.systemc_compile_options.empty()) {
        argument_error(diagnostics, "--compile-option requires systemc compile");
        return std::nullopt;
    }
    if (!invocation.vpi_modules.empty() && !elaborate && !snapshot_consumer) {
        argument_error(diagnostics,
            "--vpi requires elaborate, simulate, debug, or tcl");
        return std::nullopt;
    }
    if (!invocation.vhpi_modules.empty() && !snapshot_consumer) {
        argument_error(diagnostics, "--vhpi requires simulate, debug, or tcl");
        return std::nullopt;
    }
    if (!invocation.vhpi_registries.empty() && !snapshot_consumer) {
        argument_error(
            diagnostics, "--vhpi-registry requires simulate, debug, or tcl");
        return std::nullopt;
    }
    if (invocation.foreign_probes.has_value() && !snapshot_consumer) {
        argument_error(
            diagnostics, "--foreign-probes requires simulate, debug, or tcl");
        return std::nullopt;
    }
    if (invocation.vhpi_end_of_step_timers.has_value() && !snapshot_consumer) {
        argument_error(diagnostics,
            "--vhpi-end-of-step-timers requires simulate, debug, or tcl");
        return std::nullopt;
    }
    if (!invocation.sv_libs.empty() && !snapshot_consumer) {
        argument_error(diagnostics, "--sv-lib requires simulate, debug, or tcl");
        return std::nullopt;
    }
    if (!invocation.plusargs.empty() && !snapshot_consumer) {
        argument_error(diagnostics,
            "plusargs require simulate, debug, or tcl");
        return std::nullopt;
    }
    if (invocation.command != Command::tcl
        && invocation.command != Command::debug
        && !invocation.tcl_commands.empty()) {
        argument_error(diagnostics, "--command requires tcl or debug");
        return std::nullopt;
    }
    if (invocation.tcl_script && !invocation.tcl_commands.empty()) {
        argument_error(diagnostics,
            "a Tcl script file cannot be combined with --command");
        return std::nullopt;
    }
    if (invocation.artifact_output && !coverage_utility) {
        argument_error(diagnostics,
            "--output is available only with coverage merge or coverage report; "
            "workspace artifacts are managed by fsim");
        return std::nullopt;
    }
    if (!coverage_utility
        && (invocation.coverage_partial_merge
            || invocation.coverage_report_format.has_value()
            || !invocation.coverage_thresholds.empty())) {
        argument_error(diagnostics,
            "--partial, --format, and --threshold require coverage commands");
        return std::nullopt;
    }
    if (!invocation.help && !invocation.version && coverage_utility) {
        if (invocation.command == Command::coverage_merge) {
            if (invocation.files.empty() || !invocation.artifact_output) {
                argument_error(diagnostics,
                    "coverage merge requires input databases and --output");
                return std::nullopt;
            }
            if (invocation.coverage_report_format.has_value()
                || !invocation.coverage_thresholds.empty()) {
                argument_error(diagnostics,
                    "--format and --threshold require coverage report");
                return std::nullopt;
            }
            if (invocation.coverage_partial_merge
                && invocation.files.size() < 2U) {
                argument_error(diagnostics,
                    "coverage merge --partial requires a target and history database");
                return std::nullopt;
            }
        } else {
            if (invocation.files.size() != 1U) {
                argument_error(diagnostics,
                    "coverage report requires exactly one input database");
                return std::nullopt;
            }
            if (invocation.coverage_partial_merge) {
                argument_error(diagnostics,
                    "--partial is available only with coverage merge");
                return std::nullopt;
            }
        }
    }
    const bool has_sdf_option = !invocation.sdf_files.empty()
        || invocation.sdf_root.has_value() || invocation.sdf_cell != "*"
        || invocation.sdf_report_limit.has_value();
    if (has_sdf_option && invocation.sdf_files.empty()) {
        argument_error(diagnostics,
            "--sdf-root, --sdf-cell, and --sdf-report-limit require --sdf");
        return std::nullopt;
    }
    if (has_sdf_option && !elaborate && !snapshot_consumer) {
        argument_error(diagnostics,
            "SDF annotation requires elaboration or snapshot simulation");
        return std::nullopt;
    }
    const bool has_trace_option = invocation.trace_file.has_value()
        || invocation.trace_format.has_value()
        || invocation.trace_compression.has_value()
        || !invocation.trace_filters.empty()
        || invocation.trace_report_limit.has_value()
        || invocation.trace_enabled.has_value();
    const bool observation_phase = invocation.command == Command::compile
        || elaborate || snapshot_consumer;
    if (invocation.coverage_output.has_value() && !snapshot_consumer) {
        argument_error(diagnostics,
            "--coverage-output requires snapshot simulation");
        return std::nullopt;
    }
    if ((has_trace_option || invocation.code_coverage.has_value())
        && !observation_phase) {
        argument_error(diagnostics,
            "trace and coverage controls require compile, elaborate, "
            "or snapshot simulation");
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
    if (!snapshot_consumer
        && (invocation.duration.has_value() || invocation.max_deltas.has_value()
            || invocation.save_at.has_value() || invocation.save_file.has_value()
            || invocation.restart_file.has_value()
            || invocation.engine.has_value() || invocation.file_root.has_value()
            || invocation.compiled_processes.has_value())) {
        argument_error(diagnostics,
            "simulation options require simulate, debug, or tcl");
        return std::nullopt;
    }
    if (!elaborate && !snapshot_consumer
        && (invocation.delay_mode.has_value() || invocation.seed.has_value()
            || invocation.random_seed || invocation.optimization.has_value()
            || invocation.cache_directory.has_value())) {
        argument_error(diagnostics,
            "delay, seed, optimization, and cache options require elaborate "
            "or snapshot simulation");
        return std::nullopt;
    }
    if (invocation.jobs && !source_command && !compilation && !elaborate) {
        argument_error(diagnostics,
            "--jobs requires source compilation, checking, or elaboration");
        return std::nullopt;
    }
    if (!elaborate && (invocation.aot.has_value() || invocation.aot_scope)) {
        argument_error(diagnostics,
            "--aot, --no-aot, and --aot-scope require elaborate");
        return std::nullopt;
    }
    if (elaborate && (invocation.aot_scope || invocation.cache_directory)
        && invocation.aot != std::optional<bool> { true }) {
        argument_error(diagnostics,
            "elaborate --aot-scope and --cache require effective --aot");
        return std::nullopt;
    }
    if (snapshot_consumer && invocation.compiled_processes
        && *invocation.compiled_processes != CompiledProcessPolicy::automatic
        && invocation.engine.value_or(invocation.command == Command::debug
                   ? "debug" : "compiled") != "compiled") {
        argument_error(diagnostics,
            "--compiled-processes selected or all requires --engine compiled");
        return std::nullopt;
    }
    if (library_utility && (!invocation.files.empty()
                              || invocation.artifact_output.has_value())) {
        argument_error(diagnostics,
            "library commands accept only their mapping arguments");
        return std::nullopt;
    }
    if (invocation.artifact_output) {
        invocation.artifact_output = absolute_normalized(*invocation.artifact_output);
    }
    for (auto& sdf_file : invocation.sdf_files) {
        sdf_file = absolute_normalized(sdf_file);
    }
    if (invocation.cache_directory) {
        invocation.cache_directory = absolute_normalized(*invocation.cache_directory);
    }
    if (invocation.file_root) {
        invocation.file_root = absolute_normalized(*invocation.file_root);
    }
    return invocation;
}

std::optional<project::SourceSet> make_source_set(const Invocation& invocation,
    const project::Language language, diagnostic::Engine& diagnostics)
{
    project::SourceSet source_set;
    source_set.language = language;
    // An option the command leaves out takes the configuration's
    // `compile.<language>.option` or `compile.option` value; list options
    // add the command's values after the configured ones.
    const auto configured = [&](const std::string_view option)
        -> const std::vector<std::string>* {
        const auto* setting = invocation.configuration
            ? invocation.configuration->find("compile", language, option)
            : nullptr;
        return setting == nullptr ? nullptr : &setting->values;
    };
    const auto* configured_standard = configured("standard");
    const auto requested_standard = invocation.standard
        ? *invocation.standard
        : configured_standard != nullptr
        ? configured_standard->front()
        : std::string { project::default_standard(language) };
    const auto canonical_standard = project::canonical_standard(
        language, requested_standard);
    if (!canonical_standard) {
        argument_error(
            diagnostics,
            "unsupported standard '" + requested_standard + "' for "
                + std::string(project::to_string(language)));
        return std::nullopt;
    }
    source_set.standard = *canonical_standard;
    const bool verilog_family = language == project::Language::verilog
        || language == project::Language::system_verilog;
    if (verilog_family) {
        if (const auto* values = configured("compatibility")) {
            source_set.compatibility_switches = *values;
        }
    }
    source_set.compatibility_switches.insert(
        source_set.compatibility_switches.end(),
        invocation.compatibility_switches.begin(),
        invocation.compatibility_switches.end());
    source_set.vhdl_compatibility =
        invocation.vhdl_compatibility.value_or(std::string { });
    if (!invocation.vhdl_compatibility && language == project::Language::vhdl) {
        if (const auto* values = configured("compatibility")) {
            source_set.vhdl_compatibility = values->front();
        }
    }
    source_set.library = invocation.library;
    const auto* configured_unit = configured("compilation_unit");
    source_set.compilation_unit = invocation.compilation_unit
        ? *invocation.compilation_unit
        : configured_unit != nullptr ? configured_unit->front()
        : invocation.command == Command::compile
                && language != project::Language::vhdl
            ? "source-set"
            : "file";
    source_set.uvm_release = invocation.uvm_release.value_or(
        project::SystemVerilogUvmRelease::none);
    if (!invocation.uvm_release && language == project::Language::system_verilog) {
        if (const auto* values = configured("uvm_release")) {
            source_set.uvm_release
                = *project::parse_systemverilog_uvm_release(values->front());
        }
    }
    std::vector<std::filesystem::path> include_directories;
    if (verilog_family) {
        if (const auto* values = configured("include_directories")) {
            include_directories.assign(values->begin(), values->end());
        }
        if (const auto* values = configured("defines")) {
            source_set.defines = *values;
        }
        if (invocation.default_timescale) {
            source_set.default_timescale = *invocation.default_timescale;
        } else if (const auto* values = configured("timescale")) {
            source_set.default_timescale = values->front();
        }
    }
    include_directories.insert(include_directories.end(),
        invocation.include_directories.begin(),
        invocation.include_directories.end());
    source_set.defines.insert(source_set.defines.end(),
        invocation.defines.begin(), invocation.defines.end());
    for (const auto& directory : include_directories) {
        source_set.include_directories.push_back(absolute_normalized(directory));
    }
    return source_set;
}

void apply_invocation_options(
    const Invocation& invocation, project::Config& config)
{
    apply_overrides(invocation, config);
}

bool configuration_enabled(const Invocation& invocation)
{
    if (invocation.no_config) {
        return false;
    }
    const auto disabled = support::environment_variable("FSIM_NO_CONFIG");
    return !disabled || disabled->empty() || *disabled == "0";
}

void apply_configuration_defaults(
    const project::Configuration& configuration, Invocation& invocation)
{
    const auto values = [&](const std::string_view key)
        -> const std::vector<std::string>* {
        const auto* setting = configuration.find(key);
        return setting == nullptr ? nullptr : &setting->values;
    };
    const auto text = [&](const std::string_view key)
        -> std::optional<std::string> {
        const auto* found = values(key);
        return found == nullptr ? std::nullopt
                                : std::optional { found->front() };
    };
    const auto number = [&](const std::string_view key)
        -> std::optional<std::uint64_t> {
        const auto found = text(key);
        std::uint64_t result {};
        if (!found || !parse_unsigned(*found, result)) {
            return std::nullopt;
        }
        return result;
    };
    const auto boolean = [&](const std::string_view key) {
        const auto found = text(key);
        return found ? std::optional { *found == "true" } : std::nullopt;
    };
    // A list option keeps the configured values and adds the command's.
    const auto fill_list = [&](auto& target, const std::string_view key) {
        if (const auto* found = values(key); found != nullptr) {
            target.insert(target.begin(), found->begin(), found->end());
        }
    };
    const auto fill_seed = [&](const std::string_view key) {
        if (invocation.seed || invocation.random_seed) {
            return;
        }
        if (const auto seed = text(key)) {
            if (lowercase(*seed) == "random") {
                invocation.random_seed = true;
            } else {
                invocation.seed = number(key);
            }
        }
    };
    const auto fill_delay_mode = [&](const std::string_view key) {
        if (const auto mode = text(key); mode && !invocation.delay_mode) {
            invocation.delay_mode = project::parse_delay_mode(*mode);
        }
    };
    switch (invocation.command) {
    case Command::compile:
    case Command::check:
        if (!invocation.library_explicit) {
            if (const auto library = text("compile.library")) {
                invocation.library = *library;
            }
        }
        if (!invocation.code_coverage && invocation.command == Command::compile) {
            invocation.code_coverage = boolean("compile.code_coverage");
        }
        if (!invocation.jobs) {
            if (const auto jobs = number("compile.jobs")) {
                invocation.jobs = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    *jobs, std::numeric_limits<std::uint32_t>::max()));
            }
        }
        break;
    case Command::elaborate:
        if (!invocation.aot) {
            invocation.aot = boolean("elaborate.aot");
        }
        if (!invocation.code_coverage) {
            invocation.code_coverage = boolean("elaborate.code_coverage");
        }
        fill_delay_mode("elaborate.delay_mode");
        // Generics are keyed: the command's value of a name replaces the
        // configured one.
        if (const auto* generics = values("elaborate.generics")) {
            std::vector<std::pair<std::string, std::string>> configured;
            for (const auto& generic : *generics) {
                const auto separator = generic.find('=');
                auto name = generic.substr(0U, separator);
                const bool overridden = std::ranges::any_of(invocation.generics,
                    [&](const auto& given) { return given.first == name; });
                if (!overridden) {
                    configured.emplace_back(
                        std::move(name), generic.substr(separator + 1U));
                }
            }
            invocation.generics.insert(invocation.generics.begin(),
                configured.begin(), configured.end());
        }
        if (!invocation.jobs) {
            if (const auto jobs = number("elaborate.jobs")) {
                invocation.jobs = static_cast<std::uint32_t>(std::min<std::uint64_t>(
                    *jobs, std::numeric_limits<std::uint32_t>::max()));
            }
        }
        if (!invocation.optimization) {
            if (const auto level = text("elaborate.optimization")) {
                invocation.optimization = project::parse_optimization(*level);
            }
        }
        fill_list(invocation.search_libraries, "elaborate.search_libraries");
        fill_seed("elaborate.seed");
        fill_list(invocation.vpi_modules, "elaborate.vpi");
        break;
    case Command::simulate:
    case Command::run:
    case Command::debug:
        if (!invocation.coverage_output) {
            if (const auto path = text("simulate.coverage_output")) {
                invocation.coverage_output = std::filesystem::path { *path };
            }
        }
        fill_delay_mode("simulate.delay_mode");
        if (!invocation.duration) {
            invocation.duration = text("simulate.duration");
        }
        if (!invocation.save_at) {
            invocation.save_at = text("simulate.save_at");
        }
        if (!invocation.save_file) {
            if (const auto path = text("simulate.save_file")) {
                invocation.save_file = std::filesystem::path { *path };
            }
        }
        if (!invocation.restart_file) {
            if (const auto path = text("simulate.restart")) {
                invocation.restart_file = std::filesystem::path { *path };
            }
        }
        if (!invocation.max_deltas) {
            invocation.max_deltas = number("simulate.max_deltas");
        }
        if (!invocation.foreign_probes) {
            invocation.foreign_probes = boolean("simulate.foreign_probes");
        }
        if (!invocation.vhpi_end_of_step_timers) {
            invocation.vhpi_end_of_step_timers
                = boolean("simulate.vhpi_end_of_step_timers");
        }
        fill_seed("simulate.seed");
        fill_list(invocation.sv_libs, "simulate.sv_libs");
        if (!invocation.trace_enabled) {
            invocation.trace_enabled = boolean("simulate.trace");
        }
        if (!invocation.trace_compression) {
            if (const auto compression = text("simulate.trace_compression")) {
                invocation.trace_compression
                    = project::parse_trace_compression(*compression);
            }
        }
        fill_list(invocation.trace_filters, "simulate.trace_filters");
        if (!invocation.trace_format) {
            if (const auto format = text("simulate.trace_format")) {
                invocation.trace_format = project::parse_trace_format(*format);
            }
        }
        if (!invocation.trace_file) {
            if (const auto path = text("simulate.trace_output")) {
                invocation.trace_file = std::filesystem::path { *path };
            }
        }
        if (!invocation.trace_report_limit) {
            if (const auto limit = number("simulate.trace_report_limit")) {
                invocation.trace_report_limit = static_cast<std::size_t>(*limit);
            }
        }
        fill_list(invocation.vhpi_modules, "simulate.vhpi");
        fill_list(invocation.vhpi_registries, "simulate.vhpi_registry");
        fill_list(invocation.vpi_modules, "simulate.vpi");
        break;
    case Command::systemc_compile:
    case Command::systemc_link:
        if (!invocation.library_explicit) {
            if (const auto library = text("compile.library")) {
                invocation.library = *library;
            }
        }
        if (!invocation.systemc_compiler) {
            invocation.systemc_compiler = text("systemc.compiler");
        }
        fill_list(invocation.systemc_compile_options, "systemc.compile_options");
        fill_list(invocation.defines, "systemc.defines");
        fill_list(invocation.include_directories, "systemc.include_directories");
        fill_list(invocation.systemc_libraries, "systemc.link_libraries");
        fill_list(invocation.systemc_link_options, "systemc.link_options");
        break;
    default:
        break;
    }
}

namespace {

    // The executable's own path: the installed layout is found from it.
    std::filesystem::path executable_path(const Invocation& invocation)
    {
        std::error_code code;
        // canonical resolves the /proc/self/exe link where it exists.
        auto path = support::native_fs::canonical("/proc/self/exe", code);
        if (!code && !path.empty()) {
            return path;
        }
        path = invocation.program_path;
        if (!path.has_parent_path()) {
            // Found through PATH.
#if defined(_WIN32)
            constexpr char list_separator = ';';
#else
            constexpr char list_separator = ':';
#endif
            const auto search
                = fsim::support::environment_variable("PATH").value_or("");
            std::string_view rest { search };
            while (!rest.empty()) {
                const auto separator = rest.find(list_separator);
                const auto directory = rest.substr(0U, separator);
                const auto candidate
                    = fsim::support::path_from_utf8(directory) / path;
                if (!directory.empty()
                    && support::native_fs::is_regular_file(candidate, code)) {
                    path = candidate;
                    break;
                }
                rest = separator == std::string_view::npos
                    ? std::string_view { }
                    : rest.substr(separator + 1U);
            }
        }
        const auto resolved
            = support::native_fs::weakly_canonical(path, code);
        return code ? path : resolved;
    }

    // fsim cocotb: the integration lives in share/fsim/cocotb beside the
    // installed bin directory, or in the build tree beside the executable.
    int print_cocotb_location(const Invocation& invocation,
        std::ostream& output, diagnostic::Engine& diagnostics)
    {
        if (*invocation.cocotb_query == "libs") {
            // The cocotb GPI implementations fsim is driven through.
            output << "vpi dsim\nvhpi nvc\n";
            return kSuccess;
        }
        const auto directory = executable_path(invocation).parent_path();
        std::filesystem::path root;
        for (const auto& candidate : { directory.parent_path() / "share"
                     / "fsim" / "cocotb",
                 directory / "share" / "fsim" / "cocotb" }) {
            std::error_code code;
            if (support::native_fs::is_regular_file(
                    candidate / "Makefile.fsim", code)) {
                root = candidate;
                break;
            }
        }
        if (root.empty()) {
            diagnostics.error("FSIM-CLI-0004",
                "the cocotb integration is not installed beside "
                    + fsim::support::path_to_utf8(directory)
                    + " (share/fsim/cocotb)",
                { "<command-line>", { 1, 1, 0 }, { 1, 1, 0 } });
            return kUserError;
        }
        output << fsim::support::path_to_utf8(
                      *invocation.cocotb_query == "makefile"
                          ? root / "Makefile.fsim"
                          : root / "python")
               << '\n';
        return kSuccess;
    }

    int run_impl(
        const int argc,
        const char* const* argv,
        const Services& services,
        std::ostream& output,
        std::ostream& error,
        const bool output_is_terminal,
        const bool error_is_terminal,
        const bool builtin_stdio)
    {
        diagnostic::Engine diagnostics;
        const auto requested_format = requested_diagnostic_format(argc, argv);
        const auto requested_color = requested_color_mode(argc, argv);
        auto diagnostic_color = requested_color;
        if (!error_is_terminal) {
            const auto entry_command
                = entry_command_for_color_policy(argc, argv);
            if (entry_command == Command::tcl
                || entry_command == Command::debug) {
                diagnostic_color = diagnostic::ColorMode::never;
            }
        }
        const bool no_color = no_color_requested();
        try {
            auto invocation = parse_arguments(argc, argv, diagnostics);
            if (!invocation.has_value()) {
                print_diagnostics(error, diagnostics, requested_format,
                    diagnostic_color, error_is_terminal, no_color);
                return kUsageError;
            }
            if (!error_is_terminal
                && (invocation->command == Command::tcl
                    || invocation->command == Command::debug)) {
                diagnostic_color = diagnostic::ColorMode::never;
            }
            invocation->color_mode = diagnostic_color;
            if (invocation->version) {
                output << "fsim " << fsim::version << " (C API "
                       << FSIM_API_VERSION << ")";
#if defined(FSIM_ENABLE_ALLOCATION_PROFILING)
                output << " [allocation-profiling-build]";
#endif
                output << '\n';
                return kSuccess;
            }
            if (invocation->cocotb_query) {
                const auto status
                    = print_cocotb_location(*invocation, output, diagnostics);
                print_diagnostics(error, diagnostics, invocation->diagnostic_format,
                    invocation->color_mode, error_is_terminal, no_color);
                return status;
            }
            // The banner is for people at a console. Pipes, scripts, --quiet,
            // and --version keep their exact output.
            if (output_is_terminal && invocation->verbosity != Verbosity::quiet) {
                output
                    << "FSIM: Free mixed-language HDL simulator, version "
                    << fsim::version << "\n\n";
            }
            if (invocation->help) {
                print_help(output, invocation->program_name);
                return kSuccess;
            }
            // A console runs the scripts in its own interpreter. A debug
            // session does too, but its snapshot load needs the simulate
            // defaults first.
            if (services.load_configuration && configuration_enabled(*invocation)
                && invocation->command != Command::tcl) {
                auto configuration = std::make_shared<project::Configuration>();
                if (!services.load_configuration(
                        *invocation, *configuration, diagnostics)) {
                    print_diagnostics(error, diagnostics,
                        invocation->diagnostic_format, invocation->color_mode,
                        error_is_terminal, no_color);
                    return kUserError;
                }
                apply_configuration_defaults(*configuration, *invocation);
                invocation->configuration = std::move(configuration);
            }
            std::optional<project::Config> config;
            if (invocation->command == Command::compile
                || invocation->command == Command::check
                || invocation->command == Command::systemc_compile) {
                config = make_direct_config(*invocation, diagnostics);
            } else {
                config = make_workspace_config(*invocation, diagnostics);
            }
            if (config.has_value()) {
                // The command's options win over the configured elaboration
                // settings (elaborate.time_resolution).
                if (invocation->configuration
                    && invocation->command == Command::elaborate) {
                    invocation->configuration->apply_elaboration_settings(*config);
                }
                apply_overrides(*invocation, *config);
            }
            if (!config.has_value() || diagnostics.has_error()) {
                print_diagnostics(error, diagnostics, invocation->diagnostic_format,
                    invocation->color_mode, error_is_terminal, no_color);
                return kUserError;
            }

            const Handler* handler = select_handler(
                services, invocation->command, builtin_stdio);
            if (handler == nullptr || !*handler) {
                diagnostics.error(
                    "FSIM-CLI-0002",
                    "command '" + std::string(command_name(invocation->command)) + "' is not connected to a simulation engine in this build");
                print_diagnostics(error, diagnostics, invocation->diagnostic_format,
                    invocation->color_mode, error_is_terminal, no_color);
                return kUnavailable;
            }

            const int result = (*handler)(*invocation, *config, diagnostics, output, error);
            if (!diagnostics.empty()) {
                print_diagnostics(error, diagnostics, invocation->diagnostic_format,
                    invocation->color_mode, error_is_terminal, no_color);
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
        print_diagnostics(error, diagnostics, requested_format, diagnostic_color,
            error_is_terminal, no_color);
        return kUserError;
    }

} // namespace

int run(
    const int argc,
    const char* const* argv,
    const Services& services,
    std::ostream& output,
    std::ostream& error)
{
    // A caller-supplied stream has no portable terminal query. Treat it as
    // redirected in automatic mode; --color always remains an explicit opt-in.
    return run_impl(argc, argv, services, output, error, false, false, false);
}

int run(
    const int argc,
    const char* const* argv,
    const Services& services)
{
    return run_impl(argc, argv, services, std::cout, std::cerr,
        standard_output_is_terminal(), standard_error_is_terminal(), true);
}

} // namespace fsim::cli
