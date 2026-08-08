// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"
#include "fsim/runtime/vcd_writer.hpp"
#include "fsim/support/environment.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#ifndef FSIM_TEST_SOURCE_DIR
#error "FSIM_TEST_SOURCE_DIR must name the fsim source tree"
#endif

namespace {

using fsim::runtime::SystemVerilogClassHandle;
using fsim::runtime::SystemVerilogUvmActivityAction;
using fsim::runtime::SystemVerilogUvmActivityEvent;
using fsim::runtime::SystemVerilogUvmActivityKind;
using fsim::runtime::SystemVerilogUvmPhaseCallbackKind;
using fsim::runtime::SystemVerilogUvmPhaseKind;
using fsim::runtime::SystemVerilogUvmTaskPhaseStatus;

struct ExerciseResult {
  std::string transcript;
  std::vector<SystemVerilogUvmActivityEvent> activity;
  fsim::app::NativeCacheStatistics cache;
};

constexpr std::string_view kExpectedTranscript{
    "FSIM-UVM-PHASE-TLM-PASS phases=build/connect/eoe/sos/run/extract/"
    "check/report/final roots=left,right objection=1/0 drain=3 payload=37 "
    "result=42 source=37/42/1 race=5 deadlock=FSIM-UVM-PHASE-008"};

fsim_uvm_foreign_status_v1 FSIM_UVM_FOREIGN_CALL
accept_foreign_activity(void *, const fsim_uvm_foreign_activity_v1 *) {
  return FSIM_UVM_FOREIGN_OK;
}

void print_diagnostics(const fsim::diagnostic::Engine &diagnostics) {
  fsim::diagnostic::print_text(std::cerr, diagnostics);
}

void make_tree_writable(const std::filesystem::path &root) noexcept {
  std::error_code error;
  if (!std::filesystem::exists(root, error))
    return;
  for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
       !error && iterator != end; iterator.increment(error)) {
    std::filesystem::permissions(iterator->path(),
                                 std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::add, error);
    error.clear();
  }
  std::filesystem::permissions(root, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::add, error);
}

fsim::project::Config
make_config(const std::filesystem::path &uvm_root,
            const std::filesystem::path &work,
            const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  const auto absolute_uvm_root = std::filesystem::absolute(uvm_root);
  config.base_directory = work;
  config.project.name = "uvm-phase-tlm-example";
  config.project.time_resolution = "1ns";
  config.project.tops = {{"sv:work.fsim_uvm_phase_tlm_example", "left"},
                         {"sv:work.fsim_uvm_phase_tlm_example", "right"}};
  config.build.cache_path = work / "cache";
  config.build.optimization = optimization;
  config.run.max_deltas = 1'000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
  sources.standard = "2017";
  sources.library = "work";
  sources.compilation_unit = "source-set";
  sources.include_directories = {absolute_uvm_root / "src"};
  sources.files = {absolute_uvm_root / "src" / "uvm_pkg.sv",
                   std::filesystem::path{FSIM_TEST_SOURCE_DIR} /
                       "tests/fixtures/systemverilog/uvm_phase_tlm_example.sv"};
  config.source_sets.push_back(std::move(sources));
  return config;
}

const fsim::frontend::SystemVerilogClassSpecialization &
find_class(const fsim::app::BuiltProject &project,
           const std::string_view suffix) {
  const auto found = std::ranges::find_if(
      project.systemverilog_class_specializations,
      [&](const auto &specialization) {
        return specialization.declaration_identity.ends_with(suffix);
      });
  if (found == project.systemverilog_class_specializations.end()) {
    std::cerr << "missing class specialization ending in " << suffix << '\n';
    std::abort();
  }
  return *found;
}

std::uint64_t low_word(
    const std::optional<fsim::runtime::SystemVerilogUvmTlm1Payload> &payload) {
  assert(payload && payload->value.width() == 32);
  std::uint64_t result{};
  for (std::size_t bit = 0; bit < 32; ++bit) {
    if (payload->value.get(bit) == fsim::runtime::Logic4::one) {
      result |= std::uint64_t{1} << bit;
    }
  }
  return result;
}

fsim::runtime::PackedLogic4 packed32(const std::uint32_t value) {
  fsim::runtime::PackedLogic4 result(32, fsim::runtime::Logic4::zero);
  for (std::size_t bit = 0; bit < 32; ++bit) {
    if (((value >> bit) & 1U) != 0) {
      result.set(bit, fsim::runtime::Logic4::one);
    }
  }
  return result;
}

