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
#include <optional>
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
    std::array<std::string, 5> values;
    std::uint64_t protected_value { };
    std::string protected_wide;
    std::string access_local;
    std::string physical_local;
    std::string debugger_output;
    std::string vcd;
    std::vector<std::string> specialization_keys;
    bool analysis_cache_hit { };
    std::size_t compiled_processes { };
    std::size_t compiled_modules { };
    fsim::app::NativeCacheStatistics cache;
};

void write_source(
    const std::filesystem::path& source,
    const std::uint32_t increment)
{
    // FSIM-CONFORMANCE CF-VHDL-ACCESS-001 source=SRC-IEEE-P1076 expectation=execute
    // FSIM-CONFORMANCE CF-VHDL-PROTECTED-001 source=SRC-UVVM expectation=execute
    // FSIM-CONFORMANCE CF-VHDL-PHYSICAL-001 source=SRC-IEEE-P1076 expectation=execute
    std::ofstream output { source };
    output << R"(
package Counter_Types is
  type Counter is protected
    procedure Add(Value : integer);
    impure function Read return integer;
  end protected Counter;
  type Wide_Store is protected
    procedure Fill;
  end protected Wide_Store;
end package;

package body Counter_Types is
  type Counter is protected body
    variable Current_Value : integer := 7;
    procedure Add(Value : integer) is
    begin
      Current_Value := Current_Value + Value;
    end procedure;
    impure function Read return integer is
    begin
      return Current_Value;
    end function;
  end protected body Counter;
  type Wide_Store is protected body
    variable Wide_Value : bit_vector(136 downto 0);
    procedure Fill is
    begin
      Wide_Value := (others => '1');
    end procedure;
  end protected body Wide_Store;
end package body;

entity Advanced_Types is
end entity;

use work.Counter_Types.all;
architecture rtl of advanced_types is
  type Byte_Pointer is access bit_vector(7 downto 0);
  type Distance is range -2000000 to 2000000 units
    um;
    mm = 1000 um;
  end units Distance;
  shared variable Shared_Counter : Counter;
  shared variable Shared_Wide : Wide_Store;
  signal Access_Result : bit_vector(7 downto 0);
  signal Was_Null : boolean;
  signal Was_Deallocated : boolean;
  signal Fresh_Identity : boolean;
  signal Physical_Result : Distance;
begin
  exercise : process
    variable Pointer : Byte_Pointer;
    variable Alias_Value : Byte_Pointer;
    variable Local_Distance : Distance;
  begin
    Was_Null <= Pointer = null;
    Pointer := new bit_vector'("10100101");
    Alias_Value := Pointer;
    Access_Result <= Pointer.all;
    Deallocate(Pointer);
    Was_Deallocated <= Pointer = null;
    Pointer := new bit_vector'("01011010");
    Fresh_Identity <= Alias_Value /= Pointer;
    Local_Distance := 2 mm;
    Physical_Result <= Local_Distance + )"
           << increment << R"( um;
    Shared_Counter.Add()"
           << increment << R"();
    Shared_Counter.Add(Shared_Counter.Read());
    wait;
  end process;

  wide_exercise : process
  begin
    Shared_Wide.Fill;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization)
{
    fsim::project::Config config;
    config.base_directory = directory;
    config.project.name = "vhdl-advanced-types";
    config.project.top = "vhdl:work.advanced_types(rtl)";
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

    std::optional<std::pair<
        fsim::runtime::simir::ProcessId, std::size_t>>
        access_local;
    std::optional<std::pair<
        fsim::runtime::simir::ProcessId, std::size_t>>
        physical_local;
    for (const auto& process : project->design.processes()) {
        for (std::size_t index = 0;
            index < process.debug_locals.size(); ++index) {
            const auto& local = process.debug_locals[index];
            if (local.name == "pointer") {
                assert(local.type_name == "byte_pointer" && local.width == 32);
                access_local = std::pair { process.id, index };
            } else if (local.name == "local_distance") {
                assert(local.type_name == "distance" && local.width == 32);
                physical_local = std::pair { process.id, index };
            }
        }
    }
    assert(access_local && physical_local);
    const auto protected_member = project->design.find_container(
        "advanced_types.shared_counter.current_value");
    const auto protected_wide = project->design.find_container(
        "advanced_types.shared_wide.wide_value");
    assert(protected_member && protected_wide);

    Capture capture;
    capture.analysis_cache_hit = project->cache_hit;
    capture.specialization_keys = project->specialization_cache_keys;
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine
    };
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.cache = simulation.native_cache_statistics();

    constexpr std::array<std::string_view, 5> paths {
        "advanced_types.access_result",
        "advanced_types.was_null",
        "advanced_types.was_deallocated",
        "advanced_types.fresh_identity",
        "advanced_types.physical_result"
    };
    constexpr std::array<std::size_t, 5> widths { 8, 1, 1, 1, 32 };
    std::array<fsim::runtime::simir::SignalId, 5> signals { };
    std::array<fsim::runtime::VcdSignal, 5> traces { };
    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd { vcd_output, "1ns", 64 };
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto signal = simulation.find_signal(paths[index]);
        assert(signal);
        signals[index] = *signal;
        traces[index] = vcd.declare_signal(
            std::string { paths[index] }, widths[index]);
    }
    vcd.begin();
    for (std::size_t index = 0; index < signals.size(); ++index) {
        vcd.change(traces[index], simulation.read_signal(signals[index]));
    }
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t) {
            const auto found = std::find(signals.begin(), signals.end(), signal);
            if (found == signals.end()) {
                return;
            }
            const auto index = static_cast<std::size_t>(
                std::distance(signals.begin(), found));
            vcd.set_time(time);
            vcd.change(traces[index], value);
        });

    capture.result = simulation.run();
    for (std::size_t index = 0; index < signals.size(); ++index) {
        capture.values[index] = simulation.read_signal(signals[index]).to_msb_string();
    }
    capture.protected_value = simulation.read_container_object(*protected_member)
                                  .elements.at(0)
                                  .low_word()
                                  .aval;
    capture.protected_wide = simulation.read_container_object(*protected_wide)
                                 .elements.at(0)
                                 .to_msb_string();
    capture.access_local = simulation.read_process_local(
                                         access_local->first, access_local->second)
                               .to_msb_string();
    capture.physical_local = simulation.read_process_local(
                                           physical_local->first, physical_local->second)
                                 .to_msb_string();
    {
        std::ostringstream debugger_output;
        std::ostringstream debugger_error;
        fsim::app::DebuggerControl debugger {
            simulation, debugger_output, debugger_error
        };
        debugger.execute({ "show", "access_result" });
        debugger.execute({ "show", "physical_result" });
        assert(debugger_error.str().empty());
        capture.debugger_output = debugger_output.str();
    }
    vcd.flush();
    capture.vcd = vcd_output.str();
    return capture;
}

