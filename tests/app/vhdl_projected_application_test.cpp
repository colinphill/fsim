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
  std::uint64_t delta{};

  friend bool operator==(const Change&, const Change&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<Change> changes;
  std::vector<std::pair<std::string, std::string>> final_values;
  std::string vcd;
  std::string resolution;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "vhdl-projected";
  config.project.top = "vhdl:work.vhdl_projected(rtl)";
  config.project.time_resolution = "auto";
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

  Capture capture;
  capture.resolution = project->time_resolution;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();

  constexpr std::array<std::string_view, 15> names{
      "vhdl_projected.default_output",
      "vhdl_projected.explicit_output",
      "vhdl_projected.transport_output",
      "vhdl_projected.selected_output",
      "vhdl_projected.slice_output",
      "vhdl_projected.conditional_output",
      "vhdl_projected.sequential_output",
      "vhdl_projected.waveform_output",
      "vhdl_projected.waveform_slice_output",
      "vhdl_projected.conditional_waveform_output",
      "vhdl_projected.selected_waveform_output",
      "vhdl_projected.resolved_output",
      "vhdl_projected.delta_output",
      "vhdl_projected.forced_output",
      "vhdl_projected.forced_driver_output"};
  std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
  std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals{};
  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(
      vcd_output, capture.resolution, 128);
  for (std::size_t index = 0; index < names.size(); ++index) {
    const auto signal = simulation.find_signal(names[index]);
    assert(signal);
    signals[index] = *signal;
    vcd_signals[index] = vcd.declare_signal(
        std::string{names[index]},
        names[index].find("slice") != std::string_view::npos
            ? 4U
            : 1U);
  }
  vcd.begin(simulation.now());
  for (std::size_t index = 0; index < signals.size(); ++index) {
    vcd.change(
        vcd_signals[index], simulation.read_signal(signals[index]));
  }
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        const auto found =
            std::find(signals.begin(), signals.end(), signal);
        if (found == signals.end()) {
          return;
        }
        const auto index = static_cast<std::size_t>(
            std::distance(signals.begin(), found));
        capture.changes.push_back(
            {
                std::string{names[index]},
                value.to_msb_string(),
                time,
                delta});
        vcd.set_time(time);
        vcd.change(vcd_signals[index], value);
      });
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    capture.final_values.emplace_back(
        names[index],
        simulation.read_signal(signals[index]).to_msb_string());
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

std::vector<std::pair<std::string, fsim::runtime::SimulationTick>>
changes_for(
    const Capture& capture,
    const std::string_view signal) {
  std::vector<std::pair<
      std::string,
      fsim::runtime::SimulationTick>> result;
  for (const auto& change : capture.changes) {
    if (change.signal == signal) {
      result.emplace_back(change.value, change.time);
    }
  }
  return result;
}

std::vector<std::tuple<
    std::string,
    fsim::runtime::SimulationTick,
    std::uint64_t>>
changes_with_delta(
    const Capture& capture,
    const std::string_view signal) {
  std::vector<std::tuple<
      std::string,
      fsim::runtime::SimulationTick,
      std::uint64_t>> result;
  for (const auto& change : capture.changes) {
    if (change.signal == signal) {
      result.emplace_back(change.value, change.time, change.delta);
    }
  }
  return result;
}

