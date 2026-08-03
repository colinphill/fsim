// SPDX-License-Identifier: Apache-2.0
#include "application_test_support.hpp"

#include "fsim/systemc/hierarchy.hpp"

#include <algorithm>
#include <cassert>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

namespace fsim::test {

void ApplicationTestFixture::test_preprocessing_debug_and_cli() {
  // FSIM-CONFORMANCE CF-COMMON-DEBUGGER-001 source=SRC-FSIM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-CLI-001 source=SRC-FSIM expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-TRACE-001 source=SRC-COCOTB expectation=execute
  auto config = base_config();
  fsim::diagnostic::Engine diagnostics;
  auto first = fsim::app::build_project(config, diagnostics);
  auto second = fsim::app::build_project(config, diagnostics);
  assert(first);
  assert(second);
// Verilog preprocessing consumes exact transitive snapshots. A header edit
// must invalidate both the analysis object and the owning specialization,
// while interpreter and compiled execution retain identical semantics.
const auto preprocessor_include =
    directory / "preprocessor-include";
std::filesystem::create_directories(preprocessor_include);
const auto preprocessor_header =
    preprocessor_include / "values.svh";
const auto preprocessor_source =
    directory / "preprocessor.sv";
const auto write_preprocessor_header =
    [&](const std::string_view value) {
      std::ofstream output(
          preprocessor_header, std::ios::binary);
      output
          << "`define PREPROCESSED_VALUE " << value << '\n'
          << R"(module preprocessor_app;
logic [3:0] value;
initial begin
  value = `PREPROCESSED_VALUE;
  #1 $finish;
end
endmodule
)";
      assert(output.good());
    };
write_preprocessor_header("4'b1010");
{
  std::ofstream output(
      preprocessor_source, std::ios::binary);
  output << R"(`ifdef ENABLE_PREPROCESSOR_APP
`include "values.svh"
`endif
)";
  assert(output.good());
}
auto preprocessor_config = config;
preprocessor_config.project.name = "preprocessor-test";
preprocessor_config.project.top =
    "sv:work.preprocessor_app";
preprocessor_config.build.cache_path =
    directory / "preprocessor-cache";
preprocessor_config.source_sets.clear();
fsim::project::SourceSet preprocessor_sources;
preprocessor_sources.language =
    fsim::project::Language::system_verilog;
preprocessor_sources.standard = "2017";
preprocessor_sources.library = "work";
preprocessor_sources.files = {preprocessor_source};
preprocessor_sources.include_directories = {
    preprocessor_include};
preprocessor_sources.defines = {
    "ENABLE_PREPROCESSOR_APP=1"};
preprocessor_config.source_sets.push_back(
    std::move(preprocessor_sources));

fsim::diagnostic::Engine preprocessor_check_diagnostics;
const auto preprocessor_checked =
    fsim::app::check_project(
        preprocessor_config,
        preprocessor_check_diagnostics);
assert(preprocessor_checked);
assert(preprocessor_checked->hdl_sources.size() == 1);
assert(
    preprocessor_checked->hdl_sources.front()
        .dependencies.size()
    == 1);
assert(
    preprocessor_checked->hdl_sources.front()
        .dependencies.front().path.filename()
    == "values.svh");

const auto run_preprocessed =
    [&](const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          preprocessor_config, run_diagnostics);
      assert(project);
      assert(
          project->specialization_cache_keys.size() == 1);
      auto key =
          project->specialization_cache_keys.front();
      const bool analysis_hit = project->cache_hit;
      auto capture =
          capture_simulation(std::move(*project), engine);
      return std::tuple{
          std::move(key),
          analysis_hit,
          std::move(capture)};
    };
auto [preprocessor_key_a, preprocessor_miss_a,
      preprocessor_reference_a] =
    run_preprocessed(
        fsim::app::SimulationEngine::interpreter);
