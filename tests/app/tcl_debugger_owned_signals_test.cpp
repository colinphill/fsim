// SPDX-License-Identifier: Apache-2.0
#include "fsim/app/application.hpp"
#include "fsim/cli/driver.hpp"

#include <cassert>
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

int main(int argc, char** argv)
{
    // Under --engine debug the compiled kernel owns the design's signals:
    // the debugger examines, forces, releases and watches them at run time,
    // as a foreign library does (docs/engine-v4-semantics.md S19). The
    // expectations follow the LRMs, not a run: a force holds its value
    // against the drivers until released (IEEE 1800-2023 10.6.2; IEEE
    // 1076-2019 on a forced signal, VHPI vhpiForce), and a released signal takes its
    // drivers' value; a signal assignment takes effect in a later delta
    // (IEEE 1076-2019 14.7.3.4); a continuous assignment follows its source
    // in the same time step (IEEE 1800-2023 10.3).
    // FSIM-CONFORMANCE CF-COMMON-TCL-DEBUG-002 source=SRC-TCL expectation=execute
    assert(argc == 2);
    const auto directory = std::filesystem::path(argv[1]);
    std::filesystem::create_directories(directory);
    WorkingDirectory working_directory(directory);

    const auto vhdl_source = directory / "owned-vhdl.vhd";
    {
        std::ofstream file(vhdl_source);
        file
            << "entity owned_vhdl is end entity;\n"
            << "architecture rtl of owned_vhdl is\n"
            << "  signal s : bit := '0';\n"
            << "begin\n"
            << "  process begin\n"
            << "    wait for 5 ns;\n"
            << "    s <= '1';\n"
            << "    wait for 5 ns;\n"
            << "    s <= '0';\n"
            << "    wait for 100 ns;\n"
            << "    wait;\n"
            << "  end process;\n"
            << "end architecture;\n";
    }
    require_cli({ "fsim", "compile", "--lang", "vhdl", "--standard", "93",
        vhdl_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "vhdl:work.owned_vhdl(rtl)",
        "--snapshot", "owned-vhdl" });

    // The driver of s goes 0 -> 1 at 5 ns and 1 -> 0 at 10 ns. A force of
    // '0' made at the statement stop at time 0 hides the 5 ns update until
    // the release.
    run_script("owned-vhdl", R"tcl(
set stop [fsim::debug step statement]
if {[dict get $stop time] != 0 || [dict get $stop finished]} {
  error "statement stop left time 0: $stop"
}
if {[fsim::read_signal owned_vhdl.s] ne "0"} {
  error "s is not its initial value at the stop"
}
fsim::force_signal owned_vhdl.s 0
if {[fsim::read_signal owned_vhdl.s] ne "0"} {error "force did not show"}
set at6 [fsim::debug continue 6ns]
if {[dict get $at6 time] != 6000000} {error "not at 6 ns: $at6"}
if {[fsim::read_signal owned_vhdl.s] ne "0"} {
  error "the force did not hold s against the driver's update at 5 ns"
}
fsim::release_signal owned_vhdl.s
set at7 [fsim::debug continue 1ns]
if {[dict get $at7 time] != 7000000} {error "not at 7 ns: $at7"}
if {[fsim::read_signal owned_vhdl.s] ne "1"} {
  error "the released s did not take its driver's value 1"
}
set at11 [fsim::debug continue 4ns]
if {[dict get $at11 time] != 11000000} {error "not at 11 ns: $at11"}
if {[fsim::read_signal owned_vhdl.s] ne "0"} {
  error "s did not follow its driver to 0 at 10 ns"
}
puts "owned-vhdl-ok"
)tcl",
        "owned-vhdl-ok");

    const auto verilog_source = directory / "owned-sv.sv";
    {
        std::ofstream file(verilog_source);
        file
            << "module owned_sv; timeunit 1ns; timeprecision 1ns;\n"
            << "  logic a;\n"
            << "  wire w;\n"
            << "  assign w = a;\n"
            << "  initial begin\n"
            << "    a = 1'b0;\n"
            << "    #3 a = 1'b1;\n"
            << "    #3 a = 1'b0;\n"
            << "    #3 $finish;\n"
            << "  end\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", verilog_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_sv",
        "--snapshot", "owned-sv" });

    // w follows a by a continuous assignment: a signal breakpoint on w == 1
    // stops the run at 3 ns, where w already shows 1. A force of 1 then
    // holds w at 1 after a falls at 6 ns, until the release, after which w
    // takes its driver's value 0 (IEEE 1800-2023 10.6.2).
    run_script("owned-sv", R"tcl(
set breakpoint [fsim::debug break add signal owned_sv.w == 1]
if {[dict get $breakpoint kind] ne "signal"} {error "bad breakpoint"}
set hit [fsim::debug continue]
if {[dict get $hit time] != 3 || [dict get $hit finished]} {
  error "the signal breakpoint did not stop at 3 ns: $hit"
}
if {[fsim::read_signal owned_sv.w] ne "1"} {
  error "w does not show 1 at its breakpoint"
}
fsim::debug break clear
fsim::force_signal owned_sv.w 1
set at7 [fsim::debug continue 4ns]
if {[dict get $at7 time] != 7} {error "not at 7 ns: $at7"}
if {[fsim::read_signal owned_sv.a] ne "0"} {error "a did not fall at 6 ns"}
if {[fsim::read_signal owned_sv.w] ne "1"} {
  error "the force did not hold w at 1 against a falling a"
}
fsim::release_signal owned_sv.w
if {[fsim::read_signal owned_sv.w] ne "0"} {
  error "the released w did not take its driver's value 0"
}
puts "owned-sv-ok"
)tcl",
        "owned-sv-ok");

    const auto mid_source = directory / "owned-mid.sv";
    {
        std::ofstream file(mid_source);
        file
            << "module owned_mid; timeunit 1ns; timeprecision 1ns;\n"  // 1
            << "  logic [7:0] v;\n"                                    // 2
            << "  logic [7:0] seen;\n"                                 // 3
            << "  initial begin\n"                                     // 4
            << "    v = 8'd5;\n"                                       // 5
            << "    seen = v;\n"                                       // 6
            << "    #1 $finish;\n"                                     // 7
            << "  end\n"                                               // 8
            << "endmodule\n";                                          // 9
    }
    require_cli({ "fsim", "compile", mid_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_mid",
        "--snapshot", "owned-mid" });

    // A stop inside an activation: the blocking write of line 5 has taken
    // effect when the run stops before line 6 (IEEE 1800-2023 10.4.1), so
    // the variable shows 5 although the activation has not ended. A force
    // of 9 made there is what line 6 reads (10.6.2), and the variable keeps
    // the forced value after the statement.
    run_script("owned-mid", R"tcl(
set breakpoint [fsim::debug break add source owned-mid.sv:6]
if {[dict get $breakpoint line] != 6} {error "bad source breakpoint"}
set stop [fsim::debug continue]
if {[dict get $stop time] != 0 || [dict get $stop finished]} {
  error "the source breakpoint did not stop at time 0: $stop"
}
if {[fsim::read_signal owned_mid.v] ne "00000101"} {
  error "v does not show the blocking write of line 5 at the stop"
}
fsim::debug break clear
fsim::force_signal owned_mid.v 00001001
if {[fsim::read_signal owned_mid.v] ne "00001001"} {error "force did not show"}
fsim::debug step statement
if {[fsim::read_signal owned_mid.seen] ne "00001001"} {
  error "line 6 did not read the forced value"
}
if {[fsim::read_signal owned_mid.v] ne "00001001"} {
  error "the force did not hold v"
}
puts "owned-mid-ok"
)tcl",
        "owned-mid-ok");

    std::filesystem::current_path(working_directory.previous);
    return 0;
}
