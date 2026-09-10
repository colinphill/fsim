// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"
#include "fsim/artifact/design.hpp"
#include "fsim/artifact/object.hpp"
#include "fsim/frontend/systemverilog_standard_package.hpp"
#include "fsim/support/path.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
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

void test_design_unit_scheduling_declarations(
    const std::filesystem::path& directory)
{
    const auto scheduling = directory / "design-unit-scheduling";
    std::filesystem::create_directories(scheduling);
    const auto source = scheduling / "scheduling.sv";
    write_text(
        source,
        R"(module active_owner;
  logic marker;
  initial marker = 1'b1;
endmodule

interface active_interface;
  logic marker;
  initial marker = 1'b1;
endinterface

program reactive_owner;
  logic marker;
  initial marker = 1'b1;
endprogram
)"
    );

    fsim::project::Config config;
    config.base_directory = scheduling;
    config.project.name = "sv-2023-design-unit-scheduling";
    config.project.top = "sv:work.active_owner";
    config.source_sets = {identity_source_set(
        fsim::project::Language::system_verilog, "2023", source)};

    fsim::diagnostic::Engine check_diagnostics;
    const auto checked = fsim::app::check_project(config, check_diagnostics);
    assert(checked && !check_diagnostics.has_error());
    assert(checked->parsed.units.size() == 3);
    const auto& module = checked->parsed.units[0];
    const auto& interface_unit = checked->parsed.units[1];
    const auto& program = checked->parsed.units[2];
    assert(module.kind == fsim::frontend::UnitKind::VerilogModule);
    assert(interface_unit.kind
        == fsim::frontend::UnitKind::SystemVerilogInterface);
    assert(program.kind
        == fsim::frontend::UnitKind::SystemVerilogProgram);
    assert(module.systemverilog_scheduling_declaration);
    assert(interface_unit.systemverilog_scheduling_declaration);
    assert(program.systemverilog_scheduling_declaration);
    assert(module.systemverilog_scheduling_declaration->process_region
        == fsim::frontend::SystemVerilogProcessRegion::Active);
    assert(interface_unit.systemverilog_scheduling_declaration->process_region
        == fsim::frontend::SystemVerilogProcessRegion::Active);
    assert(program.systemverilog_scheduling_declaration->process_region
        == fsim::frontend::SystemVerilogProcessRegion::Reactive);
    assert(!module.systemverilog_scheduling_declaration->prototype);
    assert(!interface_unit.systemverilog_scheduling_declaration->prototype);
    assert(!program.systemverilog_scheduling_declaration->prototype);

    fsim::diagnostic::Engine active_diagnostics;
    const auto active = fsim::app::build_project(config, active_diagnostics);
    assert(active && !active_diagnostics.has_error());
    assert(!active->design.processes().empty());
    assert(std::ranges::none_of(
        active->design.processes(), &fsim::runtime::simir::Process::reactive));

    config.project.top = "sv:work.reactive_owner";
    fsim::diagnostic::Engine reactive_diagnostics;
    const auto reactive = fsim::app::build_project(config, reactive_diagnostics);
    assert(reactive && !reactive_diagnostics.has_error());
    assert(!reactive->design.processes().empty());
    assert(std::ranges::all_of(
        reactive->design.processes(), &fsim::runtime::simir::Process::reactive));

    assert(fsim::runtime::process_execution_phase(false, false, false)
        == fsim::runtime::SchedulerPhase::active);
    assert(fsim::runtime::process_execution_phase(false, true, false)
        == fsim::runtime::SchedulerPhase::reactive);
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

std::uint64_t run_assignment_result(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine)
{
    fsim::diagnostic::Engine diagnostics;
    auto project = fsim::app::build_project(config, diagnostics);
    if (!project) {
        std::cerr << "project: " << config.project.name << '\n';
        for (const auto& diagnostic : diagnostics.diagnostics()) {
            std::cerr << diagnostic.code << ": "
                      << diagnostic.message << '\n';
        }
    }
    assert(project && !diagnostics.has_error());
    const auto result = project->design.find_signal(
        "assignment_revision.result");
    assert(result);
    fsim::app::Simulation simulation {
        std::move(*project), config.run.max_deltas, engine };
    const auto run = simulation.run();
    assert(run.status == fsim::runtime::RunStatus::completed);
    const auto value = simulation.read_signal(*result).low_word();
    assert(value.bval == 0);
    return value.aval;
}