auto [preprocessor_key_a_warm, preprocessor_hit_a,
      preprocessor_hybrid_a] =
    run_preprocessed(
        fsim::app::SimulationEngine::compiled);
assert(!preprocessor_miss_a);
assert(preprocessor_hit_a);
assert(preprocessor_key_a == preprocessor_key_a_warm);
compare_captures(
    preprocessor_reference_a, preprocessor_hybrid_a);
assert(
    preprocessor_hybrid_a.final_values
    == std::vector<std::string>{"1010"});

write_preprocessor_header("4'b0101");
auto [preprocessor_key_b, preprocessor_miss_b,
      preprocessor_reference_b] =
    run_preprocessed(
        fsim::app::SimulationEngine::interpreter);
auto [preprocessor_key_b_warm, preprocessor_hit_b,
      preprocessor_hybrid_b] =
    run_preprocessed(
        fsim::app::SimulationEngine::compiled);
assert(!preprocessor_miss_b);
assert(preprocessor_hit_b);
assert(preprocessor_key_b == preprocessor_key_b_warm);
assert(preprocessor_key_b != preprocessor_key_a);
compare_captures(
    preprocessor_reference_b, preprocessor_hybrid_b);
assert(
    preprocessor_hybrid_b.final_values
    == std::vector<std::string>{"0101"});

const auto shared_macro_source =
    directory / "shared-macros.sv";
const auto shared_module_source =
    directory / "shared-module.sv";
const auto write_shared_macro =
    [&](const std::string_view value) {
      std::ofstream output(
          shared_macro_source, std::ios::binary);
      output << "`default_nettype tri0\n"
             << "`celldefine\n"
             << "`define SHARED_RUNTIME_VALUE "
             << value << '\n';
      assert(output.good());
    };
