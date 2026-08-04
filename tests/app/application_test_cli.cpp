// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include "fsim/systemc/hierarchy.hpp"
#include "fsim/library/artifact.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"

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

namespace {

void clone_library_writable(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
  std::filesystem::create_directories(destination);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(source)) {
    const auto relative = std::filesystem::relative(entry.path(), source);
    const auto target = destination / relative;
    if (entry.is_directory()) {
      std::filesystem::create_directories(target);
    } else if (entry.is_regular_file()) {
      std::filesystem::create_directories(target.parent_path());
      std::filesystem::copy_file(entry.path(), target);
      std::filesystem::permissions(
          target, std::filesystem::perms::owner_write,
          std::filesystem::perm_options::add);
    }
  }
}

}  // namespace

void ApplicationTestFixture::test_preprocessing_debug_and_cli() {
  // FSIM-CONFORMANCE CF-COMMON-DEBUGGER-001 source=SRC-FSIM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-CLI-001 source=SRC-FSIM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-TRACE-001 source=SRC-COCOTB expectation=execute
  auto config = base_config();
  fsim::diagnostic::Engine diagnostics;
  auto first = fsim::app::build_project(config, diagnostics);
  auto second = fsim::app::build_project(config, diagnostics);
  if (!first || !second) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(first);
  assert(second);
const auto exported_library = directory / "work.fsimlib";
auto native_export_config = config;
std::erase_if(
    native_export_config.source_sets,
    [](const auto& source_set) {
      return source_set.language == fsim::project::Language::systemc;
    });
native_export_config.build.cache_path = directory / "native-export-cache";
fsim::diagnostic::Engine export_diagnostics;
assert(fsim::app::export_library(
    native_export_config, "work", exported_library, export_diagnostics));
assert(!export_diagnostics.has_error());
fsim::diagnostic::Engine exported_metadata_diagnostics;
const auto exported_metadata = fsim::library::load_metadata(
    exported_library, "work", exported_metadata_diagnostics);
assert(exported_metadata);
assert(!exported_metadata->units.empty());
assert(!exported_metadata->sources.empty());
#if defined(FSIM_HAS_LLVM)
assert(!exported_metadata->native_artifacts.empty());
#else
assert(exported_metadata->native_artifacts.empty());
#endif
assert(std::ranges::all_of(
    exported_metadata->sources,
    [](const auto& source_entry) {
      return !source_entry.logical_name.empty()
          && !std::filesystem::path(source_entry.logical_name).is_absolute();
    }));
{
  const auto unit_path =
      exported_library / exported_metadata->units.front().artifact;
  std::ifstream input(unit_path, std::ios::binary);
  const std::string bytes{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
  fsim::diagnostic::Engine restored_diagnostics;
  const auto restored = fsim::library::deserialize_portable_unit(
      bytes, unit_path.string(), restored_diagnostics);
  assert(restored);
  assert(restored->library == "work");
}
fsim::project::Config mapped_direct_config;
mapped_direct_config.base_directory = directory;
mapped_direct_config.project.name = "mapped-native-direct";
mapped_direct_config.project.top = "sv:work.tb";
mapped_direct_config.project.time_resolution = "1ns";
mapped_direct_config.build.cache_path = directory / "mapped-native-cache";
mapped_direct_config.run.max_deltas = 1000;
mapped_direct_config.library_mappings.push_back({"work", exported_library});
fsim::diagnostic::Engine mapped_direct_diagnostics;
auto mapped_direct = fsim::app::build_project(
    mapped_direct_config, mapped_direct_diagnostics);
if (!mapped_direct) {
  for (const auto& diagnostic : mapped_direct_diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}
assert(mapped_direct);
assert(mapped_direct->mapped_libraries.size() == 1);
#if defined(FSIM_HAS_LLVM)
assert(mapped_direct->mapped_libraries.front().native_accepted);
assert(mapped_direct->mapped_libraries.front().native_kind == "llvm_object");
fsim::app::Simulation mapped_direct_simulation(
    std::move(*mapped_direct), mapped_direct_config.run.max_deltas,
    fsim::app::SimulationEngine::compiled);
assert(mapped_direct_simulation.native_cache_statistics().hits != 0);
const auto incompatible_library = directory / "work-incompatible.fsimlib";
clone_library_writable(exported_library, incompatible_library);
auto incompatible_metadata = *exported_metadata;
for (auto& native : incompatible_metadata.native_artifacts) {
  native.target += "-incompatible";
}
const auto incompatible_metadata_path =
    incompatible_library / fsim::library::kMetadataFilename;
std::filesystem::permissions(
    incompatible_metadata_path,
    std::filesystem::perms::owner_write,
    std::filesystem::perm_options::add);
{
  std::ofstream output(incompatible_metadata_path, std::ios::binary);
  output << fsim::library::serialize_metadata(incompatible_metadata);
  assert(output.good());
}
auto portable_fallback_config = mapped_direct_config;
portable_fallback_config.project.name = "mapped-native-fallback";
portable_fallback_config.build.cache_path =
    directory / "mapped-native-fallback-cache";
portable_fallback_config.library_mappings.front().path = incompatible_library;
fsim::diagnostic::Engine portable_fallback_diagnostics;
auto portable_fallback = fsim::app::build_project(
    portable_fallback_config, portable_fallback_diagnostics);
assert(portable_fallback);
assert(!portable_fallback->mapped_libraries.front().native_accepted);
fsim::app::Simulation portable_fallback_simulation(
    std::move(*portable_fallback), portable_fallback_config.run.max_deltas,
    fsim::app::SimulationEngine::compiled);
assert(portable_fallback_simulation.compiled_process_count() != 0);
assert(portable_fallback_simulation.native_cache_statistics().hits == 0);
assert(portable_fallback_simulation.native_cache_statistics().stores != 0);
const auto corrupt_native_library = directory / "work-native-corrupt.fsimlib";
clone_library_writable(exported_library, corrupt_native_library);
const auto corrupt_native_payload = corrupt_native_library
    / exported_metadata->native_artifacts.front().artifact;
{
  std::ofstream output(
      corrupt_native_payload, std::ios::binary | std::ios::app);
  output.put('\0');
  assert(output.good());
}
auto corrupt_native_config = mapped_direct_config;
corrupt_native_config.project.name = "mapped-native-corrupt";
corrupt_native_config.build.cache_path = directory / "mapped-native-corrupt-cache";
corrupt_native_config.library_mappings.front().path = corrupt_native_library;
fsim::diagnostic::Engine corrupt_native_diagnostics;
assert(!fsim::app::build_project(
    corrupt_native_config, corrupt_native_diagnostics));
assert(std::ranges::any_of(
    corrupt_native_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.code == "FSIM-LIB-0008"
          && diagnostic.message.find("checksum mismatch")
              != std::string::npos;
    }));
#else
assert(!mapped_direct->mapped_libraries.front().native_accepted);
#endif
fsim::diagnostic::Engine mapped_interpreter_diagnostics;
auto mapped_interpreter = fsim::app::build_project(
    mapped_direct_config, mapped_interpreter_diagnostics);
assert(mapped_interpreter);
const auto mapped_interpreter_capture = capture_simulation(
    std::move(*mapped_interpreter),
    fsim::app::SimulationEngine::interpreter);
assert(
    mapped_interpreter_capture.result.status
    == fsim::runtime::RunStatus::stopped);
assert(mapped_interpreter_capture.result.time == 3);
assert(mapped_interpreter_capture.normalized_vcd.find("$var")
    != std::string::npos);
const auto exported_systemc_library = directory / "models-native.fsimlib";
fsim::diagnostic::Engine systemc_export_diagnostics;
assert(fsim::app::export_library(
    config, "models", exported_systemc_library,
    systemc_export_diagnostics));
fsim::diagnostic::Engine systemc_metadata_diagnostics;
const auto systemc_metadata = fsim::library::load_metadata(
    exported_systemc_library, "models", systemc_metadata_diagnostics);
assert(systemc_metadata);
assert(systemc_metadata->units.empty());
assert(std::ranges::any_of(
    systemc_metadata->sources,
    [](const auto& source_entry) {
      return source_entry.language == "systemc";
    }));
assert(systemc_metadata->native_artifacts.size() == 1);
assert(systemc_metadata->native_artifacts.front().kind == "systemc_plugin");
auto nonportable_systemc_config = config;
nonportable_systemc_config.systemc.defines.push_back("PRODUCER_ONLY=1");
fsim::diagnostic::Engine nonportable_systemc_diagnostics;
assert(!fsim::app::export_library(
    nonportable_systemc_config, "models",
    directory / "nonportable-systemc.fsimlib",
    nonportable_systemc_diagnostics));
assert(std::ranges::any_of(
    nonportable_systemc_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.code == "FSIM-LIB-0007"
          && diagnostic.message.find("portable fallback") != std::string::npos;
    }));
fsim::project::Config mapped_systemc_config;
mapped_systemc_config.base_directory = directory;
mapped_systemc_config.project.name = "mapped-systemc-native";
mapped_systemc_config.project.top = "systemc:models.throwing_method";
mapped_systemc_config.project.time_resolution = "1ns";
mapped_systemc_config.build.cache_path = directory / "mapped-systemc-cache";
mapped_systemc_config.run.max_deltas = 1000;
mapped_systemc_config.library_mappings.push_back(
    {"models", exported_systemc_library});
fsim::diagnostic::Engine mapped_systemc_diagnostics;
auto mapped_systemc = fsim::app::build_project(
    mapped_systemc_config, mapped_systemc_diagnostics);
if (!mapped_systemc) {
  for (const auto& diagnostic : mapped_systemc_diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}
assert(mapped_systemc);
assert(mapped_systemc->systemc_plugins.size() == 1);
assert(mapped_systemc->mapped_libraries.size() == 1);
assert(mapped_systemc->mapped_libraries.front().native_accepted);
assert(
    mapped_systemc->mapped_libraries.front().native_kind
    == "systemc_plugin");
assert(
    mapped_systemc->systemc_plugins.front().parent_path().parent_path()
    == exported_systemc_library / "native");
const auto incompatible_systemc_library =
    directory / "models-systemc-incompatible.fsimlib";
clone_library_writable(
    exported_systemc_library, incompatible_systemc_library);
auto incompatible_systemc_metadata = *systemc_metadata;
incompatible_systemc_metadata.native_artifacts.front().target +=
    "-incompatible";
const auto incompatible_systemc_metadata_path =
    incompatible_systemc_library / fsim::library::kMetadataFilename;
std::filesystem::permissions(
    incompatible_systemc_metadata_path,
    std::filesystem::perms::owner_write,
    std::filesystem::perm_options::add);
{
  std::ofstream output(
      incompatible_systemc_metadata_path, std::ios::binary);
  output << fsim::library::serialize_metadata(
      incompatible_systemc_metadata);
  assert(output.good());
}
auto systemc_fallback_config = mapped_systemc_config;
systemc_fallback_config.project.name = "mapped-systemc-fallback";
systemc_fallback_config.build.cache_path =
    directory / "mapped-systemc-fallback-cache";
systemc_fallback_config.library_mappings.front().path =
    incompatible_systemc_library;
fsim::diagnostic::Engine systemc_fallback_diagnostics;
auto systemc_fallback = fsim::app::build_project(
    systemc_fallback_config, systemc_fallback_diagnostics);
if (!systemc_fallback) {
  for (const auto& diagnostic : systemc_fallback_diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}
assert(systemc_fallback);
assert(!systemc_fallback->mapped_libraries.front().native_accepted);
assert(systemc_fallback->systemc_plugins.size() == 1);
assert(
    systemc_fallback->systemc_plugins.front().string().find(
        "mapped-systemc-fallback-cache") != std::string::npos);
const auto mapped_consumer_source = directory / "mapped_consumer.sv";
{
  std::ofstream output(mapped_consumer_source, std::ios::binary);
  output << "module mapped_consumer; tb imported(); endmodule\n";
  assert(output.good());
}
fsim::project::Config mapped_config;
mapped_config.base_directory = directory;
mapped_config.project.name = "mapped-consumer";
mapped_config.project.top = "sv:consumer.mapped_consumer";
mapped_config.project.time_resolution = "1ns";
mapped_config.build.cache_path = directory / "mapped-cache";
mapped_config.run.max_deltas = 1000;
fsim::project::SourceSet mapped_sources;
mapped_sources.language = fsim::project::Language::system_verilog;
mapped_sources.standard = "2017";
mapped_sources.library = "consumer";
mapped_sources.files = {mapped_consumer_source};
mapped_config.source_sets.push_back(std::move(mapped_sources));
mapped_config.library_mappings.push_back({"work", exported_library});
mapped_config.elaboration.search_libraries = {"work"};
fsim::diagnostic::Engine mapped_build_diagnostics;
auto mapped_build = fsim::app::build_project(
    mapped_config, mapped_build_diagnostics);
if (!mapped_build) {
  for (const auto& diagnostic : mapped_build_diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}
assert(mapped_build);
assert(!mapped_build_diagnostics.has_error());
assert(mapped_build->semantics.valid());
assert(mapped_build->mapped_libraries.size() == 1);
assert(mapped_build->mapped_libraries.front().library == "work");
assert(!mapped_build->mapped_libraries.front().metadata_digest.empty());
assert(
    mapped_build->mapped_libraries.front().unit_checksums.size()
    == exported_metadata->units.size());
#if defined(FSIM_HAS_LLVM)
assert(mapped_build->mapped_libraries.front().native_accepted);
assert(mapped_build->mapped_libraries.front().native_kind == "llvm_object");
#else
assert(!mapped_build->mapped_libraries.front().native_accepted);
#endif
assert(std::ranges::any_of(
    mapped_build->design.specializations(),
    [](const auto& specialization) {
      return specialization.instance == "mapped_consumer.imported"
          && specialization.library == "work";
    }));
fsim::app::Simulation mapped_simulation(
    std::move(*mapped_build), mapped_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter);
assert(mapped_simulation.mapped_libraries().size() == 1);
assert(mapped_simulation.mapped_libraries().front().library == "work");

auto unused_mapping_config = mapped_config;
unused_mapping_config.project.name = "unused-mapping";
unused_mapping_config.project.top = "sv:consumer.local_only";
unused_mapping_config.source_sets.front().files = {
    directory / "local_only.sv"};
{
  std::ofstream output(
      unused_mapping_config.source_sets.front().files.front(),
      std::ios::binary);
  output << "module local_only; endmodule\n";
  assert(output.good());
}
unused_mapping_config.library_mappings.front().path =
    directory / "does-not-exist.fsimlib";
fsim::diagnostic::Engine unused_mapping_diagnostics;
assert(fsim::app::check_project(
    unused_mapping_config, unused_mapping_diagnostics));
assert(!unused_mapping_diagnostics.has_error());

const auto mapped_vhdl_source = directory / "mapped_gate.vhd";
{
  std::ofstream output(mapped_vhdl_source, std::ios::binary);
  output << R"vhdl(
entity mapped_gate is
  port (value : in std_logic; result : out std_logic);
end entity;
architecture rtl of mapped_gate is
begin
  result <= not value;
end architecture;
configuration mapped_gate_configuration of mapped_gate is
  for rtl
  end for;
end configuration;
)vhdl";
  assert(output.good());
}
fsim::project::Config vhdl_export_config;
vhdl_export_config.base_directory = directory;
vhdl_export_config.project.name = "mapped-vhdl-export";
vhdl_export_config.project.top = "vhdl:vendor.mapped_gate(rtl)";
fsim::project::SourceSet vhdl_export_sources;
vhdl_export_sources.language = fsim::project::Language::vhdl;
vhdl_export_sources.standard = "2008";
vhdl_export_sources.library = "vendor";
vhdl_export_sources.files = {mapped_vhdl_source};
vhdl_export_config.source_sets.push_back(std::move(vhdl_export_sources));
const auto vhdl_library = directory / "vendor.fsimlib";
fsim::diagnostic::Engine vhdl_export_diagnostics;
assert(fsim::app::export_library(
    vhdl_export_config, "vendor", vhdl_library,
    vhdl_export_diagnostics));
assert(!vhdl_export_diagnostics.has_error());

const auto mapped_vhdl_consumer = directory / "mapped_vhdl_consumer.sv";
{
  std::ofstream output(mapped_vhdl_consumer, std::ios::binary);
  output << R"sv(module mapped_vhdl_consumer;
logic value;
logic result;
mapped_gate child(.value(value), .result(result));
initial begin value = 1'b0; #1 $finish; end
endmodule
)sv";
  assert(output.good());
}
fsim::project::Config vhdl_consumer_config;
vhdl_consumer_config.base_directory = directory;
vhdl_consumer_config.project.name = "mapped-vhdl-consumer";
vhdl_consumer_config.project.top = "sv:consumer.mapped_vhdl_consumer";
vhdl_consumer_config.project.time_resolution = "1ns";
vhdl_consumer_config.build.cache_path = directory / "mapped-vhdl-cache";
fsim::project::SourceSet vhdl_consumer_sources;
vhdl_consumer_sources.language = fsim::project::Language::system_verilog;
vhdl_consumer_sources.standard = "2017";
vhdl_consumer_sources.library = "consumer";
vhdl_consumer_sources.files = {mapped_vhdl_consumer};
vhdl_consumer_config.source_sets.push_back(std::move(vhdl_consumer_sources));
vhdl_consumer_config.library_mappings.push_back({"vendor", vhdl_library});
vhdl_consumer_config.elaboration.search_libraries = {"vendor"};
fsim::diagnostic::Engine vhdl_consumer_diagnostics;
auto vhdl_consumer_build = fsim::app::build_project(
    vhdl_consumer_config, vhdl_consumer_diagnostics);
if (!vhdl_consumer_build) {
  for (const auto& diagnostic : vhdl_consumer_diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}
assert(vhdl_consumer_build);
assert(std::ranges::any_of(
    vhdl_consumer_build->design.specializations(),
    [](const auto& specialization) {
      return specialization.instance == "mapped_vhdl_consumer.child"
          && specialization.library == "vendor";
    }));
fsim::app::Simulation vhdl_mapped_simulation(
    std::move(*vhdl_consumer_build), vhdl_consumer_config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter);
const auto vhdl_mapped_result = vhdl_mapped_simulation.run();
assert(vhdl_mapped_result.status == fsim::runtime::RunStatus::stopped);
assert(vhdl_mapped_result.time == 1);
const auto vhdl_mapped_signal =
    vhdl_mapped_simulation.find_signal("mapped_vhdl_consumer.result");
assert(vhdl_mapped_signal);
assert(
    vhdl_mapped_simulation.read_signal(*vhdl_mapped_signal).to_msb_string()
    == "1");

auto mapped_top_config = vhdl_consumer_config;
mapped_top_config.project.name = "mapped-vhdl-top";
mapped_top_config.project.top = "vhdl:vendor.mapped_gate(rtl)";
mapped_top_config.build.cache_path = directory / "mapped-vhdl-top-cache";
fsim::diagnostic::Engine mapped_top_diagnostics;
assert(fsim::app::build_project(mapped_top_config, mapped_top_diagnostics));
assert(!mapped_top_diagnostics.has_error());
auto mapped_configuration_config = mapped_top_config;
mapped_configuration_config.project.name = "mapped-vhdl-configuration";
mapped_configuration_config.project.top =
    "vhdl:vendor.mapped_gate_configuration";
mapped_configuration_config.build.cache_path =
    directory / "mapped-vhdl-configuration-cache";
fsim::diagnostic::Engine mapped_configuration_diagnostics;
const auto mapped_configuration_build = fsim::app::build_project(
    mapped_configuration_config, mapped_configuration_diagnostics);
assert(mapped_configuration_build);
assert(std::ranges::any_of(
    mapped_configuration_build->design.specializations(),
    [](const auto& specialization) {
      return specialization.instance == "mapped_gate_configuration"
          && specialization.unit
              == "vhdl:vendor.mapped_gate(rtl)";
    }));

const auto ambiguous_source = directory / "ambiguous_mapped_gate.sv";
{
  std::ofstream output(ambiguous_source, std::ios::binary);
  output << "module mapped_gate; endmodule\n";
  assert(output.good());
}
auto ambiguous_mapping_config = vhdl_consumer_config;
ambiguous_mapping_config.project.name = "mapped-ambiguity";
ambiguous_mapping_config.project.top = "mapped_gate";
ambiguous_mapping_config.source_sets.front().library = "work";
ambiguous_mapping_config.source_sets.front().files = {ambiguous_source};
fsim::diagnostic::Engine ambiguous_mapping_diagnostics;
assert(!fsim::app::build_project(
    ambiguous_mapping_config, ambiguous_mapping_diagnostics));
assert(std::ranges::any_of(
    ambiguous_mapping_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.code == "FSIM-ELAB-005"
          && diagnostic.message.find("sv:work.mapped_gate")
              != std::string::npos
          && diagnostic.message.find("vhdl:vendor.mapped_gate(rtl)")
              != std::string::npos;
    }));

const auto corrupt_library = directory / "corrupt-vendor.fsimlib";
fsim::diagnostic::Engine corrupt_export_diagnostics;
assert(fsim::app::export_library(
    vhdl_export_config, "vendor", corrupt_library,
    corrupt_export_diagnostics));
fsim::diagnostic::Engine corrupt_metadata_diagnostics;
const auto corrupt_metadata = fsim::library::load_metadata(
    corrupt_library, "vendor", corrupt_metadata_diagnostics);
assert(corrupt_metadata && !corrupt_metadata->units.empty());
const auto corrupt_payload =
    corrupt_library / corrupt_metadata->units.front().artifact;
std::filesystem::permissions(
    corrupt_payload, std::filesystem::perms::owner_write,
    std::filesystem::perm_options::add);
{
  std::ofstream output(corrupt_payload, std::ios::binary | std::ios::app);
  output.put('\0');
  assert(output.good());
}
auto corrupt_use_config = vhdl_consumer_config;
corrupt_use_config.library_mappings.front().path = corrupt_library;
fsim::diagnostic::Engine corrupt_use_diagnostics;
assert(!fsim::app::check_project(
    corrupt_use_config, corrupt_use_diagnostics));
assert(std::ranges::any_of(
    corrupt_use_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.code == "FSIM-LIB-0008"
          && diagnostic.message.find("checksum mismatch")
              != std::string::npos;
    }));
auto corrupt_unused_config = unused_mapping_config;
corrupt_unused_config.library_mappings = {{"vendor", corrupt_library}};
corrupt_unused_config.elaboration.search_libraries = {"vendor"};
fsim::diagnostic::Engine corrupt_unused_diagnostics;
assert(fsim::app::check_project(
    corrupt_unused_config, corrupt_unused_diagnostics));
assert(!corrupt_unused_diagnostics.has_error());

const auto wrong_index_library = directory / "wrong-index-vendor.fsimlib";
clone_library_writable(vhdl_library, wrong_index_library);
fsim::diagnostic::Engine wrong_index_metadata_diagnostics;
auto wrong_index_metadata = fsim::library::load_metadata(
    wrong_index_library, "vendor", wrong_index_metadata_diagnostics);
assert(wrong_index_metadata && !wrong_index_metadata->units.empty());
wrong_index_metadata->units.front().kind = "interface";
{
  std::ofstream output(
      wrong_index_library / fsim::library::kMetadataFilename,
      std::ios::binary);
  output << fsim::library::serialize_metadata(*wrong_index_metadata);
  assert(output.good());
}
auto wrong_index_config = vhdl_consumer_config;
wrong_index_config.library_mappings.front().path = wrong_index_library;
fsim::diagnostic::Engine wrong_index_diagnostics;
assert(!fsim::app::check_project(
    wrong_index_config, wrong_index_diagnostics));
assert(std::ranges::any_of(
    wrong_index_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.message.find(
          "unit identity does not match its metadata index")
          != std::string::npos;
    }));