void verify_capture(const Capture& capture) {
  using TimedValue =
      std::pair<std::string, fsim::runtime::SimulationTick>;
  assert(capture.result.status == fsim::runtime::RunStatus::completed);
  assert(capture.result.time == 33);
  assert(capture.resolution == "1ps");
  assert((
      changes_for(capture, "vhdl_projected.default_output")
      == std::vector<TimedValue>{{"0", 5}}));
  assert((
      changes_for(capture, "vhdl_projected.explicit_output")
      == std::vector<TimedValue>{
          {"0", 5}, {"1", 25}, {"0", 28}}));
  assert((
      changes_for(capture, "vhdl_projected.transport_output")
      == std::vector<TimedValue>{
          {"0", 5}, {"1", 25}, {"0", 27}}));
  assert((
      changes_for(capture, "vhdl_projected.selected_output")
      == std::vector<TimedValue>{
          {"0", 4}, {"1", 24}, {"0", 27}}));
  assert((
      changes_for(capture, "vhdl_projected.slice_output")
      == std::vector<TimedValue>{
          {"Z00Z", 3}, {"Z11Z", 23}}));
  assert((
      changes_for(capture, "vhdl_projected.conditional_output")
      == std::vector<TimedValue>{
          {"0", 6}, {"1", 20}, {"0", 22}}));
  assert((
      changes_for(capture, "vhdl_projected.sequential_output")
      == std::vector<TimedValue>{
          {"0", 0}, {"1", 25}, {"0", 28}}));
  assert((
      changes_for(capture, "vhdl_projected.waveform_output")
      == std::vector<TimedValue>{
          {"0", 1}, {"1", 4}, {"0", 7}}));
  assert((
      changes_for(capture, "vhdl_projected.waveform_slice_output")
      == std::vector<TimedValue>{
          {"Z00Z", 2}, {"Z11Z", 5}}));
  assert((
      changes_for(capture, "vhdl_projected.conditional_waveform_output")
      == std::vector<TimedValue>{
          {"1", 21}, {"0", 23}}));
  assert((
      changes_for(capture, "vhdl_projected.selected_waveform_output")
      == std::vector<TimedValue>{
          {"1", 22}, {"0", 25}}));
  assert((
      changes_for(capture, "vhdl_projected.resolved_output")
      == std::vector<TimedValue>{
          {"0", 1}, {"X", 21}, {"1", 23}, {"0", 24}}));
  assert((
      changes_with_delta(capture, "vhdl_projected.delta_output")
      == std::vector<std::tuple<
          std::string,
          fsim::runtime::SimulationTick,
          std::uint64_t>>{{"0", 0, 2}}));
  assert((
      changes_for(capture, "vhdl_projected.forced_output")
      == std::vector<TimedValue>{
          {"0", 0}, {"1", 2}, {"0", 6}}));
  assert((
      changes_for(capture, "vhdl_projected.forced_driver_output")
      == std::vector<TimedValue>{
          {"0", 0}, {"X", 2}, {"0", 6}}));
  assert(capture.vcd.find("$timescale 1ps $end")
         != std::string::npos);
  assert(capture.vcd.find("#28") != std::string::npos);
}

void verify_invalid_rejection(
    const std::filesystem::path& directory) {
  const auto source = directory / "invalid_rejection.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity invalid_rejection is
end entity;
architecture rtl of invalid_rejection is
  signal result : std_logic;
begin
  result <= reject 6 ps inertial '1' after 5 ps;
end architecture;
)";
    assert(output.good());
  }
  auto config = make_config(
      directory, source, fsim::project::Optimization::o2);
  config.project.top =
      "vhdl:work.invalid_rejection(rtl)";
  fsim::diagnostic::Engine diagnostics;
  assert(!fsim::app::build_project(config, diagnostics));
  assert(std::ranges::any_of(
      diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-VHDL-SEM-032";
      }));

  const auto inexact_source =
      directory / "inexact_rejection.vhd";
  {
    std::ofstream output(inexact_source, std::ios::binary);
    output << R"(
entity inexact_rejection is
end entity;
architecture rtl of inexact_rejection is
  signal result : std_logic;
begin
  result <= reject 1 ps inertial '1' after 2 ps;
end architecture;
)";
    assert(output.good());
  }
  auto inexact_config = make_config(
      directory,
      inexact_source,
      fsim::project::Optimization::o2);
  inexact_config.project.top =
      "vhdl:work.inexact_rejection(rtl)";
  inexact_config.project.time_resolution = "2ps";
  fsim::diagnostic::Engine inexact_diagnostics;
  assert(!fsim::app::build_project(
      inexact_config, inexact_diagnostics));
  assert(std::ranges::any_of(
      inexact_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-TIME-0003";
      }));

  const auto unordered_source =
      directory / "unordered_waveform.vhd";
  {
    std::ofstream output(unordered_source, std::ios::binary);
    output << R"(
entity unordered_waveform is
end entity;
architecture rtl of unordered_waveform is
  signal result : std_logic;
begin
  result <= transport '1' after 5 ps, '0' after 5 ps;
end architecture;
)";
    assert(output.good());
  }
  auto unordered_config = make_config(
      directory,
      unordered_source,
      fsim::project::Optimization::o2);
  unordered_config.project.top =
      "vhdl:work.unordered_waveform(rtl)";
  fsim::diagnostic::Engine unordered_diagnostics;
  assert(!fsim::app::build_project(
      unordered_config, unordered_diagnostics));
  assert(std::ranges::any_of(
      unordered_diagnostics.diagnostics(),
      [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-VHDL-SEM-034";
      }));
}

