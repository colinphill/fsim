// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "application_workflow_test_support.hpp"
#include "path_test_support.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

class ScopedStdoutCapture final {
public:
    ScopedStdoutCapture()
        : output_ { std::tmpfile() }
    {
        assert(output_ != nullptr);
        std::cout.flush();
        const auto flush_status = std::fflush(stdout);
        assert(flush_status == 0);
#if defined(_WIN32)
        descriptor_ = ::_fileno(stdout);
        saved_descriptor_ = ::_dup(descriptor_);
        assert(saved_descriptor_ >= 0);
        const auto redirect_status
            = ::_dup2(::_fileno(output_), descriptor_);
        assert(redirect_status == 0);
#else
        descriptor_ = ::fileno(stdout);
        saved_descriptor_ = ::dup(descriptor_);
        assert(saved_descriptor_ >= 0);
        const auto redirect_status
            = ::dup2(::fileno(output_), descriptor_);
        assert(redirect_status >= 0);
#endif
    }

    ScopedStdoutCapture(const ScopedStdoutCapture&) = delete;
    ScopedStdoutCapture& operator=(const ScopedStdoutCapture&) = delete;

    ~ScopedStdoutCapture()
    {
        std::cout.flush();
        const auto flush_status = std::fflush(stdout);
        assert(flush_status == 0);
#if defined(_WIN32)
        (void)::_dup2(saved_descriptor_, descriptor_);
        (void)::_close(saved_descriptor_);
#else
        (void)::dup2(saved_descriptor_, descriptor_);
        (void)::close(saved_descriptor_);
#endif
        std::fclose(output_);
    }

    [[nodiscard]] std::string contents()
    {
        std::cout.flush();
        const auto stdout_flush_status = std::fflush(stdout);
        const auto output_flush_status = std::fflush(output_);
        const auto seek_status = std::fseek(output_, 0, SEEK_SET);
        assert(stdout_flush_status == 0);
        assert(output_flush_status == 0);
        assert(seek_status == 0);
        std::string result;
        char buffer[256];
        while (const auto count = std::fread(buffer, 1U, sizeof(buffer), output_)) {
            result.append(buffer, count);
        }
        return result;
    }

private:
    FILE* output_ { };
    int descriptor_ { -1 };
    int saved_descriptor_ { -1 };
};

std::string normalize_line_endings(std::string text)
{
    for (std::size_t index = 0U; index + 1U < text.size();) {
        if (text[index] == '\r' && text[index + 1U] == '\n') {
            text.erase(index, 1U);
        } else {
            ++index;
        }
    }
    return text;
}

struct OutputEvent {
    fsim::runtime::simir::ProcessId process { };
    std::string text;
    bool newline { };
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const OutputEvent&, const OutputEvent&) = default;
};

struct ReportEvent {
    fsim::runtime::simir::ProcessId process { };
    std::string message;
    fsim::runtime::simir::AssertionSeverity severity {
        fsim::runtime::simir::AssertionSeverity::note
    };
    fsim::runtime::simir::SourceLocation source;
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const ReportEvent&, const ReportEvent&) = default;
};

struct Capture {
    fsim::runtime::RunResult result;
    std::vector<OutputEvent> output;
    std::vector<ReportEvent> reports;
    std::size_t compiled_processes { };
    fsim::app::NativeCacheStatistics cache;
};

struct FailureCapture {
    std::vector<ReportEvent> reports;
    std::string message;
    fsim::runtime::simir::AssertionSeverity severity {
        fsim::runtime::simir::AssertionSeverity::note
    };
    fsim::runtime::simir::SourceLocation source;
    std::size_t compiled_processes { };
    bool failed { };
};

int run_cli(
    const std::vector<std::string>& arguments,
    std::istream& input,
    std::ostream& output,
    std::ostream& error)
{
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments) {
        raw.push_back(argument.c_str());
    }
    return fsim::test::run_fixture_command(
        static_cast<int>(raw.size()),
        raw.data(),
        fsim::test::make_fixture_services(input),
        output,
        error);
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation {
        std::move(project), 1000, engine
    };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.cache = simulation.native_cache_statistics();
    simulation.set_output_hook(
        [&capture](
            const fsim::runtime::simir::ProcessId process,
            const std::string_view text,
            const bool newline,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            capture.output.push_back(
                { process, std::string { text }, newline, time, delta });
        });
    simulation.set_report_hook(
        [&capture](
            const fsim::runtime::simir::ProcessId process,
            const std::string_view message,
            const fsim::runtime::simir::AssertionSeverity severity,
            const fsim::runtime::simir::SourceLocation& source,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            capture.reports.push_back(
                { process,
                    std::string { message },
                    severity,
                    source,
                    time,
                    delta });
        });
    capture.result = simulation.run();
    return capture;
}

