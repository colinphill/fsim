// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/app/artifact_phase.hpp"
#include "fsim/app/design_artifact.hpp"

#include <array>
#include <cassert>
#include <chrono>
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

struct ReportCapture {
  std::string message;
  fsim::runtime::simir::AssertionSeverity severity{};
  fsim::runtime::simir::SourceLocation source;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(
      const ReportCapture&,
      const ReportCapture&) = default;
};

bool equivalent_artifact_reports(
    const std::vector<ReportCapture>& left,
    const std::vector<ReportCapture>& right) {
  return left.size() == right.size()
      && std::ranges::equal(
          left, right, [](const auto& lhs, const auto& rhs) {
            return lhs.message == rhs.message
                && lhs.severity == rhs.severity
                && lhs.time == rhs.time
                && lhs.delta == rhs.delta
                && lhs.source.line == rhs.source.line
                && lhs.source.column == rhs.source.column
                && std::filesystem::path{lhs.source.path}.filename()
                    == std::filesystem::path{rhs.source.path}.filename();
          });
}

struct Capture {
  std::vector<ReportCapture> reports;
  fsim::runtime::Logic4Word marker{};
  fsim::runtime::Logic4Word qualifier_marker{};
  fsim::runtime::Logic4Word pattern_marker{};
  std::string failure;
  fsim::runtime::simir::AssertionSeverity failure_severity{};
  bool failure_was_reported{};
  std::size_t compiled_processes{};
  fsim::app::NativeCacheStatistics native_cache;
};

int run_cli(
    const std::vector<std::string>& arguments,
    std::istream& input,
    std::ostream& output,
    std::ostream& error) {
  std::vector<const char*> raw;
  raw.reserve(arguments.size());
  for (const auto& argument : arguments) {
    raw.push_back(argument.c_str());
  }
  return fsim::cli::run(
      static_cast<int>(raw.size()),
      raw.data(),
      fsim::app::make_cli_services(input),
      output,
      error);
}

fsim::project::Config config_for(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "assertion-actions";
  config.project.top = "sv:work.assertion_actions";
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
  sources.standard = "2017";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));
  return config;
}

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine) {
  fsim::app::Simulation simulation{
      std::move(project), 1000, engine};
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  simulation.set_report_hook(
      [&capture](
          const fsim::runtime::simir::ProcessId,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity severity,
          const fsim::runtime::simir::SourceLocation& source,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        capture.reports.push_back(
            {
                std::string{message},
                severity,
                source,
                time,
                delta,
            });
      });
  try {
    (void)simulation.run();
    assert(false && "$fatal must terminate the assertion fixture");
  } catch (const fsim::runtime::simir::AssertionError& error) {
    capture.failure = error.what();
    capture.failure_severity = error.severity();
    capture.failure_was_reported = error.reported();
  }
  const auto marker =
      simulation.find_signal("assertion_actions.marker");
  assert(marker);
  capture.marker = simulation.read_signal(*marker).low_word();
  const auto qualifier_marker =
      simulation.find_signal("assertion_actions.qualifier_marker");
  assert(qualifier_marker);
  capture.qualifier_marker =
      simulation.read_signal(*qualifier_marker).low_word();
  const auto pattern_marker =
      simulation.find_signal("assertion_actions.pattern_marker");
  assert(pattern_marker);
  capture.pattern_marker =
      simulation.read_signal(*pattern_marker).low_word();
  capture.native_cache = simulation.native_cache_statistics();
  return capture;
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
  return execute(std::move(*project), engine);
}