const auto write_dependency_artifact = [this](
    const std::string& library_name,
    const std::vector<std::string>& dependencies) {
  const auto artifact = directory / (library_name + ".fsimlib");
  std::filesystem::create_directories(artifact);
  fsim::library::Metadata metadata;
  metadata.library = library_name;
  metadata.producer = "dependency-test";
  metadata.runtime_schema = 1;
  metadata.dependencies = dependencies;
  metadata.units = {{
      "systemverilog", "module", "dummy", {}, {},
      "units/dummy.fsimir", std::string(64, '0')}};
  std::ofstream output(
      artifact / fsim::library::kMetadataFilename, std::ios::binary);
  output << fsim::library::serialize_metadata(metadata);
  assert(output.good());
  return artifact;
};
const auto missing_dependency_library = write_dependency_artifact(
    "needs_missing", {"missing_dependency"});
auto missing_dependency_config = unused_mapping_config;
missing_dependency_config.project.top = "sv:needs_missing.dummy";
missing_dependency_config.library_mappings = {
    {"needs_missing", missing_dependency_library}};
missing_dependency_config.elaboration.search_libraries.clear();
fsim::diagnostic::Engine missing_dependency_diagnostics;
assert(!fsim::app::check_project(
    missing_dependency_config, missing_dependency_diagnostics));
