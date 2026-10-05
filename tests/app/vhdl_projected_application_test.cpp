// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/design_artifact.hpp"

#include "fsim/runtime/scheduler.hpp"
#include "../../src/app/application_simulation_internal.hpp"
#include "../../src/runtime/simir_internal.hpp"
#include "fsim/support/environment.hpp"
#include "fsim/runtime/vcd_writer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::runtime::simir {

struct NativeRegionAllocationTestAccess {
  struct ProjectedRegionStats {
    std::uint64_t attempts{};
    std::uint64_t backend_runs{};
    std::uint64_t completions{};
    std::uint64_t members{};
    std::uint64_t publications{};
    std::uint64_t declines{};
    std::uint64_t failures{};
  };

  struct SignalMetadata {
    std::optional<std::pair<SimulationTick, std::uint64_t>> event;
    std::optional<std::pair<SimulationTick, std::uint64_t>> transaction;
    std::uint64_t revision{};

    friend bool operator==(
        const SignalMetadata&, const SignalMetadata&) = default;
  };

  [[nodiscard]] static SchedulerBatchCompactionStats generic_ticket_stats(
      const fsim::app::Simulation& simulation) noexcept {
    return simulation.impl_->interpreter->scheduler()
        .generic_batch_compaction_stats();
  }

  [[nodiscard]] static ProjectedRegionStats projected_region_stats(
      const fsim::app::Simulation& simulation) noexcept {
    const auto& state = *simulation.impl_->interpreter->impl_;
    return { state.generic_projected_region_attempts,
      state.generic_projected_region_backend_runs,
      state.generic_projected_region_completions,
      state.generic_projected_region_members,
      state.generic_projected_region_publications,
      state.generic_projected_region_declines,
      state.generic_projected_region_failures };
  }

  [[nodiscard]] static SignalMetadata signal_metadata(
      const fsim::app::Simulation& simulation, const SignalId signal) {
    const auto& state = *simulation.impl_->interpreter->impl_;
    return { state.signal_events.at(signal),
      state.signal_transactions.at(signal),
      state.signal_value_revisions.at(signal) };
  }

  [[nodiscard]] static std::array<ProcessId, 3U> writer_processes(
      const fsim::app::Simulation& simulation,
      const std::array<SignalId, 3U>& outputs) {
    const auto& state = *simulation.impl_->interpreter->impl_;
    constexpr auto no_process = std::numeric_limits<ProcessId>::max();
    std::array<ProcessId, 3U> result { no_process, no_process, no_process };
    if (!state.region_graph) {
      return result;
    }
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
      if (outputs[index] >= state.region_graph->signals().size()) {
        return { no_process, no_process, no_process };
      }
      const auto& writers
          = state.region_graph->signals()[outputs[index]].writers;
      if (writers.size() != 1U) {
        return { no_process, no_process, no_process };
      }
      result[index] = writers.front().process;
    }
    return result;
  }

  [[nodiscard]] static bool process_queued(
      const fsim::app::Simulation& simulation, const ProcessId process) {
    const auto& state = *simulation.impl_->interpreter->impl_;
    return process < state.processes.size() && state.processes[process].queued;
  }

  [[nodiscard]] static bool process_waiting_on_static(
      const fsim::app::Simulation& simulation, const ProcessId process) {
    const auto& state = *simulation.impl_->interpreter->impl_;
    return process < state.processes.size()
        && state.processes[process].waiting_on_static;
  }

  [[nodiscard]] static SchedulerGenericKeyReceipt vhdl_receipt(
      const fsim::app::Simulation& simulation, const ProcessId process) {
    const auto& state = *simulation.impl_->interpreter->impl_;
    if (process >= state.region_component_by_process.size()) {
      return { };
    }
    const auto component = state.region_component_by_process[process];
    if (component >= state.vhdl_projected_readiness_by_component.size()) {
      return { };
    }
    const auto& task = state.vhdl_projected_readiness_by_component[component];
    if (!task) {
      return { };
    }
    const auto member = task->member_index(process);
    return member ? task->members[*member].receipt
                  : SchedulerGenericKeyReceipt { };
  }

  [[nodiscard]] static SchedulerOrderKey reserve_order_key(
      fsim::app::Simulation& simulation, const StableOrder order) {
    return simulation.impl_->interpreter->scheduler().reserve_order_key(order);
  }

  [[nodiscard]] static std::size_t vhdl_projected_component_member_count(
      const fsim::app::Simulation& simulation,
      const std::array<SignalId, 3U>& outputs) {
    const auto& state = *simulation.impl_->interpreter->impl_;
    if (!state.region_graph) {
      return 0U;
    }

    constexpr auto no_component = std::numeric_limits<std::size_t>::max();
    auto component = no_component;
    std::array<ProcessId, 3U> writers{};
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
      const auto signal = outputs[index];
      if (signal >= state.region_graph->signals().size()) {
        return 0U;
      }
      const auto& signal_node = state.region_graph->signals()[signal];
      if (signal_node.writers.size() != 1U) {
        return 0U;
      }
      const auto process = signal_node.writers.front().process;
      if (process >= state.region_graph->processes().size()
          || process >= state.region_component_by_process.size()) {
        return 0U;
      }
      const auto& process_node = state.region_graph->processes()[process];
      const auto member_component
          = state.region_component_by_process[process];
      if (!process_node.pure
          || process_node.update_kind != RegionUpdateKind::vhdl_projected
          || process_node.scheduling_domain
              != ProcessSchedulingDomain::generic
          || member_component == no_component
          || (component != no_component && component != member_component)) {
        return 0U;
      }
      component = member_component;
      writers[index] = process;
    }

    if (component >= state.vhdl_projected_readiness_by_component.size()) {
      return 0U;
    }
    const auto& ticket
        = state.vhdl_projected_readiness_by_component[component];
    if (!ticket || ticket->invalidated
        || ticket->runtime_generation != state.region_runtime_generation
        || ticket->members.size() != outputs.size()) {
      return 0U;
    }
    for (const auto writer : writers) {
      if (std::ranges::count(ticket->members, writer,
              &Interpreter::Impl::VhdlProjectedReadinessTask::QueuedMember::process)
          != 1) {
        return 0U;
      }
    }
    return ticket->members.size();
  }
};

} // namespace fsim::runtime::simir

