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
#include <variant>
#include <vector>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<std::string> values;
  std::string local_value;
  std::vector<std::string> resolved_drivers;
  std::string vcd;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-logic9";
  config.project.top = "vhdl:work.vhdl_logic9(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
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

constexpr std::array<std::string_view, 16> signal_names{
    "vhdl_logic9.source",
    "vhdl_logic9.inverted",
    "vhdl_logic9.anded",
    "vhdl_logic9.ored",
    "vhdl_logic9.xored",
    "vhdl_logic9.nanded",
    "vhdl_logic9.nored",
    "vhdl_logic9.xnored",
    "vhdl_logic9.extracted",
    "vhdl_logic9.concatenated",
    "vhdl_logic9.delayed",
    "vhdl_logic9.equal_result",
    "vhdl_logic9.different_result",
    "vhdl_logic9.local_result",
    "vhdl_logic9.last_value_result",
    "vhdl_logic9.resolved_value"};

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  const auto resolved_signal =
      project->design.find_signal("vhdl_logic9.resolved_value");
  assert(resolved_signal);
  std::vector<fsim::runtime::simir::ProcessId>
      resolved_driver_processes;
  for (const auto& process : project->design.processes()) {
    const auto writes_resolved =
        std::ranges::any_of(
            process.operations,
            [&](const auto& operation) {
              const auto writes =
                  [&](const auto* value) {
                    return value != nullptr
                        && value->signal == *resolved_signal;
                  };
              return writes(std::get_if<
                            fsim::runtime::simir::WriteBlocking>(
                            &operation))
                  || writes(std::get_if<
                            fsim::runtime::simir::WriteUpdate>(
                            &operation))
                  || writes(std::get_if<
                            fsim::runtime::simir::WriteAfter>(
                            &operation))
                  || writes(std::get_if<
                            fsim::runtime::simir::WriteProjected>(
                            &operation))
                  || writes(std::get_if<
                            fsim::runtime::simir::
                                WriteProjectedWaveform>(
                            &operation));
            });
    if (writes_resolved) {
      resolved_driver_processes.push_back(process.id);
    }
  }
  assert(resolved_driver_processes.size() == 2);
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> exact_local;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      if (process.debug_locals[index].name
          == "exact_local") {
        assert(
            process.debug_locals[index].value_kind
            == fsim::runtime::simir::ValueKind::logic9);
        exact_local = std::pair{process.id, index};
      }
    }
  }
  assert(exact_local);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};

  Capture capture;
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.native_cache =
      simulation.native_cache_statistics();

  std::array<
      fsim::runtime::simir::SignalId,
      signal_names.size()> signals{};
  std::array<
      fsim::runtime::VcdSignal,
      signal_names.size()> traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0;
       index < signal_names.size();
       ++index) {
    const auto signal =
        simulation.find_signal(signal_names[index]);
    assert(signal);
    signals[index] = *signal;
    const auto width =
        index == 8 ? 4U
        : index == 11 || index == 12 ? 1U
                                     : 8U;
    traces[index] = vcd.declare_signal(
        std::string{signal_names[index]}, width);
  }
  vcd.begin();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        traces[index],
        simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        const auto found =
            std::find(signals.begin(), signals.end(), signal);
        if (found == signals.end()) {
          return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(signals.begin(), found));
        vcd.set_time(time);
        vcd.change(traces[index], value);
      });

  capture.result = simulation.run();
  for (const auto signal : signals) {
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  capture.local_value = simulation.read_process_local(
      exact_local->first, exact_local->second).to_msb_string();
  for (const auto process : resolved_driver_processes) {
    capture.resolved_drivers.push_back(
        simulation.read_driver(
            process, *resolved_signal).to_msb_string());
  }
  std::ranges::sort(capture.resolved_drivers);
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_capture(const Capture& capture) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 2);
  assert((
      capture.values
      == std::vector<std::string>{
          "ULH-WZ01",
          "U10XXX10",
          "U01XXX01",
          "U01XXX01",
          "U10XXX10",
          "U10XXX10",
          "U10XXX10",
          "U01XXX01",
          "ULH-",
          "ULH-WZ01",
          "ZWLH-U01",
          "1",
          "1",
          "ULH-WZ01",
          "ULH-WZ01",
          "HLXWUXXW"}));
  assert(capture.local_value == "ULH-WZ01");
  assert((
      capture.resolved_drivers
      == std::vector<std::string>{
          "HZZLZ10W", "ZL-HU01Z"}));
  assert(
      capture.vcd.find("bx01xxz01")
      != std::string::npos);
  assert(
      capture.vcd.find("bzx01xx01")
      != std::string::npos);
  assert(capture.vcd.find("#2") != std::string::npos);
}

