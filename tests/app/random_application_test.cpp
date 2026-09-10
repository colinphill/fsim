// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

struct Report {
    std::string message;
    fsim::runtime::simir::AssertionSeverity severity { };
    fsim::runtime::simir::SourceLocation source;
    fsim::runtime::SimulationTick time { };
    std::uint64_t delta { };

    bool operator==(const Report&) const = default;
};

struct Capture {
    fsim::runtime::RunResult result;
    std::array<fsim::runtime::Logic4Word, 58> values { };
    std::vector<std::string> output;
    std::vector<Report> reports;
    std::size_t compiled_processes { };
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
    return fsim::cli::run(
        static_cast<int>(raw.size()),
        raw.data(),
        fsim::app::make_cli_services(input),
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
    constexpr std::array<std::string_view, 58> names {
        "a", "b", "c", "d", "e", "f", "u", "p0", "p1",
        "scope_result", "scope_value", "scope_mode", "wide_nonzero",
        "inline_result", "inline_value", "inline_mode", "srandom_a",
        "srandom_b", "dist_uniform_value", "dist_uniform_seed",
        "dist_normal_value", "dist_normal_seed", "dist_exponential_value",
        "dist_exponential_seed", "dist_poisson_value", "dist_poisson_seed",
        "dist_chi_square_value", "dist_chi_square_seed", "dist_t_value",
        "dist_t_seed", "dist_erlang_value", "dist_erlang_seed",
        "dist_invalid_value", "checker_satisfied", "checker_rejected",
        "checker_satisfied_unchanged", "checker_rejected_unchanged",
        "unique_result", "unique_values_distinct", "unique_impossible",
        "dist_invalid_seed", "dist_invalid_poisson_value",
        "dist_invalid_chi_square_value", "dist_invalid_t_value",
        "dist_invalid_erlang_value", "dist_invalid_seed_final",
        "dist_equal_value", "dist_equal_seed", "dist_reversed_value",
        "dist_reversed_seed", "dist_unknown_value", "dist_unknown_seed",
        "dist_resource_value", "dist_resource_seed",
        "dist_negative_deviation_value", "dist_negative_deviation_seed",
        "dist_zero_mean_erlang_value", "dist_zero_mean_erlang_seed"
    };
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
    simulation.set_report_hook(
        [&capture](
            const fsim::runtime::simir::ProcessId,
            const std::string_view message,
            const fsim::runtime::simir::AssertionSeverity severity,
            const fsim::runtime::simir::SourceLocation& source,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
            capture.reports.push_back(
                { std::string { message }, severity, source, time, delta });
        });
    capture.result = simulation.run();
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto signal = simulation.find_signal(
            "random_test." + std::string { names[index] });
        assert(signal);
        capture.values[index] = simulation.read_signal(*signal).low_word();
    }
    return capture;
}

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization,
    const std::uint64_t seed)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "random-test";
    config.project.top = "sv:work.random_test";
    config.project.time_resolution = "1ns";
    config.project.seed = seed;
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
    assert(project->seed == config.project.seed);
    assert(!project->entropy_seed);
    return execute(std::move(*project), engine);
}