namespace {

using NativeAccess
    = fsim::runtime::simir::NativeRegionAllocationTestAccess;
using ProcessId = fsim::runtime::simir::ProcessId;
using SignalMetadata = NativeAccess::SignalMetadata;
using ProjectedRegionStats = NativeAccess::ProjectedRegionStats;

void set_environment_variable(const char* const name, const char* const value)
{
#if defined(_WIN32)
  const auto result = ::_putenv_s(name, value == nullptr ? "" : value);
#else
  const auto result = value == nullptr
      ? ::unsetenv(name)
      : ::setenv(name, value, 1);
#endif
  if (result != 0) {
    std::abort();
  }
}

class ScopedEnvironment final {
public:
  ScopedEnvironment(const char* const name, const char* const value)
      : name_(name)
      , previous_(fsim::support::environment_variable(name)) {
    set_environment_variable(name_.c_str(), value);
  }

  ~ScopedEnvironment() {
    set_environment_variable(name_.c_str(),
        previous_ ? previous_->c_str() : nullptr);
  }

  ScopedEnvironment(const ScopedEnvironment&) = delete;
  ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
  std::string name_;
  std::optional<std::string> previous_;
};

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

struct GenericCycleCapture {
  fsim::runtime::RunResult result;
  std::array<std::string, 3U> final_values;
  std::array<SignalMetadata, 3U> metadata;
  fsim::runtime::SchedulerBatchCompactionStats ticket_stats_before;
  fsim::runtime::SchedulerBatchCompactionStats ticket_stats_after;
  ProjectedRegionStats projected_stats_before;
  ProjectedRegionStats projected_stats_after;
  std::size_t component_members{};
  std::size_t compiled_processes{};
};

struct ForeignCutCapture {
  fsim::runtime::RunResult first_pause;
  fsim::runtime::RunResult suffix_pause;
  fsim::runtime::RunResult result;
  std::array<std::string, 4U> final_values;
  std::array<SignalMetadata, 4U> metadata;
  SignalMetadata source_before_deposit;
  SignalMetadata source_after_deposit;
  SignalMetadata root_before_force;
  SignalMetadata root_after_force;
  fsim::runtime::SchedulerBatchCompactionStats stats_at_foreign_pause;
  fsim::runtime::SchedulerBatchCompactionStats stats_after_suffix;
  fsim::runtime::SchedulerGenericKeyReceipt root_receipt_before;
  fsim::runtime::SchedulerGenericKeyReceipt root_receipt_at_foreign;
  fsim::runtime::SchedulerGenericKeyReceipt middle_receipt_before;
  fsim::runtime::SchedulerGenericKeyReceipt root_future_receipt;
  fsim::runtime::SchedulerGenericKeyReceipt root_receipt_at_pause;
  fsim::runtime::SchedulerGenericKeyReceipt root_receipt_after_suffix;
  fsim::runtime::SchedulerGenericKeyReceipt middle_receipt_at_pause;
  fsim::runtime::SchedulerGenericKeyReceipt middle_receipt_after_suffix;
  std::optional<fsim::runtime::SchedulerOrderKey> foreign_key;
  std::optional<fsim::runtime::SchedulerOrderKey> next_key_after_foreign;
  fsim::runtime::SimulationTick foreign_time{};
  std::uint64_t foreign_delta{};
  bool foreign_scheduled{};
  bool foreign_called{};
  bool suffix_safe_point{};
  bool force_control{};
  std::array<fsim::runtime::simir::ProcessId, 3U> writer_processes{};
  std::size_t component_members{};
  std::size_t compiled_processes{};
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
          {"U00U", 3}, {"U11U", 23}}));
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
          {"U00U", 2}, {"U11U", 5}}));
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
  assert(architecture->disconnection_specifications.size() == 3);
  const auto& named_disconnection
      = architecture->disconnection_specifications[0];
  assert(
      named_disconnection.selection
          == fsim::semantic::vhdl::DisconnectionSelection::explicit_names
      && named_disconnection.signals.size() == 2
      && named_disconnection.signals[0].canonical == "default_drive"
      && named_disconnection.signals[1].canonical == "explicit_drive"
      && named_disconnection.type_mark.canonical == "std_logic"
      && named_disconnection.delay.primary.expression
      && named_disconnection.source.valid()
      && named_disconnection.origin.valid());
  assert(
      architecture->disconnection_specifications[1].selection
          == fsim::semantic::vhdl::DisconnectionSelection::all
      && architecture->disconnection_specifications[1]
             .delay.primary.magnitude == 3
      && architecture->disconnection_specifications[1]
             .delay.primary.unit == "ps");
  assert(
      architecture->disconnection_specifications[2].selection
          == fsim::semantic::vhdl::DisconnectionSelection::others
      && architecture->disconnection_specifications[2]
             .delay.primary.magnitude == 4
      && architecture->disconnection_specifications[2]
             .delay.primary.unit == "ps");

  const auto generated_disconnections = std::ranges::find(
      architecture->generates,
      std::string { "retained_disconnections" },
      &fsim::semantic::vhdl::GenerateRegion::label);
  assert(generated_disconnections != architecture->generates.end());
  assert(
      generated_disconnections->disconnection_specifications.size() == 1
      && generated_disconnections->disconnection_specifications.front()
             .selection
          == fsim::semantic::vhdl::DisconnectionSelection::all
      && generated_disconnections->disconnection_specifications.front()
             .delay.primary.expression);
  const auto nested_disconnections = std::ranges::find(
      generated_disconnections->nested,
      std::string { "nested_disconnections" },
      &fsim::semantic::vhdl::GenerateRegion::label);
  assert(nested_disconnections != generated_disconnections->nested.end());
  assert(
      nested_disconnections->disconnection_specifications.size() == 1
      && nested_disconnections->disconnection_specifications.front()
             .selection
          == fsim::semantic::vhdl::DisconnectionSelection::others);
  const auto alternative_disconnections = std::ranges::find_if(
      generated_disconnections->nested,
      [](const auto& region) {
        return region.disconnection_specifications.size() == 1
            && region.disconnection_specifications.front().selection
                == fsim::semantic::vhdl::DisconnectionSelection::
                    explicit_names;
      });
  assert(alternative_disconnections
      != generated_disconnections->nested.end());
  assert(
      alternative_disconnections->disconnection_specifications.front()
          .signals.front().canonical == "alternative_guarded");

  fsim::diagnostic::Engine codec_diagnostics;
  const auto encoded = fsim::app::serialize_vhdl_hir_state(
      checked->vhdl_hir, checked->semantics, codec_diagnostics);
  assert(encoded && !codec_diagnostics.has_error());
  auto restored = fsim::app::deserialize_vhdl_hir_state(
      *encoded, "vhdl-disconnection-hir.bin", checked->semantics,
      codec_diagnostics);
  assert(restored && !codec_diagnostics.has_error());
  const auto restored_architecture = std::ranges::find_if(
      restored->units(), [](const auto& unit) {
        return unit.kind
                   == fsim::semantic::vhdl::UnitKind::architecture
            && unit.name == "rtl"
            && unit.primary_name == "vhdl_projected";
      });
  assert(restored_architecture != restored->units().end());
  assert(
      restored_architecture->disconnection_specifications.size() == 3
      && restored_architecture->disconnection_specifications.front()
             .delay.primary.expression
          == named_disconnection.delay.primary.expression);
  assert(fsim::app::serialize_vhdl_hir_state(
             *restored, checked->semantics, codec_diagnostics)
      == encoded);
  auto invalid = *restored;
  auto invalid_architecture = std::ranges::find_if(
      invalid.mutable_units(), [](const auto& unit) {
        return unit.kind
                   == fsim::semantic::vhdl::UnitKind::architecture
            && unit.name == "rtl"
            && unit.primary_name == "vhdl_projected";
      });
  assert(invalid_architecture != invalid.mutable_units().end());
  invalid_architecture->disconnection_specifications.front().selection
      = static_cast<fsim::semantic::vhdl::DisconnectionSelection>(255);
  fsim::diagnostic::Engine invalid_codec_diagnostics;
  assert(!fsim::app::serialize_vhdl_hir_state(
      invalid, checked->semantics, invalid_codec_diagnostics));
  assert(invalid_codec_diagnostics.has_error());

  const auto reject_top_level_hir_corruption = [&](auto mutate) {
    auto corrupted = *restored;
    mutate(corrupted);
    fsim::diagnostic::Engine corruption_diagnostics;
    assert(!fsim::app::serialize_vhdl_hir_state(
        corrupted, checked->semantics, corruption_diagnostics));
    assert(corruption_diagnostics.has_error());
  };
  auto instance_semantics = checked->semantics;
  auto instance_hir = *restored;
  const auto instance_id = instance_semantics.add_instance(
      architecture->scope, "validation_instance", "vhdl_projected",
      architecture->source, architecture->origin);
  fsim::semantic::vhdl::Instance validation_instance;
  validation_instance.id = instance_id;
  validation_instance.scope = architecture->scope;
  validation_instance.target.spelling = "vhdl_projected";
  validation_instance.target.canonical = "vhdl_projected";
  validation_instance.name = "validation_instance";
  validation_instance.source = architecture->source;
  validation_instance.origin = architecture->origin;
  instance_hir.mutable_instances().push_back(validation_instance);
  fsim::diagnostic::Engine instance_diagnostics;
  assert(fsim::app::serialize_vhdl_hir_state(
      instance_hir, instance_semantics, instance_diagnostics));
  assert(!instance_diagnostics.has_error());
  instance_hir.mutable_instances().push_back(validation_instance);
  fsim::diagnostic::Engine duplicate_instance_diagnostics;
  assert(!fsim::app::serialize_vhdl_hir_state(
      instance_hir, instance_semantics, duplicate_instance_diagnostics));
  assert(duplicate_instance_diagnostics.has_error());
  assert(!restored->overload_sets().empty());
  reject_top_level_hir_corruption([](auto& hir) {
    hir.mutable_overload_sets().push_back(hir.overload_sets().front());
  });
  reject_top_level_hir_corruption([](auto& hir) {
    hir.mutable_overload_sets().front().canonical_name.clear();
  });
  reject_top_level_hir_corruption([](auto& hir) {
    auto& overload = hir.mutable_overload_sets().front();
    overload.declarations.push_back(overload.declarations.front());
  });
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
  // check_project() returned after destroying compile-local parser storage.
  assert(waveform->id == retained_statement);
  assert(waveform->waveform.front().value == retained_expression);
}

