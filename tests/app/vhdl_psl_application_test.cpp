// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/runtime/vhpi_checkpoint.hpp"
#include "fsim/version.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace {

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
inline constexpr bool process_uses_address_sanitizer = true;
#else
inline constexpr bool process_uses_address_sanitizer = false;
#endif
#elif defined(__SANITIZE_ADDRESS__)
inline constexpr bool process_uses_address_sanitizer = true;
#else
inline constexpr bool process_uses_address_sanitizer = false;
#endif

inline constexpr std::uint64_t process_address_space_ceiling
    = 6ULL * 1024ULL * 1024ULL * 1024ULL;

[[noreturn]] void process_limit_failure(const char* operation)
{
    std::cerr << "VHDL/PSL process address-space ceiling failed: " << operation
              << '\n';
    std::abort();
}

void install_process_address_space_ceiling()
{
    if constexpr (process_uses_address_sanitizer) {
        return;
    }
#if defined(_WIN32)
    static HANDLE job = [] {
        const auto created = CreateJobObjectW(nullptr, nullptr);
        if (created == nullptr) {
            process_limit_failure("CreateJobObjectW");
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits { };
        limits.BasicLimitInformation.LimitFlags
            = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit
            = static_cast<SIZE_T>(process_address_space_ceiling);
        if (SetInformationJobObject(created, JobObjectExtendedLimitInformation,
                &limits, static_cast<DWORD>(sizeof(limits)))
            == 0) {
            process_limit_failure("SetInformationJobObject");
        }
        if (AssignProcessToJobObject(created, GetCurrentProcess()) == 0) {
            process_limit_failure("AssignProcessToJobObject");
        }
        return created;
    }();
    (void)job;
#else
    static_assert(process_address_space_ceiling
        <= std::numeric_limits<rlim_t>::max());
    rlimit current { };
    if (getrlimit(RLIMIT_AS, &current) != 0) {
        process_limit_failure("getrlimit(RLIMIT_AS)");
    }
    const auto ceiling = static_cast<rlim_t>(process_address_space_ceiling);
    auto bounded = ceiling;
    if (current.rlim_max != RLIM_INFINITY) {
        bounded = std::min(bounded, current.rlim_max);
    }
    if (current.rlim_cur != RLIM_INFINITY) {
        bounded = std::min(bounded, current.rlim_cur);
    }
    current.rlim_cur = bounded;
    if (setrlimit(RLIMIT_AS, &current) != 0) {
        process_limit_failure("setrlimit(RLIMIT_AS)");
    }
#endif
}

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl_psl_execution";
    config.project.top = "vhdl:work.psl_execution(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0 ? "cache-o0"
                                                           : "cache-o2");
    config.run.max_deltas = 1'000U;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

struct Capture {
    std::vector<fsim::runtime::VhdlPslAttemptSnapshot> attempts;
    std::vector<fsim::app::ConcurrentAssertionCoverage> coverage;
    std::vector<fsim::runtime::VhdlPslAttemptSnapshot> callback_attempts;
    std::vector<fsim::app::ConcurrentAssertionEvent> assertion_events;
    std::vector<std::pair<std::string,
        fsim::runtime::simir::AssertionSeverity>>
        reports;
    std::size_t compiled_processes { };
    fsim::app::NativeCacheStatistics cache;
    std::string debug_signature;
    std::string waveform;
};

std::string normalized_debug_signature(fsim::app::VhdlDebugSnapshot snapshot)
{
    snapshot.simulation_identity = 0U;
    for (auto& scope : snapshot.scopes) {
        scope.vhpi_handle = 0U;
    }
    for (auto& declaration : snapshot.declarations) {
        declaration.vhpi_handle = 0U;
    }
    for (auto& process : snapshot.processes) {
        process.vhpi_handle = 0U;
    }
    return fsim::app::format_vhdl_debug_snapshot(snapshot);
}

