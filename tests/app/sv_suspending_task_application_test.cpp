// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <system_error>
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
    struct Change {
        fsim::runtime::SimulationTick time { };
        std::uint64_t delta { };
        std::string value;

        bool operator==(const Change&) const = default;
    };

    fsim::runtime::RunResult result;
    std::array<std::string, 7> values;
    std::vector<std::string> keys;
    std::vector<std::string> locals;
    std::vector<fsim::runtime::simir::ExecutionPoint> points;
    std::vector<Change> result_changes;
    std::size_t compiled_processes { };
    fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sv-suspending-tasks";
    config.project.top = "sv:work.task_suspend_top";
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
    sources.compilation_unit = "file";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));
    return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);

    Capture capture;
    capture.keys = project->specialization_cache_keys;
    assert(project->design.processes().size() == 2);
    for (const auto& process : project->design.processes()) {
        for (const auto& local : process.debug_locals) {
            capture.locals.push_back(local.name);
        }
    }

    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    capture.compiled_processes = simulation.compiled_process_count();
    capture.cache = simulation.native_cache_statistics();
    simulation.set_execution_point_hook(
        [&capture](
            fsim::runtime::Scheduler&,
            const fsim::runtime::simir::ExecutionPoint& point) {
            capture.points.push_back(point);
        });

    const auto result = simulation.find_signal("task_suspend_top.result");
    const auto total = simulation.find_signal("task_suspend_top.total");
    const auto early = simulation.find_signal("task_suspend_top.early");
    const auto recursive_a = simulation.find_signal("task_suspend_top.recursive_a");
    const auto recursive_a_total = simulation.find_signal("task_suspend_top.recursive_a_total");
    const auto recursive_b = simulation.find_signal("task_suspend_top.recursive_b");
    const auto recursive_b_total = simulation.find_signal("task_suspend_top.recursive_b_total");
    assert(
        result && total && early && recursive_a && recursive_a_total
        && recursive_b && recursive_b_total);
    simulation.set_signal_change_hook(
        [&capture, result](
            const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            if (signal == *result) {
                capture.result_changes.push_back(
                    Capture::Change {
                        time, delta, value.to_msb_string() });
            }
        });
    capture.result = simulation.run();
    capture.values = {
        simulation.read_signal(*result).to_msb_string(),
        simulation.read_signal(*total).to_msb_string(),
        simulation.read_signal(*early).to_msb_string(),
        simulation.read_signal(*recursive_a).to_msb_string(),
        simulation.read_signal(*recursive_a_total).to_msb_string(),
        simulation.read_signal(*recursive_b).to_msb_string(),
        simulation.read_signal(*recursive_b_total).to_msb_string()
    };
    return capture;
}

void verify(
    const Capture& capture,
    const std::array<std::string, 7>& expected)
{
    assert(
        capture.result.status
        == fsim::runtime::RunStatus::completed);
    assert(capture.result.time == 6);
    assert(capture.values == expected);
    assert(std::ranges::find(
               capture.locals, "outer.temporary")
        != capture.locals.end());
    assert(std::ranges::find(
               capture.locals, "delayed_transform.value")
        != capture.locals.end());
    const auto call_points = std::ranges::count_if(
        capture.points,
        [](const auto& point) {
            return point.kind
                == fsim::runtime::simir::ExecutionPointKind::call;
        });
    // Observable native execution retains the same source-level callable
    // boundaries as interpreted execution.
    assert(call_points >= 9);
    const auto wait_points = std::ranges::count_if(
        capture.points,
        [](const auto& point) {
            return point.kind
                == fsim::runtime::simir::ExecutionPointKind::wait;
        });
    assert(wait_points >= 6);
    assert(std::ranges::count_if(
               capture.points,
               [](const auto& point) {
                   return point.kind
                       == fsim::runtime::simir::
                           ExecutionPointKind::process_suspend;
               })
        >= 7);
    assert(capture.result_changes.size() == 2);
    assert(capture.result_changes.front().time == 1);
    assert(capture.result_changes.front().value == "00000000");
    assert(capture.result_changes.back().time == 6);
    assert(capture.result_changes.back().value == expected[0]);
}

