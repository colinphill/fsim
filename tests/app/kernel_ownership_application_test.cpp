// SPDX-License-Identifier: Apache-2.0
// A simulation built through the library API takes the debugger's static
// kernel plan whole: its signals are kernel-owned, as under
// `--engine debug`, and its results and per-signal change histories match
// the reference interpreter (docs/engine-v4-semantics.md S19).

#include "change_mismatch_report.hpp"
#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
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

struct Change {
    std::string signal;
    std::string value;
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };

    friend bool operator==(const Change&, const Change&) = default;
};

constexpr std::array<std::string_view, 12> observed {
    "kernel_ownership.clock",
    "kernel_ownership.go",
    "kernel_ownership.x",
    "kernel_ownership.y",
    "kernel_ownership.count",
    "kernel_ownership.sampled",
    "kernel_ownership.p",
    "kernel_ownership.q",
    "kernel_ownership.e",
    "kernel_ownership.rx",
    "kernel_ownership.ry",
    "kernel_ownership.rz",
};
/// observed[host_read_first] on: read by $monitor, a host process, or a
/// named event, whose trigger history the host keeps.
constexpr std::size_t host_read_first = 6U;

struct Capture {
    fsim::runtime::RunResult result;
    std::vector<Change> changes;
    /// The changes in the order the hook heard them, with their writers.
    std::vector<fsim::test::WrittenChange> written;
    std::vector<std::string> final_values;
    std::vector<bool> kernel_owned;
    /// Each time-0 change of x, y, p, q and e with the values the string `s`
    /// and the queue `dq` hold when the hook hears it, in the order the hook
    /// heard them.
    std::vector<std::string> string_at_change;
    std::string final_string;
    std::string final_queue;
};

std::string queue_text(const fsim::runtime::simir::ContainerValue& value)
{
    std::string text;
    for (const auto& element : value.elements) {
        text += text.empty() ? "" : ",";
        text += element.to_msb_string();
    }
    return text;
}

Capture run(const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message
                      << '\n';
        }
    }
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    std::array<fsim::runtime::simir::SignalId, observed.size()> signals { };
    for (std::size_t index = 0U; index < observed.size(); ++index) {
        const auto signal = simulation.find_signal(observed[index]);
        assert(signal);
        signals[index] = *signal;
    }
    const auto& strings = simulation.design().string_objects();
    const auto string_object = std::ranges::find_if(strings,
        [](const auto& object) { return object.name == "kernel_ownership.s"; });
    assert(string_object != strings.end());
    const auto string_id = string_object->id;
    const auto& containers = simulation.design().container_objects();
    const auto queue_object = std::ranges::find_if(containers,
        [](const auto& object) { return object.name == "kernel_ownership.dq"; });
    assert(queue_object != containers.end());
    const auto queue_id = queue_object->id;
    Capture capture;
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            for (std::size_t index = 0U; index < signals.size(); ++index) {
                if (signals[index] == signal) {
                    capture.changes.push_back({ std::string { observed[index] },
                        value.to_msb_string(), time, delta });
                    capture.written.push_back({ std::string { observed[index] },
                        value.to_msb_string(), time, delta,
                        simulation.signal_change_writer() });
                    if (time == 0U
                        && (index == 2U || index == 3U
                            || (index >= 6U && index <= 8U))) {
                        capture.string_at_change.push_back(
                            std::string { observed[index] } + '='
                            + value.to_msb_string() + " s="
                            + simulation.read_string_object(string_id) + " dq="
                            + queue_text(simulation.read_container_object(queue_id)));
                    }
                }
            }
        });
    capture.result = simulation.run(42);
    fsim::test::order_changes_by_delta_and_object(
        capture.changes, [](const Change& change) {
            return std::tie(change.time, change.delta, change.signal);
        });
    for (const auto signal : signals) {
        capture.final_values.push_back(
            simulation.read_signal(signal).to_msb_string());
        capture.kernel_owned.push_back(simulation.signal_kernel_owned(signal));
    }
    capture.final_string = simulation.read_string_object(string_id);
    capture.final_queue = queue_text(simulation.read_container_object(queue_id));
    return capture;
}

