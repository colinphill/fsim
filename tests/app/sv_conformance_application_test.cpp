// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct TemporaryDirectory {
  std::filesystem::path path;

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

struct Capture {
  fsim::runtime::RunResult result;
  std::array<std::uint64_t, 4> values{};
  std::string output_file;
  std::string memory_file;
  std::vector<std::string> cache_keys;
  fsim::app::NativeCacheStatistics cache;
  std::size_t compiled_processes{};
  std::size_t compiled_modules{};
};

void write_text(
    const std::filesystem::path& path,
    const std::string_view contents) {
  std::ofstream output(
      path, std::ios::binary | std::ios::trunc);
  output << contents;
  assert(output.good());
}

[[nodiscard]] std::string read_text(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
}

fsim::project::Config make_config(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "sv-public-conformance";
  config.project.top = "sv:work.conformance_runtime_top";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "cache-o0"
             : "cache-o2");
  config.run.max_deltas = 1000;
  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2005";
  sources.library = "work";
  sources.compilation_unit = "file";
  sources.files = {source};
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture run_once(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(project);

  const std::array<std::string_view, 4> paths{
      "conformance_runtime_top.core_result",
      "conformance_runtime_top.timed_result",
      "conformance_runtime_top.assertion_seen",
      "conformance_runtime_top.data_result"};
  std::array<fsim::runtime::simir::SignalId, 4> signals{};
  for (std::size_t index = 0; index < paths.size(); ++index) {
    const auto signal = project->design.find_signal(paths[index]);
    assert(signal);
    signals[index] = *signal;
  }

  Capture capture;
  capture.cache_keys = project->specialization_cache_keys;
  fsim::app::Simulation simulation{
      std::move(*project), config.run.max_deltas, engine};
  capture.compiled_processes = simulation.compiled_process_count();
  capture.compiled_modules = simulation.compiled_module_count();
  capture.cache = simulation.native_cache_statistics();
  capture.result = simulation.run();
  for (std::size_t index = 0; index < signals.size(); ++index) {
    const auto word = simulation.read_signal(signals[index]).low_word();
    assert(word.bval == 0);
    capture.values[index] = word.aval;
  }
  capture.output_file =
      read_text(config.base_directory / "conformance-output.txt");
  capture.memory_file =
      read_text(config.base_directory / "conformance-memory.hex");
  return capture;
}

void compare_captures(
    const Capture& reference,
    const Capture& candidate) {
  assert(reference.result.status == candidate.result.status);
  assert(reference.result.time == candidate.result.time);
  assert(reference.result.delta == candidate.result.delta);
  assert(reference.values == candidate.values);
  assert(reference.output_file == candidate.output_file);
  assert(reference.memory_file == candidate.memory_file);
  assert(reference.cache_keys == candidate.cache_keys);
}

[[nodiscard]] bool has_diagnostic(
    const fsim::diagnostic::Engine& diagnostics,
    const std::string_view code)
{
    return std::ranges::any_of(
        diagnostics.diagnostics(), [&](const auto& diagnostic) {
            return diagnostic.code == code;
        });
}

fsim::project::SourceSet identity_source_set(
    const fsim::project::Language language,
    std::string standard,
    const std::filesystem::path& file,
    const std::filesystem::path& include_directory = { })
{
    fsim::project::SourceSet source_set;
    source_set.language = language;
    source_set.standard = std::move(standard);
    source_set.library = "work";
    source_set.compilation_unit = "file";
    source_set.files = { file };
    if (!include_directory.empty()) {
        source_set.include_directories = { include_directory };
    }
    return source_set;
}