write_shared_macro("1'b0");
{
  std::ofstream output(
      shared_module_source, std::ios::binary);
  output << R"(module shared_preprocessor_app;
typedef logic shared_t;
assign value = `SHARED_RUNTIME_VALUE;
initial #1 $finish;
endmodule
`endcelldefine
`resetall
)";
  assert(output.good());
}
auto shared_preprocessor_config = config;
shared_preprocessor_config.project.name =
    "shared-preprocessor-test";
shared_preprocessor_config.project.top =
    "sv:work.shared_preprocessor_app";
shared_preprocessor_config.build.cache_path =
    directory / "shared-preprocessor-cache";
shared_preprocessor_config.source_sets.clear();
fsim::project::SourceSet shared_preprocessor_sources;
shared_preprocessor_sources.language =
    fsim::project::Language::system_verilog;
shared_preprocessor_sources.standard = "2017";
shared_preprocessor_sources.library = "work";
shared_preprocessor_sources.compilation_unit = "source-set";
shared_preprocessor_sources.files = {
    shared_macro_source, shared_module_source};
shared_preprocessor_config.source_sets.push_back(
    shared_preprocessor_sources);

fsim::diagnostic::Engine shared_check_diagnostics;
auto shared_checked = fsim::app::check_project(
    shared_preprocessor_config, shared_check_diagnostics);
assert(shared_checked);
assert(shared_checked->hdl_sources.size() == 2);
assert(
    !shared_checked->hdl_sources[0]
         .compilation_unit_digest.empty());
assert(
    shared_checked->hdl_sources[0].compilation_unit_digest
    == shared_checked->hdl_sources[1].compilation_unit_digest);
assert(shared_checked->parsed.units.size() == 1);
assert(shared_checked->parsed.units.front().is_cell);
assert(
    shared_checked->parsed.units.front().default_nettype == "tri0");
assert(shared_checked->parsed.units.front().signals.size() == 1);
assert(
    shared_checked->parsed.units.front().signals.front().name == "value");
assert(
    shared_checked->parsed.units.front().signals.front().type.spelling
    == "tri0");
assert(shared_checked->semantics.source_files().size() == 2);
assert(shared_checked->semantics.units().size() == 1);
assert(shared_checked->semantics.units().front().id.value() == 0);
assert(shared_checked->semantics.units().front().scope.value() == 0);
assert(
    shared_checked->semantics.units().front().name
    == "shared_preprocessor_app");
assert(shared_checked->semantics.types().size() == 1);
assert(shared_checked->semantics.types().front().id.value() == 0);
assert(shared_checked->semantics.types().front().name == "shared_t");
assert(shared_checked->semantics.values().size() == 1);
assert(
    shared_checked->semantics.values().front().kind
    == fsim::semantic::ValueKind::signal);
assert(shared_checked->semantics.values().front().name == "value");
const auto semantic_source_count =
    shared_checked->semantics.source_spans().size();
fsim::diagnostic::Engine repeated_semantic_diagnostics;
const auto repeated_semantics = fsim::app::check_project(
    shared_preprocessor_config, repeated_semantic_diagnostics);
assert(repeated_semantics);
assert(
    repeated_semantics->semantics.source_files().size()
    == shared_checked->semantics.source_files().size());
assert(
    repeated_semantics->semantics.source_spans().size()
    == semantic_source_count);
assert(
    repeated_semantics->semantics.units().front().id
    == shared_checked->semantics.units().front().id);
assert(
    repeated_semantics->semantics.units().front().source
    == shared_checked->semantics.units().front().source);
assert(
    repeated_semantics->semantics.types().front().id
    == shared_checked->semantics.types().front().id);
assert(
    repeated_semantics->semantics.values().front().id
    == shared_checked->semantics.values().front().id);
shared_checked->parsed.units.clear();
assert(shared_checked->semantics.units().front().name
       == "shared_preprocessor_app");
assert(shared_checked->semantics.values().front().name == "value");
assert(shared_checked->semantics.source_spans().size()
       == semantic_source_count);

const auto run_shared_preprocessor =
    [&](const fsim::project::Config& run_config,
        const fsim::app::SimulationEngine engine) {
      fsim::diagnostic::Engine run_diagnostics;
      auto project = fsim::app::build_project(
          run_config, run_diagnostics);
      assert(project);
      assert(project->specialization_cache_keys.size() == 1);
      assert(project->design.specializations().size() == 1);
      assert(project->design.specializations().front().is_cell);
      auto key = project->specialization_cache_keys.front();
      auto capture =
          capture_simulation(std::move(*project), engine);
      return std::pair{
          std::move(key), std::move(capture)};
    };
auto [shared_key_a, shared_reference_a] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::interpreter);
auto [shared_key_a_warm, shared_hybrid_a] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::compiled);
assert(shared_key_a == shared_key_a_warm);
compare_captures(shared_reference_a, shared_hybrid_a);
assert(
    shared_hybrid_a.final_values
    == std::vector<std::string>{"0"});

write_shared_macro("1'b1");
auto [shared_key_b, shared_reference_b] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::interpreter);
auto [shared_key_b_warm, shared_hybrid_b] =
    run_shared_preprocessor(
        shared_preprocessor_config,
        fsim::app::SimulationEngine::compiled);
assert(shared_key_b == shared_key_b_warm);
assert(shared_key_b != shared_key_a);
compare_captures(shared_reference_b, shared_hybrid_b);
assert(
    shared_hybrid_b.final_values
    == std::vector<std::string>{"1"});

auto independent_file_config = shared_preprocessor_config;
independent_file_config.source_sets.front().compilation_unit =
    "file";
fsim::diagnostic::Engine independent_file_diagnostics;
assert(
    !fsim::app::check_project(
        independent_file_config,
        independent_file_diagnostics));
assert(std::any_of(
    independent_file_diagnostics.diagnostics().begin(),
    independent_file_diagnostics.diagnostics().end(),
    [](const fsim::diagnostic::Diagnostic& diagnostic) {
      return diagnostic.code == "FSIM-SV-PP-028";
    }));

auto combined_preprocessor_config =
    shared_preprocessor_config;
combined_preprocessor_config.project.name =
    "combined-preprocessor-test";
combined_preprocessor_config.build.cache_path =
    directory / "combined-preprocessor-cache";
combined_preprocessor_config.source_sets.clear();
auto combined_definitions = shared_preprocessor_sources;
combined_definitions.library = "definitions";
combined_definitions.compilation_unit = "combined";
combined_definitions.files = {shared_macro_source};
auto combined_module = shared_preprocessor_sources;
combined_module.compilation_unit = "combined";
combined_module.files = {shared_module_source};
combined_preprocessor_config.source_sets = {
    std::move(combined_definitions),
    std::move(combined_module)};
auto [combined_key, combined_reference] =
    run_shared_preprocessor(
        combined_preprocessor_config,
        fsim::app::SimulationEngine::interpreter);
auto [combined_key_warm, combined_hybrid] =
    run_shared_preprocessor(
        combined_preprocessor_config,
        fsim::app::SimulationEngine::compiled);
assert(combined_key == combined_key_warm);
compare_captures(combined_reference, combined_hybrid);
assert(
    combined_hybrid.final_values
    == std::vector<std::string>{"1"});

fsim::diagnostic::Engine mixed_diagnostics;
const auto mixed_manifest =
    std::filesystem::path{FSIM_TEST_SOURCE_DIR}
    / "examples/vertical_slice/fsim.toml";
auto mixed_config =
    fsim::project::load(mixed_manifest, mixed_diagnostics);
assert(mixed_config);
mixed_config->build.cache_path = directory / "mixed-cache";
mixed_config->run.trace_file.reset();
auto mixed_reference_project =
    fsim::app::build_project(*mixed_config, mixed_diagnostics);
auto mixed_hybrid_project =
    fsim::app::build_project(*mixed_config, mixed_diagnostics);
assert(mixed_reference_project);
assert(mixed_hybrid_project);
const auto mixed_reference = capture_simulation(
    std::move(*mixed_reference_project),
    fsim::app::SimulationEngine::interpreter);
const auto mixed_hybrid = capture_simulation(
    std::move(*mixed_hybrid_project),
    fsim::app::SimulationEngine::compiled);
compare_captures(mixed_reference, mixed_hybrid);
assert(
    mixed_hybrid.result.status
    == fsim::runtime::RunStatus::stopped);
assert(mixed_hybrid.result.time == 6);

fsim::app::Simulation simulation(std::move(*first), config.run.max_deltas);
const auto q = simulation.find_signal("q");
const auto two_state = simulation.find_signal("two_state");
assert(q && two_state);
assert(simulation.read_signal(*two_state).to_msb_string() == "0");
bool rejected_lossy_deposit = false;
try {
  simulation.deposit_signal(
      *two_state,
      fsim::runtime::PackedLogic4::from_msb_string("X"));
} catch (const std::invalid_argument&) {
  rejected_lossy_deposit = true;
}
assert(rejected_lossy_deposit);
const auto result = simulation.run();
assert(result.status == fsim::runtime::RunStatus::stopped);
assert(result.time == 3);
assert(simulation.finished());
assert(!simulation.poisoned());
assert(simulation.read_signal(*q).to_msb_string() == "1");

simulation.force_signal(
    *q, fsim::runtime::PackedLogic4::from_msb_string("0"));
simulation.deposit_signal(
    *q, fsim::runtime::PackedLogic4::from_msb_string("1"));
assert(simulation.read_signal(*q).to_msb_string() == "0");
simulation.release_signal(*q);
assert(simulation.read_signal(*q).to_msb_string() == "1");

std::string error;
assert(fsim::app::parse_time("25ns", "1ns", error) == 25);
assert(!fsim::app::parse_time("1ps", "1ns", error));
const auto value = fsim::app::parse_value("10xz", 4, error);
assert(value && value->to_msb_string() == "10XZ");
const auto logic9 =
    fsim::app::parse_value("uWlH-", 5, error);
assert(logic9 && logic9->is_logic9());
assert(logic9->to_msb_string() == "UWLH-");

auto compiled_debug_project =
    fsim::app::build_project(config, diagnostics);
assert(compiled_debug_project);
const std::string debug_commands =
    "scope\n"
    "scopes\n"
    "scope u_child\n"
    "signals\n"
    "show value\n"
    "scope ..\n"
    "break signal q == 1\n"
    "break time 1ns\n"
    "breakpoints\n"
    "continue\n"
    "delete 2\n"
    "break source tb.sv:14\n"
    "run-until 3ns\n"
    "delete 3\n"
    "locals\n"
    "step statement\n"
    "locals\n"
    "step statement\n"
    "delete 1\n"
    "locals\n"
    "step process\n"
    "where\n"
    "break signal child_y\n"
    "clear\n"
    "breakpoints\n"
    "continue\n"
    "continue\n"
    "step delta\n"
    "quit\n";
fsim::app::Simulation debug_simulation(
    std::move(*second),
    config.run.max_deltas,
    fsim::app::SimulationEngine::interpreter);
assert(debug_simulation.compiled_process_count() == 0);
std::size_t observed_changes = 0;
debug_simulation.set_signal_change_hook(
    [&observed_changes](
        fsim::runtime::simir::SignalId,
        const fsim::runtime::PackedLogic4&,
        fsim::runtime::SimulationTick,
        std::uint64_t) { ++observed_changes; });
debug_simulation.start();
std::istringstream debug_input{debug_commands};
std::ostringstream debug_output;
std::ostringstream debug_error;
assert(
    fsim::app::run_debug_repl(
        debug_simulation, debug_input, debug_output, debug_error)
    == 0);
assert(debug_error.str().empty());
const auto transcript = debug_output.str();
assert(transcript.find("tb.u_child") != std::string::npos);
assert(
    transcript.find("tb.u_child.value = X") != std::string::npos);
assert(
    transcript.find("breakpoint 1 set on tb.q == 1")
    != std::string::npos);
assert(
    transcript.find("breakpoint 2 set at time 1") != std::string::npos);
assert(
    transcript.find("hit breakpoint 1: tb.q changed to 1 at time 2")
    != std::string::npos);
assert(
    transcript.find("hit breakpoint 2: time 1") != std::string::npos);
assert(
    transcript.find("breakpoint 3 set at tb.sv:14")
    != std::string::npos);
const auto source_breakpoint_hit =
    transcript.find("hit breakpoint 3: ");
assert(source_breakpoint_hit != std::string::npos);
const auto source_breakpoint_end =
    transcript.find('\n', source_breakpoint_hit);
assert(
    transcript.find(
        "tb.sv:14:", source_breakpoint_hit)
    < source_breakpoint_end);
assert(transcript.find("tb.sv:15:") != std::string::npos);
assert(transcript.find("tb.sv:16:") != std::string::npos);
assert(
    transcript.find("local_state = 0") != std::string::npos);
assert(
    transcript.find("local_state = 1") != std::string::npos);
assert(transcript.find("stopped at time 2") != std::string::npos);
assert(transcript.find("time 2, delta") != std::string::npos);
assert(transcript.find("cleared all breakpoints") != std::string::npos);
assert(transcript.find("no breakpoints") != std::string::npos);
assert(
    transcript.find("simulation finished at time 3")
    != std::string::npos);
const auto first_finished =
    transcript.find("simulation has finished");
assert(first_finished != std::string::npos);
assert(
    transcript.find("simulation has finished", first_finished + 1)
    != std::string::npos);
assert(debug_simulation.finished());
assert(!debug_simulation.poisoned());
assert(observed_changes > 0);

fsim::app::Simulation compiled_debug_simulation(
    std::move(*compiled_debug_project),
    config.run.max_deltas,
    fsim::app::SimulationEngine::debug);
#if defined(FSIM_HAS_LLVM)
assert(compiled_debug_simulation.compiled_process_count() == 2);
assert(compiled_debug_simulation.compiled_module_count() == 2);
const auto debug_native_cache =
    compiled_debug_simulation.native_cache_statistics();
// The same specialization modules were already cached at the configured O2
// run setting. Cold objects here therefore prove that debug forces O0.
assert(debug_native_cache.hits == 0);
assert(debug_native_cache.misses == 2);
assert(debug_native_cache.stores == 2);
#else
assert(compiled_debug_simulation.compiled_process_count() == 0);
assert(compiled_debug_simulation.compiled_module_count() == 0);
#endif
std::size_t compiled_observed_changes = 0;
compiled_debug_simulation.set_signal_change_hook(
    [&compiled_observed_changes](
        fsim::runtime::simir::SignalId,
        const fsim::runtime::PackedLogic4&,
        fsim::runtime::SimulationTick,
        std::uint64_t) { ++compiled_observed_changes; });
compiled_debug_simulation.start();
std::istringstream compiled_debug_input{debug_commands};
std::ostringstream compiled_debug_output;
std::ostringstream compiled_debug_error;
assert(
    fsim::app::run_debug_repl(
        compiled_debug_simulation,
        compiled_debug_input,
        compiled_debug_output,
        compiled_debug_error)
    == 0);
assert(compiled_debug_error.str().empty());
assert(compiled_debug_output.str() == transcript);
assert(compiled_debug_simulation.finished());
assert(!compiled_debug_simulation.poisoned());
assert(compiled_observed_changes == observed_changes);
for (const auto& signal : debug_simulation.design().signals()) {
  assert(
      compiled_debug_simulation.read_signal(signal.id)
      == debug_simulation.read_signal(signal.id));
}

auto poisoned_project = fsim::app::build_project(config, diagnostics);
assert(poisoned_project);
fsim::app::Simulation poisoned_simulation(
    std::move(*poisoned_project), config.run.max_deltas);
#if defined(FSIM_HAS_LLVM)
assert(poisoned_simulation.compiled_process_count() > 0);
#else
assert(poisoned_simulation.compiled_process_count() == 0);
#endif
poisoned_simulation.set_signal_change_hook(
    [](
        fsim::runtime::simir::SignalId,
        const fsim::runtime::PackedLogic4&,
        fsim::runtime::SimulationTick,
        std::uint64_t) {
      throw std::runtime_error("fatal signal observer");
    });
poisoned_simulation.start();
std::istringstream poisoned_input{
    "continue\n"
    "continue\n"
    "step delta\n"
    "quit\n"};
std::ostringstream poisoned_output;
std::ostringstream poisoned_error;
assert(
    fsim::app::run_debug_repl(
        poisoned_simulation,
        poisoned_input,
        poisoned_output,
        poisoned_error)
    == 0);
assert(poisoned_simulation.poisoned());
assert(!poisoned_simulation.finished());
assert(
    poisoned_error.str().find("fatal signal observer")
    != std::string::npos);
const auto unavailable =
    poisoned_output.str().find(
        "simulation is unavailable after a fatal runtime error");
assert(unavailable != std::string::npos);
assert(
    poisoned_output.str().find(
        "simulation is unavailable after a fatal runtime error",
        unavailable + 1)
    != std::string::npos);

const auto manifest = directory / "fsim.toml";
const auto debug_trace = directory / "debug-select.vcd";
{
  std::ofstream output(manifest);
  output << R"(
schema = 1

[project]
name = "debug-cli-test"
top = "sv:work.tb"
time_resolution = "1ns"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "cli-cache"

[run]
max_deltas = 1000
trace_file = "debug-select.vcd"
trace_filters = ["__none__"]
)";
}
std::istringstream cli_input{
    "where\n"
    "trace list\n"
    "trace add q\n"
    "trace list\n"
    "run 1ns\n"
    "trace remove q\n"
    "trace list\n"
    "continue\n"
    "quit\n"};
std::ostringstream cli_output;
std::ostringstream cli_error;
auto services = fsim::app::make_cli_services(cli_input);
const auto manifest_text = manifest.string();
const std::vector<const char*> arguments{
    "fsim", "debug", "-p", manifest_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(arguments.size()),
        arguments.data(),
        services,
        cli_output,
        cli_error)
    == 0);
assert(
    cli_output.str().find("fsim debugger: tb") != std::string::npos);
#if defined(FSIM_HAS_LLVM)
assert(
    cli_output.str().find(
        "(O0 hybrid, 2 compiled process(es) in "
        "2 specialization module(s))")
    != std::string::npos);
#else
assert(
    cli_output.str().find("(reference evaluator)")
    != std::string::npos);
#endif
assert(
    cli_output.str().find("time 0, delta 0, scope tb")
    != std::string::npos);
assert(
    cli_output.str().find("(no traced signals)")
    != std::string::npos);
assert(
    cli_output.str().find("tracing tb.q")
    != std::string::npos);
assert(
    cli_output.str().find("stopped tracing tb.q")
    != std::string::npos);
std::ifstream debug_trace_stream(debug_trace);
const std::string debug_vcd{
    std::istreambuf_iterator<char>{debug_trace_stream},
    std::istreambuf_iterator<char>{}};
assert(!debug_vcd.empty());
std::string q_identifier;
std::istringstream debug_vcd_lines{debug_vcd};
for (std::string line; std::getline(debug_vcd_lines, line);) {
  if (line.starts_with("$var wire 1 ")
      && line.ends_with(" q $end")) {
    std::istringstream declaration{line};
    std::string directive;
    std::string kind;
    std::string width;
    declaration >> directive >> kind >> width >> q_identifier;
    break;
  }
}
assert(!q_identifier.empty());
assert(
    debug_vcd.find("\nx" + q_identifier + "\n")
    != std::string::npos);
assert(
    debug_vcd.find("\n0" + q_identifier + "\n")
    != std::string::npos);
assert(
    debug_vcd.find("\n1" + q_identifier + "\n")
    == std::string::npos);

restored_interrupt_count = 0;
const auto previous_interrupt_handler =
    std::signal(SIGINT, record_restored_interrupt);
assert(previous_interrupt_handler != SIG_ERR);
std::istringstream interrupted_cli_input{
    "continue\n"
    "continue\n"
    "quit\n"};
InterruptingOutputBuffer interrupted_output_buffer;
std::ostream interrupted_cli_output{&interrupted_output_buffer};
std::ostringstream interrupted_cli_error;
auto interrupted_services =
    fsim::app::make_cli_services(interrupted_cli_input);
assert(
    fsim::cli::run(
        static_cast<int>(arguments.size()),
        arguments.data(),
        interrupted_services,
        interrupted_cli_output,
        interrupted_cli_error)
    == 0);
assert(interrupted_cli_error.str().empty());
const auto interrupted_transcript =
    interrupted_output_buffer.str();
assert(
    interrupted_transcript.find("process ")
    != std::string::npos);
assert(
    interrupted_transcript.find("stopped at time 0")
    != std::string::npos);
assert(
    interrupted_transcript.find("simulation finished at time 3")
    != std::string::npos);
(void)std::raise(SIGINT);
assert(restored_interrupt_count == 1);
assert(std::signal(SIGINT, previous_interrupt_handler) != SIG_ERR);

const auto differently_named = directory / "different_filename.sv";
{
  std::ofstream output(differently_named);
  output << "module actual_top; endmodule\n";
}
std::ostringstream direct_output;
std::ostringstream direct_error;
const auto direct_text = differently_named.string();
const std::vector<const char*> direct_arguments{
    "fsim", "run", direct_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(direct_arguments.size()),
        direct_arguments.data(),
        services,
        direct_output,
        direct_error)
    == 0);
assert(
    direct_output.str().find("simulation completed at tick 0")
    != std::string::npos);

std::ostringstream json_output;
std::ostringstream json_error;
const std::vector<const char*> json_arguments{
    "fsim",
    "check",
    "--diagnostics=json",
    "--definitely-invalid"};
assert(
    fsim::cli::run(
        static_cast<int>(json_arguments.size()),
        json_arguments.data(),
        services,
        json_output,
        json_error)
    == 2);
assert(
    json_error.str().find("\"code\":\"FSIM-CLI-0001\"")
    != std::string::npos);

std::ostringstream standard_output;
std::ostringstream standard_error;
const std::vector<const char*> standard_arguments{
    "fsim",
    "check",
    "--standard=bogus",
    direct_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(standard_arguments.size()),
        standard_arguments.data(),
        services,
        standard_output,
        standard_error)
    == 1);
assert(
    standard_error.str().find("unsupported standard 'bogus'")
    != std::string::npos);

const auto scaled_manifest = directory / "scaled.toml";
const auto scaled_trace = directory / "scaled.vcd";
{
  std::ofstream output(scaled_manifest);
  output << R"(
schema = 1
[project]
name = "scaled-vcd"
top = "sv:work.tb"
time_resolution = "2ps"

[[source_set]]
language = "systemverilog"
standard = "2017"
library = "work"
files = ["tb.sv"]

[build]
cache_path = "scaled-cache"

[run]
max_deltas = 1000
trace_file = "scaled.vcd"
)";
}
std::ostringstream scaled_output;
std::ostringstream scaled_error;
const auto scaled_manifest_text = scaled_manifest.string();
const std::vector<const char*> scaled_arguments{
    "fsim", "run", "-p", scaled_manifest_text.c_str()};
assert(
    fsim::cli::run(
        static_cast<int>(scaled_arguments.size()),
        scaled_arguments.data(),
        services,
        scaled_output,
        scaled_error)
    == 0);
std::ifstream scaled_stream(scaled_trace);
const std::string scaled_vcd{
    std::istreambuf_iterator<char>{scaled_stream},
    std::istreambuf_iterator<char>{}};
assert(
    scaled_vcd.find("$timescale 1ps $end") != std::string::npos);
assert(scaled_vcd.find("#4") != std::string::npos);

const auto timescale_source = directory / "timescale.sv";
{
  std::ofstream output(timescale_source);
  output << R"(`timescale 10ns/100ps
module timed;
initial #2 $finish;
endmodule
)";
}
fsim::project::Config timescale_config;
timescale_config.base_directory = directory;
timescale_config.project.name = "timescale";
timescale_config.project.top = "sv:work.timed";
timescale_config.project.time_resolution = "auto";
timescale_config.build.cache_path = directory / "timescale-cache";
timescale_config.run.max_deltas = 1000;
fsim::project::SourceSet timescale_sources;
timescale_sources.language =
    fsim::project::Language::system_verilog;
timescale_sources.standard = "2017";
timescale_sources.library = "work";
timescale_sources.files.push_back(timescale_source);
timescale_config.source_sets.push_back(
    std::move(timescale_sources));
fsim::diagnostic::Engine timescale_diagnostics;
auto timed_project =
    fsim::app::build_project(
        timescale_config, timescale_diagnostics);
assert(timed_project);
assert(timed_project->time_resolution == "100ps");
fsim::app::Simulation timed_simulation(
    std::move(*timed_project),
    timescale_config.run.max_deltas);
const auto timed_result = timed_simulation.run();
assert(timed_result.status == fsim::runtime::RunStatus::stopped);
assert(timed_result.time == 200);
timescale_config.project.time_resolution = "1ns";
fsim::diagnostic::Engine coarse_time_diagnostics;
assert(!fsim::app::build_project(
    timescale_config, coarse_time_diagnostics));
assert(coarse_time_diagnostics.has_error());
}

}  // namespace fsim::test
