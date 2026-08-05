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
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
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

struct Change {
  std::string value;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const Change&, const Change&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<Change> changes;
  std::string final_value;
  std::vector<std::pair<std::string, std::string>> drivers;
  std::string vcd;
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics cache;
};

struct StrengthCapture {
  std::array<std::string, 17> values;
  std::vector<Change> switch_changes;
  std::vector<Change> conditional_changes;
  std::vector<Change> decay_changes;
  std::vector<Change> renewed_changes;
  std::string vcd;
  std::string debugger;

  friend bool operator==(
      const StrengthCapture&, const StrengthCapture&) = default;
};

bool writes_signal(
    const fsim::runtime::simir::Process& process,
    const fsim::runtime::simir::SignalId signal) {
  return std::ranges::any_of(
      process.operations,
      [signal](const fsim::runtime::simir::Operation& operation) {
        return fsim::runtime::simir::visit_operation(
            [signal](const auto& op) {
              using T = std::decay_t<decltype(op)>;
              if constexpr (
                  std::is_same_v<T, fsim::runtime::simir::WriteBlocking>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteUpdate>
                  || std::is_same_v<T, fsim::runtime::simir::WriteAfter>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteInertial>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteProjected>
                  || std::is_same_v<
                      T,
                      fsim::runtime::simir::WriteProjectedWaveform>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteBlockingSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteUpdateSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteAfterSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteInertialSlice>
                  || std::is_same_v<
                      T, fsim::runtime::simir::WriteProjectedSlice>
                  || std::is_same_v<
                      T,
                      fsim::runtime::simir::
                          WriteProjectedWaveformSlice>) {
                return op.signal == signal;
              } else {
                return false;
              }
            },
            operation);
      });
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& sv_source,
    const std::filesystem::path& vhdl_source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "mixed-resolution";
  config.project.top = "sv:work.resolved_top";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sv_sources;
  sv_sources.language = fsim::project::Language::system_verilog;
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

  config.bindings = {
      {
          "resolved_top.u_vhdl",
          "vhdl:work.vhdl_driver(rtl)",
          std::string{"std_logic"}},
      {
          "resolved_top.u_sv",
          "sv:work.sv_driver",
          std::string{"std_logic"}},
  };
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
  const auto shared =
      project->design.find_signal("resolved_top.shared");
  assert(shared);
  assert(
      project->design.signals().at(*shared).resolution
      == fsim::runtime::simir::ResolutionKind::std_logic);

  std::vector<std::pair<
      fsim::runtime::simir::ProcessId,
      std::string>>
      drivers;
  for (const auto& process : project->design.processes()) {
    if (writes_signal(process, *shared)) {
      drivers.emplace_back(process.id, process.name);
    }
  }
  assert(drivers.size() == 2);

  Capture capture;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.cache = simulation.native_cache_statistics();

  std::ostringstream vcd_output;
  fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
  const auto vcd_signal =
      vcd.declare_signal("resolved_top.shared", 1);
  vcd.begin(simulation.now());
  vcd.change(vcd_signal, simulation.read_signal(*shared));
  simulation.set_signal_change_hook(
      [&](const fsim::runtime::simir::SignalId signal,
          const fsim::runtime::PackedLogic4& value,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        if (signal != *shared) {
          return;
        }
        capture.changes.push_back(
            {value.to_msb_string(), time, delta});
        vcd.set_time(time);
        vcd.change(vcd_signal, value);
      });
  capture.result = simulation.run();
  capture.final_value =
      simulation.read_signal(*shared).to_msb_string();
  for (const auto& [process, name] : drivers) {
    capture.drivers.emplace_back(
        name,
        simulation.read_driver(process, *shared).to_msb_string());
  }
  vcd.flush();
  capture.vcd = vcd_output.str();
  return capture;
}

