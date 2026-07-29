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

void ApplicationTestFixture::test_specialization_and_packages() {
  auto config = base_config();
auto provenance_config = config;
provenance_config.project.name =
    "specialization-provenance-test";
provenance_config.project.top = "sv:work.provenance";
provenance_config.build.optimization =
    fsim::project::Optimization::o2;
provenance_config.build.cache_path =
    directory / "provenance-cache";
provenance_config.source_sets.clear();
fsim::project::SourceSet provenance_sources;
provenance_sources.language =
    fsim::project::Language::system_verilog;
provenance_sources.standard = "2017";
provenance_sources.library = "work";
provenance_sources.files = {
    provenance_source,
    unused_source,
};
provenance_config.source_sets.push_back(
    std::move(provenance_sources));
struct ProvenanceRun {
  std::string specialization_key;
  bool analysis_cache_hit{};
  CapturedSimulation simulation;
};
const auto run_provenance =
    [&](const fsim::project::Config& run_config) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          run_config, run_diagnostics);
      assert(project);
      assert(project->design.specializations().size() == 1);
      assert(project->specialization_cache_keys.size() == 1);
      auto key = project->specialization_cache_keys.front();
      const auto analysis_cache_hit = project->cache_hit;
      auto captured = capture_simulation(
          std::move(*project),
          fsim::app::SimulationEngine::compiled);
      assert(
          captured.result.status
          == fsim::runtime::RunStatus::stopped);
      assert(captured.result.time == 1);
      assert(captured.process_count == 1);
      return ProvenanceRun{
          std::move(key),
          analysis_cache_hit,
          std::move(captured)};
    };

const auto provenance_cold =
    run_provenance(provenance_config);
const auto provenance_warm =
    run_provenance(provenance_config);
assert(!provenance_cold.analysis_cache_hit);
assert(provenance_warm.analysis_cache_hit);
assert(
    provenance_warm.specialization_key
    == provenance_cold.specialization_key);
assert(
    provenance_warm.simulation.final_values
    == provenance_cold.simulation.final_values);
#if defined(FSIM_HAS_LLVM)
assert(provenance_cold.simulation.compiled_processes == 1);
assert(provenance_cold.simulation.compiled_modules == 1);
assert(provenance_cold.simulation.native_cache.hits == 0);
assert(provenance_cold.simulation.native_cache.misses == 1);
assert(provenance_cold.simulation.native_cache.stores == 1);
assert(provenance_warm.simulation.native_cache.hits == 1);
assert(provenance_warm.simulation.native_cache.misses == 0);
#endif

// An uninstantiated source changes the project-analysis key, but not the
// instantiated specialization's native provenance.
write_unused_source("unused revision 2");
const auto provenance_unrelated =
    run_provenance(provenance_config);
assert(!provenance_unrelated.analysis_cache_hit);
assert(
    provenance_unrelated.specialization_key
    == provenance_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
assert(provenance_unrelated.simulation.native_cache.hits == 1);
assert(provenance_unrelated.simulation.native_cache.misses == 0);
#endif

// A comment-only owning-source edit leaves SimIR unchanged but must still
// invalidate the specialization object by source provenance.
write_provenance_source("top revision 2");
const auto provenance_changed_source =
    run_provenance(provenance_config);
assert(!provenance_changed_source.analysis_cache_hit);
assert(
    provenance_changed_source.specialization_key
    != provenance_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
assert(provenance_changed_source.simulation.native_cache.hits == 0);
assert(provenance_changed_source.simulation.native_cache.misses == 1);
assert(provenance_changed_source.simulation.native_cache.stores == 1);
#endif

auto provenance_standard_config = provenance_config;
provenance_standard_config.source_sets.front().standard = "2012";
const auto provenance_changed_standard =
    run_provenance(provenance_standard_config);
assert(!provenance_changed_standard.analysis_cache_hit);
assert(
    provenance_changed_standard.specialization_key
    != provenance_changed_source.specialization_key);
#if defined(FSIM_HAS_LLVM)
assert(provenance_changed_standard.simulation.native_cache.hits == 0);
assert(provenance_changed_standard.simulation.native_cache.misses == 1);
assert(provenance_changed_standard.simulation.native_cache.stores == 1);
#endif

auto parameter_config = config;
parameter_config.project.name = "parameter-specialization-test";
parameter_config.project.top = "sv:work.parameter_top";
parameter_config.build.optimization =
    fsim::project::Optimization::o2;
parameter_config.build.cache_path =
    directory / "parameter-specialization-cache";
parameter_config.source_sets.clear();
fsim::project::SourceSet parameter_sources;
parameter_sources.language =
    fsim::project::Language::system_verilog;
parameter_sources.standard = "2017";
parameter_sources.library = "work";
parameter_sources.compilation_unit = "file";
parameter_sources.files = {
    parameter_child_source,
    parameter_top_source,
};
parameter_config.source_sets.push_back(
    std::move(parameter_sources));
const auto run_parameter_specializations =
    [&](const fsim::app::SimulationEngine engine,
        const std::string_view narrow_value) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          parameter_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 3);
      assert(project->specialization_cache_keys.size() == 3);
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
            == "parameter_top.u_narrow") {
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"WIDTH", "4"},
                  {"VALUE", std::string{narrow_value}},
                  {"LAST", "3"}}));
        } else if (
            specialization.instance
            == "parameter_top.u_wide") {
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"WIDTH", "8"},
                  {"VALUE", "3"},
                  {"LAST", "7"}}));
        }
      }
      result.simulation =
          capture_simulation(std::move(*project), engine);
      return result;
    };
