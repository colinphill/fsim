// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/library/portable_unit.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FSIM_TEST_ASAN_ENABLED 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__) && !defined(FSIM_TEST_ASAN_ENABLED)
#define FSIM_TEST_ASAN_ENABLED 1
#endif

namespace fsim::test {
namespace {

std::string read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  assert(input);
  const std::string result{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
  assert(!input.bad());
  return result;
}

std::vector<library::PortablePayload> object_payloads(
    const std::filesystem::path& directory,
    const artifact::ObjectMetadata& metadata) {
  std::vector<library::PortablePayload> result;
  for (const auto& source : metadata.sources) {
    result.push_back({
        source.artifact, read_binary_file(directory / source.artifact)});
  }
  for (const auto& unit : metadata.units) {
    result.push_back({
        unit.artifact, read_binary_file(directory / unit.artifact)});
  }
  return result;
}

void copy_tree(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
  std::filesystem::create_directories(destination);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(source)) {
    const auto relative = std::filesystem::relative(entry.path(), source);
    if (entry.is_directory()) {
      std::filesystem::create_directories(destination / relative);
    } else {
      std::filesystem::copy_file(entry.path(), destination / relative);
    }
  }
}

void make_tree_writable(const std::filesystem::path& root) {
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(root)) {
    std::filesystem::permissions(
        entry.path(), std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add);
  }
  std::filesystem::permissions(
      root, std::filesystem::perms::owner_all,
      std::filesystem::perm_options::add);
}

}  // namespace

