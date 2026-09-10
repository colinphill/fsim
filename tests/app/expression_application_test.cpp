// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
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
    std::vector<std::string> values;
    fsim::app::NativeCacheStatistics native_cache;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
};

struct StopCapture {
    fsim::runtime::RunResult paused;
    fsim::runtime::RunResult resumed;
    std::vector<std::string> paused_values;
    std::vector<std::string> resumed_values;
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
};

struct FatalCapture {
    fsim::runtime::simir::AssertionSeverity severity { };
    std::string message;
    std::uint32_t line { };
    std::uint32_t column { };
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
};

void print_diagnostics(const fsim::diagnostic::Engine& diagnostics)
{
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        for (const auto& note : diagnostic.notes) {
            std::cerr << "  " << note.message << '\n';
        }
    }
}

template <std::size_t SignalCount>
[[nodiscard]] Capture run(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine,
    const std::array<std::string, SignalCount>& signal_paths)
{
    std::array<
        fsim::runtime::simir::SignalId, SignalCount>
        signals { };
    for (std::size_t index = 0; index < signal_paths.size(); ++index) {
        const auto signal = project.design.find_signal(signal_paths[index]);
        if (!signal) {
            std::cerr << "missing expression-test signal: "
                      << signal_paths[index] << '\n';
        }
        assert(signal);
        signals[index] = *signal;
    }

    fsim::app::Simulation simulation(
        std::move(project), 1000, engine);
    Capture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.native_cache = simulation.native_cache_statistics();
    capture.result = simulation.run();
    capture.values.reserve(signals.size());
    for (const auto signal : signals) {
        capture.values.push_back(
            simulation.read_signal(signal).to_msb_string());
    }
    return capture;
}

void test_systemverilog_clog2(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "systemverilog-clog2-expression-test";
    config.project.top = "sv:work.clog2_application";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "clog2-cache-o0"
                : "clog2-cache-o2");
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
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);
    assert(reference_project->design.specializations().size() == 3);
    assert(reference_project->specialization_cache_keys.size() == 3);

    std::optional<std::size_t> narrow_specialization;
    std::optional<std::size_t> wide_specialization;
    for (std::size_t index = 0;
        index < reference_project->design.specializations().size();
        ++index) {
        const auto& specialization = reference_project->design.specializations()[index];
        if (specialization.instance == "clog2_application.narrow") {
            narrow_specialization = index;
            assert((
                specialization.parameter_values
                == std::vector<std::pair<std::string, std::string>> {
                    { "DEPTH", "9" }, { "WIDTH", "4" } }));
        } else if (
            specialization.instance == "clog2_application.wide") {
            wide_specialization = index;
            assert((
                specialization.parameter_values
                == std::vector<std::pair<std::string, std::string>> {
                    { "DEPTH", "17" }, { "WIDTH", "5" } }));
        }
    }
    assert(narrow_specialization && wide_specialization);
    assert(
        reference_project->specialization_cache_keys
            .at(*narrow_specialization)
        != reference_project->specialization_cache_keys
            .at(*wide_specialization));

    const std::array<std::string, 2> signal_paths {
        "clog2_application.narrow_result",
        "clog2_application.wide_result"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert((
        compiled.values
        == std::vector<std::string> { "0100", "00101" }));
    assert(reference.compiled_processes == 0);
    assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 2);
    assert(compiled.compiled_modules == 2);
    assert(compiled.native_cache.hits == 0);
    assert(compiled.native_cache.misses == 2);
    assert(compiled.native_cache.stores == 2);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_shift_rotate(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-shift-rotate-expression-test";
    config.project.top = "vhdl:work.shift_rotate_app(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "vhdl-cache-o0"
                : "vhdl-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2019";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 26> signal_paths {
        "shift_rotate_app.arithmetic_left",
        "shift_rotate_app.rotated_left",
        "shift_rotate_app.rotated_right",
        "shift_rotate_app.reversed_logical_left",
        "shift_rotate_app.reversed_arithmetic_right",
        "shift_rotate_app.reversed_rotate_left",
        "shift_rotate_app.oversized_arithmetic_left",
        "shift_rotate_app.wrapped_rotate_left",
        "shift_rotate_app.wrapped_rotate_right",
        "shift_rotate_app.absolute_unknown",
        "shift_rotate_app.absolute_known",
        "shift_rotate_app.power_positive",
        "shift_rotate_app.power_negative",
        "shift_rotate_app.power_zero",
        "shift_rotate_app.conditional_true",
        "shift_rotate_app.conditional_false",
        "shift_rotate_app.conditional_chain",
        "shift_rotate_app.conditional_selected",
        "shift_rotate_app.expression_conditional",
        "shift_rotate_app.short_circuit_conditional",
        "shift_rotate_app.case_expression",
        "shift_rotate_app.case_range_expression",
        "shift_rotate_app.external_name_expression",
        "shift_rotate_app.selected_choice",
        "shift_rotate_app.selected_default",
        "shift_rotate_app.selected_dynamic"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::completed);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert((
        compiled.values
        == std::vector<std::string> {
            "0X0000ZZ",
            "0X0000Z1",
            "Z10X0000",
            "010X0000",
            "0X0000ZZ",
            "Z10X0000",
            "ZZZZZZZZ",
            "0X0000Z1",
            "Z10X0000",
            "XXXXXXXX",
            "00000101",
            "01010001",
            "11111000",
            "00000001",
            "10100101",
            "01011010",
            "00000010",
            "ZZZZ0110",
            "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000001",
            "0000000000000000000000000000000000000000000000000000000000000111",
            "00111100",
            "0000000000000000000000000000000000000000000000000000000000001011",
            "11111011",
            "10100101",
            "01011010",
            "01011010" }));
    assert(reference.compiled_processes == 0);
    assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
    // Forced-all compilation includes every design process in one
    // specialization module.
    assert(compiled.compiled_processes == 12);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_vhdl_external_name_mismatch(
    const std::filesystem::path& directory,
    const std::filesystem::path& source)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-external-name-mismatch-test";
    config.project.top = "vhdl:work.external_mismatch_app(rtl)";
    config.build.cache_path = directory / "vhdl-external-mismatch-cache";

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(!project);
    assert(std::ranges::any_of(
        diagnostics.diagnostics(),
        [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-VHEXTERNAL-001";
        }));
}

