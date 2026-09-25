<!-- SPDX-License-Identifier: Apache-2.0 -->
# Batch 188K Tcl closure contract

This document fixes the public command and lifetime behavior for the approved
Batch 188K implementation. The implementation plan remains the acceptance
authority. Existing Tcl commands remain callable unless a new structured
form below supersedes their text-only behavior.

## Command surface

All commands live in `::fsim`; each returns Tcl lists, dictionaries, scalar
values, or an opaque reference. Operational failures raise Tcl errors and add
a structured fsim diagnostic. Tcl scripts may inspect `::errorCode` and
`fsim::diagnostics`; they need not parse terminal output.

| Command | Arguments | Result |
| --- | --- | --- |
| `fsim::compile` | `?-lang LANGUAGE? ?-library LIBRARY? ?-standard STANDARD? ?-verbosity LEVEL? SOURCE ...` | Dictionary with library, language, source list, object count, owned units, and diagnostics. Infer language from extensions when unambiguous. |
| `fsim::elaborate` | `?-snapshot NAME? ?-verbosity LEVEL? TOP ...` | Dictionary with snapshot, selected roots, counts, and diagnostics. A bare top uses metadata lookup; a language prefix is needed only for ambiguous names. |
| `fsim::library list` | none | List of library dictionaries. |
| `fsim::library map` | `NAME DIRECTORY` | Updated library dictionary. |
| `fsim::library unmap` | `NAME` | Updated library dictionary. |
| `fsim::library objects` | `NAME` | List of artifact dictionaries with owned units, sources, and dependencies. |
| `fsim::library delete-object` | `NAME ARTIFACT_ID` | Deletion summary dictionary. |
| `fsim::library delete` | `NAME` | Deletion summary dictionary. |
| `fsim::object roots` | none | List of loaded design root references. |
| `fsim::object resolve` | `PATH` | Loaded design object reference. |
| `fsim::object children` | `REFERENCE` | List of child references. |
| `fsim::object info` | `REFERENCE` | Dictionary with identity, kind, name, path, parent, type, width, dimensions, language, library, and provenance where available. |
| `fsim::object value` | `REFERENCE` | Typed dictionary with kind, shape, and value; unsupported reads have a specific diagnostic. |
| `fsim::object set` | `REFERENCE VALUE` | Typed value after validated mutation at a permitted simulation control boundary. |
| `fsim::object definitions` | `?LIBRARY?` | List of compiled definition dictionaries including packages/classes and members. |
| `fsim::object definition` | `LIBRARY NAME` | Catalog reference for a named compiled definition. |
| `fsim::debug` | `status`, `step`, `continue`, `break`, `watch`, `frames`, `frame`, `scope`, `inspect`, `restart`, `provenance` and their documented operands | Structured results for normal control and inspection. Existing provenance behavior remains compatible. |
| `fsim::transcript` | `start ?PATH?`, `stop`, `status` | Control a running append-only log of entered Tcl commands and stdout/stderr output. |

`fsim::load ?SNAPSHOT?` keeps its existing result schema; it additionally
advances the loaded-session generation after a successful load. `fsim tcl`
starts a general Tcl session. `fsim debug` loads the selected default or named
snapshot into the same Tcl session before accepting Tcl input. Tcl file and
`-c` execution use the same native command services. `-verbosity` accepts
`quiet`, `normal`, or `verbose`.

The workspace follows Tcl's current directory. After `cd`, subsequent fsim
commands and completion use that directory's `.fsim` store. The interactive
history path also follows the selected workspace.

## References and lifetime

References are opaque tokens, never exposed pointer addresses. A token has an
internal kind, stable object identity, and captured generation. Tcl callers
must pass the token unchanged and must not interpret its bytes. Catalog
generations are tracked independently per library, so changing one library
does not invalidate another library's references. A successful recompile,
object deletion, library deletion, or remap invalidates affected catalog
references. A failed operation preserves prior references. An already loaded
snapshot remains usable after a library change.
Changing workspace with Tcl `cd` invalidates all catalog references issued in
the previous workspace, including if the user later returns to it, while
preserving references to an already loaded independent snapshot.

Loaded-design references belong to one session generation. A successful
`fsim::load` or `fsim::debug restart` invalidates all references from the
previous loaded session; a failed load/restart preserves them. Runtime object
destruction invalidates that object's reference. A simulation start or step
within the same loaded session retains references to surviving objects.
Stale or wrong-kind references raise a Tcl error with a stable fsim diagnostic
code. They never alias a new object even if an index or path is reused.

Callbacks may read safe object metadata and values at existing safe points.
Workspace changes, debugger execution, and object mutation inside callbacks
are rejected. Mutations outside callbacks validate target kind, width, value,
and runtime state before changing simulation data. Existing
`fsim::deposit`/`force`/`release` remain supported.

## Completion and console

The completion service accepts a UTF-8 input buffer, byte cursor, command
context, available capabilities, and catalog/session generations. It returns
byte replacement ranges, candidates with kind and help, hints, and syntax or
diagnostic spans. It has no terminal or editor types and does not evaluate the
input. Providers cover Tcl names, fsim commands/options, paths, libraries,
snapshots, HDL objects/packages/types, and debugger entities. Results are
deterministically ordered, bounded, and rejected if captured generations
become stale before application.

The console uses Tcl completeness to accumulate multiline input. It supports
character, word, line, and multiline navigation, insert/delete, undo/redo,
history navigation/search, completion cycling, safe paste, resize/redraw,
cancel, interrupt, and EOF. It persists history under `.fsim` with a
configurable limit and an opt-out. Terminal state is restored on every exit.
Output emitted during editing preserves the partial input. Diagnostic styles
are selected from the actual severity; automatic color honors `NO_COLOR`,
with explicit always/never modes. Redirected input/output has no prompts or
terminal control sequences. A Tcl-disabled build has no interactive loop or
editor dependency and reports Tcl/debug unavailable through its diagnostics.
Interactive diagnostics print when reported, with their severity colors, and
remain available as structured Tcl data. The optional transcript records
commands and output without terminal color sequences and survives Tcl `cd`
until stopped.

Acceptance covers source-free snapshots, mixed HDL and SystemC provenance,
definition and runtime object data, all reference invalidation transitions,
callbacks, terminal key sequences, redirected streams, and Tcl-disabled
operation. Existing Tcl tracing, SDF, simulation, and debugger tests must
continue to pass.