const auto key_for_instance =
    [](const ParameterRun& run,
       const std::string_view instance) -> const std::string& {
      const auto found = std::find_if(
          run.keys.begin(),
          run.keys.end(),
          [&](const auto& entry) {
            return entry.first == instance;
          });
      assert(found != run.keys.end());
      return found->second;
    };

const auto parameter_reference =
    run_parameter_specializations(
        fsim::app::SimulationEngine::interpreter, "2");
const auto parameter_cold =
    run_parameter_specializations(
        fsim::app::SimulationEngine::compiled, "2");
compare_captures(
    parameter_reference.simulation,
    parameter_cold.simulation);
assert((
    parameter_cold.simulation.final_values
    == std::vector<std::string>{"0010", "00000011"}));
assert(parameter_cold.simulation.process_count == 3);
assert(
    key_for_instance(
        parameter_cold, "parameter_top.u_narrow")
    != key_for_instance(
        parameter_cold, "parameter_top.u_wide"));
#if defined(FSIM_HAS_LLVM)
assert(parameter_cold.simulation.compiled_processes == 3);
assert(parameter_cold.simulation.compiled_modules == 3);
assert(parameter_cold.simulation.native_cache.hits == 0);
assert(parameter_cold.simulation.native_cache.misses == 3);
assert(parameter_cold.simulation.native_cache.stores == 3);
#endif

const auto parameter_warm =
    run_parameter_specializations(
        fsim::app::SimulationEngine::compiled, "2");
assert(parameter_warm.keys == parameter_cold.keys);
#if defined(FSIM_HAS_LLVM)
assert(parameter_warm.simulation.native_cache.hits == 3);
assert(parameter_warm.simulation.native_cache.misses == 0);
#endif

write_parameter_top(5);
const auto parameter_changed_reference =
    run_parameter_specializations(
        fsim::app::SimulationEngine::interpreter, "5");
const auto parameter_changed =
    run_parameter_specializations(
        fsim::app::SimulationEngine::compiled, "5");
compare_captures(
    parameter_changed_reference.simulation,
    parameter_changed.simulation);
assert((
    parameter_changed.simulation.final_values
    == std::vector<std::string>{"0101", "00000011"}));
assert(
    key_for_instance(
        parameter_changed, "parameter_top")
    != key_for_instance(
        parameter_cold, "parameter_top"));
assert(
    key_for_instance(
        parameter_changed, "parameter_top.u_narrow")
    != key_for_instance(
        parameter_cold, "parameter_top.u_narrow"));
assert(
    key_for_instance(
        parameter_changed, "parameter_top.u_wide")
    == key_for_instance(
        parameter_cold, "parameter_top.u_wide"));
#if defined(FSIM_HAS_LLVM)
assert(parameter_changed.simulation.native_cache.hits == 1);
assert(parameter_changed.simulation.native_cache.misses == 2);
assert(parameter_changed.simulation.native_cache.stores == 2);
#endif

