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

void ApplicationTestFixture::test_mixed_language_and_generate() {
  auto config = base_config();
// Explicit mixed-language bindings carry construction actuals from the
// parent syntax into the selected foreign unit before boundary widths are
// checked. Exercise both hierarchy directions through the interpreter and
// the native specialization cache.
auto sv_to_vhdl_actual_config = config;
sv_to_vhdl_actual_config.project.name =
    "sv-to-vhdl-construction-actual-test";
sv_to_vhdl_actual_config.project.top =
    "sv:work.mixed_actual_sv_top";
sv_to_vhdl_actual_config.build.optimization =
    fsim::project::Optimization::o2;
sv_to_vhdl_actual_config.build.cache_path =
    directory / "sv-to-vhdl-construction-actual-cache";
sv_to_vhdl_actual_config.source_sets.clear();
fsim::project::SourceSet sv_to_vhdl_actual_vhdl_sources;
sv_to_vhdl_actual_vhdl_sources.language =
    fsim::project::Language::vhdl;
sv_to_vhdl_actual_vhdl_sources.standard = "2008";
sv_to_vhdl_actual_vhdl_sources.library = "work";
sv_to_vhdl_actual_vhdl_sources.compilation_unit = "file";
sv_to_vhdl_actual_vhdl_sources.files = {
    vhdl_generic_entity_source,
    vhdl_generic_architecture_source,
};
sv_to_vhdl_actual_config.source_sets.push_back(
    std::move(sv_to_vhdl_actual_vhdl_sources));
fsim::project::SourceSet sv_to_vhdl_actual_sv_sources;
sv_to_vhdl_actual_sv_sources.language =
    fsim::project::Language::system_verilog;
sv_to_vhdl_actual_sv_sources.standard = "2017";
sv_to_vhdl_actual_sv_sources.library = "work";
sv_to_vhdl_actual_sv_sources.compilation_unit = "file";
sv_to_vhdl_actual_sv_sources.files = {
    mixed_actual_sv_top_source};
sv_to_vhdl_actual_config.source_sets.push_back(
    std::move(sv_to_vhdl_actual_sv_sources));
sv_to_vhdl_actual_config.bindings = {
    {"mixed_actual_sv_top.child",
     "vhdl:work.vhdl_generic_child(rtl)",
     std::nullopt},
};
const auto run_sv_to_vhdl_actual =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          sv_to_vhdl_actual_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 2);
      assert(project->specialization_cache_keys.size() == 2);
      ParameterRun result;
      for (std::size_t index = 0;
           index < project->design.specializations().size();
           ++index) {
        const auto& specialization =
            project->design.specializations()[index];
        result.keys.emplace_back(
            specialization.instance,
            project->specialization_cache_keys[index]);
        if (specialization.instance
            == "mixed_actual_sv_top.child") {
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"width", "4"},
                  {"value", "5"},
                  {"last", "3"}}));
        }
      }
      result.simulation =
          capture_simulation(std::move(*project), engine);
      return result;
    };

const auto sv_to_vhdl_actual_reference =
    run_sv_to_vhdl_actual(
        fsim::app::SimulationEngine::interpreter);
const auto sv_to_vhdl_actual_cold =
    run_sv_to_vhdl_actual(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    sv_to_vhdl_actual_reference.simulation,
    sv_to_vhdl_actual_cold.simulation);
assert(
    sv_to_vhdl_actual_cold.simulation.result.status
    == fsim::runtime::RunStatus::stopped);
assert(sv_to_vhdl_actual_cold.simulation.result.time == 1);
assert((
    sv_to_vhdl_actual_cold.simulation.final_values
    == std::vector<std::string>{"0101"}));
