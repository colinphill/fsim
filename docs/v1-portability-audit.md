<!-- SPDX-License-Identifier: Apache-2.0 -->
# v1 Linux and Windows portability audit

> Historical record: this document freezes the Batch 129 v1 audit. For the
> current v2 toolchain, resource and final-qualification boundary, use the
> [release evidence and post-v2 guide](release-and-post-v2.md).

This is the Batch 129 portability inventory and repair queue. The
[feature matrix](feature-matrix.md) remains the release authority. This audit
does not broaden the supported host contract: fsim v1 supports 64-bit x86-64
Linux and Windows, and requires the same bounded language behavior on both.

Task 1 is a static, local audit. It does not claim fresh hosted Windows
execution and does not inspect GitHub Actions. Hosted release-candidate proof
remains assigned to the mandatory Batch 130 CI boundary.

## Supported host and toolchain contract

The checked build rejects non-Linux/non-Windows systems, non-x86-64 processors,
and non-64-bit pointers. The release matrix covers nine hosted
configurations plus one scheduled local sanitizer configuration:

| Matrix ID | Host/compiler/backend | Configurations | Current purpose |
|---|---|---|---|
| `PORT-CI-LINUX-GCC` | Ubuntu, GCC, LLVM disabled | Debug and Release | Strict portable C++20 build plus interpreter/runtime behavior |
| `PORT-CI-LINUX-LLVM` | Ubuntu 24.04, GCC, LLVM 22.1.8 | Debug and Release | Required JIT O0/O2, object cache, debugger, and source-map behavior |
| `PORT-CI-SANITIZERS` | Local Linux, GCC, LLVM disabled, ASan/UBSan | Debug | Ten-batch memory, lifetime, integer, and undefined-behavior containment |
| `PORT-CI-FUZZ` | Ubuntu 24.04, Clang/libFuzzer | RelWithDebInfo | Deterministic frontend lexer/parser smoke corpus |
| `PORT-CI-WINDOWS-LLVM-MINGW` | Windows Server 2022, LLVM-MinGW 20260616 UCRT, LLVM disabled | Debug and Release | Native Windows frontend/elaboration/interpreter/runtime/tool behavior with Tcl 9.0.4 |
| `PORT-CI-WINDOWS-LLVM-MINGW-LLVM` | Windows Server 2022, LLVM-MinGW 20260616 UCRT, LLVM 22.1.8 | Debug and Release | PE/COFF JIT, GNU Windows ABI, cache, plug-in, debugger, and optimized behavior |

Linux hosted build steps use `--parallel 4`; the Windows build and test steps
use `--parallel 4`. Local builds use at least eight
workers, and Ninja link/archive concurrency is bounded by the default
`FSIM_LINK_POOL_SIZE=8`. Hosted CI excludes sanitizer instrumentation,
including from its libFuzzer smoke target; the scheduled local ASan/UBSan run
remains part of every ten-batch boundary.
The ordinary hosted jobs retain 45-minute budgets. Every MSVC-compatible
Windows test executable receives an 8 MiB stack reserve, while scoped locals retains a
60-second test bound and the two deliberately large application/container
hosts retain explicit longer bounds.

## Platform boundary inventory

Only the following 14 authored C/C++ source, header, and test files contain an
explicit `_WIN32`, `_MSC_VER`, or MSVC-compatible implementation branch. Every
other authored language implementation file is expected to remain portable
C++20 and to reach platform services through these bounded seams.

| Area | Explicit platform files | Existing contract |
|---|---|---|
| Public ABI | `include/fsim/api.h`, `include/fsim/systemc_abi.h` | Windows import/export and calling-convention visibility remain layout compatible with the strict C tests |
| Application/support | `src/app/application_analysis.cpp`, `src/app/tcl.cpp`, `src/support/environment.cpp` | Target triples, console interrupts, environment access, Tcl paths, and error transport use native host APIs behind common results |
| Dynamic loading/cache | `src/platform/dynamic_library.cpp`, `src/compiler/object_cache.cpp` | `LoadLibraryW`/`GetProcAddress` and POSIX `dlopen` share lifetime/error policy; cache locking and atomic replacement use native primitives |
| SystemC compiler | `src/systemc/plugin_compiler.cpp`, `src/systemc/plugin_compiler_common.cpp`, `src/systemc/plugin_compiler_dependencies.cpp`, `src/systemc/plugin_compiler_internal.hpp`, `src/systemc/plugin_compiler_process.cpp` | GNU and MSVC command plans, quoting, response files, dependency closure, PE/shared-library output, CRT selection, and process status remain explicit |
| Platform fixtures | `tests/systemc/plugin_compiler_test.cpp`, `tests/systemc/plugin_matrix_test.cpp` | Native compiler selection is isolated while shared cache, lifecycle, and semantic assertions remain identical |

Build-system platform selection is concentrated in `CMakeLists.txt`,
`CMakePresets.json`, `cmake/FsimTcl.cmake`, `cmake/FsimWarnings.cmake`,
`tests/CMakeLists.txt`, and `.github/workflows/ci.yml`. Windows selects the
supported Tcl build and CRT, MSVC warning flags, DLL placement, bounded stacks,
and PE plug-in behavior. Linux selects POSIX loading/process services and
GCC/Clang warning and sanitizer flags. Both use the official Accellera SystemC
runtime; no platform-specific fiber backend remains.

## Cross-platform invariants

The following behavior must be byte- or value-identical after intentional
host normalization:

1. Language parsing, typed HIR, elaboration, diagnostics codes, source spans,
   DesignIR identity, interpreter results, LLVM O0/O2 results, and scheduling.