void test_assignment_revision_rules(
    const std::filesystem::path& directory)
{
    const auto assignments = directory / "assignment-revision";
    std::filesystem::create_directories(assignments);
    const auto modern_source = assignments / "modern.sv";
    write_text(
        modern_source,
        R"(module assignment_revision(output logic [31:0] result);
  typedef struct packed {
    logic [7:0] left;
    logic [7:0] right;
  } pair_t;
  pair_t pair;
  logic source;
  logic driven;
  initial begin
    pair = pair_t'('{8'h12, 8'h34});
    source = 1'b0;
    assign driven = source;
    #1 source = 1'b1;
    #1 result = {pair, 15'b0, driven};
    deassign driven;
  end
endmodule
)"
    );
    fsim::project::Config modern;
    modern.base_directory = assignments;
    modern.project.name = "sv-2023-assignment-revision";
    modern.project.top = "sv:work.assignment_revision";
    modern.run.max_deltas = 1000;
    modern.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog,
        "2023", modern_source) };
    const auto interpreted = run_assignment_result(
        modern, fsim::app::SimulationEngine::interpreter);
    assert(interpreted == 0x12340001U);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        modern.build.optimization = optimization;
        modern.build.cache_path = assignments
            / (optimization == fsim::project::Optimization::o0
                    ? "modern-cache-o0" : "modern-cache-o2");
        assert(run_assignment_result(
                   modern, fsim::app::SimulationEngine::compiled)
            == interpreted);
    }

    fsim::project::Config legacy_cast = modern;
    legacy_cast.project.name = "sv-2017-static-cast-context";
    legacy_cast.source_sets.front().standard = "2017";
    legacy_cast.build.cache_path.clear();
    fsim::diagnostic::Engine cast_diagnostics;
    assert(!fsim::app::build_project(legacy_cast, cast_diagnostics));
    assert(has_diagnostic(cast_diagnostics, "FSIM-ELAB-SVCAST-005"));

    const auto dynamic_source = assignments / "dynamic-nba.sv";
    write_text(
        dynamic_source,
        R"(module assignment_revision(output logic [31:0] result);
  int values[];
  int index;
  int source;
  initial begin
    values = '{1, 2};
    index = 0;
    source = 7;
    values[index] <= source;
    index = 1;
    source = 9;
    result = values[0] * 1000 + values[1] * 100;
    #1 result = result + values[0] * 10 + values[1];
  end
endmodule
)"
    );
    fsim::project::Config legacy_nba;
    legacy_nba.base_directory = assignments;
    legacy_nba.project.name = "sv-2017-dynamic-array-nba";
    legacy_nba.project.top = "sv:work.assignment_revision";
    legacy_nba.run.max_deltas = 1000;
    legacy_nba.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog,
        "2017", dynamic_source) };
    const auto legacy_interpreted = run_assignment_result(
        legacy_nba, fsim::app::SimulationEngine::interpreter);
    assert(legacy_interpreted == 1272U);
    legacy_nba.build.optimization = fsim::project::Optimization::o2;
    legacy_nba.build.cache_path = assignments / "legacy-cache-o2";
    assert(run_assignment_result(
               legacy_nba, fsim::app::SimulationEngine::compiled)
        == legacy_interpreted);

    fsim::project::Config rejected_nba = legacy_nba;
    rejected_nba.project.name = "sv-2023-dynamic-array-nba";
    rejected_nba.source_sets.front().standard = "2023";
    rejected_nba.build.cache_path.clear();
    fsim::diagnostic::Engine nba_diagnostics;
    assert(!fsim::app::build_project(rejected_nba, nba_diagnostics));
    assert(has_diagnostic(nba_diagnostics, "FSIM-ELAB-SVASSIGN-001"));
}

void test_streaming_and_aggregate_assignment_rules(
    const std::filesystem::path& directory)
{
    const auto assignments = directory / "streaming-aggregate-assignment";
    std::filesystem::create_directories(assignments);
    const auto source = assignments / "streaming-aggregate.sv";
    write_text(
        source,
        R"(module assignment_revision(output logic [31:0] result);
  logic [7:0] left;
  logic [7:0] right;
  logic [7:0] upper;
  logic [7:0] lower;
  logic [7:0] wide_high;
  logic [7:0] wide_low;
  initial begin
    {<<8{left, right}} = 16'h1234;
    '{upper, lower} = 16'habcd;
    {>>{wide_high, wide_low}} = 24'h56789a;
    result = {left, right, upper, lower};
    if ({wide_high, wide_low} != 16'h5678)
      result = 32'b0;
  end
endmodule
)");
    fsim::project::Config config;
    config.base_directory = assignments;
    config.project.name = "sv-2023-streaming-aggregate-assignment";
    config.project.top = "sv:work.assignment_revision";
    config.run.max_deltas = 1000;
    config.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog, "2023", source) };
    const auto interpreted = run_assignment_result(
        config, fsim::app::SimulationEngine::interpreter);
    assert(interpreted == 0x3412abcdU);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        config.build.optimization = optimization;
        config.build.cache_path = assignments
            / (optimization == fsim::project::Optimization::o0
                    ? "cache-o0" : "cache-o2");
        assert(run_assignment_result(
                   config, fsim::app::SimulationEngine::compiled)
            == interpreted);
    }

    const auto insufficient = assignments / "insufficient.sv";
    write_text(
        insufficient,
        "module assignment_revision(output logic [31:0] result); "
        "logic [7:0] a, b; initial {>>{a, b}} = 8'h12; endmodule\n");
    config.project.name = "sv-2023-streaming-source-too-small";
    config.source_sets.front().files = { insufficient };
    config.build.cache_path.clear();
    fsim::diagnostic::Engine insufficient_diagnostics;
    assert(!fsim::app::build_project(config, insufficient_diagnostics));
    assert(has_diagnostic(
        insufficient_diagnostics, "FSIM-ELAB-SVSTREAM-002"));

    const auto keyed = assignments / "keyed-target.sv";
    write_text(
        keyed,
        "module assignment_revision(output logic [31:0] result); "
        "logic [7:0] a; initial '{default: a} = 8'h12; endmodule\n");
    config.project.name = "sv-2023-keyed-assignment-pattern-target";
    config.source_sets.front().files = { keyed };
    fsim::diagnostic::Engine keyed_diagnostics;
    assert(!fsim::app::build_project(config, keyed_diagnostics));
    assert(has_diagnostic(keyed_diagnostics, "FSIM-ELAB-SVASSIGN-002"));
}

void test_operator_and_expression_revision_rules(
    const std::filesystem::path& directory)
{
    const auto expressions = directory / "operator-expression-revision";
    std::filesystem::create_directories(expressions);
    const auto source = expressions / "tolerance.sv";
    write_text(
        source,
        R"(module assignment_revision(output logic [31:0] result);
  initial begin
    result = 0;
    if (107 inside {[100 +/- 7]}) result |= 1;
    if (!(108 inside {[100 +/- 7]})) result |= 2;
    if (75 inside {[100 +%- 25]}) result |= 4;
    if (!(74 inside {[100 +%- 25]})) result |= 8;
    if (-8 inside {[-7 +%- 25]}) result |= 16;
    if (!(-9 inside {[-7 +%- 25]})) result |= 32;
  end
endmodule
)");
    fsim::project::Config config;
    config.base_directory = expressions;
    config.project.name = "sv-2023-inside-tolerance";
    config.project.top = "sv:work.assignment_revision";
    config.run.max_deltas = 1000;
    config.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog, "2023", source) };
    const auto interpreted = run_assignment_result(
        config, fsim::app::SimulationEngine::interpreter);
    assert(interpreted == 63U);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        config.build.optimization = optimization;
        config.build.cache_path = expressions
            / (optimization == fsim::project::Optimization::o0
                    ? "cache-o0" : "cache-o2");
        assert(run_assignment_result(
                   config, fsim::app::SimulationEngine::compiled)
            == interpreted);
    }

    config.project.name = "sv-2017-inside-tolerance-rejection";
    config.source_sets.front().standard = "2017";
    config.build.cache_path.clear();
    fsim::diagnostic::Engine diagnostics;
    assert(!fsim::app::build_project(config, diagnostics));
    assert(has_diagnostic(diagnostics, "FSIM-SV-PARSE-347"));
}

