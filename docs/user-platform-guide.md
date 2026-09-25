<!-- SPDX-License-Identifier: Apache-2.0 -->
# User, platform, migration, and troubleshooting guide

This guide describes the current workspace interface. The
[workspace guide](workspace-mode.md) covers library and snapshot commands.
Retained release qualification remains the authority for released binaries and
measured Windows results.

## Supported configurations

| Host | Build compiler | Execution backend | Current support boundary |
|---|---|---|---|
| Linux x86-64 | C++20 GCC or Clang | Interpreter, or LLVM 22.1.8 O0/O2 | Source builds in Debug and Release |
| Windows x86-64 | Pinned LLVM-MinGW 20260616 UCRT | Interpreter, or matching MinGW LLVM 22.1.8 O0/O2 | Source builds in Debug and Release |

`FSIM_LLVM_MODE=OFF` builds the interpreter without LLVM. `ON` requires exactly
LLVM 22.1.8, and `AUTO` uses that exact package when it is found. Mixing an
MSVC-target LLVM package with the LLVM-MinGW build is not supported.

The Windows source contains explicit MSVC-compatible command planning,
dependency parsing, `/bigobj`, CRT, member-pointer, response-file, and DLL
loading contracts. Those contracts protect code portability and SystemC
producer diagnostics; they do not make native MSVC or clang-cl a qualified release
binary target. Do not report an MSVC/clang-cl result, threshold, or package as
supported without a real final-release run and an updated support matrix.

The native integration set is fixed to Accellera SystemC 3.0.2, the governed
SCV 2.0.1 source and its recorded compatibility patches, and Tcl 9.0.4 or a later 9.0
patch release. SQLite 3.53.4 is bundled for workspace library catalogs; a
system SQLite installation is unnecessary. The old fsim SystemC facade and custom kernel are not supported.
The current plug-in ABI is the official Accellera bridge described in the
[SystemC guide](systemc-subset.md).

## Build and first run

Linux development builds use the checked-in preset:

```sh
cmake --preset dev
cmake --build --preset dev --parallel 12
ctest --preset dev --parallel 12
```

On Windows, use the pinned LLVM-MinGW archive and forward-slash paths:

```powershell
$env:LLVM_MINGW_ROOT = 'C:/llvm-mingw-20260616-ucrt-x86_64'
cmake --preset windows-llvm-mingw `
  -DLLVM_DIR=C:/msys64/clang64/lib/cmake/llvm
