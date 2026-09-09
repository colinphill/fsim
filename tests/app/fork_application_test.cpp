// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
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
    std::string value;
    std::string lifecycle;
    std::string local;
    std::string automatic_capture;
    std::string vcd;
    std::vector<fsim::runtime::simir::ExecutionPoint> points;
    bool child_debug_safe { };
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
    config.project.name = "fork-processes";
    config.project.top = "sv:work.fork_processes";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "cache-o0"
                : "cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
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
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "fork-runtime", diagnostics);
    assert(restored && !diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
        == encoded);
    assert(std::ranges::any_of(
        restored->processes(), [](const auto& process) {
            return std::ranges::any_of(
                process.operations, [&](const auto& operation) {
                    const auto* disable = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::DisableFork>(
                        &operation);
                    return disable && disable->site
                        && *disable->site < process.operations.size()
                        && fsim::runtime::simir::operation_holds<
                            fsim::runtime::simir::Fork>(
                            process.operations[*disable->site]);
                });
        }));
    assert(std::ranges::any_of(
        restored->processes(), [](const auto& process) {
            return std::ranges::any_of(
                process.operations, [&](const auto& operation) {
                    const auto* disable = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::DisableBlock>(
                        &operation);
                    return disable
                        && disable->begin < disable->end
                        && disable->end <= process.operations.size();
                });
        }));
    project->design = std::move(*restored);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const auto result = simulation.find_signal("fork_processes.result");
    assert(result);
    const auto lifecycle = simulation.find_signal("fork_processes.lifecycle");
    assert(lifecycle);
    const auto automatic_capture = simulation.find_signal(
        "fork_processes.automatic_capture");
    assert(automatic_capture);
    const auto& process = simulation.process_program(0U);
    const auto local = std::find_if(
        process.debug_locals.begin(), process.debug_locals.end(),
        [](const auto& value) { return value.name == "root.shared"; });
    assert(local != process.debug_locals.end());
    const auto local_index = static_cast<std::size_t>(
        std::distance(process.debug_locals.begin(), local));

    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.cache = simulation.native_cache_statistics();
    std::ostringstream vcd_text;
    fsim::runtime::VcdWriter vcd { vcd_text, "1ns", 8 };
    const auto trace = vcd.declare_signal("fork_processes.result", 8);
    vcd.begin(simulation.now());
    vcd.change(trace, simulation.read_signal(*result));
    simulation.set_signal_change_hook(
        [&](const auto signal,
            const fsim::runtime::PackedLogic4& value,
            const auto time,
            const auto) {
            if (signal == *result) {
                vcd.set_time(time);
                vcd.change(trace, value);
            }
        });
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler&,
            const fsim::runtime::simir::ExecutionPoint& point) {
            capture.points.push_back(point);
            if (point.process != point.design_process) {
                assert(point.design_process == 0);
                assert(!simulation.process_program(
                    point.design_process).name.empty());
                (void)simulation.read_process_local(
                    point.process, local_index);
                capture.child_debug_safe = true;
            }
            return false;
        });
    capture.result = simulation.run();
    capture.value = simulation.read_signal(*result).to_msb_string();
    capture.lifecycle = simulation.read_signal(*lifecycle).to_msb_string();
    capture.local = simulation.read_process_local(0, local_index).to_msb_string();
    capture.automatic_capture
        = simulation.read_signal(*automatic_capture).to_msb_string();
    vcd.flush();
    capture.vcd = vcd_text.str();
    return capture;
}