void test_verilog_power(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "verilog-power-expression-test";
    config.project.top = "verilog:work.verilog_power_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "verilog-power-cache-o0"
                : "verilog-power-cache-o2");
    config.run.max_deltas = 1000;

    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::verilog;
    sources.standard = "2005";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto reference_project = fsim::app::build_project(config, diagnostics);
    auto compiled_project = fsim::app::build_project(config, diagnostics);
    if (!reference_project || !compiled_project) {
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 2> signal_paths {
        "verilog_power_app.positive",
        "verilog_power_app.left_associative"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert((
        compiled.values
        == std::vector<std::string> {
            "01010001", "0000000001000000" }));
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_wildcard_equality(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "wildcard-equality-expression-test";
    config.project.top = "sv:work.wildcard_equality_app";
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
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 5> signal_paths {
        "wildcard_equality_app.masked",
        "wildcard_equality_app.left_unknown",
        "wildcard_equality_app.known_mismatch",
        "wildcard_equality_app.wildcard_neq",
        "wildcard_equality_app.logical_equal"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert((
        compiled.values
        == std::vector<std::string> { "1", "X", "0", "0", "X" }));
    assert(reference.compiled_processes == 0);
    assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 1);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

void test_systemverilog_signedness_casts(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "signedness-cast-expression-test";
    config.project.top = "sv:work.signedness_cast_app";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (optimization == fsim::project::Optimization::o0
                ? "signedness-cache-o0"
                : "signedness-cache-o2");
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
        print_diagnostics(diagnostics);
    }
    assert(reference_project);
    assert(compiled_project);

    const std::array<std::string, 64> signal_paths {
        "signedness_cast_app.signed_less",
        "signedness_cast_app.unsigned_less",
        "signedness_cast_app.signed_shift",
        "signedness_cast_app.unsigned_shift",
        "signedness_cast_app.known_is_unknown",
        "signedness_cast_app.xz_is_unknown",
        "signedness_cast_app.object_bits",
        "signedness_cast_app.concatenation_bits",
        "signedness_cast_app.descending_left",
        "signedness_cast_app.descending_right",
        "signedness_cast_app.descending_low",
        "signedness_cast_app.descending_high",
        "signedness_cast_app.descending_size",
        "signedness_cast_app.descending_increment",
        "signedness_cast_app.ascending_left",
        "signedness_cast_app.ascending_right",
        "signedness_cast_app.ascending_low",
        "signedness_cast_app.ascending_high",
        "signedness_cast_app.ascending_size",
        "signedness_cast_app.ascending_increment",
        "signedness_cast_app.dimensions",
        "signedness_cast_app.unpacked_dimensions",
        "signedness_cast_app.power_positive",
        "signedness_cast_app.power_zero",
        "signedness_cast_app.power_left_associative",
        "signedness_cast_app.power_negative_exponent",
        "signedness_cast_app.power_minus_one_negative",
        "signedness_cast_app.power_zero_negative",
        "signedness_cast_app.power_unknown",
        "signedness_cast_app.power_parameter",
        "signedness_cast_app.compound_add",
        "signedness_cast_app.compound_subtract",
        "signedness_cast_app.compound_multiply",
        "signedness_cast_app.compound_divide",
        "signedness_cast_app.compound_remainder",
        "signedness_cast_app.compound_and",
        "signedness_cast_app.compound_or",
        "signedness_cast_app.compound_xor",
        "signedness_cast_app.compound_shift_left",
        "signedness_cast_app.compound_shift_right",
        "signedness_cast_app.compound_arithmetic_left",
        "signedness_cast_app.compound_arithmetic_right",
        "signedness_cast_app.prefix_increment",
        "signedness_cast_app.postfix_decrement",
        "signedness_cast_app.compound_selected",
        "signedness_cast_app.compound_unknown",
        "signedness_cast_app.final_value",
        "signedness_cast_app.onehot_zero",
        "signedness_cast_app.onehot_single",
        "signedness_cast_app.onehot_multiple",
        "signedness_cast_app.onehot_unknown",
        "signedness_cast_app.onehot0_zero",
        "signedness_cast_app.onehot0_single",
        "signedness_cast_app.onehot0_multiple",
        "signedness_cast_app.onehot0_unknown",
        "signedness_cast_app.countones_zero",
        "signedness_cast_app.countones_single",
        "signedness_cast_app.countones_multiple",
        "signedness_cast_app.countones_unknown",
        "signedness_cast_app.countbits_known",
        "signedness_cast_app.countbits_unknown",
        "signedness_cast_app.countbits_zero_x",
        "signedness_cast_app.indexed_update",
        "signedness_cast_app.update_calls"
    };
    const auto reference = run(
        std::move(*reference_project),
        fsim::app::SimulationEngine::interpreter,
        signal_paths);
    const auto compiled = run(
        std::move(*compiled_project),
        fsim::app::SimulationEngine::compiled,
        signal_paths);

    assert(reference.result.status == fsim::runtime::RunStatus::stopped);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert((
        compiled.values
        == std::vector<std::string> {
            "1",
            "1",
            "1111",
            "0000",
            "0",
            "1",
            "00000000000000000000000000000100",
            "00000000000000000000000000001000",
            "00000000000000000000000000000111",
            "00000000000000000000000000000100",
            "00000000000000000000000000000100",
            "00000000000000000000000000000111",
            "00000000000000000000000000000100",
            "00000000000000000000000000000001",
            "00000000000000000000000000000010",
            "00000000000000000000000000000101",
            "00000000000000000000000000000010",
            "00000000000000000000000000000101",
            "00000000000000000000000000000100",
            "11111111111111111111111111111111",
            "00000000000000000000000000000001",
            "00000000000000000000000000000000",
            "01010001",
            "00000001",
            "0000000001000000",
            "00000000",
            "11111111",
            "XXXXXXXX",
            "XXXXXXXX",
            "01010001",
            "00010101",
            "00001101",
            "00100110",
            "00010001",
            "00000010",
            "10100000",
            "10101111",
            "01010101",
            "00000010",
            "01000000",
            "00000010",
            "11111100",
            "11111111",
            "11111111",
            "10100110",
            "XXXXXXXX",
            "00010101",
            "0",
            "1",
            "0",
            "1",
            "1",
            "1",
            "0",
            "1",
            "00000000000000000000000000000000",
            "00000000000000000000000000000001",
            "00000000000000000000000000000011",
            "00000000000000000000000000000001",
            "00000000000000000000000000000010",
            "00000000000000000000000000000010",
            "00000000000000000000000000000010",
            "10100100",
            "00000000000000000000000000000001" }));
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 2);
    assert(compiled.compiled_modules == 1);
#else
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
#endif
}

} // namespace