void ApplicationTestFixture::test_non_project_cli() {
  const auto object = directory / "unit.fsimobj";
  const auto extra_object = directory / "extra.fsimobj";
  const auto extra_source = directory / "extra.sv";
  const auto design = directory / "design.fsimdesign";
  const auto trace = directory / "phase.vcd";
  const auto consumer_cache = directory / "consumer-cache";
  const auto consumer_file_root = directory / "consumer-files";
  const auto source_text = fsim::support::path_to_utf8(source);
  const auto object_text = fsim::support::path_to_utf8(object);
  const auto extra_object_text = fsim::support::path_to_utf8(extra_object);
  const auto extra_source_text = fsim::support::path_to_utf8(extra_source);
  const auto design_text = fsim::support::path_to_utf8(design);
  const auto trace_text = fsim::support::path_to_utf8(trace);
  const auto consumer_cache_text =
      fsim::support::path_to_utf8(consumer_cache);
  const auto consumer_file_root_text =
      fsim::support::path_to_utf8(consumer_file_root);

  const std::vector<const char*> compile_arguments{
      "fsim", "compile", "--lang", "systemverilog", "--standard",
      "2017", "--library", "work", "--output", object_text.c_str(),
      source_text.c_str()};
  {
    std::ofstream extra_output(extra_source);
    extra_output << "`define LEAK\nmodule extra; endmodule\n";
  }
  const std::vector<const char*> extra_compile_arguments{
      "fsim", "compile", "--lang", "systemverilog", "--standard",
      "2017", "--library", "work", "--output", extra_object_text.c_str(),
      extra_source_text.c_str()};
  diagnostic::Engine compile_diagnostics;
  const auto compile = cli::parse_arguments(
      static_cast<int>(compile_arguments.size()),
      compile_arguments.data(), compile_diagnostics);
  assert(compile && !compile_diagnostics.has_error());
  assert(compile->command == cli::Command::compile);
  assert(compile->artifact_output == object);
  assert(compile->files == std::vector{source});
  const std::vector<const char*> uvm_release_arguments{
      "fsim", "check", "--uvm-release", "uvm-2020.3.1",
      source_text.c_str()};
  diagnostic::Engine uvm_release_diagnostics;
  const auto uvm_release = cli::parse_arguments(
      static_cast<int>(uvm_release_arguments.size()),
      uvm_release_arguments.data(), uvm_release_diagnostics);
  assert(uvm_release && !uvm_release_diagnostics.has_error());
  assert(uvm_release->uvm_release
      == project::SystemVerilogUvmRelease::ieee_1800_2_2020_3_1);
  const std::vector<const char*> invalid_uvm_release_arguments{
      "fsim", "check", "--uvm-release", "2024", source_text.c_str()};
  diagnostic::Engine invalid_uvm_release_diagnostics;
  assert(!cli::parse_arguments(
      static_cast<int>(invalid_uvm_release_arguments.size()),
      invalid_uvm_release_arguments.data(),
      invalid_uvm_release_diagnostics));

  const std::vector<const char*> elaborate_arguments{
      "fsim", "elaborate", "--object", object_text.c_str(), "--top",
      "primary=sv:work.tb", "--search-library", "vendor", "--output",
      design_text.c_str(), "--delay-mode", "max", "--seed", "7"};
  diagnostic::Engine elaborate_diagnostics;
  const auto elaborate = cli::parse_arguments(
      static_cast<int>(elaborate_arguments.size()),
      elaborate_arguments.data(), elaborate_diagnostics);
  assert(elaborate && !elaborate_diagnostics.has_error());
  assert(elaborate->command == cli::Command::elaborate);
  assert(elaborate->objects == std::vector{object});
  assert(elaborate->artifact_output == design);
  assert(elaborate->tops.size() == 1);
  assert(elaborate->tops.front().alias == "primary");

  const std::vector<const char*> simulate_arguments{
      "fsim", "simulate", "--design", design_text.c_str(), "--engine",
      "compiled", "--duration", "10ns", "--max-deltas", "1000",
      "--trace", trace_text.c_str(), "--trace-filter", "primary.*",
      "--cache", consumer_cache_text.c_str(), "--file-root",
      consumer_file_root_text.c_str()};
  diagnostic::Engine simulate_diagnostics;
  const auto simulate = cli::parse_arguments(
      static_cast<int>(simulate_arguments.size()),
      simulate_arguments.data(), simulate_diagnostics);
  assert(simulate && !simulate_diagnostics.has_error());
  assert(simulate->command == cli::Command::simulate);
  assert(simulate->design == design);
  assert(simulate->engine == "compiled");
  assert(simulate->trace_filters == std::vector<std::string>{"primary.*"});
  assert(simulate->cache_directory == consumer_cache);
  assert(simulate->file_root == consumer_file_root);

  const auto incremental_systemc_source = directory / "incremental.cpp";
  const auto systemc_object = directory / "incremental.fsimscobj";
  const auto systemc_plugin = directory / "incremental.fsimscplugin";
  const auto systemc_design = directory / "incremental.fsimdesign";
  const auto systemc_source_text =
      support::path_to_utf8(incremental_systemc_source);
  const auto systemc_object_text = support::path_to_utf8(systemc_object);
  const auto systemc_plugin_text = support::path_to_utf8(systemc_plugin);
  const std::vector<const char*> systemc_compile_arguments{
      "fsim", "systemc", "compile", "--output",
      systemc_object_text.c_str(), "--define", "WIDTH=8",
      "--compile-option", "-fno-omit-frame-pointer",
      systemc_source_text.c_str()};
  diagnostic::Engine systemc_compile_diagnostics;
  const auto systemc_compile = cli::parse_arguments(
      static_cast<int>(systemc_compile_arguments.size()),
      systemc_compile_arguments.data(), systemc_compile_diagnostics);
  assert(systemc_compile && !systemc_compile_diagnostics.has_error());
  assert(systemc_compile->command == cli::Command::systemc_compile);
  assert(systemc_compile->files == std::vector{incremental_systemc_source});
  assert(systemc_compile->artifact_output == systemc_object);
  assert(systemc_compile->defines == std::vector<std::string>{"WIDTH=8"});
  assert(systemc_compile->systemc_compile_options
      == std::vector<std::string>{"-fno-omit-frame-pointer"});

  const std::vector<const char*> systemc_link_arguments{
      "fsim", "systemc", "link", "--object",
      systemc_object_text.c_str(), "--library", "vendor", "--link-option",
      "-Wl,--no-undefined",
#if defined(FSIM_TEST_ASAN_ENABLED)
      "--link-option", "-fsanitize=address,undefined",
#endif
      "--output", systemc_plugin_text.c_str()};
  diagnostic::Engine systemc_link_diagnostics;
  const auto systemc_link = cli::parse_arguments(
      static_cast<int>(systemc_link_arguments.size()),
      systemc_link_arguments.data(), systemc_link_diagnostics);
  assert(systemc_link && !systemc_link_diagnostics.has_error());
  assert(systemc_link->command == cli::Command::systemc_link);
  assert(systemc_link->objects == std::vector{systemc_object});
  assert(systemc_link->library == "vendor");
  assert(systemc_link->artifact_output == systemc_plugin);

  const std::vector<const char*> systemc_elaborate_arguments{
      "fsim", "elaborate", "--object", object_text.c_str(),
      "--systemc-plugin", systemc_plugin_text.c_str(), "--top",
      "systemc:vendor.first", "--output", design_text.c_str()};
  diagnostic::Engine systemc_elaborate_diagnostics;
  const auto systemc_elaborate = cli::parse_arguments(
      static_cast<int>(systemc_elaborate_arguments.size()),
      systemc_elaborate_arguments.data(), systemc_elaborate_diagnostics);
  assert(systemc_elaborate && !systemc_elaborate_diagnostics.has_error());
  assert(systemc_elaborate->systemc_plugins == std::vector{systemc_plugin});

  const std::vector<const char*> project_arguments {
      "fsim", "simulate", "--project", "fsim.toml", "--design",
      design_text.c_str()
  };
  diagnostic::Engine project_diagnostics;
  assert(!cli::parse_arguments(
      static_cast<int>(project_arguments.size()),
      project_arguments.data(), project_diagnostics));

  bool compile_called = false;
  cli::Services services;
  const std::vector<const char*> vhdl_standard_arguments {
      "fsim", "check", "--lang", "vhdl-93", "--standard", "93",
      source_text.c_str()
  };
  const std::vector<const char*> verilog_standard_arguments {
      "fsim", "check", "--lang", "verilog-2001-noconfig", "--standard",
      "v2001-noconfig", source_text.c_str()
  };
  const std::vector<const char*> systemverilog_standard_arguments {
      "fsim", "check", "--lang", "sv-2009", "--standard", "09",
      source_text.c_str()
  };
  int standard_calls = 0;
  services.check =
      [&](const cli::Invocation& invocation,
          const project::Config& config,
          diagnostic::Engine&,
          std::ostream&,
          std::ostream&) {
          assert(config.source_sets.size() == 1);
          if (standard_calls == 0) {
              assert(invocation.language == project::Language::vhdl);
              assert(invocation.standard == "93");
              assert(config.source_sets.front().standard == "1993");
          } else if (standard_calls == 1) {
              assert(invocation.language == project::Language::verilog);
              assert(invocation.standard == "v2001-noconfig");
              assert(config.source_sets.front().standard == "2001-noconfig");
          } else {
              assert(invocation.language == project::Language::system_verilog);
              assert(invocation.standard == "09");
              assert(config.source_sets.front().standard == "2009");
          }
          ++standard_calls;
          return 0;
      };
  services.compile =
      [&](const cli::Invocation& invocation,
          const project::Config& config,
          diagnostic::Engine&,
          std::ostream&,
          std::ostream&) {
          compile_called = true;
          assert(invocation.command == cli::Command::compile);
          assert(config.source_sets.size() == 1);
          assert(config.source_sets.front().files == std::vector { source });
          return 0;
      };
  std::ostringstream output;
  std::ostringstream error;
  assert(cli::run(
             static_cast<int>(vhdl_standard_arguments.size()),
             vhdl_standard_arguments.data(), services, output, error)
      == 0);
  output.str({ });
  error.str({ });
  assert(cli::run(
             static_cast<int>(verilog_standard_arguments.size()),
             verilog_standard_arguments.data(), services, output, error)
      == 0);
  output.str({ });
  error.str({ });
  assert(cli::run(
             static_cast<int>(systemverilog_standard_arguments.size()),
             systemverilog_standard_arguments.data(), services, output, error)
      == 0);
  assert(standard_calls == 3);
  assert(error.str().empty());
  const std::vector<const char*> help_arguments { "fsim", "--help" };
  output.str({ });
  error.str({ });
  assert(cli::run(
             static_cast<int>(help_arguments.size()), help_arguments.data(),
             services, output, error)
      == 0);
  assert(output.str().find("Verilog: 95/1995") != std::string::npos);
  assert(output.str().find("SystemVerilog: 05/2005") != std::string::npos);
  assert(error.str().empty());
  output.str({ });
  error.str({ });
  assert(cli::run(
             static_cast<int>(compile_arguments.size()), compile_arguments.data(),
             services, output, error)
      == 0);
  assert(compile_called);
  assert(error.str().empty());

  bool elaborate_called = false;
  services.elaborate =
      [&](const cli::Invocation& invocation,
          const project::Config& config,
          diagnostic::Engine&,
          std::ostream&,
          std::ostream&) {
          elaborate_called = true;
          assert(invocation.command == cli::Command::elaborate);
          assert(config.manifest_path == "<non-project>");
          assert(config.project.tops == invocation.tops);
          return 0;
      };
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(elaborate_arguments.size()), elaborate_arguments.data(),
      services, output, error) == 0);
  assert(elaborate_called);
  assert(error.str().empty());

  output.str({});
  error.str({});
  auto production_services = app::make_cli_services();

  {
    std::ofstream systemc_output(incremental_systemc_source);
    systemc_output << R"(#include "fsim/systemc.hpp"
SC_MODULE(IncrementalTop) {
  sc_core::sc_signal<sc_dt::sc_uint<8>> value{"value"};
  SC_CTOR(IncrementalTop) {
    SC_METHOD(initialize);
  }
  void initialize() {
    value.write(sc_dt::sc_uint<8>{5});
  }
};
SC_FSIM_EXPORT_AS(IncrementalTop, "first");
)";
  }
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(systemc_compile_arguments.size()),
      systemc_compile_arguments.data(), production_services,
      output, error) == 0);
  assert(error.str().empty());
  output.str({});
  error.str({});
  const auto systemc_link_status = cli::run(
      static_cast<int>(systemc_link_arguments.size()),
      systemc_link_arguments.data(), production_services,
      output, error);
  if (systemc_link_status != 0) {
    std::cerr << error.str();
  }
  assert(systemc_link_status == 0);
  assert(error.str().empty());
  diagnostic::Engine systemc_object_inspection_diagnostics;
  const auto systemc_object_inspection = app::inspect_artifact(
      systemc_object, systemc_object_inspection_diagnostics);
  assert(systemc_object_inspection);
  assert(!systemc_object_inspection_diagnostics.has_error());
  assert(systemc_object_inspection->phase
      == app::ArtifactPhaseKind::systemc_compilation);
  assert(systemc_object_inspection->language == "systemc");
  assert(systemc_object_inspection->toolchain.has_value());
  assert(systemc_object_inspection->target.has_value());
  diagnostic::Engine systemc_plugin_inspection_diagnostics;
  const auto systemc_plugin_inspection = app::inspect_artifact(
      systemc_plugin, systemc_plugin_inspection_diagnostics);
  assert(systemc_plugin_inspection);
  assert(!systemc_plugin_inspection_diagnostics.has_error());
  assert(systemc_plugin_inspection->phase
      == app::ArtifactPhaseKind::systemc_link);
  assert(systemc_plugin_inspection->library == "vendor");
  assert(systemc_plugin_inspection->units
      == std::vector<std::string>{"systemc:vendor.first"});
  project::Config systemc_phase_config;
  systemc_phase_config.base_directory = directory;
  systemc_phase_config.project.name = "systemc-phase";
  systemc_phase_config.project.top = "systemc:vendor.first";
  systemc_phase_config.build.cache_path = directory / "systemc-phase-cache";
  const std::vector<std::filesystem::path> no_hdl_objects;
  const std::vector systemc_plugin_inputs{systemc_plugin};
  diagnostic::Engine systemc_publish_diagnostics;
  const auto systemc_published = app::elaborate_artifact(
      systemc_phase_config, no_hdl_objects, systemc_plugin_inputs,
      systemc_design,
      systemc_publish_diagnostics);
  if (!systemc_published) {
    diagnostic::print_text(std::cerr, systemc_publish_diagnostics);
  }
  assert(systemc_published);
  assert(!systemc_publish_diagnostics.has_error());

  diagnostic::Engine systemc_design_metadata_diagnostics;
  const auto systemc_design_metadata = artifact::load_design_metadata(
      systemc_design, systemc_design_metadata_diagnostics);
  assert(systemc_design_metadata);
  assert(!systemc_design_metadata_diagnostics.has_error());
  assert(systemc_design_metadata->format == artifact::kDesignFormatVersion);
  assert(systemc_design_metadata->objects.empty());
  assert(systemc_design_metadata->systemc_plugins.size() == 1);
  assert(systemc_design_metadata->systemc_plugins.front().logical_library
      == "vendor");
  assert(systemc_design_metadata->systemc_plugins.front().factories
      == std::vector<std::string>{"first"});

  const auto hidden_systemc_source =
      directory / "incremental.cpp.producer-hidden";
  const auto hidden_systemc_object =
      directory / "incremental.fsimscobj.producer-hidden";
  const auto systemc_plugin_metadata = systemc_plugin
      / systemc::kIncrementalPluginMetadataFilename;
  const auto hidden_systemc_plugin_metadata = systemc_plugin
      / (std::string{systemc::kIncrementalPluginMetadataFilename}
         + ".producer-hidden");
  const auto rename_producer = [&](
      const std::filesystem::path& producer_path,
      const std::filesystem::path& destination,
      const std::string_view label) {
    std::error_code operation_error;
    std::filesystem::rename(
        producer_path, destination, operation_error);
    if (operation_error) {
      std::cerr << "non-project producer hiding: " << label << ": "
                << operation_error.value() << " ("
                << operation_error.message() << ")\n";
    }
    assert(!operation_error);
    std::cerr << "non-project producer hiding: " << label << " complete\n";
  };
  const auto make_writable = [&](
      const std::filesystem::path& path, const std::string_view label) {
    std::error_code operation_error;
    std::filesystem::permissions(
        path, std::filesystem::perms::owner_write,
        std::filesystem::perm_options::add, operation_error);
    if (operation_error) {
      std::cerr << "non-project producer hiding: " << label << ": "
                << operation_error.value() << " ("
                << operation_error.message() << ")\n";
    }
    assert(!operation_error);
    std::cerr << "non-project producer hiding: " << label << " complete\n";
  };
  rename_producer(
      incremental_systemc_source, hidden_systemc_source, "source rename");
  rename_producer(systemc_object, hidden_systemc_object, "object rename");
  // Windows locks a loaded DLL against renaming its containing artifact.
  // Hiding the required metadata makes the producer artifact unusable while
  // leaving the loaded native image at its stable path. Publication makes
  // both the artifact and its contents read-only, so restore write access only
  // to the directory and metadata file being renamed.
  make_writable(systemc_plugin, "artifact permissions");
  make_writable(systemc_plugin_metadata, "metadata permissions");
  rename_producer(
      systemc_plugin_metadata, hidden_systemc_plugin_metadata,
      "metadata rename");
  std::cerr << "non-project producer hiding: loading embedded design\n";
  diagnostic::Engine systemc_design_load_diagnostics;
  auto loaded_systemc_design = app::load_design_artifact(
      systemc_design, systemc_design_load_diagnostics);
  if (!loaded_systemc_design) {
    diagnostic::print_text(std::cerr, systemc_design_load_diagnostics);
  }
  assert(loaded_systemc_design && !systemc_design_load_diagnostics.has_error());
  std::cerr << "non-project producer hiding: embedded design loaded\n";
  assert(loaded_systemc_design->systemc_hierarchies.size() == 1);
  assert(loaded_systemc_design->systemc_roots.size() == 1);
  assert(loaded_systemc_design->systemc_plugins.size() == 1);
  assert(loaded_systemc_design->design.systemc_processes().size() == 1);
  assert(loaded_systemc_design->design.systemc_instances().size() == 1);
  assert(loaded_systemc_design->design.systemc_instances().front()
      .internal_signals.size() == 1);
  std::cerr << "non-project producer hiding: embedded design validated\n";
  const auto systemc_value = loaded_systemc_design->design
      .systemc_instances().front().internal_signals.front().signal;
  app::Simulation systemc_standalone_simulation{
      std::move(*loaded_systemc_design), 1000,
      app::SimulationEngine::interpreter};
  const auto systemc_standalone_result =
      systemc_standalone_simulation.run();
  std::cerr << "non-project producer hiding: embedded simulation completed\n";
  const auto systemc_standalone_value =
      systemc_standalone_simulation.read_signal(systemc_value).to_msb_string();
  std::cerr << "non-project producer hiding: simulation status="
            << static_cast<int>(systemc_standalone_result.status)
            << " callbacks=" << systemc_standalone_result.callbacks_executed
            << " value=" << systemc_standalone_value << '\n';
  assert(systemc_standalone_result.status == runtime::RunStatus::completed);
  assert(systemc_standalone_result.callbacks_executed != 0);
  assert(systemc_standalone_value == "00000101");
  std::cerr << "non-project producer hiding: embedded simulation validated\n";

  assert(cli::run(
      static_cast<int>(compile_arguments.size()), compile_arguments.data(),
      production_services, output, error) == 0);
  assert(error.str().empty());
  assert(output.str().find("compiled 1 source file(s)") != std::string::npos);

  diagnostic::Engine object_diagnostics;
  const auto metadata = artifact::load_object_metadata(
      object, object_diagnostics);
  assert(metadata && !object_diagnostics.has_error());
  assert(metadata->language == "systemverilog");
  assert(metadata->standard == "2017");
  assert(metadata->library == "work");
  assert(metadata->compilation_unit == "source-set");
  assert(metadata->sources.size() == 1);
  assert(!metadata->units.empty());
  assert(std::ranges::any_of(metadata->units, [](const auto& indexed) {
    return indexed.name == "tb";
  }));
  std::cerr << "non-project cli: HDL object metadata validated\n";

  const auto& indexed_unit = metadata->units.front();
  std::ifstream unit_input(object / indexed_unit.artifact, std::ios::binary);
  assert(unit_input);
  const std::string unit_bytes{
      std::istreambuf_iterator<char>{unit_input},
      std::istreambuf_iterator<char>{}};
  assert(!unit_input.bad());
  unit_input.close();
  assert(!unit_input.is_open());
  diagnostic::Engine unit_diagnostics;
  const auto unit = library::deserialize_portable_unit(
      unit_bytes, support::path_to_utf8(indexed_unit.artifact), unit_diagnostics);
  assert(unit && !unit_diagnostics.has_error());
  assert(unit->name == indexed_unit.name);
  assert(!unit->span.source_name.empty());
  assert(!std::filesystem::path(unit->span.source_name).is_absolute());
  std::cerr << "non-project cli: portable unit validated\n";

  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(extra_compile_arguments.size()),
      extra_compile_arguments.data(), production_services,
      output, error) == 0);
  assert(error.str().empty());

  const auto hidden_source = directory / "tb.sv.producer-hidden";
  const auto hidden_extra_source = directory / "extra.sv.producer-hidden";
  std::filesystem::rename(source, hidden_source);
  std::filesystem::rename(extra_source, hidden_extra_source);
  diagnostic::Engine load_diagnostics;
  const std::vector object_inputs{object, extra_object};
  const auto loaded = app::load_objects(object_inputs, load_diagnostics);
  assert(loaded && !load_diagnostics.has_error());
  assert(loaded->objects.size() == 2);
  assert(loaded->parsed.units.size() == 3);
  assert(loaded->parsed.units[0].name == "child");
  assert(loaded->parsed.units[1].name == "tb");
  assert(loaded->parsed.units[2].name == "extra");
  assert(loaded->semantics.valid());
  std::cerr << "non-project cli: relocated objects loaded\n";
  project::Config object_config;
  object_config.base_directory = directory;
  object_config.project.name = "non-project-object-build";
  object_config.project.tops.push_back({"sv:work.tb", "primary"});
  object_config.project.time_resolution = "1ns";
  object_config.build.cache_path = directory / "object-build-cache";
  diagnostic::Engine object_build_diagnostics;
  const auto object_build = app::build_objects(
      object_config, object_inputs, object_build_diagnostics);
  assert(object_build && !object_build_diagnostics.has_error());
  assert(object_build->design.roots() == std::vector<std::string>{"primary"});
  assert(!object_build->design.processes().empty());
  assert(object_build->design_ir.valid(object_build->semantics));
  const auto object_provenance = app::verilog_scope_provenance(*object_build);
  assert(!object_provenance.empty());
  assert(std::ranges::all_of(
      object_provenance, [](const auto& provenance) {
        return provenance.language == semantic::Language::system_verilog
            && provenance.standard == "systemverilog-2017"
            && provenance.compatibility_profile == "none"
            && !std::filesystem::path(provenance.source_path).is_absolute();
      }));
  assert(std::ranges::any_of(
      object_provenance, [](const auto& provenance) {
        return provenance.path == "primary"
            && provenance.semantic_unit == "work::tb";
      }));
  const auto provenance_signature = [](const auto& provenance) {
    std::vector<std::string> result;
    result.reserve(provenance.size());
    for (const auto& item : provenance) {
      result.push_back(
          item.path + "|" + item.semantic_unit + "|"
          + std::to_string(item.unit.value()) + "|"
          + std::to_string(item.source.value()) + "|" + item.source_path
          + "|" + item.standard + "|" + item.compatibility_profile);
    }
    return result;
  };
  const auto object_provenance_signature =
      provenance_signature(object_provenance);
  diagnostic::Engine state_diagnostics;
  const auto runtime_state = app::serialize_runtime_state(
      object_build->design, state_diagnostics);
  const auto semantic_state = app::serialize_semantic_state(
      object_build->semantics, state_diagnostics);
  const auto design_ir_state = app::serialize_design_ir_state(
      object_build->design_ir, state_diagnostics);
  assert(runtime_state && semantic_state && design_ir_state);
  assert(!state_diagnostics.has_error());
  diagnostic::Engine restore_diagnostics;
  const auto restored_runtime = app::deserialize_runtime_state(
      *runtime_state, "runtime", restore_diagnostics);
  const auto restored_semantics = app::deserialize_semantic_state(
      *semantic_state, "semantics", restore_diagnostics);
  const auto restored_design_ir = app::deserialize_design_ir_state(
      *design_ir_state, "design-ir", restore_diagnostics);
  assert(restored_runtime && restored_semantics && restored_design_ir);
  assert(!restore_diagnostics.has_error());
  assert(restored_runtime->signal_paths()
      == object_build->design.signal_paths());
  assert(restored_runtime->processes().size()
      == object_build->design.processes().size());
  assert(restored_design_ir->valid(*restored_semantics));
  diagnostic::Engine deterministic_state_diagnostics;
  assert(app::serialize_runtime_state(
      *restored_runtime, deterministic_state_diagnostics) == runtime_state);
  assert(app::serialize_semantic_state(
      *restored_semantics, deterministic_state_diagnostics) == semantic_state);
  assert(app::serialize_design_ir_state(
      *restored_design_ir, deterministic_state_diagnostics) == design_ir_state);
  std::cerr << "non-project cli: state round trip validated\n";

  const std::vector<const char*> production_elaborate_arguments{
      "fsim", "elaborate", "--object", object_text.c_str(), "--object",
      extra_object_text.c_str(), "--top", "primary=sv:work.tb", "--output",
      design_text.c_str(), "--delay-mode", "typ", "--seed", "9"};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(production_elaborate_arguments.size()),
      production_elaborate_arguments.data(), production_services,
      output, error) == 0);
  assert(error.str().empty());
  assert(output.str().find("elaborated 1 root(s)") != std::string::npos);
  diagnostic::Engine design_metadata_diagnostics;
  const auto published_design_metadata = artifact::load_design_metadata(
      design, design_metadata_diagnostics);
  assert(published_design_metadata && !design_metadata_diagnostics.has_error());
  assert(published_design_metadata->objects.size() == 2);
  assert(
      published_design_metadata->roots.front().selected_identity
      == "sv:work.tb");
  std::cerr << "non-project cli: HDL design published\n";

  const auto hidden_object = directory / "unit.fsimobj.producer-hidden";
  const auto hidden_extra_object = directory / "extra.fsimobj.producer-hidden";
  make_writable(object, "HDL object permissions");
  make_writable(extra_object, "extra HDL object permissions");
  rename_producer(object, hidden_object, "HDL object rename");
  rename_producer(extra_object, hidden_extra_object, "extra HDL object rename");
  std::cerr << "non-project cli: loading embedded HDL design\n";
  diagnostic::Engine design_load_diagnostics;
  auto loaded_design = app::load_design_artifact(
      design, design_load_diagnostics);
  if (!loaded_design) {
    diagnostic::print_text(std::cerr, design_load_diagnostics);
  }
  assert(loaded_design && !design_load_diagnostics.has_error());
  std::cerr << "non-project cli: embedded HDL design loaded\n";
  assert(loaded_design->design_ir.valid(loaded_design->semantics));
  assert(loaded_design->artifact_identity
      == published_design_metadata->design_digest);
  const auto loaded_provenance = app::verilog_scope_provenance(*loaded_design);
  assert(provenance_signature(loaded_provenance)
      == object_provenance_signature);
  app::Simulation standalone_simulation{
      std::move(*loaded_design), 1000, app::SimulationEngine::interpreter};
  assert(provenance_signature(
             standalone_simulation.verilog_scope_provenance())
      == object_provenance_signature);
  const auto standalone_result = standalone_simulation.run();
  assert(standalone_result.status == runtime::RunStatus::stopped);
  assert(standalone_result.time == 3);
  std::cerr << "non-project cli: HDL standalone simulation validated\n";
  std::filesystem::create_directories(consumer_cache);
  std::filesystem::create_directories(consumer_file_root);
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(simulate_arguments.size()), simulate_arguments.data(),
      production_services, output, error) == 0);
  assert(error.str().empty());
  assert(output.str().find("simulation stopped at tick 3")
      != std::string::npos);
  assert(std::filesystem::is_regular_file(trace));
  assert(read_binary_file(trace).find("primary") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
  assert(std::filesystem::is_directory(consumer_cache / "llvm-native"));
#else
  assert(!std::filesystem::exists(consumer_cache / "llvm-native"));
#endif
  assert(!std::filesystem::exists(design / "llvm-native"));
  assert(!std::filesystem::exists(design / "phase.vcd"));
  std::cerr << "non-project cli: compiled CLI simulation validated\n";

  const std::vector<const char*> interpreter_simulate_arguments{
      "fsim", "simulate", "--design", design_text.c_str(), "--engine",
      "interpreter", "--duration", "2ns", "--seed", "11",
      "--delay-mode", "typ"};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(interpreter_simulate_arguments.size()),
      interpreter_simulate_arguments.data(), production_services,
      output, error) == 0);
  assert(error.str().empty());
  assert(output.str().find("simulation reached time limit at tick 2")
      != std::string::npos);

  const std::vector<const char*> debug_simulate_arguments{
      "fsim", "simulate", "--design", design_text.c_str(), "--engine",
      "debug", "--duration", "2ns"};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(debug_simulate_arguments.size()),
      debug_simulate_arguments.data(), production_services,
      output, error) == 0);
  assert(error.str().empty());
  assert(output.str().find("simulation reached time limit at tick 2")
      != std::string::npos);

  const std::vector<const char*> incompatible_delay_arguments{
      "fsim", "simulate", "--design", design_text.c_str(), "--delay-mode",
      "max"};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(incompatible_delay_arguments.size()),
      incompatible_delay_arguments.data(), production_services,
      output, error) != 0);
  assert(error.str().find("does not match the elaborated .fsimdesign")
      != std::string::npos);
  std::cerr << "non-project cli: interpreter and debug CLI validated\n";

  const auto provenance_cache = directory / "provenance-cache";
  std::filesystem::create_directories(provenance_cache);
  diagnostic::Engine cold_design_diagnostics;
  auto cold_design = app::load_design_artifact(
      design, cold_design_diagnostics);
  assert(cold_design && !cold_design_diagnostics.has_error());
  cold_design->cache_path = provenance_cache;
  app::Simulation cold_simulation{
      std::move(*cold_design), 1000, app::SimulationEngine::compiled};
  assert(provenance_signature(cold_simulation.verilog_scope_provenance())
      == object_provenance_signature);
  const auto cold_cache = cold_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
  assert(cold_simulation.compiled_process_count() != 0);
  assert(cold_cache.misses != 0);
  assert(cold_cache.stores != 0);