void verify_wide_pending_projected_cancellation(
    const std::filesystem::path& directory) {
  for (const auto width : {65U, 129U}) {
    const auto case_directory = directory
        / ("wide-projected-cancellation-" + std::to_string(width));
    std::filesystem::create_directories(case_directory);
    const auto source = case_directory / "wide_projected_cancellation.vhd";
    {
      std::ofstream output(source, std::ios::binary);
      output << R"(
entity projected_cancel is end entity;
architecture rtl of projected_cancel is
  signal wide : bit_vector()" << (width - 1U) << R"( downto 0)
      := (others => '0');
  signal transaction_before : bit := '0';
  signal transaction_after : bit := '0';
  signal transaction_after_future : bit := '0';
begin
  owner: process
  begin
    wide <= (others => '1') after 10 ps;
    wait for 2 ps;
    wide <= (others => '0');
    wait for 10 ps;
    wait;
  end process;
  probe: process
  begin
    wait for 1 ps;
    transaction_before <= wide'transaction;
    wait for 2 ps;
    transaction_after <= wide'transaction;
    wait for 8 ps;
    transaction_after_future <= wide'transaction;
    wait;
  end process;
end architecture;
)";
      assert(output.good());
    }

    struct CancellationCapture {
      fsim::runtime::RunResult result;
      std::vector<Change> changes;
      std::string final_value;
      std::array<std::string, 3> transaction_samples;
      std::size_t compiled_processes { };
    };
    const auto run = [&](const fsim::project::Optimization optimization,
                         const fsim::app::SimulationEngine engine) {
      auto config = make_config(case_directory, source, optimization);
      config.project.top = "vhdl:work.projected_cancel(rtl)";
      config.build.cache_path = case_directory
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
      bool has_wide_projected_pair = false;
      for (const auto& process : project->design.processes()) {
        for (const auto& operation : process.operations) {
          const auto* const delayed
              = fsim::runtime::simir::operation_get_if<
                  fsim::runtime::simir::WriteProjected>(&operation);
          if (delayed == nullptr || delayed->delay != 10U
              || delayed->mode
                  != fsim::runtime::simir::ProjectedDelayMode::inertial
              || project->design.signals().at(delayed->signal).width
                  != width) {
            continue;
          }
          has_wide_projected_pair |= std::ranges::any_of(
              process.operations, [&](const auto& next_operation) {
                const auto* const immediate
                    = fsim::runtime::simir::operation_get_if<
                        fsim::runtime::simir::WriteProjected>(
                        &next_operation);
                return immediate != nullptr
                    && immediate->signal == delayed->signal
                    && immediate->delay == 0U
                    && immediate->mode
                        == fsim::runtime::simir::ProjectedDelayMode::inertial;
              });
        }
      }
      assert(has_wide_projected_pair);
      fsim::app::Simulation simulation {
          std::move(*project), config.run.max_deltas, engine };
      const auto wide = simulation.find_signal("projected_cancel.wide");
      assert(wide);
      std::array<fsim::runtime::simir::SignalId, 3> transaction_signals{};
      for (std::size_t index = 0; index < transaction_signals.size();
           ++index) {
        const auto name = std::array<std::string_view, 3> {
            "projected_cancel.transaction_before",
            "projected_cancel.transaction_after",
            "projected_cancel.transaction_after_future"}[index];
        const auto signal = simulation.find_signal(name);
        assert(signal);
        transaction_signals[index] = *signal;
      }
      CancellationCapture capture;
      capture.compiled_processes = simulation.compiled_process_count();
      simulation.set_signal_change_hook(
          [&](const fsim::runtime::simir::SignalId signal,
              const fsim::runtime::PackedLogic4& value,
              const fsim::runtime::SimulationTick time,
              const std::uint64_t delta) {
            if (signal == *wide) {
              capture.changes.push_back({
                  "projected_cancel.wide", value.to_msb_string(),
                  time, delta });
            }
          });
      const auto pending = simulation.run(3U);
      assert(pending.status == fsim::runtime::RunStatus::time_limit
          && simulation.has_pending());
      capture.result = simulation.run();
      capture.final_value = simulation.read_signal(*wide).to_msb_string();
      for (std::size_t index = 0; index < transaction_signals.size();
           ++index) {
        capture.transaction_samples[index]
            = simulation.read_signal(transaction_signals[index])
                  .to_msb_string();
      }
      return capture;
    };

    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2}) {
      const auto reference = run(
          optimization, fsim::app::SimulationEngine::interpreter);
      const auto compiled = run(
          optimization, fsim::app::SimulationEngine::compiled);
      const auto expected = std::string(width, '0');
      assert(reference.result.status == fsim::runtime::RunStatus::completed
          && reference.result.time == 12U
          && reference.changes.empty()
          && reference.final_value == expected
          && reference.transaction_samples[0]
              != reference.transaction_samples[1]
          && reference.transaction_samples[1]
              == reference.transaction_samples[2]);
      assert(compiled.result.status == reference.result.status
          && compiled.result.time == reference.result.time
          && compiled.changes == reference.changes
          && compiled.final_value == reference.final_value
          && compiled.transaction_samples
              == reference.transaction_samples);
#if defined(FSIM_HAS_LLVM)
      assert(compiled.compiled_processes > 0U);
#else
      assert(compiled.compiled_processes == 0U);
#endif
    }
  }
}