void test_assertion_actions(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  const auto config = config_for(directory, source, optimization);
  const auto reference =
      run_once(config, fsim::app::SimulationEngine::interpreter);
  const auto compiled =
      run_once(config, fsim::app::SimulationEngine::compiled);
  const auto warm =
      run_once(config, fsim::app::SimulationEngine::compiled);

  {
    std::ofstream output(source, std::ios::app);
    output << "\n// qualified-case cache source edit\n";
    assert(output.good());
  }
  const auto changed =
      run_once(config, fsim::app::SimulationEngine::compiled);

  assert(reference.reports == compiled.reports);
  assert(reference.reports == warm.reports);
  assert(reference.reports == changed.reports);
  assert(reference.marker == compiled.marker);
  assert(reference.marker == warm.marker);
  assert(reference.marker == changed.marker);
  assert(reference.qualifier_marker == compiled.qualifier_marker);
  assert(reference.qualifier_marker == warm.qualifier_marker);
  assert(reference.qualifier_marker == changed.qualifier_marker);
  assert(reference.pattern_marker == compiled.pattern_marker);
  assert(reference.pattern_marker == warm.pattern_marker);
  assert(reference.pattern_marker == changed.pattern_marker);
  assert(reference.failure == compiled.failure);
  assert(reference.failure == warm.failure);
  assert(reference.failure == changed.failure);
  assert(reference.failure_severity == compiled.failure_severity);
  assert(reference.failure_severity == warm.failure_severity);
  assert(reference.failure_severity == changed.failure_severity);
  assert(
      reference.failure_was_reported
      && compiled.failure_was_reported);
  assert(
      reference.marker.bval == 0
      && reference.marker.aval == 31);
  assert(
      reference.qualifier_marker.bval == 0
      && reference.qualifier_marker.aval == 7);
  assert(
      reference.pattern_marker.bval == 0
      && reference.pattern_marker.aval == 8);
  assert(reference.reports.size() == 14);
  constexpr std::array expected_messages{
      std::string_view{"$info"},
      std::string_view{"standalone warning"},
      std::string_view{"standalone error"},
      std::string_view{"unique case has multiple matching items"},
      std::string_view{"unique case has no matching item"},
      std::string_view{"unique0 case has multiple matching items"},
      std::string_view{"priority case has no matching item"},
      std::string_view{"unique case has multiple matching items"},
      std::string_view{"unique case has multiple matching items"},
      std::string_view{"priority case has no matching item"},
      std::string_view{"pass block"},
      std::string_view{"failure block"},
      std::string_view{"assertion failed"},
      std::string_view{"terminal"},
  };
  constexpr std::array expected_severities{
      fsim::runtime::simir::AssertionSeverity::note,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::error,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::note,
      fsim::runtime::simir::AssertionSeverity::warning,
      fsim::runtime::simir::AssertionSeverity::error,
      fsim::runtime::simir::AssertionSeverity::failure,
  };
  for (std::size_t index = 0; index < expected_messages.size(); ++index) {
    assert(reference.reports[index].message == expected_messages[index]);
    assert(
        reference.reports[index].severity
        == expected_severities[index]);
    assert(
        std::filesystem::path{
            reference.reports[index].source.path}.filename()
        == "assertion_actions.sv");
    assert(reference.reports[index].source.line > 0);
    assert(reference.reports[index].time == 0);
    assert(reference.reports[index].delta == 0);
  }
  assert(
      reference.failure.find("terminal")
      != std::string::npos);
  assert(
      reference.failure_severity
      == fsim::runtime::simir::AssertionSeverity::failure);
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes == 1);
  assert(compiled.native_cache.hits == 0);
  assert(compiled.native_cache.misses == 1);
  assert(compiled.native_cache.stores == 1);
  assert(warm.compiled_processes == 1);
  assert(warm.native_cache.hits == 1);
  assert(warm.native_cache.misses == 0);
  assert(changed.compiled_processes == 1);
  assert(changed.native_cache.hits == 0);
  assert(changed.native_cache.misses == 1);
  assert(changed.native_cache.stores == 1);
#else
  assert(compiled.compiled_processes == 0);
  assert(warm.compiled_processes == 0);
  assert(changed.compiled_processes == 0);
#endif
}

void test_cli_failure_reporting(
    const std::filesystem::path& manifest) {
  std::istringstream input;
  std::ostringstream output;
  std::ostringstream error;
  const auto result = run_cli(
      {"fsim", "run", "-p", manifest.string()},
      input,
      output,
      error);
  assert(result == 1);
  const auto report_text = output.str();
  const std::string_view marker{"[FSIM-HDL-REPORT]"};
  std::size_t report_count{};
  for (auto position = report_text.find(marker);
       position != std::string::npos;
       position = report_text.find(marker, position + marker.size())) {
    ++report_count;
  }
  assert(report_count == 14);
  assert(
      report_text.find("note[FSIM-HDL-REPORT]: $info")
      != std::string::npos);
  assert(
      report_text.find("warning[FSIM-HDL-REPORT]: failure block")
      != std::string::npos);
  assert(
      report_text.find(
          "warning[FSIM-HDL-REPORT]: unique case has multiple matching items")
      != std::string::npos);
  assert(
      report_text.find("failure[FSIM-HDL-REPORT]: terminal")
      != std::string::npos);
  assert(
      error.str().find("FSIM-RUN-ASSERT-0001")
      != std::string::npos);
}

