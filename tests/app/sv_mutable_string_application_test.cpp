// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
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
    fsim::runtime::RunResult result;
    std::vector<std::string> output;
    std::vector<std::string> keys;
    std::string title;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& stable,
    const std::filesystem::path& mutable_source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "sv-mutable-strings";
    config.project.top = "sv:work.mutable_top";
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
    sources.files = { stable, mutable_source };
    config.source_sets.push_back(std::move(sources));
    return config;
}

Capture run_once(
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
    capture.keys = project->specialization_cache_keys;
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.cache = simulation.native_cache_statistics();
    simulation.set_output_hook(
        [&capture](
            const auto,
            const std::string_view text,
            const bool,
            const auto,
            const auto) {
            capture.output.emplace_back(text);
        });
    capture.result = simulation.run();
    const auto& objects = simulation.design().string_objects();
    const auto found = std::ranges::find_if(
        objects,
        [](const auto& object) {
            return object.name == "mutable_top.worker.title";
        });
    assert(found != objects.end());
    capture.title = simulation.read_string_object(found->id);
    return capture;
}

void inspect_suspended(
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
    std::optional<std::size_t> temporary;
    for (const auto& process : simulation.design().processes()) {
        for (std::size_t index = 0;
            index < process.debug_string_locals.size(); ++index) {
            if (process.debug_string_locals[index].name
                == "remember.temporary") {
                process_id = process.id;
                temporary = index;
            }
        }
    }
    assert(process_id && temporary);
    bool requested = false;
    simulation.set_execution_point_hook(
        [&](fsim::runtime::Scheduler& scheduler,
            const fsim::runtime::simir::ExecutionPoint& point) {
            if (!requested && point.process == *process_id
                && point.kind
                    == fsim::runtime::simir::ExecutionPointKind::
                        process_suspend) {
                requested = true;
                scheduler.request_stop();
            }
        });
    const auto stopped = simulation.run();
    assert(requested);
    assert(stopped.status == fsim::runtime::RunStatus::stopped);
    assert(
        simulation.read_process_string_local(
            *process_id, *temporary)
        == "fsim-v1!");
    simulation.clear_stop();
    const auto resumed = simulation.run();
    assert(resumed.status == fsim::runtime::RunStatus::completed);
}

void verify_debugger_policy(const fsim::project::Config& config)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project),
        config.run.max_deltas,
        fsim::app::SimulationEngine::debug
    };
    std::ostringstream output;
    std::ostringstream error;
    fsim::app::DebuggerControl debugger { simulation, output, error };
    debugger.execute({ "show", "worker.title" });
    debugger.execute({ "show", "worker.bridge.source" });
    debugger.execute({ "force", "worker.title", "bad" });
    debugger.execute({ "deposit", "worker.title", std::string { "A\nB" } });
    debugger.execute({ "show", "worker.title" });
    const auto text = output.str();
    assert(text.find("mutable_top.worker.title = \"fsim\"")
        != std::string::npos);
    assert(text.find(
               "mutable_top.worker.bridge.source = \"input\"")
        != std::string::npos);
    assert(text.find(
               "force is not supported for mutable string objects; "
               "use deposit")
        != std::string::npos);
    assert(text.find("mutable_top.worker.title = \"A\\nB\"")
        != std::string::npos);
    assert(error.str().empty());
}

} // namespace