void test_procedural_statement_revision_rules(
    const std::filesystem::path& directory)
{
    const auto statements = directory / "procedural-statement-revision";
    std::filesystem::create_directories(statements);
    const auto source = statements / "string-foreach.sv";
    write_text(
        source,
        R"(module assignment_revision(output logic [31:0] result);
  string text;
  initial begin
    text = "abcd";
    result = text.len() * 100;
    foreach (text[index]) begin
      if (index == 1) continue;
      if (index == 3) break;
      result = result + text[index];
    end
  end
endmodule
)");
    fsim::project::Config config;
    config.base_directory = statements;
    config.project.name = "sv-2023-string-foreach";
    config.project.top = "sv:work.assignment_revision";
    config.run.max_deltas = 1000;
    config.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog, "2023", source) };
    const auto interpreted = run_assignment_result(
        config, fsim::app::SimulationEngine::interpreter);
    assert(interpreted == 596U);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        config.build.optimization = optimization;
        config.build.cache_path = statements
            / (optimization == fsim::project::Optimization::o0
                    ? "cache-o0" : "cache-o2");
        assert(run_assignment_result(
                   config, fsim::app::SimulationEngine::compiled)
            == interpreted);
    }

    config.project.name = "sv-2017-string-foreach-rejection";
    config.source_sets.front().standard = "2017";
    config.build.cache_path.clear();
    fsim::diagnostic::Engine legacy_diagnostics;
    assert(!fsim::app::build_project(config, legacy_diagnostics));
    assert(has_diagnostic(
        legacy_diagnostics, "FSIM-ELAB-SVFOREACH-002"));

    const auto multiple = statements / "multiple-indices.sv";
    write_text(
        multiple,
        "module assignment_revision(output logic [31:0] result); "
        "string text; initial foreach (text[i,j]) result = i; endmodule\n");
    config.project.name = "sv-2023-string-foreach-multiple-indices";
    config.source_sets.front().standard = "2023";
    config.source_sets.front().files = { multiple };
    fsim::diagnostic::Engine multiple_diagnostics;
    assert(!fsim::app::build_project(config, multiple_diagnostics));
    assert(has_diagnostic(
        multiple_diagnostics, "FSIM-ELAB-SVFOREACH-001"));

    const auto write_index = statements / "write-index.sv";
    write_text(
        write_index,
        "module assignment_revision(output logic [31:0] result); "
        "string text; initial foreach (text[i]) i = 1; endmodule\n");
    config.project.name = "sv-2023-string-foreach-read-only-index";
    config.source_sets.front().files = { write_index };
    fsim::diagnostic::Engine index_diagnostics;
    assert(!fsim::app::build_project(config, index_diagnostics));
    assert(has_diagnostic(
        index_diagnostics, "FSIM-ELAB-SVFOREACH-004"));
}

void test_callable_argument_revision_rules(
    const std::filesystem::path& directory)
{
    const auto callables = directory / "callable-argument-revision";
    std::filesystem::create_directories(callables);
    const auto source = callables / "ref-static.sv";
    write_text(
        source,
        R"(module assignment_revision(output logic [31:0] result);
  int anchor;
  int state;
  function automatic int revise(
      const ref static int observation,
      ref static int destination,
      input int increment);
    destination = destination + increment;
    return observation + destination;
  endfunction
  function automatic int relay(
      const ref static int observation,
      ref static int destination);
    return revise(observation, destination, 2);
  endfunction
  task automatic adjust(
      ref static int destination,
      const ref static int observation);
    destination = destination + observation;
  endtask
  task automatic relay_task(
      ref static int destination,
      const ref static int observation);
    adjust(destination, observation);
  endtask
  initial begin
    anchor = 3;
    state = 10;
    result = relay(anchor, state);
    relay_task(state, anchor);
    result = result * 100 + state;
  end
endmodule
)");
    fsim::project::Config config;
    config.base_directory = callables;
    config.project.name = "sv-2023-ref-static-callables";
    config.project.top = "sv:work.assignment_revision";
    config.run.max_deltas = 1000;
    config.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog, "2023", source) };
    const auto interpreted = run_assignment_result(
        config, fsim::app::SimulationEngine::interpreter);
    assert(interpreted == 1515U);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        config.build.optimization = optimization;
        config.build.cache_path = callables
            / (optimization == fsim::project::Optimization::o0
                    ? "cache-o0" : "cache-o2");
        assert(run_assignment_result(
                   config, fsim::app::SimulationEngine::compiled)
            == interpreted);
    }

    config.project.name = "sv-2017-ref-static-rejection";
    config.source_sets.front().standard = "2017";
    config.build.cache_path.clear();
    fsim::diagnostic::Engine legacy_diagnostics;
    assert(!fsim::app::build_project(config, legacy_diagnostics));
    assert(has_diagnostic(legacy_diagnostics, "FSIM-SV-PARSE-368"));

    const auto automatic_function = callables / "automatic-function.sv";
    write_text(
        automatic_function,
        R"(module assignment_revision(output logic [31:0] result);
  int anchor;
  function automatic int revise(
      const ref static int observation,
      ref static int destination);
    destination = destination + observation;
    return destination;
  endfunction
  function automatic int invalid_caller();
    int transient;
    return revise(anchor, transient);
  endfunction
  initial result = invalid_caller();
endmodule
)");
    config.project.name = "sv-2023-ref-static-automatic-function";
    config.source_sets.front().standard = "2023";
    config.source_sets.front().files = { automatic_function };
    fsim::diagnostic::Engine function_diagnostics;
    assert(!fsim::app::build_project(config, function_diagnostics));
    assert(has_diagnostic(
        function_diagnostics, "FSIM-ELAB-SVFUNC-013"));

    const auto automatic_task = callables / "automatic-task.sv";
    write_text(
        automatic_task,
        R"(module assignment_revision(output logic [31:0] result);
  int anchor;
  task automatic update(ref static int destination);
    destination = destination + anchor;
  endtask
  task automatic invalid_caller();
    int transient;
    update(transient);
  endtask
  initial invalid_caller();
endmodule
)");
    config.project.name = "sv-2023-ref-static-automatic-task";
    config.source_sets.front().files = { automatic_task };
    fsim::diagnostic::Engine task_diagnostics;
    assert(!fsim::app::build_project(config, task_diagnostics));
    assert(has_diagnostic(task_diagnostics, "FSIM-ELAB-SVTASK-015"));
}

