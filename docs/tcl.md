<!-- SPDX-License-Identifier: Apache-2.0 -->
# Tcl console and automation

fsim embeds Tcl 9 for workspace automation and debugger control. Tcl commands
use the current directory as the managed workspace; generated libraries and
snapshots live under `.fsim`.

## Start a Tcl session

```text
fsim tcl [--snapshot NAME] [-c SCRIPT ... | SCRIPT [ARG ...]]
fsim debug [--snapshot NAME] [-c SCRIPT ... | SCRIPT [ARG ...]]
```

`fsim tcl` opens a general Tcl session. It does not load a snapshot on entry;
use `fsim::load_snapshot ?SNAPSHOT?` to load one. The optional `--snapshot` selects the
default snapshot for commands that load simulation state. `fsim debug` loads
the selected snapshot (default `default`) before starting its Tcl session or
running its script.

Use `-c` or `--command` more than once to evaluate Tcl text in order. A script
file may instead follow the options, with remaining arguments passed through
the standard Tcl variables `argv0`, `argv`, and `argc`. A script file and
`--command` cannot be combined.

```sh
fsim tcl
fsim tcl -c 'puts [fsim::version]'
fsim tcl inspect.tcl top.counter
fsim debug --snapshot regression
fsim debug --snapshot regression -c 'puts [fsim::debug status]'
```

## Command catalog

`fsim::help` returns the registered command list in name order.
`fsim::help COMMAND` returns that command's usage and description.

Top-level commands that return objects are named `get_*`; other top-level
single actions are `verb_noun` (such as `read_signal` and `add_callback`),
except the flow commands `compile`, `link`, `elaborate`, `run`, and `stop`.
Commands with several operations take a subcommand instead
(`fsim::library list`, `fsim::object info`); the `get_` rule does not apply
to subcommands.

From global Tcl code the `fsim::` prefix is optional: `get_signals` runs
`fsim::get_signals`. A Tcl built-in or global procedure of the same name
takes precedence.