assert(sv_to_vhdl_actual_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(
    sv_to_vhdl_actual_cold.simulation.compiled_processes
    == 2);
assert(
    sv_to_vhdl_actual_cold.simulation.compiled_modules == 2);
assert(
    sv_to_vhdl_actual_cold.simulation.native_cache.hits == 0);
assert(
    sv_to_vhdl_actual_cold.simulation.native_cache.misses
    == 2);
assert(
    sv_to_vhdl_actual_cold.simulation.native_cache.stores
    == 2);
#endif
const auto sv_to_vhdl_actual_warm =
    run_sv_to_vhdl_actual(
        fsim::app::SimulationEngine::compiled);
assert(
    sv_to_vhdl_actual_warm.keys
    == sv_to_vhdl_actual_cold.keys);
#if defined(FSIM_HAS_LLVM)
assert(
    sv_to_vhdl_actual_warm.simulation.native_cache.hits == 2);
assert(
    sv_to_vhdl_actual_warm.simulation.native_cache.misses
    == 0);
#endif

auto vhdl_to_sv_actual_config = config;
vhdl_to_sv_actual_config.project.name =
    "vhdl-to-sv-construction-actual-test";
vhdl_to_sv_actual_config.project.top =
    "vhdl:work.mixed_actual_vhdl_top(rtl)";
vhdl_to_sv_actual_config.build.optimization =
    fsim::project::Optimization::o2;
vhdl_to_sv_actual_config.build.cache_path =
    directory / "vhdl-to-sv-construction-actual-cache";
vhdl_to_sv_actual_config.source_sets.clear();
fsim::project::SourceSet vhdl_to_sv_actual_vhdl_sources;
vhdl_to_sv_actual_vhdl_sources.language =
    fsim::project::Language::vhdl;
vhdl_to_sv_actual_vhdl_sources.standard = "2008";
vhdl_to_sv_actual_vhdl_sources.library = "work";
vhdl_to_sv_actual_vhdl_sources.compilation_unit = "file";
vhdl_to_sv_actual_vhdl_sources.files = {
    mixed_actual_vhdl_top_source};
vhdl_to_sv_actual_config.source_sets.push_back(
    std::move(vhdl_to_sv_actual_vhdl_sources));
fsim::project::SourceSet vhdl_to_sv_actual_sv_sources;
vhdl_to_sv_actual_sv_sources.language =
    fsim::project::Language::system_verilog;
vhdl_to_sv_actual_sv_sources.standard = "2017";
vhdl_to_sv_actual_sv_sources.library = "work";
vhdl_to_sv_actual_sv_sources.compilation_unit = "file";
vhdl_to_sv_actual_sv_sources.files = {
    mixed_actual_sv_child_source};
vhdl_to_sv_actual_config.source_sets.push_back(
    std::move(vhdl_to_sv_actual_sv_sources));
vhdl_to_sv_actual_config.bindings = {
    {"mixed_actual_vhdl_top.child",
     "sv:work.mixed_actual_sv_child",
     std::nullopt},
};
const auto run_vhdl_to_sv_actual =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          vhdl_to_sv_actual_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 2);
      assert(project->specialization_cache_keys.size() == 2);
      ParameterRun result;
      for (std::size_t index = 0;
           index < project->design.specializations().size();
           ++index) {
        const auto& specialization =
            project->design.specializations()[index];
        result.keys.emplace_back(
            specialization.instance,
            project->specialization_cache_keys[index]);
        if (specialization.instance
            == "mixed_actual_vhdl_top.child") {
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"width", "4"},
                  {"value", "6"},
                  {"last", "3"}}));
        }
      }
      result.simulation =
          capture_simulation(std::move(*project), engine, 0);
      return result;
    };

const auto vhdl_to_sv_actual_reference =
    run_vhdl_to_sv_actual(
        fsim::app::SimulationEngine::interpreter);
const auto vhdl_to_sv_actual_cold =
    run_vhdl_to_sv_actual(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    vhdl_to_sv_actual_reference.simulation,
    vhdl_to_sv_actual_cold.simulation);
assert(
    vhdl_to_sv_actual_cold.simulation.result.status
    == fsim::runtime::RunStatus::time_limit);
assert(vhdl_to_sv_actual_cold.simulation.result.time == 0);
assert((
    vhdl_to_sv_actual_cold.simulation.final_values
    == std::vector<std::string>{"0110"}));