fsim::runtime::PackedLogic4 packed_value(const std::size_t width,
                                         const std::uint64_t value) {
  fsim::runtime::PackedLogic4 result(width, fsim::runtime::Logic4::zero);
  for (std::size_t bit = 0; bit < width; ++bit) {
    if (((value >> bit) & 1U) != 0) {
      result.set(bit, fsim::runtime::Logic4::one);
    }
  }
  return result;
}

void write_activity_trace(
    const std::filesystem::path &path,
    const std::span<const SystemVerilogUvmActivityEvent> events) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  assert(output);
  fsim::runtime::VcdWriter writer(output, "1ns");
  const auto sequence = writer.declare_signal("uvm.activity.sequence", 64);
  const auto kind = writer.declare_signal("uvm.activity.kind", 8);
  const auto action = writer.declare_signal("uvm.activity.action", 8);
  const auto value = writer.declare_signal("uvm.activity.value", 64);
  writer.begin(0);
  for (const auto &event : events) {
    writer.set_time(event.time);
    writer.change(sequence, packed_value(64, event.sequence));
    writer.change(kind,
                  packed_value(8, static_cast<std::uint64_t>(event.kind)));
    writer.change(action,
                  packed_value(8, static_cast<std::uint64_t>(event.action)));
    writer.change(value, packed_value(64, event.value));
  }
  writer.flush();
  assert(output.good());
}

