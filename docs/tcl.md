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
use `fsim::load ?SNAPSHOT?` to load one. The optional `--snapshot` selects the
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

| Command | Forms and purpose |
| --- | --- |
| `fsim::help` | `?COMMAND?`; list and describe registered fsim Tcl commands. |
| `fsim::version` | Return fsim and C API versions. |
| `fsim::workspace` | Describe the current workspace and its libraries. |
| `fsim::load ?SNAPSHOT?` | Load a managed snapshot into the Tcl session. |
| `fsim::compile` | `?-lang LANGUAGE? ?-library LIBRARY? ?-standard STANDARD? ?-verbosity LEVEL? SOURCE ...`; compile files into a managed library and return a dictionary. |
| `fsim::elaborate` | `?-snapshot NAME? ?-verbosity LEVEL? TOP ...`; create a managed snapshot and return a dictionary. |
| `fsim::library` | `list`, `map NAME DIRECTORY`, `unmap NAME`, `objects NAME`, `delete-object NAME ARTIFACT_ID`, or `delete NAME`. |
| `fsim::object` | `roots`, `resolve PATH`, `children REFERENCE`, `info REFERENCE`, `value REFERENCE`, `set REFERENCE VALUE`, `definitions ?LIBRARY?`, or `definition LIBRARY NAME`. |
| `fsim::signals` | List paths of loaded signals. |
| `fsim::read` | `SIGNAL`; read a signal value. |
| `fsim::deposit`, `fsim::force` | `SIGNAL VALUE`; change or force a signal value. |
| `fsim::release` | `SIGNAL`; release a forced value. |
| `fsim::run` | `?DURATION?`; run the loaded simulation. |
| `fsim::status` | Describe the loaded simulation state. |
| `fsim::diagnostics` | `?clear?`; read or clear structured diagnostics. |
| `fsim::debug` | `status`, `step`, `continue`, `break`, `watch`, `frames`, `frame`, `scope`, `inspect`, `restart`, or `provenance`. |
| `fsim::provenance` | `?PATH?`; return source provenance for a path or all design roots. |
| `fsim::sdf`, `fsim::trace` | Configure or inspect SDF annotation and simulation tracing. |
| `fsim::on`, `fsim::off`, `fsim::callbacks` | Register, remove, or list simulation callbacks. `fsim::stop` requests a stop at a safe point. |

The debugger subcommands accept these forms:

```text
fsim::debug status
fsim::debug step ?statement|process|phase|delta|time?
fsim::debug continue ?DURATION?
fsim::debug break add|list|delete|clear ?ARG ...?
fsim::debug watch add|list|delete|clear ?ARG ...?
fsim::debug frames
fsim::debug frame ?INDEX?
fsim::debug scope ?PATH?
fsim::debug inspect PATH_OR_REFERENCE
fsim::debug restart ?SNAPSHOT?
fsim::debug provenance ?PATH?
```

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
set workspace [fsim::workspace]
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
set value [fsim::object value $signal]
puts [dict get $value value]
```

`fsim::object info` reports fields such as `identity`, `kind`, `name`, `path`,
`parent`, `type`, `width`, and `dimensions`; language, library, and source
provenance are included when available. `fsim::object value` returns a typed
dictionary. `fsim::object set` validates the value and the simulation control
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

`fsim::provenance ?PATH?` and `fsim::debug provenance ?PATH?` return a list of
dictionaries. Records include the design path, language, library, owning unit,
source path and identity, and source line and column. Additional standard,
instance, and native plug-in fields are present when available. Provenance
comes from the compiled design or snapshot and remains available when original
source files have been removed.

```tcl
set records [fsim::provenance top.ready]
if {[llength $records] != 0} {
    set owner [lindex $records 0]
    puts "[dict get $owner source_path]:[dict get $owner source_line]"
}
```

Operational failures raise Tcl errors and set `::errorCode`; fsim also retains
structured entries in `fsim::diagnostics`.

```tcl
if {[catch {fsim::elaborate missing_top} message options]} {
    puts stderr $message
    puts stderr [dict get $options -errorcode]
    puts [fsim::diagnostics]
}
```

Use `fsim::diagnostics clear` to clear retained entries. The diagnostic list
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

## Tcl-disabled builds

Configure with `-DFSIM_TCL_MODE=OFF` to build without the Tcl interpreter and
interactive editor. The `tcl` and `debug` CLI commands remain recognized but
return an unavailable diagnostic (`FSIM-TCL-0001`) when run. CLI help and
non-Tcl commands remain usable.