bool same_points(
    const std::vector<
        fsim::runtime::simir::ExecutionPoint>& left,
    const std::vector<
        fsim::runtime::simir::ExecutionPoint>& right)
{
    return std::ranges::equal(
        left,
        right,
        [](const auto& first, const auto& second) {
            return first.process == second.process
                && first.instruction == second.instruction
                && first.kind == second.kind
                && first.source == second.source;
        });
}

void verify_suspended_locals(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };

    std::optional<fsim::runtime::simir::ProcessId> process_id;
    std::optional<std::size_t> package_value;
    std::optional<std::size_t> outer_value;
    for (std::size_t id = 0;
         id < simulation.design_ir().processes().size(); ++id) {
        const auto& process = simulation.process_program(
            static_cast<fsim::runtime::simir::ProcessId>(id));
        for (std::size_t index = 0;
            index < process.debug_locals.size(); ++index) {
            const auto& name = process.debug_locals[index].name;
            if (name == "delayed_transform.value") {
                process_id = process.id;
                package_value = index;
            } else if (name == "outer.value") {
                outer_value = index;
            }
        }
    }
    assert(process_id && package_value && outer_value);

    std::size_t task_waits = 0;
    bool armed = false;
    bool requested = false;
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::simir::ExecutionPoint& point) {
            if (point.process != *process_id) {
                return;
            }
            if (point.kind
                == fsim::runtime::simir::
                    ExecutionPointKind::wait) {
                ++task_waits;
                armed = task_waits == 2;
            } else if (
                armed && !requested
                && point.kind
                    == fsim::runtime::simir::
                        ExecutionPointKind::process_suspend) {
                requested = true;
                scheduler.request_stop();
            }
        });
    const auto suspended = simulation.run();
    assert(requested);
    assert(
        suspended.status
        == fsim::runtime::RunStatus::stopped);
    assert(suspended.time == 2);
    assert(
        simulation
            .read_process_local(*process_id, *package_value)
            .to_msb_string()
        == "00101000");
    assert(
        simulation
            .read_process_local(*process_id, *outer_value)
            .to_msb_string()
        == "00101000");

    simulation.clear_stop();
    const auto resumed = simulation.run();
    assert(
        resumed.status
        == fsim::runtime::RunStatus::completed);
    assert(resumed.time == 6);
    const auto result = simulation.find_signal("task_suspend_top.result");
    assert(result);
    assert(
        simulation.read_signal(*result).to_msb_string()
        == "00101010");
}