assert(vhdl_to_sv_actual_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(
    vhdl_to_sv_actual_cold.simulation.compiled_processes
    == 2);
assert(
    vhdl_to_sv_actual_cold.simulation.compiled_modules == 2);
assert(
    vhdl_to_sv_actual_cold.simulation.native_cache.hits == 0);
assert(
    vhdl_to_sv_actual_cold.simulation.native_cache.misses
    == 2);
assert(
    vhdl_to_sv_actual_cold.simulation.native_cache.stores
    == 2);
#endif
const auto vhdl_to_sv_actual_warm =
    run_vhdl_to_sv_actual(
        fsim::app::SimulationEngine::compiled);
assert(
    vhdl_to_sv_actual_warm.keys
    == vhdl_to_sv_actual_cold.keys);
#if defined(FSIM_HAS_LLVM)
assert(
    vhdl_to_sv_actual_warm.simulation.native_cache.hits == 2);
assert(
    vhdl_to_sv_actual_warm.simulation.native_cache.misses
    == 0);
#endif

// A selected generate branch contributes its label to the stable hierarchy
// path. Explicit bindings therefore address generated foreign instances
// without making language-dependent guesses.
auto generated_mixed_config = config;
generated_mixed_config.project.name =
    "generated-mixed-hierarchy-test";
generated_mixed_config.project.top =
    "sv:work.generated_mixed_sv_top";
generated_mixed_config.build.optimization =
    fsim::project::Optimization::o2;
generated_mixed_config.build.cache_path =
    directory / "generated-mixed-hierarchy-cache";
generated_mixed_config.source_sets.clear();
fsim::project::SourceSet generated_mixed_vhdl_sources;
generated_mixed_vhdl_sources.language =
    fsim::project::Language::vhdl;
generated_mixed_vhdl_sources.standard = "2008";
generated_mixed_vhdl_sources.library = "work";
generated_mixed_vhdl_sources.compilation_unit = "file";
generated_mixed_vhdl_sources.files = {
    vhdl_generic_entity_source,
    vhdl_generic_architecture_source,
};
generated_mixed_config.source_sets.push_back(
    std::move(generated_mixed_vhdl_sources));
fsim::project::SourceSet generated_mixed_sv_sources;
generated_mixed_sv_sources.language =
    fsim::project::Language::system_verilog;
generated_mixed_sv_sources.standard = "2017";
generated_mixed_sv_sources.library = "work";
generated_mixed_sv_sources.compilation_unit = "file";
generated_mixed_sv_sources.files = {
    generated_mixed_sv_top_source};
generated_mixed_config.source_sets.push_back(
    std::move(generated_mixed_sv_sources));
generated_mixed_config.bindings = {
    {"generated_mixed_sv_top.foreign_branch.child",
     "vhdl:work.vhdl_generic_child(rtl)",
     std::nullopt},
};
const auto run_generated_mixed =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          generated_mixed_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 2);
      assert(project->specialization_cache_keys.size() == 2);
      ParameterRun result;
      for (std::size_t index = 0;
           index < project->design.specializations().size();
           ++index) {
        const auto& specialization =
            project->design.specializations()[index];
        result.keys.emplace_back(
            specialization.instance,
            project->specialization_cache_keys[index]);
        if (specialization.instance
            == "generated_mixed_sv_top.foreign_branch.child") {
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"width", "4"},
                  {"value", "9"},
                  {"last", "3"}}));
        }
      }
      result.simulation =
          capture_simulation(std::move(*project), engine);
      return result;
    };

const auto generated_mixed_reference =
    run_generated_mixed(
        fsim::app::SimulationEngine::interpreter);
const auto generated_mixed_cold =
    run_generated_mixed(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    generated_mixed_reference.simulation,
    generated_mixed_cold.simulation);
assert(
    generated_mixed_cold.simulation.result.status
    == fsim::runtime::RunStatus::stopped);
assert(generated_mixed_cold.simulation.result.time == 1);
assert((
    generated_mixed_cold.simulation.final_values
    == std::vector<std::string>{"1001"}));
assert(generated_mixed_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(
    generated_mixed_cold.simulation.compiled_processes == 2);
assert(
    generated_mixed_cold.simulation.compiled_modules == 2);
assert(
    generated_mixed_cold.simulation.native_cache.hits == 0);
assert(
    generated_mixed_cold.simulation.native_cache.misses == 2);
assert(
    generated_mixed_cold.simulation.native_cache.stores == 2);
#endif
const auto generated_mixed_warm =
    run_generated_mixed(
        fsim::app::SimulationEngine::compiled);
assert(generated_mixed_warm.keys == generated_mixed_cold.keys);
#if defined(FSIM_HAS_LLVM)
assert(
    generated_mixed_warm.simulation.native_cache.hits == 2);
assert(
    generated_mixed_warm.simulation.native_cache.misses == 0);
#endif

// Loop-generate expansion is also specialization-owned: the loop variable
// becomes a constant construction actual and the indexed scope is stable
// enough to bind every selected foreign child explicitly.
auto generated_loop_config = config;
generated_loop_config.project.name =
    "generated-loop-mixed-hierarchy-test";
generated_loop_config.project.top =
    "sv:work.generated_loop_top";
generated_loop_config.build.optimization =
    fsim::project::Optimization::o2;
generated_loop_config.build.cache_path =
    directory / "generated-loop-mixed-hierarchy-cache";
generated_loop_config.source_sets.clear();
fsim::project::SourceSet generated_loop_vhdl_sources;
generated_loop_vhdl_sources.language =
    fsim::project::Language::vhdl;
generated_loop_vhdl_sources.standard = "2008";
generated_loop_vhdl_sources.library = "work";
generated_loop_vhdl_sources.compilation_unit = "file";
generated_loop_vhdl_sources.files = {
    generated_loop_vhdl_source};
generated_loop_config.source_sets.push_back(
    std::move(generated_loop_vhdl_sources));
fsim::project::SourceSet generated_loop_sv_sources;
generated_loop_sv_sources.language =
    fsim::project::Language::system_verilog;
generated_loop_sv_sources.standard = "2017";
generated_loop_sv_sources.library = "work";
generated_loop_sv_sources.compilation_unit = "file";
generated_loop_sv_sources.files = {
    generated_loop_sv_top_source};
generated_loop_config.source_sets.push_back(
    std::move(generated_loop_sv_sources));
generated_loop_config.bindings = {
    {"generated_loop_top.lanes[0].child",
     "vhdl:work.generated_loop_child(rtl)",
     std::nullopt},
    {"generated_loop_top.lanes[1].child",
     "vhdl:work.generated_loop_child(rtl)",
     std::nullopt},
    {"generated_loop_top.lanes[2].child",
     "vhdl:work.generated_loop_child(rtl)",
     std::nullopt},
};
const auto run_generated_loop =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          generated_loop_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 4);
      assert(project->specialization_cache_keys.size() == 4);
      ParameterRun result;
      for (std::size_t index = 0;
           index < project->design.specializations().size();
           ++index) {
        const auto& specialization =
            project->design.specializations()[index];
        result.keys.emplace_back(
            specialization.instance,
            project->specialization_cache_keys[index]);
        if (index > 0) {
          const auto lane = index - 1;
          assert(
              specialization.instance
              == "generated_loop_top.lanes["
                  + std::to_string(lane) + "].child");
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"value", std::to_string(lane + 5)}}));
        }
      }
      result.simulation =
          capture_simulation(std::move(*project), engine);
      return result;
    };