void test_optimization(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto config = make_config(directory, source, optimization);
    const auto reference = execute(config, fsim::app::SimulationEngine::interpreter);
    const auto cold = execute(config, fsim::app::SimulationEngine::compiled);
    const auto warm = execute(config, fsim::app::SimulationEngine::compiled);
    for (const auto* capture : { &reference, &cold, &warm }) {
        assert(
            capture->result.status == fsim::runtime::RunStatus::stopped
            && capture->result.time == 15
            && capture->value == "10111111"
            && capture->lifecycle == "01111100011111110010000011"
            && capture->local == "10111111"
            && capture->automatic_capture == "1010010100111100"
            && capture->child_debug_safe);
        assert(std::any_of(
            capture->points.begin(), capture->points.end(),
            [](const auto& point) { return point.process > 0; }));
    }
    assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 1);
    assert(cold.cache.hits == 0 && cold.cache.misses == 1);
    assert(warm.cache.hits == 1 && warm.cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
#endif
    assert(reference.vcd == cold.vcd && cold.vcd == warm.vcd);
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-fork-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "fork_processes.sv";
    {
        std::ofstream output(source);
        output << R"(
module fork_processes;
  logic [7:0] result;
  logic [25:0] lifecycle;
  logic [15:0] automatic_capture;
  logic shadow_regression;
  logic launch_result;
  process handle;
  process killed_handle;
  process suspended_handle;
  string random_state;
  function automatic logic launch_capture(
      input logic [7:0] value,
      input logic upper);
    fork
      begin
        #1;
        if (upper)
          automatic_capture[15:8] = value;
        else
          automatic_capture[7:0] = value;
      end
    join_none
    return 1'b1;
  endfunction
  initial begin : root
    logic [7:0] shared = 0;
    result = 0;
    automatic_capture = 0;
    launch_result = launch_capture(8'h3c, 1'b0);
    launch_result = launch_capture(8'ha5, 1'b1);
    shadow_regression = 1'b1;
    shadow_regression <= 1'b0;
    fork
      #2 shadow_regression <= 1'b1;
    join_none
    fork : all_children
      shared[0] = 1;
      #2 shared[1] = 1;
    join : all_children
    shared[2] = 1;
    fork
      #1 shared[3] = 1;
      #3 shared[4] = 1;
    join_any
    shared[5] = 1;
    wait fork;
    fork
      #5 shared[6] = 1;
    join_none
    shared[7] = 1;
    disable fork;
    result = shared;
    lifecycle = 0;
    fork
      begin
        handle = process::self();
        #2;
      end
    join_none
    #0;
    lifecycle[2:0] = handle.status();
    lifecycle[3] = handle.completed();
    handle.await();
    lifecycle[6:4] = handle.status();
    lifecycle[7] = handle.completed();
    fork
      begin
        killed_handle = process::self();
        #10;
      end
    join_none
    #0;
    killed_handle.kill();
    lifecycle[10:8] = killed_handle.status();
    lifecycle[11] = killed_handle.completed();
    repeat (2) begin
      fork
        #3 lifecycle[12] = 1;
      join_none
    end
    wait fork;
    fork
      #1 lifecycle[0] = 1;
    join_none
    fork : abort_children
      begin
        #0;
        lifecycle[14] = 1;
        disable abort_children;
      end
      #5 lifecycle[13] = 0;
    join : abort_children
    wait fork;
    fork : detached_abort
      #5 lifecycle[17] = 1;
    join_none : detached_abort
    #0;
    disable detached_abort;
    lifecycle[16] = 1;
    wait fork;
    begin : concurrent_escape
      fork
        begin
          #0;
          disable concurrent_escape;
        end
        #5 lifecycle[18] = 1;
      join
      lifecycle[19] = 1;
    end
    lifecycle[20] = 1;
    disable concurrent_escape;
    lifecycle[21] = 1;
    fork
      begin
        suspended_handle = process::self();
        #2 lifecycle[22] = 1;
      end
    join_none
    #0;
    suspended_handle.suspend();
    lifecycle[25:23] = suspended_handle.status();
    #3;
    suspended_handle.resume();
    suspended_handle.await();
    random_state = suspended_handle.get_randstate();
    suspended_handle.srandom(32'h1234);
    suspended_handle.set_randstate(random_state);
    begin : escaped
      lifecycle[13] = 1;
      begin : nested
        disable escaped;
      end
      lifecycle[14] = 1;
    end
    lifecycle[15] = 1;
    #1;
    if (shadow_regression !== 1'b1)
      $fatal(1, "fork child stable-update shadow suppressed a real transition");
    $finish;
  end
endmodule
)";
    }
    test_optimization(
        directory.path, source, fsim::project::Optimization::o0);
    test_optimization(
        directory.path, source, fsim::project::Optimization::o2);
    std::cout << "fork application tests passed\n";
    return 0;
}
