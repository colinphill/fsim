// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include "governed_process_limits.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

struct TemporaryDirectory {
    std::filesystem::path path;

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::string_view top,
    const fsim::project::Optimization optimization,
    const std::string_view standard = "2019")
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = std::string { top };
    config.project.top = "vhdl:work." + std::string { top } + "(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path = directory
        / (std::string { top }
            + (optimization == fsim::project::Optimization::o0
                    ? "-o0-cache" : "-o2-cache"));
    config.run.max_deltas = 100U;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = std::string { standard };
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));
    return config;
}

void write_source(
    const std::filesystem::path& path,
    const std::string_view source)
{
    std::ofstream output { path };
    output << source;
    assert(output.good());
}

std::int64_t observed_integer(
    const fsim::app::Simulation& simulation,
    const std::string_view name)
{
    const auto signal = simulation.find_signal(name);
    assert(signal);
    const auto value = simulation.read_signal(*signal).known_signed_value();
    assert(value);
    return *value;
}

std::string observed_bits(
    const fsim::app::Simulation& simulation,
    const std::string_view name)
{
    const auto signal = simulation.find_signal(name);
    assert(signal);
    return simulation.read_signal(*signal).to_msb_string();
}

void verify_engine(
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
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    assert(observed_integer(simulation, "wide_integer_runtime.sum_value")
        == INT64_C(4294967313));
    assert(observed_integer(simulation, "wide_integer_runtime.product_value")
        == INT64_C(12884901888));
    assert(observed_integer(simulation, "wide_integer_runtime.low_value")
        == std::numeric_limits<std::int64_t>::min());
    assert(observed_integer(simulation, "wide_integer_runtime.high_value")
        == std::numeric_limits<std::int64_t>::max());
    assert(observed_integer(simulation, "wide_integer_runtime.negative_value")
        == -INT64_C(4294967296));
    assert(observed_integer(simulation, "wide_integer_runtime.minimum_value")
        == std::numeric_limits<std::int64_t>::min());
    assert(observed_integer(simulation, "wide_integer_runtime.length_value")
        == INT64_C(6000000001));
    assert(observed_bits(simulation, "wide_integer_runtime.selected_bit")
        == "1");
    assert(observed_bits(simulation, "wide_integer_runtime.selected_slice")
        == "11");
    const auto target = observed_bits(
        simulation, "wide_integer_runtime.far_target");
    assert(std::ranges::count(target, '1') == 1);
#if defined(FSIM_HAS_LLVM)
    if (engine == fsim::app::SimulationEngine::compiled) {
        assert(simulation.compiled_process_count() > 0U);
        assert(simulation.compiled_module_count() > 0U);
    }
#endif
}

void verify_overflow(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    bool failed = false;
    try {
        (void)simulation.run();
    } catch (const std::exception& error) {
        failed = std::string_view { error.what() }.find(
            "VHDL integer arithmetic overflow") != std::string_view::npos;
    }
    assert(failed);
}

void verify_conditional_expression(
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
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    assert(observed_integer(
               simulation,
               "conditional_expression_runtime.contextual_result")
           == 7);
    assert(observed_integer(
               simulation,
               "conditional_expression_runtime.lazy_result")
           == 7);
#if defined(FSIM_HAS_LLVM)
    if (engine == fsim::app::SimulationEngine::compiled) {
        assert(simulation.compiled_process_count() > 0U);
    }
#endif
}

void verify_contextual_result_subtype(
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
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    assert(observed_bits(
               simulation, "contextual_result_runtime.narrow")
           == "1111");
    assert(observed_bits(
               simulation, "contextual_result_runtime.wide")
           == "00000000");
#if defined(FSIM_HAS_LLVM)
    if (engine == fsim::app::SimulationEngine::compiled) {
        assert(simulation.compiled_process_count() > 0U);
    }
#endif
}

void verify_sequential_block_hir(const fsim::project::Config& config)
{
    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project);
    const auto outer = std::ranges::find(
        project->vhdl_hir.statements(), "outer",
        &fsim::semantic::vhdl::Statement::label);
    const auto inner = std::ranges::find(
        project->vhdl_hir.statements(), "inner",
        &fsim::semantic::vhdl::Statement::label);
    assert(
        outer != project->vhdl_hir.statements().end()
        && inner != project->vhdl_hir.statements().end()
        && outer->kind == fsim::semantic::vhdl::StatementKind::block
        && inner->kind == fsim::semantic::vhdl::StatementKind::block
        && outer->nested_scope
        && inner->nested_scope
        && outer->declarations.size() == 3U
        && inner->declarations.size() == 1U
        && inner->scope == *outer->nested_scope
        && *inner->nested_scope != *outer->nested_scope);
}

} // namespace

