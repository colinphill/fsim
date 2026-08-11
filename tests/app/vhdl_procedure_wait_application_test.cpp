// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

struct Capture {
    fsim::runtime::RunResult result;
    std::array<std::string, 2> final_values;
    std::vector<std::tuple<std::size_t, std::string,
        fsim::runtime::SimulationTick>>
        changes;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics cache;
    std::vector<std::string> specialization_keys;
    std::string vcd;

    struct Point {
        fsim::runtime::simir::ProcessId process { };
        fsim::runtime::simir::ProcessId design_process { };
        fsim::runtime::simir::InstructionIndex instruction { };
        fsim::runtime::simir::ExecutionPointKind kind {
            fsim::runtime::simir::ExecutionPointKind::statement
        };
        fsim::runtime::simir::SourceLocation source;
        std::string scope;

        friend bool operator==(const Point&, const Point&) = default;
    };
    std::vector<Point> points;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-procedure-waits";
    config.project.top = "vhdl:work.procedure_waits(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "cache-o0"
                : "cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

Capture execute(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project);
    Capture capture;
    capture.specialization_keys = project->specialization_cache_keys;
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const std::array names {
        std::string_view { "procedure_waits.first_seen" },
        std::string_view { "procedure_waits.second_seen" }
    };
    std::array<fsim::runtime::simir::SignalId, names.size()> signals { };
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto signal = simulation.find_signal(names[index]);
        assert(signal);
        signals[index] = *signal;
    }
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.cache = simulation.native_cache_statistics();
    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd {
        vcd_output, simulation.time_resolution(), 64
    };
    std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals { };
    for (std::size_t index = 0; index < names.size(); ++index) {
        vcd_signals[index] = vcd.declare_signal(
            std::string { names[index] }, 1);
    }
    vcd.begin(simulation.now());
    for (std::size_t index = 0; index < signals.size(); ++index) {
        vcd.change(
            vcd_signals[index], simulation.read_signal(signals[index]));
    }
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t) {
            const auto found = std::ranges::find(signals, signal);
            if (found != signals.end()) {
                const auto index = static_cast<std::size_t>(
                    std::distance(signals.begin(), found));
                capture.changes.emplace_back(
                    index,
                    value.to_msb_string(), time);
                vcd.set_time(time);
                vcd.change(vcd_signals[index], value);
            }
        });
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler&,
            const fsim::runtime::simir::ExecutionPoint& point) {
            capture.points.push_back({ point.process,
                point.design_process,
                point.instruction,
                point.kind,
                point.source,
                point.scope });
        });
    capture.result = simulation.run();
    for (std::size_t index = 0; index < signals.size(); ++index) {
        capture.final_values[index] = simulation.read_signal(signals[index]).to_msb_string();
    }
    vcd.flush();
    capture.vcd = vcd_output.str();
    return capture;
}