struct ArtifactRoundTripCapture {
    fsim::runtime::RunResult result;
    std::string value;
    std::string checkpoint;
    std::string debugger_observation;
    std::vector<std::string> cache_keys;
    fsim::app::NativeCacheStatistics cache;
};

void test_2023_artifact_and_cache_round_trip(
    const std::filesystem::path& directory)
{
    const auto root = directory / "systemverilog-2023-artifact";
    std::filesystem::create_directories(root);
    const auto source = root / "round-trip.sv";
    const auto object = root / "round-trip.fsimobj";
    auto design = root / "round-trip.fsimdesign";
    const auto cache = root / "native-cache";
    const auto trace = root / "round-trip.vcd";
    write_text(
        source,
        R"(interface class RootContract;
  pure virtual function int apply(input int value);
endclass
interface class SideContract;
  pure virtual function int observe(input int value);
endclass
interface class CombinedContract extends RootContract, SideContract;
endclass
class ConcreteContract implements CombinedContract;
  function int apply(input int value);
    return value + 1;
  endfunction
  function int observe(input int value);
    return value - 1;
  endfunction
endclass

module artifact_2023_top;
  timeunit 1ns / 1ns;
  typedef struct packed {
    logic [7:0] left;
    logic [7:0] right;
  } pair_t;
  pair_t pair;
  logic clock;
  logic observed;
  logic [7:0] left;
  logic [7:0] right;
  logic [7:0] upper;
  logic [7:0] lower;
  logic [31:0] result;
  int anchor;
  int state;
  int background;

  clocking cb @(posedge clock);
    input #1 sampled = observed;
  endclocking

  function automatic int revise(
      const ref static int observation,
      ref static int destination);
    destination = destination + 2;
    return observation + destination;
  endfunction

  function automatic bit launch(input int seed);
    fork
      begin
        #1 background = seed;
      end
    join_none
    return 1'b1;
  endfunction

  initial begin
    string text;
    int byte_sum;
    int score;
    bit launched;
    clock = 1'b0;
    observed = 1'b1;
    anchor = 3;
    state = 10;
    background = 0;
    pair = pair_t'('{8'h12, 8'h34});
    {<<8{left, right}} = 16'h1234;
    '{upper, lower} = 16'habcd;
    text = "abcd";
    byte_sum = 0;
    foreach (text[index]) begin
      if (index == 1) continue;
      if (index == 3) break;
      byte_sum = byte_sum + text[index];
    end
    score = revise(anchor, state);
    launched = launch(7);
    #2;
    if (!launched || pair.left != 8'h12 || pair.right != 8'h34
        || !(107 inside {[100 +/- 7]}) || byte_sum != 196
        || background != 7 || state != 12)
      result = 0;
    else
      result = {left, right, upper, lower}
          ^ (score + byte_sum + background);
    $finish;
  end
endmodule
)");

    fsim::project::Config compile_config;
    compile_config.manifest_path = "<systemverilog-2023-artifact>";
    compile_config.base_directory = root;
    compile_config.project.name = "systemverilog-2023-artifact-compile";
    compile_config.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog, "2023", source) };
    fsim::diagnostic::Engine compile_diagnostics;
    const auto compiled = fsim::app::compile_artifact(
        compile_config, object, compile_diagnostics);
    if (!compiled) {
        fsim::diagnostic::print_text(std::cerr, compile_diagnostics);
    }
    assert(compiled && !compile_diagnostics.has_error());

    fsim::diagnostic::Engine object_diagnostics;
    const auto object_metadata = fsim::artifact::load_object_metadata(
        object, object_diagnostics);
    assert(
        object_metadata && !object_diagnostics.has_error()
        && object_metadata->format == fsim::artifact::kObjectFormatVersion
        && object_metadata->portable_schema
            == fsim::library::kPortableSchemaVersion
        && object_metadata->standard == "2023"
        && std::ranges::all_of(
            object_metadata->units, [](const auto& unit) {
                return unit.standard == "2023";
            }));
    const std::array objects { object };
    fsim::diagnostic::Engine semantic_diagnostics;
    const auto checked = fsim::app::load_objects(objects, semantic_diagnostics);
    assert(
        checked && !semantic_diagnostics.has_error()
        && std::ranges::all_of(
            checked->parsed.units, [](const auto& unit) {
                return unit.standard_revision
                    == fsim::frontend::StandardRevision::SystemVerilog2023;
            })
        && checked->parsed.systemverilog_classes.size() == 4);

    fsim::project::Config elaborate_config;
    elaborate_config.manifest_path = "<systemverilog-2023-artifact>";
    elaborate_config.base_directory = root;
    elaborate_config.project.name = "systemverilog-2023-artifact-elaborate";
    elaborate_config.project.tops.push_back(
        { "sv:work.artifact_2023_top", "dut" });
    elaborate_config.project.time_resolution = "1ns";
    elaborate_config.build.optimization = fsim::project::Optimization::o2;
    elaborate_config.build.cache_path = cache;
    elaborate_config.run.max_deltas = 1000;
    fsim::diagnostic::Engine elaborate_diagnostics;
    const auto elaborated = fsim::app::elaborate_artifact(
        elaborate_config, objects, design, elaborate_diagnostics);
    if (!elaborated) {
        fsim::diagnostic::print_text(std::cerr, elaborate_diagnostics);
    }
    assert(elaborated && !elaborate_diagnostics.has_error());

    fsim::diagnostic::Engine design_diagnostics;
    const auto design_metadata = fsim::artifact::load_design_metadata(
        design, design_diagnostics);
    assert(
        design_metadata && !design_diagnostics.has_error()
        && design_metadata->format == fsim::artifact::kDesignFormatVersion
        && design_metadata->objects.size() == 1
        && design_metadata->objects.front().standard == "2023"
        && design_metadata->verilog_unit_provenance.size() == 1
        && design_metadata->verilog_unit_provenance.front().standard
            == "2023");

    auto stale_object = fsim::artifact::serialize_object_metadata(
        *object_metadata);
    const auto stale_portable_schema =
        fsim::library::kPortableSchemaVersion - 1U;
    for (std::size_t byte = 0; byte < 4; ++byte) {
        stale_object[12 + byte] = static_cast<char>(
            (stale_portable_schema >> (byte * 8U)) & 0xffU);
    }
    fsim::diagnostic::Engine stale_object_diagnostics;
    assert(!fsim::artifact::deserialize_object_metadata(
        stale_object,
        "stale-systemverilog-2023-object", stale_object_diagnostics));
    assert(has_diagnostic(stale_object_diagnostics, "FSIM-ART-0001"));
    auto stale_design = fsim::artifact::serialize_design_metadata(
        *design_metadata);
    stale_design[8] = static_cast<char>(
        fsim::artifact::kDesignFormatVersion - 1U);
    fsim::diagnostic::Engine stale_design_diagnostics;
    assert(!fsim::artifact::deserialize_design_metadata(
        stale_design,
        "stale-systemverilog-2023-design", stale_design_diagnostics));
    assert(has_diagnostic(stale_design_diagnostics, "FSIM-ART-0010"));

    std::filesystem::rename(source, root / "round-trip.sv.hidden");
    std::filesystem::rename(object, root / "round-trip.fsimobj.hidden");
    const auto run_artifact = [&](const fsim::app::SimulationEngine engine) {
        fsim::diagnostic::Engine diagnostics;
        auto loaded = fsim::app::load_design_artifact(design, diagnostics);
        if (!loaded) {
            fsim::diagnostic::print_text(std::cerr, diagnostics);
        }
        assert(loaded && !diagnostics.has_error());
        const auto provenance = fsim::app::verilog_scope_provenance(*loaded);
        assert(
            provenance.size() == 1
            && provenance.front().standard == "systemverilog-2023"
            && std::ranges::any_of(
                loaded->systemverilog_class_specializations,
                [](const auto& specialization) {
                    return specialization.declaration_identity.ends_with(
                        "::ConcreteContract");
                }));
        const auto result_signal = loaded->design.find_signal("dut.result");
        assert(result_signal);
        ArtifactRoundTripCapture capture;
        capture.cache_keys = loaded->specialization_cache_keys;
        loaded->cache_path = cache;
        fsim::app::Simulation simulation {
            std::move(*loaded), elaborate_config.run.max_deltas, engine };
        capture.cache = simulation.native_cache_statistics();
        capture.result = simulation.run();
        capture.value = simulation.read_signal(*result_signal).to_msb_string();
        const auto checkpoint = simulation.capture_uvm_checkpoint();
        const auto checkpoint_bytes = checkpoint
            ? fsim::app::serialize_systemverilog_uvm_state(
                checkpoint.artifact, diagnostics)
            : std::nullopt;
        const auto restored_checkpoint = checkpoint_bytes
            ? fsim::app::deserialize_systemverilog_uvm_state(
                *checkpoint_bytes, "systemverilog-2023-checkpoint",
                diagnostics)
            : std::nullopt;
        assert(checkpoint && checkpoint_bytes && restored_checkpoint
            && *restored_checkpoint == checkpoint.artifact
            && !diagnostics.has_error());
        capture.checkpoint = *checkpoint_bytes;
        if (engine == fsim::app::SimulationEngine::debug) {
            std::ostringstream output;
            std::ostringstream error;
            fsim::app::DebuggerControl debugger {
                simulation, output, error
            };
            debugger.execute({ "show", "dut.result" });
            assert(error.str().empty());
            capture.debugger_observation = output.str();
        }
        return capture;
    };
    const auto interpreted = run_artifact(
        fsim::app::SimulationEngine::interpreter);
    const auto cold = run_artifact(fsim::app::SimulationEngine::compiled);
    const auto warm = run_artifact(fsim::app::SimulationEngine::compiled);
    const auto debug = run_artifact(fsim::app::SimulationEngine::debug);
    assert(
        interpreted.result.status == fsim::runtime::RunStatus::stopped
        && interpreted.result.time == 2
        && interpreted.value == "00110100000100101010101100010111"
        && cold.result.status == interpreted.result.status
        && warm.result.status == interpreted.result.status
        && debug.result.status == interpreted.result.status
        && cold.result.time == interpreted.result.time
        && warm.result.time == interpreted.result.time
        && debug.result.time == interpreted.result.time
        && cold.value == interpreted.value && warm.value == interpreted.value
        && debug.value == interpreted.value
        && cold.checkpoint == interpreted.checkpoint
        && warm.checkpoint == interpreted.checkpoint
        && debug.checkpoint == interpreted.checkpoint
        && cold.cache_keys == interpreted.cache_keys
        && warm.cache_keys == interpreted.cache_keys
        && debug.cache_keys == interpreted.cache_keys
        && debug.debugger_observation.find(interpreted.value)
            != std::string::npos);
