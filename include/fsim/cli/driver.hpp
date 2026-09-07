// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
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
};

enum class DiagnosticFormat {
  text,
  json,
};

enum class AotScope {
  selected,
  all,
};

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
  std::filesystem::path manifest{"fsim.toml"};
  bool manifest_explicit{false};
  std::vector<std::filesystem::path> files;
  std::optional<project::Language> language;
  std::optional<std::string> standard;
  std::vector<std::string> compatibility_switches;
  std::optional<std::string> compilation_unit;
  std::optional<project::SystemVerilogUvmRelease> uvm_release;
  // Normalized ordered root selections. `top` remains the one-root
  // source-compatible view.
  std::vector<project::ProjectSection::TopLevel> tops;
  std::optional<std::string> top;
  std::string library{"work"};
  std::vector<std::string> search_libraries;
  std::vector<project::LibraryMapping> library_mappings;
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
  std::optional<std::uint64_t> seed;
  bool random_seed{false};
  std::optional<std::uint32_t> jobs;
  std::optional<project::Optimization> optimization;
  std::optional<bool> code_coverage;
  bool coverage_partial_merge{false};
  std::optional<std::string> coverage_report_format;
  std::vector<CoverageThreshold> coverage_thresholds;
  std::optional<std::filesystem::path> tcl_script;
  std::vector<std::string> tcl_arguments;
  std::vector<std::string> tcl_commands;
  DiagnosticFormat diagnostic_format{DiagnosticFormat::text};
  bool help{false};
  bool version{false};
};

using Handler = std::function<int(
    const Invocation& invocation,
    const project::Config& config,
    diagnostic::Engine& diagnostics,
    std::ostream& output,
    std::ostream& error)>;

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

}  // namespace fsim::cli
