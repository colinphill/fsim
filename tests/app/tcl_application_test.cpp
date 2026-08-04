// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int run_cli(
    const std::vector<std::string>& arguments,
    std::istream& input,
    std::ostream& output,
    std::ostream& error) {
  std::vector<const char*> raw_arguments;
  raw_arguments.reserve(arguments.size());
  for (const auto& argument : arguments) {
    raw_arguments.push_back(argument.c_str());
  }
  return fsim::cli::run(
      static_cast<int>(raw_arguments.size()),
      raw_arguments.data(),
      fsim::app::make_cli_services(input),
      output,
      error);
}

}  // namespace

int main() {
  // FSIM-CONFORMANCE CF-COMMON-TCL-001 source=SRC-TCL expectation=execute
  // FSIM-CONFORMANCE CF-COMMON-TCL-N01 source=SRC-TCL expectation=reject
  {
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            "-c",
            "puts [fsim::version]",
            "-c",
            "if {[info tclversion] ne \"9.0\" || "
            "[package vcompare [info patchlevel] 9.0.4] < 0} "
            "{error \"unsupported Tcl [info patchlevel]\"}",
            "-c",
            "if {$argc != 0 || $tcl_interactive} {error bad-arguments}",
        },
        input,
        output,
        error);
    assert(result == 0);
    assert(output.str().find("0.1.0-dev (C API 1)") != std::string::npos);
    assert(error.str().empty());
  }

  const auto suffix =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto directory = std::filesystem::temp_directory_path()
      / ("fsim-tcl-test-" + std::to_string(suffix));
  std::filesystem::create_directories(directory);
  const auto display_source = directory / "display.sv";
  const auto display_manifest = directory / "display.toml";
  {
    std::ofstream file(display_source);
    file
        << "module display;\n"
        << "  initial begin\n"
        << "    $display(\"tcl-hdl-output\");\n"
        << "    $finish;\n"
        << "  end\n"
        << "endmodule\n";
  }
  {
    std::ofstream file(display_manifest);
    file
        << "schema = 2\n"
        << "[project]\n"
        << "name = \"tcl-display\"\n"
        << "top = \"sv:work.display\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[source_set]]\n"
        << "language = \"systemverilog\"\n"
        << "standard = \"2017\"\n"
        << "library = \"work\"\n"
        << "files = [\"display.sv\"]\n"
        << "[build]\n"
        << "optimization = \"O2\"\n"
        << "cache_path = \"display-cache\"\n"
        << "[run]\n"
        << "max_deltas = 1000\n";
  }
  {
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            "-p",
            display_manifest.string(),
            "-c",
            "fsim::run",
        },
        input,
        output,
        error);
    assert(result == 0);
    assert(
        output.str().find("tcl-hdl-output\n")
        != std::string::npos);
    assert(error.str().empty());
  }
  const auto mapped_artifact = directory / "tcl-display.fsimlib";
  fsim::diagnostic::Engine mapped_export_diagnostics;
  const auto mapped_export_config = fsim::project::load(
      display_manifest, mapped_export_diagnostics);
  assert(mapped_export_config);
  const auto mapped_exported = fsim::app::export_library(
      *mapped_export_config, "work", mapped_artifact,
      mapped_export_diagnostics);
  if (!mapped_exported) {
    fsim::diagnostic::print_text(std::cerr, mapped_export_diagnostics);
  }
  assert(mapped_exported);
  const auto mapped_manifest = directory / "mapped-display.toml";
  {
    std::ofstream file(mapped_manifest);
    file
        << "schema = 2\n"
        << "[project]\n"
        << "name = \"tcl-mapped-display\"\n"
        << "top = \"sv:work.display\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[library_map]]\n"
        << "library = \"work\"\n"
        << "path = \"" << mapped_artifact.string() << "\"\n"
        << "[build]\n"
        << "cache_path = \"mapped-display-cache\"\n";
  }
  {
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim", "tcl", "-p", mapped_manifest.string(),
            "-c", R"tcl(
set project [fsim::project]
set mappings [dict get $project library_mappings]
if {[llength $mappings] != 1} {error "bad mapping count"}
if {[dict get [lindex $mappings 0] library] ne "work"} {
  error "bad mapping library"
}
set built [fsim::build]
set selected [dict get $built mapped_libraries]
if {[llength $selected] != 1} {error "bad selected count"}
set mapped [lindex $selected 0]
if {[dict get $mapped library] ne "work" ||
    [dict get $mapped units] != 1} {
  error "bad mapped provenance"
}
if {[dict get $mapped native_accepted] ni {0 1}} {
  error "bad native admission value"
}
)tcl",
        },
        input, output, error);
    if (result != 0) {
      std::cerr << error.str();
    }
    assert(result == 0);
    assert(error.str().empty());
  }
  const auto script = directory / "arguments.tcl";
  {
    std::ofstream file(script);
    file
        << "if {$argc != 2} {error \"wrong argc $argc\"}\n"
        << "if {[lindex $argv 0] ne \"alpha\"} {error \"bad argv\"}\n"
        << "if {[lindex $argv 1] ne \"two words\"} {error \"bad argv\"}\n"
        << "if {$tcl_interactive} {error \"script marked interactive\"}\n"
        << "if {[file normalize $argv0] ne [file normalize [info script]]} "
           "{error \"bad argv0\"}\n"
        << "puts \"script-ok\"\n"
        << "exit 7\n";
  }
  {
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            script.string(),
            "alpha",
            "two words",
        },
        input,
        output,
        error);
    assert(result == 7);
    assert(output.str().find("script-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    std::istringstream input{
        "set message {\n"
        "hello from Tcl\n"
        "}\n"
        "set message\n"
        "exit 0\n"};
    std::ostringstream output;
    std::ostringstream error;
    const int result =
        run_cli({"fsim", "tcl"}, input, output, error);
    assert(result == 0);
    assert(output.str().find("(fsim:tcl) ") != std::string::npos);
    assert(output.str().find("... ") != std::string::npos);
    assert(output.str().find("hello from Tcl") != std::string::npos);
    assert(error.str().empty());
  }
  {
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {"fsim", "tcl", "-c", "error deliberate"},
        input,
        output,
        error);
    assert(result == 1);
    assert(
        error.str().find("error[FSIM-TCL-0003]")
        != std::string::npos);
    assert(error.str().find("deliberate") != std::string::npos);
  }
  {
    const std::string diagnostic_script = R"FSIM_TCL(
if {![catch {fsim::check} check_error]} {
  error "empty project unexpectedly passed"
}
set diagnostics [fsim::diagnostics]
if {[llength $diagnostics] != 1} {
  error "missing project diagnostic: $diagnostics"
}
set diagnostic [lindex $diagnostics 0]
if {[dict get $diagnostic severity] ne "error" ||
    [dict get $diagnostic code] ne "FSIM-FE-0001" ||
    [dict get $diagnostic message] eq ""} {
  error "malformed diagnostic dictionary: $diagnostic"
}
fsim::diagnostics clear
if {[llength [fsim::diagnostics]] != 0} {
  error "diagnostics were not cleared"
}
puts "diagnostics-ok"
)FSIM_TCL";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {"fsim", "tcl", "-c", diagnostic_script},
        input,
        output,
        error);
    assert(result == 0);
    assert(output.str().find("diagnostics-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    std::istringstream input{"set unfinished {\n"};
    std::ostringstream output;
    std::ostringstream error;
    const int result =
        run_cli({"fsim", "tcl"}, input, output, error);
    assert(result == 1);
    assert(
        error.str().find("error[FSIM-TCL-0004]")
        != std::string::npos);
  }

  const auto source = directory / "control.sv";
  {
    std::ofstream file(source);
    file
        << "module tb;\n"
        << "  logic q;\n"
        << "  initial begin\n"
        << "    q = 1'b0;\n"
        << "    #1 q = 1'b1;\n"
        << "    #1 $finish;\n"
        << "  end\n"
        << "endmodule\n";
  }
  const auto manifest = directory / "fsim.toml";
  {
    std::ofstream file(manifest);
    file
        << "schema = 2\n"
        << "[project]\n"
        << "name = \"tcl-control\"\n"
        << "top = \"sv:work.tb\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[source_set]]\n"
        << "language = \"systemverilog\"\n"
        << "standard = \"2017\"\n"
        << "library = \"work\"\n"
        << "files = [\"control.sv\"]\n"
        << "[build]\n"
        << "optimization = \"O2\"\n"
        << "cache_path = \"cache\"\n"
        << "[run]\n"
        << "max_deltas = 100000\n"
        << "trace_file = \"debug.vcd\"\n";
  }
  const auto assertion_source = directory / "assertion.sv";
  {
    std::ofstream file(assertion_source);
    file
        << "module assertion_tb;\n"
        << "  initial begin\n"
        << "    assert (1'b0) else $fatal(\"tcl assertion\");\n"
        << "  end\n"
        << "endmodule\n";
  }
  const auto assertion_manifest = directory / "assertion.toml";
  {
    std::ofstream file(assertion_manifest);
    file
        << "schema = 2\n"
        << "[project]\n"
        << "name = \"tcl-assertion\"\n"
        << "top = \"sv:work.assertion_tb\"\n"
        << "time_resolution = \"1ns\"\n"
        << "[[source_set]]\n"
        << "language = \"systemverilog\"\n"
        << "standard = \"2017\"\n"
        << "library = \"work\"\n"
        << "files = [\"assertion.sv\"]\n"
        << "[build]\n"
        << "optimization = \"O2\"\n"
        << "cache_path = \"assertion-cache\"\n"
        << "[run]\n"
        << "max_deltas = 100000\n";
  }
  {
    const std::string control_script = R"(
set project [fsim::project]
if {[dict get $project name] ne "tcl-control"} {error "bad project"}
if {[dict get $project top] ne "sv:work.tb"} {error "bad top"}
if {[llength [fsim::diagnostics]] != 0} {
  error "unexpected initial diagnostics"
}
if {[fsim::diagnostics clear] != 0} {
  error "diagnostic clear failed"
}
set checked [fsim::check]
if {[dict get $checked sources] != 1} {error "bad source count"}
set built [fsim::build]
if {[dict get $built signals] != 1} {error "bad signal count"}
if {[lsearch -exact [fsim::signals] "tb.q"] < 0} {error "missing signal"}
if {[fsim::deposit tb.q 0] ne "0"} {error "deposit failed"}
if {[fsim::force tb.q 1] ne "1"} {error "force failed"}
if {[fsim::deposit tb.q 0] ne "1"} {error "force mask failed"}
if {[fsim::release tb.q] ne "0"} {error "release failed"}
set first [fsim::run 1ns]
if {[dict get $first status] ne "time_limit"} {error "bad first run"}
if {[dict get [fsim::status] time] != 1} {error "bad status time"}
if {[fsim::read tb.q] ne "1"} {error "bad tick-1 value"}
set final [fsim::run]
if {[dict get $final time] != 2} {error "bad final time"}
if {[dict get [fsim::status] state] ne "finished"} {error "not finished"}
if {[fsim::stop] ne "stop_requested"} {error "stop request failed"}
if {![catch {fsim::debug where} mode_error]} {
  error "debug accepted a compiled-mode session"
}
if {[string first "different execution mode" $mode_error] < 0} {
  error "bad execution-mode diagnostic: $mode_error"
}
puts "control-ok"
)";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            "-p",
            manifest.string(),
            "-c",
            control_script,
        },
        input,
        output,
        error);
    assert(result == 0);
    assert(output.str().find("control-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    const std::string debug_script = R"FSIM_TCL(
set where [fsim::debug where]
if {![string match "time 0, delta 0, scope tb" $where]} {
  error "bad initial location: $where"
}
if {[fsim::debug scope] ne "tb"} {error "bad current scope"}
set child_scopes [fsim::debug scopes]
if {$child_scopes ne "tb.process_0"} {
  error "bad child scopes: $child_scopes"
}
set initial_signals [fsim::debug signals]
if {![string match "*tb.q = *" $initial_signals]} {
  error "missing debug signal: $initial_signals"
}
if {[fsim::trace list] ne "tb.q"} {error "bad initial trace list"}
if {[fsim::trace clear] ne "cleared trace selection"} {
  error "trace clear failed"
}
if {[fsim::trace list] ne "(no traced signals)"} {
  error "trace was not cleared"
}
if {[fsim::trace add tb.q] ne "tracing tb.q"} {
  error "trace add failed"
}
if {[fsim::trace list] ne "tb.q"} {error "trace add not retained"}
if {[fsim::trace remove tb.q] ne "stopped tracing tb.q"} {
  error "trace remove failed"
}
if {[fsim::trace all] ne "tracing all signals"} {
  error "trace all failed"
}
fsim::debug deposit tb.q 0
if {[fsim::debug show tb.q] ne "tb.q = 0"} {
  error "debug deposit failed"
}
fsim::debug force tb.q 1
if {[fsim::debug show tb.q] ne "tb.q = 1 (forced)"} {
  error "debug force failed"
}
fsim::debug release tb.q
if {[fsim::debug show tb.q] ne "tb.q = 0"} {
  error "debug release failed"
}
if {![string match "breakpoint 1 set at time 1*" \
          [fsim::debug break time 1ns]]} {
  error "time breakpoint failed"
}
if {![string match "breakpoint 2 set at *control.sv:4" \
          [fsim::debug break source control.sv:4]]} {
  error "source breakpoint failed"
}
if {[fsim::debug break signal tb.q == 1] ne \
        "breakpoint 3 set on tb.q == 1"} {
  error "signal breakpoint failed"
}
set breakpoints [fsim::debug breakpoints]
foreach expected {"1: time 1 ticks" "2: source control.sv:4" \
                  "3: signal tb.q == 1"} {
  if {[string first $expected $breakpoints] < 0} {
    error "missing breakpoint: $expected"
  }
}
if {[fsim::debug delete 2] ne "deleted breakpoint 2"} {
  error "breakpoint deletion failed"
}
if {[fsim::debug clear] ne "cleared all breakpoints"} {
  error "breakpoint clear failed"
}
if {[fsim::debug breakpoints] ne "no breakpoints"} {
  error "breakpoints were not cleared"
}
set relative [fsim::debug run 1ns]
if {[string first "stopped at time 1" $relative] < 0} {
  error "relative debug run failed: $relative"
}
set absolute [fsim::debug run-until 2ns]
if {[string first "simulation finished at time 2" $absolute] < 0} {
  error "absolute debug run failed: $absolute"
}

# Rebuilding deliberately starts a fresh debugger session for step coverage.
fsim::build
set statement [fsim::debug step statement]
if {[string first "process tb." $statement] < 0} {
  error "statement step failed: $statement"
}
set locals [fsim::debug locals]
if {$locals ne "(no locals)"} {error "bad locals result: $locals"}
set process [fsim::debug step process]
if {[string first "stopped at time 0" $process] < 0} {
  error "process step failed: $process"
}
set delta [fsim::debug step delta]
if {[string first "stopped at time 0" $delta] < 0} {
  error "delta step failed: $delta"
}
set time [fsim::debug step time]
if {[string first "time 1" $time] < 0} {
  error "time step failed: $time"
}
puts "debug-control-ok"
)FSIM_TCL";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            "-p",
            manifest.string(),
            "-c",
            debug_script,
        },
        input,
        output,
        error);
    if (result != 0) {
      throw std::runtime_error(
          "Tcl debugger test failed:\n" + error.str()
          + "\nTcl output:\n" + output.str());
    }
    assert(
        output.str().find("debug-control-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    const std::string callback_script = R"FSIM_TCL(
set ::lifecycle_events {}
set ::value_events {}
set ::safe_points 0
proc record_lifecycle {event} {
  lappend ::lifecycle_events $event
}
proc record_value {path value time delta} {
  lappend ::value_events [list $path $value $time $delta]
}
proc stop_at_tick_one {time delta phase} {
  incr ::safe_points
  if {$time >= 1} {
    fsim::stop
  }
}
if {[fsim::on lifecycle record_lifecycle] ne "lifecycle"} {
  error "lifecycle registration failed"
}
fsim::on value_change record_value
fsim::on safe_point stop_at_tick_one
set registered [fsim::callbacks]
if {[dict get $registered lifecycle] ne "record_lifecycle"} {
  error "bad lifecycle callback listing"
}
if {[dict get $registered value_change] ne "record_value"} {
  error "bad value callback listing"
}
set stopped [fsim::run]
if {[dict get $stopped status] ne "stopped"} {
  error "callback stop did not stop simulation: $stopped"
}
if {[dict get $stopped time] != 1} {
  error "callback stopped at wrong time: $stopped"
}
if {$::safe_points == 0} {error "safe-point callback did not run"}
if {[lsearch -exact $::lifecycle_events "started"] < 0 ||
    [lsearch -exact $::lifecycle_events "stopped"] < 0} {
  error "missing lifecycle events: $::lifecycle_events"
}
if {[llength $::value_events] == 0} {
  error "value-change callback did not run"
}
if {[fsim::off safe_point] ne "safe_point"} {
  error "callback removal failed"
}
if {[dict get [fsim::callbacks] safe_point] ne ""} {
  error "safe-point callback still registered"
}
set finished [fsim::run]
if {[dict get $finished time] != 2 ||
    [dict get [fsim::status] state] ne "finished"} {
  error "callback resume failed: $finished"
}
if {[lsearch -exact $::lifecycle_events "finished"] < 0} {
  error "missing finished lifecycle event"
}
puts "callbacks-ok"
)FSIM_TCL";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            "-p",
            manifest.string(),
            "-c",
            callback_script,
        },
        input,
        output,
        error);
    if (result != 0) {
      throw std::runtime_error(
          "Tcl callback test failed:\n" + error.str()
          + "\nTcl output:\n" + output.str());
    }
    assert(output.str().find("callbacks-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    const std::string callback_error_script = R"FSIM_TCL(
proc fail_callback {time delta phase} {
  error "deliberate callback failure"
}
fsim::on safe_point fail_callback
if {![catch {fsim::run} callback_error]} {
  error "callback failure escaped containment"
}
if {[string first "Tcl safe_point callback failed" $callback_error] < 0 ||
    [string first "deliberate callback failure" $callback_error] < 0} {
  error "bad callback diagnostic: $callback_error"
}
puts "callback-error-ok"
)FSIM_TCL";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            "-p",
            manifest.string(),
            "-c",
            callback_error_script,
        },
        input,
        output,
        error);
    if (result != 0) {
      throw std::runtime_error(
          "Tcl callback containment test failed:\n" + error.str()
          + "\nTcl output:\n" + output.str());
    }
    assert(
        output.str().find("callback-error-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    const std::string project_script =
        "set control_manifest {"
        + manifest.string()
        + "}\nset assertion_manifest {"
        + assertion_manifest.string()
        + R"FSIM_TCL(}
if {![catch {fsim::project load definitely-missing.toml} load_error]} {
  error "missing project unexpectedly loaded"
}
if {[dict get [fsim::project] name] ne "tcl"} {
  error "failed load mutated the current project"
}
set loaded [fsim::project load $control_manifest]
if {[dict get $loaded name] ne "tcl-control"} {
  error "valid project load failed"
}
set trace_path [fsim::trace configure runtime-debug.vcd tb.q]
set trace_status [fsim::trace status]
if {[dict get $trace_status file] ne $trace_path ||
    [dict get $trace_status filters] ne "tb.q"} {
  error "bad runtime trace configuration: $trace_status"
}
if {[fsim::trace list] ne "tb.q"} {
  error "runtime trace filter was not applied"
}
if {![catch {fsim::trace configure too-late.vcd} trace_error]} {
  error "live trace reconfiguration unexpectedly succeeded"
}
set replaced [fsim::project load $assertion_manifest]
if {[dict get $replaced name] ne "tcl-assertion" ||
    [dict get [fsim::status] state] ne "unbuilt"} {
  error "project replacement did not reset the live session"
}
if {[dict get [fsim::trace status] file] ne ""} {
  error "replacement project retained old trace configuration"
}
set control_again [fsim::project load $control_manifest]
if {[dict get $control_again name] ne "tcl-control"} {
  error "second project replacement failed"
}
puts "project-load-ok"
)FSIM_TCL";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {"fsim", "tcl", "-c", project_script},
        input,
        output,
        error);
    if (result != 0) {
      throw std::runtime_error(
          "Tcl project-load test failed:\n" + error.str()
          + "\nTcl output:\n" + output.str());
    }
    assert(output.str().find("project-load-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    const std::string assertion_script =
        "set assertion_manifest {"
        + assertion_manifest.string()
        + R"FSIM_TCL(}
set ::assertion_event {}
set ::assertion_lifecycle {}
proc record_assertion {tag process severity message path line column} {
  set ::assertion_event \
      [list $tag $process $severity $message $path $line $column]
}
proc record_assertion_lifecycle {event} {
  lappend ::assertion_lifecycle $event
}
fsim::on assertion {record_assertion tagged}
fsim::on lifecycle record_assertion_lifecycle
if {[dict get [fsim::callbacks] assertion] ne \
        "record_assertion tagged"} {
  error "assertion command prefix was not retained"
}
fsim::project load $assertion_manifest
if {![catch {fsim::run} assertion_error]} {
  error "failing assertion did not fail the Tcl run"
}
if {[string first "tcl assertion" $assertion_error] < 0} {
  error "bad assertion run error: $assertion_error"
}
if {[llength $::assertion_event] != 7 ||
    [lindex $::assertion_event 0] ne "tagged" ||
    [lindex $::assertion_event 2] ne "failure" ||
    [string first "tcl assertion" [lindex $::assertion_event 3]] < 0 ||
    [lindex $::assertion_event 5] != 3} {
  error "bad assertion callback metadata: $::assertion_event"
}
if {[lsearch -exact $::assertion_lifecycle "started"] < 0 ||
    [lsearch -exact $::assertion_lifecycle "stopped"] < 0} {
  error "bad assertion lifecycle: $::assertion_lifecycle"
}
set diagnostics [fsim::diagnostics]
set last [lindex $diagnostics end]
if {[dict get $last code] ne "FSIM-TCL-REPORT-0001" ||
    [dict get $last line] != 3 ||
    [dict get $last severity] ne "fatal"} {
  error "bad assertion diagnostic: $last"
}
if {[dict get [fsim::status] state] ne "poisoned"} {
  error "assertion did not poison the failed session"
}
puts "assertion-callback-ok"
)FSIM_TCL";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {"fsim", "tcl", "-c", assertion_script},
        input,
        output,
        error);
    if (result != 0) {
      throw std::runtime_error(
          "Tcl assertion callback test failed:\n" + error.str()
          + "\nTcl output:\n" + output.str());
    }
    assert(
        output.str().find("assertion-callback-ok")
        != std::string::npos);
    assert(
        error.str().find("fatal[FSIM-TCL-REPORT-0001]")
        != std::string::npos);
    assert(error.str().find("tcl assertion") != std::string::npos);
  }
  {
    const std::string debug_callback_script = R"FSIM_TCL(
set ::debug_safe_points 0
proc stop_debug_at_one {time delta phase} {
  incr ::debug_safe_points
  if {$time >= 1} {
    fsim::stop
  }
}
fsim::on safe_point stop_debug_at_one
set stopped [fsim::debug run]
if {[string first "stopped at time 1" $stopped] < 0 ||
    $::debug_safe_points == 0} {
  error "debugger replaced the Tcl safe-point callback: $stopped"
}
fsim::off safe_point
set finished [fsim::debug run]
if {[string first "simulation finished" $finished] < 0 ||
    [string first "time 2" $finished] < 0} {
  error "debugger did not resume after callback stop: $finished"
}
puts "debug-callback-ok"
)FSIM_TCL";
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        {
            "fsim",
            "tcl",
            "-p",
            manifest.string(),
            "-c",
            debug_callback_script,
        },
        input,
        output,
        error);
    if (result != 0) {
      throw std::runtime_error(
          "Tcl debugger callback test failed:\n" + error.str()
          + "\nTcl output:\n" + output.str());
    }
    assert(
        output.str().find("debug-callback-ok") != std::string::npos);
    assert(error.str().empty());
  }
  {
    std::ifstream trace(directory / "debug.vcd", std::ios::binary);
    assert(trace);
    std::ostringstream contents;
    contents << trace.rdbuf();
    assert(contents.str().find("$timescale 1ns $end") != std::string::npos);
    assert(contents.str().find("$var") != std::string::npos);
    assert(contents.str().find("q $end") != std::string::npos);
  }

  std::error_code remove_error;
  for (std::filesystem::recursive_directory_iterator iterator(
           directory, remove_error), end;
       !remove_error && iterator != end;
       iterator.increment(remove_error)) {
    std::filesystem::permissions(
        iterator->path(), std::filesystem::perms::owner_all,
        std::filesystem::perm_options::add, remove_error);
  }
  remove_error.clear();
  std::filesystem::remove_all(directory, remove_error);
  assert(!remove_error);
  return 0;
}