void test_random(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    const auto config = config_for(directory, source, optimization, 123);
    const auto reference = run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_once(config, fsim::app::SimulationEngine::compiled);
    const auto repeated = run_once(config, fsim::app::SimulationEngine::interpreter);
    auto changed_config = config;
    changed_config.project.seed = 124;
    const auto changed_seed = run_once(
        changed_config,
        fsim::app::SimulationEngine::interpreter);

    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(compiled.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.values == compiled.values);
    assert(reference.output == compiled.output);
    assert(reference.reports == compiled.reports);
    assert(
        reference.output.size() == 2
        && reference.output[0].starts_with("cli=")
        && reference.output[1].starts_with("signed="));
    assert(reference.values == repeated.values);
    assert(reference.values[0] != changed_seed.values[0]);
    assert(
        reference.values[4].bval == 0
        && reference.values[4].aval <= 9);
    assert(
        reference.values[5].bval == 0
        && reference.values[5].aval >= 3
        && reference.values[5].aval <= 9);
    assert(
        reference.values[6].bval
        == std::numeric_limits<std::uint32_t>::max());
    assert(reference.values[7] != reference.values[8]);
    assert(reference.values[9].aval == 1 && reference.values[9].bval == 0);
    assert(reference.values[10].aval <= 7 && reference.values[10].bval == 0);
    assert(reference.values[11].aval <= 2 && reference.values[11].bval == 0);
    assert(reference.values[12].aval == 1 && reference.values[12].bval == 0);
    assert(reference.values[13].aval == 1 && reference.values[13].bval == 0);
    assert(reference.values[14].aval == 5 && reference.values[14].bval == 0);
    assert(reference.values[15].aval == 2 && reference.values[15].bval == 0);
    assert(reference.values[16] == reference.values[17]);
    constexpr std::array<std::uint32_t, 15> distribution_values {
        1U, 0x92c55619U,
        79U, 0x57d07df5U,
        10U, 0x664f4e32U,
        7U, 0x7812cacaU,
        1U, 0x6051c836U,
        2U, 0x5bd4c380U,
        34U, 0x0592ad77U,
        0U
    };
    for (std::size_t index = 0; index < distribution_values.size(); ++index) {
        assert(reference.values[18U + index].aval
            == distribution_values[index]);
        assert(reference.values[18U + index].bval == 0U);
    }
    assert(reference.values[33].aval == 1 && reference.values[33].bval == 0);
    assert(reference.values[34].aval == 0 && reference.values[34].bval == 0);
    assert(reference.values[35].aval == 1 && reference.values[35].bval == 0);
    assert(reference.values[36].aval == 1 && reference.values[36].bval == 0);
    assert(reference.values[37].aval == 1 && reference.values[37].bval == 0);
    assert(reference.values[38].aval == 1 && reference.values[38].bval == 0);
    assert(reference.values[39].aval == 0 && reference.values[39].bval == 0);
    assert(reference.values[40].aval == 0x0592ad77U);
    assert(reference.values[41].aval == 0U);
    assert(reference.values[42].aval == 0U);
    assert(reference.values[43].aval == 0U);
    assert(reference.values[44].aval == 0U);
    assert(reference.values[45].aval == 0x0592ad77U);
    assert(reference.values[46].aval == 5U);
    assert(reference.values[47].aval == 0x10203040U);
    assert(reference.values[48].aval == 10U);
    assert(reference.values[49].aval == 0x10203040U);
    assert(reference.values[50].aval == 0U && reference.values[50].bval == 0U);
    assert(reference.values[51].bval
        == std::numeric_limits<std::uint32_t>::max());
    assert(reference.values[52].aval == 0U);
    assert(reference.values[53].aval == 0x76543210U);
    assert(reference.values[54].bval == 0U);
    assert(reference.values[55].aval != 0x2468ace0U);
    assert(reference.values[56].aval == 0U);
    assert(reference.values[57].aval != 0x11223344U);
    constexpr std::array<std::string_view, 7> warning_messages {
        "exponential distribution mean must be positive",
        "Poisson distribution mean must be positive",
        "chi-square distribution degrees of freedom must be positive",
        "Student-t distribution degrees of freedom must be positive",
        "Erlang distribution stage count must be positive",
        "random distribution arguments must not contain X or Z",
        "random distribution exceeded the bounded draw limit",
    };
    assert(reference.reports.size() == warning_messages.size());
    for (std::size_t index = 0; index < warning_messages.size(); ++index) {
        const auto& report = reference.reports[index];
        assert(report.message == warning_messages[index]);
        assert(report.severity
            == fsim::runtime::simir::AssertionSeverity::warning);
        assert(report.source.path == source.generic_string());
        assert(report.source.line > 0U && report.source.column > 0U);
        assert(report.time == 0U && report.delta == 0U);
    }
    assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 3);
#else
    assert(compiled.compiled_processes == 0);
#endif
}

std::string run_seeded_cli(
    const std::filesystem::path& manifest,
    const std::string_view seed)
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
            "--seed",
            std::string { seed },
        },
        input,
        output,
        error);
    assert(result == 0);
    assert(error.str().empty());
    return output.str();
}

void test_cli_seed_selection(
    const std::filesystem::path& directory,
    const std::filesystem::path& manifest)
{
    std::istringstream default_input;
    std::ostringstream default_output;
    std::ostringstream default_error;
    assert(
        run_cli(
            { "fsim", "run", "-p", manifest.string() },
            default_input,
            default_output,
            default_error)
        == 0);
    assert(default_error.str().empty());
    assert(default_output.str() == run_seeded_cli(manifest, "1"));

    const auto first = run_seeded_cli(manifest, "555");
    const auto repeated = run_seeded_cli(manifest, "555");
    const auto changed = run_seeded_cli(manifest, "556");
    assert(first == repeated);
    assert(first != changed);
    assert(first.find("cli=") != std::string::npos);

    const auto random = run_seeded_cli(manifest, "random");
    const std::string_view marker { "random seed " };
    const auto marker_position = random.find(marker);
    assert(marker_position != std::string::npos);
    const auto digits_start = marker_position + marker.size();
    const auto digits_end = random.find('\n', digits_start);
    assert(digits_end != std::string::npos && digits_end > digits_start);
    for (const auto character :
        std::string_view { random }.substr(
            digits_start, digits_end - digits_start)) {
        assert(character >= '0' && character <= '9');
    }
    assert(random.find("cli=") != std::string::npos);
    static_cast<void>(directory);
}