struct HookReplacementCapture {
    fsim::runtime::RunResult result;
    std::vector<OutputEvent> output;
    std::vector<std::array<std::string, 2U>> observed_signal_values;
};

HookReplacementCapture execute_after_builtin_hook_replacement(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation {
        std::move(project), 1000, engine
    };
    const auto marker = simulation.find_signal("stdio_marker_test.marker");
    const auto sideband = simulation.find_signal("stdio_marker_test.sideband");
    assert(marker && sideband);

    HookReplacementCapture capture;
    simulation.set_builtin_stdout_output();
    simulation.set_output_hook(
        [&capture, &simulation, marker_signal = *marker,
            sideband_signal = *sideband](
            const fsim::runtime::simir::ProcessId process,
            const std::string_view text,
            const bool newline,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            capture.output.push_back(
                { process, std::string { text }, newline, time, delta });
            capture.observed_signal_values.push_back({
                simulation.read_signal_snapshot(marker_signal).to_msb_string(),
                simulation.read_signal_snapshot(sideband_signal).to_msb_string()
            });
        });
    capture.result = simulation.run();
    return capture;
}

int run_streamed_application_cli(
    const std::vector<std::string>& arguments,
    std::istream& input,
    std::ostream& output,
    std::ostream& error)
{
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments) {
        raw.push_back(argument.c_str());
    }
    return fsim::cli::run(
        static_cast<int>(raw.size()),
        raw.data(),
        fsim::app::make_cli_services(input),
        output,
        error);
}

int run_stdio_application_cli(const std::vector<std::string>& arguments)
{
    std::vector<const char*> raw;
    raw.reserve(arguments.size());
    for (const auto& argument : arguments) {
        raw.push_back(argument.c_str());
    }
    return fsim::cli::run(
        static_cast<int>(raw.size()),
        raw.data(),
        fsim::app::make_stdio_cli_services());
}

class ScopedCurrentDirectory final {
public:
    explicit ScopedCurrentDirectory(const std::filesystem::path& path)
        : previous_ { std::filesystem::current_path() }
    {
        std::filesystem::current_path(path);
    }

    ScopedCurrentDirectory(const ScopedCurrentDirectory&) = delete;
    ScopedCurrentDirectory& operator=(const ScopedCurrentDirectory&) = delete;

    ~ScopedCurrentDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }

private:
    std::filesystem::path previous_;
};