void verify_mode(
    const std::filesystem::path& directory,
    const std::filesystem::path& sv_source,
    const std::filesystem::path& vhdl_source,
    const fsim::project::Optimization optimization) {
  const auto config =
      make_config(directory, sv_source, vhdl_source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto cold =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 6000);
  if (reference.changes
      != std::vector<Change>{
          {"1", 3000, 0},
          {"Z", 5000, 0},
      }) {
    for (const auto& change : reference.changes) {
      std::cerr << "change " << change.value << " @ "
                << change.time << " delta " << change.delta
                << '\n';
    }
  }
  assert((
      reference.changes
      == std::vector<Change>{
          {"1", 3000, 0},
          {"Z", 5000, 0},
      }));
  assert(reference.final_value == "Z");
  std::vector<std::string> driver_values;
  for (const auto& [name, value] : reference.drivers) {
    (void)name;
    driver_values.push_back(value);
  }
  std::ranges::sort(driver_values);
  assert((
      driver_values == std::vector<std::string>{"Z", "Z"}));
  assert(reference.vcd.find("$timescale 1ps $end")
         != std::string::npos);
  assert(reference.vcd.find("#3000") != std::string::npos);
  assert(reference.vcd.find("#5000") != std::string::npos);

  for (const auto* actual : {&cold, &warm}) {
    assert(reference.result.status == actual->result.status);
    assert(reference.result.time == actual->result.time);
    assert(reference.result.delta == actual->result.delta);
    assert(reference.changes == actual->changes);
    assert(reference.final_value == actual->final_value);
    assert(reference.drivers == actual->drivers);
    assert(reference.vcd == actual->vcd);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes == 3);
  assert(cold.cache.hits == 0);
  assert(cold.cache.misses == 3);
  assert(cold.cache.stores == 3);
  assert(warm.compiled_processes == 3);
  assert(warm.cache.hits == 3);
  assert(warm.cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
}

void verify_verilog_strengths(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization) {
  const auto source = directory / "strength_top.v";
  {
    std::ofstream output(source, std::ios::binary);
    output << R"(`timescale 1ns/1ps
module strength_top;
  reg drive;
  reg enabled;
  reg tran_drive;
  reg switch_control;
  reg renew_drive;
  wire strength_conflict;
  tri0 implicit_pull;
  supply1 supply_high;
  wire explicit_pull;
  wire mos_output;
  wire resistive_conflict;
  wire tran_left;
  wire tran_right;
  wire rtran_left;
  wire rtran_right;
  wire weak_mos_source;
  wire weak_mos_output;
  wire conditional_left;
  wire conditional_right;
  wire chain_left, chain_middle, chain_right;
  trireg (large) retained_charge;
  trireg (small) #1 decayed_charge;
  trireg (large) charged_conflict;
  trireg (small) #0 zero_decay;
  trireg (medium) #2 renewed_charge;
  trireg (large) [1:0] packed_charge;

  assign (weak0, weak1) strength_conflict = 1'b0;
  assign (strong0, strong1) strength_conflict = drive;
  pullup pull_source(explicit_pull);
  nmos mos_device(mos_output, drive, enabled);
  assign (strong0, strong1) resistive_conflict = 1'b0;
  rnmos resistive_device(resistive_conflict, 1'b1, enabled);
  assign tran_left = tran_drive;
  tran connected(tran_left, tran_right);
  assign (weak0, weak1) rtran_left = 1'b1;
  assign (weak0, weak1) rtran_right = 1'b0;
  rtran resistive_connected(rtran_left, rtran_right);
  assign (weak0, weak1) weak_mos_source = 1'b1;
  assign (weak0, weak1) weak_mos_output = 1'b0;
  rnmos weak_resistive_device(
      weak_mos_output, weak_mos_source, 1'b1);
  assign conditional_left = 1'b1;
  tranif1 conditional_switch(
      conditional_left, conditional_right, switch_control);
  assign (strong0, strong1) chain_left = 1'b1;
  assign (weak0, weak1) chain_right = 1'b0;
  rtran chain_first(chain_left, chain_middle);
  rtran chain_second(chain_middle, chain_right);
  assign retained_charge = drive;
  assign decayed_charge = drive;
  assign charged_conflict = drive;
  assign (weak0, weak1) charged_conflict = 1'b0;
  assign zero_decay = drive;
  assign renewed_charge = renew_drive;
  assign packed_charge = {drive, drive};

  initial begin
    drive = 1'b1;
    enabled = 1'b1;
    tran_drive = 1'b1;
    switch_control = 1'b0;
    renew_drive = 1'b1;
    #1 begin
      drive = 1'bz;
      tran_drive = 1'bz;
      switch_control = 1'b1;
      renew_drive = 1'bz;
    end
    #1 begin
      enabled = 1'b0;
      switch_control = 1'b0;
      renew_drive = 1'b0;
    end
    #1 begin
      tran_drive = 1'b0;
      switch_control = 1'bx;
    end
    #1 $finish;
  end
endmodule
)";
    assert(output.good());
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "verilog-strengths";
  config.project.top = "verilog:work.strength_top";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path = directory
      / (optimization == fsim::project::Optimization::o0
             ? "strength-cache-o0" : "strength-cache-o2");
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::verilog;
  sources.standard = "2005";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  const auto execute = [&](const fsim::app::SimulationEngine engine) {
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(project);
    const auto topology = std::ranges::find_if(
        project->design.processes(), [](const auto& process) {
          return process.name.ends_with("conditional_switch$right");
        });
    assert(topology != project->design.processes().end()
           && topology->switch_bidirectional
           && topology->switch_source && topology->switch_target
           && topology->switch_control);
    const auto topology_process = topology->id;
    const auto runtime_bytes = fsim::app::serialize_runtime_state(
        project->design, diagnostics);
    assert(runtime_bytes);
    auto restored = fsim::app::deserialize_runtime_state(
        *runtime_bytes, "strength-runtime-state", diagnostics);
    assert(restored);
    const auto restored_topology = std::ranges::find_if(
        restored->processes(), [](const auto& process) {
          return process.name.ends_with("conditional_switch$right");
        });
    assert(restored_topology != restored->processes().end()
           && restored_topology->switch_bidirectional
           && restored_topology->switch_source
           && restored_topology->switch_target
           && restored_topology->switch_control);
    project->design = std::move(*restored);
    fsim::app::Simulation simulation{
        std::move(*project), config.run.max_deltas, engine};
    const auto read = [&](const std::string_view leaf) {
      const auto signal = simulation.find_signal(
          "strength_top." + std::string{leaf});
      assert(signal);
      return *signal;
    };
    const std::array signals{
        read("strength_conflict"), read("implicit_pull"),
        read("supply_high"), read("explicit_pull"),
        read("mos_output"), read("resistive_conflict"),
        read("tran_right"), read("retained_charge"),
        read("decayed_charge"), read("charged_conflict"),
        read("rtran_right"), read("weak_mos_output"),
        read("zero_decay"), read("renewed_charge"),
        read("packed_charge")};
    const auto conditional_signal = read("conditional_right");
    const auto control_signal = read("switch_control");
    const auto chain_signal = read("chain_right");
    assert(control_signal == *restored_topology->switch_control);
    assert(conditional_signal == *restored_topology->switch_target);
    StrengthCapture capture;
    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
    const auto vcd_signal = vcd.declare_signal(
        "strength_top.decayed_charge", 1);
    vcd.begin(simulation.now());
    vcd.change(vcd_signal, simulation.read_signal(signals[8]));
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
          if (signal == signals[6]) {
            capture.switch_changes.push_back(
                {value.to_msb_string(), time, delta});
          }
          if (signal == conditional_signal) {
            capture.conditional_changes.push_back(
                {value.to_msb_string(), time, delta});
          }
          if (signal == signals[8]) {
            capture.decay_changes.push_back(
                {value.to_msb_string(), time, delta});
            vcd.set_time(time);
            vcd.change(vcd_signal, value);
          }
          if (signal == signals[13]) {
            capture.renewed_changes.push_back(
                {value.to_msb_string(), time, delta});
          }
        });
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error};
    debugger.execute({"show", "decayed_charge"});
    assert(debugger_error.str().empty());
    capture.debugger = debugger_output.str();
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::stopped);
    assert(simulation.read_signal(control_signal).to_msb_string() == "X");
    bool topology_has_no_driver = false;
    try {
      (void)simulation.read_driver(topology_process, conditional_signal);
    } catch (const std::out_of_range&) {
      topology_has_no_driver = true;
    }
    assert(topology_has_no_driver);
    for (std::size_t index = 0; index < signals.size(); ++index) {
      capture.values[index] =
          simulation.read_signal(signals[index]).to_msb_string();
    }
    capture.values.back() =
        simulation.read_signal(chain_signal).to_msb_string();
    capture.values[capture.values.size() - 2] =
        simulation.read_signal(conditional_signal).to_msb_string();
    vcd.flush();
    capture.vcd = vcd_output.str();
    return capture;
  };

  const auto reference = execute(fsim::app::SimulationEngine::interpreter);
  const auto cold = execute(fsim::app::SimulationEngine::compiled);
  const auto warm = execute(fsim::app::SimulationEngine::compiled);
  const auto expected_strengths = std::array<std::string, 17>{
      "0", "0", "1", "1", "Z", "0", "0", "1", "Z", "1", "0", "0",
      "Z", "0", "11", "X", "X"};
  if (reference.values != expected_strengths) {
    for (const auto& value : reference.values) std::cerr << value << ' ';
    std::cerr << '\n';
  }
  assert(reference.values == expected_strengths);
  assert(cold == reference);
  assert(warm == reference);
  assert(!reference.decay_changes.empty());
  assert(std::ranges::any_of(
      reference.switch_changes, [](const Change& change) {
        return change.value == "Z" && change.time == 1000;
      }));
  assert(!reference.switch_changes.empty()
         && reference.switch_changes.back().value == "0"
         && reference.switch_changes.back().time == 3000);
  assert(std::ranges::any_of(
      reference.conditional_changes, [](const Change& change) {
        return change.value == "1" && change.time == 1000;
      }));
  assert(std::ranges::any_of(
      reference.conditional_changes, [](const Change& change) {
        return change.value == "Z" && change.time == 2000;
      }));
  assert(!reference.conditional_changes.empty()
         && reference.conditional_changes.back().value == "X"
         && reference.conditional_changes.back().time == 3000);
  assert(reference.decay_changes.back().value == "Z"
         && reference.decay_changes.back().time == 2000);
  assert(std::ranges::none_of(
      reference.renewed_changes, [](const Change& change) {
        return change.value == "Z" && change.time >= 2000;
      }));
  assert(reference.vcd.find("#2000") != std::string::npos);
  assert(reference.debugger.find("decayed_charge") != std::string::npos);
}

