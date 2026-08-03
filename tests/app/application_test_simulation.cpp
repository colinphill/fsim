// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include "fsim/systemc/hierarchy.hpp"

#include <algorithm>
#include <cassert>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_simulation_semantics() {
  auto config = base_config();
auto differential_config = config;
std::erase_if(
    differential_config.source_sets,
    [](const fsim::project::SourceSet& source_set) {
      return source_set.language
          == fsim::project::Language::systemc;
    });
differential_config.build.cache_path =
    directory / "differential-cache";
for (const auto optimization : {
         fsim::project::Optimization::o0,
         fsim::project::Optimization::o2}) {
  differential_config.build.optimization = optimization;
  fsim::diagnostic::Engine differential_diagnostics;
  auto reference_project = fsim::app::build_project(
      differential_config, differential_diagnostics);
  auto hybrid_project = fsim::app::build_project(
      differential_config, differential_diagnostics);
  assert(reference_project);
  assert(hybrid_project);
  const auto reference = capture_simulation(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto hybrid = capture_simulation(
      std::move(*hybrid_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(reference, hybrid);
#if defined(FSIM_HAS_LLVM)
  assert(hybrid.process_count == 2);
  assert(
      hybrid.compiled_processes
      == hybrid.process_count);
  assert(hybrid.compiled_modules == 2);
#endif

  auto warm_project = fsim::app::build_project(
      differential_config, differential_diagnostics);
  assert(warm_project);
  const auto warm = capture_simulation(
      std::move(*warm_project),
      fsim::app::SimulationEngine::compiled);
  compare_captures(reference, warm);
#if defined(FSIM_HAS_LLVM)
  assert(
      hybrid.native_cache.misses
      == hybrid.compiled_modules);
  assert(
      hybrid.native_cache.stores
      == hybrid.compiled_modules);
  assert(hybrid.native_cache.hits == 0);
  assert(
      warm.native_cache.hits
      == warm.compiled_modules);
  assert(warm.native_cache.misses == 0);
  assert(warm.native_cache.load_failures == 0);
  assert(warm.native_cache.store_failures == 0);
#else
  assert(
      hybrid.native_cache
      == fsim::app::NativeCacheStatistics{});
  assert(
      warm.native_cache
      == fsim::app::NativeCacheStatistics{});
#endif
}

struct CapturedAssertion {
  fsim::runtime::simir::ProcessId process{};
  fsim::runtime::simir::InstructionIndex instruction{};
  fsim::runtime::simir::AssertionSeverity severity{
      fsim::runtime::simir::AssertionSeverity::error};
  fsim::runtime::simir::SourceLocation source;
  std::string message;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};
auto assertion_config = config;
assertion_config.project.name = "assertion-differential";
assertion_config.project.top = "vhdl:work.assertion_test(rtl)";
assertion_config.build.cache_path = directory / "assertion-cache";
assertion_config.source_sets.clear();
fsim::project::SourceSet assertion_sources;
assertion_sources.language = fsim::project::Language::vhdl;
assertion_sources.standard = "2008";
assertion_sources.library = "work";
assertion_sources.files.push_back(assertion_source);
assertion_config.source_sets.push_back(std::move(assertion_sources));
const auto capture_assertion =
    [&](const fsim::project::Optimization optimization,
        const fsim::app::SimulationEngine engine) {
      assertion_config.build.optimization = optimization;
      fsim::diagnostic::Engine assertion_diagnostics;
      auto project =
          fsim::app::build_project(assertion_config, assertion_diagnostics);
      assert(project);
      fsim::app::Simulation simulation(
          std::move(*project), assertion_config.run.max_deltas, engine);
      CapturedAssertion captured;
      captured.compiled_processes =
          simulation.compiled_process_count();
      captured.compiled_modules = simulation.compiled_module_count();
      try {
        (void)simulation.run();
      } catch (const fsim::runtime::simir::AssertionError& error) {
        captured.process = error.process();
        captured.instruction = error.instruction();
        captured.severity = error.severity();
        captured.source = error.source();
        captured.message = error.what();
        return captured;
      }
      throw std::runtime_error("false assertion completed successfully");
    };
const auto assertion_reference = capture_assertion(
    fsim::project::Optimization::o0,
    fsim::app::SimulationEngine::interpreter);
const auto compare_assertion =
    [&](const CapturedAssertion& candidate) {
      assert(candidate.process == assertion_reference.process);
      assert(candidate.instruction == assertion_reference.instruction);
      assert(candidate.severity == assertion_reference.severity);
      assert(candidate.source.path == assertion_reference.source.path);
      assert(candidate.source.line == assertion_reference.source.line);
      assert(candidate.source.column == assertion_reference.source.column);
      assert(candidate.message == assertion_reference.message);
    };
assert(
    assertion_reference.severity
    == fsim::runtime::simir::AssertionSeverity::failure);
assert(same_source_path(
    assertion_reference.source.path, assertion_source));
assert(assertion_reference.source.line == 9);
assert(assertion_reference.source.column == 5);
assert(
    assertion_reference.message.find("cross-engine mismatch")
    != std::string::npos);
assert(assertion_reference.compiled_processes == 0);
assert(assertion_reference.compiled_modules == 0);
const auto assertion_o0 = capture_assertion(
    fsim::project::Optimization::o0,
    fsim::app::SimulationEngine::compiled);
const auto assertion_o2 = capture_assertion(
    fsim::project::Optimization::o2,
    fsim::app::SimulationEngine::compiled);
compare_assertion(assertion_o0);
compare_assertion(assertion_o2);
#if defined(FSIM_HAS_LLVM)
assert(assertion_o0.compiled_processes == 1);
assert(assertion_o0.compiled_modules == 1);
assert(assertion_o2.compiled_processes == 1);
assert(assertion_o2.compiled_modules == 1);
#else
assert(assertion_o0.compiled_processes == 0);
assert(assertion_o0.compiled_modules == 0);
assert(assertion_o2.compiled_processes == 0);
assert(assertion_o2.compiled_modules == 0);
#endif

auto scheduled_config = config;
scheduled_config.project.name = "scheduled-write-test";
scheduled_config.project.top = "sv:work.scheduled";
scheduled_config.build.optimization =
    fsim::project::Optimization::o2;
scheduled_config.build.cache_path =
    directory / "scheduled-write-cache";
scheduled_config.source_sets.clear();
fsim::project::SourceSet scheduled_sources;
scheduled_sources.language =
    fsim::project::Language::system_verilog;
scheduled_sources.standard = "2017";
scheduled_sources.library = "work";
scheduled_sources.files.push_back(scheduled_source);
scheduled_config.source_sets.push_back(
    std::move(scheduled_sources));
fsim::diagnostic::Engine scheduled_diagnostics;
auto scheduled_reference_project =
    fsim::app::build_project(
        scheduled_config, scheduled_diagnostics);
auto scheduled_hybrid_project =
    fsim::app::build_project(
        scheduled_config, scheduled_diagnostics);
assert(scheduled_reference_project);
assert(scheduled_hybrid_project);
const auto scheduled_reference = capture_simulation(
    std::move(*scheduled_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto scheduled_hybrid = capture_simulation(
    std::move(*scheduled_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    scheduled_reference, scheduled_hybrid);
assert(scheduled_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
assert(scheduled_hybrid.compiled_processes == 1);
assert(scheduled_hybrid.compiled_modules == 1);
#endif
assert(
    scheduled_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(scheduled_hybrid.result.time == 3);
assert(scheduled_hybrid.final_values.size() == 1);
assert(scheduled_hybrid.final_values.front() == "1");
assert(scheduled_hybrid.changes.size() == 2);
assert(
    std::get<1>(scheduled_hybrid.changes.front())
    == "0");
assert(
    std::get<2>(scheduled_hybrid.changes.front())
    == 0);
assert(
    std::get<3>(scheduled_hybrid.changes.front())
    == 0);
assert(
    std::get<1>(scheduled_hybrid.changes.back())
    == "1");
assert(
    std::get<2>(scheduled_hybrid.changes.back())
    == 2);
assert(
    std::get<3>(scheduled_hybrid.changes.back())
    == 0);

auto overflow_config = scheduled_config;
overflow_config.project.name =
    "scheduled-write-overflow-test";
overflow_config.project.top =
    "sv:work.scheduled_overflow";
overflow_config.build.cache_path =
    directory / "scheduled-write-overflow-cache";
for (const auto engine : {
         fsim::app::SimulationEngine::interpreter,
         fsim::app::SimulationEngine::compiled}) {
  fsim::diagnostic::Engine overflow_diagnostics;
  auto overflow_project = fsim::app::build_project(
      overflow_config, overflow_diagnostics);
  assert(overflow_project);
  fsim::app::Simulation overflow_simulation(
      std::move(*overflow_project),
      overflow_config.run.max_deltas,
      engine);
#if defined(FSIM_HAS_LLVM)
  assert(
      overflow_simulation.compiled_process_count()
      == (engine == fsim::app::SimulationEngine::compiled
              ? 1U
              : 0U));
#endif
  const auto overflow_q =
      overflow_simulation.find_signal("q");
  assert(overflow_q);
  bool overflow_thrown = false;
  try {
    (void)overflow_simulation.run();
  } catch (const std::overflow_error& exception) {
    overflow_thrown =
        std::string_view{exception.what()}
        == "simulation time overflow while scheduling event";
  }
  assert(overflow_thrown);
  assert(overflow_simulation.poisoned());
  assert(!overflow_simulation.finished());
  assert(overflow_simulation.now() == 1);
  assert(
      overflow_simulation.read_signal(*overflow_q)
          .to_msb_string()
      == "X");
}

auto sensitivity_config = config;
sensitivity_config.project.name =
    "sensitivity-wakeup-test";
sensitivity_config.project.top =
    "sv:work.sensitivity";
sensitivity_config.build.optimization =
    fsim::project::Optimization::o2;
sensitivity_config.build.cache_path =
    directory / "sensitivity-cache";
sensitivity_config.source_sets.clear();
fsim::project::SourceSet sensitivity_sources;
sensitivity_sources.language =
    fsim::project::Language::system_verilog;
sensitivity_sources.standard = "2017";
sensitivity_sources.library = "work";
sensitivity_sources.files.push_back(sensitivity_source);
sensitivity_config.source_sets.push_back(
    std::move(sensitivity_sources));
fsim::diagnostic::Engine sensitivity_diagnostics;
auto sensitivity_reference_project =
    fsim::app::build_project(
        sensitivity_config, sensitivity_diagnostics);
auto sensitivity_hybrid_project =
    fsim::app::build_project(
        sensitivity_config, sensitivity_diagnostics);
assert(sensitivity_reference_project);
assert(sensitivity_hybrid_project);
const auto sensitivity_trigger =
    sensitivity_reference_project->design.find_signal(
        "sensitivity.trigger");
const auto sensitivity_observed =
    sensitivity_reference_project->design.find_signal(
        "sensitivity.observed");
const auto sensitivity_dynamic_observed =
    sensitivity_reference_project->design.find_signal(
        "sensitivity.dynamic_observed");
assert(sensitivity_trigger);
assert(sensitivity_observed);
assert(sensitivity_dynamic_observed);
const auto sensitivity_reference = capture_simulation(
    std::move(*sensitivity_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto sensitivity_hybrid = capture_simulation(
    std::move(*sensitivity_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    sensitivity_reference, sensitivity_hybrid);
assert(sensitivity_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
assert(sensitivity_hybrid.compiled_processes == 3);
assert(sensitivity_hybrid.compiled_modules == 1);
#endif
assert(
    sensitivity_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(sensitivity_hybrid.result.time == 3);
const decltype(sensitivity_reference.changes)
    expected_sensitivity_changes = {
        {*sensitivity_trigger, "0", 0, 0},
        {*sensitivity_trigger, "1", 1, 0},
        {*sensitivity_dynamic_observed, "1", 1, 1},
        {*sensitivity_observed, "1", 1, 1},
        {*sensitivity_trigger, "0", 2, 0},
        {*sensitivity_dynamic_observed, "0", 2, 1},
    };
assert(
    sensitivity_reference.changes
    == expected_sensitivity_changes);
assert(
    sensitivity_hybrid.changes
    == expected_sensitivity_changes);
assert((
    sensitivity_hybrid.final_values
    == std::vector<std::string>{"0", "1", "0"}));
#if defined(FSIM_HAS_LLVM)
assert(sensitivity_hybrid.native_cache.hits == 0);
assert(sensitivity_hybrid.native_cache.misses == 1);
assert(sensitivity_hybrid.native_cache.stores == 1);
auto sensitivity_warm_project =
    fsim::app::build_project(
        sensitivity_config, sensitivity_diagnostics);
assert(sensitivity_warm_project);
const auto sensitivity_warm = capture_simulation(
    std::move(*sensitivity_warm_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    sensitivity_reference, sensitivity_warm);
assert(sensitivity_warm.compiled_processes == 3);
assert(sensitivity_warm.compiled_modules == 1);
assert(sensitivity_warm.native_cache.hits == 1);
assert(sensitivity_warm.native_cache.misses == 0);
assert(sensitivity_warm.native_cache.stores == 0);
#endif

auto vhdl_wait_config = config;
vhdl_wait_config.project.name = "vhdl-explicit-wait-test";
vhdl_wait_config.project.top = "vhdl:work.vhdl_wait(rtl)";
vhdl_wait_config.build.optimization =
    fsim::project::Optimization::o2;
vhdl_wait_config.build.cache_path =
    directory / "vhdl-wait-cache";
vhdl_wait_config.source_sets.clear();
fsim::project::SourceSet vhdl_wait_sources;
vhdl_wait_sources.language = fsim::project::Language::vhdl;
vhdl_wait_sources.standard = "2008";
vhdl_wait_sources.library = "work";
vhdl_wait_sources.files.push_back(vhdl_wait_source);
vhdl_wait_config.source_sets.push_back(
    std::move(vhdl_wait_sources));
fsim::diagnostic::Engine vhdl_wait_diagnostics;
auto vhdl_wait_reference_project =
    fsim::app::build_project(
        vhdl_wait_config, vhdl_wait_diagnostics);
auto vhdl_wait_hybrid_project =
    fsim::app::build_project(
        vhdl_wait_config, vhdl_wait_diagnostics);
assert(vhdl_wait_reference_project);
assert(vhdl_wait_hybrid_project);
const auto vhdl_wait_q =
    vhdl_wait_reference_project->design.find_signal(
        "vhdl_wait.q");
assert(vhdl_wait_q);
const auto vhdl_wait_reference = capture_simulation(
    std::move(*vhdl_wait_reference_project),
    fsim::app::SimulationEngine::interpreter,
    4);
const auto vhdl_wait_hybrid = capture_simulation(
    std::move(*vhdl_wait_hybrid_project),
    fsim::app::SimulationEngine::compiled,
    4);
compare_captures(vhdl_wait_reference, vhdl_wait_hybrid);
assert(
    vhdl_wait_hybrid.result.status
    == fsim::runtime::RunStatus::time_limit);
assert(vhdl_wait_hybrid.result.time == 4);
assert(vhdl_wait_hybrid.process_count == 1);
#if defined(FSIM_HAS_LLVM)
assert(vhdl_wait_hybrid.compiled_processes == 1);
assert(vhdl_wait_hybrid.compiled_modules == 1);
#endif
const decltype(vhdl_wait_reference.changes)
    expected_vhdl_wait_changes = {
        {*vhdl_wait_q, "0", 0, 0},
        {*vhdl_wait_q, "1", 1, 0},
        {*vhdl_wait_q, "0", 2, 0},
        {*vhdl_wait_q, "1", 3, 0},
        {*vhdl_wait_q, "0", 4, 0},
    };
assert(
    vhdl_wait_reference.changes
    == expected_vhdl_wait_changes);
assert(
    vhdl_wait_hybrid.changes
    == expected_vhdl_wait_changes);
assert((
    vhdl_wait_hybrid.final_values
    == std::vector<std::string>{"0"}));

auto wildcard_config = config;
wildcard_config.project.name = "wildcard-sensitivity-test";
wildcard_config.project.top = "sv:work.wildcard_app";
wildcard_config.build.optimization =
    fsim::project::Optimization::o2;
wildcard_config.build.cache_path =
    directory / "wildcard-cache";
wildcard_config.source_sets.clear();
fsim::project::SourceSet wildcard_sources;
wildcard_sources.language =
    fsim::project::Language::system_verilog;
wildcard_sources.standard = "2017";
wildcard_sources.library = "work";
wildcard_sources.files.push_back(wildcard_source);
wildcard_config.source_sets.push_back(
    std::move(wildcard_sources));
fsim::diagnostic::Engine wildcard_diagnostics;
auto wildcard_reference_project =
    fsim::app::build_project(
        wildcard_config, wildcard_diagnostics);
auto wildcard_hybrid_project =
    fsim::app::build_project(
        wildcard_config, wildcard_diagnostics);
assert(wildcard_reference_project);
assert(wildcard_hybrid_project);
const auto wildcard_a =
    wildcard_reference_project->design.find_signal(
        "wildcard_app.a");
const auto wildcard_q =
    wildcard_reference_project->design.find_signal(
        "wildcard_app.q");
const auto wildcard_y =
    wildcard_reference_project->design.find_signal(
        "wildcard_app.y");
const auto wildcard_latched =
    wildcard_reference_project->design.find_signal(
        "wildcard_app.latched");
assert(
    wildcard_a && wildcard_q && wildcard_y
    && wildcard_latched);
const auto wildcard_reference = capture_simulation(
    std::move(*wildcard_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto wildcard_hybrid = capture_simulation(
    std::move(*wildcard_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(wildcard_reference, wildcard_hybrid);
assert(
    wildcard_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(wildcard_hybrid.result.time == 2);
assert(wildcard_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
assert(wildcard_hybrid.compiled_processes == 4);
assert(wildcard_hybrid.compiled_modules == 1);
#endif
const decltype(wildcard_reference.changes)
    expected_wildcard_changes = {
        {*wildcard_a, "0", 0, 0},
        {*wildcard_q, "0", 0, 1},
        {*wildcard_y, "1", 0, 2},
        {*wildcard_a, "1", 1, 0},
        {*wildcard_q, "1", 1, 1},
        {*wildcard_latched, "1", 1, 1},
        {*wildcard_y, "0", 1, 2},
    };
assert(
    wildcard_reference.changes
    == expected_wildcard_changes);
assert(
    wildcard_hybrid.changes
    == expected_wildcard_changes);
assert((
    wildcard_hybrid.final_values
    == std::vector<std::string>{"1", "1", "0", "1"}));

auto case_config = config;
case_config.project.name = "case-statement-test";
case_config.project.top = "sv:work.case_app";
case_config.build.optimization =
    fsim::project::Optimization::o2;
case_config.build.cache_path = directory / "case-cache";
case_config.source_sets.clear();
fsim::project::SourceSet case_sources;
case_sources.language =
    fsim::project::Language::system_verilog;
case_sources.standard = "2017";
case_sources.library = "work";
case_sources.files.push_back(case_source);
case_config.source_sets.push_back(std::move(case_sources));
fsim::diagnostic::Engine case_diagnostics;
auto case_reference_project =
    fsim::app::build_project(case_config, case_diagnostics);
auto case_hybrid_project =
    fsim::app::build_project(case_config, case_diagnostics);
assert(case_reference_project);
assert(case_hybrid_project);
const auto case_selector =
    case_reference_project->design.find_signal(
        "case_app.selector");
const auto case_result =
    case_reference_project->design.find_signal(
        "case_app.result");
assert(case_selector && case_result);
const auto case_reference = capture_simulation(
    std::move(*case_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto case_hybrid = capture_simulation(
    std::move(*case_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(case_reference, case_hybrid);
assert(
    case_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(case_hybrid.result.time == 5);
assert(case_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(case_hybrid.compiled_processes == 2);
assert(case_hybrid.compiled_modules == 1);
#endif
assert((
    case_hybrid.final_values
    == std::vector<std::string>{
        "11", "00", "1", "1", "1", "1", "0"}));
assert(std::find(
           case_hybrid.changes.begin(),
           case_hybrid.changes.end(),
           std::tuple{
               *case_result, std::string{"10"},
               fsim::runtime::SimulationTick{2},
               std::uint64_t{1}})
       != case_hybrid.changes.end());
assert(std::find(
           case_hybrid.changes.begin(),
           case_hybrid.changes.end(),
           std::tuple{
               *case_result, std::string{"11"},
               fsim::runtime::SimulationTick{3},
               std::uint64_t{1}})
       != case_hybrid.changes.end());

auto conditional_config = config;
conditional_config.project.name =
    "conditional-expression-test";
conditional_config.project.top =
    "sv:work.conditional_app";
conditional_config.build.optimization =
    fsim::project::Optimization::o2;
conditional_config.build.cache_path =
    directory / "conditional-cache";
conditional_config.source_sets.clear();
fsim::project::SourceSet conditional_sources;
conditional_sources.language =
    fsim::project::Language::system_verilog;
conditional_sources.standard = "2017";
conditional_sources.library = "work";
conditional_sources.files.push_back(conditional_source);
conditional_config.source_sets.push_back(
    std::move(conditional_sources));
fsim::diagnostic::Engine conditional_diagnostics;
auto conditional_reference_project =
    fsim::app::build_project(
        conditional_config, conditional_diagnostics);
auto conditional_hybrid_project =
    fsim::app::build_project(
        conditional_config, conditional_diagnostics);
assert(conditional_reference_project);
assert(conditional_hybrid_project);
const auto conditional_reference = capture_simulation(
    std::move(*conditional_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto conditional_hybrid = capture_simulation(
    std::move(*conditional_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    conditional_reference, conditional_hybrid);
assert(
    conditional_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(conditional_hybrid.result.time == 4);
assert(conditional_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(conditional_hybrid.compiled_processes == 2);
assert(conditional_hybrid.compiled_modules == 1);
#endif
assert((
    conditional_hybrid.final_values
    == std::vector<std::string>{
        "Z", "101Z", "100Z", "10XZ"}));

auto comparison_config = config;
comparison_config.project.name =
    "comparison-expression-test";
comparison_config.project.top = "sv:work.comparison_app";
comparison_config.build.optimization =
    fsim::project::Optimization::o2;
comparison_config.build.cache_path =
    directory / "comparison-cache";
comparison_config.source_sets.clear();
fsim::project::SourceSet comparison_sources;
comparison_sources.language =
    fsim::project::Language::system_verilog;
comparison_sources.standard = "2017";
comparison_sources.library = "work";
comparison_sources.files.push_back(comparison_source);
comparison_config.source_sets.push_back(
    std::move(comparison_sources));
fsim::diagnostic::Engine comparison_diagnostics;
auto comparison_reference_project =
    fsim::app::build_project(
        comparison_config, comparison_diagnostics);
auto comparison_hybrid_project =
    fsim::app::build_project(
        comparison_config, comparison_diagnostics);
assert(comparison_reference_project);
assert(comparison_hybrid_project);
const auto comparison_reference = capture_simulation(
    std::move(*comparison_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto comparison_hybrid = capture_simulation(
    std::move(*comparison_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(comparison_reference, comparison_hybrid);
assert(
    comparison_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(comparison_hybrid.result.time == 4);
assert(comparison_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(comparison_hybrid.compiled_processes == 2);
assert(comparison_hybrid.compiled_modules == 1);
#endif
assert((
    comparison_hybrid.final_values
    == std::vector<std::string>{
        "01Z0", "0011", "X", "X", "X", "X", "X", "0",
        "0", "1", "1", "X"}));

auto logical_config = config;
logical_config.project.name = "logical-expression-test";
logical_config.project.top = "sv:work.logical_app";
logical_config.build.optimization =
    fsim::project::Optimization::o2;
logical_config.build.cache_path =
    directory / "logical-cache";
logical_config.source_sets.clear();
fsim::project::SourceSet logical_sources;
logical_sources.language =
    fsim::project::Language::system_verilog;
logical_sources.standard = "2017";
logical_sources.library = "work";
logical_sources.files.push_back(logical_source);
logical_config.source_sets.push_back(
    std::move(logical_sources));
fsim::diagnostic::Engine logical_diagnostics;
auto logical_reference_project =
    fsim::app::build_project(
        logical_config, logical_diagnostics);
auto logical_hybrid_project =
    fsim::app::build_project(
        logical_config, logical_diagnostics);
assert(logical_reference_project);
assert(logical_hybrid_project);
const auto logical_reference = capture_simulation(
    std::move(*logical_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto logical_hybrid = capture_simulation(
    std::move(*logical_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(logical_reference, logical_hybrid);
assert(
    logical_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(logical_hybrid.result.time == 5);
assert(logical_hybrid.process_count == 11);
#if defined(FSIM_HAS_LLVM)
assert(logical_hybrid.compiled_processes == 11);
assert(logical_hybrid.compiled_modules == 1);
#endif
assert((
    logical_hybrid.final_values
    == std::vector<std::string>{
        "0010", "01", "001", "1", "1",
        "0", "1", "1", "0100", "0001",
        "0", "1", "1", "0", "1", "1", "0", "0", "1"}));

auto arithmetic_config = config;
arithmetic_config.project.name = "arithmetic-expression-test";
arithmetic_config.project.top = "sv:work.arithmetic_app";
arithmetic_config.build.optimization =
    fsim::project::Optimization::o2;
arithmetic_config.build.cache_path =
    directory / "arithmetic-cache";
arithmetic_config.source_sets.clear();
fsim::project::SourceSet arithmetic_sources;
arithmetic_sources.language =
    fsim::project::Language::system_verilog;
arithmetic_sources.standard = "2017";
arithmetic_sources.library = "work";
arithmetic_sources.files.push_back(arithmetic_source);
arithmetic_config.source_sets.push_back(
    std::move(arithmetic_sources));
fsim::diagnostic::Engine arithmetic_diagnostics;
auto arithmetic_reference_project =
    fsim::app::build_project(
        arithmetic_config, arithmetic_diagnostics);
auto arithmetic_hybrid_project =
    fsim::app::build_project(
        arithmetic_config, arithmetic_diagnostics);
assert(arithmetic_reference_project);
assert(arithmetic_hybrid_project);
const auto arithmetic_reference = capture_simulation(
    std::move(*arithmetic_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto arithmetic_hybrid = capture_simulation(
    std::move(*arithmetic_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(arithmetic_reference, arithmetic_hybrid);
assert(
    arithmetic_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(arithmetic_hybrid.result.time == 4);
assert(arithmetic_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(arithmetic_hybrid.compiled_processes == 2);
assert(arithmetic_hybrid.compiled_modules == 1);
#endif
assert((
    arithmetic_hybrid.final_values
    == std::vector<std::string>{
        "11001000", "00000111", "11000001", "01111000",
        "00011100", "00000100", "11001000", "00111000",
        "00000101", "11111101", "00000010", "00001000",
        "11110001", "11111111", "00000010", "0"}));

auto select_concat_config = config;
select_concat_config.project.name = "select-concat-test";
select_concat_config.project.top = "sv:work.select_concat_app";
select_concat_config.build.optimization =
    fsim::project::Optimization::o2;
select_concat_config.build.cache_path =
    directory / "select-concat-cache";
select_concat_config.source_sets.clear();
fsim::project::SourceSet select_concat_sources;
select_concat_sources.language =
    fsim::project::Language::system_verilog;
select_concat_sources.standard = "2017";
select_concat_sources.library = "work";
select_concat_sources.files.push_back(
    select_concat_source);
select_concat_config.source_sets.push_back(
    std::move(select_concat_sources));
fsim::diagnostic::Engine select_concat_diagnostics;
auto select_concat_reference_project =
    fsim::app::build_project(
        select_concat_config, select_concat_diagnostics);
auto select_concat_hybrid_project =
    fsim::app::build_project(
        select_concat_config, select_concat_diagnostics);
assert(select_concat_reference_project);
assert(select_concat_hybrid_project);
const auto select_concat_reference = capture_simulation(
    std::move(*select_concat_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto select_concat_hybrid = capture_simulation(
    std::move(*select_concat_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    select_concat_reference, select_concat_hybrid);
assert(
    select_concat_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(select_concat_hybrid.result.time == 2);
assert(select_concat_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(select_concat_hybrid.compiled_processes == 2);
assert(select_concat_hybrid.compiled_modules == 1);
#endif
assert((
    select_concat_hybrid.final_values
    == std::vector<std::string>{
        "Z10100X1", "1100XZ01", "0", "0", "0",
        "Z101", "00XZ", "Z1010XZ01", "10XZ1111",
        "XZ10"}));

auto vhdl_select_concat_config = config;
vhdl_select_concat_config.project.name =
    "vhdl-select-concat-test";
vhdl_select_concat_config.project.top =
    "vhdl:work.vhdl_select_concat_app(rtl)";
vhdl_select_concat_config.build.optimization =
    fsim::project::Optimization::o2;
vhdl_select_concat_config.build.cache_path =
    directory / "vhdl-select-concat-cache";
vhdl_select_concat_config.source_sets.clear();
fsim::project::SourceSet vhdl_select_concat_sources;
vhdl_select_concat_sources.language =
    fsim::project::Language::vhdl;
vhdl_select_concat_sources.standard = "2008";
vhdl_select_concat_sources.library = "work";
vhdl_select_concat_sources.files.push_back(
    vhdl_select_concat_source);
vhdl_select_concat_config.source_sets.push_back(
    std::move(vhdl_select_concat_sources));
fsim::diagnostic::Engine vhdl_select_concat_diagnostics;
auto vhdl_select_concat_reference_project =
    fsim::app::build_project(
        vhdl_select_concat_config,
        vhdl_select_concat_diagnostics);
auto vhdl_select_concat_hybrid_project =
    fsim::app::build_project(
        vhdl_select_concat_config,
        vhdl_select_concat_diagnostics);
assert(vhdl_select_concat_reference_project);
assert(vhdl_select_concat_hybrid_project);
const auto vhdl_select_concat_reference =
    capture_simulation(
        std::move(*vhdl_select_concat_reference_project),
        fsim::app::SimulationEngine::interpreter);
const auto vhdl_select_concat_hybrid =
    capture_simulation(
        std::move(*vhdl_select_concat_hybrid_project),
        fsim::app::SimulationEngine::compiled);
compare_captures(
    vhdl_select_concat_reference,
    vhdl_select_concat_hybrid);
assert(
    vhdl_select_concat_hybrid.result.status
    == fsim::runtime::RunStatus::completed);
assert(vhdl_select_concat_hybrid.result.time == 5);
assert(vhdl_select_concat_hybrid.process_count == 4);
#if defined(FSIM_HAS_LLVM)
assert(vhdl_select_concat_hybrid.compiled_processes == 4);
assert(vhdl_select_concat_hybrid.compiled_modules == 1);
#endif
assert((
    vhdl_select_concat_hybrid.final_values
    == std::vector<std::string>{
        "1XZ0", "01Z1", "Z", "Z", "Z", "X",
        "1X", "1Z", "1X10Z1", "10XZ1110",
        "XZ10"}));

auto vhdl_signed_config = config;
vhdl_signed_config.project.name = "vhdl-signed-test";
vhdl_signed_config.project.top =
    "vhdl:work.vhdl_signed_app(rtl)";
vhdl_signed_config.build.optimization =
    fsim::project::Optimization::o2;
vhdl_signed_config.build.cache_path =
    directory / "vhdl-signed-cache";
vhdl_signed_config.source_sets.clear();
fsim::project::SourceSet vhdl_signed_sources;
vhdl_signed_sources.language =
    fsim::project::Language::vhdl;
vhdl_signed_sources.standard = "2008";
vhdl_signed_sources.library = "work";
vhdl_signed_sources.files.push_back(vhdl_signed_source);
vhdl_signed_config.source_sets.push_back(
    std::move(vhdl_signed_sources));
fsim::diagnostic::Engine vhdl_signed_diagnostics;
auto vhdl_signed_reference_project =
    fsim::app::build_project(
        vhdl_signed_config, vhdl_signed_diagnostics);
auto vhdl_signed_hybrid_project =
    fsim::app::build_project(
        vhdl_signed_config, vhdl_signed_diagnostics);
assert(vhdl_signed_reference_project);
assert(vhdl_signed_hybrid_project);
const auto vhdl_signed_reference = capture_simulation(
    std::move(*vhdl_signed_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto vhdl_signed_hybrid = capture_simulation(
    std::move(*vhdl_signed_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    vhdl_signed_reference, vhdl_signed_hybrid);
assert(
    vhdl_signed_hybrid.result.status
    == fsim::runtime::RunStatus::completed);
assert(vhdl_signed_hybrid.result.time == 0);
assert(vhdl_signed_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
assert(vhdl_signed_hybrid.compiled_processes == 3);
assert(vhdl_signed_hybrid.compiled_modules == 1);
#endif
assert((
    vhdl_signed_hybrid.final_values
    == std::vector<std::string>{
        "11111011", "00000011", "11111110", "11111000",
        "11110001", "11111111", "11111110", "00000001",
        "1", "11110110", "01111101", "11111101"}));

auto conditional_statement_config = config;
conditional_statement_config.project.name =
    "conditional-statement-test";
conditional_statement_config.project.top =
    "sv:work.conditional_statement_app";
conditional_statement_config.build.cache_path =
    directory / "conditional-statement-cache";
conditional_statement_config.source_sets.clear();
fsim::project::SourceSet conditional_statement_sources;
conditional_statement_sources.language =
    fsim::project::Language::system_verilog;
conditional_statement_sources.standard = "2017";
conditional_statement_sources.library = "work";
conditional_statement_sources.files.push_back(
    conditional_statement_source);
conditional_statement_config.source_sets.push_back(
    std::move(conditional_statement_sources));
for (const auto optimization : {
         fsim::project::Optimization::o0,
         fsim::project::Optimization::o2}) {
  conditional_statement_config.build.optimization =
      optimization;
  fsim::diagnostic::Engine conditional_statement_diagnostics;
  auto conditional_statement_reference_project =
      fsim::app::build_project(
          conditional_statement_config,
          conditional_statement_diagnostics);
  auto conditional_statement_hybrid_project =
      fsim::app::build_project(
          conditional_statement_config,
          conditional_statement_diagnostics);
  if (!conditional_statement_reference_project
      || !conditional_statement_hybrid_project) {
    fsim::diagnostic::print_text(
        std::cerr, conditional_statement_diagnostics);
  }
  assert(conditional_statement_reference_project);
  assert(conditional_statement_hybrid_project);
  const auto conditional_statement_reference =
      capture_simulation(
          std::move(*conditional_statement_reference_project),
          fsim::app::SimulationEngine::interpreter);
  const auto conditional_statement_hybrid =
      capture_simulation(
          std::move(*conditional_statement_hybrid_project),
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      conditional_statement_reference,
      conditional_statement_hybrid);
  assert(
      conditional_statement_hybrid.result.status
      == fsim::runtime::RunStatus::stopped);
  assert(conditional_statement_hybrid.result.time == 3);
  assert(conditional_statement_hybrid.process_count == 5);
#if defined(FSIM_HAS_LLVM)
  assert(conditional_statement_hybrid.compiled_processes == 5);
  assert(conditional_statement_hybrid.compiled_modules == 1);
#endif
  if (conditional_statement_hybrid.final_values
      != std::vector<std::string>{
          "1000", "0", "1", "0", "0001",
          "0011", "00", "011", "011", "0",
          "0011", "0101", "0100", "0001",
          "1", "1", "0"}) {
    for (const auto& value :
         conditional_statement_hybrid.final_values) {
      std::cerr << value << ' ';
    }
    std::cerr << '\n';
  }
  assert((
      conditional_statement_hybrid.final_values
      == std::vector<std::string>{
          "1000", "0", "1", "0", "0001",
          "0011", "00", "011", "011", "0",
          "0011", "0101", "0100", "0001",
          "1", "1", "0"}));
}

auto vhdl_conditional_statement_config = config;
vhdl_conditional_statement_config.project.name =
    "vhdl-conditional-statement-test";
vhdl_conditional_statement_config.project.top =
    "vhdl:work.vhdl_conditional_statement_app(rtl)";
vhdl_conditional_statement_config.build.cache_path =
    directory / "vhdl-conditional-statement-cache";
vhdl_conditional_statement_config.source_sets.clear();
fsim::project::SourceSet vhdl_conditional_statement_sources;
vhdl_conditional_statement_sources.language =
    fsim::project::Language::vhdl;
vhdl_conditional_statement_sources.standard = "2008";
vhdl_conditional_statement_sources.library = "work";
vhdl_conditional_statement_sources.files.push_back(
    vhdl_conditional_statement_source);
vhdl_conditional_statement_config.source_sets.push_back(
    std::move(vhdl_conditional_statement_sources));
for (const auto optimization : {
         fsim::project::Optimization::o0,
         fsim::project::Optimization::o2}) {
  vhdl_conditional_statement_config.build.optimization =
      optimization;
  fsim::diagnostic::Engine vhdl_conditional_statement_diagnostics;
  auto vhdl_conditional_statement_reference_project =
      fsim::app::build_project(
          vhdl_conditional_statement_config,
          vhdl_conditional_statement_diagnostics);
  auto vhdl_conditional_statement_hybrid_project =
      fsim::app::build_project(
          vhdl_conditional_statement_config,
          vhdl_conditional_statement_diagnostics);
  assert(vhdl_conditional_statement_reference_project);
  assert(vhdl_conditional_statement_hybrid_project);
  const auto vhdl_conditional_statement_reference =
      capture_simulation(
          std::move(*vhdl_conditional_statement_reference_project),
          fsim::app::SimulationEngine::interpreter);
  const auto vhdl_conditional_statement_hybrid =
      capture_simulation(
          std::move(*vhdl_conditional_statement_hybrid_project),
          fsim::app::SimulationEngine::compiled);
  compare_captures(
      vhdl_conditional_statement_reference,
      vhdl_conditional_statement_hybrid);
  assert(
      vhdl_conditional_statement_hybrid.result.status
      == fsim::runtime::RunStatus::completed);
  assert(vhdl_conditional_statement_hybrid.result.time == 4);
  assert(vhdl_conditional_statement_hybrid.process_count == 3);
#if defined(FSIM_HAS_LLVM)
  assert(
      vhdl_conditional_statement_hybrid.compiled_processes
      == 3);
  assert(
      vhdl_conditional_statement_hybrid.compiled_modules
      == 1);
#endif
  if (vhdl_conditional_statement_hybrid.final_values
      != std::vector<std::string>{
          "U", "1", "1", "1", "1", "01",
          "0011", "00", "1", "1", "1", "1", "1",
          "1", "0", "1", "1", "1", "0"}) {
    for (const auto& value :
         vhdl_conditional_statement_hybrid.final_values) {
      std::cerr << value << ' ';
    }
    std::cerr << '\n';
  }
  assert((
      vhdl_conditional_statement_hybrid.final_values
      == std::vector<std::string>{
          "U", "1", "1", "1", "1", "01",
          "0011", "00", "1", "1", "1", "1", "1",
          "1", "0", "1", "1", "1", "0"}));
}

auto partial_group_config = config;
partial_group_config.project.name =
    "partial-specialization-group-test";
partial_group_config.project.top =
    "sv:work.partial_group";
partial_group_config.build.optimization =
    fsim::project::Optimization::o2;
partial_group_config.build.cache_path =
    directory / "partial-group-cache";
partial_group_config.source_sets.clear();
fsim::project::SourceSet partial_group_sources;
partial_group_sources.language =
    fsim::project::Language::system_verilog;
partial_group_sources.standard = "2017";
partial_group_sources.library = "work";
partial_group_sources.files.push_back(partial_group_source);
partial_group_config.source_sets.push_back(
    std::move(partial_group_sources));
fsim::diagnostic::Engine partial_group_diagnostics;
auto partial_group_reference_project =
    fsim::app::build_project(
        partial_group_config, partial_group_diagnostics);
auto partial_group_hybrid_project =
    fsim::app::build_project(
        partial_group_config, partial_group_diagnostics);
assert(partial_group_reference_project);
assert(partial_group_hybrid_project);
const auto partial_group_reference = capture_simulation(
    std::move(*partial_group_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto partial_group_hybrid = capture_simulation(
    std::move(*partial_group_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    partial_group_reference, partial_group_hybrid);
assert(partial_group_hybrid.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(partial_group_hybrid.compiled_processes == 1);
assert(partial_group_hybrid.compiled_modules == 1);
assert(partial_group_hybrid.native_cache.misses == 1);
assert(partial_group_hybrid.native_cache.stores == 1);
#endif
assert(
    partial_group_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(partial_group_hybrid.result.time == 1);

}

}  // namespace fsim::test