bool same_generic_receipt(
    const fsim::runtime::SchedulerGenericKeyReceipt& left,
    const fsim::runtime::SchedulerGenericKeyReceipt& right) noexcept {
  return left.valid == right.valid && left.time == right.time
      && left.delta == right.delta && left.phase == right.phase
      && left.stable_order == right.stable_order
      && left.sequence == right.sequence && left.payload == right.payload;
}

ForeignCutCapture run_generic_projected_foreign_cut(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization,
    const fsim::app::SimulationEngine engine,
    const bool force_before_suffix) {
  const auto engine_directory
      = engine == fsim::app::SimulationEngine::compiled
      ? "compiled"
      : "reference";
  const auto optimization_directory
      = optimization == fsim::project::Optimization::o0 ? "o0" : "o2";
  const auto case_directory = directory
      / (force_before_suffix ? "foreign-cut-force" : "foreign-cut-native")
      / engine_directory / optimization_directory;
  std::filesystem::create_directories(case_directory);
  const auto source = directory / "generic-cycle-ticket"
      / "generic_cycle_ticket.vhd";
  auto config = make_config(case_directory, source, optimization);
  config.project.name = "vhdl-cycle-ticket-foreign-cut";
  config.project.top = "vhdl:work.vhdl_cycle_ticket(rtl)";
  config.project.time_resolution = "1ps";
  config.build.cache_path = case_directory / "foreign-cut-cache";
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
      std::move(*project), config.run.max_deltas, engine,
      fsim::app::SystemVerilogVpiRuntimeUpdates::omitted };
  if (engine == fsim::app::SimulationEngine::compiled) {
    simulation.await_all_native_compilation();
  }
  const auto source_signal
      = simulation.find_signal("vhdl_cycle_ticket.source");
  const auto root_signal = simulation.find_signal("vhdl_cycle_ticket.root");
  const auto middle_signal
      = simulation.find_signal("vhdl_cycle_ticket.middle");
  const auto sink_signal = simulation.find_signal("vhdl_cycle_ticket.sink");
  assert(source_signal && root_signal && middle_signal && sink_signal);
  const std::array outputs { *root_signal, *middle_signal, *sink_signal };
  const std::array all_signals {
      *source_signal, *root_signal, *middle_signal, *sink_signal };

  simulation.start();
  const auto startup = simulation.run(0U);
  assert(startup.status == fsim::runtime::RunStatus::time_limit
      && startup.time == 0U);

  ForeignCutCapture capture;
  capture.force_control = force_before_suffix;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.writer_processes
      = NativeAccess::writer_processes(simulation, outputs);
  constexpr auto no_process = std::numeric_limits<ProcessId>::max();
  assert(std::ranges::none_of(capture.writer_processes,
      [](const ProcessId process) { return process == no_process; }));
  const auto root_process = capture.writer_processes[0U];
  const auto middle_process = capture.writer_processes[1U];
  assert(root_process < middle_process
      && middle_process < capture.writer_processes[2U]);
  if (engine == fsim::app::SimulationEngine::compiled) {
    capture.component_members
        = NativeAccess::vhdl_projected_component_member_count(
            simulation, outputs);
    assert(capture.component_members == outputs.size());
  }
  const auto injection_hook = simulation.add_safe_point_hook(
      [&simulation, &capture, source = *source_signal,
          root = *root_signal, root_process, middle_process](
          fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::SchedulerPhase) {
        if (capture.foreign_scheduled || scheduler.now() != 1U
            || scheduler.delta() == std::numeric_limits<std::uint64_t>::max()
            || !NativeAccess::process_queued(simulation, root_process)
            || !NativeAccess::process_queued(simulation, middle_process)
            || !NativeAccess::process_waiting_on_static(
                simulation, root_process)
            || !NativeAccess::process_waiting_on_static(
                simulation, middle_process)) {
          return;
        }
        const auto expected_delta = scheduler.delta() + 1U;
        if (capture.compiled_processes != 0U) {
          capture.root_receipt_before
              = NativeAccess::vhdl_receipt(simulation, root_process);
          capture.middle_receipt_before
              = NativeAccess::vhdl_receipt(simulation, middle_process);
          if (!capture.root_receipt_before.valid
              || !capture.middle_receipt_before.valid
              || capture.root_receipt_before.time != scheduler.now()
              || capture.middle_receipt_before.time != scheduler.now()
              || capture.root_receipt_before.delta != expected_delta
              || capture.middle_receipt_before.delta != expected_delta
              || capture.root_receipt_before.phase
                  != fsim::runtime::SchedulerPhase::active
              || capture.middle_receipt_before.phase
                  != fsim::runtime::SchedulerPhase::active
              || capture.root_receipt_before.stable_order != root_process
              || capture.middle_receipt_before.stable_order != middle_process) {
            return;
          }
        }
        capture.foreign_key.emplace(
            NativeAccess::reserve_order_key(simulation, root_process));
        if (capture.foreign_key->order != root_process
            || capture.foreign_key->order >= middle_process
            || (capture.compiled_processes != 0U
                && capture.foreign_key->sequence
                    <= capture.root_receipt_before.sequence)) {
          throw std::logic_error {
              "foreign key does not sort between the authentic member keys"
          };
        }
        capture.foreign_scheduled = true;
        scheduler.schedule_reserved_next_delta(
            fsim::runtime::SchedulerPhase::active, *capture.foreign_key,
            [&simulation, &capture, source, root, root_process,
                middle_process](fsim::runtime::Scheduler& active_scheduler) {
              capture.foreign_called = true;
              capture.foreign_time = active_scheduler.now();
              capture.foreign_delta = active_scheduler.delta();
              capture.next_key_after_foreign
                  = active_scheduler.next_current_order_key();
              if (capture.compiled_processes != 0U
                  && capture.foreign_delta
                      == std::numeric_limits<std::uint64_t>::max()) {
                throw std::logic_error {
                    "foreign callback cannot authenticate its next delta"
                };
              }

              if (capture.compiled_processes != 0U) {
                capture.root_receipt_at_foreign
                    = NativeAccess::vhdl_receipt(simulation, root_process);
                if (capture.root_receipt_at_foreign.valid) {
                  throw std::logic_error {
                      "the original root key remains queued at the foreign cut"
                  };
                }
              }
              capture.source_before_deposit
                  = NativeAccess::signal_metadata(simulation, source);
              simulation.deposit_signal(source,
                  fsim::runtime::PackedLogic4 {
                      1U, fsim::runtime::Logic4::zero });
              capture.source_after_deposit
                  = NativeAccess::signal_metadata(simulation, source);
              if (capture.compiled_processes != 0U) {
                capture.root_future_receipt
                    = NativeAccess::vhdl_receipt(simulation, root_process);
              }
              if (capture.force_control) {
                capture.root_before_force
                    = NativeAccess::signal_metadata(simulation, root);
                simulation.force_signal(root,
                    fsim::runtime::PackedLogic4 {
                        1U, fsim::runtime::Logic4::zero });
                capture.root_after_force
                    = NativeAccess::signal_metadata(simulation, root);
              }
              if (capture.compiled_processes != 0U
                  && (!capture.root_future_receipt.valid
                      || capture.root_future_receipt.time
                          != active_scheduler.now()
                      || capture.root_future_receipt.delta
                          != active_scheduler.delta() + 1U
                      || capture.root_future_receipt.phase
                          != fsim::runtime::SchedulerPhase::active
                      || capture.root_future_receipt.stable_order
                          != root_process)) {
                throw std::logic_error {
                    "foreign source deposit did not issue the root receipt"
                };
              }
              if (!capture.next_key_after_foreign
                  || capture.next_key_after_foreign->order != middle_process
                  || (capture.compiled_processes != 0U
                      && capture.next_key_after_foreign->sequence
                          != capture.middle_receipt_before.sequence)) {
                throw std::logic_error {
                    "foreign work did not sort before the original middle key"
                };
              }
              simulation.request_stop();
            });
      });

  capture.first_pause = simulation.run(1U);
  simulation.remove_safe_point_hook(injection_hook);
  assert(capture.first_pause.status == fsim::runtime::RunStatus::stopped
      && capture.first_pause.time == 1U
      && capture.foreign_scheduled && capture.foreign_called);
  capture.stats_at_foreign_pause
      = NativeAccess::generic_ticket_stats(simulation);
  if (capture.compiled_processes != 0U) {
    capture.root_receipt_at_pause
        = NativeAccess::vhdl_receipt(simulation, root_process);
    capture.middle_receipt_at_pause
        = NativeAccess::vhdl_receipt(simulation, middle_process);
    assert(capture.middle_receipt_before.valid
        && same_generic_receipt(capture.middle_receipt_at_pause,
            capture.middle_receipt_before)
        && capture.root_future_receipt.valid
        && NativeAccess::process_queued(simulation, middle_process));
    if (!force_before_suffix) {
      assert(same_generic_receipt(capture.root_receipt_at_pause,
                 capture.root_future_receipt)
          && NativeAccess::process_queued(simulation, root_process));
    }
  }

  const auto suffix_hook = simulation.add_safe_point_hook(
      [&simulation, &capture, root_process, middle_process](
          fsim::runtime::Scheduler& scheduler,
          const fsim::runtime::SchedulerPhase phase) {
        if (capture.suffix_safe_point
            || phase != fsim::runtime::SchedulerPhase::active
            || scheduler.now() != capture.foreign_time
            || scheduler.delta() != capture.foreign_delta
            || NativeAccess::process_queued(simulation, middle_process)
            || (!capture.force_control && capture.compiled_processes != 0U
                && !NativeAccess::process_queued(simulation, root_process))) {
          return;
        }
        capture.suffix_safe_point = true;
        capture.middle_receipt_after_suffix
            = NativeAccess::vhdl_receipt(simulation, middle_process);
        capture.root_receipt_after_suffix
            = NativeAccess::vhdl_receipt(simulation, root_process);
        capture.stats_after_suffix
            = NativeAccess::generic_ticket_stats(simulation);
        scheduler.request_stop();
      });
  simulation.clear_stop();
  capture.suffix_pause = simulation.run();
  simulation.remove_safe_point_hook(suffix_hook);
  assert(capture.suffix_pause.status == fsim::runtime::RunStatus::stopped
      && capture.suffix_safe_point);
  if (capture.compiled_processes != 0U) {
    assert(!capture.middle_receipt_after_suffix.valid);
    if (!force_before_suffix) {
      assert(NativeAccess::process_queued(simulation, root_process));
    }
  }

  simulation.clear_stop();
  capture.result = simulation.run();
  assert(capture.result.status == fsim::runtime::RunStatus::completed);
  for (std::size_t index = 0U; index < all_signals.size(); ++index) {
    capture.final_values[index]
        = simulation.read_signal(all_signals[index]).to_msb_string();
    capture.metadata[index]
        = NativeAccess::signal_metadata(simulation, all_signals[index]);
  }
  return capture;
}