void test_standard_identity(const std::filesystem::path& directory)
{
    const auto identity = directory / "standard-identity";
    std::filesystem::create_directories(identity);
    const auto legacy_header = identity / "legacy-defs.vh";
    const auto legacy = identity / "legacy.v";
    const auto modern = identity / "modern.sv";
    write_text(legacy_header, "`define LEGACY_VALUE 1\n");
    write_text(
        legacy,
        "`include \"legacy-defs.vh\"\n"
        "module legacy_unit; integer value; endmodule\n");
    write_text(modern, "module modern_unit; endmodule\n");

    fsim::project::Config mixed;
    mixed.base_directory = identity;
    mixed.project.top = "verilog:work.legacy_unit";
    mixed.source_sets = {
        identity_source_set(
            fsim::project::Language::verilog, "2001", legacy, identity),
        identity_source_set(
            fsim::project::Language::system_verilog, "2009", modern),
    };
    fsim::diagnostic::Engine mixed_diagnostics;
    const auto checked = fsim::app::check_project(mixed, mixed_diagnostics);
    if (!checked) {
        for (const auto& diagnostic : mixed_diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": " << diagnostic.message << '\n';
        }
    }
    assert(checked && !mixed_diagnostics.has_error());
    assert(checked->hdl_sources.size() == 2);
    assert(
        checked->hdl_sources[0].standard_revision
        == fsim::frontend::StandardRevision::Verilog2001);
    assert(checked->hdl_sources[0].dependencies.size() == 1);
    assert(
        checked->hdl_sources[0].dependencies[0].standard_revision
        == fsim::frontend::StandardRevision::Verilog2001);
    assert(
        checked->hdl_sources[1].standard_revision
        == fsim::frontend::StandardRevision::SystemVerilog2009);
    assert(checked->parsed.units.size() == 2);
    assert(
        checked->parsed.units[0].standard_revision
        == fsim::frontend::StandardRevision::Verilog2001);
    assert(
        checked->parsed.units[1].standard_revision
        == fsim::frontend::StandardRevision::SystemVerilog2009);

    const auto shared = identity / "shared.svh";
    const auto first = identity / "first.sv";
    const auto second = identity / "second.sv";
    write_text(shared, "`define SHARED_VALUE 1\n");
    write_text(
        first,
        "`include \"shared.svh\"\nmodule first; endmodule\n");
    write_text(
        second,
        "`include \"shared.svh\"\nmodule second; endmodule\n");
    fsim::project::Config include_mismatch;
    include_mismatch.base_directory = identity;
    include_mismatch.project.top = "sv:work.first";
    include_mismatch.source_sets = {
        identity_source_set(
            fsim::project::Language::system_verilog, "2009", first, identity),
        identity_source_set(
            fsim::project::Language::system_verilog, "2012", second, identity),
    };
    fsim::diagnostic::Engine include_diagnostics;
    assert(!fsim::app::check_project(include_mismatch, include_diagnostics));
    assert(has_diagnostic(include_diagnostics, "FSIM-FE-STANDARD-001"));

    const auto package = identity / "legacy-package.sv";
    const auto consumer = identity / "package-consumer.sv";
    write_text(package, "package legacy_pkg; parameter int VALUE = 1; endpackage\n");
    write_text(
        consumer,
        "module package_consumer; import legacy_pkg::*; "
        "integer value = VALUE; endmodule\n");
    fsim::project::Config import_mismatch;
    import_mismatch.base_directory = identity;
    import_mismatch.project.top = "sv:work.package_consumer";
    import_mismatch.source_sets = {
        identity_source_set(
            fsim::project::Language::system_verilog, "2009", package),
        identity_source_set(
            fsim::project::Language::system_verilog, "2012", consumer),
    };
    fsim::diagnostic::Engine import_diagnostics;
    assert(!fsim::app::check_project(import_mismatch, import_diagnostics));
    assert(has_diagnostic(import_diagnostics, "FSIM-FE-STANDARD-003"));

    fsim::project::Config reanalysis;
    reanalysis.base_directory = identity;
    reanalysis.project.top = "verilog:work.legacy_unit";
    reanalysis.source_sets = {
        identity_source_set(
            fsim::project::Language::verilog, "2001", legacy, identity),
        identity_source_set(
            fsim::project::Language::verilog, "2005", legacy, identity),
    };
    fsim::diagnostic::Engine reanalysis_diagnostics;
    assert(!fsim::app::check_project(reanalysis, reanalysis_diagnostics));
    assert(has_diagnostic(reanalysis_diagnostics, "FSIM-FE-STANDARD-001"));
    assert(has_diagnostic(reanalysis_diagnostics, "FSIM-FE-STANDARD-002"));
}