void verify(const Capture& capture)
{
    assert(capture.result.status == fsim::runtime::RunStatus::completed);
    assert(capture.result.time == 4);
    assert((capture.final_values
        == std::array<std::string, 2> { "1", "1" }));
    assert((capture.changes
        == std::vector<std::tuple<
            std::size_t, std::string,
            fsim::runtime::SimulationTick>> {
            { 0, "0", 0 }, { 1, "0", 0 },
            { 0, "1", 1 }, { 1, "1", 4 } }));
    assert(capture.vcd.find("$timescale 1ns $end")
        != std::string::npos);
    assert(capture.vcd.find("#4") != std::string::npos);
    assert(std::ranges::any_of(
        capture.points,
        [](const Capture::Point& point) {
            return point.kind
                == fsim::runtime::simir::ExecutionPointKind::wait
                && point.source.path.ends_with("procedure_waits.vhd")
                && point.scope.find("child") != std::string::npos;
        }));
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vhdl-procedure-waits-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "procedure_waits.vhd";
    {
        // FSIM-CONFORMANCE CF-VHDL-CALLABLE-001 source=SRC-UVVM expectation=execute
        // FSIM-CONFORMANCE CF-VHDL-WAIT-001 source=SRC-IEEE-P1076 expectation=execute
        std::ofstream output(source);
        output << R"(
entity wait_child is
  generic (timeout : time := 3 ns);
  port (
    trigger : in boolean;
    first_seen : out std_logic;
    second_seen : out std_logic);
end entity;
architecture rtl of wait_child is
  procedure leaf is
  begin
    for index in 0 to 1 loop
      if index = 1 then
        wait on trigger until trigger for timeout;
      end if;
    end loop;
  end procedure;
  procedure middle is
  begin
    if true then
      leaf;
    else
      wait;
    end if;
  end procedure;
  procedure recurse(variable value : inout integer; depth : in integer) is
    variable retained_depth : integer := depth;
  begin
    if depth > 0 then
      recurse(value, depth - 1);
      value := value + retained_depth;
    end if;
  end procedure;
begin
  worker: process
    variable recursive_total : integer := 0;
  begin
    first_seen <= '0';
    second_seen <= '0';
    middle;
    first_seen <= '1';
    middle;
    second_seen <= '1';
    recurse(recursive_total, 4);
    assert recursive_total = 10
      report "recursive procedure inout copyout"
      severity failure;
    wait;
  end process;
end architecture;

entity procedure_waits is end entity;
architecture rtl of procedure_waits is
  constant from_fs : time := 1000000 fs;
  constant from_ps : time := 1000 ps;
  constant from_us : time := 1 us;
  constant from_ms : time := 1 ms;
  constant from_sec : time := 1 sec;
  constant from_min : time := 1 min;
  constant from_hr : time := 1 hr;
  constant base_period : time := from_fs + from_ps + 4 ns;
  constant half_period : time := base_period / 2;
  constant converted_period : time := time'(half_period);
  constant ordered : boolean := converted_period = 3 ns;
  constant fs_exact : boolean := from_fs = 1 ns;
  constant ps_exact : boolean := from_ps = 1 ns;
  constant us_exact : boolean := from_us = 1000 ns;
  constant ms_exact : boolean := from_ms = 1000 us;
  constant sec_exact : boolean := from_sec = 1000 ms;
  constant min_exact : boolean := from_min = 60 sec;
  constant hr_exact : boolean := from_hr = 60 min;
  signal trigger : boolean;
  signal first_seen : std_logic;
  signal second_seen : std_logic;
begin
  driver: process
  begin
    assert ordered report "time comparison" severity failure;
    assert fs_exact and ps_exact and us_exact and ms_exact
      and sec_exact and min_exact and hr_exact
      report "time units" severity failure;
    trigger <= false;
    wait for base_period / 6;
    trigger <= true;
    wait for half_period - 2 ns;
    trigger <= false;
    wait for 2 * 1 ns;
    trigger <= true;
    wait;
  end process;
  child: entity work.wait_child
    generic map (timeout => converted_period)
    port map (
      trigger => trigger,
      first_seen => first_seen,
      second_seen => second_seen);
end architecture;
)";
    }

    std::vector<std::string> baseline_o2_keys;
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto config = make_config(
            directory.path, source, optimization);
        const auto reference = execute(
            config, fsim::app::SimulationEngine::interpreter);
        const auto cold = execute(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = execute(
            config, fsim::app::SimulationEngine::compiled);
        const auto debug = execute(
            config, fsim::app::SimulationEngine::debug);
        verify(reference);
        verify(cold);
        verify(warm);
        verify(debug);
        for (const auto* candidate : { &cold, &warm, &debug }) {
            assert(candidate->result.status == reference.result.status);
            assert(candidate->result.time == reference.result.time);
            assert(candidate->final_values == reference.final_values);
            assert(candidate->changes == reference.changes);
            assert(candidate->points == reference.points);
            assert(candidate->vcd == reference.vcd);
            assert(candidate->specialization_keys
                == reference.specialization_keys);
        }
        assert(reference.specialization_keys.size() == 2);
        if (optimization == fsim::project::Optimization::o2) {
            baseline_o2_keys = reference.specialization_keys;
        }
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes == 2);
        assert(cold.compiled_modules == 2);
        assert(cold.cache.hits == 0);
        assert(cold.cache.misses == 2);
        assert(cold.cache.stores == 2);
        assert(warm.compiled_processes == 2);
        assert(warm.compiled_modules == 2);
        assert(warm.cache.hits == 2);
        assert(warm.cache.misses == 0);
        assert(debug.compiled_processes == 2);
        assert(debug.compiled_modules == 2);
#else
        assert(cold.compiled_processes == 0);
        assert(warm.compiled_processes == 0);
        assert(debug.compiled_processes == 0);
