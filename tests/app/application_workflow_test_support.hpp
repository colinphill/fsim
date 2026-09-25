// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Language and artifact fixtures exercise the internal phases directly. The
// public CLI accepts workspace operations only; these declarations are confined
// to tests and do not add a compatibility route to the executable.
namespace fsim::app::application_detail {

int handle_check(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_build(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_run(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_debug(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::istream&, std::ostream&, std::ostream&);
int handle_compile(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_elaborate(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_simulate(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_systemc_compile(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);
int handle_systemc_link(const cli::Invocation&, const project::Config&,
    diagnostic::Engine&, std::ostream&, std::ostream&);

} // namespace fsim::app::application_detail

namespace fsim::test {

inline cli::Services make_fixture_services(std::istream& input = std::cin)
{
    auto services = app::make_cli_services(input);
    const auto workspace_services = services;
    services.check = app::application_detail::handle_check;
    services.build = app::application_detail::handle_build;
    services.run = app::application_detail::handle_run;
    services.debug = [workspace_services](const cli::Invocation& invocation,
                         const project::Config& config,
                         diagnostic::Engine& diagnostics,
                         std::ostream& output, std::ostream& error) {
        // Snapshot publication requires compiled-object provenance. Prepare the
        // fixture through the managed phases before exercising snapshot debug.
        auto phase = invocation;
        phase.verbosity = cli::Verbosity::quiet;
        for (const auto& sources : config.source_sets) {
            auto source_config = config;
            source_config.source_sets = { sources };
            phase.files = sources.files;
            phase.library = sources.library;
            const auto systemc = sources.language == project::Language::systemc;
            phase.command = systemc ? cli::Command::systemc_compile : cli::Command::compile;
            const auto& compile = systemc ? workspace_services.systemc_compile
                                         : workspace_services.compile;
            const auto status = compile(phase, source_config, diagnostics, output, error);
            if (status != 0)
                return status;
        }
        phase.command = cli::Command::elaborate;
        phase.files.clear();
        auto snapshot_config = config;
        snapshot_config.source_sets.clear();
        const auto status = workspace_services.elaborate(
            phase, snapshot_config, diagnostics, output, error);
        if (status != 0)
            return status;
        return workspace_services.debug(invocation, config, diagnostics, output, error);
    };
    services.compile = app::application_detail::handle_compile;
    services.elaborate = app::application_detail::handle_elaborate;
    services.simulate = app::application_detail::handle_simulate;
    services.systemc_compile = app::application_detail::handle_systemc_compile;
    services.systemc_link = app::application_detail::handle_systemc_link;
    return services;
}

namespace workflow_detail {

    inline void apply_fixture_overrides(
        const cli::Invocation& invocation, project::Config& config)
    {
        if (!invocation.tops.empty()) {
            config.project.tops = invocation.tops;
            config.project.top = invocation.top.value_or("");
        }
        if (invocation.duration)
            config.run.duration = invocation.duration;
        if (invocation.max_deltas)
            config.run.max_deltas = *invocation.max_deltas;
        if (invocation.delay_mode)
            config.run.delay_mode = *invocation.delay_mode;
        if (invocation.trace_file)
            config.run.trace_file = std::filesystem::absolute(*invocation.trace_file);
        if (invocation.trace_format)
            config.run.trace_format = *invocation.trace_format;
        if (invocation.trace_compression)
            config.run.trace_compression = *invocation.trace_compression;
        if (!invocation.trace_filters.empty())
            config.run.trace_filters = invocation.trace_filters;
        if (invocation.trace_report_limit)
            config.run.trace_report_limit = *invocation.trace_report_limit;
        if (invocation.trace_enabled)
            config.run.trace_enabled = *invocation.trace_enabled;
        if (invocation.seed) {
            config.project.seed = *invocation.seed;
            config.project.random_seed = false;
        } else if (invocation.random_seed)
            config.project.random_seed = true;
        if (invocation.jobs)
            config.build.jobs = *invocation.jobs;
        if (invocation.optimization)
            config.build.optimization = *invocation.optimization;
        if (invocation.code_coverage)
            config.coverage.enabled = *invocation.code_coverage;
        if (!invocation.search_libraries.empty())
            config.elaboration.search_libraries = invocation.search_libraries;
        if (invocation.systemc_compiler)
            config.systemc.compiler = *invocation.systemc_compiler;
        if (!invocation.include_directories.empty())
            config.systemc.include_directories = invocation.include_directories;
        if (!invocation.defines.empty())
            config.systemc.defines = invocation.defines;
        if (!invocation.systemc_compile_options.empty())
            config.systemc.compile_options = invocation.systemc_compile_options;
        if (!invocation.systemc_link_options.empty())
            config.systemc.link_options = invocation.systemc_link_options;
        if (!invocation.systemc_libraries.empty())
            config.systemc.libraries = invocation.systemc_libraries;
    }

    inline const cli::Handler& handler(
        const cli::Services& services, cli::Command command)
    {
        switch (command) {
        case cli::Command::check: return services.check;
        case cli::Command::build: return services.build;
        case cli::Command::run: return services.run;
        case cli::Command::debug: return services.debug;
        case cli::Command::compile: return services.compile;
        case cli::Command::elaborate: return services.elaborate;
        case cli::Command::simulate: return services.simulate;
        case cli::Command::systemc_compile: return services.systemc_compile;
        case cli::Command::systemc_link: return services.systemc_link;
        default: return services.tcl;
        }
    }

    inline bool takes_value(std::string_view option)
    {
        constexpr std::array options {
            "--top", "--lang", "--standard", "--compatibility",
            "--uvm-release", "--compilation-unit", "--library",
            "--search-library", "--compiler", "--compile-option",
            "--link-option", "--link-library", "--cache", "--aot-scope",
            "--compiled-processes", "--file-root", "--include", "-I",
            "--define", "-D", "--optimization", "-O", "--jobs", "-j",
            "--format", "--threshold", "--duration", "--max-deltas",
            "--delay-mode", "--sdf", "--sdf-root", "--sdf-cell",
            "--sdf-report-limit", "--trace", "--trace-output", "--trace-format",
            "--trace-compression", "--trace-filter", "--trace-select",
            "--trace-lifecycle", "--trace-report-limit", "--engine",
            "--seed", "--diagnostics", "--command", "-c", "--snapshot",
            "--verbosity"
        };
        return std::ranges::find(options, option) != options.end();
    }

    inline bool source_option(std::string_view option)
    {
        constexpr std::array options {
            "--lang", "--standard", "--compatibility", "--uvm-release",
            "--compilation-unit", "--library", "--include", "-I",
            "--define", "-D", "--jobs", "-j"
        };
        return std::ranges::find(options, option) != options.end()
            || (option.size() > 2U
                && (option.starts_with("-I") || option.starts_with("-D")
                    || option.starts_with("-j")));
    }

    inline std::vector<const char*> pointers(const std::vector<std::string>& values)
    {
        std::vector<const char*> result;
        for (const auto& value : values)
            result.push_back(value.c_str());
        return result;
    }

} // namespace workflow_detail

// Existing fixtures describe explicit artifact identities to probe corruption,
// relocation, and source-hidden execution. Interpret those fixture selectors
// here, normalize the remaining options with the real workspace CLI, and call
// the phase handler directly. Public CLI behavior is tested separately.
inline int run_fixture_command(int argc, const char* const* argv,
    const cli::Services& services, std::ostream& output, std::ostream& error)
{
    if (argc < 2 || !argv || !argv[0] || !argv[1])
        return cli::run(argc, argv, services, output, error);
    const std::string_view requested { argv[1] };
    const bool source_workflow = requested == "run" || requested == "build"
        || requested == "debug";
    const auto command = requested == "run" ? "simulate"
        : requested == "build" ? "elaborate" : argv[1];
    std::vector<std::string> normalized { argv[0], command };
    std::vector<std::string> source_arguments { argv[0], "check" };
    std::vector<std::filesystem::path> source_files;
    std::optional<std::filesystem::path> manifest;
    std::optional<std::filesystem::path> destination;
    std::optional<std::filesystem::path> design;
    std::vector<std::filesystem::path> objects;
    std::vector<std::filesystem::path> plugins;
    bool explicit_top = false;
    for (int index = 2; index < argc; ++index) {
        if (!argv[index])
            return 2;
        const std::string_view argument { argv[index] };
        const auto separator = argument.find('=');
        const auto option = argument.substr(0U, separator);
        const bool selector = option == "-p" || option == "--project"
            || option == "-o" || option == "--output"
            || option == "--object" || option == "--design"
            || option == "--systemc-plugin";
        if (selector) {
            std::string_view value;
            if (separator != std::string_view::npos)
                value = argument.substr(separator + 1U);
            else if (index + 1 < argc && argv[index + 1])
                value = argv[++index];
            else
                return 2;
            const auto path = std::filesystem::absolute(support::path_from_utf8(value));
            if (option == "-p" || option == "--project")
                manifest = path;
            else if (option == "-o" || option == "--output")
                destination = path;
            else if (option == "--object")
                objects.push_back(path);
            else if (option == "--design")
                design = path;
            else
                plugins.push_back(path);
            continue;
        }
        if (option == "--top")
            explicit_top = true;
        auto& target = source_workflow && workflow_detail::source_option(option)
            ? source_arguments : normalized;
        if (source_workflow && !argument.empty()
            && argument.front() != '-' && argument.front() != '+') {
            source_arguments.emplace_back(argument);
            source_files.push_back(support::path_from_utf8(argument));
            continue;
        }
        target.emplace_back(argument);
        if (separator == std::string_view::npos
            && workflow_detail::takes_value(option)) {
            if (index + 1 >= argc || !argv[index + 1])
                return 2;
            target.emplace_back(argv[++index]);
        }
    }
    if (requested == "build" && !explicit_top)
        normalized.emplace_back("fixture_top");
    std::optional<cli::Invocation> invocation;
    std::optional<project::Config> config;
    const cli::Handler capture = [&](const cli::Invocation& captured,
                                     const project::Config& captured_config,
                                     diagnostic::Engine&, std::ostream&,
                                     std::ostream&) {
        invocation = captured;
        config = captured_config;
        return 0;
    };
    cli::Services capture_services;
    capture_services.check = capture;
    capture_services.compile = capture;
    capture_services.elaborate = capture;
    capture_services.simulate = capture;
    capture_services.debug = capture;
    capture_services.systemc_compile = capture;
    capture_services.systemc_link = capture;
    auto raw = workflow_detail::pointers(normalized);
    const auto parsed = cli::run(static_cast<int>(raw.size()), raw.data(),
        capture_services, output, error);
    if (parsed != 0 || !invocation || !config)
        return parsed;
    if (requested == "run")
        invocation->command = cli::Command::run;
    else if (requested == "build") {
        invocation->command = cli::Command::build;
        if (!explicit_top) {
            invocation->top.reset();
            invocation->tops.clear();
        }
    }
    diagnostic::Engine diagnostics;
    if (manifest) {
        config = project::load(*manifest, diagnostics);
        invocation->manifest = *manifest;
        invocation->manifest_explicit = true;
    } else if (!source_files.empty()) {
        auto original_invocation = *invocation;
        raw = workflow_detail::pointers(source_arguments);
        const auto source_status = cli::run(static_cast<int>(raw.size()),
            raw.data(), capture_services, output, error);
        if (source_status != 0)
            return source_status;
        original_invocation.files = source_files;
        invocation = std::move(original_invocation);
    }
    if (!config || diagnostics.has_error()) {
        diagnostic::print_text(error, diagnostics);
        return 1;
    }
    invocation->artifact_output = destination;
    invocation->objects = std::move(objects);
    invocation->systemc_plugins = std::move(plugins);
    invocation->design = design;
    workflow_detail::apply_fixture_overrides(*invocation, *config);
    const auto& handler = workflow_detail::handler(services, invocation->command);
    if (!handler)
        return 3;
    int result = 1;
    try {
        result = handler(*invocation, *config, diagnostics, output, error);
    } catch (const std::exception& exception) {
        diagnostics.error("FSIM-CLI-0003",
            std::string { "unhandled fixture command failure: " } + exception.what());
    }
    if (!diagnostics.empty()) {
        if (invocation->diagnostic_format == cli::DiagnosticFormat::json)
            diagnostic::print_json(error, diagnostics);
        else
            diagnostic::print_text(error, diagnostics);
    }
    return result;
}

} // namespace fsim::test