auto generic_config = config;
generic_config.project.name = "vhdl-generic-specialization-test";
generic_config.project.top =
    "vhdl:work.vhdl_generic_top(rtl)";
generic_config.build.optimization =
    fsim::project::Optimization::o2;
generic_config.build.cache_path =
    directory / "vhdl-generic-specialization-cache";
generic_config.source_sets.clear();
fsim::project::SourceSet generic_sources;
generic_sources.language = fsim::project::Language::vhdl;
generic_sources.standard = "2008";
generic_sources.library = "work";
generic_sources.compilation_unit = "file";
generic_sources.files = {
    vhdl_generic_entity_source,
    vhdl_generic_architecture_source,
    vhdl_generic_top_source,
};
generic_config.source_sets.push_back(
    std::move(generic_sources));
const auto run_generic_specializations =
    [&](const fsim::app::SimulationEngine engine,
        const std::string_view narrow_value) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          generic_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 3);
      assert(project->specialization_cache_keys.size() == 3);
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
            == "vhdl_generic_top.narrow_child") {
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"width", "4"},
                  {"value", std::string{narrow_value}},
                  {"last", "3"}}));
          assert(
              specialization.source_dependencies
              == std::vector<std::string>{
                  vhdl_generic_entity_source.string()});
        } else if (
            specialization.instance
            == "vhdl_generic_top.wide_child") {
          assert((
              specialization.parameter_values
              == std::vector<
                  std::pair<std::string, std::string>>{
                  {"width", "8"},
                  {"value", "3"},
                  {"last", "7"}}));
          assert(
              specialization.source_dependencies
              == std::vector<std::string>{
                  vhdl_generic_entity_source.string()});
        }
      }
      result.simulation =
          capture_simulation(std::move(*project), engine, 0);
      return result;
    };

const auto generic_reference =
    run_generic_specializations(
        fsim::app::SimulationEngine::interpreter, "2");
const auto generic_cold =
    run_generic_specializations(
        fsim::app::SimulationEngine::compiled, "2");
compare_captures(
    generic_reference.simulation,
    generic_cold.simulation);
assert(
    generic_cold.simulation.result.status
    == fsim::runtime::RunStatus::time_limit);
assert(generic_cold.simulation.result.time == 0);
assert((
    generic_cold.simulation.final_values
    == std::vector<std::string>{"0010", "00000011"}));
assert(generic_cold.simulation.process_count == 3);
assert(
    key_for_instance(
        generic_cold,
        "vhdl_generic_top.narrow_child")
    != key_for_instance(
        generic_cold,
        "vhdl_generic_top.wide_child"));
#if defined(FSIM_HAS_LLVM)
assert(generic_cold.simulation.compiled_processes == 3);
assert(generic_cold.simulation.compiled_modules == 3);
assert(generic_cold.simulation.native_cache.hits == 0);
assert(generic_cold.simulation.native_cache.misses == 3);
assert(generic_cold.simulation.native_cache.stores == 3);
#endif

const auto generic_warm =
    run_generic_specializations(
        fsim::app::SimulationEngine::compiled, "2");
assert(generic_warm.keys == generic_cold.keys);
#if defined(FSIM_HAS_LLVM)
assert(generic_warm.simulation.native_cache.hits == 3);
assert(generic_warm.simulation.native_cache.misses == 0);
#endif

write_vhdl_generic_top(5);
const auto generic_changed_reference =
    run_generic_specializations(
        fsim::app::SimulationEngine::interpreter, "5");
const auto generic_changed =
    run_generic_specializations(
        fsim::app::SimulationEngine::compiled, "5");
compare_captures(
    generic_changed_reference.simulation,
    generic_changed.simulation);
assert((
    generic_changed.simulation.final_values
    == std::vector<std::string>{"0101", "00000011"}));
assert(
    key_for_instance(
        generic_changed, "vhdl_generic_top")
    != key_for_instance(
        generic_cold, "vhdl_generic_top"));
assert(
    key_for_instance(
        generic_changed,
        "vhdl_generic_top.narrow_child")
    != key_for_instance(
        generic_cold,
        "vhdl_generic_top.narrow_child"));
assert(
    key_for_instance(
        generic_changed,
        "vhdl_generic_top.wide_child")
    == key_for_instance(
        generic_cold,
        "vhdl_generic_top.wide_child"));