void verify_strength_artifacts_and_mapping(
    const std::filesystem::path& directory) {
  const auto source = directory / "strength-artifact.v";
  const auto object = directory / "strength-artifact.fsimobj";
  const auto design = directory / "strength-artifact.fsimdesign";
  const std::string source_text = R"(`timescale 1ns/1ns
module strength_artifact_top;
  reg drive;
  wire conflict;
  wire switch_source, switch_target;
  wire [1:0] vector_source, vector_target, vector_control;
  wire chain_left, chain_middle, chain_right;
  trireg (medium) #2 decayed;
  trireg (large) retained;
  assign (weak0, weak1) conflict = 1'b0;
  assign (strong0, strong1) conflict = 1'b1;
  assign switch_source = 1'b1;
  tran direct_link(switch_source, switch_target);
  assign vector_source = 2'b10;
  assign vector_control = 2'b01;
  tranif1 vector_link(vector_source, vector_target, vector_control);
  assign (strong0, strong1) chain_left = 1'b1;
  assign (weak0, weak1) chain_right = 1'b0;
  rtran first_link(chain_left, chain_middle);
  rtran second_link(chain_middle, chain_right);
  assign decayed = drive;
  assign retained = drive;
  initial begin
    drive = 1'b1;
    #1 drive = 1'bz;
    #3 $finish;
  end
endmodule
)";
  {
    std::ofstream output(source, std::ios::binary);
    output << source_text;
    assert(output.good());
  }

  fsim::project::Config compile_config;
  compile_config.manifest_path = "<strength-artifact-compile>";
  compile_config.base_directory = directory;
  compile_config.project.name = "strength-artifact-compile";
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::verilog;
  sources.standard = "2005";
  sources.library = "vendor";
  sources.compilation_unit = "source-set";
  sources.file_patterns = {source};
  sources.files = {source};
  compile_config.source_sets.push_back(std::move(sources));
  fsim::diagnostic::Engine compile_diagnostics;
  assert(fsim::app::compile_artifact(
      compile_config, object, compile_diagnostics));
  assert(!compile_diagnostics.has_error());
  fsim::diagnostic::Engine object_inspection_diagnostics;
  const auto object_inspection = fsim::app::inspect_artifact(
      object, object_inspection_diagnostics);
  assert(object_inspection && !object_inspection_diagnostics.has_error());
  assert(std::ranges::find(
      object_inspection->units,
      "verilog:vendor.strength_artifact_top")
      != object_inspection->units.end());

  fsim::project::Config elaborate_config;
  elaborate_config.manifest_path = "<strength-artifact-elaborate>";
  elaborate_config.base_directory = directory;
  elaborate_config.project.name = "strength-artifact-elaborate";
  elaborate_config.project.tops.push_back(
      {"verilog:vendor.strength_artifact_top", "dut"});
  elaborate_config.project.time_resolution = "1ns";
  elaborate_config.build.cache_path = directory / "strength-artifact-cache";
  const std::array objects{object};
  fsim::diagnostic::Engine elaborate_diagnostics;
  assert(fsim::app::elaborate_artifact(
      elaborate_config, objects, design, elaborate_diagnostics));
  assert(!elaborate_diagnostics.has_error());

  const auto hidden_source = directory / "strength-artifact.v.hidden";
  const auto hidden_object = directory / "strength-artifact.fsimobj.hidden";
  std::filesystem::rename(source, hidden_source);
  std::filesystem::rename(object, hidden_object);
  const auto run_standalone = [&](
      const std::filesystem::path& artifact,
      const fsim::app::SimulationEngine engine) {
    fsim::diagnostic::Engine diagnostics;
    auto loaded = fsim::app::load_design_artifact(artifact, diagnostics);
    assert(loaded && !diagnostics.has_error());
    const auto conflict = loaded->design.find_signal("dut.conflict");
    const auto switch_target = loaded->design.find_signal("dut.switch_target");
    const auto vector_target = loaded->design.find_signal("dut.vector_target");
    const auto chain_right = loaded->design.find_signal("dut.chain_right");
    const auto decayed = loaded->design.find_signal("dut.decayed");
    const auto retained = loaded->design.find_signal("dut.retained");
    assert(
        conflict && switch_target && vector_target && chain_right
        && decayed && retained);
    fsim::app::Simulation simulation{std::move(*loaded), 1000, engine};
    const auto cache = simulation.native_cache_statistics();
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::stopped);
    assert(result.time == 4);
    return std::pair{
        std::array{
            simulation.read_signal(*conflict).to_msb_string(),
            simulation.read_signal(*switch_target).to_msb_string(),
            simulation.read_signal(*vector_target).to_msb_string(),
            simulation.read_signal(*chain_right).to_msb_string(),
            simulation.read_signal(*decayed).to_msb_string(),
            simulation.read_signal(*retained).to_msb_string()},
        cache};
  };
  const auto standalone_reference = run_standalone(
      design,
      fsim::app::SimulationEngine::interpreter);
  const auto standalone_cold = run_standalone(
      design,
      fsim::app::SimulationEngine::compiled);
  const auto standalone_warm = run_standalone(
      design,
      fsim::app::SimulationEngine::compiled);
  const std::array<std::string, 6> expected{
      "1", "1", "Z0", "X", "Z", "1"};
  assert(standalone_reference.first == expected);
  assert(standalone_cold.first == standalone_reference.first);
  assert(standalone_warm.first == standalone_reference.first);
