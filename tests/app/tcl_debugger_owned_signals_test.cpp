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

// `engine` empty: the session's default engine (the debugger commands ask
// for the debug engine); otherwise `fsim tcl --engine engine`.
void run_script(
    const std::string& snapshot,
    const std::string& script,
    const std::string_view success_marker,
    const std::string_view engine = { })
{
    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    std::vector<std::string> arguments { "fsim", "tcl", "--snapshot", snapshot };
    if (!engine.empty()) {
        arguments.emplace_back("--engine");
        arguments.emplace_back(engine);
    }
    arguments.emplace_back("-c");
    arguments.push_back(script);
    const int result = run_cli(arguments, input, output, error);
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

    const auto wide_source = directory / "owned-wide.sv";
    {
        std::ofstream file(wide_source);
        file
            << "module owned_wide; timeunit 1ns; timeprecision 1ns;\n"
            << "  logic [128:0] w;\n"
            << "  initial begin\n"
            << "    w = '0;\n"
            << "    #1 w = {1'b1, {127{1'b0}}, 1'bx};\n"
            << "    #1 w = 'z;\n"
            << "    #1 w <= {1'b0, {127{1'b1}}, 1'b0};\n"
            << "    #1 $finish;\n"
            << "  end\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", wide_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_wide",
        "--snapshot", "owned-wide" });

    // A value-change observer of the debug engine hears each write of the
    // 129-bit w as one change of the whole value, as the reference engine
    // does: a blocking assignment updates its target at once (IEEE
    // 1800-2023 10.4.1) and a nonblocking assignment's update is one update
    // event (4.4.2.2, 10.4.2). The kernel stores a wide value as several
    // words; the observer never sees a value only partly written.
    constexpr auto wide_script = R"tcl(
set ::changes {}
proc wide_change {name value time delta} {
  if {$name eq "owned_wide.w"} {lappend ::changes [list $value $time]}
}
fsim::add_callback value_change wide_change
fsim::run
set expected [list \
  [list [string repeat 0 129] 0] \
  [list "1[string repeat 0 127]X" 1] \
  [list [string repeat Z 129] 2] \
  [list "0[string repeat 1 127]0" 3]]
if {$::changes ne $expected} {
  error "w changed as $::changes, not once per write"
}
puts "owned-wide-ok"
)tcl";
    for (const std::string_view engine : { "interpreter", "debug" }) {
        run_script("owned-wide", wide_script, "owned-wide-ok", engine);
    }

    const auto nba_source = directory / "owned-nba.sv";
    {
        std::ofstream file(nba_source);
        file
            << "module owned_nba; timeunit 1ns; timeprecision 1ns;\n"
            << "  logic go = 1'b0;\n"
            << "  logic [3:0] x;\n"
            << "  logic [3:0] y;\n"
            << "  logic [3:0] p;\n"
            << "  logic [3:0] q;\n"
            << "  always @(go) begin\n"
            << "    p <= 4'd2;\n"
            << "    x <= 4'd2;\n"
            << "  end\n"
            << "  initial begin\n"
            << "    #0;\n"
            << "    #0;\n"
            << "    y <= 4'd7;\n"
            << "    q <= 4'd7;\n"
            << "  end\n"
            << "  initial begin\n"
            << "    #0;\n"
            << "    x <= 4'd1;\n"
            << "    p <= 4'd1;\n"
            << "    y <= 4'd6;\n"
            << "    q <= 4'd6;\n"
            << "    go = 1'b1;\n"
            << "  end\n"
            << "  initial $monitor(\"p=%0d q=%0d\", p, q);\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", nba_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_nba",
        "--snapshot", "owned-nba" });

    // The NBA region performs its updates in the order their statements
    // executed (IEEE 1800-2023 4.6 b), 10.4.2), whichever process executed
    // them and whether or not a host process ($monitor) reads the target.
    // The last initial procedure resumes from #0 (Inactive) and executes
    // `x <= 1; p <= 1; y <= 6; q <= 6;`, and its blocking `go = 1` wakes the
    // always procedure (`p <= 2; x <= 2;`, Active) before the other initial
    // procedure resumes from its second #0 (`y <= 7; q <= 7;`, Inactive,
    // 4.4.2.3). A value-change observer hears the updates in that order.
    constexpr auto nba_script = R"tcl(