void verify_generic_projected_foreign_cut(
    const std::filesystem::path& directory) {
  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2 }) {
    const auto reference_native = run_generic_projected_foreign_cut(
        directory, optimization, fsim::app::SimulationEngine::interpreter,
        false);
    const auto compiled_native = run_generic_projected_foreign_cut(
        directory, optimization, fsim::app::SimulationEngine::compiled,
        false);
    assert(reference_native.first_pause.status
            == fsim::runtime::RunStatus::stopped
        && compiled_native.first_pause.status
            == fsim::runtime::RunStatus::stopped
        && reference_native.suffix_pause.status
            == fsim::runtime::RunStatus::stopped
        && compiled_native.suffix_pause.status
            == fsim::runtime::RunStatus::stopped
        && reference_native.first_pause.time
            == compiled_native.first_pause.time
        && reference_native.first_pause.delta
            == compiled_native.first_pause.delta
        && reference_native.suffix_pause.time
            == compiled_native.suffix_pause.time
        && reference_native.suffix_pause.delta
            == compiled_native.suffix_pause.delta
        && reference_native.result.time == compiled_native.result.time
        && reference_native.result.delta == compiled_native.result.delta
        && reference_native.final_values == compiled_native.final_values
        && reference_native.source_before_deposit
            == compiled_native.source_before_deposit
        && reference_native.source_after_deposit
            == compiled_native.source_after_deposit
        && reference_native.final_values
            == (std::array<std::string, 4U> { "0", "0", "0", "0" })
        && reference_native.metadata == compiled_native.metadata);
    assert(reference_native.foreign_time == 1U
        && compiled_native.foreign_time == reference_native.foreign_time
        && compiled_native.foreign_delta == reference_native.foreign_delta
        && reference_native.next_key_after_foreign
        && compiled_native.next_key_after_foreign
        && reference_native.next_key_after_foreign->order
            == reference_native.writer_processes[1U]
        && compiled_native.next_key_after_foreign->order
            == compiled_native.writer_processes[1U]);
#if defined(FSIM_HAS_LLVM)
    if (compiled_native.compiled_processes != 0U) {
      assert(compiled_native.component_members == 3U
          && compiled_native.foreign_key
          && compiled_native.root_receipt_before.valid
          && compiled_native.middle_receipt_before.valid
          && compiled_native.root_receipt_before.sequence
              < compiled_native.foreign_key->sequence
          && !compiled_native.root_receipt_at_foreign.valid
          && compiled_native.root_future_receipt.valid
          && compiled_native.root_receipt_at_pause.valid
          && same_generic_receipt(compiled_native.root_future_receipt,
              compiled_native.root_receipt_at_pause)
          && same_generic_receipt(compiled_native.root_future_receipt,
              compiled_native.root_receipt_after_suffix)
          && same_generic_receipt(compiled_native.middle_receipt_before,
              compiled_native.middle_receipt_at_pause)
          && compiled_native.next_key_after_foreign->sequence
              == compiled_native.middle_receipt_before.sequence
          && compiled_native.stats_after_suffix.direct_dispatches
              > compiled_native.stats_at_foreign_pause.direct_dispatches
          && compiled_native.stats_after_suffix
                  .generic_readiness_ticket_fallback_members
              == compiled_native.stats_at_foreign_pause
                  .generic_readiness_ticket_fallback_members);
    }
#endif

    const auto reference_force = run_generic_projected_foreign_cut(
        directory, optimization, fsim::app::SimulationEngine::interpreter,
        true);
    const auto compiled_force = run_generic_projected_foreign_cut(
        directory, optimization, fsim::app::SimulationEngine::compiled,
        true);
    assert(reference_force.first_pause.status
            == fsim::runtime::RunStatus::stopped
        && compiled_force.first_pause.status
            == fsim::runtime::RunStatus::stopped
        && reference_force.suffix_pause.status
            == fsim::runtime::RunStatus::stopped
        && compiled_force.suffix_pause.status
            == fsim::runtime::RunStatus::stopped
        && reference_force.result.time == compiled_force.result.time
        && reference_force.result.delta == compiled_force.result.delta
        && reference_force.final_values == compiled_force.final_values
        && reference_force.final_values
            == (std::array<std::string, 4U> { "0", "0", "0", "0" })
        && reference_force.metadata == compiled_force.metadata
        && reference_force.source_before_deposit
            == compiled_force.source_before_deposit
        && reference_force.source_after_deposit
            == compiled_force.source_after_deposit
        && reference_force.root_before_force == compiled_force.root_before_force
        && reference_force.root_after_force == compiled_force.root_after_force);
#if defined(FSIM_HAS_LLVM)
    if (compiled_force.compiled_processes != 0U) {
      assert(compiled_force.middle_receipt_before.valid
          && compiled_force.foreign_key
          && same_generic_receipt(compiled_force.middle_receipt_before,
              compiled_force.middle_receipt_at_pause)
          && compiled_force.next_key_after_foreign
          && compiled_force.next_key_after_foreign->sequence
              == compiled_force.middle_receipt_before.sequence
          && compiled_force.root_future_receipt.valid
          && compiled_force.stats_after_suffix
                  .generic_readiness_ticket_fallback_members
              > compiled_force.stats_at_foreign_pause
                  .generic_readiness_ticket_fallback_members);
    }
#endif
  }
}