Capture run_built(
    const fsim::project::Config& config,
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation {
        std::move(project), config.run.max_deltas, engine
    };
    Capture capture;
    const auto trace_clock = simulation.find_signal("psl_execution.clk");
    const auto trace_request = simulation.find_signal("psl_execution.request");
    std::ostringstream waveform;
    fsim::runtime::VcdWriter vcd { waveform, "1ns", 64U };
    std::optional<fsim::runtime::VcdSignal> vcd_clock;
    std::optional<fsim::runtime::VcdSignal> vcd_request;
    if (trace_clock && trace_request) {
        vcd_clock = vcd.declare_signal("psl_execution.clk", 1U);
        vcd_request = vcd.declare_signal("psl_execution.request", 1U);
        vcd.begin();
        vcd.change(*vcd_clock, simulation.read_signal(*trace_clock));
        vcd.change(*vcd_request, simulation.read_signal(*trace_request));
        simulation.set_signal_change_hook(
            [&](const auto signal, const auto& value, const auto time,
                const auto) {
                vcd.set_time(time);
                if (signal == *trace_clock) {
                    vcd.change(*vcd_clock, value);
                } else if (signal == *trace_request) {
                    vcd.change(*vcd_request, value);
                }
            });
    }
    bool attempt_hook_threw { };
    simulation.set_vhdl_psl_attempt_hook(
        [&](const fsim::runtime::VhdlPslAttemptSnapshot& attempt) {
            capture.callback_attempts.push_back(attempt);
            if (!attempt_hook_threw) {
                attempt_hook_threw = true;
                throw std::runtime_error("contained PSL attempt observer");
            }
        });
    bool assertion_hook_threw { };
    simulation.set_concurrent_assertion_hook(
        [&](const fsim::app::ConcurrentAssertionEvent& event) {
            capture.assertion_events.push_back(event);
            if (!assertion_hook_threw) {
                assertion_hook_threw = true;
                throw std::runtime_error("contained assertion observer");
            }
        });
    simulation.set_report_hook([&](const auto, const std::string_view message,
                                   const auto severity, const auto&, const auto,
                                   const auto) {
        if (message.starts_with("VHDL PSL ")) {
            capture.reports.emplace_back(message, severity);
        }
    });
    const auto result = simulation.run();
    if (trace_clock) {
        vcd.flush();
        capture.waveform = waveform.str();
    }
    assert(result.status == fsim::runtime::RunStatus::completed);
    capture.attempts = simulation.vhdl_psl_attempts();
    capture.coverage = simulation.vhdl_psl_coverage();
    capture.compiled_processes = simulation.compiled_process_count();
    capture.cache = simulation.native_cache_statistics();
    const auto debug_snapshot = simulation.vhdl_debug_snapshot();
    assert(debug_snapshot.simulation_identity != 0U);
    assert(debug_snapshot.time == result.time);
    assert(debug_snapshot.delta == result.delta);
    assert(debug_snapshot.psl_attempts == capture.attempts);
    assert(debug_snapshot.psl_coverage == capture.coverage);
    assert(!debug_snapshot.scopes.empty());
    capture.debug_signature = normalized_debug_signature(debug_snapshot);
    assert(std::ranges::any_of(debug_snapshot.processes,
        [](const auto& process) { return process.path.ends_with("clock_driver"); }));
    const auto formatted = fsim::app::format_vhdl_debug_snapshot(debug_snapshot);
    const auto primary_scope
        = simulation.vhdl_vhpi_objects().find("psl_execution");
    if (primary_scope) {
        assert(std::ranges::any_of(debug_snapshot.declarations,
            [](const auto& declaration) {
                return declaration.kind
                    == fsim::app::VhdlDebugDeclarationKind::file;
            }));
        assert(std::ranges::any_of(debug_snapshot.declarations,
            [](const auto& declaration) {
                return declaration.kind
                    == fsim::app::VhdlDebugDeclarationKind::access_type;
            }));
        assert(std::ranges::any_of(debug_snapshot.declarations,
            [](const auto& declaration) {
                return declaration.kind
                    == fsim::app::VhdlDebugDeclarationKind::protected_type;
            }));
        assert(std::ranges::any_of(debug_snapshot.declarations,
            [](const auto& declaration) {
                return declaration.kind
                    == fsim::app::VhdlDebugDeclarationKind::physical_type;
            }));
        assert(formatted.find("psl work:rtl:check_response")
            != std::string::npos);
        assert(formatted.find("object psl_execution.clk")
            != std::string::npos);
        assert(capture.waveform.find("$scope module psl_execution $end")
            != std::string::npos);
        assert(capture.waveform.find(" clk $end") != std::string::npos);
        assert(capture.waveform.find(" request $end")
            != std::string::npos);
        assert(capture.waveform.find("#1") != std::string::npos);
        assert(formatted.find("\nobject clk ") == std::string::npos);
        assert(formatted.find("process psl_execution.psl_execution.")
            == std::string::npos);
        const auto clock = std::ranges::find(debug_snapshot.declarations,
            "psl_execution.clk", &fsim::app::VhdlDebugDeclaration::path);
        assert(clock != debug_snapshot.declarations.end());
        assert(clock->driver_count != 0U);
        assert(clock->live_value);
        assert(clock->vhpi_handle != 0U);
        const auto postponed = std::ranges::find_if(debug_snapshot.processes,
            [](const auto& process) { return process.postponed; });
        assert(postponed != debug_snapshot.processes.end());
        assert(postponed->vhpi_handle != 0U);
        const auto external_alias = std::ranges::find(debug_snapshot.declarations,
            "psl_execution.request_alias",
            &fsim::app::VhdlDebugDeclaration::path);
        assert(external_alias != debug_snapshot.declarations.end());
        assert(external_alias->external_alias);
        const auto external_value = std::ranges::find(
            debug_snapshot.declarations, "psl_execution.external_request",
            &fsim::app::VhdlDebugDeclaration::path);
        assert(external_value != debug_snapshot.declarations.end());
        assert(external_value->live_value);

        std::ostringstream debugger_output;
        std::ostringstream debugger_error;
        fsim::diagnostic::Engine debugger_diagnostics;
        fsim::app::DebuggerControl debugger { simulation, debugger_output,
            debugger_error, config, debugger_diagnostics };
        debugger.execute({ "vhdl", "summary" });
        debugger.execute({ "vhdl", "objects" });
        debugger.execute({ "vhdl", "processes" });
        debugger.execute({ "vhdl", "psl" });
        assert(debugger_error.str().empty());
        assert(!debugger_diagnostics.has_error());
        assert(debugger_output.str().find("vhdl simulation=")
            != std::string::npos);
        assert(debugger_output.str().find("object psl_execution.sample_file kind=file")
            != std::string::npos);
        assert(debugger_output.str().find("postponed_observer postponed")
            != std::string::npos);
        assert(debugger_output.str().find("psl work:rtl:check_response")
            != std::string::npos);
        assert(debugger_output.str().find("coverage work:rtl:check_response")
            != std::string::npos);

        auto access_type = simulation.vhdl_vhpi_objects().find(
            "psl_execution.integer_pointer");
        assert(access_type);
        fsim::runtime::VhdlVhpiObjectRegistry foreign_registry(
            debug_snapshot.simulation_identity + 1U);
        assert(foreign_registry.lookup_object(access_type.value.handle).error
            == fsim::runtime::VhdlVhpiObjectError::CrossSimulation);
        assert(simulation.vhdl_vhpi_objects().release_object(
                   access_type.value.handle)
            == fsim::runtime::VhdlVhpiObjectError::None);
        fsim::runtime::VhdlVhpiObjectDescriptor replacement;
        replacement.kind = fsim::runtime::VhdlVhpiObjectKind::Type;
        replacement.parent = primary_scope.value.handle;
        replacement.name = "integer_pointer";
        const auto recreated
            = simulation.vhdl_vhpi_objects().create_object(replacement);
        assert(recreated);
        assert(simulation.vhdl_vhpi_objects()
                   .lookup_object(access_type.value.handle)
                   .error
            == fsim::runtime::VhdlVhpiObjectError::StaleHandle);
    }

    try {
        (void)simulation.vhdl_debug_snapshot(
            fsim::app::VhdlDebugLimits { 1U, 1U << 20U, 1U << 20U });
        assert(false);
    } catch (const fsim::app::VhdlDebugError&) {
    }
    try {
        (void)simulation.vhdl_debug_snapshot(
            fsim::app::VhdlDebugLimits { 16'384U, 1U, 1U << 20U });
        assert(false);
    } catch (const fsim::app::VhdlDebugError&) {
    }
    try {
        (void)fsim::app::format_vhdl_debug_snapshot(debug_snapshot, 1U);
        assert(false);
    } catch (const fsim::app::VhdlDebugError&) {
    }
    return capture;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project);
    assert(diagnostics.empty());
    return run_built(config, std::move(*project), engine);
}

void verify(const Capture& capture, const bool compiled)
{
    using fsim::runtime::VhdlPslAttemptOutcome;
    const auto outcomes = [&](const std::string_view monitor) {
        std::vector<VhdlPslAttemptOutcome> result;
        for (const auto& attempt : capture.attempts) {
            if (attempt.monitor == monitor) {
                result.push_back(attempt.outcome);
            }
        }
        return result;
    };
    assert(capture.attempts.size() == 63U);
    assert(capture.callback_attempts.size() == capture.attempts.size());
    std::vector<std::uint64_t> callback_ids;
    for (const auto& attempt : capture.callback_attempts) {
        callback_ids.push_back(attempt.attempt);
    }
    std::ranges::sort(callback_ids);
    for (std::size_t index = 0U; index < callback_ids.size(); ++index) {
        assert(callback_ids[index] == index + 1U);
    }
    assert(capture.assertion_events.size() == capture.attempts.size());
    assert(capture.coverage.size() == 21U);
    assert(capture.reports.size() == 33U);
    assert(std::ranges::count_if(capture.reports, [](const auto& report) {
        return report.second
            == fsim::runtime::simir::AssertionSeverity::warning;
    }) == 1U);
    assert(capture.attempts[0].monitor == "work:rtl:check_response");
    assert(capture.attempts[0].attempt == 1U);
    assert(capture.attempts[0].outcome == VhdlPslAttemptOutcome::pass);
    assert(capture.attempts[0].start_time == 1U);
    assert(capture.attempts[0].end_time == 3U);
    assert(capture.attempts[0].start_sample == 0U);
    assert(capture.attempts[0].end_sample == 1U);
    assert((outcomes("work:rtl:check_response")
        == std::vector { VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::vacuous }));
    assert((outcomes("work:rtl:check_repeated")
        == std::vector { VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::failure }));
    assert((outcomes("work:rtl:check_fused")
        == std::vector { VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::failure }));
    assert((outcomes("work:rtl:check_eventually")
        == std::vector { VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::failure }));
    assert((outcomes("work:rtl:check_until")
        == std::vector { VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::failure }));
    assert((outcomes("work:rtl:check_history")
        == std::vector { VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::pass }));
    assert((outcomes("work:rtl:check_always")
        == std::vector(3U, VhdlPslAttemptOutcome::failure)));
    assert((outcomes("work:rtl:check_strong")
        == std::vector(3U, VhdlPslAttemptOutcome::failure)));
    assert((outcomes("work:rtl:check_weak")
        == std::vector(3U, VhdlPslAttemptOutcome::vacuous)));
    assert((outcomes("work:rtl:check_abort")
        == std::vector { VhdlPslAttemptOutcome::aborted,
            VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::vacuous }));
    assert((outcomes("work:rtl:check_formal")
        == outcomes("work:rtl:check_repeated")));
    assert((outcomes("work:rtl:check_nonconsecutive")
        == outcomes("work:rtl:check_repeated")));
    assert((outcomes("work:rtl:check_goto")
        == outcomes("work:rtl:check_repeated")));
    assert((outcomes("work:rtl:check_within")
        == outcomes("work:rtl:check_repeated")));
    assert((outcomes("work:rtl:check_actual")
        == std::vector { VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::failure }));
    assert((outcomes("work:rtl:check_nonconsecutive_tail")
        == std::vector { VhdlPslAttemptOutcome::pass,
            VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::failure }));
    assert((outcomes("work:rtl:check_goto_tail")
        == std::vector(3U, VhdlPslAttemptOutcome::failure)));
    assert((outcomes("work:rtl:assume_response")
        == outcomes("work:rtl:check_response")));
    assert((outcomes("work:rtl:restrict_response")
        == outcomes("work:rtl:check_response")));
    assert((outcomes("work:rtl:cover_response")
        == outcomes("work:rtl:check_response")));
    assert((outcomes("work:rtl:check_vital")
        == std::vector { VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::failure,
            VhdlPslAttemptOutcome::pass }));
    const auto coverage = [&](const std::string_view name)
        -> const fsim::app::ConcurrentAssertionCoverage& {
        const auto found = std::ranges::find(capture.coverage, name,
            &fsim::app::ConcurrentAssertionCoverage::name);
        assert(found != capture.coverage.end());
        return *found;
    };
    assert(coverage("work:rtl:check_weak").vacuous == 3U);
    assert(coverage("work:rtl:check_abort").aborted == 1U);
    assert(coverage("work:rtl:assume_response").kind
        == fsim::app::ConcurrentAssertionCoverageKind::assumption);
    assert(coverage("work:rtl:restrict_response").kind
        == fsim::app::ConcurrentAssertionCoverageKind::restriction);
    assert(coverage("work:rtl:cover_response").kind
        == fsim::app::ConcurrentAssertionCoverageKind::cover);
#if defined(FSIM_HAS_LLVM)
    assert((capture.compiled_processes != 0U) == compiled);
#else
    assert(capture.compiled_processes == 0U);
    static_cast<void>(compiled);
#endif
}

} // namespace