assert(std::ranges::any_of(
    missing_dependency_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.message.find("has no declared mapping")
          != std::string::npos;
    }));

const auto cycle_a = write_dependency_artifact("cycle_a", {"cycle_b"});
const auto cycle_b = write_dependency_artifact("cycle_b", {"cycle_a"});
auto cycle_config = unused_mapping_config;
cycle_config.project.top = "sv:cycle_a.dummy";
cycle_config.library_mappings = {
    {"cycle_a", cycle_a}, {"cycle_b", cycle_b}};
cycle_config.elaboration.search_libraries.clear();
fsim::diagnostic::Engine cycle_diagnostics;
assert(!fsim::app::check_project(cycle_config, cycle_diagnostics));
assert(std::ranges::any_of(
    cycle_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.message.find("dependency cycle reaches 'cycle_a'")
          != std::string::npos;
    }));

const auto dependent_vendor = directory / "dependent-vendor.fsimlib";
fsim::diagnostic::Engine dependent_export_diagnostics;
assert(fsim::app::export_library(
    vhdl_export_config, "vendor", dependent_vendor,
    dependent_export_diagnostics));
fsim::diagnostic::Engine dependent_metadata_diagnostics;
auto dependent_metadata = fsim::library::load_metadata(
    dependent_vendor, "vendor", dependent_metadata_diagnostics);
assert(dependent_metadata);
dependent_metadata->dependencies = {"work"};
const auto dependent_metadata_path =
    dependent_vendor / fsim::library::kMetadataFilename;
std::filesystem::permissions(
    dependent_metadata_path, std::filesystem::perms::owner_write,
    std::filesystem::perm_options::add);
{
  std::ofstream output(
      dependent_metadata_path, std::ios::binary | std::ios::trunc);
  output << fsim::library::serialize_metadata(*dependent_metadata);
  assert(output.good());
}
auto positive_dependency_config = mapped_top_config;
positive_dependency_config.library_mappings = {
    {"vendor", dependent_vendor}, {"work", exported_library}};
fsim::diagnostic::Engine positive_dependency_diagnostics;
const auto positive_dependency_check = fsim::app::check_project(
    positive_dependency_config, positive_dependency_diagnostics);
assert(positive_dependency_check);
assert(positive_dependency_check->mapped_libraries.size() == 2);
assert(positive_dependency_check->mapped_libraries[0].library == "work");
assert(positive_dependency_check->mapped_libraries[1].library == "vendor");