#if defined(FSIM_HAS_LLVM)
    assert(cold.cache.misses != 0 && cold.cache.stores == cold.cache.misses);
    assert(warm.cache.hits == cold.cache.misses && warm.cache.misses == 0);
#endif
    const auto design_text = fsim::support::path_to_utf8(design);
    const auto trace_path = fsim::support::path_to_utf8(trace);
    const std::vector<const char*> trace_arguments {
        "fsim", "simulate", "--design", design_text.c_str(), "--engine",
        "compiled", "--trace", trace_path.c_str(), "--trace-filter",
        "dut.*"
    };
    auto services = fsim::app::make_cli_services();
    std::ostringstream trace_output;
    std::ostringstream trace_error;
    assert(fsim::cli::run(
               static_cast<int>(trace_arguments.size()),
               trace_arguments.data(), services, trace_output, trace_error)
            == 0
        && trace_error.str().empty());
    const auto trace_text = read_text(trace);
    assert(
        trace_text.find("dut") != std::string::npos
        && trace_text.find("result") != std::string::npos
        && trace_text.find("standard=systemverilog-2023")
            != std::string::npos
        && trace_text.find(interpreted.value) != std::string::npos);

    const auto relocated_root = root / "relocated";
    std::filesystem::create_directories(relocated_root);
    const auto relocated = relocated_root / design.filename();
    std::filesystem::create_directories(relocated);
    for (const auto& entry :
        std::filesystem::recursive_directory_iterator(design)) {
        const auto target = relocated
            / entry.path().lexically_relative(design);
        if (entry.is_directory()) {
            std::filesystem::create_directories(target);
        } else if (entry.is_regular_file()) {
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(entry.path(), target);
        }
    }
    design = relocated;
    const auto relocated_capture = run_artifact(
        fsim::app::SimulationEngine::compiled);
    assert(
        relocated_capture.value == interpreted.value
        && relocated_capture.cache_keys == interpreted.cache_keys);