void test_declaration_revision_gates(const std::filesystem::path& directory)
{
    const auto gates = directory / "declaration-revision-gates";
    std::filesystem::create_directories(gates);

    const auto v1995 = gates / "legal-1995.v";
    write_text(
        v1995,
        "module legal_1995(a, q); input [3:0] a; output [3:0] q; "
        "reg [3:0] q; parameter WIDTH = 4; always @(a) q = a; endmodule\n");
    fsim::project::Config legal_1995;
    legal_1995.base_directory = gates;
    legal_1995.project.top = "verilog:work.legal_1995";
    legal_1995.source_sets = {identity_source_set(
        fsim::project::Language::verilog, "1995", v1995)};
    fsim::diagnostic::Engine legal_1995_diagnostics;
    const auto checked_1995 =
        fsim::app::check_project(legal_1995, legal_1995_diagnostics);
    assert(checked_1995 && !legal_1995_diagnostics.has_error());
    assert(
        checked_1995->parsed.units.front().standard_revision
        == fsim::frontend::StandardRevision::Verilog1995);

    const auto rejected = gates / "rejected-1995.v";
    write_text(
        rejected,
        "module rejected_1995(input signed [3:0] a, output [3:0] q); "
        "reg value = 1'b0; assign q = a; endmodule\n");
    fsim::project::Config illegal_1995;
    illegal_1995.base_directory = gates;
    illegal_1995.project.top = "verilog:work.rejected_1995";
    illegal_1995.source_sets = {identity_source_set(
        fsim::project::Language::verilog, "1995", rejected)};
    fsim::diagnostic::Engine illegal_1995_diagnostics;
    assert(!fsim::app::check_project(illegal_1995, illegal_1995_diagnostics));
    assert(has_diagnostic(illegal_1995_diagnostics, "FSIM-SV-PARSE-346"));

    const auto v2001 = gates / "legal-2001.v";
    write_text(
        v2001,
        "module legal_2001 #(parameter WIDTH = 4) "
        "(input signed [WIDTH-1:0] a, output reg [WIDTH-1:0] q); "
        "integer state = 1; always @(a) q = a + state - 1; endmodule\n");
    fsim::project::Config legal_2001;
    legal_2001.base_directory = gates;
    legal_2001.project.top = "verilog:work.legal_2001";
    legal_2001.source_sets = {identity_source_set(
        fsim::project::Language::verilog, "2001", v2001)};
    fsim::diagnostic::Engine legal_2001_diagnostics;
    const auto checked_2001 =
        fsim::app::check_project(legal_2001, legal_2001_diagnostics);
    assert(checked_2001 && !legal_2001_diagnostics.has_error());
    assert(
        checked_2001->parsed.units.front().standard_revision
        == fsim::frontend::StandardRevision::Verilog2001);

    const auto compatibility_source = gates / "compatibility-profile.sv";
    write_text(
        compatibility_source,
        "module checker; logic [256:0] value; endmodule\n");
    fsim::project::Config compatibility_config;
    compatibility_config.base_directory = gates;
    compatibility_config.project.top = "sv:work.checker";
    auto compatibility_sources = identity_source_set(
        fsim::project::Language::system_verilog,
        "2009", compatibility_source);
    compatibility_sources.compatibility_switches = {
        "configuration", "scheduler_assertion", "lifetime", "sizing",
        "port_connection", "implicit_net", "keyword_profile"
    };
    compatibility_config.source_sets = { std::move(compatibility_sources) };
    fsim::diagnostic::Engine compatibility_diagnostics;
    const auto checked_compatibility = fsim::app::check_project(
        compatibility_config, compatibility_diagnostics);
    assert(checked_compatibility && !compatibility_diagnostics.has_error());
    assert(checked_compatibility->parsed.units.size() == 1
        && checked_compatibility->parsed.units.front().name == "checker"
        && checked_compatibility->parsed.units.front()
                .verilog_compatibility_profile
            == "keyword-profile,implicit-net,port-connection,sizing,lifetime,"
               "scheduler-assertion,configuration"
        && checked_compatibility->parsed.units.front().signals.front()
                .type.width()
            == std::optional<std::uint64_t> { 257 });
}