const auto generated_loop_reference =
    run_generated_loop(
        fsim::app::SimulationEngine::interpreter);
const auto generated_loop_cold =
    run_generated_loop(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    generated_loop_reference.simulation,
    generated_loop_cold.simulation);
assert(
    generated_loop_cold.simulation.result.status
    == fsim::runtime::RunStatus::stopped);
assert(generated_loop_cold.simulation.result.time == 1);
assert((
    generated_loop_cold.simulation.final_values
    == std::vector<std::string>{"0101", "0110", "0111"}));
assert(generated_loop_cold.simulation.process_count == 4);
#if defined(FSIM_HAS_LLVM)
assert(
    generated_loop_cold.simulation.compiled_processes == 4);
assert(
    generated_loop_cold.simulation.compiled_modules == 4);
assert(
    generated_loop_cold.simulation.native_cache.hits == 0);
assert(
    generated_loop_cold.simulation.native_cache.misses == 4);
assert(
    generated_loop_cold.simulation.native_cache.stores == 4);
#endif
const auto generated_loop_warm =
    run_generated_loop(
        fsim::app::SimulationEngine::compiled);
assert(generated_loop_warm.keys == generated_loop_cold.keys);
#if defined(FSIM_HAS_LLVM)
assert(
    generated_loop_warm.simulation.native_cache.hits == 4);
assert(
    generated_loop_warm.simulation.native_cache.misses == 0);
#endif

auto generated_case_config = config;
generated_case_config.project.name =
    "generated-case-mixed-hierarchy-test";
generated_case_config.project.top =
    "sv:work.generated_case_top";
generated_case_config.build.optimization =
    fsim::project::Optimization::o2;
generated_case_config.build.cache_path =
    directory / "generated-case-mixed-hierarchy-cache";
generated_case_config.source_sets.clear();
fsim::project::SourceSet generated_case_vhdl_sources;
generated_case_vhdl_sources.language =
    fsim::project::Language::vhdl;
generated_case_vhdl_sources.standard = "2008";
generated_case_vhdl_sources.library = "work";
generated_case_vhdl_sources.compilation_unit = "file";
generated_case_vhdl_sources.files = {
    generated_loop_vhdl_source};
generated_case_config.source_sets.push_back(
    std::move(generated_case_vhdl_sources));
fsim::project::SourceSet generated_case_sv_sources;
generated_case_sv_sources.language =
    fsim::project::Language::system_verilog;
generated_case_sv_sources.standard = "2017";
generated_case_sv_sources.library = "work";
generated_case_sv_sources.compilation_unit = "file";
generated_case_sv_sources.files = {
    generated_case_sv_top_source};
generated_case_config.source_sets.push_back(
    std::move(generated_case_sv_sources));
generated_case_config.bindings = {
    {"generated_case_top.selected.child",
     "vhdl:work.generated_loop_child(rtl)",
     std::nullopt},
};
const auto run_generated_case =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          generated_case_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 2);
      assert(project->specialization_cache_keys.size() == 2);
      ParameterRun result;
      for (std::size_t index = 0;
           index < project->design.specializations().size();
           ++index) {
        const auto& specialization =
            project->design.specializations()[index];
        result.keys.emplace_back(
            specialization.instance,
            project->specialization_cache_keys[index]);
        if (index == 1) {
          assert(
              specialization.instance
              == "generated_case_top.selected.child");
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"value", "10"}}));
        }
      }
      result.simulation =
          capture_simulation(std::move(*project), engine);
      return result;
    };

const auto generated_case_reference =
    run_generated_case(
        fsim::app::SimulationEngine::interpreter);
const auto generated_case_cold =
    run_generated_case(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    generated_case_reference.simulation,
    generated_case_cold.simulation);
assert(
    generated_case_cold.simulation.result.status
    == fsim::runtime::RunStatus::stopped);