void verify_reference_activation(
    const std::filesystem::path& directory)
{
    const auto source = directory / "reference_activation.sv";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(`timescale 1ns/1ns
module reference_activation;
  logic [7:0] value;
  logic [7:0] function_result;
  logic [7:0] first_result;
  logic [7:0] second_result;
  logic [15:0] packed_value;
  logic [15:0] selected_result;
  integer selected;

  function automatic logic [7:0] bump(
      ref logic [7:0] target, input logic [7:0] amount);
    target = target + amount;
    return target;
  endfunction

  task automatic delayed_bump(
      ref logic [7:0] target, input logic [7:0] amount);
    target = target + amount;
    #1;
    target = target + amount;
  endtask

  task automatic delayed_part(ref logic [7:0] target);
    target = target + 1;
    #1;
    selected = 1;
    #1;
    target = target + 1;
  endtask

  initial begin
    value = 8'd1;
    function_result = bump(value, 8'd2);
    delayed_bump(value, 8'd3);
    first_result = value;
    delayed_bump(value, 8'd1);
    second_result = value;
  end
  initial begin
    packed_value = 16'h0102;
    selected = 0;
    delayed_part(packed_value[selected * 8 +: 8]);
    selected_result = packed_value;
  end
endmodule
)";
        assert(output.good());
    }

    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        auto config = make_config(directory, source, optimization);
        config.project.top = "sv:work.reference_activation";
        for (const auto engine :
            { fsim::app::SimulationEngine::interpreter,
                fsim::app::SimulationEngine::compiled,
                fsim::app::SimulationEngine::compiled }) {
            fsim::diagnostic::Engine diagnostics;
            auto project = fsim::app::build_project(config, diagnostics);
            if (!project) {
                for (const auto& diagnostic : diagnostics.diagnostics()) {
                    std::cerr << diagnostic.code << ": "
                              << diagnostic.message << '\n';
                }
            }
            assert(project);
            fsim::app::Simulation simulation {
                std::move(*project), config.run.max_deltas, engine
            };
            const auto value = simulation.find_signal(
                "reference_activation.value");
            const auto function_result = simulation.find_signal(
                "reference_activation.function_result");
            const auto first_result = simulation.find_signal(
                "reference_activation.first_result");
            const auto second_result = simulation.find_signal(
                "reference_activation.second_result");
            const auto packed_value = simulation.find_signal(
                "reference_activation.packed_value");
            const auto selected_result = simulation.find_signal(
                "reference_activation.selected_result");
            assert(
                value && function_result && first_result && second_result
                && packed_value && selected_result);
            const auto result = simulation.run();
            assert(
                result.status == fsim::runtime::RunStatus::completed
                && result.time == 2);
            assert(
                simulation.read_signal(*value).to_msb_string() == "00001011"
                && simulation.read_signal(*function_result).to_msb_string()
                    == "00000011"
                && simulation.read_signal(*first_result).to_msb_string()
                    == "00001001"
                && simulation.read_signal(*second_result).to_msb_string()
                    == "00001011"
                && simulation.read_signal(*packed_value).to_msb_string()
                    == "0000000100000100"
                && simulation.read_signal(*selected_result).to_msb_string()
                    == "0000000100000100");
        }
    }
}

void verify_reference_failure(
    const std::filesystem::path& directory)
{
    const auto source = directory / "reference_failure.sv";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(`timescale 1ns/1ns
module reference_failure;
  logic [7:0] value;
  integer invalid_delay;
  task automatic fail(ref logic [7:0] target);
    target = 8'hff;
    #(invalid_delay);
    target = 8'h00;
  endtask
  initial begin
    value = 8'h07;
    invalid_delay = -1;
    fail(value);
  end
endmodule
)";
        assert(output.good());
    }

    for (const auto engine :
        { fsim::app::SimulationEngine::interpreter,
            fsim::app::SimulationEngine::compiled }) {
        auto config = make_config(
            directory, source, fsim::project::Optimization::o2);
        config.project.top = "sv:work.reference_failure";
        fsim::diagnostic::Engine diagnostics;
        auto project = fsim::app::build_project(config, diagnostics);
        if (!project) {
            for (const auto& diagnostic : diagnostics.diagnostics()) {
                std::cerr << diagnostic.code << ": "
                          << diagnostic.message << '\n';
            }
        }
        assert(project);
        fsim::app::Simulation simulation {
            std::move(*project), config.run.max_deltas, engine
        };
        const auto value = simulation.find_signal(
            "reference_failure.value");
        assert(value);
        bool rejected = false;
        try {
            (void)simulation.run();
        } catch (const std::exception& error) {
            rejected = std::string_view { error.what() }.find("cannot be negative")
                != std::string_view::npos;
        }
        assert(rejected);
        assert(
            simulation.read_signal(*value).to_msb_string() == "00000111");
    }
}

