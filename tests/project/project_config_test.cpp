#include "fsim/diagnostic/diagnostic.hpp"
// SPDX-License-Identifier: Apache-2.0
#include "fsim/project/project.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>

namespace {

int failures = 0;

void check(const bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void write_file(
    const std::filesystem::path& path,
    const std::string& contents = {}) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::binary);
  stream << contents;
}

std::filesystem::path make_workspace() {
  const auto nonce =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  const auto path =
      std::filesystem::temp_directory_path() /
      ("fsim-project-test-" + std::to_string(nonce));
  std::filesystem::create_directories(path);
  return path;
}

void test_complete_manifest_and_glob_order() {
  const auto workspace = make_workspace();
  write_file(workspace / "rtl" / "z_child.sv", "module z_child; endmodule\n");
  write_file(workspace / "rtl" / "a_top.sv", "module a_top; endmodule\n");
  write_file(workspace / "vhdl" / "counter.vhd", "entity counter is end;\n");

  const std::string manifest = R"toml(
schema = 2

[project]
name = "mixed"
top = "tb"
time_resolution = "1ps"
seed = 42

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = [
  "rtl/*.sv",
]
include_dirs = ["include"]
defines = ["WIDTH=8"]
compilation_unit = "source-set"

[[source_set]]
language = "vhdl"
files = ["vhdl/counter.vhd"]

[[binding]]
instance = "tb.dut"
target = "vhdl:work.counter(rtl)"
resolver = "std_logic"

[[binding]]
instance = "tb.bus"
resolver = "sv_wire"

[elaboration]
search_libraries = ["vendor", "shared", "vendor"]

[[library_map]]
library = "vendor"
path = "libraries/vendor.fsimlib"

[[library_map]]
library = "shared"
path = "../shared.fsimlib"

[build]
optimization = "O3"
jobs = 4
cache_path = "cache"

[run]
duration = "20ns"
max_deltas = 999
delay_mode = "max"
trace_file = "waves/out.vcd"
trace_filters = ["tb.*"]

[systemc]
compiler = "clang++"
includes = ["systemc/include"]
defines = ["SC_INCLUDE_DYNAMIC_PROCESSES"]
compile_options = ["-Wall"]
link_options = ["-pthread"]
libraries = ["m"]
)toml";

  fsim::diagnostic::Engine diagnostics;
  auto config = fsim::project::parse(
      manifest,
      (workspace / "fsim.toml").generic_string(),
      workspace,
      diagnostics);
  check(config.has_value(), "complete manifest parses");
  check(!diagnostics.has_error(), "complete manifest has no diagnostics");
  if (config.has_value()) {
    check(config->schema == 2, "schema is retained");
    check(config->project.name == "mixed", "project name is retained");
    check(config->project.top == "tb", "top is retained");
    check(
        config->project.tops
            == std::vector<fsim::project::ProjectSection::TopLevel>{
                {"tb", "tb"}},
        "legacy single top is normalized into the ordered root list");
    check(config->project.seed == 42, "seed is retained");
    check(config->source_sets.size() == 2, "two source sets are retained");
    check(
        config->source_sets[0].files.size() == 2,
        "source glob expands both SystemVerilog files");
    if (config->source_sets[0].files.size() == 2) {
      check(
          config->source_sets[0].files[0].filename() == "a_top.sv",
          "glob matches are sorted lexicographically");
      check(
          config->source_sets[0].files[1].filename() == "z_child.sv",
          "glob ordering is stable");
    }
    check(
        config->source_sets[1].standard == "2008",
        "language-specific default standard is applied");
    check(
        config->bindings.size() == 2
            && !config->bindings[1].target.has_value()
            && config->bindings[1].resolver
                == std::optional<std::string>{"sv_wire"},
        "schema 2 retains a resolver-only inferred binding");
    check(
        config->elaboration.search_libraries
            == std::vector<std::string>{"vendor", "shared", "vendor"},
        "elaboration search libraries retain declared order");
    check(
        config->library_mappings
            == std::vector<fsim::project::LibraryMapping>{
                {"vendor", workspace / "libraries" / "vendor.fsimlib"},
                {"shared", workspace.parent_path() / "shared.fsimlib"}},
        "ordered library mappings retain names and manifest-relative paths");
    check(
        config->build.optimization == fsim::project::Optimization::o3,
        "optimization is parsed");
    check(config->run.max_deltas == 999, "maximum delta count is parsed");
    check(
        config->run.delay_mode == fsim::project::DelayMode::maximum,
        "maximum delay selection mode is parsed");
    check(
        config->build.cache_path == (workspace / "cache").lexically_normal(),
        "cache path is resolved relative to the manifest");
    check(
        config->run.trace_file ==
            std::optional<std::filesystem::path>(
                (workspace / "waves" / "out.vcd").lexically_normal()),
        "trace path is resolved relative to the manifest");
  }
  std::error_code ignored;
  std::filesystem::remove_all(workspace, ignored);
}

