#include "fsim/diagnostic/diagnostic.hpp"
// SPDX-License-Identifier: Apache-2.0
#include "fsim/project/project.hpp"

#include <algorithm>
#include <array>
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
uvm_release = "2020.3.1"

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
trace_format = "vcd"
trace_compression = "none"
trace_filters = ["tb.*"]
trace_report_limit = 17
trace_enabled = false

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
        config->source_sets[0].uvm_release
            == fsim::project::SystemVerilogUvmRelease::ieee_1800_2_2020_3_1,
        "governed UVM release selection is retained canonically");
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
        config->run.trace_format == fsim::project::TraceFormat::vcd,
        "trace format is parsed");
    check(
        config->build.cache_path == (workspace / "cache").lexically_normal(),
        "cache path is resolved relative to the manifest");
    check(
        config->run.trace_file ==
            std::optional<std::filesystem::path>(
                (workspace / "waves" / "out.vcd").lexically_normal()),
        "trace path is resolved relative to the manifest");
    check(
        config->run.trace_compression
            == fsim::project::TraceCompression::none,
        "trace compression is retained");
    check(
        config->run.trace_report_limit == 17U,
        "trace report limit is retained");
    check(!config->run.trace_enabled, "trace lifecycle is retained");
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
                  && diagnostic.message
                  == "unsupported project manifest identity: found schema 1; required schema 2; regenerate fsim.toml with this fsim build";
          }),
      "schema 1 rejection identifies the current schema and regeneration action");

  const auto require_schema_rejection = [](
                                            const std::string_view source,
                                            const std::string_view expected) {
      fsim::diagnostic::Engine schema_diagnostics;
      const auto rejected = fsim::project::parse(
          source, "schema-rejection.toml", ".", schema_diagnostics);
      check(!rejected.has_value(), "non-current project schema is rejected");
      check(
          std::ranges::any_of(
              schema_diagnostics.diagnostics(),
              [&](const fsim::diagnostic::Diagnostic& diagnostic) {
                  return diagnostic.code == "FSIM-PROJ-0005"
                      && diagnostic.message == expected;
              }),
          "project schema rejection retains its exact regeneration diagnostic");
  };
  require_schema_rejection(
      R"([project]
top = "top"
)",
      "unsupported project manifest identity: found no schema; required schema 2; regenerate fsim.toml with this fsim build");
  require_schema_rejection(
      R"(schema = 0
[project]
top = "top"
)",
      "unsupported project manifest identity: found schema 0; required schema 2; regenerate fsim.toml with this fsim build");
  require_schema_rejection(
      R"(schema = 3
[project]
top = "top"
)",
      "unsupported project manifest identity: found schema 3; required schema 2; regenerate fsim.toml with this fsim build");
  require_schema_rejection(
      R"(schema = 18446744073709551615
[project]
top = "top"
)",
      "unsupported project manifest identity: found schema outside the uint32 range; required schema 2; regenerate fsim.toml with this fsim build");
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

void test_trace_format_values() {
  using Format = fsim::project::TraceFormat;
  check(
      fsim::project::RunSection{}.trace_format == Format::automatic,
      "automatic is the deterministic default trace format");
  check(
      fsim::project::parse_trace_format("automatic") == Format::automatic
          && fsim::project::parse_trace_format("VCD") == Format::vcd
          && fsim::project::parse_trace_format("fst") == Format::fst
          && fsim::project::to_string(Format::automatic) == "auto"
          && fsim::project::to_string(Format::vcd) == "vcd"
          && fsim::project::to_string(Format::fst) == "fst",
      "trace-format aliases parse and serialize canonically");

  fsim::diagnostic::Engine diagnostics;
  const auto config = fsim::project::parse(
      R"(schema = 2
[project]
top = "top"
[run]
trace_format = "wave"
)",
      "bad-trace-format.toml",
      ".",
      diagnostics);
  check(!config.has_value(), "invalid manifest trace format is rejected");
  check(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0007"
                && diagnostic.message.find("trace_format")
                    != std::string::npos;
          }),
      "invalid manifest trace format has a stable targeted diagnostic");

  using Compression = fsim::project::TraceCompression;
  check(
      fsim::project::RunSection{}.trace_compression
              == Compression::automatic
          && fsim::project::parse_trace_compression("automatic")
              == Compression::automatic
          && fsim::project::parse_trace_compression("NONE")
              == Compression::none
          && fsim::project::parse_trace_compression("fixed")
              == Compression::deterministic
          && fsim::project::parse_trace_compression("fst-v1")
              == Compression::deterministic
          && fsim::project::to_string(Compression::automatic) == "auto"
          && fsim::project::to_string(Compression::none) == "none"
          && fsim::project::to_string(Compression::deterministic)
              == "deterministic",
      "trace-compression aliases parse and serialize canonically");

  diagnostics.clear();
  const auto bad_compression = fsim::project::parse(
      R"(schema = 2
[project]
top = "top"
[run]
trace_compression = "host"
)",
      "bad-trace-compression.toml",
      ".",
      diagnostics);
  check(!bad_compression.has_value(),
      "invalid manifest trace compression is rejected");
  check(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0007"
                && diagnostic.message.find("trace_compression")
                    != std::string::npos;
          }),
      "invalid trace compression has a stable targeted diagnostic");

  diagnostics.clear();
  const auto zero_report = fsim::project::parse(
      R"(schema = 2
[project]
top = "top"
[run]
trace_report_limit = 0
)",
      "bad-trace-report.toml",
      ".",
      diagnostics);
  check(!zero_report.has_value(),
      "zero manifest trace report limit is rejected");
  check(
      std::ranges::any_of(
          diagnostics.diagnostics(),
          [](const fsim::diagnostic::Diagnostic& diagnostic) {
            return diagnostic.code == "FSIM-PROJ-0007"
                && diagnostic.message.find("trace_report_limit")
                    != std::string::npos;
          }),
      "zero trace report limit has a stable targeted diagnostic");
}

