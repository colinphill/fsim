// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <ranges>
#include <sstream>
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
    fsim::runtime::RunResult result;
    std::uint64_t prefix { };
    std::uint64_t missing { };
    std::uint64_t decimal_ok { };
    std::uint64_t decimal_value { };
    std::uint64_t wide_ok { };
    std::uint64_t text_ok { };
    fsim::runtime::PackedLogic4 wide_value;
    std::vector<std::string> output;
    std::size_t compiled_processes { };
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "plusarg-test";
    config.project.top = "sv:work.plusarg_test";
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
    const fsim::app::SimulationEngine engine,
    const std::vector<std::string>& plusargs)
{
    fsim::app::Simulation simulation { std::move(project), 1000, engine };
    simulation.set_systemverilog_plusargs(plusargs);
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    simulation.set_output_hook(
        [&capture](
            const fsim::runtime::simir::ProcessId,
            const std::string_view text,
            const bool,
            const fsim::runtime::SimulationTick,
            const std::uint64_t) {
            capture.output.emplace_back(text);
        });
    capture.result = simulation.run();
    const auto read_word = [&](const std::string_view name) {
        const auto signal = simulation.find_signal(
            "plusarg_test." + std::string { name });
        assert(signal);
        const auto value = simulation.read_signal(*signal).low_word();
        assert(value.bval == 0);
        return value.aval;
    };
    capture.prefix = read_word("prefix");
    capture.missing = read_word("missing");
    capture.decimal_ok = read_word("decimal_ok");
    capture.decimal_value = read_word("decimal_value");
    capture.wide_ok = read_word("wide_ok");
    capture.text_ok = read_word("text_ok");
    const auto wide = simulation.find_signal("plusarg_test.wide_value");
    assert(wide);
    capture.wide_value = simulation.read_signal(*wide);
    return capture;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::vector<std::string>& plusargs)
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
    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, artifact_diagnostics);
    assert(encoded && !artifact_diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "plusarg-runtime", artifact_diagnostics);
    assert(restored && !artifact_diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(
               *restored, artifact_diagnostics)
        == encoded);
    project->design = std::move(*restored);
    return execute(std::move(*project), engine, plusargs);
}

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
    return fsim::cli::run(
        static_cast<int>(raw.size()),
        raw.data(),
        fsim::app::make_cli_services(input),
        output,
        error);
}

void test_plusargs(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    std::string wide_digits;
    wide_digits.reserve(137);
    constexpr std::string_view states { "10xz" };
    for (std::size_t index = 0; index < 137; ++index) {
        wide_digits.push_back(states[index % states.size()]);
    }
    const std::vector<std::string> plusargs {
        "+FLAGGED",
        "+COUNT=-17",
        "+COUNT=99",
        "+WIDE=" + wide_digits,
        "+TEXT=hello_world"
    };
    const auto config = config_for(directory, source, optimization);
    const auto interpreted = run_once(
        config, fsim::app::SimulationEngine::interpreter, plusargs);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled, plusargs);
    assert(interpreted.result.status == fsim::runtime::RunStatus::completed);
    assert(compiled.result.status == fsim::runtime::RunStatus::completed);
    assert(interpreted.prefix == 1 && interpreted.missing == 0);
    assert(interpreted.decimal_ok == 1);
    assert(static_cast<std::uint32_t>(interpreted.decimal_value)
        == static_cast<std::uint32_t>(-17));
    assert(interpreted.wide_ok == 1 && interpreted.text_ok == 1);
    const auto expected = fsim::runtime::PackedLogic4::from_msb_string(
        wide_digits);
    assert(interpreted.wide_value == expected);
    const auto expected_output = std::vector<std::string> {
        "text=hello_world", "types=int", "|integer", "|real", "|string",
        "|wide_t", "|logic signed[136:0]", "|logic[136:0]",
        "containers=int[$]", "|int[longint]", "|int[3:1]",
        "|typename_class", "|virtual typename_if"
    };
    if (interpreted.output != expected_output) {
        for (const auto& line : interpreted.output) {
            std::cerr << '[' << line << "]\n";
        }
    }
    assert(interpreted.output == expected_output);
    assert(compiled.prefix == interpreted.prefix);
    assert(compiled.missing == interpreted.missing);
    assert(compiled.decimal_ok == interpreted.decimal_ok);
    assert(compiled.decimal_value == interpreted.decimal_value);
    assert(compiled.wide_ok == interpreted.wide_ok);
    assert(compiled.text_ok == interpreted.text_ok);
    assert(compiled.wide_value == interpreted.wide_value);
    assert(compiled.output == interpreted.output);
    assert(interpreted.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
#else
    assert(compiled.compiled_processes == 0);
#endif
}

void test_cli_plusargs(
    const std::filesystem::path& directory,
    const std::filesystem::path& manifest)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const auto result = run_cli(
        {
            "fsim",
            "run",
            "-p",
            manifest.string(),
            "+FLAGGED",
            "+COUNT=-17",
            "+WIDE=1010",
            "+TEXT=from_cli",
        },
        input,
        output,
        error);
    assert(result == 0);
    assert(error.str().empty());
    assert(output.str().find("text=from_cli\n") != std::string::npos);
    static_cast<void>(directory);
}