void test_multiple_top_manifest_model() {
  const auto workspace = make_workspace();
  write_file(
      workspace / "tops.sv",
      "module producer; endmodule\nmodule consumer; endmodule\n");
  const auto parse_manifest = [&](const std::string_view project_text) {
    fsim::diagnostic::Engine diagnostics;
    const auto config = fsim::project::parse(
        "schema = 2\n" + std::string{project_text}
            + "\n[[source_set]]\nlanguage = \"systemverilog\"\n"
              "files = [\"tops.sv\"]\n",
        (workspace / "fsim.toml").generic_string(),
        workspace,
        diagnostics);
    return std::pair{std::move(config), std::move(diagnostics)};
  };

  auto [multiple, multiple_diagnostics] = parse_manifest(R"toml(
[project]
name = "multiple"

[[project.top]]
target = "sv:work.producer"
alias = "source"

[[project.top]]
target = "consumer"
alias = "sink"
)toml");
  check(multiple.has_value(), "ordered multiple top records parse");
  check(
      !multiple_diagnostics.has_error(),
      "ordered multiple top records have no diagnostics");
  if (multiple.has_value()) {
    check(
        multiple->project.top.empty(),
        "the legacy singular top remains empty for multiple roots");
    check(
        multiple->project.tops
            == std::vector<fsim::project::ProjectSection::TopLevel>{
                {"sv:work.producer", "source"},
                {"consumer", "sink"}},
        "multiple roots retain target and alias order");
  }

  auto [single, single_diagnostics] = parse_manifest(R"toml(
[project]

[[project.top]]
target = "sv:work.producer"
)toml");
  check(single.has_value(), "a single top record may infer its alias");
  check(!single_diagnostics.has_error(), "single inferred alias is valid");
  if (single.has_value()) {
    check(
        single->project.top == "sv:work.producer"
            && single->project.tops.front().alias == "producer",
        "single top record populates the legacy view and inferred alias");
  }

  for (const auto& [name, project_text, expected] : std::vector<
           std::tuple<std::string, std::string, std::string>>{
           {"duplicate",
            R"toml([project]
[[project.top]]
target = "producer"
alias = "root"
[[project.top]]
target = "consumer"
alias = "root")toml",
            "duplicate top alias"},
           {"missing",
            R"toml([project]
[[project.top]]
target = "producer"
[[project.top]]
target = "consumer"
alias = "sink")toml",
            "missing a non-empty 'alias'"},
           {"unsafe",
            R"toml([project]
[[project.top]]
target = "producer"
alias = "bad.path")toml",
            "top alias 'bad.path'"},
           {"mixed",
            R"toml([project]
top = "producer"
[[project.top]]
target = "consumer"
alias = "sink")toml",
            "cannot be used together"}}) {
    auto [rejected, diagnostics] = parse_manifest(project_text);
    check(!rejected.has_value(), name + " top selection is rejected");
    check(
        std::ranges::any_of(
            diagnostics.diagnostics(),
            [&](const fsim::diagnostic::Diagnostic& diagnostic) {
              return diagnostic.message.find(expected) != std::string::npos;
            }),
        name + " top selection has a targeted diagnostic");
  }

  std::error_code ignored;
  std::filesystem::remove_all(workspace, ignored);
}

void test_schema_and_unknown_key_errors() {
  const std::string manifest = R"(
schema = 1
[project]
top = "top"
mystery = true
)";
  fsim::diagnostic::Engine diagnostics;
  const auto config =
      fsim::project::parse(manifest, "bad.toml", ".", diagnostics);
  check(!config.has_value(), "invalid manifest is rejected");
  check(diagnostics.has_error(), "invalid manifest emits errors");

  bool saw_schema = false;
  bool saw_unknown = false;
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    saw_schema |= diagnostic.code == "FSIM-PROJ-0005";
    saw_unknown |= diagnostic.code == "FSIM-PROJ-0003";
  }
  check(saw_schema, "unsupported schema has a stable diagnostic code");
  check(saw_unknown, "unknown key has a stable diagnostic code");
  check(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0005"
                && diagnostic.message.find("fsim migrate --to 2")
                    != std::string::npos;
          }),
      "schema 1 rejection names the migration command");
}

