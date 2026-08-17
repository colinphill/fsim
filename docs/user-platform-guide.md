<!-- SPDX-License-Identifier: Apache-2.0 -->
# User, platform, migration, and troubleshooting guide

This guide describes the current v2.0.0 interface. It separates
supported release configurations from source-portability checks and records
recovery steps that preserve artifact and cache identity. Retained release
qualification remains the authority for binaries and measured Windows results.

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
producer diagnostics; they do not make native MSVC or clang-cl a v2 release
binary target. Do not report an MSVC/clang-cl result, threshold, or package as
supported without a real final-release run and an updated support matrix.

The native integration set is fixed to Accellera SystemC 3.0.2, the governed
SCV 2.0.1 source and four compatibility patches, and Tcl 9.0.4 or a later 9.0
patch release. The old fsim SystemC facade and custom kernel are not supported.
The current plug-in ABI is the official Accellera bridge described in the
[SystemC guide](systemc-subset.md).

## Build and first run

Linux development builds use the checked-in preset:

```sh
cmake --preset dev
cmake --build --preset dev --parallel 8
ctest --preset dev
```

On Windows, use the pinned LLVM-MinGW archive and forward-slash paths:

```powershell
$env:LLVM_MINGW_ROOT = 'C:/llvm-mingw-20260616-ucrt-x86_64'
cmake --preset windows-llvm-mingw `
  -DLLVM_DIR=C:/msys64/clang64/lib/cmake/llvm
cmake --build --preset windows-llvm-mingw --parallel 8
ctest --preset windows-llvm-mingw --parallel 8
```

Run `fsim check` before `build` or `run`. Use
`--diagnostics=json` when a script needs stable codes, severity, paths, and
source locations. The [vertical-slice tutorial](../examples/vertical_slice/README.md)
is the smallest project/build/run/debug example; the
[non-project tutorial](../examples/non_project_phases/README.md) covers explicit
object, design, and SystemC plug-in phases.

## Time and resource budgets

Every GitHub Actions job has a 120-minute job limit. That limit bounds the
whole configure/build/test job; it is not a promise that an individual test or
simulation may run for 120 minutes. The `-timeout=10` option in the frontend
fuzz command is a per-input libFuzzer limit and does not change the job limit.

Local project builds use at least eight workers. `-j` or `--jobs` controls fsim
build parallelism; `--duration` and `--max-deltas` bound simulation work. Keep
those controls explicit in automation so a scheduler loop or unusually large
native build cannot look like a hung runner. Resource diagnostics are
correctness results: increasing a harness timeout must never hide a delta,
payload, recursion, allocation, or collection limit.

The largest retained UVM 1.2 and UVM 2020 Debug qualification runs used
4,631,912 KiB and 5,220,824 KiB peak RSS respectively. The governed ceiling is
6 GiB. These are Linux measurements, not inferred Windows thresholds. Windows
measurements require the pinned LLVM-MinGW hosted lanes.

## Cache and artifact recovery

Project and explicit SystemC builds use `.fsim-cache` relative to the project
base unless `[build].cache_path` or the applicable command option selects
another directory. Entries are content keyed, checksummed, and published under
per-key locks. Portable `.fsimobj`, `.fsimdesign`, `.fsimlib`, `.fsimscobj`, and
`.fsimscplugin` directories are immutable release artifacts, not scratch cache
directories.

Use this recovery order:

1. Retain the complete diagnostic, its path, the exact command, manifest, and
   compiler identity. Retry once without changing inputs; an abandoned staging
   directory or missing cache pair is normally reconstructed by the locked
   writer.
2. If the same cache key still fails, stop every fsim process that can own the
   cache or a loaded native plug-in. Confirm the diagnostic path is inside the
   intended project cache.
3. Remove only the failing key or, when its identity cannot be recovered, move
   that project's `.fsim-cache` aside. Never delete a shared parent, an
   installed artifact, or a mapped `.fsimlib` as a cache repair.
4. Re-run `fsim check`, then the failed phase. Regenerate a rejected portable
   artifact from source with the current fsim build; do not edit its metadata
   or relax checksum, ABI, producer, or schema validation.

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
names, keyword profiles, and cache identities are long. Public CLI, manifest,
diagnostic, and artifact paths are UTF-8; Windows entry points convert once to
native paths. CMake variables and manifests should use forward slashes to avoid
PowerShell, CMake, TOML, and C/C++ escape ambiguities.

fsim invokes compilers with argument arrays and uses UTF-16 response files when
needed; do not add shell quoting to manifest options. Generated plug-ins use
safe DLL search and stay loaded for the owning simulation. Windows therefore
may refuse to rename or remove a plug-in or its directory until simulation
teardown and all input streams are closed. End the owning process before cache
repair or artifact relocation. A permissions change is not a lock repair.

## Migration from earlier fsim builds

v2 is current-only. It does not promise readers for development-era artifact,
cache, checkpoint, plug-in, or ABI schemas. Migrate from sources:

1. Copy the source tree and manifest, not `.fsim-cache` or generated artifact
   directories.
2. Run current `fsim check` and address cataloged language/profile diagnostics.
   Keep explicit language standards, logical libraries, roots, and source-set
   compilation-unit policy in the manifest.
3. Regenerate `.fsimobj`, `.fsimdesign`, mapped `.fsimlib`, SystemC/SCV
   objects and plug-ins, native caches, checkpoints, and traces.
4. Replace the removed fsim SystemC facade/custom-kernel integration with
   official Accellera 3.0.2 modules and the current bridge macros. The sole
   `fsim_plugin_init_v1` symbol is the current ABI entry point despite its
   stable symbol suffix; it is not the removed facade.
5. Compare simulation transcript, final values, debugger observations, and
   VCD/FST output before retiring the old build.

There is no in-place cache conversion and no promise that a permissive parse in
an older development build denotes supported current behavior.

## Failure triage

Start with the first fsim diagnostic, compiler error, or test assertion, not a
later cascade. Record the executable version, host/toolchain, configuration,
working directory, exact command, manifest overrides, and whether the failure
is cold-cache, warm-cache, interpreter, O0, O2, or debug specific.

Then isolate the smallest boundary:

- `check` failure: preserve source profile and preprocessing/include inputs.
- `build` failure: separate analysis, elaboration, native compilation, and
  cache publication; retain the compiler response/dependency output.
- `simulate` failure: compare interpreter and compiled engines from the same
  `.fsimdesign`; retain time, delta, stop status, and trace finalization.
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

The [support matrix](../packaging/v2-support-matrix.tsv),
[release record](../packaging/v2-release-record.txt),
[changelog](changelog-v2.md), and [known issues](known-issues-v2.md) are the
current qualification entry points. Local Debug/Release, sanitizer, package,
install and supply-chain evidence is retained. Post-push hosted Linux/Windows
rows must complete before the annotated `v2.0.0` tag is published.