#if defined(FSIM_HAS_LLVM)
  assert(standalone_cold.second.misses != 0);
  assert(standalone_cold.second.stores == standalone_cold.second.misses);
  assert(standalone_warm.second.hits == standalone_cold.second.misses);
  assert(standalone_warm.second.misses == 0);
#endif

  const auto corrupt_design = directory / "strength-corrupt.fsimdesign";
  std::filesystem::create_directories(corrupt_design);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(design)) {
    const auto relative = std::filesystem::relative(entry.path(), design);
    if (entry.is_directory()) {
      std::filesystem::create_directories(corrupt_design / relative);
    } else {
      std::filesystem::copy_file(entry.path(), corrupt_design / relative);
    }
  }
  const auto corrupt_runtime = corrupt_design / "state" / "runtime.bin";
  std::filesystem::permissions(
      corrupt_runtime,
      std::filesystem::perms::owner_write,
      std::filesystem::perm_options::add);
  {
    std::ofstream output(
        corrupt_runtime, std::ios::binary | std::ios::app);
    output.put('x');
    assert(output.good());
  }
  fsim::diagnostic::Engine corrupt_diagnostics;
  assert(!fsim::app::load_design_artifact(
      corrupt_design, corrupt_diagnostics));
  assert(corrupt_diagnostics.has_error());

  std::filesystem::rename(hidden_source, source);
  std::filesystem::rename(hidden_object, object);
  auto edited_source_text = source_text;
  const auto edited_assignment = edited_source_text.find(
      "assign switch_source = 1'b1;");
  assert(edited_assignment != std::string::npos);
  edited_source_text.replace(
      edited_assignment,
      std::string{"assign switch_source = 1'b1;"}.size(),
      "assign switch_source = 1'b0;");
  {
    std::ofstream output(source, std::ios::binary);
    output << edited_source_text;
    assert(output.good());
  }
  const auto edited_object = directory / "strength-artifact-edited.fsimobj";
  const auto edited_design = directory / "strength-artifact-edited.fsimdesign";
  fsim::diagnostic::Engine edited_compile_diagnostics;
  assert(fsim::app::compile_artifact(
      compile_config, edited_object, edited_compile_diagnostics));
  assert(!edited_compile_diagnostics.has_error());
  const std::array edited_objects{edited_object};
  fsim::diagnostic::Engine edited_elaborate_diagnostics;
  assert(fsim::app::elaborate_artifact(
      elaborate_config,
      edited_objects,
      edited_design,
      edited_elaborate_diagnostics));
  assert(!edited_elaborate_diagnostics.has_error());
  const auto edited = run_standalone(
      edited_design, fsim::app::SimulationEngine::compiled);
  assert(edited.first[1] == "0" && edited.first[2] == "Z0");