#else
  assert(cold_simulation.compiled_process_count() == 0);
  assert(cold_cache.hits == 0 && cold_cache.misses == 0);
  assert(cold_cache.stores == 0);
#endif

  diagnostic::Engine warm_design_diagnostics;
  auto warm_design = app::load_design_artifact(
      design, warm_design_diagnostics);
  assert(warm_design && !warm_design_diagnostics.has_error());
  warm_design->cache_path = provenance_cache;
  app::Simulation warm_simulation{
      std::move(*warm_design), 1000, app::SimulationEngine::compiled};
  assert(provenance_signature(warm_simulation.verilog_scope_provenance())
      == object_provenance_signature);
  const auto warm_cache = warm_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
  assert(warm_cache.hits != 0);
#else
  assert(warm_cache.hits == 0 && warm_cache.misses == 0);
  assert(warm_cache.stores == 0);
#endif

  diagnostic::Engine debug_design_diagnostics;
  auto debug_design = app::load_design_artifact(
      design, debug_design_diagnostics);
  assert(debug_design && !debug_design_diagnostics.has_error());
  debug_design->cache_path = provenance_cache;
  app::Simulation debug_simulation{
      std::move(*debug_design), 1000, app::SimulationEngine::debug};
  assert(provenance_signature(debug_simulation.verilog_scope_provenance())
      == object_provenance_signature);
  const auto debug_cache = debug_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
  assert(debug_cache.misses != 0);
  assert(debug_cache.stores != 0);