const auto relocation_original = directory / "relocation-original.fsimlib";
fsim::diagnostic::Engine relocation_export_diagnostics;
assert(fsim::app::export_library(
    config, "work", relocation_original, relocation_export_diagnostics));
auto relocation_config = mapped_config;
relocation_config.project.name = "mapped-relocation";
relocation_config.build.cache_path = directory / "mapped-relocation-cache";
relocation_config.library_mappings.front().path = relocation_original;
const auto artifact_snapshot = [](const std::filesystem::path& root) {
  using Entry = std::tuple<
      std::string, std::uintmax_t, std::filesystem::perms,
      std::filesystem::file_time_type>;
  std::vector<Entry> result;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(root)) {
    const auto relative =
        std::filesystem::relative(entry.path(), root).generic_string();
    const auto size = entry.is_regular_file()
        ? entry.file_size() : std::uintmax_t{};
    result.emplace_back(
        relative, size, entry.status().permissions(),
        entry.last_write_time());
  }
  std::ranges::sort(result);
  return result;
};
const auto original_snapshot = artifact_snapshot(relocation_original);
fsim::diagnostic::Engine relocation_first_diagnostics;
auto relocation_first = fsim::app::build_project(
    relocation_config, relocation_first_diagnostics);
assert(relocation_first && !relocation_first_diagnostics.has_error());
const auto relocation_cache_key = relocation_first->cache_key;
const auto relocation_native_keys =
    relocation_first->specialization_cache_keys;
assert(std::ranges::any_of(
    relocation_first->semantics.source_files(),
    [](const auto& source_file) {
      return source_file.physical_name.starts_with("sources/");
    }));
assert(artifact_snapshot(relocation_original) == original_snapshot);

const auto relocation_moved = directory / "relocation-moved.fsimlib";
std::filesystem::rename(relocation_original, relocation_moved);
relocation_config.library_mappings.front().path = relocation_moved;
const auto moved_snapshot = artifact_snapshot(relocation_moved);
fsim::diagnostic::Engine relocation_second_diagnostics;
auto relocation_second = fsim::app::build_project(
    relocation_config, relocation_second_diagnostics);
assert(relocation_second && !relocation_second_diagnostics.has_error());
assert(relocation_second->cache_key == relocation_cache_key);
assert(relocation_second->specialization_cache_keys
    == relocation_native_keys);
assert(relocation_second->cache_hit);
assert(artifact_snapshot(relocation_moved) == moved_snapshot);
assert(std::filesystem::is_directory(relocation_config.build.cache_path));
{
  std::ofstream forbidden_write(
      relocation_moved / fsim::library::kMetadataFilename,
      std::ios::binary | std::ios::app);
  assert(!forbidden_write);
}

std::istringstream mapped_debug_input(
    "break source sources/00000000/tb.sv:14\ncontinue\nquit\n");
std::ostringstream mapped_debug_output;
std::ostringstream mapped_debug_error;
fsim::app::Simulation mapped_debug_simulation(
    std::move(*relocation_second),
    relocation_config.run.max_deltas,
    fsim::app::SimulationEngine::debug);
assert(fsim::app::run_debug_repl(
    mapped_debug_simulation, mapped_debug_input,
    mapped_debug_output, mapped_debug_error) == 0);
assert(mapped_debug_output.str().find(
    "breakpoint 1 set at sources/00000000/tb.sv:14")
    != std::string::npos);
assert(mapped_debug_output.str().find(
    "hit breakpoint 1: sources/00000000/tb.sv:14:")
    != std::string::npos);

const auto mapped_parameter_source = directory / "mapped_parameter.sv";
{
  std::ofstream output(mapped_parameter_source, std::ios::binary);
  output << R"sv(package mapped_values;
  parameter int BASE = 1;
endpackage
module mapped_parameter #(parameter int WIDTH = 4) (
  input logic [WIDTH-1:0] value,
  output logic [WIDTH-1:0] result
);
  import mapped_values::*;
  assign result = value + BASE;
endmodule
)sv";
  assert(output.good());
}
fsim::project::Config parameter_export_config;
parameter_export_config.base_directory = directory;
parameter_export_config.project.name = "mapped-parameter-export";
parameter_export_config.project.top = "sv:models.mapped_parameter";
fsim::project::SourceSet parameter_export_sources;
parameter_export_sources.language = fsim::project::Language::system_verilog;
parameter_export_sources.standard = "2017";
parameter_export_sources.library = "models";
parameter_export_sources.files = {mapped_parameter_source};
parameter_export_config.source_sets.push_back(
    std::move(parameter_export_sources));
const auto parameter_library = directory / "models.fsimlib";
fsim::diagnostic::Engine parameter_export_diagnostics;
assert(fsim::app::export_library(
    parameter_export_config, "models", parameter_library,
    parameter_export_diagnostics));

const auto parameter_consumer_source = directory / "parameter_consumer.sv";
{
  std::ofstream output(parameter_consumer_source, std::ios::binary);
  output << R"sv(module parameter_consumer;
logic [7:0] value;
logic [7:0] result;
mapped_parameter #(.WIDTH(8)) child(.value(value), .result(result));
endmodule
)sv";
  assert(output.good());
}
fsim::project::Config parameter_consumer_config;
parameter_consumer_config.base_directory = directory;
parameter_consumer_config.project.name = "mapped-parameter-consumer";
parameter_consumer_config.project.top = "sv:consumer.parameter_consumer";
parameter_consumer_config.project.tops = {
    {"sv:consumer.parameter_consumer", "local_root"},
    {"sv:models.mapped_parameter", "mapped_root"}};
parameter_consumer_config.project.time_resolution = "1ns";
parameter_consumer_config.build.cache_path = directory / "parameter-cache";
fsim::project::SourceSet parameter_consumer_sources;
parameter_consumer_sources.language = fsim::project::Language::system_verilog;
parameter_consumer_sources.standard = "2017";
parameter_consumer_sources.library = "consumer";
parameter_consumer_sources.files = {parameter_consumer_source};
parameter_consumer_config.source_sets.push_back(
    std::move(parameter_consumer_sources));
parameter_consumer_config.library_mappings = {
    {"models", parameter_library}};
parameter_consumer_config.elaboration.search_libraries = {"models"};
fsim::diagnostic::Engine parameter_consumer_diagnostics;
const auto parameter_consumer_build = fsim::app::build_project(
    parameter_consumer_config, parameter_consumer_diagnostics);
if (!parameter_consumer_build) {
  for (const auto& diagnostic : parameter_consumer_diagnostics.diagnostics()) {
    std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
  }
}
assert(parameter_consumer_build);
assert((parameter_consumer_build->design.roots()
    == std::vector<std::string>{"local_root", "mapped_root"}));
assert(std::ranges::any_of(
    parameter_consumer_build->design.specializations(),
    [](const auto& specialization) {
      return specialization.instance == "local_root.child"
          && specialization.library == "models"
          && std::ranges::find(
                 specialization.parameter_values,
                 std::pair<std::string, std::string>{"WIDTH", "8"})
              != specialization.parameter_values.end();
    }));
assert(std::ranges::any_of(
    parameter_consumer_build->design.specializations(),
    [](const auto& specialization) {
      return specialization.instance == "mapped_root"
          && specialization.library == "models"
          && std::ranges::find(
                 specialization.parameter_values,
                 std::pair<std::string, std::string>{"WIDTH", "4"})
              != specialization.parameter_values.end();
    }));
// Verilog preprocessing consumes exact transitive snapshots. A header edit
// must invalidate both the analysis object and the owning specialization,
// while interpreter and compiled execution retain identical semantics.
const auto preprocessor_include =
    directory / "preprocessor-include";
std::filesystem::create_directories(preprocessor_include);
const auto preprocessor_header =
    preprocessor_include / "values.svh";
const auto preprocessor_source =
    directory / "preprocessor.sv";
const auto write_preprocessor_header =
    [&](const std::string_view value) {
      std::ofstream output(
          preprocessor_header, std::ios::binary);
      output
          << "`define PREPROCESSED_VALUE " << value << '\n'
          << R"(module preprocessor_app;
logic [3:0] value;
initial begin
  value = `PREPROCESSED_VALUE;
  #1 $finish;
end
endmodule
)";
      assert(output.good());
    };
write_preprocessor_header("4'b1010");
{
  std::ofstream output(
      preprocessor_source, std::ios::binary);
  output << R"(`ifdef ENABLE_PREPROCESSOR_APP
`include "values.svh"
`endif
)";
  assert(output.good());
}
auto preprocessor_config = config;
preprocessor_config.project.name = "preprocessor-test";
preprocessor_config.project.top =
    "sv:work.preprocessor_app";
preprocessor_config.build.cache_path =
    directory / "preprocessor-cache";
preprocessor_config.source_sets.clear();
fsim::project::SourceSet preprocessor_sources;
preprocessor_sources.language =
    fsim::project::Language::system_verilog;
preprocessor_sources.standard = "2017";
preprocessor_sources.library = "work";
preprocessor_sources.files = {preprocessor_source};
preprocessor_sources.include_directories = {
    preprocessor_include};
preprocessor_sources.defines = {
    "ENABLE_PREPROCESSOR_APP=1"};