#if defined(FSIM_HAS_LLVM)
  assert(edited.second.misses != 0);
#endif
  {
    std::ofstream output(source, std::ios::binary);
    output << source_text;
    assert(output.good());
  }
  const auto library = directory / "strength-vendor.fsimlib";
  auto export_config = compile_config;
  export_config.project.name = "strength-library-export";
  export_config.project.top = "verilog:vendor.strength_artifact_top";
  export_config.project.time_resolution = "1ns";
  export_config.build.cache_path = directory / "strength-library-cache";
  fsim::diagnostic::Engine export_diagnostics;
  assert(fsim::app::export_library(
      export_config, "vendor", library, export_diagnostics));
  assert(!export_diagnostics.has_error());
  const auto relocated_library = directory / "relocated-strength.fsimlib";
  std::filesystem::rename(library, relocated_library);

  const auto consumer_source = directory / "strength-consumer.v";
  {
    std::ofstream output(consumer_source, std::ios::binary);
    output << R"(module mapped_strength_consumer;
  strength_artifact_top imported();
endmodule
)";
    assert(output.good());
  }
  fsim::project::Config consumer_config;
  consumer_config.base_directory = directory;
  consumer_config.project.name = "mapped-strength-consumer";
  consumer_config.project.top = "verilog:consumer.mapped_strength_consumer";
  consumer_config.project.time_resolution = "1ns";
  consumer_config.build.cache_path = directory / "mapped-strength-cache";
  fsim::project::SourceSet consumer_sources;
  consumer_sources.language = fsim::project::Language::verilog;
  consumer_sources.standard = "2005";
  consumer_sources.library = "consumer";
  consumer_sources.files = {consumer_source};
  consumer_config.source_sets.push_back(std::move(consumer_sources));
  consumer_config.library_mappings.push_back(
      {"vendor", relocated_library});
  consumer_config.elaboration.search_libraries = {"vendor"};
  const auto run_mapped = [&](const fsim::app::SimulationEngine engine) {
    fsim::diagnostic::Engine diagnostics;
    auto consumer = fsim::app::build_project(consumer_config, diagnostics);
    if (!consumer) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
      }
    }
    assert(consumer && !diagnostics.has_error());
    assert(std::ranges::any_of(
        consumer->design.specializations(), [](const auto& entry) {
          return entry.instance == "mapped_strength_consumer.imported"
              && entry.library == "vendor";
        }));
    const auto signal = [&](const std::string_view leaf) {
      const auto found = consumer->design.find_signal(
          "mapped_strength_consumer.imported." + std::string{leaf});
      assert(found);
      return *found;
    };
    const std::array signals{
        signal("conflict"), signal("switch_target"), signal("vector_target"),
        signal("chain_right"), signal("decayed"), signal("retained")};
    fsim::app::Simulation simulation{std::move(*consumer), 1000, engine};
    const auto cache = simulation.native_cache_statistics();
    const auto result = simulation.run();
    assert(result.status == fsim::runtime::RunStatus::stopped);
    std::array<std::string, signals.size()> values;
    std::ranges::transform(
        signals,
        values.begin(),
        [&](const auto id) {
          return simulation.read_signal(id).to_msb_string();
        });
    return std::pair{values, cache};
  };
  const auto mapped_reference = run_mapped(
      fsim::app::SimulationEngine::interpreter);
  const auto mapped_cold = run_mapped(
      fsim::app::SimulationEngine::compiled);
  const auto mapped_warm = run_mapped(
      fsim::app::SimulationEngine::compiled);
  assert(mapped_reference.first == expected);
  assert(mapped_cold.first == mapped_reference.first);
  assert(mapped_warm.first == mapped_reference.first);
#if defined(FSIM_HAS_LLVM)
  assert(mapped_cold.second.misses != 0);
  assert(mapped_cold.second.stores == mapped_cold.second.misses);
  assert(mapped_warm.second.hits == mapped_cold.second.misses);
  assert(mapped_warm.second.misses == 0);
#endif
}

struct BoundaryChange {
  std::string signal;
  std::string value;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const BoundaryChange&, const BoundaryChange&) = default;
};

struct BoundaryCapture {
  fsim::runtime::RunResult paused;
  fsim::runtime::RunResult result;
  std::vector<BoundaryChange> changes;
  std::vector<std::tuple<
      std::string, fsim::runtime::SimulationTick, std::uint64_t>> outputs;
  std::vector<std::string> final_values;
  std::vector<std::string> specialization_keys;
  std::vector<std::pair<std::string, std::string>> construction_identities;
  std::string debugger;
  std::string vcd;
  std::size_t observer_changes{};
  std::size_t boundary_conversions{};
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
  fsim::app::NativeCacheStatistics cache;
};

