// SPDX-License-Identifier: Apache-2.0
#include "fsim/cli/driver.hpp"

#include "fsim/api.h"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace fsim::cli {
namespace {

constexpr int kSuccess = 0;
constexpr int kUserError = 1;
constexpr int kUsageError = 2;
constexpr int kUnavailable = 3;

std::string basename(std::string_view path) {
  const auto separator = path.find_last_of("/\\");
  return std::string(separator == std::string_view::npos ? path : path.substr(separator + 1));
}

std::string lowercase(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

std::string_view command_name(const Command command) {
  switch (command) {
    case Command::check:
      return "check";
    case Command::build:
      return "build";
    case Command::run:
      return "run";
    case Command::debug:
      return "debug";
    case Command::tcl:
      return "tcl";
    case Command::migrate:
      return "migrate";
  }
  return "check";
}

std::optional<Command> parse_command(const std::string_view spelling) {
  if (spelling == "check") {
    return Command::check;
  }
  if (spelling == "build") {
    return Command::build;
  }
  if (spelling == "run") {
    return Command::run;
  }
  if (spelling == "debug") {
    return Command::debug;
  }
  if (spelling == "tcl") {
    return Command::tcl;
  }
  if (spelling == "migrate") {
    return Command::migrate;
  }
  return std::nullopt;
}

bool parse_unsigned(const std::string_view spelling, std::uint64_t& result) {
  if (spelling.empty() || spelling.front() == '-') {
    return false;
  }
  const auto [end, error] =
      std::from_chars(spelling.data(), spelling.data() + spelling.size(), result);
  return error == std::errc{} && end == spelling.data() + spelling.size();
}

void argument_error(
    diagnostic::Engine& diagnostics,
    const std::string& message) {
  diagnostics.error("FSIM-CLI-0001", message, {"<command-line>", {1, 1, 0}, {1, 1, 0}});
}

std::optional<std::string_view> option_value(
    const std::string_view argument,
    const std::string_view name) {
  if (argument.size() > name.size() && argument.starts_with(name) &&
      argument[name.size()] == '=') {
    return argument.substr(name.size() + 1);
  }
  return std::nullopt;
}

std::optional<std::string_view> take_value(
    int& index,
    const int argc,
    const char* const* argv,
    const std::string_view argument,
    const std::string_view long_name,
    diagnostic::Engine& diagnostics) {
  if (const auto inline_value = option_value(argument, long_name)) {
    if (inline_value->empty()) {
      argument_error(
          diagnostics, "option '" + std::string(long_name) + "' requires a value");
      return std::nullopt;
    }
    return inline_value;
  }
  if (index + 1 >= argc) {
    argument_error(
        diagnostics, "option '" + std::string(long_name) + "' requires a value");
    return std::nullopt;
  }
  ++index;
  return std::string_view(argv[index]);
}

bool is_option(
    const std::string_view argument,
    const std::string_view short_name,
    const std::string_view long_name) {
  return argument == short_name || argument == long_name ||
         option_value(argument, long_name).has_value();
}

std::optional<project::Optimization> parse_optimization(
    const std::string_view spelling) {
  const auto normalized = lowercase(spelling);
  if (normalized == "o0" || normalized == "0") {
    return project::Optimization::o0;
  }
  if (normalized == "o1" || normalized == "1") {
    return project::Optimization::o1;
  }
  if (normalized == "o2" || normalized == "2") {
    return project::Optimization::o2;
  }
  if (normalized == "o3" || normalized == "3") {
    return project::Optimization::o3;
  }
  return std::nullopt;
}

std::optional<project::Language> infer_language(
    const std::filesystem::path& path) {
  const auto extension = lowercase(path.extension().string());
  if (extension == ".vhd" || extension == ".vhdl") {
    return project::Language::vhdl;
  }
  if (extension == ".v") {
    return project::Language::verilog;
  }
  if (extension == ".sv" || extension == ".svh") {
    return project::Language::system_verilog;
  }
  if (extension == ".cpp" || extension == ".cc" || extension == ".cxx") {
    return project::Language::systemc;
  }
  return std::nullopt;
}

std::string default_standard(const project::Language language) {
  switch (language) {
    case project::Language::vhdl:
      return "2008";
    case project::Language::verilog:
      return "2005";
    case project::Language::system_verilog:
      return "2017";
    case project::Language::systemc:
      return "2023-subset";
  }
  return {};
}

bool supported_standard(
    const project::Language language,
    const std::string_view standard) noexcept {
  switch (language) {
    case project::Language::vhdl:
      return standard == "2008" || standard == "08";
    case project::Language::verilog:
      return standard == "2005" || standard == "2001";
    case project::Language::system_verilog:
      return standard == "2017" || standard == "2012";
    case project::Language::systemc:
      return standard == "2023-subset" || standard == "2023";
  }
  return false;
}

std::filesystem::path absolute_normalized(const std::filesystem::path& path) {
  std::error_code error;
  auto result = std::filesystem::absolute(path, error);
  return (error ? path : result).lexically_normal();
}

std::optional<project::Config> make_direct_config(
    const Invocation& invocation,
    diagnostic::Engine& diagnostics) {
  project::Config config;
  config.manifest_path = "<command-line>";
  std::error_code error;
  config.base_directory = std::filesystem::current_path(error);
  if (error) {
    config.base_directory = ".";
  }
  config.project.name = "command-line";
  if (invocation.top.has_value()) {
    config.project.top = *invocation.top;
  }

  for (const auto& input : invocation.files) {
    const auto language =
        invocation.language.has_value() ? invocation.language : infer_language(input);
    if (!language.has_value()) {
      argument_error(
          diagnostics,
          "cannot infer the language for '"
              + fsim::support::path_to_utf8(input) +
              "'; pass --lang");
      continue;
    }
    const auto path = absolute_normalized(input);
    std::error_code file_error;
    if (!std::filesystem::is_regular_file(path, file_error)) {
      argument_error(
          diagnostics,
          "source file does not exist: "
              + fsim::support::path_to_utf8(path));
      continue;
    }

    auto iterator = std::find_if(
        config.source_sets.begin(),
        config.source_sets.end(),
        [&](const project::SourceSet& set) { return set.language == *language; });
    if (iterator == config.source_sets.end()) {
      project::SourceSet source_set;
      source_set.language = *language;
      source_set.standard =
          invocation.standard.value_or(default_standard(*language));
      if (!supported_standard(*language, source_set.standard)) {
        argument_error(
            diagnostics,
            "unsupported standard '" + source_set.standard + "' for "
                + std::string(project::to_string(*language)));
        continue;
      }
      source_set.library = invocation.library;
      for (const auto& directory : invocation.include_directories) {
        source_set.include_directories.push_back(absolute_normalized(directory));
      }
      source_set.defines = invocation.defines;
      config.source_sets.push_back(std::move(source_set));
      iterator = std::prev(config.source_sets.end());
    }
    iterator->file_patterns.push_back(path);
    iterator->files.push_back(path);
  }

  if (invocation.duration.has_value()) {
    config.run.duration = invocation.duration;
  }
  if (invocation.max_deltas.has_value()) {
    config.run.max_deltas = *invocation.max_deltas;
  }
  if (invocation.delay_mode.has_value()) {
    config.run.delay_mode = *invocation.delay_mode;
  }
  if (invocation.trace_file.has_value()) {
    config.run.trace_file = absolute_normalized(*invocation.trace_file);
  }
  if (invocation.seed.has_value()) {
    config.project.seed = *invocation.seed;
  }
  config.project.random_seed = invocation.random_seed;
  if (invocation.jobs.has_value()) {
    config.build.jobs = *invocation.jobs;
  }
  if (invocation.optimization.has_value()) {
    config.build.optimization = *invocation.optimization;
  }
  if (!invocation.search_libraries.empty()) {
    config.elaboration.search_libraries = invocation.search_libraries;
  }

  if (diagnostics.has_error()) {
    return std::nullopt;
  }
  return config;
}

void apply_overrides(const Invocation& invocation, project::Config& config) {
  if (invocation.top.has_value()) {
    config.project.top = *invocation.top;
  }
  if (invocation.duration.has_value()) {
    config.run.duration = invocation.duration;
  }
  if (invocation.max_deltas.has_value()) {
    config.run.max_deltas = *invocation.max_deltas;
  }
  if (invocation.delay_mode.has_value()) {
    config.run.delay_mode = *invocation.delay_mode;
  }
  if (invocation.trace_file.has_value()) {
    config.run.trace_file = absolute_normalized(*invocation.trace_file);
  }
  if (invocation.seed.has_value()) {
    config.project.seed = *invocation.seed;
    config.project.random_seed = false;
  } else if (invocation.random_seed) {
    config.project.random_seed = true;
  }
  if (invocation.jobs.has_value()) {
    config.build.jobs = *invocation.jobs;
  }
  if (invocation.optimization.has_value()) {
    config.build.optimization = *invocation.optimization;
  }
  if (!invocation.search_libraries.empty()) {
    config.elaboration.search_libraries = invocation.search_libraries;
  }
}

void print_help(std::ostream& output, const std::string_view program) {
  output
      << "Usage: " << program
      << " <check|build|run|debug|tcl|migrate> [options] [files...]\n"
      << "\n"
      << "Commands:\n"
      << "  check   Parse and analyze sources\n"
      << "  build   Elaborate and populate the native-code cache\n"
      << "  run     Build incrementally and simulate in optimized mode\n"
      << "  debug   Build incrementally and enter the interactive debugger\n"
      << "  tcl     Enter Tcl or evaluate a Tcl script/command batch\n"
      << "  migrate Upgrade a project manifest schema\n"
      << "\n"
      << "Project and source options:\n"
      << "  -p, --project PATH       Project manifest (default: fsim.toml)\n"
      << "      --top NAME           Override the design top\n"
      << "      --lang LANGUAGE      Language for every direct source file\n"
      << "      --standard VERSION   Standard for direct source files\n"
      << "      --library NAME       Library for direct source files (default: work)\n"
      << "      --search-library NAME\n"
      << "                           Replace the manifest elaboration search list; repeatable\n"
      << "  -I, --include PATH       Add a direct-source include directory\n"
      << "  -D, --define NAME[=VAL]  Add a direct-source preprocessor definition\n"
      << "\n"
      << "Build and run options:\n"
      << "  -O, --optimization O0..O3\n"
      << "  -j, --jobs COUNT\n"
      << "      --duration TIME\n"
      << "      --max-deltas COUNT\n"
      << "      --delay-mode min|typ|max\n"
      << "      --trace PATH\n"
      << "      --seed COUNT|random\n"
      << "      --diagnostics text|json\n"
      << "      --to 2               Migration target schema\n"
      << "      --in-place           Replace the migrated manifest\n"
      << "\n"
      << "Tcl options:\n"
      << "  -c, --command SCRIPT    Evaluate Tcl text (repeatable)\n"
      << "  SCRIPT [ARG...]         Evaluate a Tcl file with argv/argc set\n"
      << "                          Omit both forms for interactive Tcl\n"
      << "  -h, --help\n"
      << "      --version\n";
}

void print_diagnostics(
    std::ostream& error,
    const diagnostic::Engine& diagnostics,
    const DiagnosticFormat format) {
  if (format == DiagnosticFormat::json) {
    diagnostic::print_json(error, diagnostics);
  } else {
    diagnostic::print_text(error, diagnostics);
  }
}

const Handler* select_handler(const Services& services, const Command command) {
  switch (command) {
    case Command::check:
      return &services.check;
    case Command::build:
      return &services.build;
    case Command::run:
      return &services.run;
    case Command::debug:
      return &services.debug;
    case Command::tcl:
      return &services.tcl;
    case Command::migrate:
      return nullptr;
  }
  return nullptr;
}

DiagnosticFormat requested_diagnostic_format(
    const int argc,
    const char* const* argv) noexcept {
  DiagnosticFormat result = DiagnosticFormat::text;
  if (argc <= 1 || argv == nullptr) {
    return result;
  }
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == nullptr) {
      continue;
    }
    const std::string_view argument{argv[index]};
    std::string_view value;
    if (argument.starts_with("--diagnostics=")) {
      value = argument.substr(std::string_view{"--diagnostics="}.size());
    } else if (
        argument == "--diagnostics" && index + 1 < argc
        && argv[index + 1] != nullptr) {
      value = argv[++index];
    } else {
      continue;
    }
    if (value == "json") {
      result = DiagnosticFormat::json;
    } else if (value == "text") {
      result = DiagnosticFormat::text;
    }
  }
  return result;
}

}  // namespace