#if defined(FSIM_HAS_LLVM)
assert(generic_changed.simulation.native_cache.hits == 1);
assert(generic_changed.simulation.native_cache.misses == 2);
assert(generic_changed.simulation.native_cache.stores == 2);
#endif

write_vhdl_generic_entity("interface revision 2");
const auto generic_interface_changed =
    run_generic_specializations(
        fsim::app::SimulationEngine::compiled, "5");
assert(
    key_for_instance(
        generic_interface_changed,
        "vhdl_generic_top")
    == key_for_instance(
        generic_changed,
        "vhdl_generic_top"));
assert(
    key_for_instance(
        generic_interface_changed,
        "vhdl_generic_top.narrow_child")
    != key_for_instance(
        generic_changed,
        "vhdl_generic_top.narrow_child"));
assert(
    key_for_instance(
        generic_interface_changed,
        "vhdl_generic_top.wide_child")
    != key_for_instance(
        generic_changed,
        "vhdl_generic_top.wide_child"));
#if defined(FSIM_HAS_LLVM)
assert(
    generic_interface_changed.simulation.native_cache.hits
    == 1);
assert(
    generic_interface_changed.simulation.native_cache.misses
    == 2);
assert(
    generic_interface_changed.simulation.native_cache.stores
    == 2);
#endif

const auto package_base_constant_source =
    directory / "package_base_constants.vhd";
const auto package_constant_source =
    directory / "package_constants.vhd";
const auto package_base_context_source =
    directory / "package_base_context.vhd";
const auto package_context_source =
    directory / "package_context.vhd";
const auto unused_package_source =
    directory / "unused_package.vhd";
const auto package_constant_user_source =
    directory / "package_constant_user.vhd";
const auto write_package_constants =
    [&](const std::uint64_t base_value) {
      std::ofstream output(package_base_constant_source);
      output << "package base_constants is\n"
             << "  constant base_width : natural := 4;\n"
             << "  constant base_value : natural := "
             << base_value << ";\n"
             << "end package base_constants;\n";
    };
write_package_constants(5);
{
  std::ofstream output(package_constant_source);
  output << R"(
use work.base_constants.all;
package constants is
constant width : natural := base_width;
constant next_value : natural := base_value + 1;
end package constants;
)";
}
{
  std::ofstream output(package_base_context_source);
  output << R"(
context package_base_context is
library work;
use work.constants.all;
end context package_base_context;
)";
}
const auto write_package_context =
    [&](const std::string_view revision) {
      std::ofstream output(package_context_source);
      output << "-- " << revision << '\n'
             << "context package_context is\n"
             << "  context work.package_base_context;\n"
             << "end context package_context;\n";
    };
write_package_context("context revision 1");
const auto write_unused_package =
    [&](const std::string_view revision) {
      std::ofstream output(unused_package_source);
      output << "-- " << revision << '\n'
             << "package unused_constants is\n"
             << "  constant unrelated : natural := 99;\n"
             << "end package unused_constants;\n";
    };
write_unused_package("unused revision 1");
{
  std::ofstream output(package_constant_user_source);
  output << R"(
entity package_constant_user is
port (
  observed : out unsigned(
    support.constants.width - 1 downto 0)
);
end entity package_constant_user;

context support.package_context;
architecture rtl of package_constant_user is
signal local_value : unsigned(width - 1 downto 0);
begin
local_value <= next_value;
observed <= local_value + 1;
end architecture rtl;
)";
}
auto package_constant_config = config;
package_constant_config.project.name =
    "vhdl-package-constant-test";
package_constant_config.project.top =
    "vhdl:work.package_constant_user(rtl)";
package_constant_config.build.optimization =
    fsim::project::Optimization::o2;
package_constant_config.build.cache_path =
    directory / "vhdl-package-constant-cache";
package_constant_config.source_sets.clear();
fsim::project::SourceSet package_library_sources;
package_library_sources.language =
    fsim::project::Language::vhdl;
package_library_sources.standard = "2008";
package_library_sources.library = "support";
package_library_sources.compilation_unit = "file";
package_library_sources.files = {
    package_base_constant_source,
    package_constant_source,
    package_base_context_source,
    package_context_source,
    unused_package_source,
};
package_constant_config.source_sets.push_back(
    std::move(package_library_sources));