void test_stdio_marker_and_builtin_hook_replacement(
    const std::filesystem::path& directory)
{
    const auto workspace = directory / "stdio-marker-workspace";
    const auto workspace_created = std::filesystem::create_directory(workspace);
    assert(workspace_created);
    const auto source = workspace / "stdio_marker_test.sv";
    {
        std::ofstream output(source);
        output << R"(
module stdio_marker_test;
  timeunit 1ns;
  timeprecision 1ps;
  logic [3:0] marker;
  logic [1:0] sideband;
  initial begin
    marker = 4'b10xz;
    sideband = 2'b01;
    $printtimescale;
    $display("before=%b/%b", marker, sideband);
    #1 marker = 4'bz01x;
    sideband = 2'b1x;
    $display("after=%b/%b", marker, sideband);
    $finish;
  end
endmodule
)";
    }

    ScopedCurrentDirectory current_directory { workspace };

    std::istringstream input;
    std::ostringstream ordinary_output;
    std::ostringstream ordinary_error;
    const auto compile_status = run_streamed_application_cli(
        { "fsim", "compile", "--library", "work", "stdio_marker_test.sv" },
        input,
        ordinary_output,
        ordinary_error);
    assert(compile_status == 0);
    assert(ordinary_error.str().empty());
    const auto elaborate_status = run_streamed_application_cli(
        { "fsim", "elaborate", "work.stdio_marker_test", "--snapshot", "default" },
        input,
        ordinary_output,
        ordinary_error);
    assert(elaborate_status == 0);
    assert(ordinary_error.str().empty());

    const std::vector<std::string> arguments { "fsim", "simulate" };
    ordinary_output.str({ });
    ordinary_output.clear();
    const auto ordinary_status = run_streamed_application_cli(
        arguments, input, ordinary_output, ordinary_error);
    assert(ordinary_status == 0);
    assert(ordinary_error.str().empty());
    const auto ordinary_transcript = ordinary_output.str();
    assert(ordinary_transcript.find(
        "Time scale of (stdio_marker_test) is 1ns / 1ps\n")
        != std::string::npos);
    assert(ordinary_transcript.find("before=10xz/01\nafter=z01x/1x\n")
        != std::string::npos);
    assert(ordinary_transcript.find('\x1f') == std::string::npos);

    int stdio_status { -1 };
    std::string stdio_transcript;
    {
        ScopedStdoutCapture capture;
        stdio_status = run_stdio_application_cli(arguments);
        std::cout.flush();
        std::fflush(stdout);
        stdio_transcript = normalize_line_endings(capture.contents());
    }
    assert(stdio_status == 0);
    assert(stdio_transcript == ordinary_transcript);
    assert(stdio_transcript.find(
        "Time scale of (stdio_marker_test) is 1ns / 1ps\n")
        != std::string::npos);
    assert(stdio_transcript.find("before=10xz/01\nafter=z01x/1x\n")
        != std::string::npos);
    assert(stdio_transcript.find('\x1f') == std::string::npos);

    fsim::project::Config config;
    config.base_directory = workspace;
    config.project.name = "stdio-marker-test";
    config.project.top = "sv:work.stdio_marker_test";
    config.project.time_resolution = "auto";
    config.build.optimization = fsim::project::Optimization::o2;
    config.build.cache_path = workspace / ".fsim" / "cache";
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto replacement_project = fsim::app::build_project(config, diagnostics);
    assert(reference_project && replacement_project);
    const auto reference = execute(
        std::move(*reference_project), fsim::app::SimulationEngine::interpreter);
    HookReplacementCapture replacement;
    std::string escaped_stdout;
    {
        ScopedStdoutCapture capture;
        replacement = execute_after_builtin_hook_replacement(
            std::move(*replacement_project), fsim::app::SimulationEngine::compiled);
        escaped_stdout = capture.contents();
    }
    assert(escaped_stdout.empty());
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(replacement.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.time == 1000);
    assert(replacement.result.time == 1000);
    assert(reference.output == replacement.output);
    assert(replacement.output.size() == 5U);
    assert(replacement.output[0].text
        == "Time scale of (stdio_marker_test) is 1ns / 1ps");
    assert(replacement.output[1].text == "before=10xz");
    assert(replacement.output[2].text == "/01");
    assert(replacement.output[3].text == "after=z01x");
    assert(replacement.output[4].text == "/1x");
    const std::array<fsim::runtime::SimulationTick, 5U> expected_times {
        0U, 0U, 0U, 1000U, 1000U
    };
    const std::array<std::uint64_t, 5U> expected_deltas {
        0U, 0U, 0U, 0U, 0U
    };
    const std::array<bool, 5U> expected_newlines {
        true, false, true, false, true
    };
    for (std::size_t index = 0U; index < replacement.output.size(); ++index) {
        assert(replacement.output[index].time == expected_times[index]);
        assert(replacement.output[index].delta == expected_deltas[index]);
        assert(replacement.output[index].newline == expected_newlines[index]);
    }
    const std::vector<std::array<std::string, 2U>> expected_states {
        { "10XZ", "01" },
        { "10XZ", "01" },
        { "10XZ", "01" },
        { "Z01X", "1X" },
        { "Z01X", "1X" }
    };
    assert(replacement.observed_signal_values == expected_states);
}

FailureCapture execute_failure(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation {
        std::move(project), 1000, engine
    };
    FailureCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    simulation.set_report_hook(
        [&capture](
            const fsim::runtime::simir::ProcessId process,
            const std::string_view message,
            const fsim::runtime::simir::AssertionSeverity severity,
            const fsim::runtime::simir::SourceLocation& source,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            capture.reports.push_back(
                { process,
                    std::string { message },
                    severity,
                    source,
                    time,
                    delta });
        });
    try {
        static_cast<void>(simulation.run());
    } catch (const fsim::runtime::simir::AssertionError& error) {
        capture.failed = true;
        capture.message = error.what();
        capture.severity = error.severity();
        capture.source = error.source();
    }
    return capture;
}