set ::changes {}
proc nba_change {name value time delta} {
  if {$time == 0} {lappend ::changes "$name=$value"}
}
fsim::add_callback value_change nba_change
fsim::run
set expected [list owned_nba.go=1 \
  owned_nba.x=0001 owned_nba.p=0001 owned_nba.y=0110 owned_nba.q=0110 \
  owned_nba.p=0010 owned_nba.x=0010 owned_nba.y=0111 owned_nba.q=0111]
if {$::changes ne $expected} {
  error "the NBA updates landed as $::changes"
}
puts "owned-nba-ok"
)tcl";
    for (const std::string_view engine : { "interpreter", "v4", "debug" }) {
        run_script("owned-nba", nba_script, "owned-nba-ok", engine);
    }

    const auto host_nba_source = directory / "owned-nba-host.sv";
    {
        std::ofstream file(host_nba_source);
        file
            << "module owned_nba_host; timeunit 1ns; timeprecision 1ns;\n"
            << "  class C; event e; endclass\n"
            << "  typedef struct { logic [3:0] m; logic [3:0] n; } pair_t;\n"
            << "  C c = new;\n"
            << "  event e;\n"
            << "  logic go = 1'b0;\n"
            << "  logic [3:0] x;\n"
            << "  logic [3:0] y;\n"
            << "  logic [3:0] z;\n"
            << "  logic [3:0] seen;\n"
            << "  string s = \"i\";\n"
            << "  logic [3:0] d[] = '{4'd0, 4'd0};\n"
            << "  logic [3:0] u[$] = '{4'd1};\n"
            << "  logic [3:0] a[int];\n"
            << "  pair_t r[2];\n"
            << "  always @(go) begin\n"
            << "    z <= 4'd2;\n"
            << "    s <= \"c\";\n"
            << "    u <= '{4'd3, 4'd4};\n"
            << "  end\n"
            << "  initial begin\n"
            << "    #0;\n"
            << "    x <= 4'd1;\n"
            << "    s <= \"a\";\n"
            << "    d <= '{4'd5, 4'd6};\n"
            << "    ->> e;\n"
            << "    ->> c.e;\n"
            << "    a <= '{3: 4'd7};\n"
            << "    y <= 4'd6;\n"
            << "    r[1].n <= 4'd8;\n"
            << "    s <= \"b\";\n"
            << "    go = 1'b1;\n"
            << "  end\n"
            << "  initial @(c.e) seen = x;\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", host_nba_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_nba_host",
        "--snapshot", "owned-nba-host" });

    // A nonblocking string update and a nonblocking update of a dynamic
    // array, a queue, an associative array (whole variables) or an element
    // of an array of structures (IEEE 1800-2023 10.4.2), and a nonblocking
    // trigger (`->>`, 15.5.1) of a named event or of a class's event, are
    // NBA-region events too: the region performs them in the order their
    // statements executed among the updates (4.6 b)). The initial procedure
    // executes `x <= 1; s <= "a"; d <= ...; ->> e; ->> c.e; a <= ...;
    // y <= 6; r[1].n <= 8; s <= "b";`, then `go = 1` wakes the always
    // procedure (`z <= 2; s <= "c"; u <= ...;`). Each value change hears the
    // string and arrays the updates before it left. The class event's
    // waiter resumes after `x <= 1` landed; when it runs among the region's
    // later updates is not fixed (4.7), so only its value is checked.
    constexpr auto host_nba_script = R"tcl(