void run_expression_application_part2(
    const std::filesystem::path& directory);

int main()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-expression-application-test-"
            + std::to_string(suffix))
    };
    std::filesystem::create_directories(directory.path);

    const auto source = directory.path / "wildcard_equality.sv";
    {
        std::ofstream output(source);
        output << R"(
module wildcard_equality_app;
  logic masked;
  logic left_unknown;
  logic known_mismatch;
  logic wildcard_neq;
  logic logical_equal;
  initial begin
    masked = 4'b10x1 ==? 4'b10?1;
    left_unknown = 4'b10x1 ==? 4'b1011;
    known_mismatch = 4'b1101 ==? 4'b10?1;
    wildcard_neq = 4'b1001 !=? 4'b10z1;
    logical_equal = 2'bx0 == 2'bx1;
    $finish;
  end
endmodule
)";
    }
    const auto vhdl_source = directory.path / "shift_rotate.vhd";
    {
        std::ofstream output(vhdl_source);
        output << R"(
entity shift_rotate_app is
end entity;

architecture rtl of shift_rotate_app is
  constant declaration_conditional : std_logic_vector(128 downto 0) :=
    129X"1" when true else 129X"0";
  constant case_declaration : std_logic_vector(128 downto 0) :=
    (case false is
       when true => 129X"2",
       when others => declaration_conditional);
  constant case_range_declaration : integer :=
    (case 2 is
       when 1 to 3 => 11,
       when others => 22);
  signal value : signed(7 downto 0);
  signal known_value : signed(7 downto 0);
  signal arithmetic_left : signed(7 downto 0);
  signal rotated_left : signed(7 downto 0);
  signal rotated_right : signed(7 downto 0);
  signal reversed_logical_left : signed(7 downto 0);
  signal reversed_arithmetic_right : signed(7 downto 0);
  signal reversed_rotate_left : signed(7 downto 0);
  signal oversized_arithmetic_left : signed(7 downto 0);
  signal wrapped_rotate_left : signed(7 downto 0);
  signal wrapped_rotate_right : signed(7 downto 0);
  signal absolute_unknown : signed(7 downto 0);
  signal absolute_known : signed(7 downto 0);
  signal power_positive : unsigned(7 downto 0);
  signal power_negative : signed(7 downto 0);
  signal power_zero : unsigned(7 downto 0);
  signal conditional_true : std_logic_vector(7 downto 0);
  signal conditional_false : std_logic_vector(7 downto 0);
  signal conditional_chain : std_logic_vector(7 downto 0);
  signal conditional_selected : std_logic_vector(7 downto 0);
  signal expression_conditional : std_logic_vector(128 downto 0);
  signal short_circuit_conditional : integer;
  signal case_expression : std_logic_vector(7 downto 0);
  signal case_range_expression : integer;
  signal external_name_expression : signed(7 downto 0);
  signal selected_choice : std_logic_vector(7 downto 0);
  signal selected_default : std_logic_vector(7 downto 0);
  signal dynamic_selector : std_logic_vector(1 downto 0);
  signal selected_dynamic : std_logic_vector(7 downto 0);
begin
  value <= 8B"10X0_000Z";
  known_value <= 8X"FB";
  calculate: process(value, known_value)
  begin
    arithmetic_left <= value sla 1;
    rotated_left <= value rol 1;
    rotated_right <= value ror 1;
    reversed_logical_left <= value sll -1;
    reversed_arithmetic_right <= value sra -1;
    reversed_rotate_left <= value rol -1;
    oversized_arithmetic_left <= value sla 8;
    wrapped_rotate_left <= value rol 9;
    wrapped_rotate_right <= value ror 9;
    absolute_unknown <= abs value;
    absolute_known <= abs known_value;
    power_positive <= "00000011" ** 4;
    power_negative <= "11111110" ** 3;
    power_zero <= "00000111" ** 0;
    conditional_true <=
      8X"A5" when true else 8X"5A";
    conditional_false <=
      "10100101" when false else "01011010";
    conditional_chain <=
      "00000001" when false else
      "00000010" when true else
      "00000011";
    conditional_selected(3 downto 0) <=
      "0110" when true else "1001";
  end process;

  expression_conditional <=
    (129X"2" when false else case_declaration);
  short_circuit_conditional <=
    (7 when true else 1 / 0);
  case_expression <=
    (case dynamic_selector is
       when "10" | "11" => 8X"3C",
       when others => 8X"C3");
  case_range_expression <= case_range_declaration;
  external_name_expression <=
    << signal .shift_rotate_app.known_value : signed(7 downto 0) >>;

  choose_known: with "01" select
    selected_choice <=
      8O"245" when "00" | "01",
      8D"90" when others;

  choose_default: with "10" select
    selected_default <=
      "10100101" when "00" | "01",
      "01011010" when others;

  drive_selector: process
  begin
    dynamic_selector <= "00";
    wait for 1 ns;
    dynamic_selector <= "10";
    wait;
  end process;

  choose_dynamic: with dynamic_selector select
    selected_dynamic <=
      "10100101" when "00" | "01",
      "01011010" when others;
end architecture;
)";
    }
    const auto external_mismatch_source = directory.path / "external_mismatch.vhd";
    {
        std::ofstream output(external_mismatch_source);
        output << R"(
entity external_mismatch_app is
end entity;

architecture rtl of external_mismatch_app is
  signal source_value : signed(7 downto 0);
  signal result : signed(7 downto 0);
begin
  result <=
    << signal .external_mismatch_app.source_value : signed(6 downto 0) >>;
end architecture;
)";
    }
    const auto verilog_power_source = directory.path / "power.v";
    {
        std::ofstream output(verilog_power_source);
        output << R"(
module verilog_power_app;
  reg [7:0] positive;
  reg [15:0] left_associative;
  initial begin
    positive = 8'd3 ** 8'd4;
    left_associative = 16'd2 ** 16'd3 ** 16'd2;
    $finish;
  end
endmodule
)";
    }
    const auto clog2_source = directory.path / "clog2.sv";
    {
        std::ofstream output(clog2_source);
        output << R"(
module clog2_child #(
  parameter int DEPTH = 1,
  localparam int WIDTH = $clog2(DEPTH)
) (
  output logic [WIDTH-1:0] result
);
  assign result = WIDTH;