void test_uvm_release_values() {
  using Release = fsim::project::SystemVerilogUvmRelease;
  check(
      fsim::project::parse_systemverilog_uvm_release("uvm-1.2")
              == Release::uvm_1_2
          && fsim::project::parse_systemverilog_uvm_release("2020.3.1")
              == Release::ieee_1800_2_2020_3_1
          && fsim::project::to_string(Release::uvm_1_2) == "1.2"
          && fsim::project::to_string(
                 Release::ieee_1800_2_2020_3_1) == "2020.3.1",
      "UVM release aliases normalize to two canonical governed values");

  fsim::diagnostic::Engine diagnostics;
  const auto config = fsim::project::parse(
      R"(schema = 2
[[source_set]]
language = "vhdl"
uvm_release = "1.2"
files = ["missing.vhd"]
)",
      "bad-uvm-release.toml", ".", diagnostics);
  check(!config.has_value(), "non-SystemVerilog UVM release is rejected");
  check(
      std::ranges::any_of(
          diagnostics.diagnostics(), [](const auto& diagnostic) {
              return diagnostic.code == "FSIM-PROJ-0007"
                  && diagnostic.message.find("uvm_release")
                  != std::string::npos;
          }),
      "invalid UVM release placement has a stable manifest diagnostic");
}

void test_vhdl_standard_values()
{
    using fsim::project::VhdlStandard;
    const std::array aliases {
        std::tuple { "87", "1987", VhdlStandard::vhdl_1987 },
        std::tuple { "1987", "1987", VhdlStandard::vhdl_1987 },
        std::tuple { "vhdl-87", "1987", VhdlStandard::vhdl_1987 },
        std::tuple { "93", "1993", VhdlStandard::vhdl_1993 },
        std::tuple { "1993", "1993", VhdlStandard::vhdl_1993 },
        std::tuple { "vhdl-1993", "1993", VhdlStandard::vhdl_1993 },
        std::tuple { "00", "2000", VhdlStandard::vhdl_2000 },
        std::tuple { "2000", "2000", VhdlStandard::vhdl_2000 },
        std::tuple { "02", "2002", VhdlStandard::vhdl_2002 },
        std::tuple { "2002", "2002", VhdlStandard::vhdl_2002 },
        std::tuple { "08", "2008", VhdlStandard::vhdl_2008 },
        std::tuple { "2008", "2008", VhdlStandard::vhdl_2008 },
    };
    for (const auto& [spelling, canonical, identity] : aliases) {
        fsim::diagnostic::Engine diagnostics;
        const auto manifest = std::string { "schema = 2\n[project]\ntop = \"standard_mode\"\n"
                                            "[[source_set]]\nlanguage = \"vhdl\"\nstandard = \"" }
            + spelling
            + "\"\nfiles = [\"project_config_test.cpp\"]\n";
        const auto config = fsim::project::parse(
            manifest, "vhdl-standard.toml",
            std::filesystem::path { __FILE__ }.parent_path(), diagnostics);
        check(
            config && !diagnostics.has_error()
                && config->source_sets.size() == 1
                && config->source_sets[0].standard == canonical,
            std::string { "VHDL standard alias is canonicalized: " } + spelling);
        check(
            fsim::project::parse_vhdl_standard(spelling) == identity
                && fsim::project::to_string(identity) == canonical,
            std::string { "VHDL standard identity round-trips: " } + spelling);
    }
    check(
        fsim::project::parse_language("VHDL-93")
                == fsim::project::Language::vhdl
            && fsim::project::default_standard(fsim::project::Language::vhdl)
                == "2008",
        "explicit VHDL language profiles retain the VHDL-2008 default");

    fsim::diagnostic::Engine legacy_diagnostics;
    const auto legacy = fsim::project::parse(
        R"(schema = 2
[project]
top = "legacy_mode"
[[source_set]]
language = "vhdl"
standard = "1988"
files = ["project_config_test.cpp"]
)",
        "vhdl-1988.toml", std::filesystem::path { __FILE__ }.parent_path(),
        legacy_diagnostics);
    check(!legacy.has_value(), "an unknown VHDL standard mode is rejected");
    check(
        std::ranges::any_of(
            legacy_diagnostics.diagnostics(), [](const auto& diagnostic) {
                return diagnostic.message.find(
                           "unsupported standard '1988' for vhdl")
                    != std::string::npos;
            }),
        "VHDL standard-mode rejection is targeted and deterministic");
}