std::vector<std::string> history(
    const Capture& capture, const std::string_view signal)
{
    std::vector<std::string> values;
    for (const auto& change : capture.changes) {
        if (change.signal == signal && change.time == 0U) {
            values.push_back(change.value);
        }
    }
    return values;
}

// Sets, or with a null value unsets, a variable of this process.
void set_environment(const char* const name, const char* const value)
{
#if defined(_WIN32)
    (void)::_putenv_s(name, value == nullptr ? "" : value);
#else
    (void)(value == nullptr ? ::unsetenv(name) : ::setenv(name, value, 1));
#endif
}

fsim::project::Config fixture_config(const std::filesystem::path& directory,
    const std::filesystem::path& source, const std::string_view top)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = std::string { top };
    config.project.top = "sv:work." + std::string { top };
    config.project.time_resolution = "1ns";
    config.build.cache_path = directory / ("cache-" + std::string { top });
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

fsim::app::Simulation make_simulation(const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message
                      << '\n';
        }
    }
    assert(project);
    return fsim::app::Simulation { std::move(*project), config.run.max_deltas,
        engine };
}

/// A host service's report from a process's time step: the time and delta
/// it passed, and Simulation::now() and delta() read in its hook.
struct ServiceEvent {
    std::string what;
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };
    fsim::runtime::SimulationTick now { };
    std::uint64_t now_delta { };

    friend bool operator==(const ServiceEvent&, const ServiceEvent&) = default;
};

struct ServiceCapture {
    fsim::runtime::RunResult result;
    std::vector<ServiceEvent> events;
};

// `warp` false: FSIM_STATIC_KERNEL_TIME_WARP=0, the kernel hands every time
// step to the scheduler.
ServiceCapture run_services(const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine, const bool warp)
{
    set_environment("FSIM_STATIC_KERNEL_TIME_WARP", warp ? nullptr : "0");
    auto simulation = make_simulation(config, engine);
    ServiceCapture capture;
    simulation.set_output_hook(
        [&](const fsim::runtime::simir::ProcessId, const std::string_view text,
            const bool, const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            capture.events.push_back({ "output " + std::string { text }, time,
                delta, simulation.now(), simulation.delta() });
        });
    simulation.set_report_hook(
        [&](const fsim::runtime::simir::ProcessId, const std::string_view message,
            const fsim::runtime::simir::AssertionSeverity,
            const fsim::runtime::simir::SourceLocation&,
            const fsim::runtime::SimulationTick time, const std::uint64_t delta) {
            capture.events.push_back({ "report " + std::string { message }, time,
                delta, simulation.now(), simulation.delta() });
        });
    capture.result = simulation.run();
    set_environment("FSIM_STATIC_KERNEL_TIME_WARP", nullptr);
    return capture;
}

struct PointCapture {
    fsim::runtime::RunResult stopped;
    fsim::runtime::SimulationTick stopped_now { };
    /// (line, Simulation::now()) of the statement points of lines 6 and 8.
    std::vector<std::pair<std::uint32_t, fsim::runtime::SimulationTick>> points;
    std::vector<std::pair<std::string, fsim::runtime::SimulationTick>> outputs;
    fsim::runtime::RunResult resumed;
};

PointCapture run_points(const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine, const bool warp)
{
    set_environment("FSIM_STATIC_KERNEL_TIME_WARP", warp ? nullptr : "0");
    auto simulation = make_simulation(config, engine);
    PointCapture capture;
    simulation.set_output_hook(
        [&](const fsim::runtime::simir::ProcessId, const std::string_view text,
            const bool, const fsim::runtime::SimulationTick time,
            const std::uint64_t) {
            capture.outputs.emplace_back(std::string { text }, time);
        });
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::simir::ExecutionPoint& point) {
            if (point.kind != fsim::runtime::simir::ExecutionPointKind::statement
                || (point.source.line != 6U && point.source.line != 8U)) {
                return;
            }
            capture.points.emplace_back(point.source.line, simulation.now());
            if (point.source.line == 8U) {
                scheduler.request_stop();
            }
        });
    capture.stopped = simulation.run();
    capture.stopped_now = simulation.now();
    simulation.set_execution_point_hook({ });
    simulation.clear_stop();
    capture.resumed = simulation.run();
    set_environment("FSIM_STATIC_KERNEL_TIME_WARP", nullptr);
    return capture;
}