2. Cache keys and telemetry after canonical generic-path normalization; native
   object bytes and native compiler fingerprints remain host-specific.
3. Debugger values, callbacks, VCD bytes, report messages, and Tcl/API state.
4. Text fixtures compare logical `\n`; binary file and memory fixtures compare
   exact bytes and open streams in binary mode where host translation matters.
5. Temporary files use `std::filesystem::temp_directory_path`, unique fixture
   directories, error-code cleanup, and paths passed as individual arguments
   rather than shell-concatenated command text.
6. Native failures are contained and converted to stable fsim diagnostics or
   application errors; exceptions do not cross the public C or SystemC C ABI.
7. Tests do not depend on filesystem iteration order, locale-specific messages,
   environment mutation outside the fixture, wall-clock timing, or addresses.
8. Debug and Release keep assertions used as test checks enabled, and neither
   optimizer nor iterator-debug settings change supported language behavior.

## Open portability queues

These queues describe evidence or repair work, not confirmed defects. A queue
closes only with checked-in positive/negative evidence and the smallest shared
implementation repair needed by that evidence.

| Queue ID | Task | Audit surface | Required closure |
|---|---:|---|---|
| `B129-T2-GNU` | 2 | **Closed.** GCC/Clang warning sets, optimization, byte/integer/iterator conversions, standard-library count types, API layout expressions, and disabled-feature builds | The complete LLVM/Tcl-disabled Clang 22 Debug tree compiles with `-Werror`; explicit byte/distance/count conversions and C++ API layout preserve behavior, while focused Clang and GCC+LLVM cross-layer gates pass. Sanitizer and Release proof remain in Task 10 |
| `B129-T3-MSVC-DEBUG` | 3 | **Closed.** MSVC Debug recursive frames, UTF-8 BOM source, Windows logical paths, CRLF line accounting, C/C++ test-host stacks, and bounded timeouts | BOM-aware SV/VHDL frontend and SV elaboration fixtures retain byte offsets and exact paths/spans; one static gate protects the common 8 MiB stack policy for every C/C++ test host plus scoped/container/application timeout bounds |
| `B129-T4-MSVC-RELEASE` | 4 | **Closed.** Optimized lifetime, initialization, aliasing, signed conversion, iterators, concurrency, scheduler order, Release assertions, CRT/iterator ABI, and deterministic plug-in outputs | Shared runtime/application fixtures cover optimizer-sensitive behavior; a static contract plus executable command-plan checks protect `/UNDEBUG`, `/O2` versus `/Od /Z7`, the exact CRT, iterator ABI, and unique object/PDB/import-library outputs. Full Release proof remains in Task 10 |
| `B129-T5-WINLLVM` | 5 | **Closed.** PE/COFF JIT, symbols, GNU Windows ABI, stack/unwind, DLL ownership, native cache, and source maps | The application cache names the complete x64 ABI environment; a static contract protects native target/data-layout/CPU/features, strict C layouts, safe DLL lookup/lifetime, atomic Windows cache replacement, and the LLVM-MinGW matrix while shared executable fixtures prove O0/O2 cold/warm/edit/debug behavior. Hosted proof remains in Batch 130 |
| `B129-T6-SYSTEMC` | 6 | **Closed.** Facade and strict C ABI, compiler command plans, Windows quoting/response files, dependency discovery, CRT, loader lifetime, threads, and lifecycle | A fingerprinted test launcher carries required parent compiler discovery options without weakening cache reuse; Clang and GCC+LLVM compiler/cache/lifecycle matrices pass, and a static contract protects GNU/MSVC commands, Windows process execution, ABI checks, exception containment, and loader ownership |
| `B129-T7-TOOLS` | 7 | **Closed.** Public API, CLI, Tcl, debugger, VCD, file/memory I/O, Unicode paths, environment, callbacks, failures, and exit status | One UTF-8/native-path seam now serves API, CLI, project/application/cache, Tcl, and runtime file paths; Windows uses UTF-16 argv/environment APIs, while Unicode API/direct-source fixtures and existing callback, binary I/O, debugger/VCD, Tcl, and exit-status checks pass on GCC+LLVM and Clang |
| `B129-T8-RESOURCES` | 8 | **Closed.** Bounded hosted builds, eight-worker local builds/link pool, executable stack, memory, timeouts, fixture isolation, and phase traces | A static gate protects the five four-worker Linux/Windows build/test steps, the eight-link pool, compact Debug objects, explicit 60/120/600/1200-second bounds, and scoped/SystemC phases |
| `B129-T9-DIFFERENTIAL` | 9 | **Closed.** Complete Debug/Release, interpreter/LLVM O0/O2, cold/warm/edit, API/ABI, plug-in, debugger/VCD, path/newline matrix | `docs/v1-portability-corpus.txt` provides 20 exact ID/surface/mode/CTest/evidence/marker rows; its gate rejects missing IDs, modes, registrations, files, or source markers, and all 20 unique owning/gate CTests pass locally. Hosted execution remains Batch 130's mandatory CI boundary |

Task 10 completed the accumulated sanitizer, source/catalog, and full local
Debug and Release regressions. ASan/UBSan passed 92/92 in 545.85 seconds;
exact LLVM Debug and Release passed 95/95 in 249.39 and 210.55 seconds. The
final gates cover 1,635 diagnostics, 437 authored sources, 1,081 required
release rows with no explicit evidence gaps, 105 conformance expectations, and
20 portability rows. Batch 129 was not a mandatory CI-inspection boundary.
Hosted proof remains assigned to Batch 130.