struct ConcurrentCapture {
  std::vector<std::string> outputs;
  std::vector<ReportCapture> reports;
  std::vector<std::string> process_names;
  std::vector<fsim::app::ConcurrentAssertionCoverage> coverage;
  std::vector<fsim::app::ConcurrentAssertionEvent> events;
  std::vector<fsim::app::ConcurrentAssertionEvent> callback_events;
  fsim::app::NativeCacheStatistics native_cache;
  std::size_t compiled_processes{};
};

ConcurrentCapture capture_concurrent_assertions(
    fsim::app::BuiltProject project,
    const std::uint64_t max_deltas,
    const fsim::app::SimulationEngine engine) {
  fsim::app::Simulation simulation{
      std::move(project), max_deltas, engine};
  ConcurrentCapture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  capture.native_cache = simulation.native_cache_statistics();
  for (const auto& process : simulation.design_ir().processes()) {
    if (process.name.find("request_check") != std::string::npos
        || process.name.find("$assertion$2") != std::string::npos) {
      capture.process_names.push_back(process.name);
    }
  }
  simulation.set_output_hook(
      [&capture](
          const fsim::runtime::simir::ProcessId,
          const std::string_view text,
          const bool,
          const fsim::runtime::SimulationTick,
          const std::uint64_t) {
        capture.outputs.emplace_back(text);
      });
  simulation.set_report_hook(
      [&capture](
          const fsim::runtime::simir::ProcessId,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity severity,
          const fsim::runtime::simir::SourceLocation& source,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        capture.reports.push_back(
            {std::string{message}, severity, source, time, delta});
      });
  simulation.set_concurrent_assertion_hook(
      [&capture](const fsim::app::ConcurrentAssertionEvent& event) {
        capture.callback_events.push_back(event);
      });
  const auto result = simulation.run();
  assert(result.status == fsim::runtime::RunStatus::stopped);
  capture.coverage = simulation.concurrent_assertion_coverage();
  capture.events = simulation.concurrent_assertion_events();
  return capture;
}

ConcurrentCapture run_concurrent_assertions(
    const fsim::project::Config& config,
    const fsim::app::SimulationEngine engine) {
  fsim::diagnostic::Engine diagnostics;
  auto project = fsim::app::build_project(config, diagnostics);
  if (!project) {
    fsim::diagnostic::print_text(std::cerr, diagnostics);
  }
  assert(project);
  return capture_concurrent_assertions(
      std::move(*project), config.run.max_deltas, engine);
}

