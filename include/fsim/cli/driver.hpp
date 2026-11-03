// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/configuration.hpp"
#include "fsim/project/project.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fsim::cli {

enum class Command {
  check,
  build,
  run,
  debug,
  tcl,
  compile,
  elaborate,
  simulate,
  systemc_compile,
  systemc_link,
  coverage_merge,
  coverage_report,
  library_map,
  library_list,
  library_unmap,
  library_objects,
  library_delete_object,
  library_delete,
  snapshot_info,
};

enum class DiagnosticFormat {
  text,
  json,
};

enum class Verbosity {
  quiet,
  normal,
  verbose,
};

/// Accepted for compatibility and ignored: elaborate --aot always populates
/// the v4 static-kernel cache.
enum class AotScope {
  selected,
  all,
  /// Engine v4 only: the static kernel's code, no per-process JIT objects.
  kernel,
};

/// Accepted for compatibility and ignored: the per-process JIT was removed.
enum class CompiledProcessPolicy {
  automatic,
  selected,
  all,
};

struct CoverageThreshold {
  std::string metric;
  std::uint32_t percent{};

  friend bool operator==(
      const CoverageThreshold&, const CoverageThreshold&) = default;
};

struct Invocation {
  std::string program_name{"fsim"};
  std::filesystem::path program_path{"fsim"};
  Command command{Command::check};
  std::string snapshot{"default"};
  Verbosity verbosity{Verbosity::normal};
  std::optional<std::string> library_name;
  std::optional<std::filesystem::path> library_mapping_path;
  std::optional<std::string> library_object_id;
  std::vector<std::filesystem::path> files;
  std::optional<project::Language> language;
  std::optional<std::string> standard;
  std::vector<std::string> compatibility_switches;
  std::optional<std::string> vhdl_compatibility;
  std::optional<std::string> compilation_unit;
  std::optional<project::SystemVerilogUvmRelease> uvm_release;
  // Normalized ordered root selections. `top` remains the one-root
  // source-compatible view.
  std::vector<project::ProjectSection::TopLevel> tops;
  std::vector<std::pair<std::string, std::string>> generics;
  std::optional<std::string> top;
  std::string library{"work"};
  bool library_explicit{false};
  std::vector<std::string> search_libraries;
  std::vector<project::LibraryMapping> library_exports;
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
  std::optional<std::filesystem::path> artifact_output;
  std::vector<std::filesystem::path> objects;
  std::vector<std::filesystem::path> systemc_plugins;
  std::optional<std::string> systemc_compiler;
  std::vector<std::string> systemc_compile_options;
  std::vector<std::string> systemc_link_options;
  std::vector<std::string> systemc_libraries;
  std::optional<std::filesystem::path> design;
  std::optional<std::filesystem::path> cache_directory;
  std::optional<std::filesystem::path> file_root;
  std::optional<std::string> engine;
  std::optional<bool> aot;
  std::optional<AotScope> aot_scope;
  std::optional<CompiledProcessPolicy> compiled_processes;
  std::vector<std::string> plusargs;
  // VPI modules (`LIB[:BOOTSTRAP]`) and DPI-C libraries loaded for the
  // design (IEEE 1800-2017 36.9.1, 35.8).
  std::vector<std::string> vpi_modules;
  std::vector<std::string> sv_libs;
  // VHPI modules (`LIB[:BOOTSTRAP]`, IEEE 1076-2019 20.2).
  std::vector<std::string> vhpi_modules;
  std::vector<std::string> trace_filters;
  std::optional<std::string> duration;
  std::optional<std::uint64_t> max_deltas;
  std::optional<project::DelayMode> delay_mode;
  std::vector<std::filesystem::path> sdf_files;
  std::optional<std::string> sdf_root;
  std::string sdf_cell{"*"};
  std::optional<std::size_t> sdf_report_limit;
  std::optional<std::filesystem::path> trace_file;
  std::optional<project::TraceFormat> trace_format;
  std::optional<project::TraceCompression> trace_compression;
  std::optional<std::size_t> trace_report_limit;
  std::optional<bool> trace_enabled;
  // `--foreign-probes`: statement, call and suspend probes for VPI/VHPI.
  std::optional<bool> foreign_probes;
  std::optional<std::filesystem::path> coverage_output;
  std::optional<std::uint64_t> seed;
  bool random_seed{false};
  std::optional<std::uint32_t> jobs;
  std::optional<project::Optimization> optimization;
  std::optional<bool> code_coverage;
  bool coverage_partial_merge{false};
  // `compile --preprocess-only`: run the Verilog preprocessor and stop.
  bool preprocess_only{false};
  // `compile --dpi-header PATH`: write the C prototypes of the DPI-C
  // imports and exports (IEEE 1800-2023 35, Annex H).
  std::optional<std::filesystem::path> dpi_header;
  std::optional<std::string> coverage_report_format;
  std::vector<CoverageThreshold> coverage_thresholds;
  std::optional<std::filesystem::path> tcl_script;
  std::vector<std::string> tcl_arguments;
  std::vector<std::string> tcl_commands;
  DiagnosticFormat diagnostic_format{DiagnosticFormat::text};
  diagnostic::ColorMode color_mode{diagnostic::ColorMode::automatic};
  bool help{false};
  bool version{false};
  // `--no-config` (or FSIM_NO_CONFIG=1): run no configuration script.
  bool no_config{false};
  // The configuration the scripts built; the commands of a Tcl session
  // read and change it. Null when no configuration was loaded.
  std::shared_ptr<project::Configuration> configuration;
};

