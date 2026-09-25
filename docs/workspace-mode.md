<!-- SPDX-License-Identifier: Apache-2.0 -->
# Managed workspaces, libraries, and snapshots

The current working directory is the workspace. fsim keeps generated state in
its `.fsim` directory and does not search parent directories for a workspace or
load `fsim.toml`. Change directory before invoking fsim to select a workspace.

```text
workspace/
  source.sv
  .fsim/
    libraries.toml
    libraries/
      work/
        library.sqlite3
        ... fsim-managed compilation artifacts ...
    snapshots/
      default/
      regression/
    cache/
```

`work` is the default compilation library. Other local libraries use
`.fsim/libraries/<name>`. Each library's SQLite catalog records the source
ownership, named HDL definitions or SystemC factories, dependencies, and
managed artifact locations. Use fsim commands to update that state; artifact
filenames and database contents are implementation details.

## Compile, elaborate, and simulate

From a directory containing `counter.sv` and `tb.sv`:

```sh
fsim check --lang systemverilog --standard 2017 counter.sv tb.sv
fsim compile --library work counter.sv tb.sv
fsim elaborate work.tb
fsim simulate
```

`check` parses and analyzes the supplied sources without publishing a library
or snapshot. `compile` creates the library if needed and publishes its named
definitions. Language is inferred from source extensions unless `--lang` and
`--standard` select an explicit source profile. `elaborate` finds the requested
top in library metadata and publishes the `default` snapshot. `simulate`
selects that snapshot when `--snapshot` is omitted.

Top names may be unqualified (`tb`) or library-qualified (`work.tb`). A
language qualifier such as `sv:work.tb` or `vhdl:work.tb` is available when
names are ambiguous. Ordinary elaboration does not require a language option.
Several tops share one simulation scheduler:

```sh
fsim elaborate work.device_tb vendor.glbl --snapshot regression
fsim simulate --snapshot regression
```

Use `ALIAS=TOP` when two roots need different hierarchy names, for example
`fsim elaborate left=work.tb right=work.tb`. Ambiguous lookup is an error;
specify the library or language instead of relying on source order.

Compilation produces one managed artifact per primary named HDL unit where
the unit model permits it. Package classes and supporting definitions stay
with their owning unit. A SystemC translation unit has one managed object
artifact. fsim selects all filenames.
Objects sharing mutable compilation-unit state remain in one artifact so the
simulation retains one shared declaration. Recompile all sources in that
compilation unit together; fsim diagnoses an incomplete replacement.

Recompiling a source replaces its previous definitions and artifacts after a
successful publication. Definitions removed from that source disappear from
the catalog. Failed compilation preserves the previously published library.
Re-elaboration similarly replaces the selected snapshot after success.

```sh
fsim compile --library work counter.sv
fsim elaborate work.tb --snapshot regression
fsim simulate --snapshot regression --engine interpreter
```

The other snapshots remain available. A snapshot contains the compiled state
needed for simulation, including selected SystemC plugin payloads. It remains
usable after sources are removed, libraries are recompiled, or library objects
are deleted. A SystemC snapshot still requires a compatible native host and
fsim/SystemC ABI.

## Reuse compiled packages

Compile package providers before their consumers:

```sh
fsim compile --library work constants.sv
fsim compile --library work consumer.sv
fsim elaborate work.consumer
fsim simulate
```

The second command uses the package already in `work`; it does not require
the old package source to remain present. VHDL packages, package bodies, and
contexts follow the same library workflow with their language's visibility
rules. SystemVerilog package types, constants, functions, and classes remain
available through compiled metadata.

Use `--search-library NAME` with compilation or elaboration when a provider
is in another logical library. Compiled consumers record provider identities.
After a provider changes or is deleted, recompile affected consumers before
creating a new snapshot. fsim diagnoses stale dependencies rather than using
old folded values. Existing snapshots retain their earlier behavior.

## Map an out-of-tree library