assert(generated_case_cold.simulation.result.time == 1);
assert((
    generated_case_cold.simulation.final_values
    == std::vector<std::string>{"1010"}));
assert(generated_case_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(
    generated_case_cold.simulation.compiled_processes == 2);
assert(
    generated_case_cold.simulation.compiled_modules == 2);
assert(
    generated_case_cold.simulation.native_cache.hits == 0);
assert(
    generated_case_cold.simulation.native_cache.misses == 2);
assert(
    generated_case_cold.simulation.native_cache.stores == 2);
#endif
const auto generated_case_warm =
    run_generated_case(
        fsim::app::SimulationEngine::compiled);
assert(generated_case_warm.keys == generated_case_cold.keys);
#if defined(FSIM_HAS_LLVM)
assert(
    generated_case_warm.simulation.native_cache.hits == 2);
assert(
    generated_case_warm.simulation.native_cache.misses == 0);
#endif

const auto make_generated_behavior_config =
    [&](const std::string_view name,
        const std::string_view top,
        const fsim::project::Language language,
        const std::filesystem::path& behavior_source) {
      auto behavior_config = config;
      behavior_config.project.name = std::string{name};
      behavior_config.project.top = std::string{top};
      behavior_config.build.optimization =
          fsim::project::Optimization::o2;
      behavior_config.build.cache_path =
          directory / (std::string{name} + "-cache");
      behavior_config.source_sets.clear();
      fsim::project::SourceSet behavior_sources;
      behavior_sources.language = language;
      behavior_sources.standard =
          language == fsim::project::Language::vhdl
          ? "2008"
          : "2017";
      behavior_sources.library = "work";
      behavior_sources.compilation_unit = "file";
      behavior_sources.files = {behavior_source};
      behavior_config.source_sets.push_back(
          std::move(behavior_sources));
      return behavior_config;
    };
auto generated_behavior_sv_config =
    make_generated_behavior_config(
        "generated-behavior-sv-test",
        "sv:work.generated_behavior_sv",
        fsim::project::Language::system_verilog,
        generated_behavior_sv_source);
auto generated_behavior_vhdl_config =
    make_generated_behavior_config(
        "generated-behavior-vhdl-test",
        "vhdl:work.generated_behavior_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_behavior_vhdl_source);
auto generated_range_behavior_vhdl_config =
    make_generated_behavior_config(
        "generated-range-behavior-vhdl-test",
        "vhdl:work.generated_range_behavior_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_behavior_vhdl_source);
auto generated_enum_behavior_vhdl_config =
    make_generated_behavior_config(
        "generated-enum-behavior-vhdl-test",
        "vhdl:work.generated_enum_behavior_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_enum_behavior_vhdl_source);
auto vhdl_statement_behavior_config =
    make_generated_behavior_config(
        "vhdl-statement-behavior-test",
        "vhdl:work.vhdl_statement_behavior(rtl)",
        fsim::project::Language::vhdl,
        vhdl_statement_behavior_source);
auto vhdl_statement_behavior_o0_config =
    vhdl_statement_behavior_config;
vhdl_statement_behavior_o0_config.project.name =
    "vhdl-statement-behavior-o0-test";
vhdl_statement_behavior_o0_config.build.optimization =
    fsim::project::Optimization::o0;
vhdl_statement_behavior_o0_config.build.cache_path =
    directory / "vhdl-statement-behavior-o0-test-cache";
auto generated_loop_declarations_vhdl_config =
    make_generated_behavior_config(
        "generated-loop-declarations-vhdl-test",
        "vhdl:work.generated_loop_declarations_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_behavior_vhdl_source);
auto generated_static_behavior_sv_config =
    make_generated_behavior_config(
        "generated-static-behavior-sv-test",
        "sv:work.generated_static_behavior_sv",
        fsim::project::Language::system_verilog,
        generated_static_behavior_sv_source);
auto generated_implicit_behavior_sv_config =
    make_generated_behavior_config(
        "generated-implicit-behavior-sv-test",
        "sv:work.generated_implicit_behavior_sv",
        fsim::project::Language::system_verilog,
        generated_implicit_behavior_sv_source);
auto generated_block_behavior_vhdl_config =
    make_generated_behavior_config(
        "generated-block-behavior-vhdl-test",
        "vhdl:work.generated_block_behavior_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_block_behavior_vhdl_source);
auto generated_block_interface_vhdl_config =
    make_generated_behavior_config(
        "generated-block-interface-vhdl-test",
        "vhdl:work.generated_block_interface_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_block_behavior_vhdl_source);
auto generated_block_interface_vhdl_o0_config =
    generated_block_interface_vhdl_config;
generated_block_interface_vhdl_o0_config.project.name =
    "generated-block-interface-vhdl-o0-test";
generated_block_interface_vhdl_o0_config.build.optimization =
    fsim::project::Optimization::o0;
generated_block_interface_vhdl_o0_config.build.cache_path =
    directory / "generated-block-interface-vhdl-o0-test-cache";
auto generated_block_nonvalue_vhdl_config =
    make_generated_behavior_config(
        "generated-block-nonvalue-vhdl-test",
        "vhdl:work.generated_block_nonvalue_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_block_behavior_vhdl_source);
auto generated_block_nonvalue_vhdl_o0_config =
    generated_block_nonvalue_vhdl_config;
generated_block_nonvalue_vhdl_o0_config.project.name =
    "generated-block-nonvalue-vhdl-o0-test";
generated_block_nonvalue_vhdl_o0_config.build.optimization =
    fsim::project::Optimization::o0;
generated_block_nonvalue_vhdl_o0_config.build.cache_path =
    directory / "generated-block-nonvalue-vhdl-o0-test-cache";
auto generated_guarded_behavior_vhdl_config =
    make_generated_behavior_config(
        "generated-guarded-behavior-vhdl-test",
        "vhdl:work.generated_guarded_behavior_vhdl(rtl)",
        fsim::project::Language::vhdl,
        generated_block_behavior_vhdl_source);
auto generated_guarded_behavior_vhdl_o0_config =
    generated_guarded_behavior_vhdl_config;
generated_guarded_behavior_vhdl_o0_config.project.name =
    "generated-guarded-behavior-vhdl-o0-test";
generated_guarded_behavior_vhdl_o0_config.build.optimization =
    fsim::project::Optimization::o0;
generated_guarded_behavior_vhdl_o0_config.build.cache_path =
    directory / "generated-guarded-behavior-vhdl-o0-test-cache";
const auto run_generated_behavior =
    [&](const fsim::project::Config& behavior_config,
        const fsim::app::SimulationEngine engine,
        const std::vector<std::string_view>& local_paths,
        const std::vector<std::pair<std::string_view,
                                    std::string_view>>&
            expected_identities = {}) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          behavior_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 1);
      assert(project->specialization_cache_keys.size() == 1);
      assert(project->design.find_signal("observed"));
      for (const auto& [name, fragment] : expected_identities) {
        assert(std::ranges::any_of(
            project->design.specializations().front()
                .parameter_identity_values,
            [&](const auto& identity) {
              return identity.first == name
                  && identity.second.find(fragment)
                      != std::string::npos;
            }));
      }
      for (const auto local_path : local_paths) {
        assert(project->design.find_signal(local_path));
      }
      ParameterRun result;
      result.keys.emplace_back(
          project->design.specializations().front().instance,
          project->specialization_cache_keys.front());
      result.simulation =
          capture_simulation(std::move(*project), engine);
      return result;
    };
const auto verify_generated_behavior =
    [&](const fsim::project::Config& behavior_config,
        const std::vector<std::string_view>& local_paths,
        const std::vector<std::string>& expected_values,
        const std::size_t expected_processes,
        const fsim::runtime::SimulationTick expected_time = 0,
        const std::vector<std::pair<std::string_view,
                                    std::string_view>>&
            expected_identities = {}) {
      const auto reference = run_generated_behavior(
          behavior_config,
          fsim::app::SimulationEngine::interpreter,
          local_paths,
          expected_identities);
      const auto cold = run_generated_behavior(
          behavior_config,
          fsim::app::SimulationEngine::compiled,
          local_paths,
          expected_identities);
      compare_captures(reference.simulation, cold.simulation);
      assert(
          cold.simulation.result.status
          == fsim::runtime::RunStatus::completed);
      assert(cold.simulation.result.time == expected_time);
      if (cold.simulation.final_values != expected_values) {
        std::cerr << behavior_config.project.name << " values:";
        for (const auto& value : cold.simulation.final_values) {
          std::cerr << ' ' << value;
        }
        std::cerr << '\n';
      }
      assert(cold.simulation.final_values == expected_values);
      assert(cold.simulation.process_count == expected_processes);
      for (const auto local_path : local_paths) {
        const auto local_separator = local_path.find_last_of('.');
        if (local_separator != std::string_view::npos) {
          const auto local_scope =
              local_path.substr(0, local_separator);
          const auto vcd_scope =
              local_scope.find_first_of("[]")
                      == std::string_view::npos
                  ? std::string{local_scope}
                  : "\\" + std::string{local_scope};
          assert(
              cold.simulation.normalized_vcd.find(
                  "$scope module " + vcd_scope
                  + " $end")
              != std::string::npos);
        }
        const auto local_name =
            local_separator == std::string_view::npos
            ? local_path
            : local_path.substr(local_separator + 1);
        assert(
            cold.simulation.normalized_vcd.find(
                " " + std::string{local_name} + " $end")
            != std::string::npos);
      }
#if defined(FSIM_HAS_LLVM)
      assert(
          cold.simulation.compiled_processes
          == expected_processes);
      assert(cold.simulation.compiled_modules == 1);
      assert(cold.simulation.native_cache.hits == 0);
      assert(cold.simulation.native_cache.misses == 1);
      assert(cold.simulation.native_cache.stores == 1);
#endif
      const auto warm = run_generated_behavior(
          behavior_config,
          fsim::app::SimulationEngine::compiled,
          local_paths,
          expected_identities);
      assert(warm.keys == cold.keys);
#if defined(FSIM_HAS_LLVM)
      assert(warm.simulation.native_cache.hits == 1);
      assert(warm.simulation.native_cache.misses == 0);
#endif
      return cold;
    };
verify_generated_behavior(
    generated_behavior_sv_config,
    {"selected.generated_value"},
    {"0110", "0101"},
    2);
const auto generated_callable_baseline = verify_generated_behavior(
    generated_behavior_vhdl_config,
    {"chosen.generated_value", "chosen.mapped_value"},
    {"1000", "0110", "0111"},
    3,
    0,
    {{"chosen.mapped_shift",
      "kind=function;template=chosen.shifted"},
     {"chosen.mapped_drive",
      "kind=procedure;template=chosen.shifted_drive"},
     {"chosen.selected_math",
      "template=work.generated_math;bias=5"}});
verify_generated_behavior(
    generated_loop_declarations_vhdl_config,
    {"architecture_value", "lanes[2].generated_value"},
    {"0111", "0110", "0110"},
    3);
verify_generated_behavior(
    generated_range_behavior_vhdl_config,
    {"selected.selected_value"},
    {"1001", "1001"},
    2);
const auto generated_enum_baseline = verify_generated_behavior(
    generated_enum_behavior_vhdl_config,
    {"selected.selected_value"},
    {"1001", "1001"},
    2);
verify_generated_behavior(
    vhdl_statement_behavior_config,
    {},
    {"00000000000000000000000000001110"},
    1);
verify_generated_behavior(
    vhdl_statement_behavior_o0_config,
    {},
    {"00000000000000000000000000001110"},
    1);
verify_generated_behavior(
    generated_static_behavior_sv_config,
    {"direct_value", "named_scope.nested_value"},
    {"0100", "0010", "0011"},
    3);
verify_generated_behavior(
    generated_implicit_behavior_sv_config,
    {"implicit_scope.generated_value"},
    {"0111", "0110"},
    2);
verify_generated_behavior(
    generated_block_behavior_vhdl_config,
    {"static_scope.generated_value"},
    {"1000", "0111"},
    2);
const auto block_interface_baseline = verify_generated_behavior(
    generated_block_interface_vhdl_config,
    {"interface_scope.default_value",
     "interface_scope.unused_output"},
    {"0111", "0011", "0101", "UUUU", "0011"},
    4);
verify_generated_behavior(
    generated_block_interface_vhdl_o0_config,
    {"interface_scope.default_value",
     "interface_scope.unused_output"},
    {"0111", "0011", "0101", "UUUU", "0011"},
    4);
const std::vector<std::pair<std::string_view, std::string_view>>
    block_nonvalue_identities{
        {"__block:nonvalue_scope:item_t", "vhdl-type-v"},
        {"__block:nonvalue_scope:transform", "vhdl-function-v"},
        {"__block:nonvalue_scope:observe", "vhdl-procedure-v"},
        {"__block:nonvalue_scope:api", "bias=3"}};
const auto block_nonvalue_baseline = verify_generated_behavior(
    generated_block_nonvalue_vhdl_config,
    {},
    {"00000000000000000000000000000110"},
    1,
    0,
    block_nonvalue_identities);
verify_generated_behavior(
    generated_block_nonvalue_vhdl_o0_config,
    {},
    {"00000000000000000000000000000110"},
    1,
    0,
    block_nonvalue_identities);
verify_generated_behavior(
    generated_guarded_behavior_vhdl_config,
    {"enabled", "guarded_scope.guard"},
    {"0", "1", "1", "1", "XXX", "X11", "0", "0", "0", "0"},
    9,
    7);
verify_generated_behavior(
    generated_guarded_behavior_vhdl_o0_config,
    {"enabled", "guarded_scope.guard"},
    {"0", "1", "1", "1", "XXX", "X11", "0", "0", "0", "0"},
    9,
    7);

std::ifstream generated_callable_source_input(
    generated_behavior_vhdl_source,
    std::ios::binary);
std::string edited_generated_callable_source{
    std::istreambuf_iterator<char>{generated_callable_source_input},
    std::istreambuf_iterator<char>{}};
generated_callable_source_input.close();
const auto old_callable_map =
    edited_generated_callable_source.find(
        "function mapped_shift is new shifted\n"
        "      generic map (amount => 0)");
assert(old_callable_map != std::string::npos);
const auto old_callable_amount =
    edited_generated_callable_source.find(
        "amount => 0", old_callable_map);
assert(old_callable_amount != std::string::npos);
edited_generated_callable_source.replace(
    old_callable_amount,
    std::string_view{"amount => 0"}.size(),
    "amount => 1");
std::ofstream generated_callable_source_output(
    generated_behavior_vhdl_source,
    std::ios::binary | std::ios::trunc);
generated_callable_source_output
    << edited_generated_callable_source;
generated_callable_source_output.close();
const auto generated_callable_edited = run_generated_behavior(
    generated_behavior_vhdl_config,
    fsim::app::SimulationEngine::compiled,
    {"chosen.generated_value", "chosen.mapped_value"},
    {{"chosen.mapped_shift", "generic=amount=1"},
     {"chosen.mapped_drive", "generic=amount=0"},
     {"chosen.selected_math", "bias=5"}});
assert(generated_callable_edited.keys != generated_callable_baseline.keys);
assert((generated_callable_edited.simulation.final_values
        == std::vector<std::string>{"1000", "0110", "1000"}));
#if defined(FSIM_HAS_LLVM)
assert(generated_callable_edited.simulation.native_cache.hits == 0);
assert(generated_callable_edited.simulation.native_cache.misses == 1);
#endif

std::ifstream generated_enum_source_input(
    generated_enum_behavior_vhdl_source,
    std::ios::binary);
std::string edited_generated_enum_source{
    std::istreambuf_iterator<char>{generated_enum_source_input},
    std::istreambuf_iterator<char>{}};
generated_enum_source_input.close();
const auto enum_default =
    edited_generated_enum_source.find("mode : state_t := 'Z'");
assert(enum_default != std::string::npos);
edited_generated_enum_source.replace(
    enum_default,
    std::string_view{"mode : state_t := 'Z'"}.size(),
    "mode : state_t := idle");
std::ofstream generated_enum_source_output(
    generated_enum_behavior_vhdl_source,
    std::ios::binary | std::ios::trunc);
generated_enum_source_output << edited_generated_enum_source;
generated_enum_source_output.close();
const auto generated_enum_edited = run_generated_behavior(
    generated_enum_behavior_vhdl_config,
    fsim::app::SimulationEngine::compiled,
    {});
assert(generated_enum_edited.keys != generated_enum_baseline.keys);
assert((generated_enum_edited.simulation.final_values
        == std::vector<std::string>{"0001"}));
#if defined(FSIM_HAS_LLVM)
assert(generated_enum_edited.simulation.native_cache.hits == 0);
assert(generated_enum_edited.simulation.native_cache.misses == 1);
#endif

std::ifstream block_source_input(
    generated_block_behavior_vhdl_source,
    std::ios::binary);
std::string edited_block_source{
    std::istreambuf_iterator<char>{block_source_input},
    std::istreambuf_iterator<char>{}};
block_source_input.close();
const auto old_map = edited_block_source.find("increment => open");
assert(old_map != std::string::npos);
edited_block_source.replace(
    old_map,
    std::string_view{"increment => open"}.size(),
    "increment => 3");
const auto old_package_map =
    edited_block_source.find("generic map (bias => 3)");
assert(old_package_map != std::string::npos);
edited_block_source.replace(
    old_package_map,
    std::string_view{"generic map (bias => 3)"}.size(),
    "generic map (bias => 4)");
std::ofstream block_source_output(
    generated_block_behavior_vhdl_source,
    std::ios::binary | std::ios::trunc);
block_source_output << edited_block_source;
block_source_output.close();
const auto block_interface_edited = run_generated_behavior(
    generated_block_interface_vhdl_config,
    fsim::app::SimulationEngine::compiled,
    {"interface_scope.default_value",
     "interface_scope.unused_output"});
assert(block_interface_edited.keys != block_interface_baseline.keys);
assert((block_interface_edited.simulation.final_values
        == std::vector<std::string>{
            "1000", "0011", "0101", "UUUU", "0011"}));
#if defined(FSIM_HAS_LLVM)
assert(block_interface_edited.simulation.native_cache.hits == 0);
assert(block_interface_edited.simulation.native_cache.misses == 1);
#endif
const auto block_nonvalue_edited = run_generated_behavior(
    generated_block_nonvalue_vhdl_config,
    fsim::app::SimulationEngine::compiled,
    {},
    {{"__block:nonvalue_scope:api", "bias=4"}});
assert(block_nonvalue_edited.keys != block_nonvalue_baseline.keys);
assert((block_nonvalue_edited.simulation.final_values
        == std::vector<std::string>{
            "00000000000000000000000000000111"}));
#if defined(FSIM_HAS_LLVM)
assert(block_nonvalue_edited.simulation.native_cache.hits == 0);
assert(block_nonvalue_edited.simulation.native_cache.misses == 1);
#endif

}

}  // namespace fsim::test