void verify_static_callable_lifetime(
    const std::filesystem::path& directory)
{
    const auto source = directory / "static_callable_lifetime.sv";
    {
        std::ofstream output(source, std::ios::binary);
        output << R"(`timescale 1ns/1ns
module static_callable_lifetime;
  logic [7:0] function_first;
  logic [7:0] function_second;
  logic [7:0] task_first;
  logic [7:0] task_second;

  function logic [7:0] retained_function;
    begin : outer
      string text = "A";
      logic [7:0] memory[1:0] = '{8'd3, 8'd2};
      begin : inner
        logic [7:0] count = 0;
        count = count + 1;
        memory[0] = memory[0] + 1;
        text = {text, "B"};
        retained_function = count + memory[0] + text.len();
      end
    end
  endfunction

  task retained_task(output logic [7:0] result);
    begin : scope
      string text = "T";
      logic [7:0] memory[1:0] = '{8'd5, 8'd4};
      logic [7:0] count = 0;
      count = count + 1;
      memory[0] = memory[0] + 1;
      text = {text, "X"};
      #1;
      result = count + memory[0] + text.len();
    end
  endtask

  initial begin
    function_first = retained_function();
    function_second = retained_function();
    retained_task(task_first);
    retained_task(task_second);
  end
endmodule
)";
        assert(output.good());
    }

    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        auto config = make_config(directory, source, optimization);
        config.project.top = "sv:work.static_callable_lifetime";
        for (const auto engine :
            { fsim::app::SimulationEngine::interpreter,
                fsim::app::SimulationEngine::compiled,
                fsim::app::SimulationEngine::compiled }) {
            fsim::diagnostic::Engine diagnostics;
            auto project = fsim::app::build_project(config, diagnostics);
            if (!project) {
                for (const auto& diagnostic : diagnostics.diagnostics()) {
                    std::cerr << diagnostic.code << ": "
                              << diagnostic.message << '\n';
                }
            }
            assert(project);
            fsim::app::Simulation simulation {
                std::move(*project), config.run.max_deltas, engine
            };
            std::optional<fsim::runtime::simir::ProcessId> process_id;
            std::optional<std::size_t> function_text;
            std::optional<std::size_t> task_text;
            std::optional<std::size_t> function_memory;
            std::optional<std::size_t> task_memory;
            bool function_count = false;
            bool task_count = false;
            for (std::size_t id = 0;
                 id < simulation.design_ir().processes().size(); ++id) {
                const auto& process = simulation.process_program(
                    static_cast<fsim::runtime::simir::ProcessId>(id));
                for (std::size_t index = 0;
                    index < process.debug_string_locals.size(); ++index) {
                    const auto& name = process.debug_string_locals[index].name;
                    if (name == "retained_function.outer.text") {
                        process_id = process.id;
                        function_text = index;
                    } else if (name == "retained_task.scope.text") {
                        process_id = process.id;
                        task_text = index;
                    }
                }
                for (std::size_t index = 0;
                    index < process.debug_container_locals.size(); ++index) {
                    const auto& name = process.debug_container_locals[index].name;
                    if (name == "retained_function.outer.memory") {
                        process_id = process.id;
                        function_memory = index;
                    } else if (name == "retained_task.scope.memory") {
                        process_id = process.id;
                        task_memory = index;
                    }
                }
                function_count = function_count
                    || std::ranges::any_of(
                        process.debug_locals,
                        [](const auto& local) {
                            return local.name
                                == "retained_function.outer.inner.count";
                        });
                task_count = task_count
                    || std::ranges::any_of(
                        process.debug_locals,
                        [](const auto& local) {
                            return local.name
                                == "retained_task.scope.count";
                        });
            }
            assert(
                process_id && function_text && task_text
                && function_memory && task_memory
                && function_count && task_count);
            const auto function_first = simulation.find_signal(
                "static_callable_lifetime.function_first");
            const auto function_second = simulation.find_signal(
                "static_callable_lifetime.function_second");
            const auto task_first = simulation.find_signal(
                "static_callable_lifetime.task_first");
            const auto task_second = simulation.find_signal(
                "static_callable_lifetime.task_second");
            assert(
                function_first && function_second
                && task_first && task_second);
            const auto result = simulation.run();
            assert(
                result.status == fsim::runtime::RunStatus::completed
                && result.time == 2);
            assert(
                simulation.read_signal(*function_first).low_word().aval == 6
                && simulation.read_signal(*function_second).low_word().aval == 9
                && simulation.read_signal(*task_first).low_word().aval == 8
                && simulation.read_signal(*task_second).low_word().aval == 11);
            assert(
                simulation.read_process_string_local(
                    *process_id, *function_text)
                    == "ABB"
                && simulation.read_process_string_local(
                       *process_id, *task_text)
                    == "TXX");
            const auto retained_function_memory = simulation.read_process_container_local(
                *process_id, *function_memory);
            const auto retained_task_memory = simulation.read_process_container_local(
                *process_id, *task_memory);
            assert(
                retained_function_memory.elements[1].low_word().aval == 4
                && retained_task_memory.elements[1].low_word().aval == 6);
        }
    }
}

} // namespace

