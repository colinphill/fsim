// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"
#include "fsim/project/project.hpp"

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
};

enum class DiagnosticFormat {
  text,
  json,
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
  std::optional<std::string> top;
  std::string library{"work"};
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
  std::optional<std::string> duration;
  std::optional<std::uint64_t> max_deltas;
  std::optional<std::filesystem::path> trace_file;
  std::optional<std::uint64_t> seed;
  bool random_seed{false};
  std::optional<std::uint32_t> jobs;
  std::optional<project::Optimization> optimization;
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