void test_concurrent_assertions(
    const std::filesystem::path& directory) {
  const auto source = directory / "concurrent_assertions.sv";
  {
    std::ofstream output(source);
    output << R"(
module concurrent_leaf(
    input logic clock,
    input logic request
);
  property requested;
    @(posedge clock) request;
  endproperty
  request_check: assert property (requested)
    $display("assert pass");
    else $warning("assert failure");
  cover property (requested) $display("cover hit");
endmodule

module concurrent_top;
  logic clock;
  logic request;
  concurrent_leaf left(clock, request);
  concurrent_leaf right(clock, request);
  initial begin
    clock = 1'b0;
    request = 1'b0;
    #1 request = 1'b1;
    clock = 1'b1;
    #1 clock = 1'b0;
    $assertoff;
    #1 request = 1'b0;
    clock = 1'b1;
    #1 clock = 1'b0;
    $asserton;
    $assertcontrol(7);
    #1 request = 1'b1;
    clock = 1'b1;
    #1 clock = 1'b0;
    $assertpasson;
    $assertfailoff;
    #1 request = 1'b0;
    clock = 1'b1;
    #1 clock = 1'b0;
    $assertfailon;
    #1 clock = 1'b1;
    #1 $finish;
  end
endmodule
)";
    assert(output.good());
  }
  auto config = config_for(
      directory, source, fsim::project::Optimization::o2);
  config.project.name = "concurrent-assertions";
  config.project.top = "sv:work.concurrent_top";
  const auto reference = run_concurrent_assertions(
      config, fsim::app::SimulationEngine::interpreter);
  const auto compiled = run_concurrent_assertions(
      config, fsim::app::SimulationEngine::compiled);
  assert(reference.outputs == compiled.outputs);
  assert(reference.reports == compiled.reports);
  assert(reference.process_names == compiled.process_names);
  assert(reference.coverage == compiled.coverage);
  assert(reference.events == compiled.events);
  assert(reference.callback_events == reference.events);
  assert(compiled.callback_events == compiled.events);
  const std::vector<std::string> expected_outputs{
      "assert pass", "cover hit",
      "assert pass", "cover hit"};
  assert(reference.outputs == expected_outputs);
  assert(reference.reports.size() == 2);
  assert(std::ranges::all_of(
      reference.reports, [](const auto& report) {
        return report.message == "assert failure"
            && report.severity
                == fsim::runtime::simir::AssertionSeverity::warning
            && std::filesystem::path{report.source.path}.filename()
                == "concurrent_assertions.sv"
            && report.time == 9;
      }));
  assert(reference.process_names.size() == 4);
  assert(reference.coverage.size() == 4);
  assert(std::ranges::all_of(
      reference.coverage, [](const auto& coverage) {
        return coverage.attempts == 4
            && coverage.passes == 2
            && coverage.failures == 2
            && !coverage.process.empty()
            && (coverage.name == "request_check"
                || coverage.name == "$assertion$2");
      }));
  assert(std::ranges::count_if(
      reference.coverage, [](const auto& coverage) {
        return coverage.kind
            == fsim::app::ConcurrentAssertionCoverageKind::assertion;
      }) == 2);
  assert(reference.events.size() == 20);
  assert(std::ranges::count_if(
      reference.events, [](const auto& event) {
        return event.outcome
            == fsim::app::ConcurrentAssertionOutcome::pass;
      }) == 8);
  assert(std::ranges::count_if(
      reference.events, [](const auto& event) {
        return event.outcome
            == fsim::app::ConcurrentAssertionOutcome::failure;
      }) == 8);
  assert(std::ranges::count_if(
      reference.events, [](const auto& event) {
        return event.outcome
            == fsim::app::ConcurrentAssertionOutcome::disabled;
      }) == 4);
  assert(std::ranges::count_if(
      reference.events, [](const auto& event) {
        return event.action_suppressed;
      }) == 12);

  const auto debug = run_concurrent_assertions(
      config, fsim::app::SimulationEngine::debug);
  assert(reference.outputs == debug.outputs);
  assert(reference.reports == debug.reports);
  assert(reference.coverage == debug.coverage);
  assert(reference.events == debug.events);

  auto multiple_roots = config;
  multiple_roots.project.name = "concurrent-assertions-multiple-roots";
  multiple_roots.project.top.clear();
  multiple_roots.project.tops = {
      {"sv:work.concurrent_top", "alpha"},
      {"sv:work.concurrent_top", "beta"}};
  multiple_roots.build.cache_path = directory / "concurrent-multiple-cache";
  const auto multiple_reference = run_concurrent_assertions(
      multiple_roots, fsim::app::SimulationEngine::interpreter);
  const auto multiple_compiled = run_concurrent_assertions(
      multiple_roots, fsim::app::SimulationEngine::compiled);
  assert(multiple_reference.outputs == multiple_compiled.outputs);
  assert(multiple_reference.reports == multiple_compiled.reports);
  assert(multiple_reference.process_names
         == multiple_compiled.process_names);
  assert(multiple_reference.coverage == multiple_compiled.coverage);
  assert(multiple_reference.events == multiple_compiled.events);
  assert(multiple_reference.callback_events
         == multiple_reference.events);
  assert(multiple_reference.coverage.size() == 8);
  assert(multiple_reference.events.size() == 40);
  assert(std::ranges::count_if(
      multiple_reference.coverage, [](const auto& coverage) {
        return coverage.process.starts_with("alpha.");
      }) == 4);
  assert(std::ranges::count_if(
      multiple_reference.coverage, [](const auto& coverage) {
        return coverage.process.starts_with("beta.");
      }) == 4);

  const auto object = directory / "concurrent-assertions.fsimobj";
  const auto design = directory / "concurrent-assertions.fsimdesign";
  fsim::diagnostic::Engine compile_diagnostics;
  const auto compiled_object = fsim::app::compile_artifact(
      config, object, compile_diagnostics);
  if (!compiled_object) {
    fsim::diagnostic::print_text(std::cerr, compile_diagnostics);
  }
  assert(compiled_object && !compile_diagnostics.has_error());
  const std::array objects{object};
  fsim::project::Config artifact_elaboration;
  artifact_elaboration.base_directory = directory;
  artifact_elaboration.project.name =
      "concurrent-assertions-artifact";
  artifact_elaboration.project.top = config.project.top;
  artifact_elaboration.project.time_resolution =
      config.project.time_resolution;
  artifact_elaboration.build.optimization = config.build.optimization;
  artifact_elaboration.build.cache_path =
      directory / "concurrent-assertions-elaboration-cache";
  artifact_elaboration.run.max_deltas = config.run.max_deltas;
  fsim::diagnostic::Engine elaborate_diagnostics;
  const auto elaborated_design = fsim::app::elaborate_artifact(
      artifact_elaboration, objects, design, elaborate_diagnostics);
  if (!elaborated_design) {
    fsim::diagnostic::print_text(std::cerr, elaborate_diagnostics);
  }
  assert(elaborated_design && !elaborate_diagnostics.has_error());
  const auto artifact_cache =
      directory / "concurrent-assertions-artifact-cache";
  const auto run_artifact = [&](const fsim::app::SimulationEngine engine) {
    fsim::diagnostic::Engine diagnostics;
    auto built = fsim::app::load_design_artifact(design, diagnostics);
    if (!built) {
      fsim::diagnostic::print_text(std::cerr, diagnostics);
    }
    assert(built && !diagnostics.has_error());
    built->cache_path = artifact_cache;
    return capture_concurrent_assertions(
        std::move(*built), config.run.max_deltas, engine);
  };
  const auto artifact_interpreted = run_artifact(
      fsim::app::SimulationEngine::interpreter);
  const auto artifact_compiled_cold = run_artifact(
      fsim::app::SimulationEngine::compiled);
  const auto artifact_compiled_warm = run_artifact(
      fsim::app::SimulationEngine::compiled);
  for (const auto* capture :
       {&artifact_interpreted,
        &artifact_compiled_cold,
        &artifact_compiled_warm}) {
    assert(capture->outputs == reference.outputs);
    assert(equivalent_artifact_reports(
        capture->reports, reference.reports));
    assert(capture->process_names == reference.process_names);
    assert(capture->coverage == reference.coverage);
    assert(capture->events == reference.events);
    assert(capture->callback_events == capture->events);
  }