void test_display(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "display-test";
    config.project.top = "sv:work.display_test";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "cache-o0"
                : "cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(reference_project && compiled_project);

    const auto reference = execute(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = execute(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(compiled.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.time == 4);
    assert(compiled.result.time == 4);
    assert(reference.output == compiled.output);
    assert(reference.output.size() == 48);
    assert(reference.output[0].process == 0);
    assert(reference.output[0].text == "first\t");
    assert(!reference.output[0].newline);
    assert(reference.output[0].time == 0);
    assert(reference.output[0].delta == 0);
    assert(reference.output[1].text == "+line\nembedded \"quote\" \\ A");
    assert(reference.output[1].newline);
    assert(reference.output[1].time == 0);
    assert(reference.output[2].text == " 42");
    assert(reference.output[2].newline);
    assert(reference.output[2].time == 0);
    assert(reference.output[2].delta == 0);
    assert(reference.output[3].text == "  -1");
    assert(reference.output[3].newline);
    assert(reference.output[3].time == 0);
    assert(reference.output[3].delta == 0);
    assert(reference.output[4].text == "q=%:10xz!");
    assert(reference.output[4].newline);
    assert(reference.output[4].time == 0);
    assert(reference.output[4].delta == 0);
    assert(reference.output[5].text == "[10xz]");
    assert(!reference.output[5].newline);
    assert(reference.output[5].time == 0);
    assert(reference.output[5].delta == 0);
    assert(reference.output[6].text == "h=x");
    assert(reference.output[6].newline);
    assert(reference.output[6].time == 0);
    assert(reference.output[7].text == "o=245");
    assert(reference.output[7].newline);
    assert(reference.output[7].time == 0);
    assert(reference.output[8].text == "d=165");
    assert(reference.output[8].newline);
    assert(reference.output[8].time == 0);
    assert(reference.output[9].text == "s=  -1");
    assert(reference.output[9].newline);
    assert(reference.output[9].time == 0);
    assert(reference.output[10].text == "u= x");
    assert(reference.output[10].newline);
    assert(reference.output[10].time == 0);
    assert(reference.output[11].text == "c=A");
    assert(reference.output[11].newline);
    assert(reference.output[11].time == 0);
    assert(reference.output[12].text == "text=test");
    assert(reference.output[12].newline);
    assert(reference.output[12].time == 0);
    assert(reference.output[13].text == "compact=a5");
    assert(reference.output[13].newline);
    assert(reference.output[13].time == 0);
    assert(reference.output[14].text == "upper=a5");
    assert(reference.output[14].newline);
    assert(reference.output[14].time == 0);
    assert(reference.output[15].text == "width=    a5");
    assert(reference.output[15].newline);
    assert(reference.output[15].time == 0);
    assert(reference.output[16].text == "left=a5    !");
    assert(reference.output[17].text == "zero=-00001");
    assert(reference.output[18].text == "multi=0011");
    assert(!reference.output[18].newline);
    assert(reference.output[19].text == "/a5");
    assert(!reference.output[19].newline);
    assert(reference.output[20].text == " tail=  -1");
    assert(reference.output[20].newline);
    assert(reference.output[21].text == " 3");
    assert(!reference.output[21].newline);
    assert(reference.output[22].text == "165");
    assert(reference.output[22].newline);
    assert(reference.output[23].text == "scope=display_test");
    assert(!reference.output[23].newline);
    assert(reference.output[24].text == " q=0011");
    assert(reference.output[24].newline);
    assert(reference.output[25].text == "post=1110 compact-time=0");
    assert(reference.output[26].text == "1110");
    assert(reference.output[27].text == "a5");
    assert(reference.output[28].text == "245");
    assert(reference.output[29].text == "mon=1110 t=                   0");
    assert(reference.output[29].time == 0);
    assert(reference.output[30].text == "mon=0101 t=                   1");
    assert(reference.output[30].time == 1);
    assert(reference.output[31].text == "mon=0111 t=                   3");
    assert(reference.output[31].time == 3);
    assert(reference.output[32].text == "time=0004");
    assert(reference.output[32].time == 4);
    assert(reference.output[33].text == " 18446744073709551616");
    assert(reference.output[33].newline);
    assert(reference.output[33].time == 4);
    assert(reference.output[34].text
        == std::string(58U, ' ') + "18446744073709551616");
    assert(reference.output[34].newline);
    assert(reference.output[34].time == 4);
    assert(reference.output[35].text == "-18446744073709551616");
    assert(reference.output[35].newline);
    assert(reference.output[35].time == 4);
    assert(reference.output[36].text == std::string(1233U, ' ') + "1");
    assert(reference.output[36].newline);
    assert(reference.output[36].time == 4);
    assert(reference.output[37].text == "0111");
    assert(reference.output[37].newline);
    assert(reference.output[38].text == "a5");
    assert(reference.output[38].newline);
    assert(reference.output[39].text == "245");
    assert(reference.output[39].newline);
    assert(reference.output[40].text == "0111");
    assert(!reference.output[40].newline);
    assert(reference.output[41].text == "a5");
    assert(!reference.output[41].newline);
    assert(reference.output[42].text == "245");
    assert(!reference.output[42].newline);
    assert(reference.output[43].text.empty());
    assert(reference.output[43].newline);
    assert(reference.output[44].text == "second");
    assert(!reference.output[44].newline);
    assert(reference.output[44].time == 4);
    assert(reference.output[45].text.empty());
    assert(!reference.output[45].newline);
    assert(reference.output[45].time == 4);
    assert(reference.output[46].text.empty());
    assert(reference.output[46].newline);
    assert(reference.output[46].time == 4);
    assert(reference.output[47].text == "sformat-time=4");
    assert(reference.output[47].newline);
    assert(reference.output[47].time == 4);
    assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 2);
#else
    assert(compiled.compiled_processes == 0);
#endif
}

void test_monitor_radix(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "monitor-radix-test";
    config.project.top = "sv:work.monitor_radix_test";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "monitor-cache-o0"
                : "monitor-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(reference_project && compiled_project);
    const auto reference = execute(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = execute(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.time == 3);
    assert(reference.output == compiled.output);
    assert(reference.output.size() == 3);
    assert(reference.output[0].text == "10xz");
    assert(reference.output[0].newline && reference.output[0].time == 0);
    assert(reference.output[1].text == "a5");
    assert(reference.output[1].newline && reference.output[1].time == 1);
    assert(reference.output[2].text == "245");
    assert(reference.output[2].newline && reference.output[2].time == 2);
}

void test_vhdl_report(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "report-test";
    config.project.top = "vhdl:work.reporter(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "report-cache-o0"
                : "report-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    auto warm_project = fsim::app::build_project(config, diagnostics);
    assert(reference_project && compiled_project && warm_project);
    const auto reference = execute(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = execute(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    const auto warm = execute(
        std::move(*warm_project),
        fsim::app::SimulationEngine::compiled);
    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(compiled.result.status == fsim::runtime::RunStatus::completed);
    assert(warm.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.output == compiled.output);
    assert(reference.output == warm.output);
    assert(reference.output.empty());
    assert(reference.reports == compiled.reports);
    assert(reference.reports == warm.reports);
    assert(reference.reports.size() == 6);
    assert(reference.reports[0].message == "vhdl \"quote\"");
    assert(
        reference.reports[0].severity
        == fsim::runtime::simir::AssertionSeverity::note);
    assert(fsim::test::same_source_path(
        reference.reports[0].source.path, source));
    assert(reference.reports[0].source.line == 10);
    assert(reference.reports[0].time == 0);
    assert(reference.reports[0].delta == 0);
    assert(reference.reports[1].message.empty());
    assert(
        reference.reports[1].severity
        == fsim::runtime::simir::AssertionSeverity::warning);
    assert(
        reference.reports[2].severity
        == fsim::runtime::simir::AssertionSeverity::error);
    assert(reference.reports[3].message == "dynamic report");
    assert(
        reference.reports[3].severity
        == fsim::runtime::simir::AssertionSeverity::warning);
    assert(fsim::test::same_source_path(
        reference.reports[3].source.path, source));
    assert(reference.reports[3].source.line == 13);
    assert(reference.reports[4].message == "dynamic assertion");
    assert(
        reference.reports[4].severity
        == fsim::runtime::simir::AssertionSeverity::warning);
    assert(fsim::test::same_source_path(
        reference.reports[4].source.path, source));
    assert(reference.reports[4].source.line == 14);
    assert(reference.reports[5].message == "dynamic error");
    assert(
        reference.reports[5].severity
        == fsim::runtime::simir::AssertionSeverity::error);
    assert(fsim::test::same_source_path(
        reference.reports[5].source.path, source));
    assert(reference.reports[5].source.line == 16);
    assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
    assert(compiled.cache.hits == 0);
    assert(compiled.cache.misses == 1);
    assert(compiled.cache.stores == 1);
    assert(warm.compiled_processes == 1);
    assert(warm.cache.hits == 1);
    assert(warm.cache.misses == 0);
#else
    assert(compiled.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
}

void test_vhdl_failure_report(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "failure-report-test";
    config.project.top = "vhdl:work.failure_reporter(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "failure-cache-o0"
                : "failure-cache-o2");

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    assert(reference_project && compiled_project);
    const auto reference = execute_failure(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter);
    const auto compiled = execute_failure(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled);
    assert(reference.failed && compiled.failed);
    assert(reference.reports == compiled.reports);
    assert(reference.reports.size() == 1);
    assert(reference.reports[0].message == "dynamic failure");
    assert(
        reference.reports[0].severity
        == fsim::runtime::simir::AssertionSeverity::failure);
    assert(
        reference.severity
        == fsim::runtime::simir::AssertionSeverity::failure);
    assert(reference.source == compiled.source);
    assert(fsim::test::same_source_path(reference.source.path, source));
    assert(reference.message == compiled.message);
    assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
#else
    assert(compiled.compiled_processes == 0);
#endif
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-display-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "display_test.sv";
    {
        std::ofstream output(source);
        output << R"(
module display_test;
  logic [3:0] q;
  logic [7:0] n;
  logic signed [7:0] s;
  string formatted_time;
  initial begin
    q = 4'b10xz;
    n = 8'd165;
    s = 8'hff;
    $write("first\t");
    $display("+line\nembedded \"quote\" \\ \101");
    $display(8'h2a);
    $display(8'shff);
    $display("q=%%:%b!", q);
    $write("[%b]", q);
    $display("h=%h", q);
    $display("o=%o", n);
    $display("d=%d", n);
    $display("s=%d", s);
    $display("u=%d", q);
    $display("c=%c", 8'd65);
    $display("text=%s", 32'h74657374);
    $display("compact=%0h", 16'h00a5);
    $strobe("post=%b compact-time=%0t", q);
    $strobeb(q);
    $strobeh(n);
    $strobeo(n);
    q = 4'b0011;
    q <= 4'b1110;
    $monitor("mon=%b t=%t", q);
    $display("upper=%X", n);
    $display("width=%6h", n);
    $display("left=%-6x!", n);
    $display("zero=%06D", s);
    $display("multi=%b/%h tail=", q, n, s);
    $display(q, n);
    $display("scope=%m q=%b", q);
    #1 q = 4'b0101;
    #1 $monitoroff;
    q = 4'b0110;
    #1 $monitoron;
    q = 4'b0111;
    #1 $display("time=%04t");
    $display(18_446_744_073_709_551_616);
    $display(257'h1_0000000000000000);
    $display(65'sh1_0000000000000000);
    $display(4097'h1);
    $displayb(q);
    $displayh(n);
    $displayo(n);
    $writeb(q);
    $writeh(n);
    $writeo(n);
    $display;
    $write("second");
    $write;
    $display;
    formatted_time = $sformatf("%0t", $time);
    $display("sformat-time=%s", formatted_time);
    $finish;
  end
  initial begin
    #5 $display("after-finish");
  end
endmodule
)";
    }

    const auto monitor_source = directory.path / "monitor_radix_test.sv";
    {
        std::ofstream output(monitor_source);
        output << R"(
module monitor_radix_test;
  logic [3:0] q;
  logic [7:0] n;
  initial begin
    q = 4'b10xz;
    n = 8'ha5;
    $monitorb(q);
    #1 $monitorh(n);
    #1 $monitoro(n);
    #1 $finish;
  end
endmodule
)";
    }

    const auto report_source = directory.path / "report.vhd";
    {
        // FSIM-CONFORMANCE CF-VHDL-REPORT-001 source=SRC-UVVM expectation=execute
        std::ofstream output(report_source);
        output << R"(
entity reporter is
end entity;
architecture rtl of reporter is
begin
  process
    variable prefix : string := "dynamic";
    variable level : severity_level := warning;
  begin
    report "vhdl ""quote""" severity note;
    report "" severity warning;
    report "error" severity error;
    report prefix & " report" severity level;
    assert false report prefix & " assertion" severity level;
    level := error;
    report prefix & " error" severity level;
    assert true report prefix & " skipped" severity failure;
    wait;
  end process;
end architecture;
)";
    }

    const auto failure_report_source = directory.path / "failure_report.vhd";
    {
        // FSIM-CONFORMANCE CF-VHDL-REPORT-N01 source=SRC-UVVM expectation=runtime-failure
        std::ofstream output(failure_report_source);
        output << R"(
entity failure_reporter is
end entity;
architecture rtl of failure_reporter is
begin
  process
    variable prefix : string := "dynamic";
    variable level : severity_level := failure;
  begin
    report prefix & " failure" severity level;
    report "unreachable" severity note;
    wait;
  end process;
end architecture;
)";
    }

    const auto manifest = directory.path / "fsim.toml";
    {
        std::ofstream output(manifest);
        output
            << "schema = 3\n"
            << "[project]\n"
            << "name = \"display-test\"\n"
            << "top = \"sv:work.display_test\"\n"
            << "time_resolution = \"1ns\"\n"
            << "[[source_set]]\n"
            << "language = \"systemverilog\"\n"
            << "standard = \"2017\"\n"
            << "library = \"work\"\n"
            << "files = [\"display_test.sv\"]\n"
            << "[build]\n"
            << "optimization = \"O2\"\n"
            << "cache_path = \"cli-cache\"\n"
            << "[run]\n"
            << "max_deltas = 1000\n";
    }
    {
        std::istringstream input;
        std::ostringstream output;
        std::ostringstream error;
        const auto result = run_cli(
            { "fsim", "run", "-p", manifest.string() },
            input,
            output,
            error);
        if (result != 0) {
            std::cerr << "display CLI failed: " << error.str();
        }
        assert(result == 0);
        assert(error.str().empty());
        assert(
            // Decimal values without an explicit field width are padded to
            // the width of their largest magnitude (IEEE 1800-2017
            // 21.2.1.3): 78 digits for 257 bits, 1234 for 4097 bits.
            output.str().find(
                std::string { "first\t+line\nembedded \"quote\" \\ A\n"
                              " 42\n  -1\nq=%:10xz!\n[10xz]h=x\no=245\n"
                              "d=165\ns=  -1\nu= x\nc=A\ntext=test\n"
                              "compact=a5\n"
                              "upper=a5\nwidth=    a5\nleft=a5    !\n"
                              "zero=-00001\nmulti=0011/a5 tail=  -1\n"
                              " 3165\n"
                              "scope=display_test q=0011\n"
                              "post=1110 compact-time=0\n1110\na5\n245\n"
                              "mon=1110 t=                   0\n"
                              "mon=0101 t=                   1\n"
                              "mon=0111 t=                   3\ntime=0004\n"
                              " 18446744073709551616\n" }
                + std::string(58U, ' ') + "18446744073709551616\n"
                + "-18446744073709551616\n" + std::string(1233U, ' ')
                + "1\n"
                  "0111\na5\n245\n0111a5245\nsecond\n"
                  "sformat-time=4\n"
                  "simulation stopped at tick 4")
            != std::string::npos);
    }

    test_stdio_marker_and_builtin_hook_replacement(directory.path);

    test_display(
        directory.path, source, fsim::project::Optimization::o0);
    test_display(
        directory.path, source, fsim::project::Optimization::o2);
    test_monitor_radix(
        directory.path,
        monitor_source,
        fsim::project::Optimization::o0);
    test_monitor_radix(
        directory.path,
        monitor_source,
        fsim::project::Optimization::o2);
    test_vhdl_report(
        directory.path,
        report_source,
        fsim::project::Optimization::o0);
    test_vhdl_report(
        directory.path,
        report_source,
        fsim::project::Optimization::o2);
    test_vhdl_failure_report(
        directory.path,
        failure_report_source,
        fsim::project::Optimization::o0);
    test_vhdl_failure_report(
        directory.path,
        failure_report_source,
        fsim::project::Optimization::o2);
    std::cout << "display application tests passed\n";
    return 0;
}