void test_invalid_plusargs(const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_plusargs.sv";
    {
        std::ofstream output(source);
        output << R"(
module invalid_plusargs;
  string dynamic_format = "COUNT=%d";
  int value;
  initial begin
    value = $value$plusargs(dynamic_format, value);
    value = $value$plusargs("COUNT=%d%h", value);
    value = $value$plusargs("COUNT=%d", value + 1);
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o0);
    config.project.name = "invalid-plusarg-test";
    config.project.top = "sv:work.invalid_plusargs";
    config.build.cache_path = directory / "invalid-cache";
    fsim::diagnostic::Engine diagnostics;
    assert(!fsim::app::build_project(config, diagnostics));
    for (const auto code : {
             "FSIM-ELAB-SVCLI-002",
             "FSIM-ELAB-SVCLI-003",
         }) {
        assert(std::ranges::any_of(
            diagnostics.diagnostics(), [&](const auto& diagnostic) {
                return diagnostic.code == code;
            }));
    }
}

struct UnboundedCapture {
    fsim::runtime::RunResult result;
    std::vector<std::uint64_t> values;
    std::size_t compiled_processes { };
};

fsim::project::Config unbounded_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "unbounded-parameter-test";
    config.project.top = "sv:work.unbounded_top";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "unbounded-cache-o0"
                : "unbounded-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2017";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

UnboundedCapture execute_unbounded(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine)
{
    constexpr std::array<std::string_view, 12> paths {
        "unbounded_top.default_value.direct_value",
        "unbounded_top.default_value.parameter_value",
        "unbounded_top.default_value.bounded_value",
        "unbounded_top.default_value.generated_value",
        "unbounded_top.symbolic_value.direct_value",
        "unbounded_top.symbolic_value.parameter_value",
        "unbounded_top.symbolic_value.bounded_value",
        "unbounded_top.symbolic_value.generated_value",
        "unbounded_top.bounded_value.direct_value",
        "unbounded_top.bounded_value.parameter_value",
        "unbounded_top.bounded_value.bounded_value",
        "unbounded_top.bounded_value.generated_value"
    };
    fsim::app::Simulation simulation { std::move(project), 1000, engine };
    UnboundedCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.result = simulation.run();
    for (const auto path : paths) {
        const auto signal = simulation.find_signal(path);
        assert(signal);
        const auto word = simulation.read_signal(*signal).low_word();
        assert(word.bval == 0);
        capture.values.push_back(word.aval);
    }
    return capture;
}

UnboundedCapture run_unbounded(
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
    assert(project && !diagnostics.has_error());
    const auto symbolic = std::ranges::find_if(
        project->design.specializations(), [](const auto& specialization) {
            return specialization.instance
                == "unbounded_top.symbolic_value";
        });
    const auto bounded = std::ranges::find_if(
        project->design.specializations(), [](const auto& specialization) {
            return specialization.instance
                == "unbounded_top.bounded_value";
        });
    assert(symbolic != project->design.specializations().end());
    assert(bounded != project->design.specializations().end());
    assert((symbolic->parameter_values
        == std::vector<std::pair<std::string, std::string>> {
            { "P", "$" }
        }));
    assert((bounded->parameter_values
        == std::vector<std::pair<std::string, std::string>> {
            { "P", "17" }
        }));
    assert(symbolic->parameter_identity_values
        != bounded->parameter_identity_values);

    fsim::diagnostic::Engine artifact_diagnostics;
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, artifact_diagnostics);
    assert(encoded && !artifact_diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "unbounded-runtime", artifact_diagnostics);
    assert(restored && !artifact_diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(
               *restored, artifact_diagnostics)
        == encoded);
    project->design = std::move(*restored);
    return execute_unbounded(std::move(*project), engine);
}

void test_unbounded(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    const auto expected = std::vector<std::uint64_t> {
        1, 1, 0, 1,
        1, 1, 0, 1,
        1, 0, 0, 0
    };
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto config = unbounded_config(
            directory, source, optimization);
        const auto interpreted = run_unbounded(
            config, fsim::app::SimulationEngine::interpreter);
        const auto compiled_cold = run_unbounded(
            config, fsim::app::SimulationEngine::compiled);
        const auto compiled_warm = run_unbounded(
            config, fsim::app::SimulationEngine::compiled);
        assert(interpreted.result.status
            == fsim::runtime::RunStatus::stopped);
        assert(compiled_cold.result.status == interpreted.result.status);
        assert(compiled_warm.result.status == interpreted.result.status);
        assert(interpreted.values == expected);
        assert(compiled_cold.values == expected);
        assert(compiled_warm.values == expected);
        assert(interpreted.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
        assert(compiled_cold.compiled_processes != 0);
        assert(compiled_warm.compiled_processes
            == compiled_cold.compiled_processes);
#else
        assert(compiled_cold.compiled_processes == 0);
        assert(compiled_warm.compiled_processes == 0);
#endif
    }
}