int main()
{
    install_process_address_space_ceiling();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vhdl-psl-"
            + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "psl_execution.vhd";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.vital_timing.all;

entity psl_execution is end entity;

architecture rtl of psl_execution is
  type sample_distance is range 0 to 1000 units
    quantum;
    interval = 10 quantum;
  end units sample_distance;
  type sample_file is file of integer;
  type integer_pointer is access integer;
  type sample_guard is protected
    impure function ready return boolean;
  end protected sample_guard;
  signal clk : std_logic;
  signal request : std_logic;
  signal acknowledge : std_logic;
  signal reset : std_logic;
  signal pulse : std_logic;
  signal tail : std_logic;
  signal numeric_value : unsigned(128 downto 0);
  signal vital_acknowledge : std_logic;
  signal external_request : std_logic;
  alias request_alias : std_logic is request;
  -- psl default clock is clk = '1';
  -- psl boolean requested is request = '1';
  -- psl sequence repeated is {requested[*2]};
  -- psl sequence formal_repeat(count : natural := 2) is {requested[*count]};
  -- psl sequence nonconsecutive is {requested[=2]};
  -- psl sequence goto_repeat is {requested[->2]};
  -- psl sequence nonconsecutive_tail is {pulse[=1] : tail};
  -- psl sequence goto_tail is {pulse[->1] : tail};
  -- psl sequence fused is {requested : acknowledge = '1'};
  -- psl sequence envelope is {requested ; acknowledge = '1'};
  -- psl sequence enclosed is {requested} within envelope;
  -- psl property response is requested |-> next[1] acknowledge = '1';
  -- psl property repeated_property is repeated;
  -- psl property formal_property is formal_repeat;
  -- psl property actual_property is formal_repeat(1);
  -- psl property nonconsecutive_property is nonconsecutive;
  -- psl property goto_property is goto_repeat;
  -- psl property within_property is enclosed;
  -- psl property fused_property is fused;
  -- psl property eventually_ack is eventually[0:1] acknowledge = '1';
  -- psl property request_until_ack is request = '1' until acknowledge = '1';
  -- psl property request_history is prev request = '1';
  -- psl property request_always is always request = '1';
  -- psl property strong_future is strong next[10] acknowledge = '1';
  -- psl property weak_future is weak next[10] acknowledge = '1';
  -- psl property aborted_response is response async_abort reset = '1';
  -- psl property nonconsecutive_tail_property is nonconsecutive_tail;
  -- psl property goto_tail_property is goto_tail;
  -- psl property vital_property is vital_acknowledge = '1';
begin
  external_request <=
    << signal .psl_execution.request : std_logic >>;
  -- psl CHECK_RESPONSE: assert response;
  -- psl CHECK_REPEATED: assert repeated_property;
  -- psl CHECK_FORMAL: assert formal_property;
  -- psl CHECK_ACTUAL: assert actual_property;
  -- psl CHECK_NONCONSECUTIVE: assert nonconsecutive_property;
  -- psl CHECK_GOTO: assert goto_property;
  -- psl CHECK_WITHIN: assert within_property;
  -- psl CHECK_FUSED: assert fused_property;
  -- psl CHECK_EVENTUALLY: assert eventually_ack;
  -- psl CHECK_UNTIL: assert request_until_ack;
  -- psl CHECK_HISTORY: assert request_history;
  -- psl CHECK_ALWAYS: assert request_always;
  -- psl CHECK_STRONG: assert strong_future;
  -- psl CHECK_WEAK: assert weak_future;
  -- psl CHECK_ABORT: assert aborted_response;
  -- psl CHECK_NONCONSECUTIVE_TAIL: assert nonconsecutive_tail_property;
  -- psl CHECK_GOTO_TAIL: assert goto_tail_property;
  -- psl ASSUME_RESPONSE: assume response;
  -- psl RESTRICT_RESPONSE: restrict response;
  -- psl COVER_RESPONSE: cover response;
  -- psl CHECK_VITAL: assert vital_property;

  vital_delay_process : process(acknowledge)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => vital_acknowledge,
        GlitchData => glitch,
        OutSignalName => "vital_acknowledge",
        OutTemp => acknowledge,
        Paths => (0 => (acknowledge'last_event, 1 ns, true)),
        DefaultDelay => 1 ns,
        Mode => VitalTransport,
        XOn => false);
  end process;

  postponed_observer : postponed process(clk)
  begin
    if rising_edge(clk) then
      null;
    end if;
  end postponed process;

  clock_driver : process
  begin
    clk <= '0';
    wait for 1 ns;
    clk <= '1';
    wait for 1 ns;
    clk <= '0';
    wait for 1 ns;
    clk <= '1';
    wait for 1 ns;
    clk <= '0';
    wait for 1 ns;
    clk <= '1';
    wait;
  end process;

  stimulus : process
  begin
    request <= '1';
    acknowledge <= '0';
    reset <= '0';
    pulse <= '1';
    tail <= '0';
    numeric_value <= to_unsigned(1, 129);
    wait for 2 ns;
    acknowledge <= transport '1' after 1 ns;
    reset <= '1';
    pulse <= '0';
    tail <= '1';
    wait for 1 ns;
    reset <= '0';
    wait for 1 ns;
    request <= '0';
    acknowledge <= transport '0' after 1 ns;
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }

    const auto interpreter = run_once(
        make_config(directory.path, source, fsim::project::Optimization::o0),
        fsim::app::SimulationEngine::interpreter);
    const auto llvm_o0 = run_once(
        make_config(directory.path, source, fsim::project::Optimization::o0),
        fsim::app::SimulationEngine::compiled);
    const auto debug = run_once(
        make_config(directory.path, source, fsim::project::Optimization::o0),
        fsim::app::SimulationEngine::debug);
    const auto cold = run_once(
        make_config(directory.path, source, fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        make_config(directory.path, source, fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::compiled);

    verify(interpreter, false);
    verify(llvm_o0, true);
    verify(debug, true);
    verify(cold, true);
    verify(warm, true);
    assert(cold.attempts == warm.attempts);
    assert(cold.callback_attempts == warm.callback_attempts);
    assert(cold.assertion_events == warm.assertion_events);
    assert(cold.coverage == warm.coverage);
    assert(interpreter.debug_signature == llvm_o0.debug_signature);
    assert(interpreter.debug_signature == debug.debug_signature);
    assert(interpreter.debug_signature == cold.debug_signature);
    assert(cold.debug_signature == warm.debug_signature);
    assert(interpreter.waveform == llvm_o0.waveform);
    assert(interpreter.waveform == debug.waveform);
    assert(interpreter.waveform == cold.waveform);
    assert(cold.waveform == warm.waveform);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses != 0U);
    assert(warm.cache.hits != 0U);
#else
    assert(cold.cache.hits == 0U && cold.cache.misses == 0U);
    assert(cold.cache.stores == 0U);
    assert(warm.cache.hits == 0U && warm.cache.misses == 0U);
    assert(warm.cache.stores == 0U);
#endif

    auto breakpoint_config = make_config(
        directory.path, source, fsim::project::Optimization::o0);
    breakpoint_config.project.name = "vhdl_psl_debug_breakpoint";
    breakpoint_config.build.cache_path = directory.path / "breakpoint-cache";
    fsim::diagnostic::Engine breakpoint_diagnostics;
    auto breakpoint_project
        = fsim::app::build_project(breakpoint_config, breakpoint_diagnostics);
    assert(breakpoint_project);
    fsim::app::Simulation breakpoint_simulation { std::move(*breakpoint_project),
        breakpoint_config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter };
    std::ostringstream breakpoint_output;
    std::ostringstream breakpoint_error;
    fsim::app::DebuggerControl breakpoint_debugger { breakpoint_simulation,
        breakpoint_output, breakpoint_error, breakpoint_config,
        breakpoint_diagnostics };
    breakpoint_debugger.execute(
        { "break", "signal", "psl_execution.clk" });
    breakpoint_debugger.execute({ "run" });
    breakpoint_debugger.execute({ "vhdl", "summary" });
    assert(breakpoint_output.str().find(
               "breakpoint 1 set on psl_execution.clk")
        != std::string::npos);
    assert(breakpoint_output.str().find(
               "psl_execution.clk changed to")
        != std::string::npos);
    assert(breakpoint_output.str().find("vhdl simulation=")
        != std::string::npos);
    breakpoint_debugger.execute({ "clear" });
    breakpoint_debugger.execute({ "continue" });
    assert(breakpoint_simulation.finished());
    assert(breakpoint_simulation.vhdl_psl_attempts().size() == 63U);
    assert(breakpoint_error.str().empty());
    assert(!breakpoint_diagnostics.has_error());

    auto artifact_config = make_config(
        directory.path, source, fsim::project::Optimization::o2);
    artifact_config.project.name = "vhdl_psl_artifact_replay";
    artifact_config.build.cache_path = directory.path / "artifact-build-cache";
    const auto object = directory.path / "vhdl-psl.fsimobj";
    const auto library = directory.path / "vhdl-psl.fsimlib";
    const auto design = directory.path / "vhdl-psl.fsimdesign";
    fsim::diagnostic::Engine artifact_diagnostics;
    assert(fsim::app::compile_artifact(
        artifact_config, object, artifact_diagnostics));
    assert(fsim::app::export_library(
        artifact_config, "work", library, artifact_diagnostics));
    const std::array objects { object };
    auto elaborate_config = artifact_config;
    elaborate_config.source_sets.clear();
    const auto elaborated = fsim::app::elaborate_artifact(
        elaborate_config, objects, design, artifact_diagnostics);
    if (!elaborated) {
        for (const auto& diagnostic : artifact_diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(elaborated);
    assert(!artifact_diagnostics.has_error());

    const auto relocated_object = directory.path / "relocated-vhdl-psl.fsimobj";
    const auto relocated_library = directory.path / "relocated-vhdl-psl.fsimlib";
    const auto relocated_design = directory.path / "relocated-vhdl-psl.fsimdesign";
    std::filesystem::rename(object, relocated_object);
    std::filesystem::rename(library, relocated_library);
    std::filesystem::rename(design, relocated_design);
    const auto hidden_source = directory.path / "hidden-vhdl-psl.vhd";
    std::filesystem::rename(source, hidden_source);

    auto mapped_config = artifact_config;
    mapped_config.project.name = "vhdl_psl_mapped_replay";
    mapped_config.source_sets.clear();
    mapped_config.library_mappings.push_back({ "work", relocated_library });
    mapped_config.build.cache_path = directory.path / "mapped-replay-cache";
    const auto mapped_cold = run_once(
        mapped_config, fsim::app::SimulationEngine::compiled);
    const auto mapped_warm = run_once(
        mapped_config, fsim::app::SimulationEngine::compiled);
    verify(mapped_cold, true);
    assert(mapped_cold.attempts == cold.attempts);
    assert(mapped_cold.coverage == cold.coverage);
    assert(mapped_cold.debug_signature == cold.debug_signature);
    assert(mapped_cold.waveform == cold.waveform);
    assert(mapped_cold.attempts == mapped_warm.attempts);
#if defined(FSIM_HAS_LLVM)
    assert(mapped_warm.cache.hits != 0U);
#else
    assert(mapped_cold.cache.hits == 0U && mapped_cold.cache.misses == 0U);
    assert(mapped_cold.cache.stores == 0U);
    assert(mapped_warm.cache.hits == 0U && mapped_warm.cache.misses == 0U);
    assert(mapped_warm.cache.stores == 0U);
#endif

    const auto run_design_artifact = [&] {
        fsim::diagnostic::Engine diagnostics;
        auto built = fsim::app::load_design_artifact(
            relocated_design, diagnostics);
        assert(built && !diagnostics.has_error());
        assert(std::ranges::any_of(
            built->vhdl_hir.units(), [](const auto& unit) {
                return !unit.psl_directives.empty();
            }));
        built->cache_path = directory.path / "design-replay-cache";
        return run_built(artifact_config, std::move(*built),
            fsim::app::SimulationEngine::compiled);
    };
    const auto design_cold = run_design_artifact();
    const auto design_warm = run_design_artifact();
    verify(design_cold, true);
    assert(design_cold.attempts == cold.attempts);
    assert(design_cold.coverage == cold.coverage);
    assert(design_cold.debug_signature == cold.debug_signature);
    assert(design_cold.waveform == cold.waveform);
    assert(design_cold.attempts == design_warm.attempts);
#if defined(FSIM_HAS_LLVM)
    assert(design_warm.cache.hits != 0U);
#else
    assert(design_cold.cache.hits == 0U && design_cold.cache.misses == 0U);
    assert(design_cold.cache.stores == 0U);
    assert(design_warm.cache.hits == 0U && design_warm.cache.misses == 0U);
    assert(design_warm.cache.stores == 0U);
#endif

    fsim::diagnostic::Engine checkpoint_diagnostics;
    auto checkpoint_built = fsim::app::load_design_artifact(
        relocated_design, checkpoint_diagnostics);
    assert(checkpoint_built && !checkpoint_diagnostics.has_error());
    const fsim::runtime::VhdlVhpiCheckpointCompatibility compatibility {
        checkpoint_built->artifact_identity,
        checkpoint_built->cache_key + ":"
            + std::string { fsim::standard_library_cache_version },
        { }, { }
    };
    fsim::app::Simulation checkpoint_source { std::move(*checkpoint_built),
        artifact_config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter };
    const auto checkpoint_snapshot = checkpoint_source.vhdl_debug_snapshot();
    std::vector<fsim_vhpi_handle_v1> checkpoint_handles;
    checkpoint_handles.reserve(checkpoint_snapshot.scopes.size()
        + checkpoint_snapshot.declarations.size()
        + checkpoint_snapshot.processes.size());
    for (const auto& scope : checkpoint_snapshot.scopes) {
        checkpoint_handles.push_back(scope.vhpi_handle);
    }
    for (const auto& declaration : checkpoint_snapshot.declarations) {
        checkpoint_handles.push_back(declaration.vhpi_handle);
    }
    for (const auto& process : checkpoint_snapshot.processes) {
        checkpoint_handles.push_back(process.vhpi_handle);
    }
    const auto checkpoint = fsim::runtime::capture_vhdl_vhpi_checkpoint(
        checkpoint_source.vhdl_vhpi_objects(), checkpoint_handles,
        compatibility);
    assert(checkpoint);
    auto restarted_built = fsim::app::load_design_artifact(
        relocated_design, checkpoint_diagnostics);
    assert(restarted_built && !checkpoint_diagnostics.has_error());
    fsim::app::Simulation checkpoint_restart { std::move(*restarted_built),
        artifact_config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter };
    const auto restored_checkpoint
        = fsim::runtime::restore_vhdl_vhpi_checkpoint(checkpoint.artifact,
            fsim::runtime::VhdlVhpiCheckpointFlow::PortableArtifact,
            checkpoint_restart.vhdl_vhpi_objects(), compatibility);
    assert(restored_checkpoint);
    assert(restored_checkpoint.handles.size() == checkpoint_handles.size());
    assert(std::ranges::all_of(restored_checkpoint.handles,
        [](const auto& handle) {
            return handle.source != 0U && handle.target != 0U
                && !handle.full_name.empty();
        }));

    const auto root_source = directory.path / "psl_roots.vhd";
    {
        std::ofstream output(root_source, std::ios::binary);
        output << R"(
library ieee;
use ieee.std_logic_1164.all;
entity psl_root is end entity;
architecture rtl of psl_root is
  signal clk : std_logic;
  signal observed : std_logic;
  -- psl default clock is rising_edge(clk);
  -- psl property seen is observed = '1';
begin
  -- psl CHECK_SEEN: assert seen;
  clock_driver : process
  begin
    clk <= '0';
    wait for 1 ns;
    clk <= '1';
    wait for 1 ns;
    clk <= '0';
    wait for 1 ns;
    clk <= '1';
    wait;
  end process;
  stimulus : process
  begin
    observed <= '1';
    wait for 2 ns;
    observed <= '0';
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    auto root_config = make_config(directory.path, root_source,
        fsim::project::Optimization::o0);
    root_config.project.name = "vhdl_psl_multiple_roots";
    root_config.project.top.clear();
    root_config.project.tops = {
        { "vhdl:work.psl_root(rtl)", "left" },
        { "vhdl:work.psl_root(rtl)", "right" },
    };
    const auto roots = run_once(
        root_config, fsim::app::SimulationEngine::interpreter);
    assert(roots.attempts.size() == 4U);
    assert(roots.attempts[0].monitor == "work:rtl:check_seen@left");
    assert(roots.attempts[0].instance_identity == "left");
    assert(roots.attempts[1].monitor == "work:rtl:check_seen@right");
    assert(roots.attempts[1].instance_identity == "right");
    assert(roots.attempts[2].monitor == "work:rtl:check_seen@left");
    assert(roots.attempts[3].monitor == "work:rtl:check_seen@right");
    assert(roots.attempts[0].outcome
            == fsim::runtime::VhdlPslAttemptOutcome::pass
        && roots.attempts[2].outcome
            == fsim::runtime::VhdlPslAttemptOutcome::failure);

    auto reordered_config = root_config;
    std::ranges::reverse(reordered_config.project.tops);
    reordered_config.project.name = "vhdl_psl_reordered_roots";
    reordered_config.build.cache_path = directory.path / "reordered-cache";
    const auto reordered_roots = run_once(
        reordered_config, fsim::app::SimulationEngine::compiled);
    assert(reordered_roots.attempts.size() == 4U);
    assert(reordered_roots.attempts[0].monitor
        == "work:rtl:check_seen@right");
    assert(reordered_roots.attempts[1].monitor
        == "work:rtl:check_seen@left");

    const auto sv_root_source = directory.path / "psl_mixed_root.sv";
    {
        std::ofstream output(sv_root_source, std::ios::binary);
        output << R"(
module sv_root;
  logic clk;
  logic observed;
  initial begin
    clk = 1'b0;
    observed = 1'b0;
    #1 clk = 1'b1;
  end
endmodule
)";
        assert(output.good());
    }
    auto mixed_config = make_config(directory.path, root_source,
        fsim::project::Optimization::o2);
    mixed_config.project.name = "vhdl_psl_mixed_roots";
    mixed_config.project.top.clear();
    mixed_config.project.tops = {
        { "vhdl:work.psl_root(rtl)", "vhdl_side" },
        { "sv:work.sv_root", "sv_side" },
    };
    mixed_config.build.cache_path = directory.path / "mixed-cache";
    fsim::project::SourceSet sv_sources;
    sv_sources.language = fsim::project::Language::system_verilog;
    sv_sources.standard = "2017";
    sv_sources.library = "work";
    sv_sources.files.push_back(sv_root_source);
    mixed_config.source_sets.push_back(std::move(sv_sources));
    const auto mixed_roots = run_once(
        mixed_config, fsim::app::SimulationEngine::compiled);
    assert(mixed_roots.attempts.size() == 2U);
    assert(mixed_roots.attempts[0].monitor == "work:rtl:check_seen");
    assert(mixed_roots.attempts[0].instance_identity == "vhdl_side");

    const auto systemc_root_source = directory.path / "psl_systemc_root.cpp";
    {
        std::ofstream output(systemc_root_source, std::ios::binary);
        output << R"(
#include <systemc>

SC_MODULE(PslCollisionNative) {
  sc_core::sc_signal<sc_dt::sc_logic> clk{"clk"};
  sc_core::sc_signal<sc_dt::sc_logic> observed{"observed"};

  SC_CTOR(PslCollisionNative) {
    SC_THREAD(drive);
  }

  void drive() {
    clk.write(sc_dt::sc_logic{'0'});
    observed.write(sc_dt::sc_logic{'0'});
    wait(sc_core::sc_time{1, sc_core::SC_NS});
    clk.write(sc_dt::sc_logic{'1'});
  }
};

extern "C" fsim_sc_status_v1 fsim_plugin_init_v1(
    const fsim_sc_host_v1* host,
    fsim_sc_registrar_v1* registrar) {
  if (host == nullptr || registrar == nullptr
      || host->abi_version != FSIM_SYSTEMC_ABI_VERSION
      || registrar->abi_version != FSIM_SYSTEMC_ABI_VERSION) {
    return FSIM_SC_ABI_MISMATCH;
  }
  return fsim::systemc::register_module_factory<PslCollisionNative>(
      host, registrar, "psl_collision_native");
}
)";
        assert(output.good());
    }
    auto systemc_config = make_config(directory.path, root_source,
        fsim::project::Optimization::o2);
    systemc_config.project.name = "vhdl_psl_systemc_roots";
    systemc_config.project.top.clear();
    systemc_config.project.tops = {
        { "vhdl:work.psl_root(rtl)", "vhdl_side" },
        { "systemc:models.psl_collision_native", "systemc_side" },
    };
    systemc_config.build.cache_path = directory.path / "systemc-cache";
    fsim::project::SourceSet systemc_sources;
    systemc_sources.language = fsim::project::Language::systemc;
    systemc_sources.standard = "2023-subset";
    systemc_sources.library = "models";
    systemc_sources.files.push_back(systemc_root_source);
    systemc_sources.include_directories.emplace_back(
        std::filesystem::path { FSIM_TEST_SOURCE_DIR } / "include");
    systemc_config.source_sets.push_back(std::move(systemc_sources));
    const auto systemc_roots = run_once(
        systemc_config, fsim::app::SimulationEngine::compiled);
    assert(systemc_roots.attempts.size() == 2U);
    assert(systemc_roots.attempts[0].monitor == "work:rtl:check_seen");
    assert(systemc_roots.attempts[0].instance_identity == "vhdl_side");
    std::cout
        << "FSIM-VHDL-PSL-PASS stages=direct/interpreter/llvm-o0/llvm-o2/"
           "cache-cold/cache-warm/debug/vcd/object/library/design/relocation/"
           "replay/checkpoint/multiple-root/systemverilog/systemc "
           "resources=as6g/delta1000/vcd64 gaps=0\n";
    return 0;
}
