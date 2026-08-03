// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "path_test_support.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
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
  std::vector<std::string> values;
  std::vector<std::string> keys;
  std::vector<std::string> resolved_drivers;
  std::vector<std::string> logic9_resolved_drivers;
  std::vector<std::string> composite_resolved_drivers;
  std::vector<std::string> package_resolved_drivers;
  std::string vcd;
  fsim::app::NativeCacheStatistics cache;
};

std::string bits(const std::uint32_t value) {
  return std::bitset<32>{value}.to_string();
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& package_declaration,
    const std::filesystem::path& package_body,
    const std::filesystem::path& top,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-overloads";
  config.project.top = "vhdl:work.overload_app(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 100;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {package_declaration, package_body, top};
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
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);
  assert(project->design.specializations().size() == 1);
  const auto resolved =
      project->design.find_signal("overload_app.resolved_value");
  const auto resolved_logic =
      project->design.find_signal("overload_app.resolved_logic_value");
  const auto resolved_pair =
      project->design.find_signal("overload_app.resolved_pair_value");
  const auto package_resolved =
      project->design.find_signal("overload_app.package_resolved_value");
  assert(
      resolved && resolved_logic && resolved_pair
      && package_resolved);
  assert(
      project->design.signals().at(*resolved).resolution
      == fsim::runtime::simir::ResolutionKind::vhdl_user_or);
  assert(
      project->design.signals().at(*resolved_logic).resolution
      == fsim::runtime::simir::ResolutionKind::vhdl_user_or);
  assert(
      project->design.signals().at(*resolved_logic).source_domain
      == fsim::frontend::ValueDomain::Logic9);
  assert(
      project->design.signals().at(*resolved_pair).resolution
      == fsim::runtime::simir::ResolutionKind::vhdl_user_or);
  assert(
      project->design.signals().at(*package_resolved).resolution
      == fsim::runtime::simir::ResolutionKind::vhdl_user_or);
  const auto driver_processes = [&](const auto signal) {
    std::vector<fsim::runtime::simir::ProcessId> result;
    for (const auto& process : project->design.processes()) {
      if (std::ranges::any_of(
              process.operations,
              [&](const auto& operation) {
                const auto writes = [&](const auto* value) {
                  return value != nullptr && value->signal == signal;
                };
                return writes(fsim::runtime::simir::operation_get_if<
                              fsim::runtime::simir::WriteAfter>(
                              &operation))
                    || writes(fsim::runtime::simir::operation_get_if<
                              fsim::runtime::simir::WriteInertial>(
                              &operation))
                    || writes(fsim::runtime::simir::operation_get_if<
                              fsim::runtime::simir::WriteProjected>(
                              &operation))
                    || writes(fsim::runtime::simir::operation_get_if<
                              fsim::runtime::simir::WriteProjectedWaveform>(
                              &operation));
              })) {
        result.push_back(process.id);
      }
    }
    return result;
  };
  const auto resolved_driver_processes = driver_processes(*resolved);
  const auto logic9_driver_processes = driver_processes(*resolved_logic);
  const auto composite_driver_processes = driver_processes(*resolved_pair);
  const auto package_driver_processes =
      driver_processes(*package_resolved);
  assert(resolved_driver_processes.size() == 2);
  assert(logic9_driver_processes.size() == 2);
  assert(composite_driver_processes.size() == 2);
  assert(package_driver_processes.size() == 2);
  assert(fsim::test::has_source_dependency(
      project->design.specializations().front().source_dependencies,
      config.source_sets.front().files[1]));

  Capture capture;
  capture.keys = project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.cache = simulation.native_cache_statistics();

  const auto integer_input =
      simulation.find_signal("overload_app.integer_input");
  const auto boolean_input =
      simulation.find_signal("overload_app.boolean_input");
  assert(integer_input && boolean_input);
  simulation.deposit_signal(
      *integer_input,
      fsim::runtime::PackedLogic4::from_msb_string(bits(5)));
  simulation.deposit_signal(
      *boolean_input,
      fsim::runtime::PackedLogic4::from_msb_string("1"));

  constexpr std::array<std::string_view, 39> outputs{
      "package_integer",
      "selected_integer",
      "package_boolean",
      "selected_boolean",
      "local_integer",
      "local_boolean",
      "package_proc_integer",
      "selected_proc_integer",
      "package_proc_boolean",
      "selected_proc_boolean",
      "local_proc_integer",
      "local_proc_boolean",
      "contextual_integer",
      "contextual_boolean",
      "named_integer",
      "named_boolean",
      "defaulted_integer",
      "nested_integer",
      "nested_boolean",
      "named_proc_integer",
      "named_proc_boolean",
      "converted_integer",
      "converted_enum",
      "resolved_value",
      "resolved_and_value",
      "static_overload_value",
      "operator_value",
      "attribute_value",
      "resolved_logic_value",
      "resolved_pair_value",
      "package_resolved_value",
      "static_converted_value",
      "static_attribute_value",
      "static_operator_value",
      "static_nested_value",
      "static_length_value",
      "aggregate_value",
      "indexed_value",
      "slice_value"};
  std::array<fsim::runtime::simir::SignalId, outputs.size()> ids{};
  for (std::size_t index = 0; index < outputs.size(); ++index) {
    const auto signal = simulation.find_signal(
        "overload_app." + std::string{outputs[index]});
    assert(signal);
    ids[index] = *signal;
  }

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd{vcd_output, "1ns", 64};
  const auto or_trace = vcd.declare_signal(
      "overload_app.resolved_value", 1);
  const auto and_trace = vcd.declare_signal(
      "overload_app.resolved_and_value", 1);
  const auto logic_trace = vcd.declare_signal(
      "overload_app.resolved_logic_value", 1);
  const auto pair_trace = vcd.declare_signal(
      "overload_app.resolved_pair_value", 2);
  const auto package_trace = vcd.declare_signal(
      "overload_app.package_resolved_value", 1);
  vcd.begin();
  vcd.change(or_trace, simulation.read_signal(ids[23]));
  vcd.change(and_trace, simulation.read_signal(ids[24]));
  vcd.change(logic_trace, simulation.read_signal(ids[28]));
  vcd.change(pair_trace, simulation.read_signal(ids[29]));
  vcd.change(package_trace, simulation.read_signal(ids[30]));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t) {
        if (signal != ids[23] && signal != ids[24]
            && signal != ids[28] && signal != ids[29]
            && signal != ids[30]) {
          return;
        }
        vcd.set_time(time);
        const auto trace = signal == ids[23]
            ? or_trace
            : signal == ids[24]
                ? and_trace
                : signal == ids[28]
                    ? logic_trace
                    : signal == ids[29]
                        ? pair_trace
                        : package_trace;
        vcd.change(trace, value);
      });

  capture.result = simulation.run();
  for (const auto signal : ids) {
    capture.values.push_back(
        simulation.read_signal(signal).to_msb_string());
  }
  for (const auto process : resolved_driver_processes) {
    capture.resolved_drivers.push_back(
        simulation.read_driver(process, *resolved).to_msb_string());
  }
  for (const auto process : logic9_driver_processes) {
    capture.logic9_resolved_drivers.push_back(
        simulation.read_driver(
            process, *resolved_logic).to_msb_string());
  }
  for (const auto process : composite_driver_processes) {
    capture.composite_resolved_drivers.push_back(
        simulation.read_driver(
            process, *resolved_pair).to_msb_string());
  }
  for (const auto process : package_driver_processes) {
    capture.package_resolved_drivers.push_back(
        simulation.read_driver(
            process, *package_resolved).to_msb_string());
  }
  std::ranges::sort(capture.resolved_drivers);
  std::ranges::sort(capture.logic9_resolved_drivers);
  std::ranges::sort(capture.composite_resolved_drivers);
  std::ranges::sort(capture.package_resolved_drivers);
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify(
    const Capture& capture,
    const std::uint32_t bias,
    const bool boolean_passthrough) {
  assert(
      capture.result.status
      == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 2);
  const auto boolean_value =
      boolean_passthrough ? std::string{"1"} : std::string{"0"};
  const std::vector<std::string> expected{
      bits(5 + bias),
      bits(5 + bias),
      boolean_value,
      boolean_value,
      bits(35),
      "0",
      bits(15 + bias),
      bits(15 + bias),
      boolean_value,
      boolean_value,
      bits(45),
      "0",
      bits(55),
      "1",
      bits(65),
      "1",
      bits(65),
      bits(155),
      "1",
      bits(95),
      "1",
      bits(65),
      bits(101),
      "1",
      "0",
      bits(5),
      bits(77),
      bits(101),
      "1",
      "11",
      "1",
      bits(4),
      bits(9),
      bits(10),
      bits(4),
      bits(8),
      bits(88),
      bits(89),
      bits(88)};
  if (capture.values != expected) {
    for (std::size_t index = 0; index < capture.values.size(); ++index) {
      std::cerr << index << ": actual=" << capture.values[index]
                << " expected=" << expected[index] << '\n';
    }
  }
  assert(capture.values == expected);
  assert((capture.resolved_drivers
          == std::vector<std::string>{"0", "1"}));
  assert((capture.logic9_resolved_drivers
          == std::vector<std::string>{"H", "L"}));
  assert((capture.composite_resolved_drivers
          == std::vector<std::string>{"01", "10"}));
  assert((capture.package_resolved_drivers
          == std::vector<std::string>{"0", "1"}));
  assert(capture.vcd.find("#1") != std::string::npos);
  assert(capture.vcd.find("#2") != std::string::npos);
}

}  // namespace

