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
  test_schema_and_unknown_key_errors();
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