// A closed design runs as a kernel that may run its own time steps ahead of
// the scheduler (docs/engine-v4-semantics.md S10). The host services its
// process calls, and Simulation::now() and delta() read in their hooks,
// report the time of the step that calls them: a delay control resumes the
// process at the current time plus the delay (IEEE 1800-2023 9.4.1), so the
// $display runs at 3, the $readmemh at 7 (its error report, 21.4) and the
// failing immediate assertion's $error at 9 (16.3, 20.10).
void test_warped_host_services(const std::filesystem::path& directory)
{
    const auto source = directory / "warp_services.sv";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(timeunit 1ns / 1ns;
module warp_services;
  logic [7:0] memory [0:3];
  initial begin
    #3 $display("display");
    #4 $readmemh("absent.hex", memory);
    #2 assert (memory[0] === 8'd1) else $error("immediate");
    #1 $finish;
  end
endmodule
)";
        assert(output.good());
    }
    const auto config = fixture_config(directory, source, "warp_services");
    const auto reference = run_services(
        config, fsim::app::SimulationEngine::interpreter, true);
    assert(reference.result.time == 10U);
    const std::vector<std::pair<std::string, fsim::runtime::SimulationTick>>
        expected { { "output display", 3U },
            { "report cannot open read-memory file 'absent.hex'", 7U },
            { "report immediate", 9U } };
    assert(reference.events.size() == expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const auto& event = reference.events[index];
        assert(event.what == expected[index].first);
        assert(event.time == expected[index].second);
        assert(event.now == event.time && event.now_delta == event.delta);
    }
    for (const auto engine : { fsim::app::SimulationEngine::compiled,
             fsim::app::SimulationEngine::debug }) {
        for (const bool warp : { true, false }) {
            const auto actual = run_services(config, engine, warp);
            if (actual.events != reference.events) {
                for (const auto& event : actual.events) {
                    std::cerr << event.what << " time " << event.time << '+'
                              << event.delta << " now " << event.now << '+'
                              << event.now_delta << '\n';
                }
            }
            assert(actual.events == reference.events);
            assert(actual.result.status == reference.result.status);
            assert(actual.result.time == reference.result.time);
        }
    }
}

// A library client's execution-point hook sees each statement at its time and
// may stop the run there (application.hpp, Simulation): the $display of line
// 6 runs at 5 and the statement of line 8 at 9 (IEEE 1800-2023 9.4.1). A stop
// requested at line 8 ends run() at 9 with the statement not yet executed;
// the continued run executes it at 9 and finishes at 10 (20.2).
void test_warped_execution_point_stop(const std::filesystem::path& directory)
{
    const auto source = directory / "warp_points.sv";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(timeunit 1ns / 1ns;
module warp_points;
  initial begin
    #2;
    #3;
    $display("five");
    #4;
    $display("nine");
    #1 $finish;
  end
endmodule
)";
        assert(output.good());
    }
    const auto config = fixture_config(directory, source, "warp_points");
    const auto reference = run_points(
        config, fsim::app::SimulationEngine::interpreter, true);
    const std::vector<std::pair<std::uint32_t, fsim::runtime::SimulationTick>>
        expected_points { { 6U, 5U }, { 8U, 9U } };
    const std::vector<std::pair<std::string, fsim::runtime::SimulationTick>>
        expected_outputs { { "five", 5U }, { "nine", 9U } };
    assert(reference.stopped.status == fsim::runtime::RunStatus::stopped);
    assert(reference.stopped.time == 9U && reference.stopped_now == 9U);
    assert(reference.points == expected_points);
    assert(reference.outputs == expected_outputs);
    assert(reference.resumed.time == 10U);
    for (const auto engine : { fsim::app::SimulationEngine::compiled,
             fsim::app::SimulationEngine::debug }) {
        for (const bool warp : { true, false }) {
            const auto actual = run_points(config, engine, warp);
            if (actual.points != reference.points
                || actual.stopped_now != reference.stopped_now) {
                std::cerr << "stopped at " << actual.stopped.time << " now "
                          << actual.stopped_now << '\n';
                for (const auto& [line, now] : actual.points) {
                    std::cerr << "line " << line << " now " << now << '\n';
                }
            }
            assert(actual.stopped.status == reference.stopped.status);
            assert(actual.stopped.time == reference.stopped.time);
            assert(actual.stopped_now == reference.stopped_now);
            assert(actual.points == reference.points);
            assert(actual.outputs == reference.outputs);
            assert(actual.resumed.status == reference.resumed.status);
            assert(actual.resumed.time == reference.resumed.time);
        }
    }
}

} // namespace

