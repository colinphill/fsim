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
        throw std::runtime_error("fixture command failed: " + error.str());
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

} // namespace

int main()
{
    const auto suffix = std::chrono::steady_clock::now()
                            .time_since_epoch()
                            .count();
    const auto directory = std::filesystem::temp_directory_path()
        / ("fsim-tcl-object-test-" + std::to_string(suffix));
    std::filesystem::create_directories(directory);
    WorkingDirectory working_directory(directory);

    const auto source = directory / "object-model.sv";
    {
        std::ofstream file(source);
        file << R"sv(
package object_model_pkg;
  class Counter;
    int value;
    function new();
      value = 7;
    endfunction
  endclass
endpackage

module object_model_tb;
  logic [3:0] q = 4'b1010;
  string label = "ready";
  int data [0:1];
  object_model_pkg::Counter counter;

  initial begin
    counter = new();
    #3;
  end
endmodule
)sv";
    }
    require_cli({ "fsim", "compile", source.string() });
    require_cli({ "fsim", "elaborate", "--top", "work.object_model_tb",
        "--snapshot", "object-model" });
    if (!std::filesystem::remove(source)) {
        throw std::runtime_error("source fixture was not removed");
    }

    const std::string catalog_script = R"tcl(
set definitions [fsim::object definitions work]
set package_found 0
set class_found 0
foreach definition $definitions {
  if {[dict get $definition name] eq "object_model_pkg" &&
      [dict get $definition kind] eq "package"} {
    set package_found 1
  }
  if {[dict get $definition name] eq "work::object_model_pkg::Counter" &&
      [dict get $definition kind] eq "class"} {
    set class_found 1
  }
}
if {!$package_found || !$class_found} {
  error "compiled library definitions were not available without a snapshot: $definitions"
}

set package_ref [fsim::object definition work object_model_pkg]
set package_info [fsim::object info $package_ref]
if {[dict get $package_info kind] ne "package"} {
  error "bad no-snapshot package metadata: $package_info"
}
set class_ref [fsim::object definition work work::object_model_pkg::Counter]
set class_info [fsim::object info $class_ref]
if {[dict get $class_info kind] ne "class"} {
  error "bad no-snapshot class metadata: $class_info"
}
set member_names {}
foreach member [fsim::object children $class_ref] {
  lappend member_names [dict get [fsim::object info $member] name]
}
if {[lsearch -exact $member_names value] < 0} {
  error "compiled class member reflection omitted value: $member_names"
}

fsim::load object-model
set loaded_roots [fsim::object roots]
set loaded_root [lindex $loaded_roots 0]
set loaded_children [fsim::object children $loaded_root]
set loaded_q_ref ""
foreach child $loaded_children {
  if {[dict get [fsim::object info $child] name] eq "q"} {
    set loaded_q_ref $child
  }
}
if {$loaded_q_ref eq ""} {
  error "loaded snapshot reference was not created: roots=$loaded_roots children=$loaded_children"
}
set loaded_q_info [fsim::object info $loaded_q_ref]
if {[dict get $loaded_q_info name] ne "q" ||
    [dict get $loaded_q_info width] != 4 ||
    [dict get $loaded_q_info reference] ne $loaded_q_ref} {
  error "loaded snapshot reference has bad identity or shape: $loaded_q_info"
}
fsim::run 1ns
set loaded_q_value [fsim::object value $loaded_q_ref]
if {[dict get $loaded_q_value kind] ne "logic" ||
    [dict get $loaded_q_value shape] ne "packed" ||
    [dict get $loaded_q_value value] ne "1010"} {
  error "loaded snapshot did not preserve the expected signal value: $loaded_q_value"
}

fsim::library delete work
if {![catch {fsim::object info $package_ref} stale_error] ||
    [string first "stale" $stale_error] < 0} {
  error "deleting a compiled library did not stale its catalog reference"
}
set loaded_q_after_delete [fsim::object value $loaded_q_ref]
if {[dict get [fsim::object info $loaded_q_ref] name] ne "q" ||
    [dict get $loaded_q_after_delete value] ne [dict get $loaded_q_value value]} {
  error "deleting the compiled library invalidated an independent loaded snapshot reference"
}
set diagnostics [fsim::diagnostics]
if {[dict get [lindex $diagnostics end] code] ne "FSIM-TCL-OBJECT-0002"} {
  error "stale catalog reference did not add its structured diagnostic: $diagnostics"
}
fsim::diagnostics clear
puts "catalog-no-snapshot-ok"
)tcl";
    {
        std::istringstream input;
        std::ostringstream output;
        std::ostringstream error;
        const int result = run_cli(
            { "fsim", "tcl", "-c", catalog_script }, input, output, error);
        if (result != 0) {
            std::cerr << "Tcl no-snapshot catalog test failed:\n"
                      << error.str()
                      << "\nTcl output:\n"
                      << output.str();
        }
        assert(result == 0);
        assert(output.str().find("catalog-no-snapshot-ok") != std::string::npos);
        assert(error.str().empty());
    }

    const std::string script = R"tcl(
fsim::load object-model
fsim::run 1ns
set root [lindex [fsim::object roots] 0]
set root_info [fsim::object info $root]
if {[dict get $root_info kind] ne "instance" ||
    [dict get $root_info name] ne "object_model_tb"} {
  error "bad object root metadata: $root_info"
}
set root_path [dict get $root_info path]
if {[fsim::object resolve $root_path] ne $root} {
  error "root path did not resolve to its reference"
}