preprocessor_config.source_sets.push_back(
    std::move(preprocessor_sources));

fsim::diagnostic::Engine preprocessor_check_diagnostics;
const auto preprocessor_checked =
    fsim::app::check_project(
        preprocessor_config,
        preprocessor_check_diagnostics);
assert(preprocessor_checked);
assert(preprocessor_checked->hdl_sources.size() == 1);
assert(
    preprocessor_checked->hdl_sources.front()
        .dependencies.size()
    == 1);
assert(
    preprocessor_checked->hdl_sources.front()
        .dependencies.front().path.filename()
    == "values.svh");

const auto run_preprocessed =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          preprocessor_config, run_diagnostics);
      assert(project);
      assert(
          project->specialization_cache_keys.size() == 1);
      auto key =
          project->specialization_cache_keys.front();
      const bool analysis_hit = project->cache_hit;
      auto capture =
          capture_simulation(std::move(*project), engine);
      return std::tuple{
          std::move(key),
          analysis_hit,
          std::move(capture)};
    };
auto [preprocessor_key_a, preprocessor_miss_a,
      preprocessor_reference_a] =
    run_preprocessed(
        fsim::app::SimulationEngine::interpreter);
auto [preprocessor_key_a_warm, preprocessor_hit_a,
      preprocessor_hybrid_a] =
    run_preprocessed(
        fsim::app::SimulationEngine::compiled);
assert(!preprocessor_miss_a);
assert(preprocessor_hit_a);
assert(preprocessor_key_a == preprocessor_key_a_warm);
compare_captures(
    preprocessor_reference_a, preprocessor_hybrid_a);
assert(
    preprocessor_hybrid_a.final_values
    == std::vector<std::string>{"1010"});

write_preprocessor_header("4'b0101");
auto [preprocessor_key_b, preprocessor_miss_b,
      preprocessor_reference_b] =
    run_preprocessed(
        fsim::app::SimulationEngine::interpreter);
auto [preprocessor_key_b_warm, preprocessor_hit_b,
      preprocessor_hybrid_b] =
    run_preprocessed(
        fsim::app::SimulationEngine::compiled);
assert(!preprocessor_miss_b);
assert(preprocessor_hit_b);
assert(preprocessor_key_b == preprocessor_key_b_warm);
assert(preprocessor_key_b != preprocessor_key_a);
compare_captures(
    preprocessor_reference_b, preprocessor_hybrid_b);
assert(
    preprocessor_hybrid_b.final_values
    == std::vector<std::string>{"0101"});

const auto shared_macro_source =
    directory / "shared-macros.sv";
const auto shared_module_source =
    directory / "shared-module.sv";
const auto write_shared_macro =
    [&](const std::string_view value) {
      std::ofstream output(
          shared_macro_source, std::ios::binary);
      output << "`default_nettype tri0\n"
             << "`celldefine\n"
             << "`define SHARED_RUNTIME_VALUE "
             << value << '\n';
      assert(output.good());
    };