#else
  assert(debug_cache.hits == 0 && debug_cache.misses == 0);
  assert(debug_cache.stores == 0);
#endif

  const auto alternate_design = directory / "alternate.fsimdesign";
  copy_tree(design, alternate_design);
  make_tree_writable(alternate_design);
  auto alternate_metadata = *published_design_metadata;
  ++alternate_metadata.seed;
  alternate_metadata.design_digest =
      artifact::compute_design_digest(alternate_metadata);
  {
    std::ofstream alternate_output(
        alternate_design / artifact::kDesignMetadataFilename,
        std::ios::binary | std::ios::trunc);
    const auto alternate_bytes =
        artifact::serialize_design_metadata(alternate_metadata);
    alternate_output.write(
        alternate_bytes.data(),
        static_cast<std::streamsize>(alternate_bytes.size()));
    assert(alternate_output);
  }
  diagnostic::Engine alternate_design_diagnostics;
  auto alternate_loaded = app::load_design_artifact(
      alternate_design, alternate_design_diagnostics);
  assert(alternate_loaded && !alternate_design_diagnostics.has_error());
  assert(alternate_loaded->artifact_identity
      != published_design_metadata->design_digest);
  alternate_loaded->cache_path = provenance_cache;
  app::Simulation alternate_simulation{
      std::move(*alternate_loaded), 1000, app::SimulationEngine::compiled};
  const auto alternate_cache = alternate_simulation.native_cache_statistics();