#if defined(FSIM_HAS_LLVM)
    assert(relocated_capture.cache.hits == cold.cache.misses);
#endif
}

void test_constant_function_severity_messages(
    const std::filesystem::path& root)
{
    const auto source = root / "constant-function-severity.sv";
    write_text(source, R"(
module constant_function_severity;
  function automatic int announce(input int value);
    $info("selected=%0d", value);
    $warning("checked");
    return value;
  endfunction
  localparam int SELECTED = announce(7);
endmodule
)" );

    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "constant-function-severity";
    config.project.top = "constant_function_severity";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine diagnostics;
    const auto project = fsim::app::build_project(config, diagnostics);
    assert(project && !diagnostics.has_error());
    const auto& entries = diagnostics.diagnostics();
    assert(std::ranges::count_if(entries, [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-SVCONST-002"
            && diagnostic.severity == fsim::diagnostic::Severity::note
            && diagnostic.message.find("selected=7") != std::string::npos;
    }) == 1);
    assert(std::ranges::count_if(entries, [](const auto& diagnostic) {
        return diagnostic.code == "FSIM-ELAB-SVCONST-002"
            && diagnostic.severity == fsim::diagnostic::Severity::warning
            && diagnostic.message.find("checked") != std::string::npos;
    }) == 1);
}

void test_program_elaboration_severity_messages(
    const std::filesystem::path& root)
{
    const auto source = root / "program-elaboration-severity.sv";
    write_text(source, R"(
program program_elaboration_severity;
  localparam int VALUE = 9;
  $warning("direct=%0d", VALUE);
  if (VALUE == 9) $info("selected=%0d", VALUE);
  else $error("unselected");
endprogram
)" );

    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "program-elaboration-severity";
    config.project.top = "program_elaboration_severity";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files = { source };
    config.source_sets.push_back(std::move(sources));

    const auto require_messages = [&](const std::string_view phase) {
        fsim::diagnostic::Engine diagnostics;
        const auto project = fsim::app::build_project(config, diagnostics);
        assert(project && !diagnostics.has_error());
        assert(project->design.processes().empty());
        const auto& entries = diagnostics.diagnostics();
        assert(std::ranges::count_if(entries, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVPROGRAM-002"
                && diagnostic.severity == fsim::diagnostic::Severity::warning
                && diagnostic.message.find("direct=9") != std::string::npos;
        }) == 1);
        assert(std::ranges::count_if(entries, [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVPROGRAM-002"
                && diagnostic.severity == fsim::diagnostic::Severity::note
                && diagnostic.message.find("selected=9") != std::string::npos;
        }) == 1);
        assert(std::ranges::none_of(entries, [](const auto& diagnostic) {
            return diagnostic.message.find("unselected")
                != std::string::npos;
        }));
        (void)phase;
    };
    require_messages("cold");
    require_messages("warm");

    const auto failing_source = root / "program-elaboration-error.sv";
    write_text(failing_source, R"(
program program_elaboration_error;
  if (1) $error("selected failure");
endprogram
)" );
    config.project.name = "program-elaboration-error";
    config.project.top = "program_elaboration_error";
    config.source_sets.front().files = { failing_source };
    fsim::diagnostic::Engine failing_diagnostics;
    assert(!fsim::app::build_project(config, failing_diagnostics));
    assert(std::ranges::any_of(
        failing_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-ELAB-SVPROGRAM-002"
                && diagnostic.severity == fsim::diagnostic::Severity::error
                && diagnostic.message.find("selected failure")
                    != std::string::npos;
        }));
}