endmodule

module clog2_application;
  logic [3:0] narrow_result;
  logic [4:0] wide_result;
  clog2_child #(.DEPTH(9)) narrow(.result(narrow_result));
  clog2_child #(.DEPTH(17)) wide(.result(wide_result));
endmodule
)";
    }
    const auto signedness_source = directory.path / "signedness_cast.sv";
    {
        std::ofstream output(signedness_source);
        output << R"(
module signedness_cast_app #(
  parameter int PARAMETER_POWER = 3 ** 4
);
  logic [3:0] unsigned_value;
  logic signed [3:0] signed_value;
  logic signed_less;
  logic unsigned_less;
  logic [3:0] signed_shift;
  logic [3:0] unsigned_shift;
  logic known_is_unknown;
  logic xz_is_unknown;
  logic [31:0] object_bits;
  logic [31:0] concatenation_bits;
  logic [7:4] descending;
  logic [2:5] ascending;
  logic signed [31:0] descending_left;
  logic signed [31:0] descending_right;
  logic signed [31:0] descending_low;
  logic signed [31:0] descending_high;
  logic signed [31:0] descending_size;
  logic signed [31:0] descending_increment;
  logic signed [31:0] ascending_left;
  logic signed [31:0] ascending_right;
  logic signed [31:0] ascending_low;
  logic signed [31:0] ascending_high;
  logic signed [31:0] ascending_size;
  logic signed [31:0] ascending_increment;
  logic signed [31:0] dimensions;
  logic signed [31:0] unpacked_dimensions;
  logic [7:0] power_positive;
  logic [7:0] power_zero;
  logic [15:0] power_left_associative;
  logic signed [7:0] power_negative_exponent;
  logic signed [7:0] power_minus_one_negative;
  logic signed [7:0] power_zero_negative;
  logic [7:0] power_unknown;
  logic [7:0] power_parameter;
  logic [7:0] compound_add;
  logic [7:0] compound_subtract;
  logic [7:0] compound_multiply;
  logic [7:0] compound_divide;
  logic [7:0] compound_remainder;
  logic [7:0] compound_and;
  logic [7:0] compound_or;
  logic [7:0] compound_xor;
  logic [7:0] compound_shift_left;
  logic [7:0] compound_shift_right;
  logic [7:0] compound_arithmetic_left;
  logic signed [7:0] compound_arithmetic_right;
  logic [7:0] prefix_increment;
  logic [7:0] postfix_decrement;
  logic [7:0] compound_selected;
  logic [7:0] compound_unknown;
  logic [7:0] final_value;
  logic onehot_zero;
  logic onehot_single;
  logic onehot_multiple;
  logic onehot_unknown;
  logic onehot0_zero;
  logic onehot0_single;
  logic onehot0_multiple;
  logic onehot0_unknown;
  logic signed [31:0] countones_zero;
  logic signed [31:0] countones_single;
  logic signed [31:0] countones_multiple;
  logic signed [31:0] countones_unknown;
  logic signed [31:0] countbits_known;
  logic signed [31:0] countbits_unknown;
  logic signed [31:0] countbits_zero_x;
  logic [7:0] indexed_update;
  logic signed [31:0] update_calls;
  function automatic int next_update_index();
    update_calls = update_calls + 1;
    return 2;
  endfunction
  initial begin
    unsigned_value = 4'b1111;
    signed_value = 4'b0001;
    signed_less = $signed(unsigned_value) < signed_value;
    unsigned_less = $unsigned(signed_value) < unsigned_value;
    signed_shift = $signed(unsigned_value) >>> 1;
    unsigned_shift = $unsigned(signed_value) >>> 1;
    known_is_unknown = $isunknown(unsigned_value);
    xz_is_unknown = $isunknown(4'b10xz);
    object_bits = $bits(unsigned_value);
    concatenation_bits = $bits({unsigned_value, signed_value});
    descending_left = $left(descending, 1);
    descending_right = $right(descending);
    descending_low = $low(descending);
    descending_high = $high(descending);
    descending_size = $size(descending);
    descending_increment = $increment(descending);
    ascending_left = $left(ascending);
    ascending_right = $right(ascending);
    ascending_low = $low(ascending);
    ascending_high = $high(ascending);
    ascending_size = $size(ascending, 1);
    ascending_increment = $increment(ascending);
    dimensions = $dimensions(descending);
    unpacked_dimensions = $unpacked_dimensions(ascending);
    power_positive = 8'd3 ** 8'd4;
    power_zero = 8'd7 ** 8'd0;
    power_left_associative =
        16'd2 ** 16'd3 ** 16'd2;
    power_negative_exponent =
        $signed(8'hfe) ** $signed(8'hfd);
    power_minus_one_negative =
        $signed(8'hff) ** $signed(8'hfd);
    power_zero_negative =
        $signed(8'h00) ** $signed(8'hff);
    power_unknown = 8'b000000x1 ** 8'd2;
    power_parameter = PARAMETER_POWER;
    compound_add = 8'h12;
    compound_add += 8'h03;
    compound_subtract = 8'h12;
    compound_subtract -= 8'h05;
    compound_multiply = 8'h13;
    compound_multiply *= 8'h02;
    compound_divide = 8'h44;
    compound_divide /= 8'h04;
    compound_remainder = 8'h12;
    compound_remainder %= 8'h04;
    compound_and = 8'had;
    compound_and &= 8'hf0;
    compound_or = 8'ha0;
    compound_or |= 8'h0f;
    compound_xor = 8'hff;
    compound_xor ^= 8'haa;
    compound_shift_left = 8'h81;
    compound_shift_left <<= 8'h01;
    compound_shift_right = 8'h81;
    compound_shift_right >>= 8'h01;
    compound_arithmetic_left = 8'h81;
    compound_arithmetic_left <<<= 8'h01;
    compound_arithmetic_right = -8'sd16;
    compound_arithmetic_right >>>= 8'sd2;
    prefix_increment = 8'hfe;
    ++prefix_increment;
    postfix_decrement = 8'h00;
    postfix_decrement--;
    compound_selected = 8'ha5;
    compound_selected[3:0] += 4'h1;
    compound_unknown = 8'b000000x1;
    compound_unknown *= 8'd2;
    onehot_zero = $onehot(4'b0000);
    onehot_single = $onehot(4'b0010);
    onehot_multiple = $onehot(4'b1010);
    onehot_unknown = $onehot(4'bx001);
    onehot0_zero = $onehot0(4'b0000);
    onehot0_single = $onehot0(4'b0010);
    onehot0_multiple = $onehot0(4'b1010);
    onehot0_unknown = $onehot0(4'bz001);
    countones_zero = $countones(4'b0000);
    countones_single = $countones(4'b0010);
    countones_multiple = $countones(4'b1011);
    countones_unknown = $countones(4'bxz01);
    countbits_known = $countbits(4'b10xz, 1'b0, 1'b1);
    countbits_unknown = $countbits(4'b10xz, 1'bx, 1'bz);
    countbits_zero_x = $countbits(4'b10xz, 1'b0, 1'bx);
    indexed_update = 8'ha0;
    update_calls = 0;
    indexed_update[next_update_index()] += 1'b1;
    $finish;
  end
  final begin
    final_value = 8'h15;
  end
endmodule
)";
    }
    const auto literal_closure_source = directory.path / "literal_closure.sv";
    {
        std::ofstream output(literal_closure_source);
        output << R"(
module literal_closure_app;
  logic [256:0] decimal_x;
  logic [256:0] decimal_z;
  logic [256:0] decimal_question;
  logic [256:0] unbased_one;
  logic [256:0] unbased_x;
  initial begin
    decimal_x = 257'dx;
    decimal_z = 257'dz;
    decimal_question = 257'd?;
    unbased_one = '1;
    unbased_x = 'x;
    $finish;
  end
endmodule
)";
    }
    const auto packed_index_source = directory.path / "packed_index.sv";
    {
        std::ofstream output(packed_index_source);
        output << R"(
module packed_index_app;
  logic [3:0] values;
  logic [1:0] slot;
  logic wrapped_select;
  initial begin
    values = 4'b0001;
    slot = 2'b11;
    wrapped_select = values[slot + 1'b1];
  end
endmodule
)";
    }
    const auto concurrent_assertion_source = directory.path / "concurrent_assertion.vhd";
    {
        std::ofstream output(concurrent_assertion_source);
        output << R"(
entity concurrent_assertion_app is
end entity;

architecture rtl of concurrent_assertion_app is
  signal passed : boolean;
begin
  passed <= true;
  constant_check: assert true
    report "unreachable concurrent assertion" severity failure;
end architecture;
)";
    }
    const auto falling_edge_source = directory.path / "falling_edge.vhd";
    {
        std::ofstream output(falling_edge_source);
        output << R"(
entity falling_edge_app is
end entity;

architecture rtl of falling_edge_app is
  signal clk : std_logic;
  signal hit : std_logic;
  signal legacy_hit : std_logic;
  signal falling_previous : std_logic;
  signal elapsed : signed(63 downto 0);
  signal active_elapsed : signed(63 downto 0);
  signal stable_during_event : boolean;
  signal stable_after_event : boolean;
  signal redundant : std_logic;
  signal redundant_active : boolean;
  signal redundant_event : boolean;
  signal driven : std_logic;
  signal driving_seen : boolean;
  signal driving_value_seen : std_logic;
  signal driven_vector : std_logic_vector(3 downto 0);
  signal driving_vector_seen : std_logic_vector(3 downto 0);
  signal foreign_driving : boolean;
  signal single : std_logic;
  signal stable_window_early : boolean;
  signal quiet_window_early : boolean;
  signal delayed_sample : std_logic;
  signal redundant_transaction : boolean;
  signal single_transaction : boolean;
  signal stable_window_late : boolean;
  signal quiet_window_late : boolean;
  signal transaction_sensitive : boolean;
  signal transaction_waited : boolean;
begin
  stimulus: process
  begin
    clk <= '1';
    redundant <= '0';
    driven <= '1';
    driven_vector <= "10ZX";
    single <= '1';
    wait for 1 ns;
    driving_seen <= driven'driving;
    driving_value_seen <= driven'driving_value;
    driving_vector_seen <= driven_vector'driving_value;
    clk <= '0';
    redundant <= '0';
    wait for 0 ns;
    redundant <= '0';
    wait for 0 ns;
    redundant <= '0';
    wait for 1 ns;
    wait;
  end process;

  resolved_driver: process
  begin
    driven <= '0';
    wait;
  end process;

  capture: process(clk)
  begin
    if falling_edge(clk) then
      hit <= '1';
      falling_previous <= clk'last_value;
      if redundant'active then
        redundant_active <= true;
      else
        redundant_active <= false;
      end if;
      if redundant'event then
        redundant_event <= true;
      else
        redundant_event <= false;
      end if;
    end if;
  end process;

  legacy_capture: process(clk)
  begin
    if clk'event and clk = '1' then
      legacy_hit <= '1';
      if clk'stable then
        stable_during_event <= false;
      else
        stable_during_event <= true;
      end if;
    end if;
  end process;

  transaction_capture: process(redundant'transaction)
  begin
    transaction_sensitive <= redundant'transaction;
  end process;

  transaction_wait: process
  begin
    wait on single'transaction;
    transaction_waited <= true;
    wait;
  end process;

  observe_elapsed: process
  begin
    wait for 2 ns;
    elapsed <= clk'last_event;
    active_elapsed <= redundant'last_active;
    foreign_driving <= driven'driving;
    wait for 0 ns;
    stable_window_early <= clk'stable(2 ns);
    quiet_window_early <= redundant'quiet(2 ns);
    delayed_sample <= clk'delayed(1 ns);
    redundant_transaction <= redundant'transaction;
    single_transaction <= single'transaction;
    wait for 2 ns;
    wait for 0 ns;
    stable_window_late <= clk'stable(2 ns);
    quiet_window_late <= redundant'quiet(2 ns);
    stable_after_event <= clk'stable;
    wait;
  end process;
end architecture;
)";
    }
    const auto stop_source = directory.path / "stop.sv";
    {
        std::ofstream output(stop_source);
        output << R"(
module stop_app;
  logic before_stop;
  logic after_stop;
  logic final_hit;
  initial begin
    before_stop = 1'b1;
    $stop;
    after_stop = 1'b1;
    $finish;
  end
  final final_hit = 1'b1;
endmodule
)";
    }
    const auto fatal_source = directory.path / "fatal.sv";
    {
        std::ofstream output(fatal_source);
        output << R"(
module fatal_app;
  initial $fatal(1, "fatal\nsource\tmessage \"quoted\"");
endmodule
)";
    }
    const auto exit_source = directory.path / "exit.sv";
    {
        std::ofstream output(exit_source);
        output << R"(
program first_program(
    output logic after_exit,
    output logic background);
  initial begin
    after_exit = 1'b0;
    background = 1'b0;
    fork
      begin
        #5 background = 1'b1;
      end
    join_none
    #1 $exit();
    after_exit = 1'b1;
  end
endprogram

program second_program(output logic done);
  initial begin
    done = 1'b0;
    #2 done = 1'b1;
  end
endprogram

module exit_app;
  logic first_after;
  logic first_background;
  logic second_done;
  logic module_late;
  logic final_hit;
  first_program first(first_after, first_background);
  second_program second(second_done);
  initial begin
    module_late = 1'b0;
    #3 module_late = 1'b1;
  end
  final final_hit = 1'b1;
endmodule
)";
    }
    const auto sampled_source = directory.path / "sampled_values.sv";
    {
        std::ofstream output(sampled_source);
        output << R"(
module sampled_value_app;
  logic clock;
  logic data;
  logic gate;
  logic [6:0] first;
  logic [6:0] second;
  logic [6:0] third;
  logic [3:0] explicit_first;
  logic [3:0] explicit_second;
  logic [3:0] explicit_third;
  logic [4:0] global_past_first;
  logic [4:0] global_past_second;
  logic [4:0] global_past_third;
  logic [4:0] global_future_first;
  logic [4:0] global_future_second;
  logic [4:0] global_future_third;
  initial begin
    @(posedge clock);
    first = {$sampled(data), $rose(data), $fell(data),
             $stable(data), $changed(data), $past(data), $past(data, 2)};
    explicit_first = {$sampled(data, @(posedge clock)),
                      $rose(data, @(posedge clock)),
                      $past(data, 1, , @(posedge clock)),
                      $past(data, 1, gate, @(posedge clock))};
    global_past_first = {$past_gclk(data), $rose_gclk(data),
                         $fell_gclk(data), $stable_gclk(data),
                         $changed_gclk(data)};
    global_future_first = {$future_gclk(data), $rising_gclk(data),
                           $falling_gclk(data), $steady_gclk(data),
                           $changing_gclk(data)};
    @(posedge clock);
    second = {$sampled(data), $rose(data), $fell(data),
              $stable(data), $changed(data), $past(data), $past(data, 2)};
    explicit_second = {$sampled(data, @(posedge clock)),
                       $rose(data, @(posedge clock)),
                       $past(data, 1, , @(posedge clock)),
                       $past(data, 1, gate, @(posedge clock))};
    global_past_second = {$past_gclk(data), $rose_gclk(data),
                          $fell_gclk(data), $stable_gclk(data),
                          $changed_gclk(data)};
    global_future_second = {$future_gclk(data), $rising_gclk(data),
                            $falling_gclk(data), $steady_gclk(data),
                            $changing_gclk(data)};
    @(posedge clock);
    third = {$sampled(data), $rose(data), $fell(data),
             $stable(data), $changed(data), $past(data), $past(data, 2)};
    explicit_third = {$sampled(data, @(posedge clock)),
                      $fell(data, @(posedge clock)),
                      $past(data, 1, , @(posedge clock)),
                      $past(data, 1, gate, @(posedge clock))};
    global_past_third = {$past_gclk(data), $rose_gclk(data),
                         $fell_gclk(data), $stable_gclk(data),
                         $changed_gclk(data)};
    global_future_third = {$future_gclk(data), $rising_gclk(data),
                           $falling_gclk(data), $steady_gclk(data),
                           $changing_gclk(data)};
  end
  initial begin
    clock = 1'b0;
    data = 1'b0;
    gate = 1'b1;
    #1 data = 1'b1;
    clock = 1'b1;
    #1 gate = 1'b0;
    clock = 1'b0;
    #1 data = 1'b0;
    clock = 1'b1;
    #1 gate = 1'b1;
    clock = 1'b0;
    #1 data = 1'b1;
    clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    }
    const auto attribute_source = directory.path / "attributes.vhd";
    {
        std::ofstream output(attribute_source);
        output << R"(
entity attribute_app is
end entity;

architecture rtl of attribute_app is
  signal descending : std_logic_vector(7 downto 4);
  signal ascending : std_logic_vector(2 to 5);
  signal descending_left : signed(31 downto 0);
  signal descending_right : signed(31 downto 0);
  signal descending_low : signed(31 downto 0);
  signal descending_high : signed(31 downto 0);
  signal descending_length : signed(31 downto 0);
  signal descending_ascending : boolean;
  signal ascending_left : signed(31 downto 0);
  signal ascending_right : signed(31 downto 0);
  signal ascending_low : signed(31 downto 0);
  signal ascending_high : signed(31 downto 0);
  signal ascending_length : signed(31 downto 0);
  signal ascending_ascending : boolean;
begin
  observe: process
  begin
    descending_left <= descending'left;
    descending_right <= descending'right(1);
    descending_low <= descending'low;
    descending_high <= descending'high;
    descending_length <= descending'length;
    descending_ascending <= descending'ascending;
    ascending_left <= ascending'left;
    ascending_right <= ascending'right;
    ascending_low <= ascending'low;
    ascending_high <= ascending'high;
    ascending_length <= ascending'length(1);
    ascending_ascending <= ascending'ascending;
    wait;
  end process;
end architecture;
)";
    }

    test_wildcard_equality(
        directory.path, source, fsim::project::Optimization::o0);
    test_wildcard_equality(
        directory.path, source, fsim::project::Optimization::o2);
    test_vhdl_shift_rotate(
        directory.path,
        vhdl_source,
        fsim::project::Optimization::o0);
    test_vhdl_shift_rotate(
        directory.path,
        vhdl_source,
        fsim::project::Optimization::o2);
    test_vhdl_external_name_mismatch(
        directory.path, external_mismatch_source);
    test_verilog_power(
        directory.path,
        verilog_power_source,
        fsim::project::Optimization::o0);
    test_verilog_power(
        directory.path,
        verilog_power_source,
        fsim::project::Optimization::o2);
    test_systemverilog_clog2(
        directory.path,
        clog2_source,
        fsim::project::Optimization::o0);
    test_systemverilog_clog2(
        directory.path,
        clog2_source,
        fsim::project::Optimization::o2);
    test_systemverilog_signedness_casts(
        directory.path,
        signedness_source,
        fsim::project::Optimization::o0);
    test_systemverilog_signedness_casts(
        directory.path,
        signedness_source,
        fsim::project::Optimization::o2);
    run_expression_application_part2(directory.path);
    return 0;
}
