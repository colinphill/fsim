// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
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
        << "schema = 1\n"
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
if {[fsim::debug scopes] ne "(no child scopes)"} {
  error "bad child scopes"
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
    std::ifstream trace(directory / "debug.vcd", std::ios::binary);
    assert(trace);
    std::ostringstream contents;
    contents << trace.rdbuf();
    assert(contents.str().find("$timescale 1ns $end") != std::string::npos);
    assert(contents.str().find("$var") != std::string::npos);
    assert(contents.str().find("q $end") != std::string::npos);
  }

  std::error_code remove_error;
  std::filesystem::remove_all(directory, remove_error);
  assert(!remove_error);
  return 0;
}
