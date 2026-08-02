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

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult result;
  std::array<std::string, 24> values;
  std::array<std::string, 7> composite_values;
  std::string top_local;
  std::string dynamic_local;
  std::string dynamic_slice_local;
  std::string top_aggregate;
  std::string child_local;
  std::string generic_aggregate;
  std::string debugger_output;
  std::string vcd;
  std::vector<std::string> specialization_keys;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_source,
    const std::filesystem::path& child_source,
    const std::filesystem::path& top_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-array";
  config.project.top = "vhdl:work.array_top(rtl)";
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
  sources.compilation_unit = "file";
  sources.files = {
      package_source, child_source, top_source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const std::filesystem::path& package_source,
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
  assert(project->design.specializations().size() == 2);
  for (const auto& specialization :
       project->design.specializations()) {
    assert(std::find(
               specialization.source_dependencies.begin(),
               specialization.source_dependencies.end(),
               package_source.string())
           != specialization.source_dependencies.end());
  }

  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> top_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> child_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> top_aggregate;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> dynamic_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> dynamic_slice_local;
  std::optional<std::pair<
      fsim::runtime::simir::ProcessId,
      std::size_t>> generic_aggregate;
  for (const auto& process : project->design.processes()) {
    for (std::size_t index = 0;
         index < process.debug_locals.size();
         ++index) {
      const auto& local = process.debug_locals[index];
      if (local.name == "top_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        top_local = std::pair{process.id, index};
      } else if (local.name == "dynamic_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        dynamic_local = std::pair{process.id, index};
      } else if (local.name == "dynamic_slice_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        dynamic_slice_local = std::pair{process.id, index};
      } else if (local.name == "top_aggregate") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        top_aggregate = std::pair{process.id, index};
      } else if (local.name == "child_local") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        child_local = std::pair{process.id, index};
      } else if (local.name == "generic_aggregate") {
        assert(
            local.width == 8
            && local.value_kind
                == fsim::runtime::simir::ValueKind::logic9);
        generic_aggregate = std::pair{process.id, index};
      }
    }
  }
  assert(
      top_local && dynamic_local && dynamic_slice_local && top_aggregate
      && child_local && generic_aggregate);

  Capture capture;
  capture.specialization_keys =
      project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes =
      simulation.compiled_process_count();
  capture.compiled_modules =
      simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 24> paths{
      "array_top.source",
      "array_top.result",
      "array_top.conditional_result",
      "array_top.slice_result",
      "array_top.indexed_result",
      "array_top.equal_result",
      "array_top.ascending_result",
      "array_top.boolean_result",
      "array_top.aggregate_positional",
      "array_top.aggregate_named",
      "array_top.aggregate_equal",
      "array_top.attribute_left",
      "array_top.attribute_right",
      "array_top.attribute_length",
      "array_top.attribute_ascending",
      "array_top.range_order",
      "array_top.reverse_order",
      "array_top.attribute_slice",
      "array_top.dynamic_read",
      "array_top.dynamic_result",
      "array_top.dynamic_slice_read",
      "array_top.dynamic_slice_local_result",
      "array_top.dynamic_slice_signal",
      "array_top.dynamic_slice_waveform"};
  constexpr std::array<std::size_t, 24> widths{
      8, 8, 8, 4, 1, 1, 8, 4, 8, 8, 1,
      32, 32, 32, 1, 32, 32, 4, 1, 8, 4, 8, 8, 8};
  std::array<fsim::runtime::simir::SignalId, 24> signals{};
  std::array<fsim::runtime::VcdSignal, 24> traces{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = simulation.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
    traces[index] = vcd.declare_signal(
        std::string{paths[index]}, widths[index]);
  }
  constexpr std::array<std::string_view, 7> composite_paths{
      "array_top.matrix_positional",
      "array_top.matrix_named",
      "array_top.matrix_ranged",
      "array_top.matrix_conditional",
      "array_top.matrix_local_result",
      "array_top.nested",
      "array_top.cells"};
  std::array<fsim::runtime::simir::SignalId, 7>
      composite_signals{};
  for (std::size_t index = 0;
       index < composite_paths.size(); ++index) {
    const auto signal =
        simulation.find_signal(composite_paths[index]);
    assert(signal);
    composite_signals[index] = *signal;
  }
  vcd.begin();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        traces[index], simulation.read_signal(signals[index]));
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
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.values[index] =
        simulation.read_signal(signals[index]).to_msb_string();
  }
  for (std::size_t index = 0;
       index < composite_signals.size(); ++index) {
    capture.composite_values[index] =
        simulation.read_signal(composite_signals[index])
            .to_msb_string();
  }
  capture.top_local = simulation.read_process_local(
      top_local->first, top_local->second).to_msb_string();
  capture.dynamic_local = simulation.read_process_local(
      dynamic_local->first,
      dynamic_local->second).to_msb_string();
  capture.dynamic_slice_local = simulation.read_process_local(
      dynamic_slice_local->first,
      dynamic_slice_local->second).to_msb_string();
  capture.top_aggregate = simulation.read_process_local(
      top_aggregate->first,
      top_aggregate->second).to_msb_string();
  capture.child_local = simulation.read_process_local(
      child_local->first, child_local->second).to_msb_string();
  capture.generic_aggregate = simulation.read_process_local(
      generic_aggregate->first,
      generic_aggregate->second).to_msb_string();
  {
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error};
    debugger.execute({"show", "source"});
    debugger.execute({"show", "result"});
    debugger.execute({"show", "aggregate_named"});
    debugger.execute({"show", "attribute_slice"});
    debugger.execute({"show", "slice_result"});
    debugger.execute({"show", "dynamic_slice_read"});
    debugger.execute({"show", "dynamic_slice_local_result"});
    debugger.execute({"show", "dynamic_slice_signal"});
    debugger.execute({"show", "dynamic_slice_waveform"});
    assert(debugger_error.str().empty());
    capture.debugger_output = debugger_output.str();
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_capture(const Capture& capture) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert((
      capture.values
      == std::array<std::string, 24>{
          "01LH10Z-",
          "11LH10Z-",
          "1111Z0ZH",
          "10Z-",
          "1",
          "0",
          "1111Z0Z0",
          "1010",
          "01LH10Z-",
          "1111Z0ZH",
          "1",
          "00000000000000000000000000000111",
          "00000000000000000000000000000000",
          "00000000000000000000000000001000",
          "0",
          "00000000011101001010000001001010",
          "00000000000000000011000000111001",
          "1111",
          "L",
          "00H00000",
          "1HH1",
          "0Z10X000",
          "010XZ000",
          "0Z01X000"}));
  assert((
      capture.composite_values
      == std::array<std::string, 7>{
          "101010",
          "101000",
          "111001",
          "101010",
          "011110",
          "10000101",
          "110001"}));
  assert(capture.top_local == "00LH10Z-");
  assert(capture.dynamic_local == "01HH10Z-");
  assert(capture.dynamic_slice_local == "0Z10X000");
  assert(capture.top_aggregate == "1111Z0ZH");
  assert(capture.child_local == "11LH10Z-");
  assert(capture.generic_aggregate == "10000000");
  assert(
      capture.debugger_output.find(
          "source = 01LH10Z-")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "result = 11LH10Z-")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "aggregate_named = 1111Z0ZH")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "attribute_slice = 1111")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "slice_result = 10Z-")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_read = 1HH1")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_local_result = 0Z10X000")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_signal = 010XZ000")
      != std::string::npos);
  assert(
      capture.debugger_output.find(
          "dynamic_slice_waveform = 0Z01X000")
      != std::string::npos);
  assert(capture.vcd.find("b010110zx") != std::string::npos);
  assert(capture.vcd.find("b110110zx") != std::string::npos);
  assert(capture.vcd.find("b010xz000") != std::string::npos);
  assert(capture.vcd.find("b0z01x000") != std::string::npos);
}