void verify_sequential_block_runtime(
    const std::filesystem::path& directory) {
  const auto block_directory = directory / "sequential-block-2019";
  std::filesystem::create_directories(block_directory);
  const auto source = block_directory / "sequential_block_runtime.vhd";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity sequential_block_runtime is
end entity;
architecture rtl of sequential_block_runtime is
  type Bit_Pointer is access bit;
  signal wait_value : integer := 0;
  signal return_value : integer := 0;
  signal reentry_value : integer := 0;
  procedure leave_nested(variable target : out integer) is
  begin
    returned : block is
      variable local_value : integer := 7;
      variable transient : Bit_Pointer;
    begin
      transient := new bit;
      target := local_value;
      return;
      target := 99;
    end block returned;
  end procedure;
begin
  exercise : process
    variable result : integer := 0;
    variable iteration : integer := 0;
    variable total : integer := 0;
  begin
    delayed : block is
      variable retained : integer := 2;
    begin
      retained := retained + 1;
      wait for 2 ns;
      retained := retained + 2;
      wait_value <= retained;
    end block delayed;

    leave_nested(result);
    return_value <= result;

    while iteration < 2 loop
      iteration := iteration + 1;
      repeated : block is
        variable fresh : integer := 3;
      begin
        fresh := fresh + iteration;
        total := total + fresh;
      end block repeated;
    end loop;
    reentry_value <= total;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  struct Result {
    fsim::runtime::RunResult run;
    std::array<std::uint64_t, 3> values{};
    std::size_t compiled_processes{};
  };
  const auto run = [&](const fsim::project::Optimization optimization,
                       const fsim::app::SimulationEngine engine) {
    auto config = make_config(block_directory, source, optimization);
    config.project.name = "vhdl-2019-sequential-block-runtime";
    config.project.top = "vhdl:work.sequential_block_runtime(rtl)";
    config.source_sets.front().standard = "2019";
    config.build.cache_path = block_directory
        / (optimization == fsim::project::Optimization::o0
               ? "cache-o0"
               : "cache-o2");
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(project);
    assert(std::ranges::any_of(
        project->design.processes().front().operations,
        [](const auto& operation) {
          return fsim::runtime::simir::operation_get_if<
                     fsim::runtime::simir::DeleteContainer>(&operation)
              != nullptr;
        }));
    for (const std::string_view label : {
             "delayed", "returned", "repeated"}) {
      const auto block = std::ranges::find(
          project->vhdl_hir.statements(), label,
          &fsim::semantic::vhdl::Statement::label);
      assert(
          block != project->vhdl_hir.statements().end()
          && block->kind == fsim::semantic::vhdl::StatementKind::block
          && block->nested_scope);
    }
    fsim::app::Simulation simulation{
        std::move(*project), config.run.max_deltas, engine};
    const auto waited = simulation.find_signal(
        "sequential_block_runtime.wait_value");
    const auto returned = simulation.find_signal(
        "sequential_block_runtime.return_value");
    const auto reentered = simulation.find_signal(
        "sequential_block_runtime.reentry_value");
    assert(waited && returned && reentered);
    Result result;
    result.compiled_processes = simulation.compiled_process_count();
    result.run = simulation.run();
    result.values = {
        simulation.read_signal(*waited).low_word().aval,
        simulation.read_signal(*returned).low_word().aval,
        simulation.read_signal(*reentered).low_word().aval};
    return result;
  };

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto reference = run(
        optimization, fsim::app::SimulationEngine::interpreter);
    const auto cold = run(
        optimization, fsim::app::SimulationEngine::compiled);
    const auto warm = run(
        optimization, fsim::app::SimulationEngine::compiled);
    constexpr std::array<std::uint64_t, 3> expected{5, 7, 9};
    assert(
        reference.run.status == fsim::runtime::RunStatus::completed
        && reference.run.time == 2
        && reference.values == expected
        && cold.run.status == reference.run.status
        && cold.run.time == reference.run.time
        && cold.values == reference.values
        && warm.run.status == reference.run.status
        && warm.run.time == reference.run.time
        && warm.values == reference.values);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0 && warm.compiled_processes > 0);