int main() {
  const auto serial =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-overloads-" + std::to_string(serial))};
  std::filesystem::create_directories(directory.path);
  const auto package_declaration = directory.path / "overload_pkg.vhd";
  const auto package_body = directory.path / "overload_pkg_body.vhd";
  const auto top = directory.path / "overload_app.vhd";

  {
    std::ofstream output(package_declaration, std::ios::binary);
    output << R"(
package overload_pkg is
  type package_bit_pair is array (0 to 1) of bit;
  function choose(value : integer) return integer;
  function choose(value : boolean) return boolean;
  procedure assign_value(
    source : in integer;
    variable target : out integer);
  procedure assign_value(
    source : in boolean;
    variable target : out boolean);
  function static_select(value : boolean) return integer;
  function static_select(value : integer) return integer;
  subtype static_range_t is integer range 2 to 9;
  constant static_base : integer := 3;
  constant static_converted : static_range_t :=
    static_range_t(static_base + 1);
  constant static_attribute : integer := static_range_t'high;
  constant static_operator : integer :=
    static_converted * 2 + static_range_t'low;
  constant static_nested : integer :=
    static_select(static_select(2));
  type static_bits is array
    (static_range_t'low to static_attribute) of bit;
  constant static_length : integer := static_bits'length;
  function aggregate_select(value : static_bits) return integer;
  function bit_select(value : bit) return integer;
  function package_resolve(values : package_bit_pair) return bit;
  subtype package_resolved_bit is package_resolve bit;
  constant static_overload : integer := static_select(4);
end package;
)";
    assert(output.good());
  }

  const auto write_package_body =
      [&](const std::uint32_t bias,
          const bool boolean_passthrough) {
        std::ofstream output(
            package_body,
            std::ios::binary | std::ios::trunc);
        output << R"(
package body overload_pkg is
  function choose(value : integer) return integer is
  begin
    return value + )" << bias << R"(;
  end function;
  function choose(value : boolean) return boolean is
  begin
    return )" << (boolean_passthrough ? "value" : "not value") << R"(;
  end function;
  procedure assign_value(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + )" << (bias + 10U) << R"(;
  end procedure;
  procedure assign_value(
    source : in boolean;
    variable target : out boolean) is
  begin
    target := )" << (boolean_passthrough ? "source" : "not source") << R"(;
  end procedure;
  function static_select(value : boolean) return integer is
  begin
    return 99;
  end function;
  function static_select(value : integer) return integer is
  begin
    return value + 1;
  end function;
  function aggregate_select(value : static_bits) return integer is
  begin
    return 88;
  end function;
  function bit_select(value : bit) return integer is
  begin
    return 89;
  end function;
  function package_resolve(values : package_bit_pair) return bit is
  begin
    return values(0) or values(1);
  end function;
end package body;
)";
        assert(output.good());
      };

  {
    std::ofstream output(top, std::ios::binary);
    output << R"(
entity overload_app is
end entity;
use work.overload_pkg.all;
architecture rtl of overload_app is
  signal integer_input : integer;
  signal boolean_input : boolean;
  signal package_integer : integer;
  signal selected_integer : integer;
  signal package_boolean : boolean;
  signal selected_boolean : boolean;
  signal local_integer : integer;
  signal local_boolean : boolean;
  signal package_proc_integer : integer;
  signal selected_proc_integer : integer;
  signal package_proc_boolean : boolean;
  signal selected_proc_boolean : boolean;
  signal local_proc_integer : integer;
  signal local_proc_boolean : boolean;
  signal contextual_integer : integer;
  signal contextual_boolean : boolean;
  signal named_integer : integer;
  signal named_boolean : boolean;
  signal defaulted_integer : integer;
  signal nested_integer : integer;
  signal nested_boolean : boolean;
  signal named_proc_integer : integer;
  signal named_proc_boolean : boolean;
  signal converted_integer : integer;
  signal converted_enum : integer;
  type first_kind is (shared, first_only);
  type second_kind is (shared, second_only);
  type bit_pair is array (0 to 1) of bit;
  type logic_pair is array (0 to 1) of std_logic;
  function local_choose(value : integer) return integer is
  begin
    return value + 30;
  end function;
  function local_choose(value : boolean) return boolean is
  begin
    return not value;
  end function;
  procedure local_assign(
    source : in integer;
    variable target : out integer) is
  begin
    target := source + 40;
  end procedure;
  procedure local_assign(
    source : in boolean;
    variable target : out boolean) is
  begin
    target := not source;
  end procedure;
  function contextual(value : integer) return integer is
  begin
    return value + 50;
  end function;
  function contextual(value : integer) return boolean is
  begin
    return true;
  end function;
  function named_select(
    number : integer;
    flag : boolean := false) return integer is
  begin
    return number + 60;
  end function;
  function named_select(
    flag : boolean;
    number : integer := 0) return boolean is
  begin
    return flag;
  end function;
  function inner(value : integer) return integer is
  begin
    return value + 70;
  end function;
  function inner(value : boolean) return boolean is
  begin
    return not value;
  end function;
  function outer(value : integer) return integer is
  begin
    return value + 80;
  end function;
  function outer(value : boolean) return boolean is
  begin
    return not value;
  end function;
  procedure named_assign(
    integer_source : in integer;
    variable target : out integer;
    flag : in boolean := false) is
  begin
    target := integer_source + 90;
  end procedure;
  procedure named_assign(
    flag : in boolean;
    variable target : out boolean;
    number : in integer := 0) is
  begin
    target := flag;
  end procedure;
  function enum_select(value : first_kind) return integer is
  begin
    return 101;
  end function;
  function enum_select(value : second_kind) return integer is
  begin
    return 102;
  end function;
  function "+"(
    left_value : first_kind;
    right_value : first_kind) return integer is
  begin
    return 77;
  end function;
  function resolve_or(values : bit_pair) return bit is
  begin
    return values(0) or values(1);
  end function;
  function resolve_and(values : bit_pair) return bit is
  begin
    return values(0) and values(1);
  end function;
  function resolve_logic_or(values : logic_pair) return std_logic is
  begin
    return values(0) or values(1);
  end function;
  subtype resolved_bit is resolve_or bit;
  subtype resolved_and_bit is resolve_and bit;
  subtype resolved_logic is resolve_logic_or std_logic;
  type resolved_pair is array (0 to 1) of resolved_bit;
  signal resolved_value : resolved_bit;
  signal resolved_and_value : resolved_and_bit;
  signal resolved_logic_value : resolved_logic;
  signal resolved_pair_value : resolved_pair;
  signal package_resolved_value : package_resolved_bit;
  signal static_vector_value : static_bits;
  signal static_overload_value : integer;
  signal operator_value : integer;
  signal attribute_value : integer;
  signal static_converted_value : integer;
  signal static_attribute_value : integer;
  signal static_operator_value : integer;
  signal static_nested_value : integer;
  signal static_length_value : integer;
  signal aggregate_value : integer;
  signal indexed_value : integer;
  signal slice_value : integer;
begin
  resolved_value <= '1' after 1 ns;
  resolved_value <= '0' after 2 ns;
  resolved_and_value <= '1' after 1 ns;
  resolved_and_value <= '0' after 2 ns;
  resolved_logic_value <= 'H' after 1 ns;
  resolved_logic_value <= 'L' after 2 ns;
  resolved_pair_value <= "10" after 1 ns;
  resolved_pair_value <= "01" after 2 ns;
  package_resolved_value <= '1' after 1 ns;
  package_resolved_value <= '0' after 2 ns;
  static_vector_value <= (others => '1');
  static_overload_value <= static_overload;
  operator_value <=
    first_kind(shared) + first_kind(shared);
  attribute_value <= enum_select(first_kind'val(0));
  static_converted_value <= static_converted;
  static_attribute_value <= static_attribute;
  static_operator_value <= static_operator;
  static_nested_value <= static_nested;
  static_length_value <= static_length;
  aggregate_value <= aggregate_select((others => '1'));
  indexed_value <= bit_select(static_vector_value(2));
  slice_value <= aggregate_select(static_vector_value(2 to 4));
  process(integer_input, boolean_input)
  begin
    package_integer <= choose(integer_input);
    selected_integer <= overload_pkg.choose(integer_input);
    package_boolean <= choose(boolean_input);
    selected_boolean <= work.overload_pkg.choose(boolean_input);
    local_integer <= local_choose(integer_input);
    local_boolean <= local_choose(boolean_input);
    contextual_integer <= contextual(integer_input);
    contextual_boolean <= contextual(integer_input);
    named_integer <= named_select(number => integer_input);
    named_boolean <= named_select(flag => boolean_input);
    defaulted_integer <= named_select(integer_input);
    nested_integer <= outer(inner(integer_input));
    nested_boolean <= outer(inner(boolean_input));
    converted_integer <= named_select(positive(integer_input));
    converted_enum <= enum_select(first_kind(shared));
    assign_value(integer_input, package_proc_integer);
    overload_pkg.assign_value(
      integer_input, selected_proc_integer);
    assign_value(boolean_input, package_proc_boolean);
    work.overload_pkg.assign_value(
      boolean_input, selected_proc_boolean);
    local_assign(integer_input, local_proc_integer);
    local_assign(boolean_input, local_proc_boolean);
    named_assign(
      target => named_proc_integer,
      integer_source => integer_input);
    named_assign(
      flag => boolean_input,
      target => named_proc_boolean);
  end process;
end architecture;
)";
    assert(output.good());
  }

  for (const auto optimization :
       {fsim::project::Optimization::o0,
        fsim::project::Optimization::o2}) {
    write_package_body(10, false);
    const auto config = make_config(
        directory.path,
        package_declaration,
        package_body,
        top,
        optimization);
    const auto reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto cold = run_once(
        config, fsim::app::SimulationEngine::compiled);
    const auto warm = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(reference, 10, false);
    verify(cold, 10, false);
    verify(warm, 10, false);
    assert(reference.values == cold.values);
    assert(cold.values == warm.values);
    assert(reference.resolved_drivers == cold.resolved_drivers);
    assert(cold.resolved_drivers == warm.resolved_drivers);
    assert(
        reference.logic9_resolved_drivers
        == cold.logic9_resolved_drivers);
    assert(
        cold.logic9_resolved_drivers
        == warm.logic9_resolved_drivers);
    assert(
        reference.composite_resolved_drivers
        == cold.composite_resolved_drivers);
    assert(
        cold.composite_resolved_drivers
        == warm.composite_resolved_drivers);
    assert(
        reference.package_resolved_drivers
        == cold.package_resolved_drivers);
    assert(
        cold.package_resolved_drivers
        == warm.package_resolved_drivers);
    assert(reference.vcd == cold.vcd);
    assert(cold.vcd == warm.vcd);
    assert(reference.keys == cold.keys);
    assert(cold.keys == warm.keys);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores == cold.cache.misses);
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);
#endif

    write_package_body(11, true);
    const auto changed_reference = run_once(
        config, fsim::app::SimulationEngine::interpreter);
    const auto changed = run_once(
        config, fsim::app::SimulationEngine::compiled);
    verify(changed_reference, 11, true);
    verify(changed, 11, true);
    assert(reference.keys != changed_reference.keys);
    assert(changed_reference.keys == changed.keys);
  }
  return 0;
}