int main()
{
    const auto nonce
        = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory { std::filesystem::temp_directory_path()
        / ("fsim-kernel-ownership-test-" + std::to_string(nonce)) };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "kernel_ownership.sv";
    {
        std::ofstream output(source, std::ios::binary);
        // `x`, `y`, `p` and `q` take two nonblocking updates at time 0,
        // performed in the order their statements executed (IEEE 1800-2023
        // 4.6 b)): the procedures that execute later come first in the
        // source. `p` and `q` are also read by $monitor, a host process, and
        // each procedure writes them between and after the others. The
        // string `s`, the queue `dq` and the named event `e` take nonblocking
        // updates (10.4.2) and a nonblocking trigger (`->>`, 15.5.1) between
        // them, which the NBA region performs in the same order.
        output << R"(timeunit 1ns / 1ns;
module kernel_ownership;
  logic clock = 1'b0;
  logic go = 1'b0;
  logic [3:0] x;
  logic [3:0] y;
  logic [3:0] p;
  logic [3:0] q;
  logic [3:0] count = 4'd0;
  logic [3:0] sampled = 4'd0;
  string s = "i";
  event e;
  logic [3:0] dq[$] = '{4'd0};
  logic [3:0] rx;
  logic [3:0] ry;
  logic [3:0] rz;

  always #5 clock = ~clock;
  always @(posedge clock) count <= count + 4'd1;
  always @(posedge clock) sampled <= count;

  initial begin
    #0;
    #0;
    #0;
    rx <= 4'd1;
    ry <= 4'd2;
    rz <= 4'd3;
    rx <= 4'd6;
  end
  always @(go) begin
    p <= 4'd2;
    s <= "c";
    x <= 4'd2;
    dq <= '{4'd5, 4'd2};
  end
  initial begin
    #0;
    #0;
    y <= 4'd7;
    q <= 4'd7;
  end
  initial begin
    #0;
    x <= 4'd1;
    s <= "a";
    p <= 4'd1;
    ->> e;
    dq <= '{4'd5};
    y <= 4'd6;
    s <= "b";
    q <= 4'd6;
    go = 1'b1;
  end
  initial $monitor("p=%0d q=%0d", p, q);