void test_verilog_systemverilog_standard_values()
{
    using fsim::project::SystemVerilogStandard;
    using fsim::project::VerilogStandard;
    const std::array verilog_aliases {
        std::tuple { "95", "1995", VerilogStandard::verilog_1995 },
        std::tuple { "verilog-1995", "1995", VerilogStandard::verilog_1995 },
        std::tuple { "01", "2001", VerilogStandard::verilog_2001 },
        std::tuple { "v2001", "2001", VerilogStandard::verilog_2001 },
        std::tuple { "2001-noconfig", "2001-noconfig",
            VerilogStandard::verilog_2001_noconfig },
        std::tuple { "verilog-2001-noconfig", "2001-noconfig",
            VerilogStandard::verilog_2001_noconfig },
        std::tuple { "05", "2005", VerilogStandard::verilog_2005 },
        std::tuple { "verilog-2005", "2005", VerilogStandard::verilog_2005 },
    };
    for (const auto& [spelling, canonical, identity] : verilog_aliases) {
        fsim::diagnostic::Engine diagnostics;
        const auto manifest = std::string { "schema = 2\n[project]\ntop = \"standard_mode\"\n"
                                            "[[source_set]]\nlanguage = \"verilog\"\nstandard = \"" }
            + spelling
            + "\"\nfiles = [\"project_config_test.cpp\"]\n";
        const auto config = fsim::project::parse(
            manifest, "verilog-standard.toml",
            std::filesystem::path { __FILE__ }.parent_path(), diagnostics);
        check(
            config && !diagnostics.has_error()
                && config->source_sets.size() == 1
                && config->source_sets[0].standard == canonical,
            std::string { "Verilog standard alias is canonicalized: " }
                + spelling);
        check(
            fsim::project::parse_verilog_standard(spelling) == identity
                && fsim::project::to_string(identity) == canonical,
            std::string { "Verilog standard identity round-trips: " }
                + spelling);
    }

    const std::array systemverilog_aliases {
        std::tuple { "05", "2005",
            SystemVerilogStandard::systemverilog_2005 },
        std::tuple { "systemverilog-2005", "2005",
            SystemVerilogStandard::systemverilog_2005 },
        std::tuple { "09", "2009",
            SystemVerilogStandard::systemverilog_2009 },
        std::tuple { "sv-2009", "2009",
            SystemVerilogStandard::systemverilog_2009 },
        std::tuple { "12", "2012",
            SystemVerilogStandard::systemverilog_2012 },
        std::tuple { "systemverilog-2012", "2012",
            SystemVerilogStandard::systemverilog_2012 },
        std::tuple { "17", "2017",
            SystemVerilogStandard::systemverilog_2017 },
        std::tuple { "sv-2017", "2017",
            SystemVerilogStandard::systemverilog_2017 },
    };
    for (const auto& [spelling, canonical, identity] : systemverilog_aliases) {
        fsim::diagnostic::Engine diagnostics;
        const auto manifest = std::string { "schema = 2\n[project]\ntop = \"standard_mode\"\n"
                                            "[[source_set]]\nlanguage = \"systemverilog\"\nstandard = \"" }
            + spelling
            + "\"\nfiles = [\"project_config_test.cpp\"]\n";
        const auto config = fsim::project::parse(
            manifest, "systemverilog-standard.toml",
            std::filesystem::path { __FILE__ }.parent_path(), diagnostics);
        check(
            config && !diagnostics.has_error()
                && config->source_sets.size() == 1
                && config->source_sets[0].standard == canonical,
            std::string { "SystemVerilog standard alias is canonicalized: " }
                + spelling);
        check(
            fsim::project::parse_systemverilog_standard(spelling) == identity
                && fsim::project::to_string(identity) == canonical,
            std::string { "SystemVerilog standard identity round-trips: " }
                + spelling);
    }

    check(
        fsim::project::parse_language("Verilog-2001-noconfig")
                == fsim::project::Language::verilog
            && fsim::project::parse_language("SV-2009")
                == fsim::project::Language::system_verilog
            && fsim::project::default_standard(
                   fsim::project::Language::verilog)
                == "2005"
            && fsim::project::default_standard(
                   fsim::project::Language::system_verilog)
                == "2017",
        "explicit language profiles retain the Verilog-2005 and SystemVerilog-2017 defaults");
    check(
        !fsim::project::canonical_standard(
            fsim::project::Language::verilog, "sv-2009")
            && !fsim::project::canonical_standard(
                fsim::project::Language::system_verilog, "verilog-1995")
            && !fsim::project::parse_verilog_standard("1996")
            && !fsim::project::parse_systemverilog_standard("2018"),
        "cross-family and unknown Verilog/SystemVerilog standards are rejected");

    const std::array rejected {
        std::tuple { "verilog", "sv-2009" },
        std::tuple { "systemverilog", "2018" },
    };
    for (const auto& [language, standard] : rejected) {
        fsim::diagnostic::Engine diagnostics;
        const auto manifest = std::string { "schema = 2\n[project]\ntop = \"standard_mode\"\n"
                                            "[[source_set]]\nlanguage = \"" }
            + language + "\"\nstandard = \"" + standard
            + "\"\nfiles = [\"project_config_test.cpp\"]\n";
        const auto config = fsim::project::parse(
            manifest, "rejected-standard.toml",
            std::filesystem::path { __FILE__ }.parent_path(), diagnostics);
        check(
            !config.has_value()
                && std::ranges::any_of(
                    diagnostics.diagnostics(), [&](const auto& diagnostic) {
                        return diagnostic.message.find(
                                   std::string { "unsupported standard '" }
                                   + standard + "' for " + language)
                            != std::string::npos;
                    }),
            std::string { "standard rejection is source-owned: " } + language + '/' + standard);
    }
}

