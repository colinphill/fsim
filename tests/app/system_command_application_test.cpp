// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
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
    std::uint64_t function_status { };
    std::uint64_t null_status { };
    std::vector<std::optional<std::string>> commands;
    std::size_t compiled_processes { };
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "system-command-test";
    config.project.top = "sv:work.system_command_test";
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
    return config;
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    fsim::app::Simulation simulation { std::move(project), 1000, engine };
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    std::size_t null_calls { };
    simulation.set_system_command_hook(
        [&capture, &null_calls](
            const std::optional<std::string_view> command) {
            capture.commands.emplace_back(command
                    ? std::optional<std::string> { *command }
                    : std::nullopt);
            if (!command) {
                return static_cast<std::int32_t>(91U + null_calls++);
            }
            if (*command == "function-command") {
                return std::int32_t { -17 };
            }
            assert(*command == "task-command");
            return std::int32_t { 23 };
        });
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    const auto read = [&](const std::string_view name) {
        const auto signal = simulation.find_signal(
            "system_command_test." + std::string { name });
        assert(signal);
        const auto value = simulation.read_signal(*signal).low_word();
        assert(value.bval == 0);
        return value.aval;
    };
    capture.function_status = read("function_status");
    capture.null_status = read("null_status");
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
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project);
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "system-command-runtime", diagnostics);
    assert(restored && !diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
        == encoded);
    project->design = std::move(*restored);
    return execute(std::move(*project), engine);
}

void test_system_command(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto config = config_for(directory, source, optimization);
    const auto interpreted = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto expected_commands
        = std::vector<std::optional<std::string>> {
              std::string { "function-command" },
              std::nullopt,
              std::string { "task-command" },
              std::nullopt,
          };
    for (const auto* capture : { &interpreted, &compiled }) {
        assert(capture->function_status
            == static_cast<std::uint32_t>(-17));
        assert(capture->null_status == 91);
        assert(capture->commands == expected_commands);
    }
    assert(interpreted.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
#else
    assert(compiled.compiled_processes == 0);
#endif
}

void test_invalid_system_command(const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_system_command.sv";
    {
        std::ofstream output(source);
        output << R"(
module system_command_test;
  initial $system("one", "two");
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o0);
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project && diagnostics.has_error());
    const auto expected = std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVSYS-001";
        });
    if (!expected) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(expected);
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now()
                           .time_since_epoch()
                           .count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-system-command-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "system_command_test.sv";
    {
        std::ofstream output(source);
        output << R"(
module system_command_test;
  int function_status;
  int null_status;
  initial begin
    function_status = $system("function-command");
    null_status = $system();
    $system("task-command");
    $system;
  end
endmodule
)";
    }
    test_system_command(
        directory.path, source, fsim::project::Optimization::o0);
    test_system_command(
        directory.path, source, fsim::project::Optimization::o2);
    test_invalid_system_command(directory.path);
    std::cout << "$system application tests passed\n";
    return 0;
}