void verify_boundary_delays(
    const std::filesystem::path& directory,
    const fsim::project::Optimization optimization) {
  // FSIM-CONFORMANCE CF-MIX-TIMING-001 source=SRC-FSIM expectation=execute
  const auto sv_source = directory / "mixed_timing_top.sv";
  {
    std::ofstream output(sv_source, std::ios::binary);
    output << R"(timeunit 1ps / 1ps;
module sv_transition_leaf(output wire result);
  logic drive;
  assign #(2ps, 3ps, 4ps) result = drive;
  initial begin
    drive = 1'b0;
    #10ps drive = 1'b1;
    #10ps drive = 1'bz;
  end
endmodule

module sv_region_leaf(output logic [1:0] result);
  initial begin
    result = 2'b00;
    #0 result = 2'b01;
    result <= 2'b10;
    result <= 2'b11;
    $strobe("region=%b", result);
    #1ps $stop;
  end
endmodule

module mixed_timing_top;
  logic [7:0] zero_result;
  logic [7:0] inertial_result;
  logic [7:0] transport_result;
  logic [7:0] reject_result;
  logic [7:0] transition_result;
  logic [7:0] region_result;
  logic [7:0] postponed_result;
  logic [7:0] loop_source;
  logic [7:0] loop_result;
  vhdl_timing_mid #(.Enabled(1'b1)) mid(
      .zero_result(zero_result),
      .inertial_result(inertial_result),
      .transport_result(transport_result),
      .reject_result(reject_result),
      .transition_result(transition_result),
      .region_result(region_result),
      .postponed_result(postponed_result),
      .loop_source(loop_source),
      .loop_result(loop_result));
  always @(loop_result) begin
    if (loop_result[3:0] === 4'bxxxx)
      loop_source = 8'h00;
    else if (loop_result < 8'h03)
      loop_source = loop_result + 8'h01;
  end
  initial #30ps $finish;
endmodule
)";
    assert(output.good());
  }
  const auto vhdl_source = directory / "mixed_timing_mid.vhd";
  {
    std::ofstream output(vhdl_source, std::ios::binary);
    output << R"(entity Mixed_Timing_Mid is
  generic (Enabled : boolean := false);
  port (
    Zero_Result : out std_logic_vector(3 downto 0);
    Inertial_Result : out std_logic_vector(3 downto 0);
    Transport_Result : out std_logic_vector(3 downto 0);
    Reject_Result : out std_logic_vector(3 downto 0);
    Transition_Result : out std_logic_vector(3 downto 0);
    Region_Result : out std_logic_vector(3 downto 0);
    Postponed_Result : out std_logic_vector(3 downto 0);
    Loop_Source : in std_logic_vector(3 downto 0);
    Loop_Result : out std_logic_vector(3 downto 0));
end entity;

architecture rtl of Mixed_Timing_Mid is
begin
  Transition_Leaf : sv_transition_leaf
    port map (Result => Transition_Result);
  Region_Leaf : sv_region_leaf
    port map (Result => Region_Result);
  Postponed_Result <= transport Region_Result;
  Loop_Result <= transport Loop_Source;
  Zero_Result <= transport "1010" when Enabled else "0101";

  inertial_driver: process
  begin
    Inertial_Result <= "0000" after 5 ps;
    wait for 10 ps;
    Inertial_Result <= "1111" after 5 ps;
    wait for 1 ps;
    Inertial_Result <= "0000" after 5 ps;
    wait for 9 ps;
    Inertial_Result <= "1111" after 5 ps;
    wait;
  end process;

  transport_driver: process
  begin
    Transport_Result <= transport "0000" after 5 ps;
    wait for 10 ps;
    Transport_Result <= transport "1111" after 5 ps;
    wait for 1 ps;
    Transport_Result <= transport "0000" after 5 ps;
    wait for 9 ps;
    Transport_Result <= transport "1111" after 5 ps;
    wait;
  end process;

  reject_driver: process
  begin
    Reject_Result <= reject 2 ps inertial "0000" after 5 ps;
    wait for 10 ps;
    Reject_Result <= reject 2 ps inertial "1111" after 5 ps;
    wait for 1 ps;
    Reject_Result <= reject 2 ps inertial "0000" after 5 ps;
    wait for 9 ps;
    Reject_Result <= reject 2 ps inertial "1111" after 5 ps;
    wait;
  end process;
end architecture;
)";
    assert(output.good());
  }

  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "mixed-boundary-timing";
  config.project.top = "sv:work.mixed_timing_top";
  config.project.time_resolution = "auto";
  config.build.optimization = optimization;
  config.build.cache_path = directory
      / (optimization == fsim::project::Optimization::o0
             ? "timing-cache-o0"
             : "timing-cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sv_sources;
  sv_sources.language = fsim::project::Language::system_verilog;
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
  config.bindings = {
      {"mixed_timing_top.mid", "vhdl:work.mixed_timing_mid(rtl)",
       std::nullopt},
      {"mixed_timing_top.mid.transition_leaf",
       "sv:work.sv_transition_leaf", std::nullopt},
      {"mixed_timing_top.mid.region_leaf",
       "sv:work.sv_region_leaf", std::nullopt},
  };
  constexpr std::array<std::string_view, 11> names{
      "mixed_timing_top.zero_result",
      "mixed_timing_top.inertial_result",
      "mixed_timing_top.transport_result",
      "mixed_timing_top.reject_result",
      "mixed_timing_top.transition_result",
      "mixed_timing_top.region_result",
      "mixed_timing_top.postponed_result",
      "mixed_timing_top.mid.region_result",
      "mixed_timing_top.mid.region_leaf.result",
      "mixed_timing_top.loop_source",
      "mixed_timing_top.loop_result"};
  const auto execute = [&](const fsim::app::SimulationEngine engine) {
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
      for (const auto& diagnostic : diagnostics.diagnostics()) {
        std::cerr << diagnostic.code << ": "
                  << diagnostic.message << '\n';
      }
    }
    assert(project);
    assert(project->time_resolution == "1ps");
    BoundaryCapture capture;
    capture.boundary_conversions =
        project->design.boundary_conversions().size();
    capture.specialization_keys = project->specialization_cache_keys;
    for (const auto& specialization : project->design.specializations()) {
      if (specialization.instance == "mixed_timing_top.mid") {
        capture.construction_identities =
            specialization.parameter_identity_values;
      }
    }
    std::array<fsim::runtime::simir::SignalId, names.size()> signals{};
    fsim::app::Simulation simulation{
        std::move(*project), config.run.max_deltas, engine};
    capture.compiled_processes = simulation.compiled_process_count();
    capture.compiled_modules = simulation.compiled_module_count();
    capture.cache = simulation.native_cache_statistics();
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto signal = simulation.find_signal(names[index]);
      assert(signal);
      signals[index] = *signal;
    }
    std::ostringstream vcd_output;
    fsim::runtime::VcdWriter vcd(vcd_output, "1ps", 64);
    std::array<fsim::runtime::VcdSignal, names.size()> vcd_signals{};
    for (std::size_t index = 0; index < names.size(); ++index) {
      vcd_signals[index] = vcd.declare_signal(
          std::string{names[index]},
          simulation.read_signal(signals[index]).width());
    }
    vcd.begin(simulation.now());
    for (std::size_t index = 0; index < names.size(); ++index) {
      vcd.change(
          vcd_signals[index], simulation.read_signal(signals[index]));
    }
    simulation.set_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4& value,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
          const auto found = std::find(
              signals.begin(), signals.end(), signal);
          if (found == signals.end()) {
            return;
          }
          const auto index = static_cast<std::size_t>(
              std::distance(signals.begin(), found));
          capture.changes.push_back({
              std::string{names[index]}, value.to_msb_string(), time, delta});
          vcd.set_time(time);
          vcd.change(vcd_signals[index], value);
        });
    const auto observer = simulation.add_signal_change_hook(
        [&](const fsim::runtime::simir::SignalId signal,
            const fsim::runtime::PackedLogic4&,
            const fsim::runtime::SimulationTick,
            const std::uint64_t) {
          if (std::find(signals.begin(), signals.end(), signal)
              != signals.end()) {
            ++capture.observer_changes;
          }
        });
    simulation.set_output_hook(
        [&](const fsim::runtime::simir::ProcessId,
            const std::string_view text,
            const bool,
            const fsim::runtime::SimulationTick time,
            const std::uint64_t delta) {
          capture.outputs.emplace_back(text, time, delta);
        });
    capture.paused = simulation.run();
    assert(!simulation.finished());
    std::ostringstream debugger_output;
    std::ostringstream debugger_error;
    fsim::app::DebuggerControl debugger{
        simulation, debugger_output, debugger_error};
    debugger.execute({"show", "region_result"});
    debugger.execute({"show", "postponed_result"});
    assert(debugger_error.str().empty());
    capture.debugger = debugger_output.str();
    simulation.clear_stop();
    capture.result = simulation.run();
    assert(simulation.finished());
    simulation.remove_signal_change_hook(observer);
    vcd.flush();
    capture.vcd = vcd_output.str();
    for (const auto signal : signals) {
      capture.final_values.push_back(
          simulation.read_signal(signal).to_msb_string());
    }
    return capture;
  };

  const auto reference = execute(fsim::app::SimulationEngine::interpreter);
  const auto cold = execute(fsim::app::SimulationEngine::compiled);
  const auto warm = execute(fsim::app::SimulationEngine::compiled);
  std::ifstream edited_input(vhdl_source, std::ios::binary);
  std::ostringstream edited_text;
  edited_text << edited_input.rdbuf();
  auto edited_source = edited_text.str();
  const auto original_literal = std::string{
      "Zero_Result <= transport \"1010\""};
  const auto edited_literal = std::string{
      "Zero_Result <= transport \"0011\""};
  const auto edit_offset = edited_source.find(original_literal);
  assert(edit_offset != std::string::npos);
  edited_source.replace(
      edit_offset, original_literal.size(), edited_literal);
  {
    std::ofstream edited_output(vhdl_source, std::ios::binary);
    edited_output << edited_source;
    assert(edited_output.good());
  }
  const auto edited = execute(fsim::app::SimulationEngine::compiled);
  const auto& changes = reference.changes;

  const auto observed = [&](const std::string_view name,
                            const bool positive_only = false) {
    std::vector<std::pair<std::string, fsim::runtime::SimulationTick>> values;
    for (const auto& change : changes) {
      if (change.signal == name && (!positive_only || change.time > 0)) {
        values.emplace_back(change.value, change.time);
      }
    }
    return values;
  };
  const auto observed_with_delta = [&](const std::string_view name) {
    std::vector<std::tuple<
        std::string, fsim::runtime::SimulationTick, std::uint64_t>> values;
    for (const auto& change : changes) {
      if (change.signal == name) {
        values.emplace_back(change.value, change.time, change.delta);
      }
    }
    return values;
  };
  assert((observed(names[0])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00001010", 0}}));
  assert((observed(names[1])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00000000", 5},
              {"00001111", 25}}));
  assert((observed(names[2])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00000000", 5},
              {"00001111", 15},
              {"00000000", 16}, {"00001111", 25}}));
  assert((observed(names[3])
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"0000XXXX", 0}, {"00000000", 5},
              {"00001111", 25}}));
  assert((observed(names[4], true)
          == std::vector<std::pair<
              std::string, fsim::runtime::SimulationTick>>{
              {"00000000", 3}, {"00000001", 12},
              {"0000000Z", 24}}));
  assert((observed_with_delta(names[8])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"00", 0, 0}, {"01", 0, 0}, {"11", 0, 0}}));
  assert((observed_with_delta(names[7])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"00XX", 0, 0}, {"0011", 0, 1}}));
  assert((observed_with_delta(names[5])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"0000XXXX", 0, 0}, {"000000XX", 0, 1},
              {"00000011", 0, 2}}));
  assert((observed_with_delta(names[6])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"0000XXXX", 0, 0}, {"000000XX", 0, 2},
              {"00000011", 0, 3}}));
  assert((observed_with_delta(names[9])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"00000000", 0, 1}, {"00000001", 0, 5},
              {"00000010", 0, 9}, {"00000011", 0, 13}}));
  assert((observed_with_delta(names[10])
          == std::vector<std::tuple<
              std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
              {"0000XXXX", 0, 0}, {"00000000", 0, 4},
              {"00000001", 0, 8}, {"00000010", 0, 12},
              {"00000011", 0, 16}}));
  assert(reference.paused.status == fsim::runtime::RunStatus::stopped);
  assert(reference.paused.time == 1);
  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 30);
  assert(reference.boundary_conversions == 11);
  assert(std::ranges::any_of(
      reference.construction_identities,
      [](const auto& identity) {
        return identity.first == "enabled"
            && identity.second.starts_with("vhdlconst-v1;")
            && identity.second.find(";type=boolean;")
                != std::string::npos;
      }));
  assert(reference.observer_changes == reference.changes.size());
  assert(reference.debugger.find("region_result")
         != std::string::npos);
  assert(reference.debugger.find("postponed_result")
         != std::string::npos);
  assert(reference.vcd.find("$timescale 1ps $end")
         != std::string::npos);
  assert((reference.outputs == std::vector<std::tuple<
      std::string, fsim::runtime::SimulationTick, std::uint64_t>>{
      {"region=11", 0, 0}}));
  assert(reference.final_values[9] == "00000011");
  assert(reference.final_values[10] == "00000011");
  for (const auto* actual : {&cold, &warm}) {
    assert(actual->paused.status == reference.paused.status);
    assert(actual->paused.time == reference.paused.time);
    assert(actual->paused.delta == reference.paused.delta);
    assert(actual->result.status == reference.result.status);
    assert(actual->result.time == reference.result.time);
    assert(actual->result.delta == reference.result.delta);
    assert(actual->changes == reference.changes);
    assert(actual->outputs == reference.outputs);
    assert(actual->final_values == reference.final_values);
    assert(actual->specialization_keys == reference.specialization_keys);
    assert(actual->construction_identities
           == reference.construction_identities);
    assert(actual->debugger == reference.debugger);
    assert(actual->vcd == reference.vcd);
    assert(actual->observer_changes == actual->changes.size());
    assert(actual->boundary_conversions == reference.boundary_conversions);
  }