| Command | Forms and purpose |
| --- | --- |
| `fsim::help` | `?COMMAND?`; list and describe registered fsim Tcl commands. |
| `fsim::version` | Return fsim and C API versions. |
| `fsim::get_workspace` | Return the current workspace and its libraries. |
| `fsim::load_snapshot ?SNAPSHOT?` | Load a managed snapshot into the Tcl session. |
| `fsim::compile` | `?-lang LANGUAGE? ?-library LIBRARY? ?-standard STANDARD? ?-verbosity LEVEL? ?-include DIRECTORY? ?-define NAME[=VALUE]? ?-compiler PATH? ?-compile-option ARG? SOURCE ...`; compile files into a managed library and return a dictionary. `-include`, `-define`, and `-compile-option` repeat; `-compiler` and `-compile-option` apply to SystemC sources. |
| `fsim::link` | `?-library LIBRARY? ?-compiler PATH? ?-link-option ARG? ?-link-library ARG? ?-verbosity LEVEL?`; link a library's compiled SystemC objects into its plug-in, register its modules for elaboration, and return a dictionary. `-link-option` and `-link-library` repeat. |
| `fsim::elaborate` | `?-snapshot NAME? ?-verbosity LEVEL? TOP ...`; create a managed snapshot and return a dictionary. |
| `fsim::config` | `set KEY VALUE`, `get KEY`, `unset KEY`, `list ?PATTERN?`, or `keys ?PATTERN?`; set or read the default options of fsim commands (see [Configuration](#configuration)). |
| `fsim::coverage` | `fsm\|exclude add\|remove\|show\|list\|clear ...`; manage coverage FSM hints and exclusions applied at elaboration. |
| `fsim::bind` | `add\|remove\|show\|list\|clear ...`; bind design instances to units or port resolvers at elaboration. |
| `fsim::library` | `list`, `map NAME DIRECTORY`, `unmap NAME`, `objects NAME`, `delete-object NAME ARTIFACT_ID`, or `delete NAME`. |
| `fsim::object` | `roots`, `resolve PATH`, `children REFERENCE`, `info REFERENCE`, `read REFERENCE`, `write REFERENCE VALUE`, `definitions ?LIBRARY?`, or `definition LIBRARY NAME`. |
| `fsim::get_signals` | Return the paths of loaded signals. |
| `fsim::read_signal` | `SIGNAL`; read a signal value. |
| `fsim::deposit_signal`, `fsim::force_signal` | `SIGNAL VALUE`; change or force a signal value. |
| `fsim::release_signal` | `SIGNAL`; release a forced value. |
| `fsim::run` | `?DURATION?`; run the loaded simulation. |
| `fsim::get_simulation` | Return the loaded simulation state. |
| `fsim::get_diagnostics`, `fsim::clear_diagnostics` | Return or discard structured diagnostics. |
| `fsim::debug` | `status`, `step`, `continue`, `break`, `watch`, `frames`, `frame`, `scope`, `inspect`, `restart`, or `provenance`. |
| `fsim::get_provenance` | `?PATH?`; return source provenance for a path or all design roots. |
| `fsim::sdf`, `fsim::tracing` | Configure or inspect SDF annotation and simulation tracing. |
| `fsim::add_callback`, `fsim::remove_callback`, `fsim::get_callbacks` | Register, remove, or return simulation callbacks. `fsim::stop` requests a stop at a safe point. |

The debugger subcommands accept these forms:

```text
fsim::debug status
fsim::debug step ?statement|process|phase|delta|time?
fsim::debug continue ?DURATION?
fsim::debug break add|list|remove|clear ?ARG ...?
fsim::debug watch add|list|remove|clear ?ARG ...?
fsim::debug frames
fsim::debug frame ?INDEX?
fsim::debug scope ?PATH?
fsim::debug inspect PATH_OR_REFERENCE
fsim::debug restart ?SNAPSHOT?
fsim::debug provenance ?PATH?
```

## Configuration

Every fsim command first runs the per-user `fsim.tcl`, then the workspace's
`fsim.tcl`. The per-user script is `$XDG_CONFIG_HOME/fsim/fsim.tcl`, else
`~/.config/fsim/fsim.tcl`, or `%APPDATA%\fsim\fsim.tcl` on Windows. A missing
script is skipped. `--no-config` or `FSIM_NO_CONFIG=1` runs neither, which
scripted regressions use to stay independent of the user's settings. A
configuration script is ordinary Tcl code, as a Makefile is code: run only
scripts you trust. A failing script stops the command with `FSIM-TCL-CFG-001`,
which names the script and carries the Tcl error trace.

```tcl
# fsim.tcl
fsim::config set compile.sv.standard 2023
fsim::config set elaborate.time_resolution 1ns
fsim::config set elaborate.search_libraries {models vendor}
fsim::config set simulate.max_deltas 1000
fsim::coverage fsm add -instance tb.u_ctrl -current state -legal {IDLE RUN}
fsim::bind add tb.u_dut -target sv:work.fast_cell
```

A key sets the default of one command option. An option given on the command
line wins over the default; for a list key, such as
`elaborate.search_libraries`, the command-line values are added after the
configured ones. A key named `command.language.option` applies to sources of
that language (`vhdl`, `verilog`, or `sv`) and wins over `command.option`. Values
are checked when they are set: a boolean accepts `1`/`true`/`yes`/`on` and
`0`/`false`/`no`/`off`, and an integer is unsigned.

`fsim::config set KEY VALUE` records a value and `unset KEY` removes it.
`get KEY` returns the value in force or the default. `list ?PATTERN?` returns
a dictionary of the keys that are set, each with its value and origin: its
`layer` and `source`, the script and line or `interactive`. `keys ?PATTERN?` returns every documented
key with its kind, default, and summary. A setting made in a Tcl session wins
over both scripts until the session ends.

FSM hints, coverage exclusions, and bindings are keyed entries:
`fsim::coverage fsm` is keyed by instance and current-state object,
`fsim::coverage exclude` by its `-name` (generated when omitted), and
`fsim::bind` by instance. Each takes `add`, which replaces an entry with the
same key, `remove`, `show`, `list ?PATTERN?`, and `clear`, and reports the
origin of each entry. The [coverage guide](code-coverage.md) describes the FSM
hint and exclusion options.

Elaboration records each `elaborate.*` setting and keyed entry in force, with
its origin, in the snapshot. When a snapshot is simulated with other settings
in force, `FSIM-TCL-CFG-003` names the keys that differ; run `fsim elaborate`
to apply them.

<!-- fsim-config-keys:begin -->
| Key | Kind | Default | Purpose |
| --- | --- | --- | --- |
| `compile.code_coverage` | boolean | `false` | compile with code coverage; elaborate.code_coverage must match |
| `compile.compilation_unit` | text |  | compilation unit of every language: file, source-set, combined |
| `compile.defines` | list |  | preprocessor defines (NAME or NAME=VALUE) of Verilog and SystemVerilog |
| `compile.include_directories` | list |  | include directories of Verilog and SystemVerilog, relative to the workspace |
| `compile.jobs` | integer | `0` | parallel compilation jobs; 0 picks the machine's count |
| `compile.library` | text | `work` | library compiled sources go into |
| `compile.sv.compatibility` | list |  | SystemVerilog compatibility switches |
| `compile.sv.compilation_unit` | text | `source-set` | SystemVerilog compilation unit |
| `compile.sv.defines` | list |  | SystemVerilog preprocessor defines |
| `compile.sv.include_directories` | list |  | SystemVerilog include directories |
| `compile.sv.standard` | text | `2017` | SystemVerilog standard |
| `compile.sv.timescale` | text |  | SystemVerilog default timescale (UNIT/PRECISION) |
| `compile.sv.uvm_release` | text | `none` | governed UVM release SystemVerilog sources compile against |
| `compile.timescale` | text |  | default timescale (UNIT/PRECISION, 1ns/1ps) of Verilog and SystemVerilog design elements no `timescale applies to |
| `compile.verilog.compatibility` | list |  | Verilog compatibility switches |
| `compile.verilog.compilation_unit` | text | `source-set` | Verilog compilation unit |
| `compile.verilog.defines` | list |  | Verilog preprocessor defines |
| `compile.verilog.include_directories` | list |  | Verilog include directories |
| `compile.verilog.standard` | text | `2005` | Verilog standard |
| `compile.verilog.timescale` | text |  | Verilog default timescale (UNIT/PRECISION) |
| `compile.vhdl.compatibility` | text |  | VHDL compatibility profile |
| `compile.vhdl.compilation_unit` | text | `file` | VHDL compilation unit |
| `compile.vhdl.standard` | text | `2008` | VHDL standard |
| `elaborate.aot` | boolean | `false` | build the static kernel's native code at elaboration |
| `elaborate.code_coverage` | boolean | `false` | instrument the design for code coverage; compile.code_coverage must match |
| `elaborate.delay_mode` | text | `typical` | delay mode: minimum, typical, maximum |
| `elaborate.generics` | list |  | top-level generic and parameter overrides, NAME=VALUE |
| `elaborate.jobs` | integer | `0` | parallel elaboration jobs; 0 picks the machine's count |
| `elaborate.optimization` | text | `2` | optimization level 0 to 3 |
| `elaborate.search_libraries` | list |  | libraries searched for unqualified design units, in order |
| `elaborate.seed` | text | `1` | random seed, or 'random' |
| `elaborate.time_resolution` | text | `auto` | simulation time resolution, or 'auto' |
| `elaborate.vpi` | list |  | VPI modules LIB[:BOOTSTRAP] loaded for elaboration |
| `simulate.coverage_output` | text |  | code-coverage database written at the end of simulation |
| `simulate.delay_mode` | text | `typical` | delay mode: minimum, typical, maximum |
| `simulate.duration` | text |  | time to simulate, e.g. 10us; empty runs to the end |
| `simulate.foreign_probes` | boolean | `false` | statement, call and suspend callbacks for VPI/VHPI libraries |
| `simulate.max_deltas` | integer | `100000` | delta cycles allowed in one time step |
| `simulate.seed` | text | `1` | random seed, or 'random' |
| `simulate.sv_libs` | list |  | DPI-C libraries loaded for simulation |
| `simulate.trace` | boolean | `true` | write the waveform trace |
| `simulate.trace_compression` | text | `auto` | trace compression: auto, none, deterministic |
| `simulate.trace_filters` | list |  | trace selections |
| `simulate.trace_format` | text | `auto` | trace format: auto, vcd, fst |
| `simulate.trace_output` | text |  | waveform trace file |
| `simulate.trace_report_limit` | integer | `4096` | trace diagnostics reported before the rest are counted |
| `simulate.vhpi` | list |  | VHPI modules LIB[:BOOTSTRAP] loaded for simulation |
| `simulate.vhpi_end_of_step_timers` | boolean | `false` | accept VHPI timers registered at the end of a time step |
| `simulate.vhpi_registry` | list |  | VHPI tabular registry files read for simulation |
| `simulate.vpi` | list |  | VPI modules LIB[:BOOTSTRAP] loaded for simulation |
| `systemc.compile_options` | list |  | options passed to the SystemC compiler |
| `systemc.compiler` | text |  | SystemC C++ compiler |
| `systemc.defines` | list |  | SystemC preprocessor defines |
| `systemc.include_directories` | list |  | SystemC include directories |
| `systemc.link_libraries` | list |  | libraries linked into SystemC plug-ins |
| `systemc.link_options` | list |  | options passed to the SystemC linker |
<!-- fsim-config-keys:end -->

## Structured workspace results

Workspace and build commands return Tcl dictionaries and lists, so scripts can
read individual fields without parsing printed messages.

The Tcl built-in `cd` changes the active fsim workspace. After a successful
directory change, workspace and library commands use the new directory and
subsequent fsim commands and completion use the new workspace. Catalog
references from the prior workspace become stale. References to the already
loaded snapshot remain valid until that session is successfully reloaded or
restarted. A failed `cd` leaves the workspace and references unchanged.
`cd` is rejected inside a simulation callback, leaving the current workspace
and its references unchanged.

```tcl
set workspace [fsim::get_workspace]
puts [dict get $workspace managed_directory]
foreach library [dict get $workspace libraries] {
    puts "[dict get $library library]: [dict get $library path]"
}

set compilation [fsim::compile -library work -verbosity quiet top.sv]
puts [dict get $compilation owned_units]
set snapshot [fsim::elaborate -snapshot inspectable -verbosity quiet top]
puts [dict get $snapshot snapshot]
```

Each entry from `fsim::library list` is a dictionary with `library`, `path`,
and `mapped` fields. Compile results include the selected library and language,
source files, object count, owned units, and diagnostics. Elaboration results
include the snapshot name, selected roots, counts, and diagnostics.

SystemC sources (`.cpp`, `.cc`, `.cxx`, or `-lang systemc`) compile to one
managed object each; `link` then builds the library's plug-in and registers
its modules, which elaboration needs:

```tcl
fsim::compile -library models -include include -define WIDTH=8 bridge.cpp
set linked [fsim::link -library models]
puts [dict get $linked modules]
fsim::elaborate models.bridge
```

Link results include the library, the plug-in artifact, its registered
`modules`, the number of linked objects, and diagnostics. Compile options
added with `-include`, `-define`, `-compiler`, and `-compile-option`, and link
options added with `-compiler`, `-link-option`, and `-link-library`, apply to
that command only.

Compile, link, and elaborate results also include a `messages` list. Each entry is
one nonempty progress line from the operation. Quiet verbosity returns an
empty list; normal verbosity includes progress, and verbose verbosity adds
detail. The list contains progress only: operational results remain in their
named fields, and diagnostics remain separately available in the `diagnostics`
field and `fsim::get_diagnostics`.

## Loaded objects and debugger results

`fsim::object roots` and `fsim::object children` return opaque references. Keep
and pass each reference unchanged; use `fsim::object info` to inspect it.
Loaded-object references are tied to the loaded session and become stale after
a successful snapshot load, debugger restart, or destruction of the referenced
runtime object. Catalog references returned by `fsim::object definition` are
invalidated when their library is successfully replaced, deleted, or remapped.
Failed library operations preserve existing references. Stale and wrong-kind
references raise Tcl errors.

```tcl
set root [lindex [fsim::object roots] 0]
set root_info [fsim::object info $root]
puts [dict get $root_info path]

set signal [fsim::object resolve top.ready]
set value [fsim::object read $signal]
puts [dict get $value value]
```

`fsim::object info` reports fields such as `identity`, `kind`, `name`, `path`,
`parent`, `type`, `width`, and `dimensions`; language, library, and source
provenance are included when available. `fsim::object read` returns a typed
dictionary. `fsim::object write` validates the value and the simulation control
state before applying a supported mutation.

The debugger also returns structured dictionaries for status, breakpoints,
watches, scopes, frames, and inspected objects.

```tcl
set start [fsim::debug status]
set breakpoint [fsim::debug break add time 20ns]
fsim::debug continue
set stopped [fsim::debug status]
puts "time=[dict get $stopped time], finished=[dict get $stopped finished]"
```

## Source provenance and errors

`fsim::get_provenance ?PATH?` and `fsim::debug provenance ?PATH?` return a list of
dictionaries. Records include the design path, language, library, owning unit,
source path and identity, and source line and column. Additional standard,
instance, and native plug-in fields are present when available. Provenance
comes from the compiled design or snapshot and remains available when original
source files have been removed.

```tcl
set records [fsim::get_provenance top.ready]
if {[llength $records] != 0} {
    set owner [lindex $records 0]
    puts "[dict get $owner source_path]:[dict get $owner source_line]"
}
```

Operational failures raise Tcl errors and set `::errorCode`; fsim also retains
structured entries in `fsim::get_diagnostics`.

```tcl
if {[catch {fsim::elaborate missing_top} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorcode]
    puts [fsim::get_diagnostics]
}
```

Use `fsim::clear_diagnostics` to clear retained entries. The diagnostic list
contains dictionaries with `severity`, `code`, `message`, `span`, and `notes`.

## Interactive console

The terminal console uses Tcl's command completeness rules. Enter submits a
complete command; incomplete input continues on a `...` prompt. Tab completes
Tcl names and fsim commands, options, paths, libraries, snapshots, HDL
definitions, design objects, and debugger entities. Suggestions are checked
against the live workspace and loaded-session generations before they are
inserted.

| Key | Action |
| --- | --- |
| Left/Right, Ctrl-B/Ctrl-F | Move by character; Ctrl-F at the end also invokes completion. |
| Home/End, Ctrl-A/Ctrl-E | Move to the start or end of the visual line. |
| Ctrl-Left/Right, Shift-Left/Right, Alt-B/F | Move by word. |
| Up/Down | Move through visual lines; in the completion menu, move through candidates. |
| Ctrl-P/Ctrl-N | Move backward or forward through command history. |
| Ctrl-R/Ctrl-S | Search backward or forward through command history. |
| Tab | Trigger contextual completion; in the completion menu, move to another candidate. |
| Shift-Tab | Insert a newline; in the completion menu, move to another candidate. |
| Ctrl-J / Shift-Enter | Insert a newline; with more than nine candidates, show the full completion list. |
| Enter | Insert a newline for an incomplete Tcl command, otherwise submit; accept the selected menu candidate. |
| Ctrl-Z/Ctrl-_ and Ctrl-Y | Undo and redo editing. |
| Backspace/Delete, Ctrl-W, Ctrl-U/Ctrl-K | Delete a character, word, or to the visual line boundary. |
| Ctrl-C | Cancel the current input. |
| Esc | Dismiss the completion menu; outside the menu, cancel the current input. |
| Ctrl-D | Delete at the cursor, or end the console session when input is empty. |

In the completion menu, number keys `1`–`9` select an entry. Page Down or
Ctrl-J displays the full completion list. Bracketed paste inserts its text as
one undo step and normalizes line endings.

Persistent history defaults to `.fsim/tcl_history` with a limit of 200
commands. Set `FSIM_TCL_HISTORY=off` to disable history persistence, or set it
to a path to choose another history file. Relative paths are resolved from the
current workspace directory. After `cd` changes the workspace, the default
history file follows its `.fsim/tcl_history`; an explicit relative path is also
resolved from the new workspace, while an absolute path stays fixed. Set
`FSIM_TCL_HISTORY_LIMIT` to a nonnegative integer to change the limit; `0`
disables history, and values above 10000 are capped at 10000. Invalid limit
values are ignored.

Use `fsim::transcript start ?PATH?` to append a running plain-text log of
entered commands and all stdout/stderr output, including Tcl `puts`, simulator
output, and diagnostics. The default path is `.fsim/tcl.log` in the current
workspace; an explicit path is resolved when recording starts and remains
fixed if Tcl later changes directory. `fsim::transcript status` returns its
active state and path, and `fsim::transcript stop` closes it. Set
`FSIM_TCL_LOG` to a path to start recording when the interactive console opens.
The log starts after the `start` command, records later commands and output in
order, and appends across sessions. Terminal color codes are omitted from the
log.

When stdin and stdout are redirected, fsim uses a plain multiline reader with
no editor, prompts, or terminal control sequences. It still accumulates input
until Tcl considers the command complete. An incomplete command at end of
input is reported as an error.

## Diagnostic color

Text diagnostics use severity colors: notes are cyan, warnings yellow, errors
red, and fatal errors bold red. `--color auto` (the default) colors terminal
diagnostics and honors a nonempty `NO_COLOR` environment variable. `--color
always` forces color for text diagnostics from regular CLI commands, including
redirected error output; `--color never` disables it. `--diagnostics json`
never includes ANSI escape sequences, regardless of color mode. In Tcl's
interactive console, automatic color is enabled only for an interactive
terminal; a nonempty `NO_COLOR` turns it off. When a `tcl` or `debug` command
writes diagnostics to a redirected error stream, fsim keeps that output plain
even with `--color always`.
The interactive console prints diagnostics as they are reported, beside other
output, while retaining their structured forms in `fsim::get_diagnostics`.

## Tcl-disabled builds

Configure with `-DFSIM_TCL_MODE=OFF` to build without the Tcl interpreter and
interactive editor. The `tcl` and `debug` CLI commands remain recognized but
return an unavailable diagnostic (`FSIM-TCL-0001`) when run. CLI help and
non-Tcl commands remain usable.