void test_systemverilog_2023_annex_diagnostics(
    const std::filesystem::path& root)
{
    const auto candidates = root / "annex-deprecation-candidates.sv";
    write_text(candidates, R"(
module annex_leaf #(parameter int VALUE = 1) (output logic result);
  assign result = VALUE;
endmodule
module annex_deprecation_candidates(output logic result);
  annex_leaf leaf(result);
  defparam leaf.VALUE = 2;
  initial begin
    assign result = 1'b1;
    deassign result;
  end
endmodule
)" );

    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "systemverilog-2023-annex-candidates";
    config.project.top = "annex_deprecation_candidates";
    fsim::project::SourceSet sources;
    sources.language = fsim::project::Language::system_verilog;
    sources.standard = "2023";
    sources.library = "work";
    sources.files = { candidates };
    config.source_sets.push_back(std::move(sources));

    fsim::diagnostic::Engine candidate_diagnostics;
    assert(fsim::app::check_project(config, candidate_diagnostics));
    assert(!candidate_diagnostics.has_error());
    assert(std::ranges::count_if(
        candidate_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-DEPR-002"
                && diagnostic.severity
                    == fsim::diagnostic::Severity::warning
                && diagnostic.span.begin.line != 0U
                && diagnostic.span.begin.column != 0U;
        }) == 3);

    config.project.name = "systemverilog-2017-annex-candidates";
    config.source_sets.front().standard = "2017";
    fsim::diagnostic::Engine retained_diagnostics;
    assert(fsim::app::check_project(config, retained_diagnostics));
    assert(std::ranges::none_of(
        retained_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code.starts_with("FSIM-SV-DEPR-");
        }));

    const auto removed = root / "annex-removed-sampled-clock.sv";
    write_text(removed, R"(
module annex_removed_sampled_clock(input logic clock, value);
  logic observed;
  initial observed = $sampled(value, @(posedge clock));
endmodule
)" );
    config.project.name = "systemverilog-2023-annex-removed";
    config.source_sets.front().standard = "2023";
    config.source_sets.front().files = { removed };
    fsim::diagnostic::Engine removed_diagnostics;
    assert(!fsim::app::check_project(config, removed_diagnostics));
    assert(std::ranges::any_of(
        removed_diagnostics.diagnostics(), [](const auto& diagnostic) {
            return diagnostic.code == "FSIM-SV-DEPR-001"
                && diagnostic.severity == fsim::diagnostic::Severity::error
                && diagnostic.message.find("$sampled")
                    != std::string::npos;
        }));
}

void test_package_lookup_revision_rules(
    const std::filesystem::path& directory)
{
    const auto root = directory / "package-lookup-revision";
    std::filesystem::create_directories(root);
    const auto source = root / "package-lookup.sv";
    write_text(source, R"(
package base_lookup;
  parameter int VALUE = 9;
  typedef logic [7:0] word_t;
  function automatic int bump(input int value);
    return value + 3;
  endfunction
  let adjust(value) = value + 2;
  interface class observer;
    pure virtual function int sample();
  endclass
endpackage

package selected_lookup;
  import base_lookup::*;
  export base_lookup::VALUE, base_lookup::word_t, base_lookup::bump,
         base_lookup::adjust, base_lookup::observer;
endpackage

package public_lookup;
  import base_lookup::*;
  import selected_lookup::*;
  export base_lookup::*;
  export selected_lookup::*;
endpackage

module assignment_revision
    import public_lookup::VALUE, public_lookup::word_t,
           public_lookup::bump, public_lookup::adjust,
           public_lookup::observer;
    #(parameter int INITIAL = VALUE)
    (output logic [31:0] result);
  word_t retained_type;
  observer retained_contract;
  initial begin
    retained_type = INITIAL;
    result = bump(adjust(retained_type));
  end
endmodule
)");

    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "sv-2023-package-lookup-revision";
    config.project.top = "sv:work.assignment_revision";
    config.run.max_deltas = 1000;
    config.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog, "2023", source) };

    const auto interpreted = run_assignment_result(
        config, fsim::app::SimulationEngine::interpreter);
    assert(interpreted == 14U);
    for (const auto optimization : {
             fsim::project::Optimization::o0,
             fsim::project::Optimization::o2 }) {
        config.build.optimization = optimization;
        config.build.cache_path = root
            / (optimization == fsim::project::Optimization::o0
                    ? "cache-o0" : "cache-o2");
        const auto cold = run_assignment_result(
            config, fsim::app::SimulationEngine::compiled);
        const auto warm = run_assignment_result(
            config, fsim::app::SimulationEngine::compiled);
        assert(cold == interpreted);
        assert(warm == interpreted);
    }

    const auto shadow_source = root / "package-wildcard-shadow.sv";
    write_text(shadow_source, R"(
package first_lookup;
  parameter int VALUE = 1;
endpackage
package second_lookup;
  parameter int VALUE = 2;
endpackage
module assignment_revision import first_lookup::*, second_lookup::*;
    (output logic [31:0] result);
  localparam int VALUE = 7;
  initial result = VALUE;
endmodule
)");
    config.project.name = "sv-2023-package-wildcard-shadow";
    config.source_sets.front().files = { shadow_source };
    config.build.cache_path.clear();
    assert(run_assignment_result(
               config, fsim::app::SimulationEngine::interpreter)
        == 7U);

    const auto unused_source = root / "package-wildcard-unused.sv";
    write_text(unused_source, R"(
package first_lookup;
  parameter int VALUE = 1;
endpackage
package second_lookup;
  parameter int VALUE = 2;
endpackage
module assignment_revision import first_lookup::*, second_lookup::*;
    (output logic [31:0] result);
  initial result = 5;
endmodule
)");
    config.project.name = "sv-2023-package-wildcard-unused";
    config.source_sets.front().files = { unused_source };
    assert(run_assignment_result(
               config, fsim::app::SimulationEngine::interpreter)
        == 5U);

    const auto conflict_source = root / "package-explicit-conflict.sv";
    write_text(conflict_source, R"(
package conflict_lookup;
  parameter int VALUE = 1;
endpackage
module assignment_revision import conflict_lookup::VALUE;
    (output logic [31:0] result);
  localparam int VALUE = 7;
  initial result = VALUE;
endmodule
)");
    config.project.name = "sv-2023-package-explicit-conflict";
    config.source_sets.front().files = { conflict_source };
    fsim::diagnostic::Engine conflict_diagnostics;
    assert(!fsim::app::build_project(config, conflict_diagnostics));
    assert(has_diagnostic(
        conflict_diagnostics, "FSIM-ELAB-SVPKG-009"));
}