void test_predefined_environment_revision_gates(
    const std::filesystem::path& directory)
{
    const auto gates = directory / "predefined-environment-revision-gates";
    std::filesystem::create_directories(gates);
    const auto rejected = gates / "predefined-in-verilog.v";
    write_text(
        rejected,
        R"(module predefined_in_verilog;
  mailbox box;
  reg result;
  initial begin
    result = null == null;
    std::randomize(result);
    result = $root.predefined_in_verilog.result;
  end
endmodule
)");

    fsim::project::Config config;
    config.base_directory = gates;
    config.project.name = "sv-predefined-environment-revision-gates";
    config.project.top = "verilog:work.predefined_in_verilog";
    config.source_sets = {identity_source_set(
        fsim::project::Language::verilog, "2005", rejected)};
    fsim::diagnostic::Engine diagnostics;
    assert(!fsim::app::check_project(config, diagnostics));
    assert(has_diagnostic(diagnostics, "FSIM-SV-PARSE-349"));
}

} // namespace

int main()
{
    const auto serial = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectory directory {
        std::filesystem::temp_directory_path()
        / ("fsim-sv-public-conformance-" + std::to_string(serial))
    };
    std::filesystem::create_directories(directory.path);
    test_standard_identity(directory.path);
    test_declaration_revision_gates(directory.path);
    test_predefined_environment_revision_gates(directory.path);
    const auto source = directory.path / "conformance.sv";

    write_text(directory.path / "conformance-input.txt", "13 fsim\n");
    write_text(
        directory.path / "conformance-memory-input.hex",
        "0a\n0b\n0c\n0d\n");
    write_text(
        source,
        R"(module conformance_core(
  output logic [31:0] core_result,
  output logic [31:0] timed_result,
  output logic assertion_seen
);
  event completed;
  logic side_effect;

  // FSIM-CONFORMANCE CF-SV-EXPR-001 source=SRC-SV-TESTS expectation=execute
  function automatic logic touch();
    side_effect = 1'b1;
    return 1'b1;
  endfunction

  // FSIM-CONFORMANCE CF-SV-CALL-001 source=SRC-SLANG expectation=execute
  function automatic int transform(input int value);
    return value * 3 + 1;
  endfunction

  task automatic delayed_add(input int seed, output int value);
    int local_value;
    local_value = seed;
    #1;
    value = local_value + 2;
  endtask

  // FSIM-CONFORMANCE CF-SV-PROC-001 source=SRC-SURELOG expectation=execute
  // FSIM-CONFORMANCE CF-SV-TIME-001 source=SRC-SV-TESTS expectation=execute
  initial begin : producer
    int total;
    total = 0;
    side_effect = 1'b0;
    core_result = 0;
    assertion_seen = 1'b0;
    for (int index = 0; index < 4; index++) begin
      if (index == 1)
        continue;
      total = total + index;
    end
    casez (4'b10z1)
      4'b1?01: total = total + 4;
      default: total = 99;
    endcase
    if (1'b0 && touch())
      total = 99;
    delayed_add(transform(total), core_result);
    -> completed;
    // FSIM-CONFORMANCE CF-SV-ASSERT-001 source=SRC-SLANG expectation=execute
    assert (core_result == 30 && side_effect == 1'b0)
      assertion_seen = 1'b1;
    else
      $fatal(1, "public conformance core mismatch");
  end

  initial begin : observer
    @completed;
    #2 timed_result <= core_result + 4;
  end
endmodule

module conformance_data(output logic [31:0] data_result);
  integer reader;
  integer writer;
  integer scan_count;
  integer scanned;
  string label;
  string formatted;
  logic [7:0] memory[0:3];
  int dynamic_values[];
  byte queue_values[$:3];
  byte associative_values[int];

  initial begin : worker
    // FSIM-CONFORMANCE CF-SV-STRING-001 source=SRC-SV-TESTS expectation=execute
    // FSIM-CONFORMANCE CF-SV-FILE-001 source=SRC-SURELOG expectation=execute
    reader = $fopen("conformance-input.txt", "r");
    scan_count = $fscanf(reader, "%d %s", scanned, label);
    $fclose(reader);
    formatted = $sformatf("%s-%0d", label.toupper(), 7);

    // FSIM-CONFORMANCE CF-SV-MEMORY-001 source=SRC-SV-TESTS expectation=execute
    $readmemh("conformance-memory-input.hex", memory);
    $writememh("conformance-memory.hex", memory);

    // FSIM-CONFORMANCE CF-SV-DYNAMIC-001 source=SRC-SV-TESTS expectation=execute
    dynamic_values = '{4, 5, 6};
    // FSIM-CONFORMANCE CF-SV-QUEUE-001 source=SRC-SV-TESTS expectation=execute
    queue_values = '{3, 1};
    queue_values.push_back(2);
    queue_values.sort();
    // FSIM-CONFORMANCE CF-SV-ASSOC-001 source=SRC-SV-TESTS expectation=execute
    associative_values[4] = 9;
    associative_values[-1] = 5;

    data_result = scanned + memory[0]
        + dynamic_values.sum() + queue_values.sum()
        + associative_values.sum() + formatted.len();
    assert (scan_count == 2);
    assert (queue_values[0] == 1 && queue_values[2] == 3);
    formatted = $sformatf("result=%0d text=%s", data_result, formatted);
    writer = $fopen("conformance-output.txt", "w");
    $fdisplay(writer, "%s", formatted);
    $fclose(writer);
  end
endmodule

module conformance_runtime_top;
  logic [31:0] core_result;
  logic [31:0] timed_result;
  logic assertion_seen;
  logic [31:0] data_result;
  conformance_core core(core_result, timed_result, assertion_seen);
  conformance_data data(data_result);
endmodule
)");

    for (const auto optimization :
        { fsim::project::Optimization::o0,
            fsim::project::Optimization::o2 }) {
        const auto config = make_config(directory.path, source, optimization);
        const auto reference = run_once(config, fsim::app::SimulationEngine::interpreter);
        const auto cold = run_once(config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_once(config, fsim::app::SimulationEngine::compiled);

        compare_captures(reference, cold);
        compare_captures(reference, warm);
        assert(reference.result.status == fsim::runtime::RunStatus::completed);
        assert(reference.result.time == 3);
        assert((reference.values == std::array<std::uint64_t, 4> { 30, 34, 1, 64 }));
        assert(reference.output_file == "result=64 text=FSIM-7\n");
        assert(reference.memory_file == "0a\n0b\n0c\n0d\n");
        assert(reference.compiled_processes == 0);
        assert(reference.compiled_modules == 0);
#if defined(FSIM_HAS_LLVM)
    assert(cold.compiled_processes > 0);
    assert(cold.compiled_modules > 0);
    assert(cold.cache.misses > 0);
    assert(cold.cache.stores > 0);
    assert(warm.cache.hits > 0);
    assert(warm.cache.misses == 0);
#endif
  }
  return 0;
}