#endif
  }

  const auto exception_source =
      block_directory / "sequential_block_exception.vhd";
  {
    std::ofstream output(exception_source, std::ios::binary);
    output << R"(
entity sequential_block_exception is
end entity;
architecture rtl of sequential_block_exception is
begin
  exercise : process
  begin
    failing : block is
      variable retained : integer := 4;
    begin
      wait for 1 ns;
      retained := retained + 1;
      assert retained = 0
        report "sequential block exception propagated"
        severity failure;
    end block failing;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    for (const auto engine : {
             fsim::app::SimulationEngine::interpreter,
             fsim::app::SimulationEngine::compiled}) {
      auto config = make_config(
          block_directory, exception_source, optimization);
      config.project.name = "vhdl-2019-sequential-block-exception";
      config.project.top =
          "vhdl:work.sequential_block_exception(rtl)";
      config.source_sets.front().standard = "2019";
      config.build.cache_path = block_directory
          / (optimization == fsim::project::Optimization::o0
                 ? "exception-cache-o0"
                 : "exception-cache-o2");
      fsim::diagnostic::Engine diagnostics;
      auto project = fsim::app::build_project(config, diagnostics);
      if (!project) {
        for (const auto& diagnostic : diagnostics.diagnostics()) {
          std::cerr << diagnostic.code << ": "
                    << diagnostic.message << '\n';
        }
      }
      assert(project);
      assert(std::ranges::any_of(
          project->design.processes().front().operations,
          [](const auto& operation) {
            const auto* point =
                fsim::runtime::simir::operation_get_if<
                    fsim::runtime::simir::DebugPoint>(&operation);
            return point != nullptr
                && point->scope.find(".failing")
                    != std::string::npos;
          }));
      fsim::app::Simulation simulation{
          std::move(*project), config.run.max_deltas, engine};
#if defined(FSIM_HAS_LLVM)
      if (engine == fsim::app::SimulationEngine::compiled) {
        assert(simulation.compiled_process_count() > 0);
      }
#endif
      bool propagated = false;
      try {
        (void)simulation.run();
      } catch (const fsim::runtime::simir::AssertionError& error) {
        propagated = std::string_view{error.what()}.find(
                         "sequential block exception propagated")
                != std::string_view::npos
            && error.source().path.ends_with(
                "sequential_block_exception.vhd")
            && error.source().line != 0;
      }
      assert(propagated);
    }
  }
}