void verify_capture(
    const Capture& capture,
    const std::uint32_t increment)
{
    assert(capture.result.status == fsim::runtime::RunStatus::completed);
    assert(capture.values[0] == "10100101");
    assert(capture.values[1] == "1");
    assert(capture.values[2] == "1");
    assert(capture.values[3] == "1");
    assert(
        capture.values[4]
        == fsim::runtime::PackedLogic4::from_aval_bval(
            32, 2000 + increment, 0)
            .to_msb_string());
    assert(capture.protected_value == 2 * (7 + increment));
    assert(capture.protected_wide == std::string(137, '1'));
    assert(capture.access_local
        == "00000000000000000000000000000010");
    assert(
        capture.physical_local
        == "00000000000000000000011111010000");
    assert(
        capture.debugger_output.find("access_result = 10100101")
        != std::string::npos);
    assert(
        capture.debugger_output.find("physical_result = ")
        != std::string::npos);
    assert(capture.vcd.find("b10100101") != std::string::npos);
    assert(capture.vcd.find(capture.values[4]) != std::string::npos);
}

void verify_vhdl_1987_declaration_execution(
    const std::filesystem::path& directory)
{
    const auto legacy_directory = directory / "legacy-1987";
    std::filesystem::create_directories(legacy_directory);
    const auto source = legacy_directory / "legacy_declarations.vhd";
    {
        std::ofstream output { source };
        output << R"(
entity Legacy_Declarations is
  port (Result : out bit_vector(136 downto 0));
end entity;
architecture rtl of Legacy_Declarations is
begin
  exercise : process
  begin
    Result <= B")"
               << std::string(137U, '1') << R"(";
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }

    fsim::project::Config config;
    config.base_directory = legacy_directory;
    config.project.name = "vhdl87-declarations";
    config.project.top = "vhdl:work.legacy_declarations(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = fsim::project::Optimization::o0;
    config.build.cache_path = legacy_directory / "cache";
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "1987";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(project);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas,
        fsim::app::SimulationEngine::interpreter
    };
    const auto signal = simulation.find_signal("legacy_declarations.result");
    assert(signal);
    const auto result = simulation.run();
    const auto value = simulation.read_signal(*signal).to_msb_string();
    assert(result.status == fsim::runtime::RunStatus::completed && value.size() == 137U && std::ranges::all_of(value, [](const char bit) { return bit == '1'; }));
}