set ::changes {}
set ::seen {}
proc host_value {name} {
  return [dict get [fsim::object read [fsim::object resolve owned_nba_host.$name]] value]
}
proc host_nba_change {name value time delta} {
  if {$time != 0} {return}
  set text "s=[host_value s] d={[host_value d]} u={[host_value u]} a={[host_value a]}"
  if {$name eq "owned_nba_host.seen"} {
    lappend ::seen "$name=$value"
  } elseif {$name ne "owned_nba_host.c" && [string first {$} $name] < 0} {
    lappend ::changes "$name=$value $text"
  }
}
fsim::add_callback value_change host_nba_change
fsim::run
set key 00000000000000000000000000000011
set expected [list \
  "owned_nba_host.go=1 s=i d={0000 0000} u={0001} a={}" \
  "owned_nba_host.x=0001 s=i d={0000 0000} u={0001} a={}" \
  "owned_nba_host.e=1 s=a d={0101 0110} u={0001} a={}" \
  "owned_nba_host.y=0110 s=a d={0101 0110} u={0001} a={{$key 0111}}" \
  "owned_nba_host.r=XXXXXXXXXXXX1000 s=a d={0101 0110} u={0001} a={{$key 0111}}" \
  "owned_nba_host.z=0010 s=b d={0101 0110} u={0001} a={{$key 0111}}"]
if {$::changes ne $expected} {
  error "the NBA events landed as $::changes"
}
if {$::seen ne [list owned_nba_host.seen=0001]} {
  error "the class event's waiter saw $::seen"
}
set final "s=[host_value s] u={[host_value u]}"
if {$final ne "s=c u={0011 0100}"} {
  error "the string and queue ended as $final"
}
puts "owned-nba-host-ok"
)tcl";
    for (const std::string_view engine : { "interpreter", "v4", "debug" }) {
        run_script("owned-nba-host", host_nba_script, "owned-nba-host-ok", engine);
    }

    const auto protected_source = directory / "owned-protected.vhd";
    {
        std::ofstream file(protected_source);
        file
            << "package owned_counter_types is\n"
            << "  type counter_t is protected\n"
            << "    procedure add (value : integer);\n"
            << "    impure function read return integer;\n"
            << "  end protected counter_t;\n"
            << "end package;\n"
            << "package body owned_counter_types is\n"
            << "  type counter_t is protected body\n"
            << "    variable current : integer := 7;\n"
            << "    procedure add (value : integer) is\n"
            << "    begin\n"
            << "      current := current + value;\n"
            << "    end procedure;\n"
            << "    impure function read return integer is\n"
            << "    begin\n"
            << "      return current;\n"
            << "    end function;\n"
            << "  end protected body counter_t;\n"
            << "end package body;\n"
            << "use work.owned_counter_types.all;\n"
            << "entity owned_protected is end entity;\n"
            << "architecture rtl of owned_protected is\n"
            << "  shared variable counter : counter_t;\n"
            << "begin\n"
            << "  process begin\n"
            << "    counter.add(4);\n"
            << "    counter.add(counter.read);\n"
            << "    wait for 5 ns;\n"
            << "    counter.add(1);\n"
            << "    wait;\n"
            << "  end process;\n"
            << "end architecture;\n";
    }
    require_cli({ "fsim", "compile", "--lang", "vhdl", "--standard", "2008",
        protected_source.string() });
    require_cli({ "fsim", "elaborate", "--top",
        "vhdl:work.owned_protected(rtl)", "--snapshot", "owned-protected" });

    // The variable of a protected type's shared variable (IEEE 1076-2019
    // 5.6.2, 6.4.2.4) holds what its methods left: 7 + 4, doubled, is 22
    // at 1 ns. A value written through the object interface is the one the
    // next method call sees: 100, then add(1) at 5 ns leaves 101. The v4
    // kernel holds the variable in its own storage; the host reads and
    // writes it there (docs/engine-v4-semantics.md S19).
    constexpr auto protected_script = R"tcl(