#if defined(FSIM_HAS_LLVM)
  assert(alternate_cache.misses != 0);
  assert(alternate_cache.stores != 0);
#else
  assert(alternate_cache.hits == 0 && alternate_cache.misses == 0);
  assert(alternate_cache.stores == 0);
#endif

  const auto corrupt_design = directory / "corrupt.fsimdesign";
  copy_tree(design, corrupt_design);
  make_tree_writable(corrupt_design);
  {
    std::ofstream corrupt_runtime(
        corrupt_design / "state/runtime.bin",
        std::ios::binary | std::ios::app);
    corrupt_runtime.put('x');
  }
  diagnostic::Engine corrupt_design_diagnostics;
  assert(!app::load_design_artifact(
      corrupt_design, corrupt_design_diagnostics));
  assert(corrupt_design_diagnostics.has_error());

  const auto partial_design = directory / "partial.fsimdesign";
  copy_tree(design, partial_design);
  make_tree_writable(partial_design);
  assert(std::filesystem::remove(partial_design / "state/semantics.bin"));
  diagnostic::Engine partial_design_diagnostics;
  assert(!app::load_design_artifact(
      partial_design, partial_design_diagnostics));
  assert(partial_design_diagnostics.has_error());

  const auto incompatible_design = directory / "incompatible.fsimdesign";
  copy_tree(design, incompatible_design);
  make_tree_writable(incompatible_design);
  auto incompatible_metadata = *published_design_metadata;
  ++incompatible_metadata.runtime_abi;
  incompatible_metadata.design_digest =
      artifact::compute_design_digest(incompatible_metadata);
  {
    std::ofstream incompatible_output(
        incompatible_design / artifact::kDesignMetadataFilename,
        std::ios::binary | std::ios::trunc);
    const auto incompatible_bytes =
        artifact::serialize_design_metadata(incompatible_metadata);
    incompatible_output.write(
        incompatible_bytes.data(),
        static_cast<std::streamsize>(incompatible_bytes.size()));
    assert(incompatible_output);
  }
  diagnostic::Engine incompatible_design_diagnostics;
  assert(!app::load_design_artifact(
      incompatible_design, incompatible_design_diagnostics));
  assert(incompatible_design_diagnostics.has_error());

  std::filesystem::rename(hidden_object, object);
  std::filesystem::rename(hidden_extra_object, extra_object);
  std::filesystem::rename(hidden_source, source);
  std::filesystem::rename(hidden_extra_source, extra_source);

  const auto corrupt_object = directory / "corrupt.fsimobj";
  std::filesystem::create_directory(corrupt_object);
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(object)) {
    const auto relative = std::filesystem::relative(entry.path(), object);
    if (entry.is_directory()) {
      std::filesystem::create_directories(corrupt_object / relative);
    } else {
      std::filesystem::copy_file(entry.path(), corrupt_object / relative);
    }
  }
  const auto corrupt_unit = corrupt_object / indexed_unit.artifact;
  std::filesystem::permissions(
      corrupt_unit, std::filesystem::perms::owner_write,
      std::filesystem::perm_options::add);
  {
    std::ofstream corrupt_output(
        corrupt_unit, std::ios::binary | std::ios::app);
    corrupt_output.put('x');
  }
  diagnostic::Engine corrupt_diagnostics;
  const std::vector corrupt_inputs{corrupt_object};
  assert(!app::load_objects(corrupt_inputs, corrupt_diagnostics));
  assert(corrupt_diagnostics.has_error());

  diagnostic::Engine duplicate_diagnostics;
  const std::vector duplicate_inputs{object, object};
  assert(!app::load_objects(duplicate_inputs, duplicate_diagnostics));
  assert(duplicate_diagnostics.has_error());

  const auto vhdl_source = directory / "ordered.vhd";
  const auto vhdl_object = directory / "ordered.fsimobj";
  const auto vhdl_source_text = support::path_to_utf8(vhdl_source);
  const auto vhdl_object_text = support::path_to_utf8(vhdl_object);
  {
    std::ofstream vhdl_output(vhdl_source);
    vhdl_output << R"(
package SharedPkg is
  constant FLAG : boolean := true;
end package;
use work.SharedPkg.all;
entity MixedCase is end entity;
architecture rtl of MixedCase is
begin
  process begin
    assert FLAG;
    wait;
  end process;
end architecture;
)";
  }
  const std::vector<const char*> vhdl_compile_arguments{
      "fsim", "compile", "--lang", "vhdl", "--standard", "2008",
      "--library", "work", "--compilation-unit", "file", "--output",
      vhdl_object_text.c_str(), vhdl_source_text.c_str()};
  output.str({});
  error.str({});
  const auto vhdl_compile_result = cli::run(
      static_cast<int>(vhdl_compile_arguments.size()),
      vhdl_compile_arguments.data(), production_services,
      output, error);
  if (vhdl_compile_result != 0) {
    std::cerr << error.str();
  }
  assert(vhdl_compile_result == 0);
  assert(error.str().empty());
  diagnostic::Engine vhdl_metadata_diagnostics;
  const auto vhdl_metadata = artifact::load_object_metadata(
      vhdl_object, vhdl_metadata_diagnostics);
  assert(vhdl_metadata && !vhdl_metadata_diagnostics.has_error());
  assert(vhdl_metadata->units.size() == 3);
  assert(vhdl_metadata->units[0].name == "sharedpkg");
  assert(vhdl_metadata->units[1].name == "mixedcase");
  assert(vhdl_metadata->units[2].primary_name == "mixedcase");

  auto package_metadata = *vhdl_metadata;
  package_metadata.units = {vhdl_metadata->units[0]};
  package_metadata.compilation_digest =
      artifact::compute_object_compilation_digest(package_metadata);
  auto design_metadata = *vhdl_metadata;
  design_metadata.units = {
      vhdl_metadata->units[1], vhdl_metadata->units[2]};
  design_metadata.compilation_digest =
      artifact::compute_object_compilation_digest(design_metadata);
  const auto package_object = directory / "package.fsimobj";
  const auto design_object = directory / "design-units.fsimobj";
  diagnostic::Engine package_publish_diagnostics;
  assert(artifact::publish_object(
      package_object, package_metadata,
      object_payloads(vhdl_object, package_metadata),
      package_publish_diagnostics));
  assert(!package_publish_diagnostics.has_error());
  diagnostic::Engine design_publish_diagnostics;
  assert(artifact::publish_object(
      design_object, design_metadata,
      object_payloads(vhdl_object, design_metadata),
      design_publish_diagnostics));
  assert(!design_publish_diagnostics.has_error());

  diagnostic::Engine ordered_vhdl_diagnostics;
  const std::vector ordered_vhdl_inputs{package_object, design_object};
  const auto ordered_vhdl = app::load_objects(
      ordered_vhdl_inputs, ordered_vhdl_diagnostics);
  assert(ordered_vhdl && !ordered_vhdl_diagnostics.has_error());
  assert(ordered_vhdl->objects.size() == 2U);
  assert(std::ranges::all_of(
      ordered_vhdl->objects, [](const auto& provenance) {
        return provenance.language == "vhdl"
            && provenance.standard == "2008"
            && provenance.compatibility_profile
                == "fsim-synopsys-ieee-compat-v2";
      }));
  diagnostic::Engine reversed_vhdl_diagnostics;
  const std::vector reversed_vhdl_inputs{design_object, package_object};
  assert(!app::load_objects(
      reversed_vhdl_inputs, reversed_vhdl_diagnostics));
  assert(reversed_vhdl_diagnostics.has_error());

  auto wrong_library_metadata = package_metadata;
  wrong_library_metadata.library = "other";
  wrong_library_metadata.compilation_digest =
      artifact::compute_object_compilation_digest(wrong_library_metadata);
  const auto wrong_library_object = directory / "wrong-library.fsimobj";
  diagnostic::Engine wrong_publish_diagnostics;
  assert(artifact::publish_object(
      wrong_library_object, wrong_library_metadata,
      object_payloads(vhdl_object, package_metadata),
      wrong_publish_diagnostics));
  diagnostic::Engine wrong_load_diagnostics;
  const std::vector wrong_library_inputs{wrong_library_object};
  assert(!app::load_objects(wrong_library_inputs, wrong_load_diagnostics));
  assert(wrong_load_diagnostics.has_error());

  assert(std::filesystem::remove(vhdl_source));
  std::filesystem::rename(
      vhdl_object, directory / "ordered.fsimobj.producer-hidden");
  assert(!std::filesystem::exists(vhdl_object));
  diagnostic::Engine standalone_vhdl_diagnostics;
  const auto standalone_vhdl = app::load_objects(
      ordered_vhdl_inputs, standalone_vhdl_diagnostics);
  assert(standalone_vhdl && !standalone_vhdl_diagnostics.has_error());
  assert(standalone_vhdl->objects.size() == ordered_vhdl->objects.size());
  for (std::size_t index = 0; index < ordered_vhdl->objects.size(); ++index) {
    const auto& expected = ordered_vhdl->objects[index];
    const auto& actual = standalone_vhdl->objects[index];
    assert(actual.metadata_digest == expected.metadata_digest);
    assert(actual.compilation_digest == expected.compilation_digest);
    assert(actual.standard == expected.standard);
    assert(actual.compatibility_profile == expected.compatibility_profile);
    assert(actual.vhdl_package_dependencies
        == expected.vhdl_package_dependencies);
  }

  const auto isolated_source = directory / "isolated.sv";
  const auto isolated_object = directory / "isolated.fsimobj";
  const auto isolated_source_text = support::path_to_utf8(isolated_source);
  const auto isolated_object_text = support::path_to_utf8(isolated_object);
  {
    std::ofstream isolated_output(isolated_source);
    isolated_output << R"(`ifdef LEAK
module leaked; endmodule
`else
module isolated; endmodule
`endif
)";
  }
  const std::vector<const char*> isolated_arguments{
      "fsim", "compile", "--lang", "systemverilog", "--standard",
      "2017", "--library", "work", "--output",
      isolated_object_text.c_str(), isolated_source_text.c_str()};
  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(isolated_arguments.size()), isolated_arguments.data(),
      production_services, output, error) == 0);
  diagnostic::Engine isolated_diagnostics;
  const auto isolated_metadata = artifact::load_object_metadata(
      isolated_object, isolated_diagnostics);
  assert(isolated_metadata && isolated_metadata->units.size() == 1);
  assert(isolated_metadata->units.front().name == "isolated");

  output.str({});
  error.str({});
  assert(cli::run(
      static_cast<int>(compile_arguments.size()), compile_arguments.data(),
      production_services, output, error) == 1);
  assert(error.str().find("already exists") != std::string::npos);
}

}  // namespace fsim::test