endmodule
)";
        assert(output.good());
    }

    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "kernel-ownership";
    config.project.top = "sv:work.kernel_ownership";
    config.project.time_resolution = "1ns";
    config.build.cache_path = directory.path / "cache";
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    const auto reference
        = run(config, fsim::app::SimulationEngine::interpreter);
    assert(reference.result.status == fsim::runtime::RunStatus::time_limit);
    assert(reference.result.time == 42U);
    assert((reference.final_values
        == std::vector<std::string> {
            "0", "1", "0010", "0111", "0100", "0011", "0010", "0111", "1", "0110", "0010",
            "0011" }));
    assert((history(reference, "kernel_ownership.x")
        == std::vector<std::string> { "0001", "0010" }));
    assert((history(reference, "kernel_ownership.y")
        == std::vector<std::string> { "0110", "0111" }));
    assert((history(reference, "kernel_ownership.p")
        == std::vector<std::string> { "0001", "0010" }));
    assert((history(reference, "kernel_ownership.q")
        == std::vector<std::string> { "0110", "0111" }));
    assert((history(reference, "kernel_ownership.e")
        == std::vector<std::string> { "1" }));
    assert((history(reference, "kernel_ownership.rx")
        == std::vector<std::string> { "0001", "0110" }));
    // `rx <= 1; ry <= 2; rz <= 3; rx <= 6;` updates rx, ry, rz, rx in that
    // order (IEEE 1800-2023 4.6 b)): the second update of rx comes after
    // the others, not with the first.
    const auto rewrite_order = [](const Capture& capture) {
        std::vector<std::string> order;
        for (const auto& change : capture.written) {
            if (change.time == 0U
                && change.signal.starts_with("kernel_ownership.r")) {
                order.push_back(change.signal + '=' + change.value);
            }
        }
        return order;
    };
    const std::vector<std::string> expected_rewrite {
        "kernel_ownership.rx=0001",
        "kernel_ownership.ry=0010",
        "kernel_ownership.rz=0011",
        "kernel_ownership.rx=0110",
    };
    assert(rewrite_order(reference) == expected_rewrite);
    // The last initial procedure's NBA updates and trigger land in the order
    // its statements executed, then the always procedure's (which executed
    // after it, woken by `go = 1`), then the other initial procedure's (from
    // its second #0, Inactive; IEEE 1800-2023 4.4.2.3, 4.6 b)); each string
    // and queue update lands between its neighbours, so the hook of each
    // signal's update reads the string and queue the updates before it left.
    const std::vector<std::string> expected_strings {
        "kernel_ownership.x=0001 s=i dq=0000",
        "kernel_ownership.p=0001 s=a dq=0000",
        "kernel_ownership.e=1 s=a dq=0000",
        "kernel_ownership.y=0110 s=a dq=0101",
        "kernel_ownership.q=0110 s=b dq=0101",
        "kernel_ownership.p=0010 s=b dq=0101",
        "kernel_ownership.x=0010 s=c dq=0101",
        "kernel_ownership.y=0111 s=c dq=0101,0010",
        "kernel_ownership.q=0111 s=c dq=0101,0010",
    };
    if (reference.string_at_change != expected_strings) {
        for (const auto& line : reference.string_at_change) {
            std::cerr << "kernel_ownership reference heard " << line << '\n';
        }
    }
    assert(reference.string_at_change == expected_strings);
    assert(reference.final_string == "c");
    assert(reference.final_queue == "0101,0010");
    for (const bool owned : reference.kernel_owned) {
        assert(!owned);
    }

    for (const auto engine : { fsim::app::SimulationEngine::compiled,
             fsim::app::SimulationEngine::debug }) {
        const auto actual = run(config, engine);
#if defined(FSIM_HAS_LLVM)
        // The library API keeps no signal on the host that only kernel
        // processes read: those live in the kernel, as under the debugger.
        for (std::size_t index = 0U; index < host_read_first; ++index) {
            if (!actual.kernel_owned[index]) {
                std::cerr << observed[index] << " is not kernel-owned\n";
            }
            assert(actual.kernel_owned[index]);
        }
#endif
        assert(actual.result.status == reference.result.status);
        assert(actual.result.time == reference.result.time);
        fsim::test::report_change_mismatch(
            "kernel_ownership", reference.changes, actual.changes);
        assert(actual.changes == reference.changes);
        // `x <= 4'd1; p <= 4'd1; ->> e; y <= 4'd6; q <= 4'd6;` of one
        // procedure update in that order in every engine (IEEE 1800-2023 4.6
        // b)), as do `p <= 4'd2; x <= 4'd2;` and `y <= 4'd7; q <= 4'd7;`,
        // whether the signal lives in the kernel or is a host signal, and
        // `rx <= 1; ry <= 2; rz <= 3; rx <= 6;`: the ten pairs of the first,
        // one pair of each of the next two and the six of the last.
        std::size_t compared { };
        assert(fsim::test::same_writer_order_matches("kernel_ownership",
            reference.written, actual.written, compared));
        assert(compared >= 18U);
        assert(actual.final_values == reference.final_values);
        // The string's and queue's updates land among them in the same
        // order.
        if (actual.string_at_change != expected_strings) {
            for (const auto& line : actual.string_at_change) {
                std::cerr << "kernel_ownership heard " << line << '\n';
            }
        }
        assert(actual.string_at_change == expected_strings);
        assert(actual.final_string == "c");
        assert(actual.final_queue == reference.final_queue);
        assert(rewrite_order(actual) == expected_rewrite);
    }
    test_warped_host_services(directory.path);
    test_warped_execution_point_stop(directory.path);
    std::cout << "kernel ownership application tests passed\n";
    return 0;
}
