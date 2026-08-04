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

inline constexpr std::uint32_t kSchemaVersion = 2;

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
  struct TopLevel {
    std::string target;
    std::string alias;

    friend bool operator==(const TopLevel&, const TopLevel&) = default;
  };

  std::string name;
  // Legacy source-compatible spelling for one root. Parsed configurations
  // always expose the normalized ordered selection through tops.
  std::string top;
  std::vector<TopLevel> tops;
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
  std::optional<std::string> target;
  std::optional<std::string> resolver;
};

// Maps one logical library to a relocatable, read-only precompiled library
// directory. Paths parsed from a manifest are absolute and normalized against
// the manifest directory; programmatic callers may provide absolute paths.
struct LibraryMapping {
  std::string library;
  std::filesystem::path path;

  friend bool operator==(const LibraryMapping&, const LibraryMapping&) = default;
};

struct ElaborationSection {
  // Unqualified lookup uses the parent logical library followed by first
  // occurrences from this ordered list as one complete ambiguity scope.
  std::vector<std::string> search_libraries;
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
  std::vector<LibraryMapping> library_mappings;
  ElaborationSection elaboration;
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

// Parses, validates, and resolves a schema-2 fsim.toml. Relative paths are
// resolved against the manifest directory. Source globs are expanded in listed
// pattern order, with the matches for each pattern sorted lexicographically.
[[nodiscard]] std::optional<Config> load(
    const std::filesystem::path& manifest,
    diagnostic::Engine& diagnostics);

// Produces a schema-2 manifest without modifying the input file. Schema 1 is
// upgraded by changing only the top-level schema declaration; schema 2 is
// returned unchanged so migration scripts are idempotent.
[[nodiscard]] std::optional<std::string> migrate_to_schema_2(
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