int main()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-sv-mutable-strings-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);
    const auto stable = directory.path / "stable.sv";
    const auto mutable_source = directory.path / "mutable.sv";
    {
        std::ofstream output(stable, std::ios::binary);
        output << R"(
module stable_child;
  initial $display("%s", "stable");
endmodule
)";
        assert(output.good());
    }
    const auto write_mutable = [&](const std::string_view suffix) {
        std::ofstream output(
            mutable_source, std::ios::binary | std::ios::trunc);
        output << R"(
module string_port_child(
    input string source,
    output string sink,
    inout string shared);
  initial begin
    sink = {source, ":port"};
    shared.putc(0, 8'h53);
  end
endmodule
module mutable_child;
  string title = "fsim";
  string port_source = "input";
  string port_sink;
  string port_shared = "shared";
  string_port_child bridge(port_source, port_sink, port_shared);
  function automatic string decorate(input string value);
    return {value, ")"
               << suffix << R"("};
  endfunction
  function static string accumulate(input string value);
    string retained = "seed";
    retained = {retained, value};
    return retained;
  endfunction
  task automatic remember(input string value, output string copied);
    string temporary;
    temporary = {value, "!"};
    #1;
    copied = temporary;
  endtask
  task static remember_static(
      input string value, output string copied);
    string retained = "task";
    retained = {retained, value};
    copied = retained;
  endtask
  initial begin : worker
    string copy;
    string upper;
    string lower;
    string middle;
    string mutated;
    string decimal_text;
    string hex_text;
    string octal_text;
    string binary_text;
    string formatted;
    string written;
    string written_binary;
    string written_hexadecimal;
    string written_octal;
    string functional;
    string unicode;
    real parsed_real;
    string real_text;
    string static_first;
    string static_second;
    string task_first;
    string task_second;
    remember(decorate(title), copy);
    copy[0] = "F";
    if (copy.len() == 8 && copy != "")
      title = copy;
    upper = copy.toupper();
    lower = upper.tolower();
    middle = copy.substr(1, 3);
    mutated = middle;
    mutated.putc(0, 8'h53);
    decimal_text.itoa(-42);
    hex_text.hextoa(255);
    octal_text.octtoa(9);
    binary_text.bintoa(5);
    $sformat(
      formatted, "fmt=%0d/%s/%m/%04t", 42, mutated);
    $swrite(written, "%s:%02h", lower, 8'h0a);
    $swriteb(written_binary, 4'b0111, 4'b10xz);
    $swriteh(written_hexadecimal, 8'ha5, 8'hxz);
    $swriteo(written_octal, 8'ha5, 3'b101);
    functional = $sformatf("%-5s|%b|%%", middle, 4'b0011);
    unicode = "Aπ😀";
    unicode[1] = "🙂";
    parsed_real = "1_2.5".atoreal();
    real_text.realtoa(parsed_real);
    static_first = accumulate("A");
    static_second = accumulate("B");
    remember_static("A", task_first);
    remember_static("B", task_second);
    $display(
      "%s|%s|%s|%s|%s|%s|%s|%s|%s|%0d|%0d|%0d|%0d|%0d|%0d|%0d|%0d",
      title, upper, lower, middle, mutated,
      decimal_text, hex_text, octal_text, binary_text,
      copy.compare(title), copy.icompare(upper),
      copy.getc(0), copy.getc(99),
      "12_3".atoi(), "ff".atohex(), "17".atooct(), "101".atobin());
    $display("|%s|%s|%s", formatted, written, functional);
    $display("|%s|%s|%s", written_binary, written_hexadecimal, written_octal);
    $display("|%s|%s", port_sink, port_shared);
    $display(
      "%s|%0d|%0d|%s",
      unicode, unicode.len(), unicode[1], unicode.substr(1, 2));
    $display("|%s", real_text);
    $display("|%s|%s", static_first, static_second);
    $display("|%s|%s", task_first, task_second);
  end
endmodule
module mutable_top;
  stable_child stable();
  mutable_child worker();
endmodule
)";
        assert(output.good());
    };

    write_mutable("-v1");
    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        const auto config = config_for(
            directory.path, stable, mutable_source, optimization);
        const auto reference = run_once(config, fsim::app::SimulationEngine::interpreter);
        const auto cold = run_once(config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_once(config, fsim::app::SimulationEngine::compiled);
        assert(reference.result.status
            == fsim::runtime::RunStatus::completed);
        assert(reference.result.time == 1);
        auto expected = std::vector<std::string> {
            "stable", "Fsim-v1!", "|FSIM-V1!", "|fsim-v1!", "|sim",
            "|Sim", "|-42", "|ff", "|11", "|101",
            "|0", "|0", "|70", "|0", "|123", "|255", "|15", "|5"
        };
        const auto formatted_expected = std::vector<std::string> {
            "|fmt=42/Sim/mutable_top.worker/0001",
            "|fsim-v1!:0a", "|sim  |0011|%",
            "|011110xz", "|a5xz", "|2455",
            "|input:port", "|Shared",
            "A🙂😀", "|3", "|128578", "|🙂😀", "|12.5",
            "|seedA", "|seedAB", "|taskA", "|taskAB"
        };
        expected.insert(
            expected.end(), formatted_expected.begin(), formatted_expected.end());
        if (reference.output != expected) {
            for (const auto& line : reference.output) {
                std::cerr << '[' << line << "]\n";
            }
        }
        assert(reference.output == expected);
        assert(reference.title == "Fsim-v1!");
        assert(reference.output == cold.output);
        assert(cold.output == warm.output);
        assert(reference.title == cold.title && cold.title == warm.title);
        assert(reference.keys == cold.keys && cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes == 3);
        assert(cold.compiled_modules == 3);
        assert(cold.cache.misses == 3 && cold.cache.stores == 3);
        assert(warm.cache.hits == 3);
#endif
        inspect_suspended(
            config, fsim::app::SimulationEngine::interpreter);
        inspect_suspended(
            config, fsim::app::SimulationEngine::compiled);
        verify_debugger_policy(config);
    }

    const auto baseline = run_once(
        config_for(
            directory.path,
            stable,
            mutable_source,
            fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::compiled);
    write_mutable("-v2");
    const auto changed = run_once(
        config_for(
            directory.path,
            stable,
            mutable_source,
            fsim::project::Optimization::o2),
        fsim::app::SimulationEngine::compiled);
    assert(changed.output == std::vector<std::string>({ "stable", "Fsim-v2!", "|FSIM-V2!", "|fsim-v2!", "|sim", "|Sim", "|-42", "|ff", "|11", "|101", "|0", "|0", "|70", "|0", "|123", "|255", "|15", "|5", "|fmt=42/Sim/mutable_top.worker/0001", "|fsim-v2!:0a", "|sim  |0011|%", "|011110xz", "|a5xz", "|2455", "|input:port", "|Shared", "A🙂😀", "|3", "|128578", "|🙂😀", "|12.5", "|seedA", "|seedAB", "|taskA", "|taskAB" }));
    assert(changed.keys != baseline.keys);
#if defined(FSIM_HAS_LLVM)
    assert(changed.cache.hits == 1);
    assert(changed.cache.misses == 2);
    assert(changed.cache.stores == 2);
#endif
    return 0;
}