fsim::project::SourceSet package_user_sources;
package_user_sources.language =
    fsim::project::Language::vhdl;
package_user_sources.standard = "2008";
package_user_sources.library = "work";
package_user_sources.compilation_unit = "file";
package_user_sources.files = {package_constant_user_source};
package_constant_config.source_sets.push_back(
    std::move(package_user_sources));
struct PackageConstantRun {
  std::string specialization_key;
  CapturedSimulation simulation;
};
const auto run_package_constants =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          package_constant_config, run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 1);
      assert(project->specialization_cache_keys.size() == 1);
      const auto& dependencies =
          project->design.specializations()
              .front()
              .source_dependencies;
      assert(
          std::find(
              dependencies.begin(),
              dependencies.end(),
              package_constant_source.string())
          != dependencies.end());
      assert(
          std::find(
              dependencies.begin(),
              dependencies.end(),
              package_base_constant_source.string())
          != dependencies.end());
      assert(
          std::find(
              dependencies.begin(),
              dependencies.end(),
              package_base_context_source.string())
          != dependencies.end());
      assert(
          std::find(
              dependencies.begin(),
              dependencies.end(),
              package_context_source.string())
          != dependencies.end());
      assert(
          std::find(
              dependencies.begin(),
              dependencies.end(),
              unused_package_source.string())
          == dependencies.end());
      auto key = project->specialization_cache_keys.front();
      auto simulation = capture_simulation(
          std::move(*project), engine);
      return PackageConstantRun{
          std::move(key), std::move(simulation)};
    };
const auto package_constant_reference =
    run_package_constants(
        fsim::app::SimulationEngine::interpreter);
const auto package_constant_cold =
    run_package_constants(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    package_constant_reference.simulation,
    package_constant_cold.simulation);
assert((
    package_constant_cold.simulation.final_values
    == std::vector<std::string>{"0111", "0110"}));