using Handler = std::function<int(
    const Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error)>;

// Runs the per-user and workspace configuration scripts of a command into
// the configuration; false after a diagnosed failure.
using ConfigurationLoader = std::function<bool(
    const Invocation& invocation,
    project::Configuration& configuration,
    diagnostic::Engine& diagnostics)>;

struct Services {
  Handler check;
  Handler build;
  Handler run;
  Handler debug;
  Handler tcl;
  Handler compile;
  Handler elaborate;
  Handler simulate;
  Handler systemc_compile;
  Handler systemc_link;
  Handler coverage_merge;
  Handler coverage_report;
  Handler library_map;
  Handler library_list;
  Handler library_unmap;
  Handler library_objects;
  Handler library_delete_object;
  Handler library_delete;
  // Used only by the executable's no-stream CLI entry point. Stream-injected
  // callers always use run/simulate, preserving their callback semantics.
  Handler stdio_run { };
  Handler stdio_simulate { };
  // Absent for callers that read no configuration scripts.
  ConfigurationLoader load_configuration { };
  Handler snapshot_info { };
};

[[nodiscard]] std::optional<Invocation> parse_arguments(
    int argc,
    const char* const* argv,
    diagnostic::Engine& diagnostics);

int run(
    int argc,
    const char* const* argv,
    const Services& services,
    std::ostream& output,
    std::ostream& error);

int run(int argc, const char* const* argv, const Services& services = {});

// Puts the invocation's run, elaboration and SystemC options into the
// configuration, as the driver does before it calls a handler.
void apply_invocation_options(
    const Invocation& invocation, project::Config& config);

// Whether a command reads configuration scripts: not when the invocation
// says --no-config or the environment sets FSIM_NO_CONFIG=1.
[[nodiscard]] bool configuration_enabled(const Invocation& invocation);

// Fills the options the invocation does not give from the configuration's
// keys for its command (`compile.*`, `elaborate.*`, `simulate.*`,
// `systemc.*`). Explicit single-value options keep their values; list
// options (defines, include directories, search libraries, compiler and
// linker options, trace filters, generics) add theirs after the configured
// ones, a generic of the same name replacing its configured value.
void apply_configuration_defaults(
    const project::Configuration& configuration, Invocation& invocation);

// The source set a compile of `language` uses: the invocation's options,
// and for each option it leaves out the configuration's
// `compile.<language>.option` or `compile.option` value, else the default.
// Empty after a diagnosed unsupported standard. Files are left to the caller.
[[nodiscard]] std::optional<project::SourceSet> make_source_set(
    const Invocation& invocation, project::Language language,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::cli