void test_invalid_unbounded(
    const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_unbounded.sv";
    const auto write_source = [&](const std::string_view text) {
        std::ofstream output(source);
        output << text;
    };
    write_source(R"(
module invalid_typed #(parameter int P = $);
endmodule
)");
    auto config = unbounded_config(
        directory, source, fsim::project::Optimization::o0);
    config.project.top = "sv:work.invalid_typed";
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVCONST-001"
                && diagnostic.message.find("implicit parameter type")
                    != std::string::npos;
        }));

    write_source(R"(
module invalid_arithmetic #(parameter P = $ + 1);
endmodule
)");
    config.project.top = "sv:work.invalid_arithmetic";
    diagnostics.clear();
    const auto arithmetic = fsim::app::build_project(config, diagnostics);
    assert(!arithmetic && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-PARAM-005"
                && diagnostic.message.find("symbolic unbounded")
                    != std::string::npos;
        }));

    write_source(R"(
module invalid_arity;
  localparam bit VALUE = $isunbounded();
endmodule
)");
    config.project.top = "sv:work.invalid_arity";
    diagnostics.clear();
    const auto arity = fsim::app::build_project(config, diagnostics);
    assert(!arity && diagnostics.has_error());
    assert(std::ranges::any_of(
        diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-SEM-075";
        }));
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-plusarg-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "plusarg_test.sv";
    {
        std::ofstream output(source);
        output << R"(
interface typename_if;
  logic value;
endinterface

class typename_class;
endclass

module plusarg_test;
  typedef logic signed [136:0] wide_t;
  int prefix, missing, decimal_ok, decimal_value, wide_ok, text_ok;
  integer four_state_integer;
  real real_value;
  logic [136:0] wide_value;
  wide_t named_value;
  string text_value;
  int queue_value[$];
  int associative_value[longint];
  int static_value[3:1];
  typename_class class_value;
  virtual typename_if interface_value;
  initial begin
    prefix = $test$plusargs("FLAG");
    missing = $test$plusargs("ABSENT");
    decimal_ok = $value$plusargs("COUNT=%d", decimal_value);
    wide_ok = $value$plusargs("WIDE=%b", wide_value);
    text_ok = $value$plusargs("TEXT=%s", text_value);
    $display("text=%s", text_value);
    $display(
      "types=%s|%s|%s|%s|%s|%s|%s",
      $typename(prefix), $typename(four_state_integer),
      $typename(real_value), $typename(text_value),
      $typename(wide_t), $typename(named_value),
      $typename(137'h1));
    $display(
      "containers=%s|%s|%s|%s|%s",
      $typename(queue_value), $typename(associative_value),
      $typename(static_value), $typename(class_value),
      $typename(interface_value));
  end
endmodule
)";
    }
    const auto unbounded_source = directory.path / "unbounded_test.sv";
    {
        std::ofstream output(unbounded_source);
        output << R"(
module unbounded_value #(parameter P = $);
  bit direct_value;
  bit parameter_value;
  bit bounded_value;
  bit generated_value;
  initial begin
    direct_value = $isunbounded($);
    parameter_value = $isunbounded(P);
    bounded_value = $isunbounded(17);
  end
  generate
    if ($isunbounded(P)) begin : symbolic
      initial generated_value = 1'b1;
    end else begin : bounded
      initial generated_value = 1'b0;
    end
  endgenerate
endmodule

module unbounded_top;
  unbounded_value default_value();
  unbounded_value #(.P($)) symbolic_value();
  unbounded_value #(.P(17)) bounded_value();
  initial begin
    #1;
    $finish;
  end
endmodule
)";
    }
    const auto manifest = directory.path / "fsim.toml";
    {
        std::ofstream output(manifest);
        output
            << "schema = 3\n"
            << "[project]\n"
            << "name = \"plusarg-test\"\n"
            << "top = \"sv:work.plusarg_test\"\n"
            << "time_resolution = \"1ns\"\n"
            << "[[source_set]]\n"
            << "language = \"systemverilog\"\n"
            << "standard = \"2017\"\n"
            << "library = \"work\"\n"
            << "files = [\"plusarg_test.sv\"]\n"
            << "[build]\n"
            << "optimization = \"O2\"\n"
            << "cache_path = \"cli-cache\"\n"
            << "[run]\n"
            << "max_deltas = 1000\n";
    }
    test_plusargs(
        directory.path, source, fsim::project::Optimization::o0);
    test_plusargs(
        directory.path, source, fsim::project::Optimization::o2);
    test_cli_plusargs(directory.path, manifest);
    test_invalid_plusargs(directory.path);
    test_unbounded(directory.path, unbounded_source);
    test_invalid_unbounded(directory.path);
    std::cout << "plusarg application tests passed\n";
    return 0;
}