cmake --build --preset windows-llvm-mingw --parallel 12
ctest --preset windows-llvm-mingw --parallel 12
```

Select a workspace by changing to its directory, then run the three phases:

```sh
fsim check --lang systemverilog tb.sv
fsim compile --library work tb.sv
fsim elaborate work.tb
fsim simulate
```

fsim stores its generated files under `.fsim`. Recompilation replaces the
relevant library objects, and re-elaboration replaces the `default` snapshot.
Use `--snapshot NAME` for a named snapshot. Use `--verbosity quiet|normal|verbose`
on compile, elaborate, SystemC, and library commands, and `--diagnostics=json`
when scripts need structured codes, severity, paths, and source locations.

The [vertical-slice tutorial](../examples/vertical_slice/README.md) is the
smallest mixed-language example; the
[workspace phase tutorial](../examples/non_project_phases/README.md) adds
independent package/library and SystemC phases.

## Time and resource budgets

Every GitHub Actions job has a 120-minute job limit. That limit bounds the
whole configure/build/test job; it is not a promise that an individual test or
simulation may run for 120 minutes. The `-timeout=10` option in the frontend
fuzz command is a per-input libFuzzer limit and does not change the job limit.

Local fsim builds use at least twelve workers. `-j` or `--jobs` controls
applicable fsim compilation work; `--duration` and `--max-deltas` bound simulation work. Keep
those controls explicit in automation so a scheduler loop or unusually large
native build cannot look like a hung runner. Resource diagnostics are
correctness results: increasing a harness timeout must never hide a delta,
payload, recursion, allocation, or collection limit.

The largest retained UVM 1.2 and UVM 2020 Debug qualification runs used
4,631,912 KiB and 5,220,824 KiB peak RSS respectively. The governed ceiling is
6 GiB. These are Linux measurements, not inferred Windows thresholds. Windows
measurements require the pinned LLVM-MinGW hosted lanes.

## Workspace and cache recovery

The workspace is exactly the current working directory. Local libraries use
`.fsim/libraries/<name>`, mappings use `.fsim/libraries.toml`, snapshots use
`.fsim/snapshots/<name>`, and derived caches default to `.fsim/cache`. A mapped
library directory owns its own `library.sqlite3` catalog and managed artifacts.
Catalogs and snapshots are persistent state; caches can be regenerated.

Use this recovery order:

1. Retain the diagnostic, exact command, working directory, library/snapshot
   names, and compiler identity. Retry once without changing inputs.
2. If a cache key still fails, stop processes that can own that cache or a
   loaded native plugin. Confirm the diagnostic identifies a path inside the
   intended workspace's cache.
3. Move only the failing cache entry, or that workspace's `.fsim/cache`, aside.
   Preserve `.fsim/libraries`, `.fsim/snapshots`, and mapped directories.
4. Repeat the failed phase. For a rejected library object, recompile its source
   with the current build and recompile stale consumers. Re-elaborate to replace
   a rejected snapshot. Do not edit SQLite records or artifact metadata.

`fsim library objects NAME` lists managed IDs and definitions.
`fsim library delete-object NAME OBJECT_ID` removes a selected object;
`fsim library delete NAME` removes a library's managed state. Whole-library
removal preserves unrelated files in mapped directories and removes this
workspace's mapping. Existing snapshots remain usable after library deletion.
Use `library unmap NAME` when only the mapping should be removed.

`FSIM-CACHE-0002` and `FSIM-CACHE-0004` report discarded or unusable cache
state. `FSIM-SC-I001` through `FSIM-SC-I003` identify invalid incremental
metadata or publication. `FSIM-SC-I004` is the translation-unit compile and
dependency-scan boundary; its message distinguishes an invalid compiler/input
from a cache-lock directory or acquisition failure. When it names
`systemc/incremental/locks`, preserve that exact path, verify that its parent is
writable and present, stop possible owners, and retry. Current fsim creates the
lock parent before acquisition, so repeated "path not found" is a product or
filesystem failure worth reporting, not a reason to weaken locking.

## Windows paths and loaded files

Use a short workspace and cache root for native builds, especially when source
names, keyword profiles, and cache identities are long. Public CLI, mapping,
diagnostic, and artifact paths are UTF-8; Windows entry points convert once to
native paths. CMake variables and mapping files should use forward slashes to avoid
PowerShell, CMake, TOML, and C/C++ escape ambiguities.

fsim invokes compilers with argument arrays and uses UTF-16 response files when
needed; do not embed shell quoting inside individual compiler-option arguments. Generated plug-ins use
safe DLL search and stay loaded for the owning simulation. Windows therefore
may refuse to rename or remove a plug-in or its directory until simulation
teardown and all input streams are closed. End the owning process before cache
repair or manual workspace relocation. A permissions change is not a lock repair.

## Migration from earlier fsim builds

Project mode and its `build`, `run`, and `--project` interface are removed.
fsim no longer discovers or reads `fsim.toml` for public commands. Migrate the
source lists, standards, libraries, and simulation controls into explicit
workspace commands:

1. Select a workspace directory and retain the original HDL and SystemC sources.
2. Compile package providers first, then consumers, with their language
   standards, include paths, definitions, and compilation-unit policy.
3. Create external library mappings with `library map NAME DIRECTORY` before
   compiling into them. A mapped directory contains the new library catalog;
   an old standalone `.fsimlib` is not a workspace catalog.
4. Compile SystemC sources with `systemc compile --library NAME`, then run
   `systemc link --library NAME` to register their factories.
5. Elaborate the top names and select a default or named snapshot. Run
   `simulate`, `debug`, or `tcl` against that snapshot.
6. Compare the simulation transcript, final values, debugger observations, and
   VCD/FST output before retiring the previous workflow.

Public phases no longer take `--object`, `--design`, or artifact `--output`
arguments. Rebuild old objects, plugins, and snapshots from source instead of
editing their schemas. Coverage merge/report retain their output-file options.

The removed fsim SystemC facade/custom kernel must be replaced with official
Accellera 3.0.2 modules and the current bridge macros. The sole
`fsim_plugin_init_v1` symbol remains the current ABI entry point despite its
stable suffix. SQLite catalogs, snapshots, native caches, checkpoints, and
plugins each retain their own current-version validation; no in-place
conversion of older development state is promised.

## Failure triage

Start with the first fsim diagnostic, compiler error, or test assertion, not a
later cascade. Record the executable version, host/toolchain, configuration,
working directory, exact command, library mappings, snapshot name, and whether the failure
is cold-cache, warm-cache, interpreter, O0, O2, or debug specific.

Then isolate the smallest boundary:

- `check` failure: preserve source profile and preprocessing/include inputs.
- `compile` failure: retain the source profile, package dependencies, and
  compiler response/dependency output. A failed update preserves the old library.
- `elaborate` failure: check top names, mapped libraries, stale dependencies,
  and snapshot publication diagnostics.
- `simulate` failure: compare interpreter and compiled engines from the same
  snapshot; retain time, delta, stop status, and trace finalization.
- artifact rejection: record found and required identities, then regenerate
  directly from source with the current producer.
- trace failure: retain both the final diagnostic and any staging/lock path; a
  partial VCD/FST is not a successful result.
- test failure: require the expected PASS/finish/value evidence as well as exit
  status, because an assertion, crash, or wrapper defect may obscure the first
  diagnostic.

Search the [diagnostic catalog](diagnostics.md) by exact code before changing a
limit or deleting state. When reporting a native or Windows issue, include the
shortest reproducer and the diagnostic path exactly as rendered; do not infer
Windows behavior from a Linux pass.

## Release records

The [v3 release documentation](changelog-v3.md),
[known issues](known-issues-v3.md), and
[implementation plan](implementation_plan_v3.md) identify released and
in-progress work. Older v2 records remain historical evidence for their own
binaries. Local correctness results and hosted Linux/Windows results must be
reported separately; a Linux pass does not establish Windows behavior.