std::string run_expected_dynamic_failure(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine,
    const std::string_view expected_message) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  assert(project);
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  try {
    (void)simulation.run();
    assert(false && "dynamic packed-index failure was not reported");
  } catch (const fsim::runtime::simir::InterpreterError& error) {
    const auto message = std::string{error.what()};
    assert(message.find(expected_message) != std::string::npos);
    return message;
  }
  return {};
}

} // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-array-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_source =
      directory.path / "array_types.vhd";
  const auto child_source =
      directory.path / "array_child.vhd";
  const auto top_source =
      directory.path / "array_top.vhd";
  const auto failure_source =
      directory.path / "dynamic_failure.vhd";
  const auto slice_failure_source =
      directory.path / "dynamic_slice_failure.vhd";

  const auto write_package =
      [&](const std::string_view revision) {
        std::ofstream output{package_source};
        output << "-- " << revision << R"(
package Array_Types is
  constant Byte_Width : positive := 8;
  constant First_Index : natural := 0;
  type Logic_Array_T is array (natural range <>) of std_logic;
  subtype Byte_T is Logic_Array_T(Byte_Width - 1 downto 0);
  subtype Nibble_T is Logic_Array_T(3 downto 0);
  subtype Ascending_Byte_T is Logic_Array_T(0 to 7);
  type Boolean_Array_T is array (natural range <>) of boolean;
  subtype Boolean_Nibble_T is Boolean_Array_T(0 to 3);
end package;
)";
        assert(output.good());
      };
  write_package("revision one");
  {
    std::ofstream output{child_source};
    output << R"(
use work.array_types.all;
entity Array_Child is
  generic (Width : positive := Byte_Width);
  port (
    Source : in Logic_Array_T(Width - 1 downto 0);
    Result : out Logic_Array_T(Width - 1 downto 0)
  );
end entity;

use work.array_types.all;
architecture rtl of Array_Child is
begin
  transform : process(Source)
    variable Child_Local : Byte_T;
    variable Generic_Aggregate :
      Logic_Array_T(Width - 1 downto 0) :=
        (Width - 1 => '1', others => '0');
  begin
    Child_Local := Source;
    Child_Local(7) := '1';
    Child_Local(3 downto 0) := "10Z-";
    Result <= Child_Local;
  end process;
end architecture;
)";
    assert(output.good());
  }
  {
    std::ofstream output{top_source};
    output << R"(
use work.array_types.all;
entity Array_Top is
end entity;

use work.array_types.all;
architecture rtl of Array_Top is
  type Matrix_T is array (0 to 1, 3 downto 1) of bit;
  type Nibble_Array_T is array (3 downto 0) of bit;
  type Nibble_Memory_T is array (0 to 1) of Nibble_Array_T;
  type Cell_T is record
    Flag : boolean;
    Data : bit_vector(1 downto 0);
  end record;
  type Cells_T is array (2 to 3) of Cell_T;
  signal Source : Byte_T;
  signal Result : Byte_T;
  signal Conditional_Result : Byte_T;
  signal Slice_Result : Nibble_T;
  signal Indexed_Result : std_logic;
  signal Equal_Result : boolean;
  signal Ascending_Result : Ascending_Byte_T;
  signal Boolean_Result : Boolean_Nibble_T;
  signal Aggregate_Positional : Byte_T;
  signal Aggregate_Named : Byte_T;
  signal Aggregate_Equal : boolean;
  signal Attribute_Left : integer;
  signal Attribute_Right : integer;
  signal Attribute_Length : integer;
  signal Attribute_Ascending : boolean;
  signal Range_Order : integer;
  signal Reverse_Order : integer;
  signal Attribute_Slice : Nibble_T;
  signal Dynamic_Read : std_logic;
  signal Dynamic_Result : Byte_T;
  signal Dynamic_Slice_Read : Nibble_T;
  signal Dynamic_Slice_Local_Result : Byte_T;
  signal Dynamic_Slice_Signal : Byte_T;
  signal Dynamic_Slice_Waveform : Byte_T;
  signal Matrix_Positional : Matrix_T;
  signal Matrix_Named : Matrix_T;
  signal Matrix_Ranged : Matrix_T;
  signal Matrix_Conditional : Matrix_T;
  signal Matrix_Local_Result : Matrix_T;
  signal Nested : Nibble_Memory_T;
  signal Cells : Cells_T;
begin
  drive : process
    variable Top_Local : Byte_T := "01LH10Z-";
    variable Dynamic_Local : Byte_T := "01LH10Z-";
    variable Dynamic_Slice_Local : Byte_T := "00000000";
    variable Dynamic_Index : integer := 5;
    variable Dynamic_Left : integer := 6;
    variable Dynamic_Right : integer := 3;
    variable Top_Aggregate : Byte_T :=
      (Byte_T'left downto Byte_T'high - 3 => '1',
       3 | 1 => 'Z',
       First_Index => 'H', others => '0');
    variable Forward_Order : integer := 0;
    variable Backward_Order : integer := 0;
    variable Matrix_Local : Matrix_T :=
      ((3 => '0', others => '1'),
       (3 downto 2 => '1', others => '0'));
  begin
    Source <= Top_Local;
    Aggregate_Named <= Top_Aggregate;
    Top_Local(6) := '0';
    Dynamic_Read <= Dynamic_Local(Dynamic_Index);
    Dynamic_Local(Dynamic_Index) := 'H';
    Dynamic_Result <= (others => '0');
    Dynamic_Result(Dynamic_Index) <= 'H';
    Dynamic_Slice_Read <=
      Dynamic_Local(Dynamic_Left downto Dynamic_Right);
    Dynamic_Slice_Local(Dynamic_Left downto Dynamic_Right) := "Z10X";
    Dynamic_Slice_Local_Result <= Dynamic_Slice_Local;
    Dynamic_Slice_Signal <= (others => '0');
    Dynamic_Slice_Signal(Dynamic_Left downto Dynamic_Right) <=
      transport "10XZ" after 2 ns;
    Dynamic_Slice_Waveform <= (others => '0');
    Dynamic_Slice_Waveform(Dynamic_Left downto Dynamic_Right) <=
      transport "1010" after 1 ns, "Z01X" after 3 ns;
    Top_Aggregate :=
      (Byte_T'left downto Byte_T'high - 3 => '1',
       3 | 1 => 'Z',
       work.array_types.byte_t'right => 'H',
       others => '0');
    Top_Aggregate(Byte_T'right) := 'H';
    for Index in Byte_T'range loop
      if Index = 5 then
        next;
      end if;
      Forward_Order := Forward_Order * 10 + Index;
    end loop;
    for Index in
      work.array_types.byte_t'reverse_range(1) loop
      if Index = 6 then
        exit;
      end if;
      Backward_Order := Backward_Order * 10 + Index;
    end loop;
    Attribute_Left <= Top_Aggregate'left;
    Attribute_Right <=
      work.array_types.byte_t'right(1);
    Attribute_Length <= Byte_T'length;
    Attribute_Ascending <= Byte_T'ascending;
    Range_Order <= Forward_Order;
    Reverse_Order <= Backward_Order;
    Matrix_Local_Result <= Matrix_Local;
    wait;
  end process;

  child : entity work.Array_Child(rtl)
    generic map (Width => Byte_Width)
    port map (
      Source => Source,
      Result => Result
    );

  Conditional_Result <=
    (others => '0') when false else
    (7 downto 4 => '1', 3 | 1 => 'Z',
     work.array_types.first_index => 'H', others => '0');
  Slice_Result <= Result(3 downto 0);
  Indexed_Result <= Result(7);
  Equal_Result <= Result = Source;
  Ascending_Result <=
    (0 to 3 => '1', 4 | 6 => 'Z', others => '0');
  Boolean_Result <=
    (0 | 2 => true, others => false);
  Aggregate_Positional <=
    ('0', '1', 'L', 'H', '1', '0', 'Z', '-');
  Aggregate_Equal <=
    Aggregate_Named =
      (7 downto 4 => '1', 3 | 1 => 'Z',
       First_Index => 'H', others => '0');
  Attribute_Slice <=
    Aggregate_Named(
      Byte_T'high downto Byte_T'high - 3);
  Matrix_Positional <=
    (('1', '0', '1'), ('0', '1', '0'));
  Matrix_Named <=
    (0 => (3 => '1', 2 => '0', others => '1'),
     1 => (others => '0'));
  Matrix_Ranged <=
    (0 to 0 => (others => '1'),
     others => ('0', '0', '1'));
  Matrix_Conditional <=
    (others => (others => '0')) when false else
    (0 => ('1', '0', '1'), others => ('0', '1', '0'));
  Nested <=
    ((3 => '1', others => '0'),
     ('0', '1', '0', '1'));
  Cells <=
    (2 => (Flag => true, Data => "10"),
     others => (Data => "01", Flag => false));
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config = make_config(
        directory.path,
        package_source,
        child_source,
        top_source,
        optimization);
    const auto reference = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    verify_capture(cold);
    verify_capture(warm);
    assert(reference.result.status == cold.result.status);
    assert(reference.result.time == cold.result.time);
    assert(reference.result.delta == cold.result.delta);
    assert(reference.values == cold.values);
    assert(reference.composite_values == cold.composite_values);
    assert(reference.top_local == cold.top_local);
    assert(reference.dynamic_local == cold.dynamic_local);
    assert(
        reference.dynamic_slice_local
        == cold.dynamic_slice_local);
    assert(reference.top_aggregate == cold.top_aggregate);
    assert(reference.child_local == cold.child_local);
    assert(
        reference.generic_aggregate
        == cold.generic_aggregate);
    assert(reference.debugger_output == cold.debugger_output);
    assert(reference.vcd == cold.vcd);
    assert(cold.values == warm.values);
    assert(cold.composite_values == warm.composite_values);
    assert(cold.top_local == warm.top_local);
    assert(cold.dynamic_local == warm.dynamic_local);
    assert(
        cold.dynamic_slice_local
        == warm.dynamic_slice_local);
    assert(cold.top_aggregate == warm.top_aggregate);
    assert(cold.child_local == warm.child_local);
    assert(
        cold.generic_aggregate
        == warm.generic_aggregate);
    assert(cold.debugger_output == warm.debugger_output);
    assert(cold.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.compiled_modules == 2);
    assert(cold.cache.hits == 0);
    assert(cold.cache.misses == 2);
    assert(cold.cache.stores == 2);
    assert(warm.compiled_processes == cold.compiled_processes);
    assert(warm.compiled_modules == cold.compiled_modules);
    assert(warm.cache.hits == 2);
    assert(warm.cache.misses == 0);
    assert(warm.cache.stores == 0);
#endif

    write_package("revision two");
    const auto changed = run_once(
        config,
        package_source,
        fsim::app::SimulationEngine::compiled);
    verify_capture(changed);
    assert(
        changed.specialization_keys
        != warm.specialization_keys);
#if defined(FSIM_HAS_LLVM)
    assert(changed.compiled_processes == cold.compiled_processes);
    assert(changed.compiled_modules == 2);
    assert(changed.cache.hits == 0);
    assert(changed.cache.misses == 2);
    assert(changed.cache.stores == 2);
#endif
    write_package("revision one");
  }

  {
    std::ofstream output{failure_source};
    output << R"(
entity Dynamic_Failure is
end entity;

architecture rtl of Dynamic_Failure is
begin
  fail : process
    variable Value : std_logic_vector(7 downto 0) :=
      "00000000";
    variable Index : integer := 8;
  begin
    Value(Index) := '1';
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "vhdl-dynamic-index-failure";
    config.project.top = "vhdl:work.dynamic_failure(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path =
        directory.path
        / (optimization == fsim::project::Optimization::o0
               ? "failure-cache-o0"
               : "failure-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = {failure_source};
    config.source_sets.push_back(std::move(sources));
    const auto reference = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::interpreter,
        "dynamic packed index is outside the declared range");
    const auto compiled = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::compiled,
        "dynamic packed index is outside the declared range");
    assert(reference == compiled);
  }

  {
    std::ofstream output{slice_failure_source};
    output << R"(
entity Dynamic_Slice_Failure is
end entity;

architecture rtl of Dynamic_Slice_Failure is
begin
  fail : process
    variable Value : std_logic_vector(7 downto 0) := "00000000";
    variable Result : std_logic_vector(3 downto 0);
    variable Left_Bound : integer := 6;
    variable Right_Bound : integer := 4;
  begin
    Result := Value(Left_Bound downto Right_Bound);
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    fsim::project::Config config;
    config.base_directory = directory.path;
    config.project.name = "vhdl-dynamic-slice-failure";
    config.project.top = "vhdl:work.dynamic_slice_failure(rtl)";
    config.project.time_resolution = "1ns";
    config.build.optimization = optimization;
    config.build.cache_path =
        directory.path
        / (optimization == fsim::project::Optimization::o0
               ? "slice-failure-cache-o0"
               : "slice-failure-cache-o2");
    config.run.max_deltas = 1000;
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::vhdl;
    sources.standard = "2008";
    sources.library = "work";
    sources.compilation_unit = "file";
    sources.files = {slice_failure_source};
    config.source_sets.push_back(std::move(sources));
    const auto reference = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::interpreter,
        "VHDL integer subtype range check failed");
    const auto compiled = run_expected_dynamic_failure(
        config,
        fsim::app::SimulationEngine::compiled,
        "VHDL integer subtype range check failed");
    assert(reference == compiled);
  }

  std::cout << "VHDL array application tests passed\n";
  return 0;
}
