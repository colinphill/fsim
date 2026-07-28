// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"

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

struct OutputEvent {
  fsim::runtime::simir::ProcessId process{};
  std::string text;
  bool newline{};
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const OutputEvent&, const OutputEvent&) = default;
};

struct ReportEvent {
  fsim::runtime::simir::ProcessId process{};
  std::string message;
  fsim::runtime::simir::AssertionSeverity severity{
      fsim::runtime::simir::AssertionSeverity::note};
  fsim::runtime::simir::SourceLocation source;
  fsim::runtime::SimulationTick time{};
  std::uint64_t delta{};

  friend bool operator==(const ReportEvent&, const ReportEvent&) = default;
};

struct Capture {
  fsim::runtime::RunResult result;
  std::vector<OutputEvent> output;
  std::vector<ReportEvent> reports;
  std::size_t compiled_processes{};
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

Capture execute(
    fsim::app::BuiltProject project,
    const fsim::app::SimulationEngine engine) {
  fsim::app::Simulation simulation{
      std::move(project), 1000, engine};
  Capture capture;
  capture.compiled_processes = simulation.compiled_process_count();
  simulation.set_output_hook(
      [&capture](
          const fsim::runtime::simir::ProcessId process,
          const std::string_view text,
          const bool newline,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        capture.output.push_back(
            {process, std::string{text}, newline, time, delta});
      });
  simulation.set_report_hook(
      [&capture](
          const fsim::runtime::simir::ProcessId process,
          const std::string_view message,
          const fsim::runtime::simir::AssertionSeverity severity,
          const fsim::runtime::simir::SourceLocation& source,
          const fsim::runtime::SimulationTick time,
          const std::uint64_t delta) {
        capture.reports.push_back(
            {
                process,
                std::string{message},
                severity,
                source,
                time,
                delta});
      });
  capture.result = simulation.run();
  return capture;
}

void test_display(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "display-test";
  config.project.top = "sv:work.display_test";
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

  fsim::diagnostic::Engine diagnostics;
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  if (!reference_project || !compiled_project) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
      std::cerr << diagnostic.code << ": "
                << diagnostic.message << '\n';
    }
  }
  assert(reference_project && compiled_project);

  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  assert(reference.result.status == fsim::runtime::RunStatus::stopped);
  assert(compiled.result.status == fsim::runtime::RunStatus::stopped);
  assert(reference.result.time == 2);
  assert(compiled.result.time == 2);
  assert(reference.output == compiled.output);
  assert(reference.output.size() == 17);
  assert(reference.output[0].process == 0);
  assert(reference.output[0].text == "first\t");
  assert(!reference.output[0].newline);
  assert(reference.output[0].time == 0);
  assert(reference.output[0].delta == 0);
  assert(reference.output[1].text == "+line\nembedded \"quote\" \\ A");
  assert(reference.output[1].newline);
  assert(reference.output[1].time == 0);
  assert(reference.output[2].text == "42");
  assert(reference.output[2].newline);
  assert(reference.output[2].time == 0);
  assert(reference.output[2].delta == 0);
  assert(reference.output[3].text == "-1");
  assert(reference.output[3].newline);
  assert(reference.output[3].time == 0);
  assert(reference.output[3].delta == 0);
  assert(reference.output[4].text == "q=%:10xz!");
  assert(reference.output[4].newline);
  assert(reference.output[4].time == 0);
  assert(reference.output[4].delta == 0);
  assert(reference.output[5].text == "[10xz]");
  assert(!reference.output[5].newline);
  assert(reference.output[5].time == 0);
  assert(reference.output[5].delta == 0);
  assert(reference.output[6].text == "h=x");
  assert(reference.output[6].newline);
  assert(reference.output[6].time == 0);
  assert(reference.output[7].text == "o=245");
  assert(reference.output[7].newline);
  assert(reference.output[7].time == 0);
  assert(reference.output[8].text == "d=165");
  assert(reference.output[8].newline);
  assert(reference.output[8].time == 0);
  assert(reference.output[9].text == "s=-1");
  assert(reference.output[9].newline);
  assert(reference.output[9].time == 0);
  assert(reference.output[10].text == "u=x");
  assert(reference.output[10].newline);
  assert(reference.output[10].time == 0);
  assert(reference.output[11].text == "c=A");
  assert(reference.output[11].newline);
  assert(reference.output[11].time == 0);
  assert(reference.output[12].text == "post=10xz");
  assert(reference.output[12].newline);
  assert(reference.output[12].time == 0);
  assert(reference.output[13].text == "monitored");
  assert(reference.output[13].newline);
  assert(reference.output[13].time == 0);
  assert(reference.output[14].text == "second");
  assert(!reference.output[14].newline);
  assert(reference.output[14].time == 2);
  assert(reference.output[15].text.empty());
  assert(!reference.output[15].newline);
  assert(reference.output[15].time == 2);
  assert(reference.output[16].text.empty());
  assert(reference.output[16].newline);
  assert(reference.output[16].time == 2);
  assert(reference.compiled_processes == 0);
  assert(compiled.compiled_processes == 1);
}