assert(package_constant_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(
    package_constant_cold.simulation.compiled_processes == 2);
assert(
    package_constant_cold.simulation.compiled_modules == 1);
assert(package_constant_cold.simulation.native_cache.hits == 0);
assert(
    package_constant_cold.simulation.native_cache.misses == 1);
assert(
    package_constant_cold.simulation.native_cache.stores == 1);
#endif

const auto package_constant_warm =
    run_package_constants(
        fsim::app::SimulationEngine::compiled);
assert(
    package_constant_warm.specialization_key
    == package_constant_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
assert(package_constant_warm.simulation.native_cache.hits == 1);
assert(
    package_constant_warm.simulation.native_cache.misses == 0);
#endif

write_unused_package("unused revision 2");
const auto package_unrelated_changed =
    run_package_constants(
        fsim::app::SimulationEngine::compiled);
assert(
    package_unrelated_changed.specialization_key
    == package_constant_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
assert(
    package_unrelated_changed.simulation.native_cache.hits
    == 1);
assert(
    package_unrelated_changed.simulation.native_cache.misses
    == 0);
#endif

write_package_context("context revision 2");
const auto package_context_changed =
    run_package_constants(
        fsim::app::SimulationEngine::compiled);
assert(
    package_context_changed.specialization_key
    != package_constant_cold.specialization_key);
assert(
    package_context_changed.simulation.final_values
    == package_constant_cold.simulation.final_values);
#if defined(FSIM_HAS_LLVM)
assert(
    package_context_changed.simulation.native_cache.hits == 0);
assert(
    package_context_changed.simulation.native_cache.misses == 1);
assert(
    package_context_changed.simulation.native_cache.stores == 1);
#endif

write_package_constants(9);
const auto package_constant_changed_reference =
    run_package_constants(
        fsim::app::SimulationEngine::interpreter);
const auto package_constant_changed =
    run_package_constants(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    package_constant_changed_reference.simulation,
    package_constant_changed.simulation);
assert(
    package_constant_changed.specialization_key
    != package_context_changed.specialization_key);
assert((
    package_constant_changed.simulation.final_values
    == std::vector<std::string>{"1011", "1010"}));
#if defined(FSIM_HAS_LLVM)
assert(
    package_constant_changed.simulation.native_cache.hits == 0);
assert(
    package_constant_changed.simulation.native_cache.misses == 1);
assert(
    package_constant_changed.simulation.native_cache.stores == 1);
#endif

const auto systemverilog_base_package_source =
    directory / "systemverilog_base_package.sv";
const auto systemverilog_derived_package_source =
    directory / "systemverilog_derived_package.sv";
const auto systemverilog_unused_package_source =
    directory / "systemverilog_unused_package.sv";
const auto systemverilog_package_user_source =
    directory / "systemverilog_package_user.sv";
const auto write_systemverilog_base_package =
    [&](const std::uint64_t base_value,
        const std::uint64_t width) {
      std::ofstream output(
          systemverilog_base_package_source);
      output << "package base_values;\n"
             << "  parameter int WIDTH = "
             << width << ";\n"
             << "  localparam int BASE = "
             << base_value << ";\n"
             << "  typedef logic [WIDTH-1:0] word_t;\n"
             << "  typedef enum logic [WIDTH-1:0] {\n"
             << "    IDLE = 0,\n"
             << "    ACTIVE = BASE + 1\n"
             << "  } state_t;\n"
             << "  typedef struct packed {\n"
             << "    logic [WIDTH-1:0] payload;\n"
             << "    bit valid;\n"
             << "  } packet_t;\n"
             << "  typedef union packed {\n"
             << "    logic [WIDTH-1:0] payload;\n"
             << "    logic [WIDTH-1:0] mirror;\n"
             << "  } overlay_t;\n"
             << "endpackage : base_values\n";
    };
write_systemverilog_base_package(5, 4);
{
  std::ofstream output(
      systemverilog_derived_package_source);
  output << R"(
import base_values::*;
package derived_values;
localparam int NEXT = BASE + 1;
typedef base_values::state_t result_t;
endpackage : derived_values
)";
}
const auto write_systemverilog_unused_package =
    [&](const std::string_view revision) {
      std::ofstream output(
          systemverilog_unused_package_source);
      output << "// " << revision << '\n'
             << "package unused_values;\n"
             << "  localparam int UNRELATED = 99;\n"
             << "endpackage : unused_values\n";
    };
write_systemverilog_unused_package("unused revision 1");
{
  std::ofstream output(
      systemverilog_package_user_source);
  output << R"(
import derived_values::NEXT, derived_values::result_t;
import base_values::ACTIVE, base_values::WIDTH, base_values::packet_t,
     base_values::overlay_t;
module systemverilog_package_user(
output result_t observed
);
packet_t packet;
overlay_t overlay;
logic signed [WIDTH-1:0] signed_shift;
logic [WIDTH-1:0] unsigned_shift;
logic [WIDTH-1:0] xnor_value;
logic nand_value;
logic nor_value;
logic xnor_reduction;
initial begin
  packet.payload = ACTIVE;
  packet.valid = 1'b1;
  overlay.payload = ACTIVE;
  packet.payload[0 +: 1] = 1'b0;
  overlay.mirror[1 +: WIDTH-1] =
    packet.payload[base_values::WIDTH-1 -: WIDTH-1];
  overlay.payload[0] = packet.payload[0 +: 1];
  overlay.payload = overlay.payload & {WIDTH{1'b1}};
  signed_shift = {1'b1, {WIDTH-1{1'b0}}};
  signed_shift = signed_shift >>> 1;
  unsigned_shift = {1'b1, {WIDTH-1{1'b0}}};
  unsigned_shift = unsigned_shift >>> 1;
  xnor_value = signed_shift ~^ unsigned_shift;
  nand_value = ~&xnor_value;
  nor_value = ~|xnor_value;
  xnor_reduction = ^~xnor_value;
  overlay.payload = overlay.payload <<< 0;
end
assign observed = {
  overlay.mirror[WIDTH-1:1],
  packet.payload[0]
};
endmodule
)";
}
auto systemverilog_package_config = config;
systemverilog_package_config.project.name =
    "systemverilog-package-test";
systemverilog_package_config.project.top =
    "sv:work.systemverilog_package_user";
systemverilog_package_config.build.optimization =
    fsim::project::Optimization::o2;
systemverilog_package_config.build.cache_path =
    directory / "systemverilog-package-cache";
