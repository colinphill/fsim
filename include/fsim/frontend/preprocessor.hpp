// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "fsim/frontend/token.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace fsim::frontend {

inline constexpr std::string_view verilog_preprocessor_cache_version =
    "fsim-verilog-preprocessor-v3";

struct PreprocessorOptions {
  std::vector<std::filesystem::path> include_directories;
  std::vector<std::string> defines;
  std::size_t maximum_include_depth{64};
  std::size_t maximum_macro_expansion_depth{256};
};

// Exact source snapshots consumed by one preprocessing run. The root is first;
// recursively included files follow in first-use order and are listed once.
struct PreprocessedDependency {
  std::filesystem::path path;
  std::string contents;
};

struct PreprocessResult {
  LexResult lexed;
  std::vector<PreprocessedDependency> dependencies;

  [[nodiscard]] bool ok() const {
    return lexed.ok();
  }
};

struct PreprocessedRoot {
  std::filesystem::path path;
  std::string contents;
  // Includes consumed while preprocessing this root, in first-use order.
  std::vector<PreprocessedDependency> dependencies;
};

struct PreprocessCompilationUnitResult {
  LexResult lexed;
  // Ordered manifest roots and their exact include closures.
  std::vector<PreprocessedRoot> roots;
  // Every distinct root/include snapshot in compilation-unit first-use order.
  std::vector<PreprocessedDependency> inputs;

  [[nodiscard]] bool ok() const {
    return lexed.ok();
  }
};

// Preprocesses one Verilog/SystemVerilog compilation-unit root. Object-like and
// function-like macros, command-line definitions, quoted/angle includes,
// conditional compilation, undefinition, and token concatenation are applied
// before parsing. Parser-state directives remain in the ordered token stream.
// Source and expansion provenance remain attached to tokens.
[[nodiscard]] PreprocessResult preprocess_verilog_file(
    const std::filesystem::path& path,
    Language language,
    const PreprocessorOptions& options = {});

// In-memory root variant used by embedders and fuzzers. Includes, when
// encountered, still resolve relative to source.name and configured roots.
[[nodiscard]] PreprocessResult preprocess_verilog(
    SourceText source,
    Language language,
    const PreprocessorOptions& options = {});

// Processes ordered roots as one Verilog compilation unit. Macro definitions,
// conditionals, and parser-visible directive context persist between roots.
[[nodiscard]] PreprocessCompilationUnitResult
preprocess_verilog_compilation_unit(
    const std::vector<std::filesystem::path>& paths,
    Language language,
    const PreprocessorOptions& options = {});

}  // namespace fsim::frontend
