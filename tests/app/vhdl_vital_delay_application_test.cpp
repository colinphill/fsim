// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
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
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;
  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Change {
  std::string signal;
  std::string value;
  fsim::runtime::SimulationTick time{};
  friend bool operator==(const Change&, const Change&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<Change> changes;
  std::vector<std::string> values;
  std::string vcd;
  std::size_t reports{};
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

constexpr std::array<std::string_view, 12> kNames{
    "vital_delay.signal_delayed", "vital_delay.wire_delayed",
    "vital_delay.wire01_delayed", "vital_delay.wire01z_delayed",
    "vital_delay.path_delayed", "vital_delay.path01_delayed",
    "vital_delay.path01z_delayed", "vital_delay.on_detect_delayed",
    "vital_delay.on_event_delayed", "vital_delay.transport_delayed",
    "vital_delay.inertial_delayed", "vital_delay.ignored_default_delayed"};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vital-delay";
  config.project.top = "vhdl:work.vital_delay(rtl)";
  config.project.time_resolution = "1ns";
  config.build.jobs = 8;
  config.build.optimization = optimization;
  config.build.cache_path = directory /
      (optimization == fsim::project::Optimization::o0
           ? "cache-o0" : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(project);
  std::size_t delay_operations{};
  std::size_t memory_operations{};
  std::size_t embedded_memory_operations{};
  std::size_t glitch_locals{};
  for (const auto& process : project->design.processes()) {
    delay_operations += static_cast<std::size_t>(std::ranges::count_if(
        process.operations, [](const auto& operation) {
          return fsim::runtime::simir::operation_get_if<
              fsim::runtime::simir::VitalDelay>(&operation) != nullptr;
        }));
    memory_operations += static_cast<std::size_t>(std::ranges::count_if(
        process.operations, [](const auto& operation) {
          return fsim::runtime::simir::operation_get_if<
              fsim::runtime::simir::VitalMemoryDeclare>(&operation)
              != nullptr;
        }));
    for (const auto& operation : process.operations) {
      const auto* memory = fsim::runtime::simir::operation_get_if<
          fsim::runtime::simir::VitalMemoryDeclare>(&operation);
      if (memory != nullptr && memory->embedded_load) {
        ++embedded_memory_operations;
        assert(memory->embedded_load_text == "@3 a5\n");
      }
    }
    glitch_locals += static_cast<std::size_t>(std::ranges::count_if(
        process.debug_locals, [](const auto& local) {
          return local.name == "glitch";
        }));
  }
  assert(delay_operations == kNames.size() + 1U);
  assert(memory_operations == 3U);
  assert(embedded_memory_operations == 1U);
  assert(glitch_locals == 8U);

  Capture capture;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();
  std::array<fsim::runtime::simir::SignalId, kNames.size()> signals{};
  std::array<fsim::runtime::VcdSignal, kNames.size()> traces{};
  std::ostringstream output;
  fsim::runtime::VcdWriter vcd{output, "1ns", 64};
  for (std::size_t index = 0; index < kNames.size(); ++index) {
    const auto signal = simulation.find_signal(kNames[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(std::string{kNames[index]}, 1U);
  }
  vcd.begin(simulation.now());
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(traces[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          std::uint64_t) {
        const auto found = std::ranges::find(signals, signal);
        if (found == signals.end()) return;
        const auto index = static_cast<std::size_t>(
            std::distance(signals.begin(), found));
        capture.changes.push_back(
            {std::string{kNames[index]}, value.to_msb_string(), time});
        vcd.set_time(time);
        vcd.change(traces[index], value);
      });
  simulation.set_report_hook(
      [&](fsim::runtime::simir::ProcessId,
          std::string_view message,
          fsim::runtime::simir::AssertionSeverity,
          const fsim::runtime::simir::SourceLocation&,
          fsim::runtime::SimulationTick,
          std::uint64_t) {
        if (message.starts_with("VitalPathDelay(pulse_")) ++capture.reports;
      });
  capture.result = simulation.run();
  for (const auto signal : signals) {
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  vcd.flush();
  capture.vcd = output.str();
  return capture;
}

std::vector<std::pair<std::string, fsim::runtime::SimulationTick>> changes_for(
    const Capture& capture, const std::string_view signal) {
  std::vector<std::pair<std::string, fsim::runtime::SimulationTick>> result;
  for (const auto& change : capture.changes) {
    if (change.signal == signal) result.emplace_back(change.value, change.time);
  }
  return result;
}

void verify(const Capture& capture) {
  using Timed = std::pair<std::string, fsim::runtime::SimulationTick>;
  assert(capture.result.status == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 60U);
  assert((changes_for(capture, kNames[0]) == std::vector<Timed>{
      {"0", 5}, {"1", 25}, {"Z", 35}, {"0", 45}}));
  assert((changes_for(capture, kNames[1]) == std::vector<Timed>{
      {"0", 3}, {"1", 23}, {"Z", 33}, {"0", 43}}));
  assert((changes_for(capture, kNames[2]) == std::vector<Timed>{
      {"0", 5}, {"1", 22}, {"Z", 35}, {"0", 45}}));
  assert((changes_for(capture, kNames[3]) == std::vector<Timed>{
      {"0", 7}, {"1", 22}, {"Z", 36}, {"0", 47}}));
  assert((changes_for(capture, kNames[4]) == std::vector<Timed>{
      {"0", 8}, {"1", 24}, {"Z", 34}, {"0", 44}}));
  assert((changes_for(capture, kNames[5]) == std::vector<Timed>{
      {"0", 8}, {"1", 22}, {"Z", 35}, {"0", 45}}));
  assert((changes_for(capture, kNames[6]) == std::vector<Timed>{
      {"0", 8}, {"H", 22}, {"Z", 36}, {"0", 47}}));
  assert((changes_for(capture, kNames[7]) == std::vector<Timed>{
      {"X", 22}, {"0", 32}}));
  assert((changes_for(capture, kNames[8]) == std::vector<Timed>{
      {"X", 30}, {"0", 32}}));
  assert((changes_for(capture, kNames[9]) == std::vector<Timed>{
      {"1", 30}, {"0", 32}}));
  assert((changes_for(capture, kNames[10]) == std::vector<Timed>{
      {"0", 32}}));
  assert(changes_for(capture, kNames[11]).empty());
  assert(capture.reports == 4U);
  auto expected_values = std::vector<std::string>(kNames.size(), "0");
  expected_values.back() = "U";
  assert(capture.values == expected_values);
  assert(capture.vcd.find("$timescale 1ns $end") != std::string::npos);
  assert(capture.vcd.find("#47") != std::string::npos);
}

void expect_failure(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::string_view declarations,
    const std::string_view statement,
    const std::string_view code) {
  std::ofstream output{source, std::ios::binary};
  output << "library ieee;\n"
            "use ieee.std_logic_1164.all;\n"
            "use ieee.vital_timing.all;\n"
            "entity vital_delay is end entity;\n"
            "architecture rtl of vital_delay is\n"
            "  signal in_value : std_logic;\n"
            "  signal out_value : std_logic;\n  "
         << declarations
         << "\nbegin\n"
            "  checker : process(in_value)\n"
            "    variable glitch : VitalGlitchDataType;\n"
            "  begin\n    "
         << statement
         << "\n  end process;\nend architecture;\n";
  assert(output.good());
  output.close();
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(
      make_config(
          directory, source, fsim::project::Optimization::o0),
      diagnostics);
  assert(!project);
  const auto found = std::ranges::any_of(
      diagnostics.diagnostics(), [&](const auto& diagnostic) {
        return diagnostic.code == code;
      });
  if (!found) {
    std::cerr << "expected " << code << " for " << statement << '\n';
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(found);
}

void expect_memory_load_failure(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const std::filesystem::path& missing) {
  std::ofstream output{source, std::ios::binary};
  output << "library ieee;\n"
            "use ieee.vital_memory.all;\n"
            "entity vital_delay is end entity;\n"
            "architecture rtl of vital_delay is begin\n"
            "  model : process\n"
            "    variable memory : VitalMemoryDataType := "
            "VitalDeclareMemory(NoOfWords => 4, NoOfBitsPerWord => 8, "
            "MemoryLoadFile => \""
         << missing.generic_string()
         << "\");\n"
            "  begin wait; end process;\n"
            "end architecture;\n";
  assert(output.good());
  output.close();
  fsim::diagnostic::Engine diagnostics;
  const auto project = fsim::app::build_project(
      make_config(
          directory, source, fsim::project::Optimization::o0),
      diagnostics);
  assert(!project);
  assert(std::ranges::any_of(
      diagnostics.diagnostics(), [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-VITALMEM-004";
      }));
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vital-delay-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "vital_delay.vhd";
  const auto memory_load = directory.path / "vendor-memory.hex";
  {
    std::ofstream load{memory_load, std::ios::binary};
    load << "@3 a5\n";
    assert(load.good());
  }
  {
    std::ofstream output{source, std::ios::binary};
    output << R"(
library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_memory.all;

entity vendor_cell is
  generic (TimingChecksOn : boolean := true);
  port (a : in std_logic; y : out std_logic);
  attribute VITAL_LEVEL0 : boolean;
  attribute VITAL_LEVEL0 of vendor_cell : entity is true;
end entity;

architecture vital of vendor_cell is
  attribute VITAL_LEVEL1 : boolean;
  attribute VITAL_LEVEL1 of vital : architecture is true;
  signal \vendor$input\ : std_logic;
begin
  \vendor$input\ <= a;
  -- pragma translate_off
  assert true report "vendor timing model compatibility" severity note;
  -- pragma translate_on
  timing_enabled : if TimingChecksOn generate
    delay_process : process(\vendor$input\)
    begin
      VitalSignalDelay(y, \vendor$input\, 1 ns);
    end process;
  end generate;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_memory.all;

entity vendor_memory is
  generic (
    Width : positive := 8;
    TimingChecksOn : boolean := true);
  attribute VITAL_LEVEL0 : boolean;
  attribute VITAL_LEVEL0 of vendor_memory : entity is true;
end entity;

architecture vital of vendor_memory is
  attribute VITAL_LEVEL1 : boolean;
  attribute VITAL_LEVEL1 of vital : architecture is true;
begin
  model : process
    variable memory : VitalMemoryDataType :=
        VitalDeclareMemory(
            NoOfWords => 16,
            NoOfBitsPerWord => Width,
            NoOfBitsPerSubWord => Width,
            MemoryLoadFile => )"
           << '"' << memory_load.generic_string() << '"'
           << R"(,
            BinaryLoadFile => false);
  begin
    if TimingChecksOn then
      assert Width > 0
        report "vendor memory width is invalid" severity failure;
    end if;
    wait;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.vital_timing.all;
use ieee.vital_memory.all;

entity vital_delay is
end entity;

architecture rtl of vital_delay is
  component vendor_cell is
    generic (TimingChecksOn : boolean := true);
    port (a : in std_logic; y : out std_logic);
  end component;
  component vendor_memory is
    generic (
      Width : positive := 8;
      TimingChecksOn : boolean := true);
  end component;
  for all : vendor_cell use entity work.vendor_cell(vital);
  for all : vendor_memory use entity work.vendor_memory(vital);
  signal input_value : std_logic;
  signal pulse_input : std_logic;
  signal pulse_enable : boolean;
  signal signal_delayed : std_logic;
  signal wire_delayed : std_logic;
  signal wire01_delayed : std_logic;
  signal wire01z_delayed : std_logic;
  signal path_delayed : std_logic;
  signal path01_delayed : std_logic;
  signal path01z_delayed : std_logic;
  signal on_detect_delayed : std_logic;
  signal on_event_delayed : std_logic;
  signal transport_delayed : std_logic;
  signal inertial_delayed : std_logic;
  signal ignored_default_delayed : std_logic;
  signal vendor_delayed : std_logic;
begin
  configured_cell : vendor_cell
    generic map (TimingChecksOn => true)
    port map (a => input_value, y => vendor_delayed);
  configured_memory : vendor_memory
    generic map (Width => 8, TimingChecksOn => true);

  stimulus : process
    variable memory_default : VitalMemoryDataType :=
        VitalDeclareMemory(4, 65);
    variable memory_subword : VitalMemoryDataType := VitalDeclareMemory(
        NoOfWords => 2,
        NoOfBitsPerWord => 8,
        NoOfBitsPerSubWord => 4,
        MemoryLoadFile => "",
        BinaryLoadFile => true);
  begin
    input_value <= '0';
    pulse_input <= '0';
    pulse_enable <= false;
    wait for 10 ns;
    pulse_enable <= true;
    wait for 10 ns;
    input_value <= '1';
    pulse_input <= '1';
    wait for 2 ns;
    pulse_input <= '0';
    wait for 8 ns;
    input_value <= 'Z';
    wait for 10 ns;
    input_value <= '0';
    wait for 20 ns;
    wait;
  end process;

  signal_delay_process : process(input_value)
  begin
    VitalSignalDelay(signal_delayed, input_value, 5 ns);
  end process;

  wire_delay_process : process(input_value)
  begin
    VitalWireDelay(wire_delayed, input_value, 3 ns);
  end process;

  wire01_delay_process : process(input_value)
  begin
    VitalWireDelay(wire01_delayed, input_value, (2 ns, 5 ns));
  end process;

  wire01z_delay_process : process(input_value)
  begin
    VitalWireDelay(
        wire01z_delayed, input_value,
        (2 ns, 5 ns, 3 ns, 4 ns, 6 ns, 7 ns));
  end process;

  path_delay_process : process(input_value)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => path_delayed,
        GlitchData => glitch,
        OutSignalName => "path_delayed",
        OutTemp => input_value,
        Paths => (0 => (input_value'last_event, 4 ns, true)),
        DefaultDelay => 8 ns,
        Mode => VitalTransport,
        XOn => false);
  end process;

  path01_delay_process : process(input_value)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay01(
        OutSignal => path01_delayed,
        GlitchData => glitch,
        OutSignalName => "path01_delayed",
        OutTemp => input_value,
        Paths => (0 => (input_value'last_event, (2 ns, 5 ns), true)),
        DefaultDelay => (8 ns, 8 ns),
        Mode => VitalTransport,
        XOn => false);
  end process;

  path01z_delay_process : process(input_value)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay01Z(
        OutSignal => path01z_delayed,
        GlitchData => glitch,
        OutSignalName => "path01z_delayed",
        OutTemp => input_value,
        Paths => (0 => (input_value'last_event,
            (2 ns, 5 ns, 3 ns, 4 ns, 6 ns, 7 ns), true)),
        DefaultDelay => (8 ns, 8 ns, 8 ns, 8 ns, 8 ns, 8 ns),
        Mode => VitalTransport,
        XOn => false,
        OutputMap => "UX0HZWLH-");
  end process;

  on_detect_process : process(pulse_input)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => on_detect_delayed,
        GlitchData => glitch,
        OutSignalName => "pulse_on_detect",
        OutTemp => pulse_input,
        Paths => (0 => (pulse_input'last_event, 10 ns, pulse_enable)),
        DefaultDelay => 0 ns,
        Mode => OnDetect,
        IgnoreDefaultDelay => true);
  end process;

  on_event_process : process(pulse_input)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => on_event_delayed,
        GlitchData => glitch,
        OutSignalName => "pulse_on_event",
        OutTemp => pulse_input,
        Paths => (0 => (pulse_input'last_event, 10 ns, pulse_enable)),
        DefaultDelay => 0 ns,
        Mode => OnEvent,
        IgnoreDefaultDelay => true);
  end process;

  transport_process : process(pulse_input)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => transport_delayed,
        GlitchData => glitch,
        OutSignalName => "pulse_transport",
        OutTemp => pulse_input,
        Paths => (0 => (pulse_input'last_event, 10 ns, pulse_enable)),
        DefaultDelay => 0 ns,
        Mode => VitalTransport,
        XOn => false,
        IgnoreDefaultDelay => true);
  end process;

  inertial_process : process(pulse_input)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => inertial_delayed,
        GlitchData => glitch,
        OutSignalName => "pulse_inertial",
        OutTemp => pulse_input,
        Paths => (0 => (pulse_input'last_event, 10 ns, pulse_enable)),
        DefaultDelay => 0 ns,
        Mode => VitalInertial,
        XOn => false,
        IgnoreDefaultDelay => true);
  end process;

  ignored_default_process : process(input_value)
    variable glitch : VitalGlitchDataType;
  begin
    VitalPathDelay(
        OutSignal => ignored_default_delayed,
        GlitchData => glitch,
        OutSignalName => "ignored_default",
        OutTemp => input_value,
        Paths => (1 to 0 => (input_value'last_event, 4 ns, true)),
        DefaultDelay => 8 ns,
        Mode => VitalTransport,
        XOn => false,
        IgnoreDefaultDelay => true);
  end process;
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(directory.path, source, optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto compiled = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto debug = optimization == fsim::project::Optimization::o0
        ? std::optional<Capture>{run_once(
              config, fsim::app::SimulationEngine::debug)}
        : std::nullopt;
    verify(reference);
    assert(reference.changes == compiled.changes);
    assert(reference.changes == warm.changes);
    assert(reference.values == compiled.values);
    assert(reference.values == warm.values);
    assert(reference.reports == compiled.reports);
    assert(reference.reports == warm.reports);
    assert(reference.vcd == compiled.vcd);
    assert(reference.vcd == warm.vcd);
    if (debug) {
      assert(reference.changes == debug->changes);
      assert(reference.values == debug->values);
      assert(reference.reports == debug->reports);
      assert(reference.vcd == debug->vcd);
    }
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes > 0U);
    assert(compiled.cache.misses > 0U);
    assert(warm.cache.hits > 0U);
#endif
  }

  const auto artifact_config = make_config(
      directory.path, source, fsim::project::Optimization::o2);
  {
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(artifact_config, diagnostics);
    assert(project && !diagnostics.has_error());
    const auto encoded = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(encoded && !diagnostics.has_error());
    auto restored = fsim::app::deserialize_runtime_state(
        *encoded, "vital-delay-runtime", diagnostics);
    assert(restored && !diagnostics.has_error());
    assert(fsim::app::serialize_runtime_state(*restored, diagnostics)
           == encoded);
    std::size_t restored_delays{};
    std::size_t restored_memories{};
    std::size_t restored_embedded_memories{};
    for (const auto& process : restored->processes()) {
      restored_delays += static_cast<std::size_t>(std::ranges::count_if(
          process.operations, [](const auto& operation) {
            return fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::VitalDelay>(&operation) != nullptr;
          }));
      restored_memories += static_cast<std::size_t>(std::ranges::count_if(
          process.operations, [](const auto& operation) {
            return fsim::runtime::simir::operation_get_if<
                fsim::runtime::simir::VitalMemoryDeclare>(&operation)
                != nullptr;
          }));
      for (const auto& operation : process.operations) {
        const auto* memory = fsim::runtime::simir::operation_get_if<
            fsim::runtime::simir::VitalMemoryDeclare>(&operation);
        if (memory != nullptr && memory->embedded_load) {
          ++restored_embedded_memories;
          assert(memory->embedded_load_text == "@3 a5\n");
        }
      }
    }
    assert(restored_delays == kNames.size() + 1U);
    assert(restored_memories == 3U);
    assert(restored_embedded_memories == 1U);
    project->design = std::move(*restored);
    fsim::app::Simulation simulation{
        std::move(*project), artifact_config.run.max_deltas,
        fsim::app::SimulationEngine::compiled};
    std::vector<fsim::runtime::simir::SignalId> signals;
    for (const auto name : kNames) {
      const auto signal = simulation.find_signal(name);
      assert(signal);
      signals.push_back(*signal);
    }
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    for (const auto signal : signals) {
      const auto expected = signal == signals.back() ? "U" : "0";
      assert(simulation.read_signal(signal).to_msb_string() == expected);
    }
  }
  {
    fsim::diagnostic::Engine diagnostics;
    const auto object = directory.path / "vital-delay.fsimobj";
    const auto artifact = directory.path / "vital-delay.fsimdesign";
    assert(fsim::app::compile_artifact(
        artifact_config, object, diagnostics));
    auto elaborate_config = artifact_config;
    elaborate_config.source_sets.clear();
    const std::array objects{object};
    assert(fsim::app::elaborate_artifact(
        elaborate_config, objects, artifact, diagnostics));
    const auto relocated_directory = directory.path / "relocated";
    std::filesystem::create_directories(relocated_directory);
    const auto relocated = relocated_directory / artifact.filename();
    std::filesystem::create_directories(relocated / "state");
    std::filesystem::copy_file(
        artifact / "fsim-design.bin", relocated / "fsim-design.bin");
    for (const std::string_view payload : {
             "runtime.bin", "semantics.bin", "design-ir.bin",
             "classes.bin", "sv-constraint-hir.bin", "vhdl-hir.bin",
             "sv-coverage.bin", "sv-uvm.bin"}) {
      std::filesystem::copy_file(
          artifact / "state" / payload,
          relocated / "state" / payload);
    }
    constexpr auto read_only_directory =
        std::filesystem::perms::owner_read
        | std::filesystem::perms::owner_exec
        | std::filesystem::perms::group_read
        | std::filesystem::perms::group_exec
        | std::filesystem::perms::others_read
        | std::filesystem::perms::others_exec;
    std::filesystem::permissions(
        relocated / "state", read_only_directory,
        std::filesystem::perm_options::replace);
    std::filesystem::permissions(
        relocated, read_only_directory,
        std::filesystem::perm_options::replace);
    std::filesystem::remove(memory_load);
    auto loaded = fsim::app::load_design_artifact(relocated, diagnostics);
    if (!loaded) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(loaded && !diagnostics.has_error());
    fsim::app::Simulation simulation{
        std::move(*loaded), artifact_config.run.max_deltas,
        fsim::app::SimulationEngine::compiled};
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::completed);
    for (std::size_t index = 0; index < kNames.size(); ++index) {
      const auto name = kNames[index];
      const auto signal = simulation.find_signal(name);
      assert(signal);
      assert(
          simulation.read_signal(*signal).to_msb_string()
          == (index + 1U == kNames.size() ? "U" : "0"));
    }
    {
      std::ofstream load{memory_load, std::ios::binary};
      load << "@3 a5\n";
      assert(load.good());
    }
  }

  expect_failure(
      directory.path, source, "",
      "VitalSignalDelay(OutSig => out_value, OutSig => out_value, "
      "InSig => in_value, Dly => 1 ns);",
      "FSIM-ELAB-VITAL-017");
  expect_failure(
      directory.path, source, "",
      "VitalPathDelay(OutSignal => out_value, GlitchData => glitch, "
      "OutTemp => in_value, Paths => (0 => (in_value'last_event, "
      "1 ns, true)), DefaultDelay => 1 ns, Mode => in_value);",
      "FSIM-ELAB-VITAL-018");
  expect_failure(
      directory.path, source, "signal integer_output : integer;",
      "VitalSignalDelay(integer_output, in_value, 1 ns);",
      "FSIM-ELAB-VITAL-019");
  expect_failure(
      directory.path, source, "",
      "VitalSignalDelay(out_value, in_value, -1 ns);",
      "FSIM-ELAB-VITAL-020");
  expect_failure(
      directory.path, source, "",
      "VitalWireDelay(out_value, in_value, (1 ns, 2 ns, 3 ns));",
      "FSIM-ELAB-VITAL-020");
  expect_failure(
      directory.path, source, "",
      "VitalPathDelay(OutSignal => out_value, GlitchData => glitch, "
      "OutTemp => in_value, Paths => in_value, DefaultDelay => 1 ns);",
      "FSIM-ELAB-VITAL-021");
  expect_memory_load_failure(
      directory.path, source, directory.path / "missing-memory.hex");
  return 0;
}