std::optional<Invocation> parse_arguments(
    const int argc,
    const char* const* argv,
    diagnostic::Engine& diagnostics) {
  if (argc <= 0 || argv == nullptr || argv[0] == nullptr) {
    argument_error(diagnostics, "missing program name");
    return std::nullopt;
  }

  Invocation invocation;
  invocation.program_path = fsim::support::path_from_utf8(argv[0]);
  invocation.program_name = basename(argv[0]);
  bool command_selected = false;
  const auto executable = lowercase(invocation.program_name);
  if (executable == "fsim-vhdl" || executable == "fsim-vhdl.exe") {
    invocation.command = Command::check;
    invocation.language = project::Language::vhdl;
    command_selected = true;
  } else if (executable == "fsim-sv" || executable == "fsim-sv.exe") {
    invocation.command = Command::check;
    invocation.language = project::Language::system_verilog;
    command_selected = true;
  } else if (executable == "fsim-elab" || executable == "fsim-elab.exe") {
    invocation.command = Command::build;
    command_selected = true;
  } else if (executable == "fsim-run" || executable == "fsim-run.exe") {
    invocation.command = Command::run;
    command_selected = true;
  }

  bool positional_only = false;
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == nullptr) {
      argument_error(diagnostics, "null command-line argument");
      return std::nullopt;
    }
    const std::string_view argument(argv[index]);
    if (!positional_only && argument == "--") {
      positional_only = true;
      continue;
    }
    if (!positional_only && (argument == "-h" || argument == "--help")) {
      invocation.help = true;
      continue;
    }
    if (!positional_only && argument == "--version") {
      invocation.version = true;
      continue;
    }
    if (!positional_only && argument.starts_with('-')) {
      if (is_option(argument, "-p", "--project")) {
        const auto value =
            take_value(index, argc, argv, argument, "--project", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.manifest = fsim::support::path_from_utf8(*value);
        invocation.manifest_explicit = true;
      } else if (is_option(argument, "", "--top")) {
        const auto value = take_value(index, argc, argv, argument, "--top", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.top = std::string(*value);
      } else if (is_option(argument, "", "--lang")) {
        const auto value = take_value(index, argc, argv, argument, "--lang", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.language = project::parse_language(*value);
        if (!invocation.language.has_value()) {
          argument_error(diagnostics, "unknown language '" + std::string(*value) + "'");
          return std::nullopt;
        }
      } else if (is_option(argument, "", "--standard")) {
        const auto value =
            take_value(index, argc, argv, argument, "--standard", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.standard = std::string(*value);
      } else if (is_option(argument, "", "--library")) {
        const auto value =
            take_value(index, argc, argv, argument, "--library", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.library = std::string(*value);
      } else if (is_option(argument, "", "--search-library")) {
        const auto value = take_value(
            index, argc, argv, argument, "--search-library", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        if (value->empty()) {
          argument_error(
              diagnostics,
              "--search-library requires a non-empty library name");
          return std::nullopt;
        }
        invocation.search_libraries.emplace_back(*value);
      } else if (
          argument == "-I" || is_option(argument, "", "--include") ||
          (argument.size() > 2 && argument.starts_with("-I"))) {
        std::optional<std::string_view> value;
        if (argument.size() > 2 && argument.starts_with("-I")) {
          value = argument.substr(2);
        } else {
          value = take_value(index, argc, argv, argument, "--include", diagnostics);
        }
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.include_directories.emplace_back(
            fsim::support::path_from_utf8(*value));
      } else if (
          argument == "-D" || is_option(argument, "", "--define") ||
          (argument.size() > 2 && argument.starts_with("-D"))) {
        std::optional<std::string_view> value;
        if (argument.size() > 2 && argument.starts_with("-D")) {
          value = argument.substr(2);
        } else {
          value = take_value(index, argc, argv, argument, "--define", diagnostics);
        }
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.defines.emplace_back(*value);
      } else if (
          argument == "-O" || is_option(argument, "", "--optimization") ||
          (argument.size() == 3 && argument.starts_with("-O"))) {
        std::optional<std::string_view> value;
        if (argument.size() == 3 && argument.starts_with("-O")) {
          value = argument.substr(1);
        } else {
          value =
              take_value(index, argc, argv, argument, "--optimization", diagnostics);
        }
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.optimization = parse_optimization(*value);
        if (!invocation.optimization.has_value()) {
          argument_error(diagnostics, "optimization must be O0, O1, O2, or O3");
          return std::nullopt;
        }
      } else if (
          argument == "-j" || is_option(argument, "", "--jobs") ||
          (argument.size() > 2 && argument.starts_with("-j"))) {
        std::optional<std::string_view> value;
        if (argument.size() > 2 && argument.starts_with("-j")) {
          value = argument.substr(2);
        } else {
          value = take_value(index, argc, argv, argument, "--jobs", diagnostics);
        }
        std::uint64_t number = 0;
        if (!value.has_value() || !parse_unsigned(*value, number) ||
            number > std::numeric_limits<std::uint32_t>::max()) {
          argument_error(diagnostics, "jobs must be a non-negative 32-bit integer");
          return std::nullopt;
        }
        invocation.jobs = static_cast<std::uint32_t>(number);
      } else if (is_option(argument, "", "--duration")) {
        const auto value =
            take_value(index, argc, argv, argument, "--duration", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.duration = std::string(*value);
      } else if (is_option(argument, "", "--max-deltas")) {
        const auto value =
            take_value(index, argc, argv, argument, "--max-deltas", diagnostics);
        std::uint64_t number = 0;
        if (!value.has_value() || !parse_unsigned(*value, number) || number == 0) {
          argument_error(diagnostics, "max-deltas must be a positive integer");
          return std::nullopt;
        }
        invocation.max_deltas = number;
      } else if (is_option(argument, "", "--delay-mode")) {
        const auto value = take_value(
            index, argc, argv, argument, "--delay-mode", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.delay_mode = project::parse_delay_mode(*value);
        if (!invocation.delay_mode) {
          argument_error(
              diagnostics, "delay-mode must be min, typ, or max");
          return std::nullopt;
        }
      } else if (is_option(argument, "", "--trace")) {
        const auto value = take_value(index, argc, argv, argument, "--trace", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.trace_file = fsim::support::path_from_utf8(*value);
      } else if (is_option(argument, "", "--seed")) {
        const auto value = take_value(index, argc, argv, argument, "--seed", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        if (lowercase(*value) == "random") {
          invocation.random_seed = true;
          invocation.seed.reset();
        } else {
          std::uint64_t number = 0;
          if (!parse_unsigned(*value, number)) {
            argument_error(diagnostics, "seed must be an unsigned integer or 'random'");
            return std::nullopt;
          }
          invocation.seed = number;
          invocation.random_seed = false;
        }
      } else if (is_option(argument, "", "--diagnostics")) {
        const auto value =
            take_value(index, argc, argv, argument, "--diagnostics", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        if (*value == "text") {
          invocation.diagnostic_format = DiagnosticFormat::text;
        } else if (*value == "json") {
          invocation.diagnostic_format = DiagnosticFormat::json;
        } else {
          argument_error(diagnostics, "diagnostics must be 'text' or 'json'");
          return std::nullopt;
        }
      } else if (is_option(argument, "-c", "--command")) {
        const auto value =
            take_value(index, argc, argv, argument, "--command", diagnostics);
        if (!value.has_value()) {
          return std::nullopt;
        }
        invocation.tcl_commands.emplace_back(*value);
      } else if (is_option(argument, "", "--to")) {
        const auto value =
            take_value(index, argc, argv, argument, "--to", diagnostics);
        std::uint64_t schema = 0;
        if (!value.has_value() || !parse_unsigned(*value, schema)
            || schema > std::numeric_limits<std::uint32_t>::max()) {
          argument_error(diagnostics, "--to requires a schema version");
          return std::nullopt;
        }
        invocation.migration_schema = static_cast<std::uint32_t>(schema);
      } else if (argument == "--in-place") {
        invocation.migration_in_place = true;
      } else {
        argument_error(diagnostics, "unknown option '" + std::string(argument) + "'");
        return std::nullopt;
      }
      continue;
    }

    if (!command_selected) {
      const auto command = parse_command(argument);
      if (!command.has_value()) {
        argument_error(
            diagnostics,
            "unknown command '" + std::string(argument) +
                "'; expected check, build, run, debug, tcl, or migrate");
        return std::nullopt;
      }
      invocation.command = *command;
      command_selected = true;
    } else if (invocation.command == Command::tcl) {
      if (!invocation.tcl_script.has_value()) {
        invocation.tcl_script = fsim::support::path_from_utf8(argument);
      } else {
        invocation.tcl_arguments.emplace_back(argument);
      }
    } else {
      invocation.files.emplace_back(fsim::support::path_from_utf8(argument));
    }
  }

  if (!command_selected && !invocation.help && !invocation.version) {
    argument_error(
        diagnostics,
        "missing command; expected check, build, run, debug, or tcl");
    return std::nullopt;
  }
  if (invocation.command != Command::tcl
      && !invocation.tcl_commands.empty()) {
    argument_error(
        diagnostics, "--command is available only with the tcl command");
    return std::nullopt;
  }
  if (invocation.command == Command::tcl
      && invocation.tcl_script.has_value()
      && !invocation.tcl_commands.empty()) {
    argument_error(
        diagnostics,
        "a Tcl script file cannot be combined with --command");
    return std::nullopt;
  }
  if (invocation.manifest_explicit && !invocation.files.empty()) {
    argument_error(diagnostics, "--project cannot be combined with direct source files");
    return std::nullopt;
  }
  if (invocation.command == Command::migrate) {
    if (invocation.files.size() != 1) {
      argument_error(diagnostics, "migrate requires exactly one manifest path");
      return std::nullopt;
    }
    if (invocation.migration_schema != 2) {
      argument_error(diagnostics, "migrate currently requires '--to 2'");
      return std::nullopt;
    }
    if (!invocation.search_libraries.empty()) {
      argument_error(
          diagnostics,
          "--search-library is not available with migrate");
      return std::nullopt;
    }
  } else if (invocation.migration_schema.has_value()
             || invocation.migration_in_place) {
    argument_error(
        diagnostics, "--to and --in-place are available only with migrate");
    return std::nullopt;
  }
  return invocation;
}

int run(
    const int argc,
    const char* const* argv,
    const Services& services,
    std::ostream& output,
    std::ostream& error) {
  diagnostic::Engine diagnostics;
  const auto requested_format =
      requested_diagnostic_format(argc, argv);
  try {
    auto invocation = parse_arguments(argc, argv, diagnostics);
    if (!invocation.has_value()) {
      print_diagnostics(error, diagnostics, requested_format);
      return kUsageError;
    }
    if (invocation->version) {
      output << "fsim 0.1.0-dev (C API " << FSIM_API_VERSION << ")\n";
      return kSuccess;
    }
    if (invocation->help) {
      print_help(output, invocation->program_name);
      return kSuccess;
    }
    if (invocation->command == Command::migrate) {
      auto migrated = project::migrate_to_schema_2(
          invocation->files.front(), diagnostics);
      if (!migrated.has_value() || diagnostics.has_error()) {
        print_diagnostics(error, diagnostics, invocation->diagnostic_format);
        return kUserError;
      }
      if (!invocation->migration_in_place) {
        output << *migrated;
        return kSuccess;
      }
      std::ofstream destination(
          invocation->files.front(), std::ios::binary | std::ios::trunc);
      destination.write(
          migrated->data(), static_cast<std::streamsize>(migrated->size()));
      if (!destination) {
        diagnostics.error(
            "FSIM-PROJ-0010",
            "cannot write migrated project manifest: "
                + fsim::support::path_to_utf8(invocation->files.front()));
        print_diagnostics(error, diagnostics, invocation->diagnostic_format);
        return kUserError;
      }
      return kSuccess;
    }

    std::optional<project::Config> config;
    if (invocation->command == Command::tcl
        && !invocation->manifest_explicit) {
      project::Config tcl_config;
      tcl_config.manifest_path = "<tcl>";
      std::error_code current_error;
      tcl_config.base_directory =
          std::filesystem::current_path(current_error);
      if (current_error) {
        tcl_config.base_directory = ".";
      }
      tcl_config.project.name = "tcl";
      config = std::move(tcl_config);
      apply_overrides(*invocation, *config);
    } else if (invocation->files.empty()) {
      config = project::load(invocation->manifest, diagnostics);
      if (config.has_value()) {
        apply_overrides(*invocation, *config);
      }
    } else {
      config = make_direct_config(*invocation, diagnostics);
    }
    if (!config.has_value() || diagnostics.has_error()) {
      print_diagnostics(error, diagnostics, invocation->diagnostic_format);
      return kUserError;
    }

    const Handler* handler = select_handler(services, invocation->command);
    if (handler == nullptr || !*handler) {
      diagnostics.error(
          "FSIM-CLI-0002",
          "command '" + std::string(command_name(invocation->command)) +
              "' is not connected to a simulation engine in this build");
      print_diagnostics(error, diagnostics, invocation->diagnostic_format);
      return kUnavailable;
    }

    const int result =
        (*handler)(*invocation, *config, diagnostics, output, error);
    if (!diagnostics.empty()) {
      print_diagnostics(error, diagnostics, invocation->diagnostic_format);
    }
    return result;
  } catch (const std::exception& exception) {
    diagnostics.error(
        "FSIM-CLI-0003",
        "unhandled command failure: " + std::string{exception.what()});
  } catch (...) {
    diagnostics.error(
        "FSIM-CLI-0003", "unhandled unknown command failure");
  }
  print_diagnostics(error, diagnostics, requested_format);
  return kUserError;
}

int run(
    const int argc,
    const char* const* argv,
    const Services& services) {
  return run(argc, argv, services, std::cout, std::cerr);
}

}  // namespace fsim::cli