#if defined(FSIM_HAS_LLVM)
  assert(cold.compiled_processes > 0);
  assert(cold.compiled_modules > 0);
  assert(cold.cache.hits == 0);
  assert(cold.cache.misses == cold.compiled_modules);
  assert(cold.cache.stores == cold.compiled_modules);
  assert(warm.compiled_processes == cold.compiled_processes);
  assert(warm.compiled_modules == cold.compiled_modules);
  assert(warm.cache.hits == warm.compiled_modules);
  assert(warm.cache.misses == 0);
#else
  assert(cold.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
#endif
  assert(edited.final_values[0] == "00000011");
  assert(std::equal(
      edited.final_values.begin() + 1,
      edited.final_values.end(),
      warm.final_values.begin() + 1));
  assert(edited.specialization_keys != warm.specialization_keys);
  assert(edited.construction_identities == warm.construction_identities);
  assert(edited.boundary_conversions == warm.boundary_conversions);
#if defined(FSIM_HAS_LLVM)
  assert(edited.cache.misses > 0);
  assert(edited.cache.hits + edited.cache.misses
         == edited.compiled_modules);
#endif
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-resolution-application-test-"
         + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);

  const auto sv_source = directory.path / "resolved_top.sv";
  {
    // FSIM-CONFORMANCE CF-MIX-RESOLUTION-001 source=SRC-FSIM expectation=execute
    // FSIM-CONFORMANCE CF-MIX-OWNERSHIP-001 source=SRC-FSIM expectation=execute
    std::ofstream output(sv_source, std::ios::binary);
    output << R"(timeunit 1ns / 1ps;
module sv_driver(output logic value);
  initial begin
    value = 1'b1;
    #5ns value = 1'bz;
  end
endmodule

module resolved_top;
  logic shared;
  vhdl_driver u_vhdl(.value(shared));
  sv_driver u_sv(.value(shared));
  initial #6ns $finish;
endmodule
)";
  }

  const auto vhdl_source = directory.path / "vhdl_driver.vhd";
  {
    std::ofstream output(vhdl_source, std::ios::binary);
    output << R"(entity vhdl_driver is
  port (value : out std_logic);
end entity;

architecture rtl of vhdl_driver is
begin
  drive: process
  begin
    value <= '0';
    wait for 3 ns;
    value <= 'Z';
    wait;
  end process;
end architecture;
)";
  }

  verify_mode(
      directory.path,
      sv_source,
      vhdl_source,
      fsim::project::Optimization::o0);
  verify_mode(
      directory.path,
      sv_source,
      vhdl_source,
      fsim::project::Optimization::o2);
  verify_verilog_strengths(
      directory.path, fsim::project::Optimization::o0);
  verify_verilog_strengths(
      directory.path, fsim::project::Optimization::o2);
  verify_strength_artifacts_and_mapping(directory.path);
  verify_boundary_delays(
      directory.path, fsim::project::Optimization::o0);
  verify_boundary_delays(
      directory.path, fsim::project::Optimization::o2);
  return 0;
}