#if defined(FSIM_HAS_LLVM)
  assert(artifact_compiled_cold.compiled_processes >= 5);
  assert(artifact_compiled_warm.native_cache.hits != 0);
#endif
  const auto relocated_design =
      directory / "relocated-concurrent-assertions.fsimdesign";
  std::filesystem::rename(design, relocated_design);
  fsim::diagnostic::Engine relocated_diagnostics;
  auto relocated = fsim::app::load_design_artifact(
      relocated_design, relocated_diagnostics);
  assert(relocated && !relocated_diagnostics.has_error());
  relocated->cache_path = artifact_cache;
  const auto artifact_relocated = capture_concurrent_assertions(
      std::move(*relocated), config.run.max_deltas,
      fsim::app::SimulationEngine::compiled);
  assert(artifact_relocated.outputs == reference.outputs);
  assert(equivalent_artifact_reports(
      artifact_relocated.reports, reference.reports));
  assert(artifact_relocated.process_names == reference.process_names);
  assert(artifact_relocated.coverage == reference.coverage);
  assert(artifact_relocated.events == reference.events);
  assert(std::ranges::count_if(
      reference.coverage, [](const auto& coverage) {
        return coverage.kind
            == fsim::app::ConcurrentAssertionCoverageKind::cover;
      }) == 2);
  assert(reference.compiled_processes == 0);
