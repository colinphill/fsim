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

class TemporaryDirectory final {
public:
    TemporaryDirectory()
    {
        const auto unique = std::chrono::steady_clock::now()
                                .time_since_epoch()
                                .count();
        path_ = std::filesystem::temp_directory_path()
            / ("fsim-tcl-workspace-" + std::to_string(unique));
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

class WorkingDirectory final {
public:
    explicit WorkingDirectory(const std::filesystem::path& path)
        : previous_(std::filesystem::current_path())
    {
        std::filesystem::current_path(path);
    }

    ~WorkingDirectory()
    {
        std::error_code error;
        std::filesystem::current_path(previous_, error);
    }

private:
    std::filesystem::path previous_;
};

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

void run_workspace_commands_test()
{
    TemporaryDirectory temporary;
    WorkingDirectory working_directory(temporary.path());
    {
        std::ofstream source { "top.sv" };
        source << R"sv(package workspace_pkg;
parameter int VERSION = 1;
endpackage
module top;
  logic ready;
  initial begin
    ready = 0;
    #1 ready = 1;
  end
endmodule
)sv";
        assert(source.good());
    }
    const auto external_workspace = temporary.path() / "external-workspace";
    std::filesystem::create_directories(external_workspace);
    {
        std::ofstream source { external_workspace / "external.sv" };
        source << "module external_unit; endmodule\n";
        assert(source.good());
    }
    const auto switched_workspace = temporary.path() / "switched-workspace";
    std::filesystem::create_directories(switched_workspace);
    {
        std::ofstream source { switched_workspace / "switched.sv" };
        source << "package switched_pkg;\n"
                  "parameter int VERSION = 1;\n"
                  "endpackage\n"
                  "module switched_top;\n"
                  "  logic ready;\n"
                  "  initial begin ready = 0; #1 ready = 1; #1 ready = 1; #10; end\n"
                  "endmodule\n";
        assert(source.good());
    }
    {
        WorkingDirectory external_directory { external_workspace };
        std::istringstream input;
        std::ostringstream output;
        std::ostringstream error;
        const auto result = run_cli(
            { "fsim", "tcl", "-c",
                "fsim::compile -library alias -verbosity quiet external.sv" },
            input, output, error);
        if (result != 0) {
            throw std::runtime_error(
                "external alias catalog compilation failed: " + error.str());
        }
        assert(error.str().empty());
    }

    constexpr auto script = R"FSIM_TCL(
proc write_top_source {version value_at_one} {
  set source [open top.sv w]
  puts $source "package workspace_pkg;"
  puts $source "parameter int VERSION = $version;"
  puts $source "endpackage"
  puts $source "module top;"
  puts $source "logic ready;"
  puts $source "initial begin"
  puts $source "ready = 0;"
  puts $source "#1 ready = $value_at_one;"
  puts $source "end"
  puts $source "endmodule"
  close $source
}

proc assert_stale_catalog_reference {reference operation} {
  if {![catch {fsim::object info $reference} message] ||
      [string first "stale" $message] < 0} {
    error "$operation did not stale the old catalog reference"
  }
  set diagnostics [fsim::diagnostics]
  if {[llength $diagnostics] == 0 ||
      [dict get [lindex $diagnostics end] code] ne "FSIM-TCL-OBJECT-0002"} {
    error "$operation did not publish the stale-reference diagnostic"
  }
  fsim::diagnostics clear
}

proc assert_messages {operation verbosity messages} {
  if {$verbosity eq "quiet"} {
    if {[llength $messages] != 0} {
      error "$operation quiet verbosity returned progress messages: $messages"
    }
    return
  }
  if {[llength $messages] == 0} {
    error "$operation $verbosity verbosity returned no progress messages"
  }
  foreach message $messages {
    if {[string trim $message] eq "" ||
        [string first "\n" $message] >= 0 ||
        [string first "\r" $message] >= 0} {
      error "$operation returned an empty or multiline progress message: $messages"
    }
  }
}

set compilation [fsim::compile -library work -verbosity quiet top.sv]
assert_messages compile quiet [dict get $compilation messages]
if {[dict get $compilation library] ne "work"} {
  error "compile returned the wrong library"
}
if {[dict get $compilation language] ne "systemverilog"} {
  error "compile did not infer SystemVerilog"
}
if {[dict get $compilation object_count] != 2} {
  error "compile returned the wrong object count"
}
if {[llength [dict get $compilation owned_units]] != 2} {
  error "compile did not return the package and module units"
}
if {[llength [dict get $compilation diagnostics]] != 0} {
  error "successful compile returned unexpected diagnostics"
}
set normal_compilation [fsim::compile -library work -verbosity normal top.sv]
set normal_compile_messages [dict get $normal_compilation messages]
assert_messages compile normal $normal_compile_messages
if {![dict exists $normal_compilation diagnostics] ||
    [llength [dict get $normal_compilation diagnostics]] != 0} {
  error "normal compile progress changed the structured diagnostics result"
}
if {![string match {compiling *} [lindex $normal_compile_messages 0]] ||
    ![string match {compiled *} [lindex $normal_compile_messages end]]} {
  error "normal compile verbosity omitted its progress lines: $normal_compile_messages"
}
set verbose_compilation [fsim::compile -library work -verbosity verbose top.sv]
set verbose_compile_messages [dict get $verbose_compilation messages]
assert_messages compile verbose $verbose_compile_messages
if {![dict exists $verbose_compilation diagnostics] ||
    [llength [dict get $verbose_compilation diagnostics]] != 0} {
  error "verbose compile progress changed the structured diagnostics result"
}
if {[llength $verbose_compile_messages] <= [llength $normal_compile_messages] ||
    [lsearch -glob $verbose_compile_messages {  compiled environment:*}] < 0} {
  error "verbose compile verbosity omitted its extra detail: $verbose_compile_messages"
}
set original_catalog_ref [fsim::object definition work workspace_pkg]
set original_catalog_info [fsim::object info $original_catalog_ref]
if {[dict get $original_catalog_info kind] ne "package"} {
  error "compile did not publish the package definition"
}
set original_artifacts [fsim::library objects work]
if {[llength $original_artifacts] != 2} {
  error "initial compile did not publish two objects"
}
set original_artifact_id [dict get [lindex $original_artifacts 0] id]

set default_snapshot [fsim::elaborate -verbosity quiet top]
assert_messages elaborate quiet [dict get $default_snapshot messages]
if {[dict get $default_snapshot snapshot] ne "default"} {
  error "elaborate did not use the default snapshot"
}
if {[dict get [dict get $default_snapshot counts] roots] != 1} {
  error "elaborate returned the wrong root count"
}
set normal_snapshot [fsim::elaborate -snapshot normal -verbosity normal top]
set normal_elaborate_messages [dict get $normal_snapshot messages]
assert_messages elaborate normal $normal_elaborate_messages
if {![dict exists $normal_snapshot diagnostics] ||
    [llength [dict get $normal_snapshot diagnostics]] != 0} {
  error "normal elaborate progress changed the structured diagnostics result"
}
if {![string match {elaborating *} [lindex $normal_elaborate_messages 0]] ||
    ![string match {snapshot * is ready} [lindex $normal_elaborate_messages end]]} {
  error "normal elaborate verbosity omitted its progress lines: $normal_elaborate_messages"
}
set verbose_snapshot [fsim::elaborate -snapshot verbose -verbosity verbose top]
set verbose_elaborate_messages [dict get $verbose_snapshot messages]
assert_messages elaborate verbose $verbose_elaborate_messages
if {![dict exists $verbose_snapshot diagnostics] ||
    [llength [dict get $verbose_snapshot diagnostics]] != 0} {
  error "verbose elaborate progress changed the structured diagnostics result"
}
if {[llength $verbose_elaborate_messages] <= [llength $normal_elaborate_messages] ||
    [lsearch -glob $verbose_elaborate_messages {  loading *}] < 0} {
  error "verbose elaborate verbosity omitted its extra detail: $verbose_elaborate_messages"
}
set named_snapshot [fsim::elaborate -snapshot inspectable -verbosity quiet top]
assert_messages elaborate quiet [dict get $named_snapshot messages]
if {[dict get $named_snapshot snapshot] ne "inspectable"} {
  error "elaborate did not use the named snapshot"
}

set initial_directory [file normalize [pwd]]
set switched_directory [file normalize [file join \
    $initial_directory switched-workspace]]
set cd_result [cd $switched_directory]
if {$cd_result ne ""} {
  error "successful Tcl cd did not preserve its empty result"
}
set switched_view [fsim::workspace]
if {[file normalize [dict get $switched_view directory]] ne $switched_directory ||
    [file normalize [dict get $switched_view managed_directory]] ne
        [file normalize [file join $switched_directory .fsim]]} {
  error "cd did not switch the fsim workspace: $switched_view"
}
if {[llength [dict get $switched_view libraries]] != 0 ||
    [llength [fsim::library list]] != 0} {
  error "new workspace exposed libraries from the prior directory"
}
set switched_compile [fsim::compile -verbosity quiet switched.sv]
if {[dict get $switched_compile library] ne "work" ||
    [llength [dict get [fsim::workspace] libraries]] != 1} {
  error "compile did not publish into the switched workspace"
}
set switched_library [lindex [dict get [fsim::workspace] libraries] 0]
if {[dict get $switched_library library] ne "work" ||
    [file normalize [dict get $switched_library path]] ne
        [file normalize [file join $switched_directory .fsim libraries work]]} {
  error "switched workspace returned the wrong library view: $switched_library"
}
set switched_elaboration [fsim::elaborate \
    -snapshot switched -verbosity quiet switched_top]
if {[dict get $switched_elaboration snapshot] ne "switched"} {
  error "elaborate did not publish in the switched workspace"
}
set switched_load [fsim::load switched]
if {[dict get $switched_load snapshot] ne "switched"} {
  error "load did not select the switched workspace snapshot"
}
set switched_root [lindex [fsim::object roots] 0]
if {[dict get [fsim::object info $switched_root] name] ne "switched_top"} {
  error "loaded snapshot did not come from the switched workspace"
}
set switched_ready ""
foreach child [fsim::object children $switched_root] {
  if {[dict get [fsim::object info $child] name] eq "ready"} {
    set switched_ready $child
  }
}
if {$switched_ready eq ""} {error "switched snapshot omitted ready"}
if {[dict get [fsim::run 1ns] status] ni {completed time_limit} ||
    [dict get [fsim::object value $switched_ready] value] ne "1"} {
  error "switched snapshot did not run independently"
}
set switched_catalog_ref [fsim::object definition work switched_pkg]

set missing_directory [file join $switched_directory missing-directory]
if {![catch {::cd $missing_directory} cd_error cd_options]} {
  error "cd to a missing directory unexpectedly succeeded"
}
if {$cd_error eq "" ||
    [string first $missing_directory $cd_error] < 0 ||
    ![dict exists $cd_options -errorcode] ||
    [llength [dict get $cd_options -errorcode]] == 0} {
  error "failed cd did not preserve Tcl's native error details: $cd_error"
}
if {[file normalize [pwd]] ne $switched_directory} {
  error "failed cd changed the process directory"
}
set failed_cd_view [fsim::workspace]
if {[file normalize [dict get $failed_cd_view directory]] ne
        $switched_directory ||
    [dict get [fsim::object info $switched_catalog_ref] name] ne "switched_pkg"} {
  error "failed cd changed the workspace or catalog reference"
}

set ::callback_cd_attempted 0
proc reject_callback_cd {target args} {
  if {$::callback_cd_attempted} {return}
  set ::callback_cd_attempted 1
  foreach spelling {cd ::cd} {
    set before [file normalize [pwd]]
    set options {}
    if {![catch [list $spelling $target] message options]} {
      error "callback $spelling unexpectedly changed the workspace"
    }
    if {![dict exists $options -errorcode] ||
        [dict get $options -errorcode] ne {FSIM TCL WORKSPACE CALLBACK}} {
      error "callback $spelling returned the wrong rejection: $message"
    }
    if {[file normalize [pwd]] ne $before} {
      error "callback $spelling changed the process directory"
    }
    set callback_view [fsim::workspace]
    if {[file normalize [dict get $callback_view directory]] ne $before ||
        [dict get [fsim::object info $::switched_catalog_ref] name] ne
            "switched_pkg"} {
      error "callback $spelling changed the workspace or catalog generation"
    }
  }
}
fsim::on safe_point [list reject_callback_cd $initial_directory]
set callback_run [fsim::run 2ns]
fsim::off safe_point
if {!$::callback_cd_attempted ||
    [dict get $callback_run status] ni {completed time_limit}} {
  error "callback cd regression did not complete normally: $callback_run"
}

set cd_result [::cd $initial_directory]
if {$cd_result ne ""} {
  error "qualified Tcl cd did not preserve its empty result"
}
assert_stale_catalog_reference $switched_catalog_ref cd
set restored_view [fsim::workspace]
if {[file normalize [dict get $restored_view directory]] ne $initial_directory ||
    [llength [dict get $restored_view libraries]] != 1 ||
    [dict get [lindex [dict get $restored_view libraries] 0] library] ne "work"} {
  error "cd back did not restore the original workspace view: $restored_view"
}
if {[dict get [fsim::object value $switched_ready] value] ne "1"} {
  error "changing workspace invalidated the loaded snapshot"
}

set work_path ""
foreach library [fsim::library list] {
  if {[dict get $library library] eq "work"} {
    set work_path [dict get $library path]
  }
}
if {$work_path eq ""} {
  error "library list omitted work"
}
set alias_path [file join [pwd] external-workspace .fsim libraries alias]
set mapped [fsim::library map alias $alias_path]
if {![dict get $mapped mapped]} {
  error "library map did not return the mapped state"
}
set unmapped [fsim::library unmap alias]
if {[dict get $unmapped mapped]} {
  error "library unmap returned the mapped state"
}

write_top_source 2 0
set replacement [fsim::compile -library work -verbosity quiet top.sv]
if {[llength [dict get $replacement diagnostics]] != 0} {
  error "successful replacement returned unexpected diagnostics"
}
if {[dict get [fsim::object value $switched_ready] value] ne "1"} {
  error "another workspace's catalog replacement changed the loaded snapshot"
}
assert_stale_catalog_reference $original_catalog_ref replacement

set artifacts [fsim::library objects work]
if {[llength $artifacts] != 2} {
  error "library objects returned the wrong artifact count"
}
set artifact_id [dict get [lindex $artifacts 0] id]
if {$artifact_id eq $original_artifact_id} {
  error "source replacement did not replace the compiled object"
}
set replaced_catalog_ref [fsim::object definition work workspace_pkg]
set deletion [fsim::library delete-object work $artifact_id]
if {![dict get $deletion deleted]} {
  error "delete-object did not report deletion"
}
assert_stale_catalog_reference $replaced_catalog_ref delete-object
if {[llength [fsim::library objects work]] != 1} {
  error "delete-object did not remove exactly one artifact"
}
set deletion [fsim::library delete work]
if {![dict get $deletion deleted]} {
  error "library delete did not report deletion"
}
if {[dict get [fsim::object value $switched_ready] value] ne "1"} {
  error "another workspace's library deletion changed the loaded snapshot"
}
file delete top.sv

# The snapshot owns its design payload and remains loadable after its source
# library and source file have been deleted.
set loaded [fsim::load inspectable]
if {[dict get $loaded snapshot] ne "inspectable"} {
  error "source-free named snapshot did not load"
}
set loaded_roots [fsim::object roots]
if {[llength $loaded_roots] != 1} {
  error "loaded snapshot returned the wrong root count"
}
set loaded_root [lindex $loaded_roots 0]
set loaded_root_info [fsim::object info $loaded_root]
if {[dict get $loaded_root_info name] ne "top"} {
  error "loaded snapshot returned the wrong root: $loaded_root_info"
}
set ready_ref ""
set ready_info ""
foreach child [fsim::object children $loaded_root] {
  set info [fsim::object info $child]
  if {[dict get $info name] eq "ready"} {
    set ready_ref $child
    set ready_info $info
  }
}
if {$ready_ref eq "" || [dict get $ready_info kind] ne "signal" ||
    [dict get $ready_info width] != 1} {
  error "loaded snapshot omitted the expected ready signal: $ready_info"
}
set ready_path [dict get $ready_info path]
if {[fsim::object resolve $ready_path] ne $ready_ref} {
  error "loaded snapshot path did not resolve to the same signal"
}
set run_result [fsim::run 1ns]
if {[dict get $run_result status] ni {completed time_limit}} {
  error "loaded snapshot did not run: $run_result"
}
if {[dict get [fsim::object value $ready_ref] value] ne "1"} {
  error "loaded snapshot did not preserve the expected run value"
}

# Catalog changes after load must not invalidate the independent loaded design.
write_top_source 3 0
set post_load_compilation [fsim::compile -library work -verbosity quiet top.sv]
if {[llength [dict get $post_load_compilation diagnostics]] != 0} {
  error "successful post-load compile returned unexpected diagnostics"
}
set post_load_catalog_ref [fsim::object definition work workspace_pkg]
if {[dict get [fsim::object value $ready_ref] value] ne "1"} {
  error "compiling a replacement changed the loaded snapshot"
}
set deletion [fsim::library delete work]
if {![dict get $deletion deleted]} {
  error "post-load library delete did not report deletion"
}
assert_stale_catalog_reference $post_load_catalog_ref library-delete
if {[dict get [fsim::object value $ready_ref] value] ne "1" ||
    [dict get [fsim::object info $ready_ref] path] ne $ready_path} {
  error "deleting the compiled library changed the loaded snapshot"
}

if {![catch {fsim::elaborate missing_top} message]} {
  error "missing top unexpectedly elaborated"
}
if {[lindex $::errorCode 0] ne "FSIM"} {
  error "workspace failure did not set an fsim errorCode"
}
set diagnostics [fsim::diagnostics]
if {[llength $diagnostics] == 0
    || ![string match "FSIM-WS-*" [dict get [lindex $diagnostics end] code]]} {
  error "workspace failure did not publish a structured diagnostic"
}
puts workspace-commands-ok
)FSIM_TCL";

    std::istringstream input;
    std::ostringstream output;
    std::ostringstream error;
    const auto result = run_cli(
        { "fsim", "tcl", "-c", script }, input, output, error);
    if (result != 0) {
        throw std::runtime_error(
            "Tcl workspace command session failed: " + error.str());
    }
    assert(output.str().find("workspace-commands-ok") != std::string::npos);
    const auto error_text = error.str();
    std::size_t error_count = 0;
    for (auto offset = error_text.find("error["); offset != std::string::npos;
        offset = error_text.find("error[", offset + 1U)) {
        ++error_count;
    }
    if (error_count != 1U
        || error_text.find("error[FSIM-WS-") == std::string::npos
        || error_text.find("missing_top") == std::string::npos) {
        throw std::runtime_error(
            "Tcl workspace script emitted unexpected diagnostics: "
            + error_text);
    }
}

} // namespace

int main()
{
    try {
        run_workspace_commands_test();
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
    std::cout << "Tcl workspace command tests passed\n";
    return 0;
}