systemverilog_package_config.source_sets.clear();
fsim::project::SourceSet systemverilog_package_sources;
systemverilog_package_sources.language =
    fsim::project::Language::system_verilog;
systemverilog_package_sources.standard = "2017";
systemverilog_package_sources.library = "work";
systemverilog_package_sources.compilation_unit = "file";
systemverilog_package_sources.files = {
    systemverilog_base_package_source,
    systemverilog_derived_package_source,
    systemverilog_unused_package_source,
    systemverilog_package_user_source,
};
systemverilog_package_config.source_sets.push_back(
    std::move(systemverilog_package_sources));
const auto run_systemverilog_packages =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          systemverilog_package_config,
          run_diagnostics);
      if (!project) {
        fsim::diagnostic::print_text(
            std::cerr, run_diagnostics);
      }
      assert(project);
      assert(project->design.specializations().size() == 1);
      assert(project->specialization_cache_keys.size() == 1);
      const auto& dependencies =
          project->design.specializations()
              .front()
              .source_dependencies;
      const auto has_dependency =
          [&](const std::filesystem::path& expected) {
            return std::any_of(
                dependencies.begin(),
                dependencies.end(),
                [&](const std::string& dependency) {
                  std::error_code comparison_error;
                  return std::filesystem::equivalent(
                             std::filesystem::path{dependency},
                             expected,
                             comparison_error)
                      && !comparison_error;
                });
          };
      assert(
          has_dependency(systemverilog_base_package_source));
      assert(
          has_dependency(systemverilog_derived_package_source));
      assert(
          !has_dependency(systemverilog_unused_package_source));
      auto key = project->specialization_cache_keys.front();
      auto simulation = capture_simulation(
          std::move(*project), engine);
      return PackageConstantRun{
          std::move(key), std::move(simulation)};
    };
const auto systemverilog_package_reference =
    run_systemverilog_packages(
        fsim::app::SimulationEngine::interpreter);
const auto systemverilog_package_cold =
    run_systemverilog_packages(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    systemverilog_package_reference.simulation,
    systemverilog_package_cold.simulation);
assert((
    systemverilog_package_cold.simulation.final_values
    == std::vector<std::string>{
        "0110", "01101", "0110", "1100", "0100",
        "0111", "1", "0", "0"}));
assert(
    systemverilog_package_cold.simulation.process_count == 2);
#if defined(FSIM_HAS_LLVM)
assert(
    systemverilog_package_cold.simulation.compiled_processes
    == 2);
assert(
    systemverilog_package_cold.simulation.compiled_modules
    == 1);
assert(
    systemverilog_package_cold.simulation.native_cache.misses
    == 1);
#endif

const auto systemverilog_package_warm =
    run_systemverilog_packages(
        fsim::app::SimulationEngine::compiled);
assert(
    systemverilog_package_warm.specialization_key
    == systemverilog_package_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
assert(
    systemverilog_package_warm.simulation.native_cache.hits
    == 1);
#endif

write_systemverilog_unused_package("unused revision 2");
const auto systemverilog_package_unrelated =
    run_systemverilog_packages(
        fsim::app::SimulationEngine::compiled);
assert(
    systemverilog_package_unrelated.specialization_key
    == systemverilog_package_cold.specialization_key);
#if defined(FSIM_HAS_LLVM)
assert(
    systemverilog_package_unrelated
        .simulation.native_cache.hits
    == 1);
#endif

write_systemverilog_base_package(9, 5);
const auto systemverilog_package_changed_reference =
    run_systemverilog_packages(
        fsim::app::SimulationEngine::interpreter);
const auto systemverilog_package_changed =
    run_systemverilog_packages(
        fsim::app::SimulationEngine::compiled);
compare_captures(
    systemverilog_package_changed_reference.simulation,
    systemverilog_package_changed.simulation);
assert(
    systemverilog_package_changed.specialization_key
    != systemverilog_package_cold.specialization_key);
assert((
    systemverilog_package_changed.simulation.final_values
    == std::vector<std::string>{
        "01010", "010101", "01010", "11000", "01000",
        "01111", "1", "0", "1"}));
#if defined(FSIM_HAS_LLVM)
assert(
    systemverilog_package_changed.simulation.native_cache.misses
    == 1);
#endif

}

}  // namespace fsim::test