void test_elaboration_search_library_validation() {
  const std::string manifest = R"(
schema = 2
[project]
top = "top"
[elaboration]
search_libraries = ["vendor", ""]
[[source_set]]
language = "systemverilog"
files = ["top.sv"]
)";
  fsim::diagnostic::Engine diagnostics;
  const auto config =
      fsim::project::parse(manifest, "bad-search.toml", ".", diagnostics);
  check(!config.has_value(), "an empty search-library name is rejected");
  check(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0007"
                && diagnostic.message.find("search_libraries")
                    != std::string::npos;
          }),
      "empty search-library names use the stable value diagnostic");
}

void test_library_mapping_validation() {
  fsim::diagnostic::Engine mapping_only_diagnostics;
  const auto mapping_only = fsim::project::parse(
      "schema = 2\n[project]\ntop = \"sv:vendor.top\"\n"
      "[[library_map]]\nlibrary = \"vendor\"\n"
      "path = \"vendor.fsimlib\"\n",
      "mapping-only.toml", ".", mapping_only_diagnostics);
  check(
      mapping_only.has_value(),
      "a mapping-only precompiled-library project is accepted");

  const auto parse_mapping = [](const std::string_view mapping_text) {
    fsim::diagnostic::Engine diagnostics;
    const auto config = fsim::project::parse(
        "schema = 2\n[project]\ntop = \"top\"\n"
        "[[source_set]]\nlanguage = \"systemverilog\"\n"
        "library = \"work\"\nfiles = [\"top.sv\"]\n"
            + std::string{mapping_text},
        "bad-mapping.toml",
        ".",
        diagnostics);
    return std::pair{std::move(config), std::move(diagnostics)};
  };

  auto [duplicate, duplicate_diagnostics] = parse_mapping(R"toml(
[[library_map]]
library = "vendor"
path = "one.fsimlib"
[[library_map]]
library = "VENDOR"
path = "two.fsimlib"
)toml");
  check(!duplicate.has_value(), "case-folded duplicate mappings are rejected");
  check(
      std::ranges::any_of(
          duplicate_diagnostics.diagnostics(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0004"
                && diagnostic.message.find("duplicate mapped")
                    != std::string::npos;
          }),
      "duplicate mappings use the stable duplicate diagnostic");

  auto [colliding, colliding_diagnostics] = parse_mapping(R"toml(
[[library_map]]
library = "work"
path = "work.fsimlib"
)toml");
  check(!colliding.has_value(), "project-built library collisions are rejected");
  check(
      std::ranges::any_of(
          colliding_diagnostics.diagnostics(),
          [](const auto& diagnostic) {
            return diagnostic.message.find("project-built")
                != std::string::npos;
          }),
      "project-built collisions receive a targeted diagnostic");

  auto [unsafe, unsafe_diagnostics] = parse_mapping(R"toml(
[[library_map]]
library = "../vendor"
path = "vendor.fsimlib"
[[library_map]]
library = "ieee"
path = "ieee.fsimlib"
[[library_map]]
library = "missing_path"
)toml");
  check(!unsafe.has_value(), "unsafe, reserved, and incomplete mappings reject");
  check(
      std::ranges::any_of(
          unsafe_diagnostics.diagnostics(),
          [](const auto& diagnostic) {
            return diagnostic.message.find("only letters, digits")
                != std::string::npos;
          }),
      "path-unsafe logical names receive a targeted diagnostic");
  check(
      std::ranges::any_of(
          unsafe_diagnostics.diagnostics(),
          [](const auto& diagnostic) {
            return diagnostic.message.find("reserved by fsim")
                != std::string::npos;
          }),
      "reserved mapped libraries receive a targeted diagnostic");
  check(
      std::ranges::any_of(
          unsafe_diagnostics.diagnostics(),
          [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0006"
                && diagnostic.message.find("non-empty 'path'")
                    != std::string::npos;
          }),
      "missing mapping paths use the stable required-value diagnostic");
}