#if defined(FSIM_HAS_LLVM)
  assert(compiled.compiled_processes >= 5);
#else
  assert(compiled.compiled_processes == 0);
#endif
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-assertion-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "assertion_actions.sv";
  {
    std::ofstream output(source);
    output << R"(
module assertion_actions;
  logic [5:0] marker;
  logic [1:0] selector;
  logic [3:0] qualifier_marker;
  logic [3:0] pattern_marker;
  function automatic logic [3:0] qualified_seed(input logic [1:0] value);
    unique case (value)
      2'b01: return 4'h0;
      default: return 4'hf;
    endcase
  endfunction
  localparam logic [3:0] QUALIFIED_SEED = qualified_seed(2'b01);
  function automatic logic [3:0] pattern_seed(input logic [1:0] value);
    case (value) matches
      2'b01: return 4'h1;
      .*: return 4'hf;
    endcase
  endfunction
  localparam logic [3:0] PATTERN_SEED = pattern_seed(2'b01);
  initial begin
    marker = 0;
    selector = 2'b01;
    qualifier_marker = QUALIFIED_SEED;
    pattern_marker = PATTERN_SEED;
    $info;
    $warning("standalone warning");
    $error("standalone error");
    unique case (selector)
      2'b01: qualifier_marker += 1;
      2'b01: qualifier_marker += 8;
    endcase
    unique case (selector)
      2'b11: qualifier_marker = 15;
    endcase
    unique0 casex (2'bx1)
      2'b01: qualifier_marker += 2;
      2'b11: qualifier_marker += 8;
    endcase
    priority casez (2'b11)
      2'b00: qualifier_marker = 15;
    endcase
    unique case (selector) inside
      2'b01: qualifier_marker += 4;
      [2'b00:2'b10]: qualifier_marker += 8;
    endcase
    case (selector) matches
      2'b01: pattern_marker += 1;
      .*: pattern_marker = 15;
    endcase
    case (2'b10) matches
      2'b01: pattern_marker = 15;
      .*: pattern_marker += 2;
    endcase
    unique case (selector) matches
      2'b01: pattern_marker += 4;
      .*: pattern_marker = 15;
    endcase
    priority case (2'b11) matches
      2'b00: pattern_marker = 15;
    endcase
    assert (1'b1) marker += 1; else marker = 63;
    assert (1'b0) marker = 63; else marker += 2;
    assert (1'b1) begin
      logic [5:0] pass_value;
      pass_value = 4;
      marker += pass_value;
      $info("pass block");
    end else begin
      marker = 63;
    end
    assert (1'b0) begin
      marker = 63;
    end else begin
      logic [5:0] failure_value;
      failure_value = 8;
      marker += failure_value;
      $warning("failure block");
    end
    assert (1'b0);
    marker += 16;
    $fatal(1, "terminal");
    marker = 63;
  end
endmodule
)";
  }
  const auto manifest = directory.path / "fsim.toml";
  {
    std::ofstream output(manifest);
    output
        << "schema = 2\n"
        << "[project]\n"
        << "name = \"assertion-actions\"\n"
        << "top = \"sv:work.assertion_actions\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[source_set]]\n"
        << "language = \"systemverilog\"\n"
        << "standard = \"2017\"\n"
        << "library = \"work\"\n"
        << "files = [\"assertion_actions.sv\"]\n"
        << "[build]\n"
        << "optimization = \"O2\"\n"
        << "cache_path = \"cli-cache\"\n"
        << "[run]\n"
        << "max_deltas = 1000\n";
  }

  test_assertion_actions(
      directory.path,
      source,
      fsim::project::Optimization::o0);
  test_assertion_actions(
      directory.path,
      source,
      fsim::project::Optimization::o2);
  test_cli_failure_reporting(manifest);
  test_concurrent_assertions(directory.path);
  std::cout << "assertion application tests passed\n";
  return 0;
}