int main()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-sv-suspending-tasks-"
            + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "tasks.sv";

    const auto write_source =
        [&](const unsigned increment) {
            std::ofstream output(
                source, std::ios::binary | std::ios::trunc);
            output
                << "`timescale 1ns/1ns\n"
                << "package suspend_pkg;\n"
                << "  task automatic delayed_transform(\n"
                << "      input logic [7:0] value,\n"
                << "      output logic [7:0] transformed);\n"
                << "    #2;\n"
                << "    transformed = value + "
                << increment << ";\n"
                << "  endtask\n"
                << "endpackage\n"
                << R"(
module task_suspend_top;
  import suspend_pkg::*;
  event kick;
  logic ready;
  logic launch;
  logic [7:0] result;
  logic [7:0] total;
  logic [7:0] early;
  logic [7:0] recursive_a;
  logic [7:0] recursive_a_total;
  logic [7:0] recursive_b;
  logic [7:0] recursive_b_total;

  task automatic recursive_delay(
      input int depth,
      input logic [7:0] seed,
      output logic [7:0] observed,
      inout logic [7:0] accumulator);
    logic [7:0] nested;
    #1;
    if (depth == 0) begin
      observed = seed;
      accumulator = accumulator + seed;
      return;
    end
    recursive_delay(depth - 1, seed + 1, nested, accumulator);
    observed = nested + 1;
    accumulator = accumulator + 1;
  endtask

  task automatic outer(
      input logic [7:0] value,
      output logic [7:0] transformed,
      inout logic [7:0] accumulator);
    logic [7:0] temporary;
    #1;
    ->> #3 kick;
    delayed_transform(value, temporary);
    @(kick);
    wait (ready);
    transformed = temporary;
    accumulator = accumulator + temporary;
    return;
    transformed = 0;
    accumulator = 0;
  endtask

  always @(posedge launch) begin
    result = 0;
    total = 1;
    outer(40, result, total);
  end

  initial begin
    launch = 0;
    ready = 0;
    early = 8'hff;
    recursive_a = 0;
    recursive_a_total = 0;
    recursive_b = 0;
    recursive_b_total = 0;
    fork
      recursive_delay(3, 8, recursive_a, recursive_a_total);
      recursive_delay(2, 20, recursive_b, recursive_b_total);
    join_none
    #1 launch = 1;
    #4;
    early = result;
    #1 ready <= 1;
  end
endmodule
)";
            assert(output.good());
        };

    write_source(2);
    verify_reference_activation(directory.path);
    verify_reference_failure(directory.path);
    verify_static_callable_lifetime(directory.path);
    std::vector<std::string> baseline_keys;
    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        const auto config = make_config(directory.path, source, optimization);
        const auto reference = run_once(config, fsim::app::SimulationEngine::interpreter);
        const auto cold = run_once(config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_once(config, fsim::app::SimulationEngine::compiled);
        const std::array<std::string, 7> expected {
            "00101010", "00101011", "00000000",
            "00001110", "00001110", "00011000", "00011000"
        };
        verify(reference, expected);
        verify(cold, expected);
        verify(warm, expected);
        assert(reference.keys == cold.keys);
        assert(cold.keys == warm.keys);
        assert(reference.result_changes == cold.result_changes);
        assert(cold.result_changes == warm.result_changes);
        assert(same_points(cold.points, warm.points));
        if (optimization == fsim::project::Optimization::o2) {
            baseline_keys = warm.keys;
        }
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes == 2);
        assert(cold.cache.misses == 1);
        assert(cold.cache.stores == 1);
        assert(warm.cache.hits == 1);
#else
        assert(cold.compiled_processes == 0);
#endif
        verify_suspended_locals(
            config, fsim::app::SimulationEngine::interpreter);
        verify_suspended_locals(
            config, fsim::app::SimulationEngine::debug);
    }

    write_source(3);
    const auto changed = run_once(
        make_config(
            directory.path,
            source,
            fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::compiled);
    verify(
        changed,
        { "00101011", "00101100", "00000000",
            "00001110", "00001110", "00011000", "00011000" });
    assert(baseline_keys.size() == 1);
    assert(changed.keys.size() == 1);
    assert(changed.keys.front() != baseline_keys.front());
#if defined(FSIM_HAS_LLVM)
    assert(changed.cache.hits == 0);
    assert(changed.cache.misses == 1);
    assert(changed.cache.stores == 1);
#endif
    return 0;
}