void test_schema_migration() {
  const auto workspace = make_workspace();
  const auto manifest = workspace / "fsim.toml";
  write_file(
      manifest,
      "# retained comment\n  schema = 1 # version\n[project]\n"
      "top = \"top\"\n");
  fsim::diagnostic::Engine diagnostics;
  const auto migrated =
      fsim::project::migrate_to_schema_2(manifest, diagnostics);
  check(migrated.has_value(), "schema 1 migration succeeds");
  check(!diagnostics.has_error(), "schema migration has no diagnostics");
  if (migrated.has_value()) {
    check(
        migrated->find("  schema = 2 # version")
            != std::string::npos,
        "migration changes only the schema value");
    check(
        migrated->find("# retained comment") == 0,
        "migration retains surrounding text");
  }
  std::error_code ignored;
  std::filesystem::remove_all(workspace, ignored);
}

void test_json_diagnostics_are_escaped() {
  fsim::diagnostic::Engine diagnostics;
  diagnostics.error(
      "FSIM-TEST",
      "quoted \"message\"\nnext",
      {"a\\b.sv", {2, 3, 4}, {2, 4, 5}});
  std::ostringstream output;
  fsim::diagnostic::print_json(output, diagnostics);
  check(
      output.str().find("quoted \\\"message\\\"\\nnext") != std::string::npos,
      "JSON diagnostic message is escaped");
  check(
      output.str().find("a\\\\b.sv") != std::string::npos,
      "JSON diagnostic path is escaped");
}

void test_zero_time_resolution_is_rejected() {
  const auto workspace = make_workspace();
  write_file(workspace / "top.sv", "module top; endmodule\n");
  const std::string manifest = R"(
schema = 2
[project]
top = "top"
time_resolution = "0ns"

[[source_set]]
language = "systemverilog"
files = ["top.sv"]
)";
  fsim::diagnostic::Engine diagnostics;
  const auto config = fsim::project::parse(
      manifest,
      (workspace / "fsim.toml").generic_string(),
      workspace,
      diagnostics);
  check(!config.has_value(), "zero time resolution is rejected");
  bool found = false;
  for (const auto& diagnostic : diagnostics.diagnostics()) {
    found = found
        || diagnostic.message.find("time_resolution")
            != std::string::npos;
  }
  check(found, "zero time resolution has a targeted manifest diagnostic");
  std::error_code ignored;
  std::filesystem::remove_all(workspace, ignored);
}

void test_delay_mode_values() {
  check(
      fsim::project::RunSection{}.delay_mode
          == fsim::project::DelayMode::typical,
      "typical is the deterministic default delay mode");
  check(
      fsim::project::parse_delay_mode("minimum")
          == fsim::project::DelayMode::minimum
          && fsim::project::parse_delay_mode("TYP")
              == fsim::project::DelayMode::typical
          && fsim::project::parse_delay_mode("maximum")
              == fsim::project::DelayMode::maximum
          && fsim::project::to_string(
                 fsim::project::DelayMode::minimum)
              == "min"
          && fsim::project::to_string(
                 fsim::project::DelayMode::typical)
              == "typ"
          && fsim::project::to_string(
                 fsim::project::DelayMode::maximum)
              == "max",
      "delay-mode aliases parse and serialize canonically");

  fsim::diagnostic::Engine diagnostics;
  const auto config = fsim::project::parse(
      R"(schema = 2
[project]
top = "top"
[run]
delay_mode = "slow"
)",
      "bad-delay-mode.toml",
      ".",
      diagnostics);
  check(!config.has_value(), "invalid manifest delay mode is rejected");
  check(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0007"
                && diagnostic.message.find("delay_mode")
                    != std::string::npos;
          }),
      "invalid manifest delay mode has a stable targeted diagnostic");
}

}  // namespace

int main() {
  // FSIM-CONFORMANCE CF-COMMON-PROJECT-001 source=SRC-FSIM expectation=accept
  // FSIM-CONFORMANCE CF-COMMON-DIAGNOSTIC-N01 source=SRC-FSIM expectation=reject
  test_complete_manifest_and_glob_order();
  test_multiple_top_manifest_model();
  test_schema_and_unknown_key_errors();
  test_elaboration_search_library_validation();
  test_library_mapping_validation();
  test_schema_migration();
  test_json_diagnostics_are_escaped();
  test_zero_time_resolution_is_rejected();
  test_delay_mode_values();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "project_config_test: all tests passed\n";
  return 0;
}