void verify_generic_projected_readiness_compaction_width(
    const std::filesystem::path& directory, const std::size_t width) {
  const auto case_directory = width == 1U
      ? directory / "generic-cycle-ticket"
      : directory / ("generic-cycle-ticket-width-" + std::to_string(width));
  std::filesystem::create_directories(case_directory);
  const auto source = case_directory / "generic_cycle_ticket.vhd";
  const auto design_name = width == 1U
      ? std::string { "vhdl_cycle_ticket" }
      : "vhdl_cycle_ticket_" + std::to_string(width);
  const auto value_type = width == 1U
      ? std::string { "bit" }
      : "bit_vector(" + std::to_string(width - 1U) + " downto 0)";
  const auto zero_value = width == 1U
      ? std::string { "'0'" }
      : std::string { "(others => '0')" };
  const auto one_value = width == 1U
      ? std::string { "'1'" }
      : std::string { "(others => '1')" };
  {
    std::ofstream output(source, std::ios::binary);
    output << "entity " << design_name << " is end entity;\n"
           << "architecture rtl of " << design_name << " is\n"
           << "  signal source : " << value_type << " := "
           << zero_value << ";\n"
           << "  signal root : " << value_type << " := "
           << zero_value << ";\n"
           << "  signal middle : " << value_type << " := "
           << zero_value << ";\n"
           << "  signal sink : " << value_type << " := "
           << zero_value << ";\n"
           << "begin\n"
           << "  root_driver: root <= source;\n"
           << "  middle_driver: middle <= root xor source;\n"
           << "  sink_driver: sink <= middle;\n"
           << "  stimulus: process begin\n"
           << "    wait for 1 ps;\n"
           << "    source <= " << one_value << ";\n"
           << "    wait;\n"
           << "  end process;\n"
           << "end architecture;\n";
    assert(output.good());
  }

  const auto run = [&](const fsim::project::Optimization optimization,
                       const fsim::app::SimulationEngine engine,
                       const fsim::app::SystemVerilogVpiRuntimeUpdates
                           vpi_runtime_updates) {
    auto config = make_config(case_directory, source, optimization);
    config.project.name = "vhdl-cycle-ticket-" + std::to_string(width);
    config.project.top = "vhdl:work." + design_name + "(rtl)";
    config.project.time_resolution = "1ps";
    config.build.cache_path = case_directory
        / (optimization == fsim::project::Optimization::o0
                ? "cycle-cache-o0"
                : "cycle-cache-o2");
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(project);

    std::optional<ScopedEnvironment> region_kernel;
    std::optional<ScopedEnvironment> wave_profile;
    if (engine == fsim::app::SimulationEngine::compiled
        && vpi_runtime_updates
            == fsim::app::SystemVerilogVpiRuntimeUpdates::omitted) {
      region_kernel.emplace("FSIM_ENABLE_SV_REGION_KERNEL", "1");
      wave_profile.emplace("FSIM_PROFILE_SV_WAVES", "1");
    }
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine,
        vpi_runtime_updates };
    if (engine == fsim::app::SimulationEngine::compiled) {
      simulation.await_all_native_compilation();
    }
    const auto source_signal
        = simulation.find_signal(design_name + ".source");
    const auto root_signal
        = simulation.find_signal(design_name + ".root");
    const auto middle_signal
        = simulation.find_signal(design_name + ".middle");
    const auto sink_signal
        = simulation.find_signal(design_name + ".sink");
    assert(source_signal && root_signal && middle_signal && sink_signal);
    const std::array outputs {
        *root_signal, *middle_signal, *sink_signal };

    simulation.start();
    const auto startup = simulation.run(0U);
    assert(startup.status == fsim::runtime::RunStatus::time_limit
        && startup.time == 0U);

    GenericCycleCapture capture;
    capture.compiled_processes = simulation.compiled_process_count();
    capture.ticket_stats_before
        = NativeAccess::generic_ticket_stats(simulation);
    capture.projected_stats_before
        = NativeAccess::projected_region_stats(simulation);
    if (engine == fsim::app::SimulationEngine::compiled) {
      capture.component_members
          = NativeAccess::vhdl_projected_component_member_count(
              simulation, outputs);
    }

    capture.result = simulation.run();
    assert(capture.result.status == fsim::runtime::RunStatus::completed);
    capture.ticket_stats_after
        = NativeAccess::generic_ticket_stats(simulation);
    capture.projected_stats_after
        = NativeAccess::projected_region_stats(simulation);
    for (std::size_t index = 0U; index < outputs.size(); ++index) {
      capture.metadata[index]
          = NativeAccess::signal_metadata(simulation, outputs[index]);
      capture.final_values[index]
          = simulation.read_signal(outputs[index]).to_msb_string();
    }
    return capture;
  };

  for (const auto optimization : {
           fsim::project::Optimization::o0,
           fsim::project::Optimization::o2 }) {
    const auto reference = run(
        optimization, fsim::app::SimulationEngine::interpreter,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    const auto compiled = run(
        optimization, fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::omitted);
    assert(reference.result.status == compiled.result.status
        && reference.result.time == compiled.result.time
        && reference.result.delta == compiled.result.delta
        && reference.final_values == compiled.final_values
        && reference.metadata == compiled.metadata);
    assert(reference.final_values
        == (std::array<std::string, 3U> {
            std::string(width, '1'), std::string(width, '0'),
            std::string(width, '0') }));
    assert(std::ranges::all_of(compiled.metadata,
        [](const SignalMetadata& metadata) {
          return metadata.event.has_value()
              && metadata.transaction.has_value();
        }));

    // The ordinary application constructor keeps the live VPI mirror active.
    // That is a real observer, so retain its checked-path behavior as a control
    // instead of weakening RegionGraph's observer boundary.
    const auto vpi_enabled = run(
        optimization, fsim::app::SimulationEngine::compiled,
        fsim::app::SystemVerilogVpiRuntimeUpdates::enabled);
    assert(vpi_enabled.result.status == reference.result.status
        && vpi_enabled.result.time == reference.result.time
        && vpi_enabled.result.delta == reference.result.delta
        && vpi_enabled.final_values == reference.final_values
        && vpi_enabled.metadata == reference.metadata);
#if defined(FSIM_HAS_LLVM)
    assert(compiled.compiled_processes >= 3U
        && compiled.component_members == 3U
        && vpi_enabled.compiled_processes >= 3U
        && vpi_enabled.component_members == 0U);
    const auto& before = compiled.ticket_stats_before;
    const auto& after = compiled.ticket_stats_after;
    assert(after.generic_readiness_ticket_queue_insertions
                >= before.generic_readiness_ticket_queue_insertions + 1U
        && after.generic_readiness_ticket_members
            >= before.generic_readiness_ticket_members + 2U
        && after.generic_readiness_ticket_members_elided
            >= before.generic_readiness_ticket_members_elided + 1U
        && after.direct_dispatches >= before.direct_dispatches + 1U
        && after.direct_members >= before.direct_members + 2U
        && after.generic_readiness_ticket_fallback_members
            == before.generic_readiness_ticket_fallback_members);
    const auto& projected_before = compiled.projected_stats_before;
    const auto& projected_after = compiled.projected_stats_after;
    const auto projected_attempts
        = projected_after.attempts - projected_before.attempts;
    const auto projected_backend_runs
        = projected_after.backend_runs - projected_before.backend_runs;
    const auto projected_completions
        = projected_after.completions - projected_before.completions;
    assert(projected_attempts > 0U
        && projected_backend_runs == projected_attempts
        && projected_completions == projected_attempts
        && projected_after.members >= projected_before.members + 2U
        && projected_after.publications
            >= projected_before.publications + 2U
        && projected_after.declines == projected_before.declines
        && projected_after.failures == projected_before.failures);
    const auto& vpi_before = vpi_enabled.ticket_stats_before;
    const auto& vpi_after = vpi_enabled.ticket_stats_after;
    assert(vpi_after.generic_readiness_ticket_queue_insertions
                == vpi_before.generic_readiness_ticket_queue_insertions
        && vpi_after.generic_readiness_ticket_members
            == vpi_before.generic_readiness_ticket_members
        && vpi_after.generic_readiness_ticket_members_elided
            == vpi_before.generic_readiness_ticket_members_elided
        && vpi_after.generic_readiness_ticket_fallback_members
            == vpi_before.generic_readiness_ticket_fallback_members);
#else
    assert(compiled.compiled_processes == 0U);
#endif
  }
}

void verify_generic_projected_readiness_compaction(
    const std::filesystem::path& directory) {
  for (const auto width : { 1U, 65U, 129U }) {
    verify_generic_projected_readiness_compaction_width(directory, width);
  }
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
  disconnect default_drive, explicit_drive : std_logic
    after projected_delay;
  disconnect all : boolean after 3 ps;
  disconnect others : std_logic_vector after 4 ps;
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

  retained_disconnections: if true generate
    signal generated_guarded : std_logic;
    disconnect all : std_logic after projected_delay;
  begin
    nested_disconnections: block is
      signal nested_guarded : std_logic;
      disconnect others : std_logic after 5 ps;
    begin
    end block nested_disconnections;
  else generate
    signal alternative_guarded : std_logic;
    disconnect alternative_guarded : std_logic after 6 ps;
  begin
  end generate retained_disconnections;

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
  verify_wide_pending_projected_cancellation(directory.path);
  verify_generic_projected_readiness_compaction(directory.path);
  verify_generic_projected_foreign_cut(directory.path);

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