void test_verilog_systemverilog_compatibility_selection()
{
    fsim::diagnostic::Engine diagnostics;
    const auto config = fsim::project::parse(
        R"(schema = 2
[project]
top = "compatibility_mode"
[[source_set]]
language = "systemverilog"
standard = "2009"
compatibility = ["sizing", "implicit_net", "keyword-profile"]
files = ["project_config_test.cpp"]
)",
        "compatibility.toml",
        std::filesystem::path { __FILE__ }.parent_path(), diagnostics);
    check(
        config && !diagnostics.has_error()
            && fsim::project::compatibility_profile(
                   config->source_sets.front().compatibility_switches)
                == "keyword-profile,implicit-net,sizing",
        "compatibility switches canonicalize independently of revision and input order");
    check(
        fsim::project::parse_compatibility_switch("scheduler_assertion")
                == "scheduler-assertion"
            && !fsim::project::parse_compatibility_switch("future-switch"),
        "compatibility switch aliases round-trip and unknown switches are rejected");

    fsim::diagnostic::Engine invalid_diagnostics;
    const auto invalid = fsim::project::parse(
        R"(schema = 2
[project]
top = "invalid_compatibility"
[[source_set]]
language = "vhdl"
compatibility = ["sizing"]
files = ["project_config_test.cpp"]
)",
        "invalid-compatibility.toml",
        std::filesystem::path { __FILE__ }.parent_path(),
        invalid_diagnostics);
    check(
        !invalid && invalid_diagnostics.has_error(),
        "compatibility switches reject cross-family selection");
}

} // namespace

int main()
{
    // FSIM-CONFORMANCE CF-COMMON-PROJECT-001 source=SRC-FSIM expectation=accept
    // FSIM-CONFORMANCE CF-COMMON-DIAGNOSTIC-N01 source=SRC-FSIM expectation=reject
    test_complete_manifest_and_glob_order();
    test_multiple_top_manifest_model();
    test_schema_and_unknown_key_errors();
    test_elaboration_search_library_validation();
    test_library_mapping_validation();
    test_json_diagnostics_are_escaped();
    test_zero_time_resolution_is_rejected();
    test_delay_mode_values();
    test_trace_format_values();
    test_uvm_release_values();
    test_vhdl_standard_values();
    test_verilog_systemverilog_standard_values();
    test_verilog_systemverilog_compatibility_selection();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "project_config_test: all tests passed\n";
    return 0;
}