set q_ref ""
set label_ref ""
set data_ref ""
set counter_ref ""
foreach child [fsim::object children $root] {
  set info [fsim::object info $child]
  switch -- [dict get $info name] {
    q {set q_ref $child}
    label {set label_ref $child}
    data {set data_ref $child}
    counter {set counter_ref $child}
  }
}
if {$q_ref eq "" || $label_ref eq "" || $data_ref eq "" ||
    $counter_ref eq ""} {
  error "missing expected root children"
}

set q_info [fsim::object info $q_ref]
if {[dict get $q_info kind] ne "signal" ||
    [dict get $q_info width] != 4 ||
    [llength [dict get $q_info dimensions]] != 1} {
  error "bad packed-signal metadata: $q_info"
}
if {[dict get [fsim::object value $q_ref] value] ne "1010"} {
  error "bad initial packed signal value"
}
if {![catch {fsim::object set $q_ref 010q} invalid_value_error]} {
  error "invalid packed signal assignment unexpectedly succeeded"
}
if {[dict get [fsim::object value $q_ref] value] ne "1010"} {
  error "invalid packed signal assignment changed the signal"
}
set q_value [fsim::object set $q_ref 0011]
if {[dict get $q_value value] ne "0011"} {
  error "typed signal mutation did not return the changed value: $q_value"
}

if {[dict get [fsim::object value $label_ref] value] ne "ready"} {
  error "bad initial string object value"
}
set label_value [fsim::object set $label_ref changed]
if {[dict get $label_value value] ne "changed"} {
  error "typed string mutation failed: $label_value"
}

set data_info [fsim::object info $data_ref]
if {[dict get $data_info kind] ne "container" ||
    [llength [dict get $data_info dimensions]] != 1} {
  error "bad unpacked-container metadata: $data_info"
}
set data_value [fsim::object set $data_ref {3 4}]
set data_elements [dict get $data_value value]
if {[string trimleft [lindex $data_elements 0] 0] ne "11" ||
    [string trimleft [lindex $data_elements 1] 0] ne "100"} {
  error "typed integer container mutation was not width-normalized: $data_value"
}
if {![catch {fsim::object set $data_ref {3 2147483648}} range_error]} {
  error "out-of-range signed integer container mutation unexpectedly succeeded"
}
set read_data_elements [dict get [fsim::object value $data_ref] value]
if {[string trimleft [lindex $read_data_elements 0] 0] ne "11" ||
    [string trimleft [lindex $read_data_elements 1] 0] ne "100"} {
  error "container value did not reflect the validated mutation"
}

fsim::run 1ns
set counter_value [fsim::object value $counter_ref]
if {[dict get $counter_value kind] ne "class" ||
    [dict get $counter_value null]} {
  error "class-handle signal did not expose its live object: $counter_value"
}
set counter_object [dict get $counter_value value]
set counter_info [fsim::object info $counter_object]
if {[dict get $counter_info dynamic_type] ne "work::object_model_pkg::Counter"} {
  error "bad live class object metadata: $counter_info"
}
set value_ref ""
foreach member [fsim::object children $counter_object] {
  if {[dict get [fsim::object info $member] name] eq "value"} {
    set value_ref $member
  }
}
set live_property_value ""
if {$value_ref ne ""} {
  set live_property_value [fsim::object value $value_ref]
}
if {$value_ref eq "" ||
    [string trimleft [dict get $live_property_value value] 0] ne "111"} {
  error "live class property value was not reflected: ref=$value_ref value=$live_property_value object=$counter_value"
}

set stale_ref $q_ref
fsim::load object-model
if {![catch {fsim::object info $stale_ref} stale_error] ||
    [string first "stale" $stale_error] < 0} {
  error "reloading a snapshot did not stale the previous object reference"
}
set diagnostics [fsim::diagnostics]
if {[dict get [lindex $diagnostics end] code] ne "FSIM-TCL-OBJECT-0002"} {
  error "stale reference did not add its structured diagnostic: $diagnostics"
}
fsim::diagnostics clear

set root [lindex [fsim::object roots] 0]
foreach child [fsim::object children $root] {
  if {[dict get [fsim::object info $child] name] eq "q"} {
    set ::callback_signal_ref $child
  }
}
proc mutate_from_callback {time delta phase} {
  if {![catch {
    fsim::object set $::callback_signal_ref 0000
  } message]} {
    error "object mutation succeeded from a callback"
  }
  if {[string first "not allowed from a Tcl callback" $message] < 0} {
    error "bad callback mutation error: $message"
  }
}
fsim::on safe_point mutate_from_callback
fsim::run 1ns
fsim::diagnostics clear
puts "object-model-ok"
)tcl";

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const int result = run_cli(
        { "fsim", "tcl", "--snapshot", "object-model", "-c", script },
        input,
        output,
        error);
    if (result != 0) {
        std::cerr << "Tcl object command test failed:\n"
                  << error.str()
                  << "\nTcl output:\n"
                  << output.str();
    }
    assert(result == 0);
    assert(output.str().find("object-model-ok") != std::string::npos);
    assert(error.str().empty());

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
}