void verify_executable_hir(const fsim::project::Config& config) {
  fsim::diagnostic::Engine diagnostics;
  auto checked = fsim::app::check_project(config, diagnostics);
  if (!checked) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
    }
  }
  assert(checked);
  const auto architecture = std::ranges::find_if(
      checked->vhdl_hir.units(), [](const auto& unit) {
        return unit.kind
                   == fsim::semantic::vhdl::UnitKind::architecture
            && unit.name == "rtl"
            && unit.primary_name == "vhdl_projected";
      });
  assert(architecture != checked->vhdl_hir.units().end());
  assert(architecture->processes.size() == 5);
  assert(std::ranges::any_of(
      checked->vhdl_hir.processes(),
      [](const auto& process) {
        return process.name == "settled_observer"
            && process.postponed;
      }));
  assert(std::ranges::any_of(
      checked->vhdl_hir.statements(),
      [](const auto& statement) {
        return statement.label == "settled_concurrent_observer"
            && statement.postponed;
      }));
  assert(std::ranges::any_of(
      checked->vhdl_hir.statements(),
      [](const auto& statement) {
        return statement.label == "settled_procedure_observer"
            && statement.postponed;
      }));
  assert(std::ranges::any_of(
      checked->vhdl_hir.statements(),
      [](const auto& statement) {
        return statement.label == "guarded_driver"
            && statement.disconnection_delay
            && statement.disconnection_delay->primary.magnitude == 2;
      }));
  assert(!architecture->concurrent_statements.empty());
  const auto waveform = std::ranges::find_if(
      checked->vhdl_hir.statements(), [](const auto& statement) {
        return statement.kind
                   == fsim::semantic::vhdl::StatementKind::signal_assignment
            && statement.waveform.size() == 3;
      });
  assert(waveform != checked->vhdl_hir.statements().end());
  assert(
      waveform->delay_mechanism
      == fsim::semantic::vhdl::DelayMechanism::transport);
  assert(waveform->waveform[0].delay);
  assert(waveform->waveform[1].delay);
  assert(waveform->waveform[2].delay);
  assert(
      waveform->waveform[0].delay->primary.magnitude == 1);
  assert(
      waveform->waveform[2].delay->primary.magnitude == 7);
  assert(std::ranges::any_of(
      checked->vhdl_hir.statements(), [](const auto& statement) {
        return statement.unaffected;
      }));
  assert(std::ranges::any_of(
      checked->vhdl_hir.statements(), [](const auto& statement) {
        return statement.kind
            == fsim::semantic::vhdl::StatementKind::wait_statement;
      }));
  assert(!checked->vhdl_hir.expressions().empty());
  assert(
      checked->semantics.expression_identities().size()
      == checked->vhdl_hir.expressions().size());
  assert(
      checked->semantics.statement_identities().size()
      == checked->vhdl_hir.statements().size());
  assert(
      checked->semantics.process_identities().size()
      == checked->vhdl_hir.processes().size());
  const auto retained_statement = waveform->id;
  const auto retained_expression = waveform->waveform.front().value;
  checked->parsed.units.clear();
  assert(waveform->id == retained_statement);
  assert(waveform->waveform.front().value == retained_expression);
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-vhdl-projected-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "vhdl_projected.vhd";
  {
    // FSIM-CONFORMANCE CF-VHDL-TRANSACTION-001 source=SRC-IEEE-P1076 expectation=execute
    std::ofstream output(source, std::ios::binary);
    output << R"(
entity vhdl_projected is
end entity;

architecture rtl of vhdl_projected is
  signal default_drive : std_logic;
  signal explicit_drive : std_logic;
  signal transport_drive : std_logic;
  signal selected_drive : std_logic;
  signal slice_drive : std_logic_vector(1 downto 0);
  signal selector : boolean;
  signal default_output : std_logic;
  signal explicit_output : std_logic;
  signal transport_output : std_logic;
  signal selected_output : std_logic;
  signal slice_output : std_logic_vector(3 downto 0);
  signal conditional_output : std_logic;
  signal sequential_output : std_logic;
  signal waveform_output : std_logic;
  signal waveform_slice_output : std_logic_vector(3 downto 0);
  signal conditional_waveform_output : std_logic;
  signal selected_waveform_output : std_logic;
  signal resolved_a : std_logic;
  signal resolved_b : std_logic;
  signal resolved_output : std_logic;
  signal delta_source : std_logic;
  signal delta_middle : std_logic;
  signal delta_output : std_logic;
  signal forced_output : std_logic;
  signal forced_driver_output : std_logic;
  signal guard_enabled : boolean;
  signal guarded_value : std_logic;
  constant rejection_limit : time := 2 ps;
  constant projected_delay : time := 5 ps;
  procedure observe_settled(value : in std_logic) is
  begin
    null;
  end procedure;
begin
  default_output <= default_drive after 5 ps;
  explicit_output <=
      reject rejection_limit inertial
      explicit_drive after projected_delay;
  transport_output <= transport transport_drive after 5 ps;
  with selector select
    selected_output <= reject 2 ps inertial
      selected_drive after 4 ps when true,
      '0' after 4 ps when others;
  slice_output(2 downto 1) <=
      transport slice_drive after 3 ps;
  conditional_output <= transport
      transport_drive when selector else '0' after 6 ps;
  waveform_output <= transport
      '0' after 1 ps, '1' after 4 ps, '0' after 7 ps;
  waveform_slice_output(2 downto 1) <= transport
      "00" after 2 ps, "11" after 5 ps;
  conditional_waveform_output <= transport
      '1' after 1 ps, '0' after 3 ps
      when selector else unaffected;
  with selector select
    selected_waveform_output <= transport
      '1' after 2 ps, '0' after 5 ps when true,
      unaffected when others;
  resolved_output <= transport resolved_a after 1 ps;
  resolved_output <= transport resolved_b after 1 ps;
  delta_middle <= transport delta_source;
  delta_output <= transport delta_middle;

  guarded_scope: block (guard_enabled) is
    disconnect guarded_value : std_logic after 2 ps;
  begin
    guarded_driver: guarded_value <= guarded '1';
  end block guarded_scope;

  settled_concurrent_observer: postponed assert
      not delta_source'event or delta_source = '0'
    report "postponed concurrent assertion missed the committed value"
    severity failure;

  settled_procedure_observer: postponed observe_settled(delta_source);

  settled_observer: postponed process(delta_source)
  begin
    if delta_source'event then
      assert delta_source = '0'
        report "postponed process missed the committed triggering value"
        severity failure;
    end if;
  end postponed process settled_observer;

  stimulus: process
  begin
    default_drive <= '0';
    explicit_drive <= '0';
    transport_drive <= '0';
    selected_drive <= '0';
    slice_drive <= "00";
    selector <= false;
    resolved_a <= '0';
    resolved_b <= 'Z';
    delta_source <= '0';
    wait for 20 ps;
    default_drive <= '1';
    explicit_drive <= '1';
    transport_drive <= '1';
    selected_drive <= '1';
    slice_drive <= "11";
    selector <= true;
    resolved_a <= '1';
    resolved_b <= '0';
    wait for 2 ps;
    default_drive <= '0';
    transport_drive <= '0';
    resolved_b <= 'Z';
    wait for 1 ps;
    explicit_drive <= '0';
    selector <= false;
    resolved_a <= '0';
    wait for 10 ps;
    wait;
  end process;

  sequential: process
  begin
    sequential_output <= '0';
    wait for 20 ps;
    sequential_output <= reject 2 ps inertial '1' after 5 ps;
    wait for 3 ps;
    sequential_output <= reject 2 ps inertial '0' after 5 ps;
    wait;
  end process;

  forcing: process
  begin
    forced_output <= '0';
    forced_driver_output <= 'Z';
    wait for 2 ps;
    forced_output <= force in '1';
    forced_driver_output <= force out '1';
    assert forced_driver_output'driving_value = '1'
      report "force out must update driving_value"
      severity failure;
    wait for 2 ps;
    forced_output <= '0';
    wait for 2 ps;
    forced_output <= release in;
    forced_driver_output <= release out;
    assert forced_driver_output'driving_value = 'Z'
      report "release out must restore driving_value"
      severity failure;
    wait;
  end process;

  other_forced_driver: process
  begin
    forced_driver_output <= '0';
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  verify_executable_hir(
      make_config(
          directory.path,
          source,
          fsim::project::Optimization::o0));
  verify_sequential_block_runtime(directory.path);

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2}) {
    const auto config =
        make_config(directory.path, source, optimization);
    const auto reference =
        run_once(config, fsim::app::SimulationEngine::interpreter);
    const auto cold =
        run_once(config, fsim::app::SimulationEngine::compiled);
    const auto warm =
        run_once(config, fsim::app::SimulationEngine::compiled);
    verify_capture(reference);
    assert(reference.result.status == cold.result.status);
    assert(reference.result.time == cold.result.time);
    assert(reference.changes == cold.changes);
    assert(reference.final_values == cold.final_values);
    assert(reference.vcd == cold.vcd);
    assert(reference.changes == warm.changes);
    assert(reference.final_values == warm.final_values);
    assert(reference.vcd == warm.vcd);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes == 23);
    assert(cold.native_cache.hits == 0);
    assert(cold.native_cache.misses != 0);
    assert(cold.native_cache.stores == cold.native_cache.misses);
    assert(warm.compiled_processes == 23);
    assert(warm.native_cache.hits == cold.native_cache.misses);
    assert(warm.native_cache.misses == 0);
#else
    assert(cold.compiled_processes == 0);
    assert(warm.compiled_processes == 0);
#endif
  }
  verify_invalid_rejection(directory.path);
  std::cout << "VHDL projected waveform application tests passed\n";
  return 0;
}