#endif
    }

    std::string edited_source;
    {
        std::ifstream input(source);
        edited_source.assign(
            std::istreambuf_iterator<char> { input },
            std::istreambuf_iterator<char> { });
    }
    const auto replace_once =
        [&](const std::string_view before,
            const std::string_view after) {
            const auto position = edited_source.find(before);
            assert(position != std::string::npos);
            edited_source.replace(position, before.size(), after);
        };
    replace_once(
        "from_fs + from_ps + 4 ns",
        "from_fs + from_ps + 8 ns");
    replace_once(
        "converted_period = 3 ns",
        "converted_period = 5 ns");
    {
        std::ofstream output(source, std::ios::trunc);
        output << edited_source;
        assert(output.good());
    }
    const auto edited_config = make_config(
        directory.path, source, fsim::project::Optimization::o2);
    const auto edited = execute(
        edited_config, fsim::app::SimulationEngine::compiled);
    assert(edited.result.status == fsim::runtime::RunStatus::completed);
    assert(edited.result.time == 6);
    assert((edited.final_values
        == std::array<std::string, 2> { "1", "1" }));
    assert((edited.changes
        == std::vector<std::tuple<
            std::size_t, std::string,
            fsim::runtime::SimulationTick>> {
            { 0, "0", 0 }, { 1, "0", 0 },
            { 0, "1", 1 }, { 1, "1", 6 } }));
    assert(edited.specialization_keys.size() == 2);
    assert(edited.specialization_keys != baseline_o2_keys);
#if defined(FSIM_HAS_LLVM)
    assert(edited.compiled_processes == 2);
    assert(edited.compiled_modules == 2);
    assert(edited.cache.hits == 0);
    assert(edited.cache.misses == 2);
    assert(edited.cache.stores == 2);
#endif
    assert(edited.vcd.find("#6") != std::string::npos);

    const auto auto_source = directory.path / "auto_time.vhd";
    {
        std::ofstream output(auto_source);
        output << R"(
entity auto_time is end entity;
architecture rtl of auto_time is
  signal done : std_logic;
begin
  process
  begin
    wait for 1 ps + 1 ps;
    done <= '1';
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }
    auto auto_config = make_config(
        directory.path,
        auto_source,
        fsim::project::Optimization::o0);
    auto_config.project.top = "vhdl:work.auto_time(rtl)";
    auto_config.project.time_resolution = "auto";
    fsim::diagnostic::Engine auto_diagnostics;
    auto auto_project = fsim::app::build_project(
        auto_config, auto_diagnostics);
    assert(auto_project);
    assert(auto_project->time_resolution == "1ps");
    fsim::app::Simulation auto_simulation {
        std::move(*auto_project),
        auto_config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter
    };
    const auto done = auto_simulation.find_signal("auto_time.done");
    assert(done);
    const auto auto_result = auto_simulation.run();
    assert(auto_result.status == fsim::runtime::RunStatus::completed);
    assert(auto_result.time == 2);
    assert(auto_simulation.read_signal(*done).to_msb_string() == "1");

    const auto expect_build_diagnostic =
        [&](const std::string_view filename,
            const std::string_view top,
            const std::string_view contents,
            const std::string_view code) {
            const auto negative_source = directory.path / filename;
            {
                std::ofstream output(negative_source);
                output << contents;
                assert(output.good());
            }
            auto config = make_config(
                directory.path,
                negative_source,
                fsim::project::Optimization::o0);
            config.project.top = std::string { top };
            fsim::diagnostic::Engine diagnostics;
            assert(!fsim::app::build_project(config, diagnostics));
            assert(std::ranges::any_of(
                diagnostics.diagnostics(),
                [&](const auto& diagnostic) {
                    return diagnostic.code == code;
                }));
        };
    expect_build_diagnostic(
        "inexact_time.vhd",
        "vhdl:work.inexact_time(rtl)",
        R"(
entity inexact_time is end entity;
architecture rtl of inexact_time is
  constant too_fine : time := 1 ps;
begin
  process begin wait for too_fine; end process;
end architecture;
)",
        "FSIM-ELAB-VHTIME-003");
    expect_build_diagnostic(
        "overflow_time.vhd",
        "vhdl:work.overflow_time(rtl)",
        R"(
entity overflow_time is end entity;
architecture rtl of overflow_time is
  constant too_large : time := 9223372036854775807 hr;
begin
  process begin wait; end process;
end architecture;
)",
        "FSIM-ELAB-VHTIME-002");
    expect_build_diagnostic(
        "nonstatic_time.vhd",
        "vhdl:work.nonstatic_time(rtl)",
        R"(
entity nonstatic_time is end entity;
architecture rtl of nonstatic_time is
begin
  process begin wait for missing_time; end process;
end architecture;
)",
        "FSIM-ELAB-VHTIME-001");
    std::cout << "VHDL procedure wait application tests passed\n";
    return 0;
}