ExerciseResult exercise(fsim::app::BuiltProject project,
                        const fsim::app::SimulationEngine engine,
                        const std::filesystem::path &trace_path) {
  const auto &component_class = find_class(project, "::fsim_native_component");
  const auto component_specialization = component_class.specialization_identity;
  const auto component_declaration = component_class.declaration_identity;
  const auto payload_declaration =
      find_class(project, "::fsim_uvm_payload").declaration_identity;
  fsim::app::Simulation simulation(std::move(project), 1'000, engine);
  std::vector<SystemVerilogUvmActivityEvent> callbacks;
  const auto callback_token = simulation.add_uvm_activity_hook(
      [&](const auto &event) { callbacks.push_back(event); });

  const auto left_root = simulation.create_uvm_root("left");
  const auto right_root = simulation.create_uvm_root("right");
  const auto left = simulation.allocate_uvm_component(
      component_specialization, "worker", 0, left_root, component_declaration);
  const auto right = simulation.allocate_uvm_component(
      component_specialization, "worker", 0, right_root, component_declaration);
  assert(
      left && right &&
      simulation.read_class_property(left, "marker").packed.low_word().aval ==
          10 &&
      simulation.read_class_property(right, "marker").packed.low_word().aval ==
          10);

  std::vector<std::string> phase_order;
  const auto component_name = [&](const SystemVerilogClassHandle component) {
    const auto root = simulation.uvm_components().root_of(component);
    return std::string{simulation.uvm_components().root_identity(root)};
  };
  const auto record_execution = [&](const auto &result) {
    for (const auto &event : result.events) {
      if (event.callback == SystemVerilogUvmPhaseCallbackKind::Execute) {
        phase_order.push_back(
            std::string{
                simulation.uvm_phases().snapshot(event.phase).identity} +
            ':' + component_name(event.component));
      }
    }
  };
  auto &phases = simulation.uvm_phases();
  const auto schedule = *phases.standard_schedule();
  for (const auto kind :
       {SystemVerilogUvmPhaseKind::Build, SystemVerilogUvmPhaseKind::Connect,
        SystemVerilogUvmPhaseKind::EndOfElaboration,
        SystemVerilogUvmPhaseKind::StartOfSimulation}) {
    const auto result =
        simulation.execute_uvm_function_phase(schedule.phase(kind));
    if (!result.success()) {
      for (const auto &failure : result.failures) {
        std::cerr << failure.diagnostic_code << ": " << failure.message << '\n';
      }
    }
    assert(result.success());
    record_execution(result);
  }

  const auto run_phase = schedule.phase(SystemVerilogUvmPhaseKind::Run);
  auto &objections = simulation.uvm_objections();
  const auto source = objections.bind_source(left);
  const auto task_result = simulation.execute_uvm_task_phase(
      run_phase,
      [&](const SystemVerilogClassHandle component, const auto, const auto) {
        if (component == left) {
          objections.set_drain_time(run_phase, source, 3);
          const auto raised =
              objections.raise(run_phase, source, "transfer", 1);
          assert(raised.success());
        }
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  if (!task_result.success() || task_result.processes.size() != 2) {
    std::cerr << "run phase failed: state="
              << static_cast<unsigned>(task_result.final_state)
              << " processes=" << task_result.processes.size()
              << " events=" << task_result.events.size() << '\n';
    for (const auto &failure : task_result.failures) {
      std::cerr << failure.diagnostic_code << ": " << failure.message << '\n';
    }
  }
  assert(task_result.success() && task_result.processes.size() == 2);
  record_execution(task_result);
  fsim::runtime::SystemVerilogUvmCheckpointLimits process_limits;
  process_limits.maximum_external_phase_processes = 1;
  const auto bounded_process_checkpoint =
      simulation.capture_uvm_checkpoint(process_limits);
  assert(!bounded_process_checkpoint &&
         bounded_process_checkpoint.error ==
             fsim::runtime::SystemVerilogUvmCheckpointError::ResourceLimit);
  fsim::runtime::SystemVerilogUvmCheckpointLimits callback_limits;
  callback_limits.maximum_external_callbacks = 0;
  std::uint64_t foreign_callback_token{};
  assert(simulation.uvm_foreign().add_callback(accept_foreign_activity, nullptr,
                                               foreign_callback_token) ==
         FSIM_UVM_FOREIGN_OK);
  const auto bounded_callback_checkpoint =
      simulation.capture_uvm_checkpoint(callback_limits);
  assert(!bounded_callback_checkpoint &&
         bounded_callback_checkpoint.error ==
             fsim::runtime::SystemVerilogUvmCheckpointError::ResourceLimit);
  assert(simulation.uvm_foreign().remove_callback(foreign_callback_token) ==
         FSIM_UVM_FOREIGN_OK);
  assert(objections.source_count(run_phase, source) == 1 &&
         objections.propagated_count(run_phase, left_root) == 1);
  for (const auto &process : task_result.processes) {
    phases.complete_task_process(process);
  }
  const auto dropped = objections.drop(run_phase, source, "transfer", 1);
  assert(dropped.success() && objections.source_count(run_phase, source) == 0 &&
         objections.has_pending_drain(run_phase, source));
  const auto settled = phases.settle_task_phase(
      run_phase, [](const auto, const auto, const auto) {}, 10);
  assert(settled.status ==
             fsim::runtime::SystemVerilogUvmQuiescenceStatus::Completed &&
         settled.time == 3 &&
         settled.final_state ==
             fsim::runtime::SystemVerilogUvmPhaseState::Done);

  for (const auto kind :
       {SystemVerilogUvmPhaseKind::Extract, SystemVerilogUvmPhaseKind::Check,
        SystemVerilogUvmPhaseKind::Report, SystemVerilogUvmPhaseKind::Final}) {
    const auto result =
        simulation.execute_uvm_function_phase(schedule.phase(kind));
    assert(result.success());
    record_execution(result);
  }

  auto &tlm = simulation.uvm_tlm1();
  const fsim::runtime::SystemVerilogUvmTlm1Profile profile{
      fsim::runtime::SystemVerilogUvmTlm1Interface::Bidirectional,
      fsim::runtime::SystemVerilogUvmTlm1Direction::Bidirectional,
      payload_declaration,
      {}};
  std::vector<std::uint64_t> tlm_results;
  for (const auto &[component, root, prefix] :
       std::array{std::tuple{left, left_root, std::string_view{"left"}},
                  std::tuple{right, right_root, std::string_view{"right"}}}) {
    const auto port = tlm.register_endpoint(
        {fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Port, profile,
         component, "phase_port", 1, 1});
    const auto implementation = tlm.register_endpoint(
        {fsim::runtime::SystemVerilogUvmTlm1EndpointKind::Implementation,
         profile, component, "phase_fifo", 0, 0});
    tlm.connect(port, implementation);
    tlm.resolve_all();
    tlm.configure_fifo(implementation, 2);
    assert(
        tlm.try_put(port, {payload_declaration, packed32(37), 0, root, {}, 0}));
    const auto payload = tlm.try_get(port);
    assert(payload && payload->owner_root == root);
    tlm_results.push_back(low_word(payload) + 5);
    assert(tlm_results.back() == 42);
    (void)prefix;
  }

  const auto matrix_domain = phases.create_domain(
      "matrix", fsim::runtime::SystemVerilogUvmDomainKind::Custom);
  phases.participate(matrix_domain, left_root);
  phases.participate(matrix_domain, right_root);
  const auto race_phase = phases.create_custom_phase(
      matrix_domain, "race",
      fsim::runtime::SystemVerilogUvmPhaseExecutionKind::Task);
  const auto deadlock_phase = phases.create_custom_phase(
      matrix_domain, "deadlock",
      fsim::runtime::SystemVerilogUvmPhaseExecutionKind::Task);
  phases.connect(race_phase, deadlock_phase);
  const auto race_execution = phases.execute_task_phase(
      race_phase, [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  for (const auto &process : race_execution.processes) {
    phases.complete_task_process(process);
  }
  objections.set_drain_time(race_phase, source, 2);
  bool raced{};
  const auto race_settled = phases.settle_task_phase(
      race_phase,
      [&](const auto component, const auto phase, const auto kind) {
        if (!raced && component == left &&
            kind == SystemVerilogUvmPhaseCallbackKind::PhaseReadyToEnd) {
          raced = true;
          assert(objections.raise(phase, source, "matrix-race", 1).success());
          assert(objections.drop(phase, source, "matrix-race", 1).success());
        }
      },
      10);
  assert(raced &&
         race_settled.status ==
             fsim::runtime::SystemVerilogUvmQuiescenceStatus::Completed &&
         race_settled.time == 5 && phases.process_count() == 0 &&
         objections.phase_quiescent(race_phase));

  (void)phases.execute_task_phase(
      deadlock_phase, [](const auto, const auto, const auto) {},
      [](const auto, const auto, const auto) {
        return SystemVerilogUvmTaskPhaseStatus::Suspended;
      });
  bool deadlock_rejected{};
  try {
    (void)phases.settle_task_phase(deadlock_phase,
                                   [](const auto, const auto, const auto) {});
  } catch (const fsim::runtime::SystemVerilogUvmPhaseError &error) {
    deadlock_rejected = error.diagnostic_code() == "FSIM-UVM-PHASE-008";
  }
  assert(deadlock_rejected &&
         phases.snapshot(deadlock_phase).state ==
             fsim::runtime::SystemVerilogUvmPhaseState::Done &&
         phases.process_count() == 0 &&
         objections.phase_quiescent(deadlock_phase));

  std::ostringstream source_output;
  simulation.set_output_hook([&](const auto, const std::string_view text,
                                 const bool newline, const auto, const auto) {
    source_output << text;
    if (newline)
      source_output << '\n';
  });
  const auto run = simulation.run();
  assert(run.status == fsim::runtime::RunStatus::completed && run.time == 5);
  const std::string source_line{
      "FSIM-UVM-PHASE-TLM-SOURCE payload=37 result=42 pass=1\n"};
  assert(source_output.str() == source_line + source_line);
  assert((phase_order ==
          std::vector<std::string>{
              "build:left", "build:right", "connect:left", "connect:right",
              "end_of_elaboration:left", "end_of_elaboration:right",
              "start_of_simulation:left", "start_of_simulation:right",
              "run:left", "run:right", "extract:left", "extract:right",
              "check:left", "check:right", "report:left", "report:right",
              "final:left", "final:right"}));
  assert((tlm_results == std::vector<std::uint64_t>{42, 42}));
  assert(
      simulation.read_class_property(left, "marker").packed.low_word().aval ==
          9 &&
      simulation.read_class_property(right, "marker").packed.low_word().aval ==
          9);
  assert(std::ranges::any_of(callbacks, [](const auto &event) {
    return event.kind == SystemVerilogUvmActivityKind::Objection &&
           event.action == SystemVerilogUvmActivityAction::Raised;
  }));
  assert(std::ranges::any_of(callbacks, [](const auto &event) {
    return event.kind == SystemVerilogUvmActivityKind::Drain &&
           event.action == SystemVerilogUvmActivityAction::Completed &&
           event.time == 3;
  }));
  assert(std::ranges::any_of(callbacks, [](const auto &event) {
    return event.kind == SystemVerilogUvmActivityKind::Fifo &&
           event.action == SystemVerilogUvmActivityAction::Updated;
  }));
  write_activity_trace(trace_path, callbacks);
  std::ifstream trace(trace_path, std::ios::binary);
  const std::string trace_text{std::istreambuf_iterator<char>{trace},
                               std::istreambuf_iterator<char>{}};
  assert(trace_text.find("$var wire 64 ! sequence $end") != std::string::npos &&
         trace_text.find("#3") != std::string::npos &&
         trace_text.find("#5") != std::string::npos);
  simulation.remove_uvm_activity_hook(callback_token);

  std::ostringstream transcript;
  transcript << "FSIM-UVM-PHASE-TLM-PASS "
                "phases=build/connect/eoe/sos/run/extract/check/report/final"
             << " roots=left,right objection=1/0 drain=3 payload=37 result=42"
             << " source=37/42/1 race=5 deadlock=FSIM-UVM-PHASE-008";
  return {transcript.str(), std::move(callbacks),
          simulation.native_cache_statistics()};
}

fsim::project::Optimization parse_optimization(const std::string_view value) {
  if (value == "o0")
    return fsim::project::Optimization::o0;
  if (value == "o2")
    return fsim::project::Optimization::o2;
  throw std::invalid_argument{"expected o0 or o2"};
}

fsim::app::SimulationEngine parse_engine(const std::string_view value) {
  if (value == "interpreter") {
    return fsim::app::SimulationEngine::interpreter;
  }
  if (value == "compiled")
    return fsim::app::SimulationEngine::compiled;
  if (value == "debug")
    return fsim::app::SimulationEngine::debug;
  throw std::invalid_argument{"expected interpreter, compiled, or debug"};
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 5) {
    std::cerr << "usage: " << argv[0]
              << " <direct|compile|elaborate|simulate> <uvm-root> <work-dir>"
                 " <release> [optimization] [engine] [trace-suffix]\n";
    return 2;
  }
  const std::string_view mode{argv[1]};
  const std::filesystem::path uvm_root{argv[2]};
  const std::filesystem::path work{argv[3]};
  const std::string release{argv[4]};
  std::error_code cleanup_error;
  if (mode == "direct" &&
      !fsim::support::environment_variable("FSIM_TEST_REUSE_WORK")) {
    make_tree_writable(work);
    std::filesystem::remove_all(work, cleanup_error);
  }
  assert(!cleanup_error);
  std::filesystem::create_directories(work);

  if (mode == "direct") {
    auto config =
        make_config(uvm_root, work / "direct", fsim::project::Optimization::o2);
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(project && !diagnostics.has_error());
    const auto result =
        exercise(std::move(*project), fsim::app::SimulationEngine::interpreter,
                 work / "direct.vcd");
    assert(result.transcript == kExpectedTranscript);
    std::cout << result.transcript << " release=" << release
              << " stage=direct\n";
    return 0;
  }

  const auto object = work / "phase-tlm.fsimobj";
  if (mode == "compile") {
    auto config = make_config(uvm_root, work / "compile",
                              fsim::project::Optimization::o2);
    fsim::diagnostic::Engine diagnostics;
    const auto compiled =
        fsim::app::compile_artifact(config, object, diagnostics);
    if (!compiled || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(compiled && !diagnostics.has_error());
    std::cout << kExpectedTranscript << " release=" << release
              << " stage=compile\n";
    return 0;
  }

  if (argc < 6) {
    std::cerr << "elaborate/simulate requires an optimization\n";
    return 2;
  }
  const std::string optimization_name{argv[5]};
  const auto optimization = parse_optimization(optimization_name);
  auto config = make_config(uvm_root, work / optimization_name, optimization);
  const auto design = work / (optimization_name + ".fsimdesign");
  if (mode == "elaborate") {
    config.source_sets.clear();
    fsim::diagnostic::Engine diagnostics;
    const std::array objects{object};
    const auto elaborated =
        fsim::app::elaborate_artifact(config, objects, design, diagnostics);
    if (!elaborated || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(elaborated && !diagnostics.has_error());
    std::cout << kExpectedTranscript << " release=" << release
              << " stage=elaborate optimization=" << optimization_name << '\n';
    return 0;
  }

  if (mode == "simulate" && argc == 8) {
    const auto engine = parse_engine(argv[6]);
    fsim::diagnostic::Engine diagnostics;
    auto loaded = fsim::app::load_design_artifact(design, diagnostics);
    if (!loaded || diagnostics.has_error())
      print_diagnostics(diagnostics);
    assert(loaded && !diagnostics.has_error());
    const auto result = exercise(std::move(*loaded), engine,
                                 work / (optimization_name + '-' + argv[7]));
    assert(result.transcript == kExpectedTranscript);
    if (engine == fsim::app::SimulationEngine::compiled &&
        std::string_view{argv[7]} == "cold.fst") {
      assert(result.cache.hits == 0 && result.cache.misses != 0 &&
             result.cache.stores != 0);
    }
    if (engine == fsim::app::SimulationEngine::compiled &&
        std::string_view{argv[7]} == "warm.fst") {
      assert(result.cache.hits != 0 && result.cache.misses == 0);
    }
    std::cout << result.transcript << " release=" << release
              << " stage=simulate optimization=" << optimization_name
              << " engine=" << argv[6] << " trace=" << argv[7] << '\n';
    return 0;
  }
  std::cerr << "unknown mode or invalid mode arguments\n";
  return 2;
}
