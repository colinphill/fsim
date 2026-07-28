// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/diagnostic/diagnostic.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::project {

inline constexpr std::uint32_t kSchemaVersion = 1;

enum class Language : std::uint8_t {
  vhdl,
  verilog,
  system_verilog,
  systemc,
};

enum class Optimization : std::uint8_t {
  o0,
  o1,
  o2,
  o3,
};

enum class DelayMode : std::uint8_t {
  minimum,
  typical,
  maximum,
};

struct ProjectSection {
  std::string name;
  std::string top;
  std::string time_resolution{"auto"};
  std::uint64_t seed{1};
  bool random_seed{false};
};

struct SourceSet {
  Language language{Language::system_verilog};
  std::string standard;
  std::string library{"work"};
  std::vector<std::filesystem::path> file_patterns;
  std::vector<std::filesystem::path> files;
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
  std::string compilation_unit{"file"};
};

struct Binding {
  std::string instance;
  std::string target;
  std::optional<std::string> resolver;
};

struct BuildSection {
  Optimization optimization{Optimization::o2};
  std::uint32_t jobs{0};
  std::filesystem::path cache_path{".fsim-cache"};
};

struct RunSection {
  std::optional<std::string> duration;
  std::uint64_t max_deltas{100'000};
  DelayMode delay_mode{DelayMode::typical};
  std::optional<std::filesystem::path> trace_file;
  std::vector<std::string> trace_filters;
};

struct SystemCSection {
  std::string compiler;
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
  std::vector<std::string> compile_options;
  std::vector<std::string> link_options;
  std::vector<std::string> libraries;
};

struct Config {
  std::uint32_t schema{kSchemaVersion};
  std::filesystem::path manifest_path;
  std::filesystem::path base_directory;
  ProjectSection project;
  std::vector<SourceSet> source_sets;
  std::vector<Binding> bindings;
  BuildSection build;
  RunSection run;
  SystemCSection systemc;
};

[[nodiscard]] std::string_view to_string(Language language) noexcept;
[[nodiscard]] std::string_view to_string(Optimization optimization) noexcept;
[[nodiscard]] std::string_view to_string(DelayMode mode) noexcept;
[[nodiscard]] std::optional<Language> parse_language(std::string_view spelling) noexcept;
[[nodiscard]] std::optional<DelayMode> parse_delay_mode(
    std::string_view spelling) noexcept;

// Parses, validates, and resolves a schema-1 fsim.toml. Relative paths are
// resolved against the manifest directory. Source globs are expanded in listed
// pattern order, with the matches for each pattern sorted lexicographically.
[[nodiscard]] std::optional<Config> load(
    const std::filesystem::path& manifest,
    diagnostic::Engine& diagnostics);

// Parses an in-memory manifest. source_name is used in diagnostics and
// base_directory is used for resolving project paths and source globs.
[[nodiscard]] std::optional<Config> parse(
    std::string_view source,
    std::string source_name,
    const std::filesystem::path& base_directory,
    diagnostic::Engine& diagnostics);

}  // namespace fsim::project