Create a mapping before compiling into an external library directory:

```sh
mkdir -p ../shared/vendor
fsim library map vendor ../shared/vendor
fsim compile --library vendor ../vendor-src/cells.sv
fsim library list
fsim library objects vendor
fsim compile --library work --search-library vendor tb.sv
fsim elaborate work.tb --search-library vendor
```

fsim stores the mapping in `.fsim/libraries.toml`; relative paths are resolved
against the workspace directory. The mapped directory itself contains
`library.sqlite3` and the library's managed artifacts. Another workspace can
map that directory under the same logical library name and use its compiled
definitions without the original sources.

`fsim library unmap vendor` removes the workspace mapping and preserves the
external library. Mapping a library does not rename its logical identity.

## Inspect and delete library objects

```sh
fsim library objects
fsim library objects vendor
fsim library objects vendor --verbose
fsim library delete-object vendor OBJECT_ID
fsim library delete vendor
```

`library objects` defaults to `work` and lists the managed object IDs and
definitions; `--verbose` includes source details. Pass an ID from that list to
`delete-object`. Deletion removes
its definitions from future lookup. Whole-library deletion removes its
managed catalog and artifacts; for an external library it also removes this
workspace's mapping and preserves unrelated files in the directory. Other
workspaces using that library must account for the deleted definitions.

## Compile and link SystemC

```sh
fsim systemc compile --library models bridge.cpp helper.cpp
fsim systemc link --library models
fsim compile --library work tb.sv
fsim elaborate work.tb --search-library models --snapshot mixed
fsim simulate --snapshot mixed
```

Compilation tracks each C++ translation unit independently. Linking uses the
current objects in the target library and records the resulting plugin's
exported factories in the catalog. After recompiling a translation unit, link
that library again before elaboration. Publication uses fresh native image
paths so a previous snapshot can retain a loaded plugin on Windows.

## Progress, debugging, and simulation controls

Compile, elaborate, SystemC, and library commands support progress verbosity. Use
`--verbosity quiet` or `-q` to suppress progress, and `--verbosity verbose` or
`-v` for source and publication detail. Diagnostics remain available at every
verbosity; `--diagnostics=json` selects structured diagnostics. The global
`--color auto|always|never` option controls text diagnostic colors; automatic
mode detects terminal output and honors `NO_COLOR`.

```sh
fsim compile -v --library work tb.sv
fsim elaborate work.tb --snapshot debug -q
fsim debug --snapshot debug
fsim debug --snapshot debug inspect.tcl top.counter
fsim tcl -c 'puts [fsim::version]'
```

Debug and Tcl use the `default` snapshot unless `--snapshot NAME` selects
another one. `fsim debug` loads that snapshot before starting the Tcl
debugger; `fsim tcl` starts a general Tcl session and `fsim::load ?SNAPSHOT?`
loads simulation state. The [Tcl console and automation guide](tcl.md)
documents all commands, structured workspace/object/debug/provenance results,
console controls, history, and redirected behavior. Within Tcl, the built-in
`cd` changes the active workspace; catalog references from the previous
workspace become stale while the loaded snapshot remains usable. Subsequent
fsim commands and completion use the new workspace, and interactive history
uses its `.fsim/tcl_history` file by default. Relative paths from
`FSIM_TCL_HISTORY` are resolved against the new workspace; an absolute path
stays fixed.

`simulate` accepts engine, duration, delta, seed, file-I/O, and trace options,
for example `fsim simulate --engine compiled --duration 100ns --trace run.vcd`.
Generated caches stay under `.fsim` by default. Explicit trace, coverage
database, and HDL file-I/O outputs remain user-selected outputs.

Project-mode `build`, `run`, `--project`, and project-file discovery have been
removed. Compile, elaborate, simulate, and SystemC phase commands no longer
accept artifact paths through `--object`, `--design`, or `--output`. Coverage
merge and report still accept `--output` for their output files.
