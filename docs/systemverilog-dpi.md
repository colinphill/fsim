<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemVerilog DPI-C support

Fsim v2 provides a bounded, checked DPI-C boundary for selectable
SystemVerilog-2005/2009/2012/2017 profiles. It retains imports and exports in
their exact revision, compatibility profile, compilation-unit, package,
module, interface, or program scope; validates callable profiles; marshals
supported values without host-layout aliases; and loads portable C/C++ shared
libraries through a versioned C ABI.

The boundary participates in the frozen SystemVerilog release contract. Its
bounded profile and configured materialization/work ceilings are explicit API/
resource limits, not general SystemVerilog legality limits. See
the [SystemVerilog release audit](v1-systemverilog-release-audit.md) for the
governed clause, arbitrary-width, engine, artifact, and public-API evidence.

## Supported surface

| Area | Supported behavior | Primary evidence |
|---|---|---|
| Declarations | `import`/`export "DPI-C"`, `pure`/`context`, functions/tasks, optional C aliases, exact owner/tokens/spans, typed export resolution | `frontend_dpi_tests.cpp` |
| Scalars | Arbitrary bounded two-/four-state aval/bval planes, shortreal/real/realtime exact bits, strict UTF-8 strings, simulation-owned chandle identities | `runtime_dpi_tests.cpp` |
| Composites | Recursive fixed arrays, structs, arbitrary-width two-/four-state enums with exact literal identity, canonical flattened leaves, checked layout and resource budgets | `runtime_dpi_tests.cpp` |
| Open arrays | Declared range direction, multidimensional indices, contiguous and element pointers, direction-aware mutation, epoch lifetime | `runtime_dpi_tests.cpp` |
| Scope and callbacks | Exact simulation-owned `svScope`, context-local set/restore, exported callbacks, disabled-state acknowledgement, exception rollback | `runtime_dpi_tests.cpp` |
| Imported tasks | Deterministic timed suspension/resume, cancellation, nested callback re-entry, transactional publication, scheduler containment | `runtime_dpi_tests.cpp` |
| Plug-ins | Relative versioned manifests, argv compile/link plans, exact symbols, ABI descriptor, SHA-256 provenance/cache identity, leased lifetime and quarantine | `runtime_dpi_tests.cpp` |

## Negative matrix

| Boundary | Rejected before publication |
|---|---|
| Parse/profile | Missing terminator/kind/name/profile, invalid link string, illegal qualifier/direction/default/alias, duplicates, conflicts, missing export target |
| Values | Width/plane/high-bit mismatch, X/Z into two-state, kind/direction mismatch, nonfinite real, malformed UTF-8, embedded NUL, stale chandle |
| Composite/open array | Invalid descriptor/enum/layout, overflow/depth/element/bit budget, rank/index/leaf mismatch, noncontiguous whole pointer, input mutation, stale epoch |
| Scope/callback/task | Malformed or duplicate scope, foreign handle, missing linkage, arity/direction error, unacknowledged disable, cancellation, exception, scheduling failure |
| Manifest/artifact | Version/name/path/source/symbol errors, duplicates, missing compiler/artifact/symbol/descriptor, ABI size/version/pointer/flags/name mismatch, unreadable artifact |

All mutable callback/task results remain private until success. Exceptions do
not cross the scheduler boundary. A failed loader publishes neither a partial
symbol table nor a loaded plug-in facade.

Enum descriptors and values never pass through a 64-bit host projection.
Marshalling compares the complete declared-width `aval`/`bval` planes, retains
signed and `X`/`Z` literal identity, and rejects wrong-width, duplicate, or
two-state-unknown profiles transactionally.

## Platform and engine matrix

| Dimension | Covered behavior |
|---|---|
| Linux/POSIX | C++20, PIC, hidden-by-default compilation, explicit default-visible C exports, local eager loading, `.so` artifact |
| Windows/LLVM-MinGW | C++20 UCRT, C calling convention, explicit exported symbols, safe DLL-directory loading, `.dll` artifact; MSVC-style plans remain source-portability checks, not release-package evidence |
| Interpreter | Scalar values traverse the owning aval/bval marshal/unmarshal path before the real C ABI call |
| Compiled O0/O2 | The same leased callable and exact values cross both compiled-call paths |
| Multiple roots | Distinct simulation identities and current-scope contexts invoke the same plug-in without aliasing |
| Suspension/re-entry | A scheduled imported task resumes through the real symbol; nested callbacks restore task and caller scope |

The retained implementation evidence covers exact-LLVM Debug runtime and
source-policy gates plus full non-sanitized Debug and Release regressions.
Final fresh sanitizer and hosted Linux/Windows CI execution remains a Batch 177
release obligation.

## Source inventory

- Public ABI/runtime: `dpi_plugin_abi.h`, `dpi_marshalling.hpp`,
  `dpi_scope.hpp`, `dpi_callback.hpp`, `dpi_task.hpp`, `dpi_plugin.hpp`.
- Runtime implementations: `dpi_marshalling.cpp`, `dpi_composite.cpp`,
  `dpi_open_array.cpp`, `dpi_scope.cpp`, `dpi_callback.cpp`, `dpi_task.cpp`,
  `dpi_plugin.cpp`.
- Frontend: `verilog_parser_dpi.cpp` plus DPI declaration ownership in
  `design.hpp` and parser integration in the Verilog parser core/units.
- Evidence: `frontend_dpi_tests.cpp`, `runtime_dpi_tests.cpp`, the independent
  `dpi_test_plugin.cpp`/`dpi_test_plugin_c.c` fixture, and
  `dpi_bad_abi_plugin.cpp`.

Unrestricted foreign profiles, varargs and producer-specific extensions remain
outside the bounded DPI-C contract. VPI, VHPI and UVM are separate current v2
interfaces documented in `systemverilog-vpi.md`, `vhdl-vhpi.md` and
`systemverilog-uvm.md`; they are not DPI extensions.