void test_vhdl_report(
    const std::filesystem::path& directory,
    const std::filesystem::path& source,
    const fsim::project::Optimization optimization) {
  fsim::project::Config config;
  config.base_directory = directory;
  config.project.name = "report-test";
  config.project.top = "vhdl:work.reporter(rtl)";
  config.project.time_resolution = "1ns";
  config.build.optimization = optimization;
  config.build.cache_path =
      directory
      / (optimization == fsim::project::Optimization::o0
             ? "report-cache-o0"
             : "report-cache-o2");
  config.run.max_deltas = 1000;

  fsim::project::SourceSet sources;
  sources.language = fsim::project::Language::vhdl;
  sources.standard = "2008";
  sources.library = "work";
  sources.files.push_back(source);
  config.source_sets.push_back(std::move(sources));

  fsim::diagnostic::Engine diagnostics;
  auto reference_project =
      fsim::app::build_project(config, diagnostics);
  auto compiled_project =
      fsim::app::build_project(config, diagnostics);
  assert(reference_project && compiled_project);
  const auto reference = execute(
      std::move(*reference_project),
      fsim::app::SimulationEngine::interpreter);
  const auto compiled = execute(
      std::move(*compiled_project),
      fsim::app::SimulationEngine::compiled);
  assert(reference.result.status == fsim::runtime::RunStatus::completed);
  assert(compiled.result.status == fsim::runtime::RunStatus::completed);
  assert(reference.output == compiled.output);
  assert(reference.output.empty());
  assert(reference.reports == compiled.reports);
  assert(reference.reports.size() == 3);
  assert(reference.reports[0].message == "vhdl \"quote\"");
  assert(
      reference.reports[0].severity
      == fsim::runtime::simir::AssertionSeverity::note);
  assert(reference.reports[0].source.path == source.string());
  assert(reference.reports[0].source.line == 8);
  assert(reference.reports[0].time == 0);
  assert(reference.reports[0].delta == 0);
  assert(reference.reports[1].message.empty());
  assert(
      reference.reports[1].severity
      == fsim::runtime::simir::AssertionSeverity::warning);
  assert(
      reference.reports[2].severity
      == fsim::runtime::simir::AssertionSeverity::error);
  assert(reference.compiled_processes == 0);
  assert(compiled.compiled_processes == 1);
}

}  // namespace

int main() {
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  TemporaryDirectory directory{
      std::filesystem::temp_directory_path()
      / ("fsim-display-test-" + std::to_string(nonce))};
  std::filesystem::create_directories(directory.path);
  const auto source = directory.path / "display_test.sv";
  {
    std::ofstream output(source);
    output << R"(
module display_test;
  logic [3:0] q;
  logic [7:0] n;
  logic signed [7:0] s;
  initial begin
    q = 4'b10xz;
    n = 8'd165;
    s = 8'hff;
    $write("first\t");
    $display("+line\nembedded \"quote\" \\ \101");
    $display(8'h2a);
    $display(8'shff);
    $display("q=%%:%b!", q);
    $write("[%b]", q);
    $display("h=%h", q);
    $display("o=%o", n);
    $display("d=%d", n);
    $display("s=%d", s);
    $display("u=%d", q);
    $display("c=%c", 8'd65);
    $strobe("post=%b", q);
    q = 4'b0011;
    $monitor("monitored");
    #2 $write("second");
    $write;
    $display;
    $finish;
  end
endmodule
)";
  }

  const auto report_source = directory.path / "report.vhd";
  {
    std::ofstream output(report_source);
    output << R"(
entity reporter is
end entity;
architecture rtl of reporter is
begin
  process
  begin
    report "vhdl ""quote""" severity note;
    report "" severity warning;
    report "error" severity error;
    wait;
  end process;
end architecture;
)";
  }

  const auto manifest = directory.path / "fsim.toml";
  {
    std::ofstream output(manifest);
    output
        << "schema = 1\n"
        << "[project]\n"
        << "name = \"display-test\"\n"
        << "top = \"sv:work.display_test\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[source_set]]\n"
        << "language = \"systemverilog\"\n"
        << "standard = \"2017\"\n"
        << "library = \"work\"\n"
        << "files = [\"display_test.sv\"]\n"
        << "[build]\n"
        << "optimization = \"O2\"\n"
        << "cache_path = \"cli-cache\"\n"
        << "[run]\n"
        << "max_deltas = 1000\n";
  }
  {
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const auto result = run_cli(
        {"fsim", "run", "-p", manifest.string()},
        input,
        output,
        error);
    assert(result == 0);
    assert(error.str().empty());
    assert(
        output.str().find(
            "first\t+line\nembedded \"quote\" \\ A\n"
            "42\n-1\nq=%:10xz!\n[10xz]h=x\no=245\n"
            "d=165\ns=-1\nu=x\nc=A\npost=10xz\n"
            "monitored\nsecond\n"
            "simulation stopped at tick 2")
        != std::string::npos);
  }

  test_display(
      directory.path, source, fsim::project::Optimization::o0);
  test_display(
      directory.path, source, fsim::project::Optimization::o2);
  test_vhdl_report(
      directory.path,
      report_source,
      fsim::project::Optimization::o0);
  test_vhdl_report(
      directory.path,
      report_source,
      fsim::project::Optimization::o2);
  std::cout << "display application tests passed\n";
}
