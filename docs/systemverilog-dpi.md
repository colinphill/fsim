<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemVerilog DPI-C support

Fsim v3 provides a bounded, checked DPI-C boundary for selectable
SystemVerilog-2005/2009/2012/2017/2023 profiles. It retains imports and exports in
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
| Declarations | `import`/`export "DPI-C"`, `pure`/`context`, functions/tasks, optional C aliases and formal names, input defaults, exact owner/tokens/spans, compatible shared C-linkage profiles, typed export resolution | `frontend_dpi_tests.cpp` |
| Standard C layer | Installed root-level `svdpi.h`, canonical scalar/vector/time layouts, four-state constants, packed-word helpers, complete nondeprecated open-array/scope/context/time declarations, and exported version/bit-select/part-select utilities | `runtime_dpi_abi_c_test.c`, `runtime_foreign_abi_cpp_test.cpp`, `runtime_dpi_tests.cpp` |
| Scalars | Arbitrary bounded two-/four-state aval/bval planes, shortreal/real/realtime exact bits, strict UTF-8 strings, simulation-owned chandle identities | `runtime_dpi_tests.cpp` |
| Composites | Recursive fixed arrays, structs, arbitrary-width two-/four-state enums with exact literal identity, canonical flattened leaves, checked layout and resource budgets | `runtime_dpi_tests.cpp` |
| Open arrays | Declared range direction, multidimensional indices, contiguous and element pointers, direction-aware mutation, epoch lifetime | `runtime_dpi_tests.cpp` |
| Scope and callbacks | Exact simulation-owned `svScope`, bounded reentrant foreign-call contexts, context-local set/restore, caller/time/user-data services, declaration-checked exported callbacks, disabled-state acknowledgement, exception rollback | `runtime_dpi_tests.cpp`, `dpi_test_plugin_c.c` |
| Imported tasks | Declaration-checked `context` task registration, deterministic timed suspension/resume, cancellation, nested callback re-entry, transactional publication, scheduler containment | `runtime_dpi_tests.cpp` |
| Plug-ins | Relative versioned manifests, language-correct C11/C++20 argv compile/link plans, exact symbols, ABI descriptor, SHA-256 provenance/cache identity, leased lifetime and quarantine | `runtime_dpi_tests.cpp` |

## Live call service: argument types

`fsim simulate` calls an import through libffi
(`src/app/application_dpi_foreign.cpp`) with the C representation of
IEEE 1800-2023 Annex H. The lowerer maps each formal to a signature code
(`hir_dpi_profile` in `lowerer_hir_callable.cpp`, `dpi_c_type.cpp`); the
service parses the signature once per call site.

| Formal | C representation |
|---|---|
| `byte`, `shortint`, `int`, `longint` (and unsigned), `real`, `shortreal`, `chandle`, `string`, scalar `bit`/`logic` | by value for an input, by pointer for an output or inout (H.7.4, H.8) |
| packed array, packed struct or union, `bit [W-1:0]`, `logic [...]` | `svBitVecVal*` / `svLogicVecVal*` in canonical form (H.7.7); `W` is any constant expression of the declaring scope (a literal, a parameter, `$clog2`, arithmetic) |
| `integer`, `time` | a 4-state vector of 32 or 64 bits |
| enumeration | the base type |
| sized unpacked array `int a [3:0]`, `bit [7:0] b [2][3]` | a pointer to the elements in normalized order: the lowest index is C index 0 in each dimension, row-major, in every direction (H.7.3, H.7.6) |
| open array (`int a []`, `bit [] v`, `logic [] a [3:1]`) | an `svOpenArrayHandle` (H.12); an unsized unpacked dimension takes the actual's range, an unsized packed dimension its linearized width; `svLeft`, `svRight`, `svLow`, `svHigh`, `svSize`, `svDimensions`, `svGetArrayPtr`, `svGetArrElemPtr*` and the vector accessors work on it |
| unpacked struct | a pointer to a C structure with the C compiler's layout of the members (H.7.8); nested structures and unpacked-array members included |
| array of a struct, `real`, `shortreal`, `chandle` or enumeration | as an array of its element type, sized or open |

An open-array formal of any direction accepts a dynamic array or queue
actual: its size when called is the size of the array, the elements the C code
writes copy back, and the size is unchanged (35.5.6.1). A dynamic array,
queue or open array formal is an error in an export, as is a class formal, an
unpacked union, and a result that is not a small value (`FSIM-SV-SEM-460` to
`-463`). Exports with sized unpacked or struct formals are called from C with
the element pointer or the structure pointer as the argument.

Not supported: an array of `string` formals, and a structure with a `string`,
dynamic array, queue or class member.

## Negative matrix

| Boundary | Rejected before publication |
|---|---|
| Parse/profile | Missing terminator/kind/name/profile, invalid link string, reference-passed import formals, malformed defaults, illegal qualifier/alias, incompatible shared linkage profiles, duplicates, conflicts, missing export target |
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

The standard C context is installed only while a governed callback or imported
task body executes. Nested entry uses a bounded 64-frame thread-local stack;
exit, exception, suspension, and disable paths remove the exact top frame.
Calls made without an active foreign invocation return their standard no-
context failure value and cannot reach another simulation.

The retained implementation evidence covers exact-LLVM Debug runtime and
source-policy gates plus the completed Batch 186 Debug and Release regressions.
Fresh sanitizer and hosted Linux/Windows CI execution remains a Batch 188
release obligation.

## Source inventory

- Public ABI/runtime: `dpi_plugin_abi.h`, `dpi_marshalling.hpp`,
  `dpi_scope.hpp`, `dpi_callback.hpp`, `dpi_task.hpp`, `dpi_plugin.hpp`, and the
  installed `svdpi.h` standard C surface.
- Runtime implementations: `dpi_marshalling.cpp`, `dpi_composite.cpp`,
  `dpi_open_array.cpp`, `dpi_scope.cpp`, `dpi_callback.cpp`, `dpi_task.cpp`,
  `dpi_plugin.cpp`, and `svdpi.cpp`.
- Frontend: `verilog_parser_dpi.cpp` plus DPI declaration ownership in
  `design.hpp` and parser integration in the Verilog parser core/units.
- Evidence: `frontend_dpi_tests.cpp`, `runtime_dpi_tests.cpp`, the independent
  `dpi_test_plugin.cpp`/`dpi_test_plugin_c.c` fixture, and
  `dpi_bad_abi_plugin.cpp`.

The deprecated implementation-representation portion of historical `svdpi.h`
interfaces is intentionally omitted; the canonical representation remains the
portable boundary. Unrestricted foreign profiles, producer-specific extensions,
and foreign source inclusion remain outside this bounded slice. VPI, VHPI and UVM are separate current v3
interfaces documented in `systemverilog-vpi.md`, `vhdl-vhpi.md` and
`systemverilog-uvm.md`; they are not DPI extensions.
