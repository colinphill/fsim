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
#include <string_view>
#include <vector>

namespace {

int run_cli(
    const std::vector<std::string>& arguments,
    std::istream& input,
    std::ostream& output,
    std::ostream& error)
{
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

void require_cli(const std::vector<std::string>& arguments)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    if (run_cli(arguments, input, output, error) != 0) {
        throw std::runtime_error(
            "debugger fixture command failed: " + error.str());
    }
}

struct WorkingDirectory {
    std::filesystem::path previous { std::filesystem::current_path() };

    explicit WorkingDirectory(const std::filesystem::path& path)
    {
        std::filesystem::current_path(path);
    }

    ~WorkingDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous, error);
    }
};

void run_script(
    const std::string& snapshot,
    const std::string& script,
    const std::string_view success_marker)
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        { "fsim", "tcl", "--snapshot", snapshot, "-c", script },
        input,
        output,
        error);
    if (result != 0) {
        std::cerr << "Tcl debugger test failed:\n"
                  << error.str()
                  << "\nTcl output:\n"
                  << output.str();
    }
    assert(result == 0);
    assert(output.str().find(success_marker) != std::string::npos);
    if (!error.str().empty()) {
        std::cerr << "Unexpected Tcl debugger stderr:\n"
                  << error.str();
    }
    assert(error.str().empty());
}

} // namespace