void test_invalid_scope_randomize(
    const std::filesystem::path& directory)
{
    const auto source = directory / "invalid_scope_randomize.sv";
    {
        std::ofstream output(source);
        output << R"(
module invalid_scope_randomize;
  logic module_value;
  initial begin
    logic local_value;
    int result;
    result = std::randomize();
    result = std::randomize(local_value + 1);
    result = std::randomize(module_value);
    $srandom();
    result = $dist_uniform(1, 0, 10);
  end
endmodule
)";
    }
    auto config = config_for(
        directory, source, fsim::project::Optimization::o0, 1);
    config.project.name = "invalid-scope-randomize";
    config.project.top = "sv:work.invalid_scope_randomize";
    config.build.cache_path = directory / "invalid-scope-randomize-cache";
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project);
    for (const auto code : {
             "FSIM-ELAB-SVRAND-001",
             "FSIM-ELAB-SVRAND-002",
             "FSIM-ELAB-SVRAND-003",
             "FSIM-ELAB-SVRAND-007",
             "FSIM-ELAB-SVRAND-008" }) {
        assert(std::ranges::any_of(
            diagnostics.diagnostics(), [&](const auto& diagnostic) {
                return diagnostic.code == code;
            }));
    }
}

} // namespace

int main()
{
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-random-test-" + std::to_string(nonce))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "random_test.sv";
    {
        std::ofstream output(source);
        output << R"(
class UniquePacket;
  rand logic [2:0] left;
  rand logic [2:0] right;
  rand logic [2:0] third;
  constraint bounded {
    left < 4;
    right < 4;
    third < 4;
  }
  constraint distinct {
    unique {left, right, third};
  }
endclass

module random_test;
  typedef enum logic [1:0] {MODE_ZERO, MODE_ONE, MODE_TWO} mode_t;
  logic [31:0] a, b, c, d, e, f, u, p0, p1;
  logic [31:0] srandom_a, srandom_b;
  integer dist_seed;
  integer dist_uniform_value, dist_uniform_seed;
  integer dist_normal_value, dist_normal_seed;
  integer dist_exponential_value, dist_exponential_seed;
  integer dist_poisson_value, dist_poisson_seed;
  integer dist_chi_square_value, dist_chi_square_seed;
  integer dist_t_value, dist_t_seed;
  integer dist_erlang_value, dist_erlang_seed;
  integer dist_invalid_value;
  integer dist_invalid_seed, dist_invalid_poisson_value;
  integer dist_invalid_chi_square_value, dist_invalid_t_value;
  integer dist_invalid_erlang_value, dist_invalid_seed_final;
  integer dist_equal_value, dist_equal_seed;
  integer dist_reversed_value, dist_reversed_seed;
  integer dist_unknown_value, dist_unknown_seed;
  integer dist_resource_value, dist_resource_seed;
  integer dist_negative_deviation_value, dist_negative_deviation_seed;
  integer dist_zero_mean_erlang_value, dist_zero_mean_erlang_seed;
  int scope_result;
  logic [2:0] scope_value;
  mode_t scope_mode;
  logic wide_nonzero;
  int inline_result;
  logic [2:0] inline_value;
  mode_t inline_mode;
  int checker_satisfied, checker_rejected;
  logic checker_satisfied_unchanged, checker_rejected_unchanged;
  int unique_result, unique_impossible;
  logic unique_values_distinct;
  initial begin
    logic [2:0] scoped;
    mode_t mode;
    logic [136:0] wide;
    u = $urandom_range(4'bx);
    a = $urandom;
    b = $urandom();
    c = $random;
    d = $random();
    e = $urandom_range(9);
    f = $urandom_range(3, 9);
    scope_result = std::randomize(scoped, mode, wide);
    scope_value = scoped;
    scope_mode = mode;
    wide_nonzero = |wide;
    inline_result = std::randomize(scoped, mode) with {
      solve mode before scoped;
      mode dist {2 := 8};
      mode == 2;
      (mode == 2) -> scoped inside {[5:6]};
      soft scoped == 5;
    };
    inline_value = scoped;
    inline_mode = mode;
    $display("cli=%h", $urandom);
    $display("signed=%d", $random);
    $srandom(32'h13579bdf);
    srandom_a = $urandom;
    $srandom(32'h13579bdf);
    srandom_b = $urandom;
    dist_seed = 32'h12345678;
    dist_uniform_value = $dist_uniform(dist_seed, -10, 10);
    dist_uniform_seed = dist_seed;
    dist_normal_value = $dist_normal(dist_seed, 100, 15);
    dist_normal_seed = dist_seed;
    dist_exponential_value = $dist_exponential(dist_seed, 20);
    dist_exponential_seed = dist_seed;
    dist_poisson_value = $dist_poisson(dist_seed, 7);
    dist_poisson_seed = dist_seed;
    dist_chi_square_value = $dist_chi_square(dist_seed, 5);
    dist_chi_square_seed = dist_seed;
    dist_t_value = $dist_t(dist_seed, 8);
    dist_t_seed = dist_seed;
    dist_erlang_value = $dist_erlang(dist_seed, 3, 30);
    dist_erlang_seed = dist_seed;
    dist_invalid_value = $dist_exponential(dist_seed, 0);
    dist_invalid_seed = dist_seed;
    dist_invalid_poisson_value = $dist_poisson(dist_seed, 0);
    dist_invalid_chi_square_value = $dist_chi_square(dist_seed, 0);
    dist_invalid_t_value = $dist_t(dist_seed, 0);
    dist_invalid_erlang_value = $dist_erlang(dist_seed, 0, 30);
    dist_invalid_seed_final = dist_seed;
    dist_seed = 32'h10203040;
    dist_equal_value = $dist_uniform(dist_seed, 5, 5);
    dist_equal_seed = dist_seed;
    dist_reversed_value = $dist_uniform(dist_seed, 10, -10);
    dist_reversed_seed = dist_seed;
    dist_seed = 'x;
    dist_unknown_value = $dist_poisson(dist_seed, 7);
    dist_unknown_seed = dist_seed;
    dist_seed = 32'h76543210;
    dist_resource_value = $dist_erlang(dist_seed, 1000001, 1);
    dist_resource_seed = dist_seed;
    dist_seed = 32'h2468ace0;
    dist_negative_deviation_value = $dist_normal(dist_seed, 100, -15);
    dist_negative_deviation_seed = dist_seed;
    dist_seed = 32'h11223344;
    dist_zero_mean_erlang_value = $dist_erlang(dist_seed, 3, 0);
    dist_zero_mean_erlang_seed = dist_seed;
    begin
      UniquePacket packet;
      packet = new;
      packet.left = 1;
      packet.right = 2;
      packet.third = 3;
      checker_satisfied = packet.randomize(null);
      checker_satisfied_unchanged = packet.left == 1
          && packet.right == 2 && packet.third == 3;
      packet.right = 1;
      checker_rejected = packet.randomize(null);
      checker_rejected_unchanged = packet.left == 1
          && packet.right == 1 && packet.third == 3;
      unique_result = packet.randomize();
      unique_values_distinct = packet.left != packet.right
          && packet.left != packet.third && packet.right != packet.third;
      unique_impossible = packet.randomize() with {
        unique {left, right};
        left == right;
      };
    end
  end
  initial p0 = $urandom;
  initial p1 = $urandom;
endmodule
)";
    }
    const auto manifest = directory.path / "fsim.toml";
    {
        std::ofstream output(manifest);
        output
            << "schema = 3\n"
            << "[project]\n"
            << "name = \"random-test\"\n"
            << "top = \"sv:work.random_test\"\n"
            << "time_resolution = \"1ns\"\n"
            << "[[source_set]]\n"
            << "language = \"systemverilog\"\n"
            << "standard = \"2023\"\n"
            << "library = \"work\"\n"
            << "files = [\"random_test.sv\"]\n"
            << "[build]\n"
            << "optimization = \"O2\"\n"
            << "cache_path = \"cli-cache\"\n"
            << "[run]\n"
            << "max_deltas = 1000\n";
    }

    test_random(
        directory.path, source, fsim::project::Optimization::o0);
    test_random(
        directory.path, source, fsim::project::Optimization::o2);
    test_invalid_scope_randomize(directory.path);
    test_cli_seed_selection(directory.path, manifest);
    std::cout << "random application tests passed\n";
    return 0;
}