void test_standard_package_revision_rules(
    const std::filesystem::path& directory)
{
    using MemberKind
        = fsim::frontend::SystemVerilogStandardPackageMemberKind;
    const auto package_2017 = fsim::frontend::systemverilog_standard_package(
        fsim::frontend::StandardRevision::SystemVerilog2017);
    const auto package_2023 = fsim::frontend::systemverilog_standard_package(
        fsim::frontend::StandardRevision::SystemVerilog2023);
    assert(package_2017 && package_2023);
    assert(package_2017->declarations.size() == 4U);
    assert(package_2023->declarations.size() == 5U);
    assert(package_2017->revision == "ieee-1800-2017:std:fsim-v3");
    assert(package_2023->revision == "ieee-1800-2023:std:fsim-v3");
    assert(package_2017->declaration_identity
        != package_2023->declaration_identity);
    const auto* process
        = fsim::frontend::find_systemverilog_standard_package_declaration(
            fsim::frontend::StandardRevision::SystemVerilog2023,
            "process");
    const auto* weak
        = fsim::frontend::find_systemverilog_standard_package_declaration(
            fsim::frontend::StandardRevision::SystemVerilog2023,
            "weak_reference");
    assert(process && process->kind == MemberKind::class_type
        && process->abstract_class && process->final_in_2023);
    assert(weak && weak->kind == MemberKind::class_type
        && weak->parameterized);
    assert(fsim::frontend::find_systemverilog_standard_package_declaration(
               fsim::frontend::StandardRevision::SystemVerilog2017,
               "weak_reference") == nullptr);
    assert(!fsim::frontend::systemverilog_standard_package(
        fsim::frontend::StandardRevision::Verilog2005));

    const auto root = directory / "standard-package-revision";
    std::filesystem::create_directories(root);
    const auto source = root / "standard-package.sv";
    write_text(source, R"(
module assignment_revision(output logic [31:0] result);
  std::mailbox #(int) qualified_box;
  mailbox #(int) imported_box;
  std::semaphore qualified_gate;
  std::process current;
  weak_reference #(process) weak_handle;
  initial begin
    current = std::process::self();
    result = current != null;
  end
endmodule
)");
    fsim::project::Config config;
    config.base_directory = root;
    config.project.name = "sv-2023-standard-package";
    config.project.top = "sv:work.assignment_revision";
    config.run.max_deltas = 1000;
    config.source_sets = { identity_source_set(
        fsim::project::Language::system_verilog, "2023", source) };
    fsim::diagnostic::Engine diagnostics;
    const auto checked = fsim::app::check_project(config, diagnostics);
    if (!checked || diagnostics.has_error()) {
        fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(checked && !diagnostics.has_error());
    assert(checked->parsed.units.size() == 1U);
    const auto& provenance
        = checked->parsed.units.front().systemverilog_standard_package;
    assert(provenance);
    assert(provenance->revision == package_2023->revision);
    assert(provenance->declaration_identity
        == package_2023->declaration_identity);
    assert(run_assignment_result(
               config, fsim::app::SimulationEngine::interpreter)
        == 1U);

    config.source_sets.front().standard = "2017";
    config.project.name = "sv-2017-standard-package-isolation";
    fsim::diagnostic::Engine old_diagnostics;
    assert(!fsim::app::check_project(config, old_diagnostics));
    assert(has_diagnostic(old_diagnostics, "FSIM-SV-PARSE-349"));

    const auto unknown = root / "standard-package-unknown.sv";
    write_text(unknown, R"(
module assignment_revision(output logic [31:0] result);
  initial result = std::missing();
endmodule
)");
    config.source_sets.front().standard = "2023";
    config.source_sets.front().files = { unknown };
    config.project.name = "sv-2023-standard-package-unknown";
    fsim::diagnostic::Engine unknown_diagnostics;
    assert(!fsim::app::build_project(config, unknown_diagnostics));
    assert(has_diagnostic(
        unknown_diagnostics, "FSIM-ELAB-SVPKG-011"));

    const auto redeclared = root / "standard-package-redeclared.sv";
    write_text(redeclared, "package std; endpackage\n");
    config.source_sets.front().files = { redeclared };
    config.project.name = "sv-2023-standard-package-redeclared";
    fsim::diagnostic::Engine redeclared_diagnostics;
    assert(!fsim::app::check_project(config, redeclared_diagnostics));
    assert(has_diagnostic(
        redeclared_diagnostics, "FSIM-SV-SEM-270"));
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
    test_design_unit_scheduling_declarations(directory.path);
    test_predefined_environment_revision_gates(directory.path);
    test_assignment_revision_rules(directory.path);
    test_streaming_and_aggregate_assignment_rules(directory.path);
    test_operator_and_expression_revision_rules(directory.path);
    test_procedural_statement_revision_rules(directory.path);
    test_callable_argument_revision_rules(directory.path);
    test_constant_function_severity_messages(directory.path);
    test_program_elaboration_severity_messages(directory.path);
    test_systemverilog_2023_annex_diagnostics(directory.path);
    test_package_lookup_revision_rules(directory.path);
    test_standard_package_revision_rules(directory.path);
    test_2023_artifact_and_cache_round_trip(directory.path);
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