int main()
{
    // FSIM-CONFORMANCE CF-COMMON-TCL-DEBUG-001 source=SRC-TCL expectation=execute
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("fsim-tcl-debugger-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);
    WorkingDirectory working_directory(directory);

    const auto verilog_source = directory / "debug-top.sv";
    {
        std::ofstream file(verilog_source);
        file
            << "module debug_top; timeunit 1ns; timeprecision 1ns;\n"
            << "  logic q;\n"
            << "  initial begin\n"
            << "    q = 1'b0;\n"
            << "    #1 q = 1'b1;\n"
            << "    #1 $finish;\n"
            << "  end\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", verilog_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.debug_top",
        "--snapshot", "debug-top" });
    if (!std::filesystem::remove(verilog_source)) {
        throw std::runtime_error("debugger HDL source was not removed");
    }

    run_script("debug-top", R"tcl(
set initial [fsim::debug status]
if {[dict get $initial time] != 0 || [dict get $initial delta] != 0 ||
    [dict get $initial scope] ne "debug_top" ||
    [dict get $initial finished]} {
  error "bad structured debugger status: $initial"
}

set breakpoint [fsim::debug break add time 1ns]
set breakpoint_id [dict get $breakpoint id]
if {[dict get $breakpoint kind] ne "time" ||
    [llength [fsim::debug break list]] != 1} {
  error "breakpoint add/list failed: $breakpoint"
}
fsim::debug break delete $breakpoint_id
if {[llength [fsim::debug break list]] != 0} {
  error "breakpoint delete failed"
}

set watch [fsim::debug watch add debug_top.q]
set watch_id [dict get $watch id]
if {[dict get $watch kind] ne "watch" ||
    [llength [fsim::debug watch list]] != 1} {
  error "watch add/list failed: $watch"
}
fsim::debug watch delete $watch_id
if {[llength [fsim::debug watch list]] != 0} {
  error "watch delete failed"
}

set conditional [fsim::debug break add signal debug_top.q == 0]
if {[dict get $conditional comparison] ne "==" ||
    [dict get $conditional value] ne "0"} {
  error "signal breakpoint condition was lost: $conditional"
}
fsim::debug break clear

set scope [fsim::debug scope]
if {[dict get $scope path] ne "debug_top"} {
  error "bad structured scope: $scope"
}
set direct [fsim::debug inspect debug_top.q]
if {[dict get $direct kind] ne "signal" ||
    [dict get $direct path] ne "debug_top.q" ||
    [dict get $direct width] != 1} {
  error "bad structured signal inspection: $direct"
}
set q_ref [fsim::object resolve debug_top.q]
set through_reference [fsim::debug inspect $q_ref]
if {[dict get $through_reference path] ne "debug_top.q"} {
  error "loaded-object reference inspection failed"
}

set stepped [fsim::debug step statement]
if {![dict exists $stepped time] || ![dict exists $stepped stop_reason]} {
  error "step did not return structured status: $stepped"
}
set frames [fsim::debug frames]
if {[llength $frames] != 1} {error "expected one current frame: $frames"}
set frame [fsim::debug frame]
if {[dict get $frame index] != 0 ||
    ![dict exists $frame source_path] ||
    ![dict exists $frame locals]} {
  error "bad structured frame: $frame"
}

set records [fsim::debug provenance]
if {[llength $records] != 1} {error "bad debugger provenance: $records"}
set owner [lindex $records 0]
if {[dict get $owner language] ne "systemverilog" ||
    [dict get $owner library] ne "work" ||
    [dict get $owner instance] ne "debug_top" ||
    [dict get $owner source_path] eq "" ||
    [dict get $owner source_identity] eq ""} {
  error "bad archived debugger provenance: $owner"
}

if {![catch {fsim::debug restart missing-debug-snapshot} reload_error]} {
  error "missing snapshot restart succeeded"
}
set reload_diagnostics [fsim::diagnostics]
if {[llength $reload_diagnostics] != 1 ||
    [dict get [lindex $reload_diagnostics 0] code] ne "FSIM-WS-001"} {
  error "failed restart did not publish only its expected diagnostic: $reload_diagnostics"
}
if {[dict get [fsim::debug inspect $q_ref] path] ne "debug_top.q"} {
  error "failed restart changed the active debugger session"
}
fsim::diagnostics clear
set restarted [fsim::debug restart]
if {[dict get $restarted snapshot] ne "debug-top" ||
    [dict get $restarted time] != 0} {
  error "bad structured restart result: $restarted"
}
if {![catch {fsim::debug inspect $q_ref} stale_error] ||
    [string first "stale" $stale_error] < 0} {
  error "successful restart did not invalidate loaded object references"
}
set stale_diagnostics [fsim::diagnostics]
if {[llength $stale_diagnostics] != 1 ||
    [dict get [lindex $stale_diagnostics 0] code] ne "FSIM-TCL-DEBUG-REF-0001"} {
  error "stale reference did not publish only its expected diagnostic: $stale_diagnostics"
}
fsim::diagnostics clear
puts "debugger-commands-ok"
)tcl",
        "debugger-commands-ok");

    const auto vhdl_source = directory / "source-unit.vhd";
    {
        std::ofstream file(vhdl_source);
        file
            << "entity vhdl_top is end entity;\n"
            << "architecture rtl of vhdl_top is\n"
            << "  signal q : bit;\n"
            << "begin\n"
            << "  process begin\n"
            << "    q <= '0';\n"
            << "    wait;\n"
            << "  end process;\n"
            << "end architecture;\n";
    }
    require_cli({ "fsim", "compile", "--lang", "vhdl", "--standard", "93",
        vhdl_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "vhdl:work.vhdl_top(rtl)",
        "--snapshot", "vhdl-debugger" });
    if (!std::filesystem::remove(vhdl_source)) {
        throw std::runtime_error("VHDL provenance source was not removed");
    }

    run_script("vhdl-debugger", R"tcl(
set records [fsim::provenance]
if {[llength $records] != 1} {error "bad VHDL provenance: $records"}
set owner [lindex $records 0]
if {[dict get $owner language] ne "vhdl" ||
    [dict get $owner library] ne "work" ||
    [dict get $owner instance] ne "vhdl_top" ||
    [dict get $owner source_path] eq "" ||
    [dict get $owner source_identity] eq "" ||
    [dict get $owner source_line] != 2} {
  error "VHDL source metadata was not restored: $owner"
}
puts "vhdl-provenance-ok"
)tcl",
        "vhdl-provenance-ok");

    const auto systemc_source = directory / "systemc-debug-plugin.cpp";
    {
        std::ofstream file(systemc_source);
        file
            << "#include \"fsim/systemc.hpp\"\n"
            << "SC_MODULE(DebugPlugin) { SC_CTOR(DebugPlugin) {} };\n"
            << "SC_FSIM_EXPORT_AS(DebugPlugin, \"probe\");\n";
    }
    require_cli({ "fsim", "systemc", "compile", "--library", "vendor",
        systemc_source.string() });
    require_cli({ "fsim", "systemc", "link", "--library", "vendor" });
    if (!std::filesystem::remove(systemc_source)) {
        throw std::runtime_error("SystemC provenance source was not removed");
    }
    require_cli({ "fsim", "elaborate", "--top", "systemc:vendor.probe",
        "--snapshot", "systemc-debugger" });

    run_script("systemc-debugger", R"tcl(
set records [fsim::debug provenance]
if {[llength $records] != 1} {error "bad SystemC provenance: $records"}
set owner [lindex $records 0]
if {[dict get $owner language] ne "systemc" ||
    [dict get $owner library] ne "vendor" ||
    [dict get $owner unit] ne "probe" ||
    [dict get $owner instance] eq "" ||
    [dict get $owner path] ne [dict get $owner instance] ||
    [dict get $owner plugin_identity] ne "vendor:probe" ||
    [dict get $owner source_identity] eq "" ||
    [dict get $owner source_identity] ne [dict get $owner plugin_input_digest]} {
  error "SystemC instance or archived source identity was lost: $owner"
}
foreach field {plugin_input_digest plugin_link_digest plugin_library_checksum} {
  set digest [dict get $owner $field]
  if {[string length $digest] != 64 || ![regexp {^[0-9a-f]+$} $digest]} {
    error "invalid archived SystemC digest $field: $digest"
  }
}
if {[dict get $owner plugin_compiler_fingerprint] eq ""} {
  error "SystemC compiler fingerprint was not restored: $owner"
}
if {[fsim::debug provenance] ne $records || [fsim::provenance] ne $records} {
  error "SystemC provenance changed across equivalent archived queries"
}
puts "systemc-plugin-provenance-ok"
)tcl",
        "systemc-plugin-provenance-ok");

    std::filesystem::current_path(working_directory.previous);
    std::error_code cleanup_error;
    for (std::filesystem::recursive_directory_iterator iterator(
             directory, cleanup_error),
        end;
        !cleanup_error && iterator != end;
        iterator.increment(cleanup_error)) {
        std::filesystem::permissions(
            iterator->path(), std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add, cleanup_error);
    }
    cleanup_error.clear();
    std::filesystem::remove_all(directory, cleanup_error);
    assert(!cleanup_error);
    return 0;
}
