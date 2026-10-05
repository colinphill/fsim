// SPDX-License-Identifier: Apache-2.0
#include "fsim/cli/driver.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using fsim::diagnostic::ColorMode;
using fsim::diagnostic::Diagnostic;
using fsim::diagnostic::Severity;
using fsim::diagnostic::SeverityColor;

void require(const bool condition, const std::string_view message)
{
    if (!condition) {
        std::cerr << "cli_diagnostic_color_test: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::optional<fsim::cli::Invocation> parse(
    const std::initializer_list<const char*> arguments,
    fsim::diagnostic::Engine& diagnostics)
{
    std::vector<const char*> argv { "fsim" };
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    return fsim::cli::parse_arguments(
        static_cast<int>(argv.size()), argv.data(), diagnostics);
}

std::string render(
    const Severity severity,
    const bool color_enabled)
{
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.message = "sample";
    std::ostringstream output;
    fsim::diagnostic::print_text(output, diagnostic, color_enabled);
    return output.str();
}

void test_color_mode_resolution()
{
    require(!fsim::diagnostic::color_enabled(
                ColorMode::automatic, false, false),
        "auto colored a redirected stream");
    require(fsim::diagnostic::color_enabled(
                ColorMode::automatic, true, false),
        "auto did not color a terminal without NO_COLOR");
    require(!fsim::diagnostic::color_enabled(
                ColorMode::automatic, true, true),
        "auto ignored NO_COLOR");
    require(fsim::diagnostic::color_enabled(
                ColorMode::always, false, true),
        "always did not override redirection and NO_COLOR");
    require(!fsim::diagnostic::color_enabled(
                ColorMode::never, true, false),
        "never enabled color on a terminal");
}

void test_severity_color_mapping_and_plain_text()
{
    require(fsim::diagnostic::severity_color(Severity::note)
            == SeverityColor::cyan,
        "note severity is not mapped to cyan");
    require(fsim::diagnostic::severity_color(Severity::warning)
            == SeverityColor::yellow,
        "warning severity is not mapped to yellow");
    require(fsim::diagnostic::severity_color(Severity::error)
            == SeverityColor::red,
        "error severity is not mapped to red");
    require(fsim::diagnostic::severity_color(Severity::fatal)
            == SeverityColor::bold_red,
        "fatal severity is not mapped to bold red");

    require(render(Severity::note, false) == "note: sample\n",
        "plain note output changed");
    require(render(Severity::warning, false) == "warning: sample\n",
        "plain warning output changed");
    require(render(Severity::error, false) == "error: sample\n",
        "plain error output changed");
    require(render(Severity::fatal, false) == "fatal: sample\n",
        "plain fatal output changed");

    require(render(Severity::note, true)
            == "\033[36mnote\033[0m: sample\n",
        "note did not render with its severity color");
    require(render(Severity::warning, true)
            == "\033[33mwarning\033[0m: sample\n",
        "warning did not render with its severity color");
    require(render(Severity::error, true)
            == "\033[31merror\033[0m: sample\n",
        "error did not render with its severity color");
    require(render(Severity::fatal, true)
            == "\033[1;31mfatal\033[0m: sample\n",
        "fatal did not render with its severity color");

    Diagnostic diagnostic;
    diagnostic.severity = Severity::warning;
    diagnostic.message = "sample";
    diagnostic.notes.push_back({ "additional detail", { } });
    std::ostringstream output;
    fsim::diagnostic::print_text(output, diagnostic, true);
    require(output.str().find("\033[36mnote\033[0m: additional detail\n")
            != std::string::npos,
        "nested diagnostic note did not use note severity color");
}

void test_cli_color_option_and_json_bypass()
{
    fsim::diagnostic::Engine diagnostics;
    auto automatic = parse({ "tcl", "--color", "auto" }, diagnostics);
    require(automatic.has_value(), "--color auto was rejected");
    require(automatic->color_mode == ColorMode::automatic,
        "--color auto selected the wrong mode");

    diagnostics.clear();
    auto always = parse({ "debug", "--color=always" }, diagnostics);
    require(always.has_value(), "--color=always was rejected");
    require(always->color_mode == ColorMode::always,
        "--color=always selected the wrong mode");

    diagnostics.clear();
    auto never = parse({ "tcl", "--color", "never" }, diagnostics);
    require(never.has_value(), "--color never was rejected");
    require(never->color_mode == ColorMode::never,
        "--color never selected the wrong mode");

    diagnostics.clear();
    require(!parse({ "tcl", "--color", "bright" }, diagnostics),
        "an unknown --color mode was accepted");
    require(diagnostics.has_error(),
        "an unknown --color mode produced no parse diagnostic");

    const auto run_invalid = [](const std::initializer_list<const char*> args) {
        std::vector<const char*> argv { "fsim" };
        argv.insert(argv.end(), args.begin(), args.end());
        std::ostringstream output;
        std::ostringstream error;
        const int status = fsim::cli::run(
            static_cast<int>(argv.size()), argv.data(), { }, output, error);
        return std::pair { status, error.str() };
    };

    const auto always_text = run_invalid(
        { "--color", "always", "--invalid-option" });
    require(always_text.first == 2, "CLI invalid-option status changed");
    require(always_text.second.find("\033[31merror\033[0m")
            != std::string::npos,
        "--color always did not color a CLI diagnostic");

    const auto auto_text = run_invalid({ "--color", "auto", "--invalid-option" });
    require(auto_text.second.find("\033[") == std::string::npos,
        "auto colored diagnostics written to a redirected stream");

    const auto never_text = run_invalid(
        { "--color", "never", "--invalid-option" });
    require(never_text.second.find("\033[") == std::string::npos,
        "--color never emitted ANSI escapes");

    const auto always_json = run_invalid(
        { "--color", "always", "--diagnostics", "json", "--invalid-option" });
    require(always_json.second.starts_with("[{\"severity\":"),
        "JSON diagnostics lost their structured format");
    require(always_json.second.find("\033[") == std::string::npos,
        "JSON diagnostics contained ANSI escapes under --color always");
}

void test_redirected_tcl_diagnostics_stay_plain()
{
    const auto diagnostic_handler = [](
                                        const fsim::cli::Invocation&,
                                        const fsim::project::Config&,
                                        fsim::diagnostic::Engine& diagnostics,
                                        std::ostream&,
                                        std::ostream&) {
        diagnostics.error("FSIM-TCL-TEST", "redirected Tcl diagnostic");
        return 1;
    };

    for (const auto command : { "tcl", "debug" }) {
        fsim::cli::Services services;
        services.tcl = diagnostic_handler;
        services.debug = diagnostic_handler;
        std::vector<const char*> argv {
            "fsim", command, "--color", "always"
        };
        std::ostringstream output;
        std::ostringstream error;
        const int status = fsim::cli::run(
            static_cast<int>(argv.size()), argv.data(), services, output, error);

        require(status == 1, "redirected Tcl diagnostic status changed");
        require(error.str().find("error[FSIM-TCL-TEST]") != std::string::npos,
            "redirected Tcl diagnostic was not written");
        require(error.str().find("\033[") == std::string::npos,
            "--color always added ANSI escapes to redirected Tcl output");
    }
}

void test_redirected_tcl_parse_errors_stay_plain()
{
    const auto run_invalid = [](const std::initializer_list<const char*> args) {
        std::vector<const char*> argv { "fsim" };
        argv.insert(argv.end(), args.begin(), args.end());
        std::ostringstream output;
        std::ostringstream error;
        const int status = fsim::cli::run(
            static_cast<int>(argv.size()), argv.data(), { }, output, error);
        return std::pair { status, error.str() };
    };

    const auto tcl_error = run_invalid(
        { "tcl", "--invalid-option", "--color", "always" });
    require(tcl_error.first == 2, "Tcl parse-error status changed");
    require(tcl_error.second.find("\033[") == std::string::npos,
        "redirected Tcl parse error contained ANSI escapes");

    const auto debug_error = run_invalid(
        { "--color", "always", "debug", "--invalid-option" });
    require(debug_error.first == 2, "debug parse-error status changed");
    require(debug_error.second.find("\033[") == std::string::npos,
        "redirected debug parse error contained ANSI escapes");

    const auto regular_error = run_invalid(
        { "--snapshot", "tcl", "compile", "--color", "always",
            "--invalid-option" });
    require(regular_error.first == 2, "compile parse-error status changed");
    require(regular_error.second.find("\033[31merror\033[0m")
            != std::string::npos,
        "Tcl-looking option value changed ordinary CLI color behavior");
}

class ScopedStreamBuffer final {
public:
    ScopedStreamBuffer(std::ostream& stream, std::streambuf* replacement)
        : stream_(stream), previous_(stream.rdbuf(replacement))
    {
    }

    ~ScopedStreamBuffer()
    {
        stream_.rdbuf(previous_);
    }

private:
    std::ostream& stream_;
    std::streambuf* previous_;
};

void test_stdio_handlers_are_exclusive_to_no_stream_simulate()
{
    fsim::cli::Services services;
    std::vector<std::string> selected_handlers;
    std::vector<fsim::cli::Command> selected_commands;
    std::ostream* expected_output { };
    std::ostream* expected_error { };
    bool received_expected_streams { true };

    const auto make_handler = [&](const char* route) {
        return [&, route](const fsim::cli::Invocation& invocation,
                   const fsim::project::Config&,
                   fsim::diagnostic::Engine&,
                   std::ostream& output,
                   std::ostream& error) {
            selected_handlers.emplace_back(route);
            selected_commands.push_back(invocation.command);
            received_expected_streams
                = received_expected_streams
                && &output == expected_output && &error == expected_error;
            output << route << '\n';
            return 0;
        };
    };

    // These sentinels model the separate stdio and stream-injected service
    // entries without invoking any application simulation handler.
    services.simulate = make_handler("simulate-injected");
    services.stdio_run = make_handler("run-stdio");
    services.stdio_simulate = make_handler("simulate-stdio");
    services.debug = make_handler("debug-ordinary");
    services.tcl = make_handler("tcl-ordinary");

    const char* simulate_arguments[] { "fsim", "simulate" };
    const char* debug_arguments[] { "fsim", "debug" };
    const char* tcl_arguments[] { "fsim", "tcl" };

    const auto invoke_injected = [&](const char* const* arguments,
                                     const fsim::cli::Command command,
                                     const char* selected,
                                     const char* expected_output_text) {
        std::ostringstream output;
        std::ostringstream error;
        selected_handlers.clear();
        selected_commands.clear();
        received_expected_streams = true;
        expected_output = &output;
        expected_error = &error;
        const auto status = fsim::cli::run(
            2, arguments, services, output, error);
        require(status == 0 && error.str().empty(),
            "stream-injected CLI dispatch reached a service successfully");
        require(selected_handlers
                    == std::vector<std::string> { selected }
                && selected_commands
                    == std::vector<fsim::cli::Command> { command }
                && received_expected_streams
                && output.str() == expected_output_text,
            "stream-injected simulate uses its ordinary handler and caller streams");
    };

    const auto invoke_no_stream = [&](const char* const* arguments,
                                      const fsim::cli::Command command,
                                      const char* selected,
                                      const char* expected_output_text) {
        std::ostringstream captured_output;
        const ScopedStreamBuffer capture {
            std::cout, captured_output.rdbuf() };
        selected_handlers.clear();
        selected_commands.clear();
        received_expected_streams = true;
        expected_output = &std::cout;
        expected_error = &std::cerr;
        const auto status = fsim::cli::run(2, arguments, services);
        require(status == 0, "no-stream CLI dispatch reaches a service successfully");
        require(selected_handlers
                    == std::vector<std::string> { selected }
                && selected_commands
                    == std::vector<fsim::cli::Command> { command }
                && received_expected_streams
                && captured_output.str() == expected_output_text,
            "no-stream CLI dispatch uses its selected handler and builtin stdout");
    };

    invoke_injected(simulate_arguments, fsim::cli::Command::simulate,
        "simulate-injected", "simulate-injected\n");
    invoke_no_stream(simulate_arguments, fsim::cli::Command::simulate,
        "simulate-stdio", "simulate-stdio\n");
    invoke_no_stream(debug_arguments, fsim::cli::Command::debug,
        "debug-ordinary", "debug-ordinary\n");
    invoke_no_stream(tcl_arguments, fsim::cli::Command::tcl,
        "tcl-ordinary", "tcl-ordinary\n");

    services.stdio_run = { };
    services.stdio_simulate = { };
    invoke_no_stream(simulate_arguments, fsim::cli::Command::simulate,
        "simulate-injected", "simulate-injected\n");
}

} // namespace

int main()
{
    test_color_mode_resolution();
    test_severity_color_mapping_and_plain_text();
    test_cli_color_option_and_json_bypass();
    test_redirected_tcl_diagnostics_stay_plain();
    test_redirected_tcl_parse_errors_stay_plain();
    test_stdio_handlers_are_exclusive_to_no_stream_simulate();
    return EXIT_SUCCESS;
}