proc counter_value {} {
  set ref [fsim::object resolve owned_protected.counter.current]
  scan [lindex [dict get [fsim::object read $ref] value] 0] %b value
  return $value
}
fsim::run 1ns
if {[counter_value] != 22} {
  error "the counter read [counter_value] at 1 ns"
}
fsim::object write [fsim::object resolve owned_protected.counter.current] {100}
if {[counter_value] != 100} {
  error "the counter read [counter_value] after the write"
}
fsim::run
if {[counter_value] != 101} {
  error "the counter read [counter_value] at the end"
}
puts "owned-protected-ok"
)tcl";
    for (const std::string_view engine : { "interpreter", "v4", "debug" }) {
        run_script("owned-protected", protected_script, "owned-protected-ok",
            engine);
    }

    const auto rewrite_source = directory / "owned-nba-rewrite.sv";
    {
        std::ofstream file(rewrite_source);
        file
            << "module owned_nba_rewrite; timeunit 1ns; timeprecision 1ns;\n"
            << "  logic [3:0] x;\n"
            << "  logic [3:0] y;\n"
            << "  logic [3:0] z;\n"
            << "  initial begin\n"
            << "    #0;\n"
            << "    x <= 4'd1;\n"
            << "    y <= 4'd2;\n"
            << "    z <= 4'd3;\n"
            << "    x <= 4'd6;\n"
            << "    y <= 4'd7;\n"
            << "  end\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", rewrite_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_nba_rewrite",
        "--snapshot", "owned-nba-rewrite" });

    // Each nonblocking update is its own update event, performed in the
    // order the statements executed (IEEE 1800-2023 4.4.2.2, 4.6 b)): a
    // second update of x and of y comes after the updates executed between,
    // so a value-change observer hears x, y, z, x, y.
    constexpr auto rewrite_script = R"tcl(
set ::changes {}
proc rewrite_change {name value time delta} {
  if {$time == 0} {lappend ::changes "$name=$value"}
}
fsim::add_callback value_change rewrite_change
fsim::run
set expected [list owned_nba_rewrite.x=0001 owned_nba_rewrite.y=0010 \
  owned_nba_rewrite.z=0011 owned_nba_rewrite.x=0110 owned_nba_rewrite.y=0111]
if {$::changes ne $expected} {
  error "the NBA updates landed as $::changes"
}
puts "owned-nba-rewrite-ok"
)tcl";
    for (const std::string_view engine : { "interpreter", "v4", "debug" }) {
        run_script("owned-nba-rewrite", rewrite_script, "owned-nba-rewrite-ok",
            engine);
    }

    const auto containers_source = directory / "owned-containers.sv";
    {
        std::ofstream file(containers_source);
        file
            << "module owned_containers; timeunit 1ns; timeprecision 1ns;\n"
            << "  int q[$] = '{1, 2, 3};\n"
            << "  int g[$] = '{1};\n"
            << "  int d[] = '{4, 5};\n"
            << "  int a[int];\n"
            << "  string sa[string];\n"
            << "  string s[$] = '{\"x\"};\n"
            << "  string ds[] = '{\"m\", \"n\"};\n"
            << "  string summary;\n"
            << "  initial begin\n"
            << "    a = '{default: 7};\n"
            << "    a[3] = 30;\n"
            << "    a[-1] = 10;\n"
            << "    sa[\"k\"] = \"v\";\n"
            << "    #5 summary = $sformatf(\"q=%0d:%0d,%0d g=%0d:%0d,%0d,%0d d=%0d:%0d,%0d a=%0d:%0d,%0d,%0d,%0d sa=%0d:%s,%s s=%0d:%s ds=%s,%s\",\n"
            << "      q.size(), q[0], q[1], g.size(), g[0], g[1], g[2], d.size(), d[0], d[1],\n"
            << "      a.num(), a[3], a[-5], a.exists(-1), a[9], sa.num(), sa[\"a\"], sa[\"b\"],\n"
            << "      s.size(), s[2], ds[0], ds[1]);\n"
            << "  end\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", containers_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_containers",
        "--snapshot", "owned-containers" });

    // A value written through the object interface is the container's new
    // value, as an assignment of it would be: a queue or a dynamic array
    // takes the list's elements and its length, shorter, longer or the
    // same (IEEE 1800-2023 7.5, 7.10), a string array its strings, and an
    // associative array exactly the {key element} pairs given, in key order
    // (7.8, 7.8.2), keeping the array's default (7.9.11). The object reads
    // back so, and a process reading the containers afterwards sees those
    // elements and sizes, and the default for a key the write left out.
    constexpr auto containers_script = R"tcl(