void verify_vhdl_1993_shared_execution(
    const std::filesystem::path& directory)
{
    const auto legacy_directory = directory / "legacy-1993-shared";
    std::filesystem::create_directories(legacy_directory);
    const auto source = legacy_directory / "legacy_shared.vhd";
    {
        std::ofstream output { source };
        output << R"(
entity Legacy_Shared is end entity;
architecture rtl of legacy_shared is
  shared variable Count : integer := 1;
  signal Observed : integer;
begin
  first : process
  begin
    Count := Count + 2;
    Observed <= Count;
    wait;
  end process;
  second : process
  begin
    Count := Count + 3;
    wait;
  end process;
end architecture;
)";
        assert(output.good());
    }

    fsim::project::Config config;
    config.base_directory = legacy_directory;
    config.project.name = "vhdl93-shared";
    config.project.top = "vhdl:work.legacy_shared(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = fsim::project::Optimization::o0;
    config.build.cache_path = legacy_directory / "cache";
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "1993";
    sources.library = "work";
    sources.files.push_back(source);
    config.source_sets.push_back(std::move(sources));

    for (const auto engine : {
             fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::compiled }) {
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
        const auto count = simulation.find_signal("legacy_shared.count");
        const auto observed = simulation.find_signal("legacy_shared.observed");
        assert(count && observed);
        assert(simulation.read_signal(*count).low_word().aval == 1U);
        const auto result = simulation.run();
        assert(
            result.status == fsim::runtime::RunStatus::completed
            && simulation.read_signal(*count).low_word().aval == 6U
            && simulation.read_signal(*observed).low_word().aval == 3U);
    }
}

void verify_protected_revision_execution(
    const std::filesystem::path& directory)
{
    for (const std::string_view standard : { "2000", "2002" }) {
        const auto revision_directory = directory / ("protected-" + std::string { standard });
        std::filesystem::create_directories(revision_directory);
        const auto source = revision_directory / "advanced_types.vhd";
        write_source(source, 4);
        auto config = make_config(
            revision_directory, source, fsim::project::Optimization::o0);
        config.source_sets.front().standard = standard;
        const auto reference = run_once(
            config, fsim::app::SimulationEngine::interpreter);
        const auto compiled = run_once(
            config, fsim::app::SimulationEngine::compiled);
        verify_capture(reference, 4);
        verify_capture(compiled, 4);
        assert(
            reference.values == compiled.values
            && reference.protected_value == compiled.protected_value
            && reference.protected_wide == compiled.protected_wide
            && reference.result.time == compiled.result.time
            && reference.result.delta == compiled.result.delta);
    }
}

} // namespace

int main()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-vhdl-advanced-types-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);
    const auto source = directory.path / "advanced_types.vhd";

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        write_source(source, 4);
        const auto config = make_config(
            directory.path, source, optimization);
        const auto reference = run_once(
            config, fsim::app::SimulationEngine::interpreter);
        const auto cold = run_once(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_once(
            config, fsim::app::SimulationEngine::compiled);
        verify_capture(reference, 4);
        verify_capture(cold, 4);
        verify_capture(warm, 4);
        assert(!reference.analysis_cache_hit);
        assert(cold.analysis_cache_hit && warm.analysis_cache_hit);
        assert(reference.result.time == cold.result.time);
        assert(reference.result.delta == cold.result.delta);
        assert(reference.values == cold.values && cold.values == warm.values);
        assert(reference.protected_value == cold.protected_value);
        assert(reference.vcd == cold.vcd && cold.vcd == warm.vcd);
        assert(reference.specialization_keys == cold.specialization_keys);
#if defined(FSIM_HAS_LLVM)
        assert(cold.compiled_processes == 1 && cold.compiled_modules == 1);
        assert(cold.cache.hits == 0 && cold.cache.misses == 1);
        assert(warm.compiled_processes == 1 && warm.compiled_modules == 1);
        assert(warm.cache.hits == 1 && warm.cache.misses == 0);
#endif

        write_source(source, 5);
        const auto edited = run_once(
            config, fsim::app::SimulationEngine::compiled);
        verify_capture(edited, 5);
        assert(!edited.analysis_cache_hit);
        assert(edited.specialization_keys != cold.specialization_keys);
#if defined(FSIM_HAS_LLVM)
        assert(edited.compiled_processes == 1 && edited.compiled_modules == 1);
        assert(edited.cache.hits == 0 && edited.cache.misses == 1);
#endif
    }
    verify_protected_revision_execution(directory.path);
    verify_vhdl_1993_shared_execution(directory.path);
    verify_vhdl_1987_declaration_execution(directory.path);
    std::cout << "VHDL advanced type application tests passed\n";
    return 0;
}