int main()
{
    fsim::test::install_governed_process_address_space_ceiling();
    const auto serial =
        std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vhdl2019-integer-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);

    const auto runtime_source = directory.path / "wide_integer_runtime.vhd";
    write_source(runtime_source, R"(
entity wide_integer_runtime is end entity;
architecture rtl of wide_integer_runtime is
  subtype wide_range is integer range -3000000000 to 3000000000;
  signal sum_value : integer := 0;
  signal product_value : integer := 0;
  signal low_value : integer := 0;
  signal high_value : integer := 0;
  signal negative_value : integer := 0;
  signal minimum_value : integer := 0;
  signal length_value : integer := 0;
  signal far_source : bit_vector(4294967300 to 4294967303) := "1111";
  signal far_target : bit_vector(4294967300 to 4294967303) := "0000";
  signal selected_bit : bit := '0';
  signal selected_slice : bit_vector(0 to 1) := "00";
begin
  exercise : process
    variable wide_value : integer := 4294967296;
  begin
    sum_value <= wide_value + 17;
    product_value <= wide_value * 3;
    low_value <= integer'low;
    high_value <= integer'high;
    negative_value <= -4294967296;
    minimum_value <= -9223372036854775808;
    length_value <= wide_range'length;
    selected_bit <= far_source(wide_value + 5);
    selected_slice <= far_source(wide_value + 4 to wide_value + 5);
    far_target(wide_value + 6) <= '1';
    wait;
  end process;
end architecture;
)" );

    const auto overflow_source = directory.path / "wide_integer_overflow.vhd";
    write_source(overflow_source, R"(
entity wide_integer_overflow is end entity;
architecture rtl of wide_integer_overflow is
  signal result_value : integer := 0;
begin
  exercise : process
    variable maximum_value : integer := 9223372036854775807;
  begin
    result_value <= maximum_value + 1;
    wait;
  end process;
end architecture;
)" );

    const auto conditional_source =
        directory.path / "conditional_expression_runtime.vhd";
    write_source(conditional_source, R"(
entity conditional_expression_runtime is end entity;
architecture rtl of conditional_expression_runtime is
  function selected(value : integer) return integer is
  begin
    return value;
  end function;
  function selected(value : integer) return boolean is
  begin
    return value /= 0;
  end function;
  signal choose : std_logic := '1';
  signal divisor : integer := 0;
  signal contextual_result : integer := 0;
  signal lazy_result : integer := 0;
begin
  exercise : process
  begin
    contextual_result <=
        (selected(7) when choose else selected(9));
    lazy_result <=
        (7 when choose else 8 when false else 1 / divisor);
    wait;
  end process;
end architecture;
)" );

    const auto contextual_result_source =
        directory.path / "contextual_result_runtime.vhd";
    write_source(contextual_result_source, R"(
entity contextual_result_runtime is end entity;
architecture rtl of contextual_result_runtime is
  function fill(value : bit) return result_t of bit_vector is
    variable answer : result_t := (others => value);
    variable result_length : integer := result_t'length;
  begin
    if result_length = answer'length then
      return answer;
    end if;
    return (others => not value);
  end function;
  signal narrow : bit_vector(3 downto 0) := (others => '0');
  signal wide : bit_vector(7 downto 0) := (others => '1');
begin
  exercise : process
  begin
    narrow <= fill('1');
    wide <= fill('0');
    wait;
  end process;
end architecture;
)" );

    const auto sequential_block_source =
        directory.path / "sequential_block_hir.vhd";
    write_source(sequential_block_source, R"(
entity sequential_block_hir is end entity;
architecture rtl of sequential_block_hir is
  signal result_value : bit := '0';
begin
  exercise : process
  begin
    outer : block is
      constant seed : integer := 1;
      subtype local_bit is bit;
      variable value : local_bit := '1';
    begin
      inner : block
        variable nested : bit := value;
      begin
        result_value <= nested;
      end block inner;
    end block outer;
    wait;
  end process;
end architecture;
)" );

    verify_sequential_block_hir(config_for(
        directory.path,
        sequential_block_source,
        "sequential_block_hir",
        fsim::project::Optimization::o0));

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        const auto runtime_config = config_for(
            directory.path, runtime_source, "wide_integer_runtime",
            optimization);
        verify_engine(runtime_config, fsim::app::SimulationEngine::interpreter);
        verify_engine(runtime_config, fsim::app::SimulationEngine::compiled);
        const auto overflow_config = config_for(
            directory.path, overflow_source, "wide_integer_overflow",
            optimization);
        verify_overflow(
            overflow_config, fsim::app::SimulationEngine::interpreter);
        verify_overflow(
            overflow_config, fsim::app::SimulationEngine::compiled);
        const auto conditional_config = config_for(
            directory.path, conditional_source,
            "conditional_expression_runtime", optimization);
        verify_conditional_expression(
            conditional_config,
            fsim::app::SimulationEngine::interpreter);
        verify_conditional_expression(
            conditional_config,
            fsim::app::SimulationEngine::compiled);
        const auto contextual_result_config = config_for(
            directory.path, contextual_result_source,
            "contextual_result_runtime", optimization);
        verify_contextual_result_subtype(
            contextual_result_config,
            fsim::app::SimulationEngine::interpreter);
        verify_contextual_result_subtype(
            contextual_result_config,
            fsim::app::SimulationEngine::compiled);
    }

    fsim::diagnostic::Engine legacy_diagnostics;
    const auto legacy = fsim::app::build_project(
        config_for(
            directory.path, runtime_source, "wide_integer_runtime",
            fsim::project::Optimization::o0, "2008"),
        legacy_diagnostics);
    assert(!legacy);
    assert(std::ranges::any_of(
        legacy_diagnostics.diagnostics(),
        [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-INTEGER-002";
        }));

    std::cout << "VHDL-2019 integer application test passed\n";
    return 0;
}