write_shared_macro("1'b0");
{
  std::ofstream output(
      shared_module_source, std::ios::binary);
  output << R"(module shared_preprocessor_app;
typedef logic shared_t;
assign value = `SHARED_RUNTIME_VALUE;
initial #1 $finish;
endmodule
`endcelldefine
`resetall
)";
  assert(output.good());
}
auto shared_preprocessor_config = config;
shared_preprocessor_config.project.name =
    "shared-preprocessor-test";
shared_preprocessor_config.project.top =
    "sv:work.shared_preprocessor_app";
shared_preprocessor_config.build.cache_path =
    directory / "shared-preprocessor-cache";
shared_preprocessor_config.source_sets.clear();
fsim::project::SourceSet shared_preprocessor_sources;
shared_preprocessor_sources.language =
    fsim::project::Language::system_verilog;
shared_preprocessor_sources.standard = "2017";
shared_preprocessor_sources.library = "work";
shared_preprocessor_sources.compilation_unit = "source-set";
shared_preprocessor_sources.files = {
    shared_macro_source, shared_module_source};
shared_preprocessor_config.source_sets.push_back(
    shared_preprocessor_sources);

fsim::diagnostic::Engine shared_check_diagnostics;
auto shared_checked = fsim::app::check_project(
    shared_preprocessor_config, shared_check_diagnostics);
assert(shared_checked);
assert(shared_checked->hdl_sources.size() == 2);
assert(
    !shared_checked->hdl_sources[0]
         .compilation_unit_digest.empty());
assert(
    shared_checked->hdl_sources[0].compilation_unit_digest
    == shared_checked->hdl_sources[1].compilation_unit_digest);
assert(shared_checked->parsed.units.size() == 1);
assert(shared_checked->parsed.units.front().is_cell);
assert(
    shared_checked->parsed.units.front().default_nettype == "tri0");
assert(shared_checked->parsed.units.front().signals.size() == 1);
assert(
    shared_checked->parsed.units.front().signals.front().name == "value");
assert(
    shared_checked->parsed.units.front().signals.front().type.spelling
    == "tri0");
assert(shared_checked->semantics.source_files().size() == 2);
assert(shared_checked->semantics.units().size() == 1);
assert(shared_checked->semantics.units().front().id.value() == 0);
assert(shared_checked->semantics.units().front().scope.value() == 0);
assert(
    shared_checked->semantics.units().front().name
    == "shared_preprocessor_app");
assert(shared_checked->semantics.types().size() == 1);
assert(shared_checked->semantics.types().front().id.value() == 0);
assert(shared_checked->semantics.types().front().name == "shared_t");
assert(shared_checked->semantics.values().size() == 1);
assert(
    shared_checked->semantics.values().front().kind
    == fsim::semantic::ValueKind::signal);
assert(shared_checked->semantics.values().front().name == "value");
const auto semantic_source_count =
    shared_checked->semantics.source_spans().size();
fsim::diagnostic::Engine repeated_semantic_diagnostics;
const auto repeated_semantics = fsim::app::check_project(
    shared_preprocessor_config, repeated_semantic_diagnostics);
assert(repeated_semantics);
assert(
    repeated_semantics->semantics.source_files().size()
    == shared_checked->semantics.source_files().size());
assert(
    repeated_semantics->semantics.source_spans().size()
    == semantic_source_count);
assert(
    repeated_semantics->semantics.units().front().id
    == shared_checked->semantics.units().front().id);
assert(
    repeated_semantics->semantics.units().front().source
    == shared_checked->semantics.units().front().source);
assert(
    repeated_semantics->semantics.types().front().id
    == shared_checked->semantics.types().front().id);
assert(
    repeated_semantics->semantics.values().front().id
    == shared_checked->semantics.values().front().id);
shared_checked->parsed.units.clear();
assert(shared_checked->semantics.units().front().name
       == "shared_preprocessor_app");
assert(shared_checked->semantics.values().front().name == "value");
assert(shared_checked->semantics.source_spans().size()
       == semantic_source_count);

const auto run_shared_preprocessor =
    [&](const fsim::project::Config& run_config,
        const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          run_config, run_diagnostics);
      assert(project);
      assert(project->specialization_cache_keys.size() == 1);
      assert(project->design.specializations().size() == 1);
      assert(project->design.specializations().front().is_cell);
      auto key = project->specialization_cache_keys.front();
      auto capture =
          capture_simulation(std::move(*project), engine);
      return std::pair{
          std::move(key), std::move(capture)};
    };
auto [shared_key_a, shared_reference_a] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::interpreter);
auto [shared_key_a_warm, shared_hybrid_a] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::compiled);
assert(shared_key_a == shared_key_a_warm);
compare_captures(shared_reference_a, shared_hybrid_a);
assert(
    shared_hybrid_a.final_values
    == std::vector<std::string>{"0"});

write_shared_macro("1'b1");
auto [shared_key_b, shared_reference_b] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::interpreter);
auto [shared_key_b_warm, shared_hybrid_b] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::compiled);
assert(shared_key_b == shared_key_b_warm);
assert(shared_key_b != shared_key_a);
compare_captures(shared_reference_b, shared_hybrid_b);
assert(
    shared_hybrid_b.final_values
    == std::vector<std::string>{"1"});

auto independent_file_config = shared_preprocessor_config;
independent_file_config.source_sets.front().compilation_unit =
    "file";
fsim::diagnostic::Engine independent_file_diagnostics;
assert(
    !fsim::app::check_project(
        independent_file_config,
        independent_file_diagnostics));
assert(std::any_of(
    independent_file_diagnostics.diagnostics().begin(),
    independent_file_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SV-PP-028";
    }));

auto combined_preprocessor_config =
    shared_preprocessor_config;
combined_preprocessor_config.project.name =
    "combined-preprocessor-test";
combined_preprocessor_config.build.cache_path =
    directory / "combined-preprocessor-cache";
combined_preprocessor_config.source_sets.clear();
auto combined_definitions = shared_preprocessor_sources;
combined_definitions.library = "definitions";
combined_definitions.compilation_unit = "combined";
combined_definitions.files = {shared_macro_source};
auto combined_module = shared_preprocessor_sources;
combined_module.compilation_unit = "combined";
combined_module.files = {shared_module_source};
combined_preprocessor_config.source_sets = {
    std::move(combined_definitions),
    std::move(combined_module)};
auto [combined_key, combined_reference] =
    run_shared_preprocessor(
        combined_preprocessor_config,
        fsim::app::SimulationEngine::interpreter);
auto [combined_key_warm, combined_hybrid] =
    run_shared_preprocessor(
        combined_preprocessor_config,
        fsim::app::SimulationEngine::compiled);
assert(combined_key == combined_key_warm);
compare_captures(combined_reference, combined_hybrid);
assert(
    combined_hybrid.final_values
    == std::vector<std::string>{"1"});

fsim::diagnostic::Engine mixed_diagnostics;
const auto mixed_manifest =
    std::filesystem::path{FSIM_TEST_SOURCE_DIR}
    / "examples/vertical_slice/fsim.toml";
auto mixed_config =
    fsim::project::load(mixed_manifest, mixed_diagnostics);
assert(mixed_config);
mixed_config->build.cache_path = directory / "mixed-cache";
mixed_config->run.trace_file.reset();
auto mixed_reference_project =
    fsim::app::build_project(*mixed_config, mixed_diagnostics);
auto mixed_hybrid_project =
    fsim::app::build_project(*mixed_config, mixed_diagnostics);
assert(mixed_reference_project);
assert(mixed_hybrid_project);
const auto mixed_reference = capture_simulation(
    std::move(*mixed_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto mixed_hybrid = capture_simulation(
    std::move(*mixed_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(mixed_reference, mixed_hybrid);
assert(
    mixed_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(mixed_hybrid.result.time == 6);

fsim::diagnostic::Engine three_language_diagnostics;
const auto three_language_manifest =
    std::filesystem::path{FSIM_TEST_SOURCE_DIR}
    / "examples/three_language_hierarchy/fsim.toml";
auto three_language_config = fsim::project::load(
    three_language_manifest, three_language_diagnostics);
assert(three_language_config);
three_language_config->build.cache_path =
    directory / "three-language-cache";
three_language_config->run.trace_file.reset();
auto three_language_reference_project =
    fsim::app::build_project(
        *three_language_config, three_language_diagnostics);
auto three_language_hybrid_project =
    fsim::app::build_project(
        *three_language_config, three_language_diagnostics);
assert(three_language_reference_project);
assert(three_language_hybrid_project);
assert(
    three_language_reference_project->design.find_signal(
        "three_language_tb.u_bridge.to_vhdl"));
assert(
    three_language_reference_project->design.find_signal(
        "three_language_tb.u_bridge.u_vhdl.value"));
assert(
    three_language_reference_project->design.systemc_instances()
        .size()
    == 1);
assert(std::ranges::any_of(
    three_language_reference_project->design_ir.objects(),
    [](const auto& object) {
      return object.kind
              == fsim::semantic::design::ObjectKind::systemc_module
          && object.path
              == "three_language_tb.u_bridge.u_vhdl";
    }));
assert(std::ranges::any_of(
    three_language_reference_project->design_ir.objects(),
    [](const auto& object) {
      return object.kind
              == fsim::semantic::design::ObjectKind::systemc_port
          && object.path
              == "three_language_tb.u_bridge.u_vhdl.value";
    }));
const auto three_language_reference = capture_simulation(
    std::move(*three_language_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto three_language_hybrid = capture_simulation(
    std::move(*three_language_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(
    three_language_reference, three_language_hybrid);
assert(
    three_language_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(three_language_hybrid.result.time == 3);
assert((
    three_language_hybrid.final_values
    == std::vector<std::string>{"0", "0", "1"}));

fsim::app::Simulation simulation(std::move(*first), config.run.max_deltas);
const auto q = simulation.find_signal("q");
const auto two_state = simulation.find_signal("two_state");
assert(q && two_state);
assert(simulation.read_signal(*two_state).to_msb_string() == "0");
bool rejected_lossy_deposit = false;
try {
  simulation.deposit_signal(
      *two_state,
      fsim::runtime::PackedLogic4::from_msb_string("X"));
} catch (const std::invalid_argument&) {
  rejected_lossy_deposit = true;
}
assert(rejected_lossy_deposit);
const auto result = simulation.run();
assert(result.status == fsim::runtime::RunStatus::stopped);
assert(result.time == 3);
assert(simulation.finished());
assert(!simulation.poisoned());
assert(simulation.read_signal(*q).to_msb_string() == "1");

simulation.force_signal(
    *q, fsim::runtime::PackedLogic4::from_msb_string("0"));
simulation.deposit_signal(
    *q, fsim::runtime::PackedLogic4::from_msb_string("1"));
assert(simulation.read_signal(*q).to_msb_string() == "0");
simulation.release_signal(*q);
assert(simulation.read_signal(*q).to_msb_string() == "1");

std::string error;
assert(fsim::app::parse_time("25ns", "1ns", error) == 25);
assert(!fsim::app::parse_time("1ps", "1ns", error));
const auto value = fsim::app::parse_value("10xz", 4, error);
assert(value && value->to_msb_string() == "10XZ");
const auto logic9 =
    fsim::app::parse_value("uWlH-", 5, error);
assert(logic9 && logic9->is_logic9());
assert(logic9->to_msb_string() == "UWLH-");

auto compiled_debug_project =
    fsim::app::build_project(config, diagnostics);
assert(compiled_debug_project);
const std::string debug_commands =
    "scope\n"
    "scopes\n"
    "scope u_child\n"
    "signals\n"
    "show value\n"
    "scope ..\n"
    "break signal q == 1\n"
    "break time 1ns\n"
    "breakpoints\n"
    "continue\n"
    "delete 2\n"
    "break source tb.sv:14\n"
    "run-until 3ns\n"
    "delete 3\n"
    "locals\n"
    "step statement\n"
    "locals\n"
    "step statement\n"
    "delete 1\n"
    "locals\n"
    "step process\n"
    "where\n"
    "break signal child_y\n"
    "clear\n"
    "breakpoints\n"
    "continue\n"
    "continue\n"
    "step delta\n"
    "quit\n";
fsim::app::Simulation debug_simulation(
    std::move(*second),
    config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter);
assert(debug_simulation.compiled_process_count() == 0);
std::size_t observed_changes = 0;
debug_simulation.set_signal_change_hook(
    [&observed_changes](
        fsim::runtime::simir::SignalId,
        const fsim::runtime::PackedLogic4&,
        fsim::runtime::SimulationTick,
        std::uint64_t) { ++observed_changes; });
debug_simulation.start();
std::istringstream debug_input{debug_commands};
std::ostringstream debug_output;
std::ostringstream debug_error;
assert(
    fsim::app::run_debug_repl(
        debug_simulation, debug_input, debug_output, debug_error)
    == 0);
assert(debug_error.str().empty());
const auto transcript = debug_output.str();
assert(transcript.find("tb.u_child") != std::string::npos);
assert(
    transcript.find("tb.u_child.value = X") != std::string::npos);
assert(
    transcript.find("breakpoint 1 set on tb.q == 1")
    != std::string::npos);
assert(
    transcript.find("breakpoint 2 set at time 1") != std::string::npos);
assert(
    transcript.find("hit breakpoint 1: tb.q changed to 1 at time 2")
    != std::string::npos);
assert(
    transcript.find("hit breakpoint 2: time 1") != std::string::npos);
assert(
    transcript.find("breakpoint 3 set at tb.sv:14")
    != std::string::npos);
const auto source_breakpoint_hit =
    transcript.find("hit breakpoint 3: ");
assert(source_breakpoint_hit != std::string::npos);
const auto source_breakpoint_end =
    transcript.find('\n', source_breakpoint_hit);
assert(
    transcript.find(
        "tb.sv:14:", source_breakpoint_hit)
    < source_breakpoint_end);
assert(transcript.find("tb.sv:15:") != std::string::npos);
assert(transcript.find("tb.sv:16:") != std::string::npos);
assert(
    transcript.find("local_state = 0") != std::string::npos);
assert(
    transcript.find("local_state = 1") != std::string::npos);
assert(transcript.find("stopped at time 2") != std::string::npos);
assert(transcript.find("time 2, delta") != std::string::npos);
assert(transcript.find("cleared all breakpoints") != std::string::npos);
assert(transcript.find("no breakpoints") != std::string::npos);
assert(
    transcript.find("simulation finished at time 3")
    != std::string::npos);
const auto first_finished =
    transcript.find("simulation has finished");
assert(first_finished != std::string::npos);
assert(
    transcript.find("simulation has finished", first_finished + 1)
    != std::string::npos);
assert(debug_simulation.finished());
assert(!debug_simulation.poisoned());
assert(observed_changes > 0);

fsim::app::Simulation compiled_debug_simulation(
    std::move(*compiled_debug_project),
    config.run.max_deltas,
    fsim::app::SimulationEngine::debug);
#if defined(FSIM_HAS_LLVM)
assert(compiled_debug_simulation.compiled_process_count() == 2);
assert(compiled_debug_simulation.compiled_module_count() == 2);
const auto debug_native_cache =
    compiled_debug_simulation.native_cache_statistics();
// The same specialization modules were already cached at the configured O2
// run setting. Cold objects here therefore prove that debug forces O0.
assert(debug_native_cache.hits == 0);
assert(debug_native_cache.misses == 2);
assert(debug_native_cache.stores == 2);
#else
assert(compiled_debug_simulation.compiled_process_count() == 0);
assert(compiled_debug_simulation.compiled_module_count() == 0);
#endif
std::size_t compiled_observed_changes = 0;
compiled_debug_simulation.set_signal_change_hook(
    [&compiled_observed_changes](
        fsim::runtime::simir::SignalId,
        const fsim::runtime::PackedLogic4&,
        fsim::runtime::SimulationTick,
        std::uint64_t) { ++compiled_observed_changes; });
compiled_debug_simulation.start();
std::istringstream compiled_debug_input{debug_commands};
std::ostringstream compiled_debug_output;
std::ostringstream compiled_debug_error;
assert(
    fsim::app::run_debug_repl(
        compiled_debug_simulation,
        compiled_debug_input,
        compiled_debug_output,
        compiled_debug_error)
    == 0);
assert(compiled_debug_error.str().empty());
assert(compiled_debug_output.str() == transcript);
assert(compiled_debug_simulation.finished());
assert(!compiled_debug_simulation.poisoned());
assert(compiled_observed_changes == observed_changes);
for (const auto& signal : debug_simulation.design().signals()) {
  assert(
      compiled_debug_simulation.read_signal(signal.id)
      == debug_simulation.read_signal(signal.id));
}

auto poisoned_project = fsim::app::build_project(config, diagnostics);
assert(poisoned_project);
fsim::app::Simulation poisoned_simulation(
    std::move(*poisoned_project), config.run.max_deltas);
#if defined(FSIM_HAS_LLVM)
assert(poisoned_simulation.compiled_process_count() > 0);
#else
assert(poisoned_simulation.compiled_process_count() == 0);
#endif
poisoned_simulation.set_signal_change_hook(
    [](
        fsim::runtime::simir::SignalId,
        const fsim::runtime::PackedLogic4&,
        fsim::runtime::SimulationTick,
        std::uint64_t) {
      throw std::runtime_error("fatal signal observer");
    });
poisoned_simulation.start();
std::istringstream poisoned_input{
    "continue\n"
    "continue\n"
    "step delta\n"
    "quit\n"};
std::ostringstream poisoned_output;
std::ostringstream poisoned_error;
assert(
    fsim::app::run_debug_repl(
        poisoned_simulation,
        poisoned_input,
        poisoned_output,
        poisoned_error)
    == 0);
assert(poisoned_simulation.poisoned());
assert(!poisoned_simulation.finished());
assert(
    poisoned_error.str().find("fatal signal observer")
    != std::string::npos);
const auto unavailable =
    poisoned_output.str().find(
        "simulation is unavailable after a fatal runtime error");
assert(unavailable != std::string::npos);
assert(
    poisoned_output.str().find(
        "simulation is unavailable after a fatal runtime error",
        unavailable + 1)
    != std::string::npos);

const auto manifest = directory / "fsim.toml";
const auto debug_trace = directory / "debug-select.vcd";
{
  std::ofstream output(manifest);
  output << R"(
schema = 2

[project]
name = "debug-cli-test"
top = "sv:work.tb"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "cli-cache"

[run]
max_deltas = 1000
trace_file = "debug-select.vcd"
trace_filters = ["__none__"]
)";
}
std::istringstream cli_input{
    "where\n"
    "trace list\n"
    "trace add q\n"
    "trace list\n"
    "run 1ns\n"
    "trace remove q\n"
    "trace list\n"
    "continue\n"
    "quit\n"};
std::ostringstream cli_output;
std::ostringstream cli_error;
auto services = fsim::app::make_cli_services(cli_input);
const auto manifest_text = manifest.string();
const std::vector<const char*> arguments{
    "fsim", "debug", "-p", manifest_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(arguments.size()),
        arguments.data(),
        services,
        cli_output,
        cli_error)
    == 0);
assert(
    cli_output.str().find("fsim debugger: tb") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
assert(
    cli_output.str().find(
        "(O0 hybrid, 2 compiled process(es) in "
        "2 specialization module(s))")
    != std::string::npos);
#else
assert(
    cli_output.str().find("(reference evaluator)")
    != std::string::npos);
#endif
assert(
    cli_output.str().find("time 0, delta 0, scope tb")
    != std::string::npos);
assert(
    cli_output.str().find("(no traced signals)")
    != std::string::npos);
assert(
    cli_output.str().find("tracing tb.q")
    != std::string::npos);
assert(
    cli_output.str().find("stopped tracing tb.q")
    != std::string::npos);
std::ifstream debug_trace_stream(debug_trace);
const std::string debug_vcd{
    std::istreambuf_iterator<char>{debug_trace_stream},
    std::istreambuf_iterator<char>{}};
assert(!debug_vcd.empty());
std::string q_identifier;
std::istringstream debug_vcd_lines{debug_vcd};
for (std::string line; std::getline(debug_vcd_lines, line);) {
  if (line.starts_with("$var wire 1 ")
      && line.ends_with(" q $end")) {
    std::istringstream declaration{line};
    std::string directive;
    std::string kind;
    std::string width;
    declaration >> directive >> kind >> width >> q_identifier;
    break;
  }
}
assert(!q_identifier.empty());
assert(
    debug_vcd.find("\nx" + q_identifier + "\n")
    != std::string::npos);
assert(
    debug_vcd.find("\n0" + q_identifier + "\n")
    != std::string::npos);
assert(
    debug_vcd.find("\n1" + q_identifier + "\n")
    == std::string::npos);

restored_interrupt_count = 0;
const auto previous_interrupt_handler =
    std::signal(SIGINT, record_restored_interrupt);
assert(previous_interrupt_handler != SIG_ERR);
std::istringstream interrupted_cli_input{
    "continue\n"
    "continue\n"
    "quit\n"};
InterruptingOutputBuffer interrupted_output_buffer;
std::ostream interrupted_cli_output{&interrupted_output_buffer};
std::ostringstream interrupted_cli_error;
auto interrupted_services =
    fsim::app::make_cli_services(interrupted_cli_input);
assert(
    fsim::cli::run(
        static_cast<int>(arguments.size()),
        arguments.data(),
        interrupted_services,
        interrupted_cli_output,
        interrupted_cli_error)
    == 0);
assert(interrupted_cli_error.str().empty());
const auto interrupted_transcript =
    interrupted_output_buffer.str();
assert(
    interrupted_transcript.find("process ")
    != std::string::npos);
assert(
    interrupted_transcript.find("stopped at time 0")
    != std::string::npos);
assert(
    interrupted_transcript.find("simulation finished at time 3")
    != std::string::npos);
(void)std::raise(SIGINT);
assert(restored_interrupt_count == 1);
assert(std::signal(SIGINT, previous_interrupt_handler) != SIG_ERR);

const auto differently_named = directory / "different_filename.sv";
{
  std::ofstream output(differently_named);
  output << "module actual_top; endmodule\n";
}
std::ostringstream direct_output;
std::ostringstream direct_error;
const auto direct_text = differently_named.string();
const std::vector<const char*> direct_arguments{
    "fsim", "run", direct_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(direct_arguments.size()),
        direct_arguments.data(),
        services,
        direct_output,
        direct_error)
    == 0);
assert(
    direct_output.str().find("simulation completed at tick 0")
    != std::string::npos);

const auto unicode_directory =
    directory / fsim::support::path_from_utf8("tool-path-\xC3\xA9");
std::filesystem::create_directories(unicode_directory);
const auto unicode_source =
    unicode_directory / fsim::support::path_from_utf8("source-\xCE\xBB.sv");
{
  std::ofstream output(unicode_source, std::ios::binary);
  output << "module unicode_top; endmodule\n";
}
const auto unicode_text = fsim::support::path_to_utf8(unicode_source);
const auto unicode_include = fsim::support::path_to_utf8(unicode_directory);
const auto unicode_trace = fsim::support::path_to_utf8(
    unicode_directory / fsim::support::path_from_utf8("trace-\xCE\xBB.vcd"));
const std::vector<const char*> unicode_arguments{
    "fsim", "run", "--include", unicode_include.c_str(),
    "--trace", unicode_trace.c_str(), unicode_text.c_str()};
fsim::diagnostic::Engine unicode_diagnostics;
const auto unicode_invocation = fsim::cli::parse_arguments(
    static_cast<int>(unicode_arguments.size()),
    unicode_arguments.data(),
    unicode_diagnostics);
assert(unicode_invocation && !unicode_diagnostics.has_error());
assert(unicode_invocation->files == std::vector{unicode_source});
assert(
    unicode_invocation->include_directories
    == std::vector{unicode_directory});
assert(
    unicode_invocation->trace_file
    == unicode_directory
        / fsim::support::path_from_utf8("trace-\xCE\xBB.vcd"));
const std::vector<const char*> search_arguments{
    "fsim", "build",
    "--search-library", "vendor",
    "--search-library=shared"};
fsim::diagnostic::Engine search_diagnostics;
const auto search_invocation = fsim::cli::parse_arguments(
    static_cast<int>(search_arguments.size()),
    search_arguments.data(),
    search_diagnostics);
assert(search_invocation && !search_diagnostics.has_error());
assert((
    search_invocation->search_libraries
    == std::vector<std::string>{"vendor", "shared"}));
const auto vendor_library = directory / "vendor.fsimlib";
const auto shared_library = directory / "shared.fsimlib";
const auto vendor_library_text = vendor_library.string();
const auto shared_mapping_text =
    std::string{"shared="} + shared_library.string();
const auto vendor_mapping_text =
    std::string{"vendor="} + vendor_library_text;
const std::vector<const char*> mapping_arguments{
    "fsim", "build",
    "--map-library", vendor_mapping_text.c_str(),
    "--map-library", shared_mapping_text.c_str()};
fsim::diagnostic::Engine mapping_diagnostics;
const auto mapping_invocation = fsim::cli::parse_arguments(
    static_cast<int>(mapping_arguments.size()),
    mapping_arguments.data(),
    mapping_diagnostics);
assert(mapping_invocation && !mapping_diagnostics.has_error());
assert((
    mapping_invocation->library_mappings
    == std::vector<fsim::project::LibraryMapping>{
        {"vendor", vendor_library}, {"shared", shared_library}}));
const std::vector<const char*> invalid_mapping_arguments{
    "fsim", "check", "--map-library", "vendor"};
fsim::diagnostic::Engine invalid_mapping_diagnostics;
assert(!fsim::cli::parse_arguments(
    static_cast<int>(invalid_mapping_arguments.size()),
    invalid_mapping_arguments.data(),
    invalid_mapping_diagnostics));
assert(std::ranges::any_of(
    invalid_mapping_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.message.find("LIBRARY=DIRECTORY")
          != std::string::npos;
    }));
const auto export_library_path = directory / "cli-export.fsimlib";
const auto export_mapping_text =
    std::string{"work="} + export_library_path.string();
const std::vector<const char*> export_arguments{
    "fsim", "build", "--export-library", export_mapping_text.c_str()};
fsim::diagnostic::Engine export_option_diagnostics;
const auto export_invocation = fsim::cli::parse_arguments(
    static_cast<int>(export_arguments.size()),
    export_arguments.data(),
    export_option_diagnostics);
assert(export_invocation && !export_option_diagnostics.has_error());
assert((export_invocation->library_exports
    == std::vector<fsim::project::LibraryMapping>{
        {"work", export_library_path}}));
const std::vector<const char*> top_arguments{
    "fsim", "build",
    "--top", "source=sv:work.producer",
    "--top=sink=consumer"};
fsim::diagnostic::Engine top_diagnostics;
const auto top_invocation = fsim::cli::parse_arguments(
    static_cast<int>(top_arguments.size()),
    top_arguments.data(),
    top_diagnostics);
assert(top_invocation && !top_diagnostics.has_error());
assert((
    top_invocation->tops
    == std::vector<fsim::project::ProjectSection::TopLevel>{
        {"sv:work.producer", "source"},
        {"consumer", "sink"}}));
assert(!top_invocation->top.has_value());
const std::vector<const char*> invalid_top_arguments{
    "fsim", "build", "--top", "producer", "--top", "sink=consumer"};
fsim::diagnostic::Engine invalid_top_diagnostics;
assert(!fsim::cli::parse_arguments(
    static_cast<int>(invalid_top_arguments.size()),
    invalid_top_arguments.data(),
    invalid_top_diagnostics));
assert(std::ranges::any_of(
    invalid_top_diagnostics.diagnostics(),
    [](const auto& diagnostic) {
      return diagnostic.message.find("every repeated --top")
          != std::string::npos;
    }));
const auto search_manifest = directory / "search-override.toml";
{
  std::ofstream output(search_manifest);
  output << R"(schema = 2
[project]
top = "actual_top"
[elaboration]
search_libraries = ["manifest_only"]
[[library_map]]
library = "manifest_only"
path = "manifest-only.fsimlib"
[[source_set]]
language = "systemverilog"
library = "work"
files = ["different_filename.sv"]
)";
  assert(output.good());
}
bool search_handler_called = false;
fsim::cli::Services search_services;
search_services.check =
    [&](const fsim::cli::Invocation&,
        const fsim::project::Config& captured,
        fsim::diagnostic::Engine&,
        std::ostream&,
        std::ostream&) {
      search_handler_called = true;
      assert((
          captured.elaboration.search_libraries
          == std::vector<std::string>{"vendor", "shared"}));
      assert((
          captured.library_mappings
          == std::vector<fsim::project::LibraryMapping>{
              {"vendor", vendor_library}, {"shared", shared_library}}));
      assert(captured.project.top.empty());
      assert((
          captured.project.tops
          == std::vector<fsim::project::ProjectSection::TopLevel>{
              {"actual_top", "primary"},
              {"actual_top", "secondary"}}));
      return 0;
    };
const auto search_manifest_text = search_manifest.string();
const std::vector<const char*> search_override_arguments{
    "fsim", "check", "--project", search_manifest_text.c_str(),
    "--search-library", "vendor", "--search-library", "shared",
    "--map-library", vendor_mapping_text.c_str(),
    "--map-library", shared_mapping_text.c_str(),
    "--top", "primary=actual_top", "--top", "secondary=actual_top"};
std::ostringstream search_output;
std::ostringstream search_error;
assert(
    fsim::cli::run(
        static_cast<int>(search_override_arguments.size()),
        search_override_arguments.data(),
        search_services,
        search_output,
        search_error)
    == 0);
assert(search_handler_called);
assert(search_error.str().empty());
std::ostringstream unicode_output;
std::ostringstream unicode_error;
assert(
    fsim::cli::run(
        static_cast<int>(unicode_arguments.size()),
        unicode_arguments.data(),
        services,
        unicode_output,
        unicode_error)
    == 0);
assert(unicode_error.str().empty());
assert(
    unicode_output.str().find("simulation completed at tick 0")
    != std::string::npos);

std::ostringstream json_output;
std::ostringstream json_error;
const std::vector<const char*> json_arguments{
    "fsim",
    "check",
    "--diagnostics=json",
    "--definitely-invalid"};
assert(
    fsim::cli::run(
        static_cast<int>(json_arguments.size()),
        json_arguments.data(),
        services,
        json_output,
        json_error)
    == 2);
assert(
    json_error.str().find("\"code\":\"FSIM-CLI-0001\"")
    != std::string::npos);

std::ostringstream standard_output;
std::ostringstream standard_error;
const std::vector<const char*> standard_arguments{
    "fsim",
    "check",
    "--standard=bogus",
    direct_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(standard_arguments.size()),
        standard_arguments.data(),
        services,
        standard_output,
        standard_error)
    == 1);
assert(
    standard_error.str().find("unsupported standard 'bogus'")
    != std::string::npos);

const auto scaled_manifest = directory / "scaled.toml";
const auto scaled_trace = directory / "scaled.vcd";
{
  std::ofstream output(scaled_manifest);
  output << R"(
schema = 2
[project]
name = "scaled-vcd"
top = "sv:work.tb"
time_resolution = "2ps"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "scaled-cache"

[run]
max_deltas = 1000
trace_file = "scaled.vcd"
)";
}
std::ostringstream scaled_output;
std::ostringstream scaled_error;
const auto scaled_manifest_text = scaled_manifest.string();
const std::vector<const char*> scaled_arguments{
    "fsim", "run", "-p", scaled_manifest_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(scaled_arguments.size()),
        scaled_arguments.data(),
        services,
        scaled_output,
        scaled_error)
    == 0);
std::ifstream scaled_stream(scaled_trace);
const std::string scaled_vcd{
    std::istreambuf_iterator<char>{scaled_stream},
    std::istreambuf_iterator<char>{}};
assert(
    scaled_vcd.find("$timescale 1ps $end") != std::string::npos);
assert(scaled_vcd.find("#4") != std::string::npos);

const auto timescale_source = directory / "timescale.sv";
{
  std::ofstream output(timescale_source);
  output << R"(`timescale 10ns/100ps
module timed;
initial #2 $finish;
endmodule
)";
}
fsim::project::Config timescale_config;
timescale_config.base_directory = directory;
timescale_config.project.name = "timescale";
timescale_config.project.top = "sv:work.timed";
timescale_config.project.time_resolution = "auto";
timescale_config.build.cache_path = directory / "timescale-cache";
timescale_config.run.max_deltas = 1000;
fsim::project::SourceSet timescale_sources;
timescale_sources.language =
    fsim::project::Language::system_verilog;
timescale_sources.standard = "2017";
timescale_sources.library = "work";
timescale_sources.files.push_back(timescale_source);
timescale_config.source_sets.push_back(
    std::move(timescale_sources));
fsim::diagnostic::Engine timescale_diagnostics;
auto timed_project =
    fsim::app::build_project(
        timescale_config, timescale_diagnostics);
assert(timed_project);
assert(timed_project->time_resolution == "100ps");
fsim::app::Simulation timed_simulation(
    std::move(*timed_project),
    timescale_config.run.max_deltas);
const auto timed_result = timed_simulation.run();
assert(timed_result.status == fsim::runtime::RunStatus::stopped);
assert(timed_result.time == 200);
timescale_config.project.time_resolution = "1ns";
fsim::diagnostic::Engine coarse_time_diagnostics;
assert(!fsim::app::build_project(
    timescale_config, coarse_time_diagnostics));
assert(coarse_time_diagnostics.has_error());
}

}  // namespace fsim::test