struct MixedCapture {
  fsim::runtime::RunResult result;
  std::string value;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

fsim::project::Config make_mixed_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& sv_source,
    const std::filesystem::path& vhdl_source,
    const std::string& top,
    const fsim::project::Binding& binding,
    const std::string& cache_name,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = cache_name;
  config.project.top = top;
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (cache_name
         + (optimization == fsim::project::Optimization::o0
                ? "-o0"
                : "-o2"));
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sv_sources;
  sv_sources.language =
      fsim::project::Language::system_verilog;
  sv_sources.standard = "2017";
  sv_sources.library = "work";
  sv_sources.files.push_back(sv_source);
  config.source_sets.push_back(std::move(sv_sources));

  fsim::project::SourceSet vhdl_sources;
  vhdl_sources.language = fsim::project::Language::vhdl;
  vhdl_sources.standard = "2008";
  vhdl_sources.library = "work";
  vhdl_sources.files.push_back(vhdl_source);
  config.source_sets.push_back(std::move(vhdl_sources));
  config.bindings.push_back(binding);
  return config;
}

MixedCapture run_mixed(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view signal_path) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  const auto signal = project->design.find_signal(signal_path);
  assert(signal);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  MixedCapture capture;
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.result = simulation.run();
  capture.value =
      simulation.read_signal(*signal).to_msb_string();
  return capture;
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-logic9-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "logic9.vhd";
  {
    std::ofstream output{source};
    output << R"(
entity vhdl_logic9 is
  port (
    source : out std_logic_vector(7 downto 0);
    inverted : out std_logic_vector(7 downto 0);
    anded : out std_logic_vector(7 downto 0);
    ored : out std_logic_vector(7 downto 0);
    xored : out std_logic_vector(7 downto 0);
    nanded : out std_logic_vector(7 downto 0);
    nored : out std_logic_vector(7 downto 0);
    xnored : out std_logic_vector(7 downto 0);
    extracted : out std_logic_vector(3 downto 0);
    concatenated : out std_logic_vector(7 downto 0);
    delayed : out std_logic_vector(7 downto 0);
    equal_result : out boolean;
    different_result : out boolean;
    local_result : out std_logic_vector(7 downto 0);
    last_value_result : out std_logic_vector(7 downto 0);
    resolved_value : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of vhdl_logic9 is
  signal attribute_source : std_logic_vector(7 downto 0);
begin
  source <= "ULH-WZ01";
  inverted <= not source;
  anded <= source and "11111111";
  ored <= source or "00000000";
  xored <= source xor "11111111";
  nanded <= source nand "11111111";
  nored <= source nor "00000000";
  xnored <= source xnor "11111111";
  extracted <= source(7 downto 4);
  concatenated <= source(7 downto 4) & source(3 downto 0);
  delayed <= transport "ZWLH-U01" after 2 ns;
  equal_result <= true when source = "ULH-WZ01" else false;
  different_result <= true when source /= "ULH-WZ00" else false;
  local_copy : process
    variable exact_local :
      std_logic_vector(7 downto 0) := "ULH-WZ01";
  begin
    local_result <= exact_local;
    wait;
  end process;
  attribute_source <= transport
    "ULH-WZ01" after 1 ns,
    "ZWLH-U01" after 2 ns;
  last_value_result <= attribute_source'last_value;
  resolved_value <= "ZL-HU01Z";
  resolved_value <= "HZZLZ10W";
end architecture;
)";
    assert(output.good());
  }

  const auto sv_parent_source =
      directory.path / "sv_parent.sv";
  {
    std::ofstream output{sv_parent_source};
    output << R"(
module sv_logic9_parent;
  logic [7:0] result;
  vhdl_logic9_child child(.result(result));
endmodule
)";
    assert(output.good());
  }
  const auto vhdl_child_source =
      directory.path / "vhdl_child.vhd";
  {
    std::ofstream output{vhdl_child_source};
    output << R"(
entity vhdl_logic9_child is
  port (result : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of vhdl_logic9_child is
begin
  result <= "ULH-WZ01";
end architecture;
)";
    assert(output.good());
  }

  const auto vhdl_parent_source =
      directory.path / "vhdl_parent.vhd";
  {
    std::ofstream output{vhdl_parent_source};
    output << R"(
entity vhdl_logic9_parent is
  port (result : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of vhdl_logic9_parent is
  signal source : std_logic_vector(7 downto 0);
begin
  source <= "ULH-WZ01";
  child : entity work.sv_logic9_child
    port map (data => source, result => result);
end architecture;
)";
    assert(output.good());
  }
  const auto sv_child_source =
      directory.path / "sv_child.sv";
  {
    std::ofstream output{sv_child_source};
    output << R"(
module sv_logic9_child(
  input logic [7:0] data,
  output logic [7:0] result
);
  assign result = data;
endmodule
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config =
        make_config(directory.path, source, optimization);
    const auto reference =
        run_once(
            config, fsim::app::SimulationEngine::interpreter);
    const auto compiled =
        run_once(
            config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(compiled);
    assert(reference.result.status == compiled.result.status);
    assert(reference.result.time == compiled.result.time);
    assert(reference.result.delta == compiled.result.delta);
    assert(reference.values == compiled.values);
    assert(reference.local_value == compiled.local_value);
    assert(
        reference.resolved_drivers
        == compiled.resolved_drivers);
    assert(reference.vcd == compiled.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes == 0);
    assert(compiled.compiled_modules == 0);
    assert(compiled.native_cache.hits == 0);
    assert(compiled.native_cache.misses == 0);
    assert(compiled.native_cache.stores == 0);
#endif

    const auto sv_parent_config = make_mixed_config(
        directory.path,
        sv_parent_source,
        vhdl_child_source,
        "sv:work.sv_logic9_parent",
        {
            "sv_logic9_parent.child",
            "vhdl:work.vhdl_logic9_child(rtl)",
            std::nullopt},
        "sv-parent-logic9",
        optimization);
    const auto sv_parent_reference = run_mixed(
        sv_parent_config,
        fsim::app::SimulationEngine::interpreter,
        "sv_logic9_parent.result");
    const auto sv_parent_compiled = run_mixed(
        sv_parent_config,
        fsim::app::SimulationEngine::compiled,
        "sv_logic9_parent.result");
    assert(
        sv_parent_reference.result.status
        == fsim::runtime::RunStatus::completed);
    assert(sv_parent_reference.value == "X01XXZ01");
    assert(
        sv_parent_reference.value
        == sv_parent_compiled.value);

    const auto vhdl_parent_config = make_mixed_config(
        directory.path,
        sv_child_source,
        vhdl_parent_source,
        "vhdl:work.vhdl_logic9_parent(rtl)",
        {
            "vhdl_logic9_parent.child",
            "sv:work.sv_logic9_child",
            std::nullopt},
        "vhdl-parent-logic9",
        optimization);
    const auto vhdl_parent_reference = run_mixed(
        vhdl_parent_config,
        fsim::app::SimulationEngine::interpreter,
        "vhdl_logic9_parent.result");
    const auto vhdl_parent_compiled = run_mixed(
        vhdl_parent_config,
        fsim::app::SimulationEngine::compiled,
        "vhdl_logic9_parent.result");
    assert(
        vhdl_parent_reference.result.status
        == fsim::runtime::RunStatus::completed);
    assert(vhdl_parent_reference.value == "X01XXZ01");
    assert(
        vhdl_parent_reference.value
        == vhdl_parent_compiled.value);
#if defined(FSIM_HAS_LLVM)
    assert(sv_parent_compiled.compiled_processes == 0);
    assert(sv_parent_compiled.compiled_modules == 0);
    assert(vhdl_parent_compiled.compiled_processes == 0);
    assert(vhdl_parent_compiled.compiled_modules == 0);
#endif
  }

  std::cout << "VHDL Logic9 application tests passed\n";
  return 0;
}