proc ref {name} { return [fsim::object resolve owned_containers.$name] }
proc value {name} { return [dict get [fsim::object read [ref $name]] value] }
proc word {n} { return [format %032b [expr {$n & 0xffffffff}]] }
fsim::run 2ns
fsim::object write [ref q] {5 6}
fsim::object write [ref g] {9 10 11}
fsim::object write [ref d] {7 8}
fsim::object write [ref a] {{3 33} {-5 55}}
fsim::object write [ref sa] {{b two} {a one}}
fsim::object write [ref s] {p q r}
fsim::object write [ref ds] {u w}
set read "q={[value q]} g={[value g]} d={[value d]} a={[value a]} sa={[value sa]} s={[value s]} ds={[value ds]}"
set expected "q={[word 5] [word 6]} g={[word 9] [word 10] [word 11]} d={[word 7] [word 8]} a={{[word -5] [word 55]} {[word 3] [word 33]}} sa={{a one} {b two}} s={p q r} ds={u w}"
if {$read ne $expected} { error "the containers read $read" }
fsim::run
set summary [value summary]
if {$summary ne "q=2:5,6 g=3:9,10,11 d=2:7,8 a=2:33,55,0,7 sa=2:one,two s=3:r ds=u,w"} {
  error "the process read $summary"
}
puts "owned-containers-ok"
)tcl";
    for (const std::string_view engine : { "interpreter", "v4", "debug" }) {
        run_script("owned-containers", containers_script, "owned-containers-ok",
            engine);
    }

    const auto writes_source = directory / "owned-writes.sv";
    {
        std::ofstream file(writes_source);
        file
            << "module owned_writes; timeunit 1ns; timeprecision 1ns;\n"
            << "  logic [3:0] x;\n"
            << "  initial begin\n"
            << "    x = 1; x = 2;\n"
            << "    #1 x = 3; x = 4; x = 5;\n"
            << "    #1 $finish;\n"
            << "  end\n"
            << "endmodule\n";
    }
    require_cli({ "fsim", "compile", writes_source.string() });
    require_cli({ "fsim", "elaborate", "--top", "sv:work.owned_writes",
        "--snapshot", "owned-writes" });

    // A value-change observer hears each blocking write of x that changes
    // its value, also several in one time step: a blocking assignment
    // updates its target at once (IEEE 1800-2023 10.4.1), so each is a
    // change of its own, on every engine.
    constexpr auto writes_script = R"tcl(
set ::changes {}
proc write_change {name value time delta} {
  if {$name eq "owned_writes.x"} {lappend ::changes [list $value $time]}
}
fsim::add_callback value_change write_change
fsim::run
set expected [list {0001 0} {0010 0} {0011 1} {0100 1} {0101 1}]
if {$::changes ne $expected} {
  error "x changed as $::changes, not once per write"
}
puts "owned-writes-ok"
)tcl";
    for (const std::string_view engine : { "interpreter", "v4", "debug" }) {
        run_script("owned-writes", writes_script, "owned-writes-ok", engine);
    }

    std::filesystem::current_path(working_directory.previous);
    return 0;
}
