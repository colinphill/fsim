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
| Standard C layer | Installed root-level `svdpi.h`, canonical scalar/vector/time layouts, four-state constants, packed-word helpers, complete nondeprecated open-array/scope/context/time declarations, and exported version/bit-select/part-select utilities | `runtime_dpi_abi_c_test.c`, `runtime_foreign_abi_cpp_test.cpp`, `fsim.foreign.dpi-types` |
| Scalars and vectors | Two-/four-state `svBit`/`svLogic` and packed `svBitVecVal`/`svLogicVecVal` planes, shortreal/real, strings, chandles; bit-vector function results rejected (35.5.5) | `fsim.foreign.dpi-types`, `fsim.foreign.dpi-strings`, `fsim.foreign.dpi-result-vector` |
| Composites | Unpacked structures, fixed arrays and enumerations in the Annex H normalized layout | `fsim.foreign.dpi-aggregates`, `fsim.foreign.dpi-types` |
| Open arrays | Declared ranges, multidimensional indices, element pointers, direction-aware mutation, dynamic-array and queue actuals | `fsim.foreign.dpi-aggregates` |
| Scope and exports | `svScope` set/get, caller and time services, exported functions and tasks in module, `$unit` and generate scopes | `fsim.foreign.dpi-scope-*`, `fsim.foreign.dpi-time-scope`, `fsim.foreign.dpi-unit-export`, `fsim.foreign.dpi-generate-export` |
| Imported tasks | `context` tasks that suspend in exported tasks, disable with `svIsDisabledState`/`svAckDisabledState` | `fsim.foreign.dpi-disable`, `fsim.foreign.dpi-disable-export` |
| Libraries | C and C++ shared libraries given with `--sv-lib`, resolved by C name at elaboration and called through libffi | `fsim.foreign.dpi-*` |

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

An array of `string` formals, sized (`string s [3]`) or open (`string s []`),
passes one `const char*` per element: a sized array as a pointer to the
elements, lowest index first in every dimension, an open array as an
`svOpenArrayHandle` whose elements `svGetArrElemPtr*` returns as `const
char**` (H.7.4, H.12). The actual is a fixed-size unpacked array of strings of
any rank, and the direction may be input, output or inout.

Not supported: a dynamic array or queue of strings as the actual of an array
formal, an array of strings in an export, and a structure with a `string`,
dynamic array, queue or class member.

## Export scopes and the disable protocol

An export called from the C code of a context import runs in the scope the
call chain is in: the scope of the import declaration, or the one `svSetScope`
names (35.5.3). Three kinds of declaration have a scope:

| Export declared in | Scope that serves it |
| --- | --- |
| a module, program or interface | each instance, named by its hierarchical path |
| a generate block (35.7: in the scope of the function it exports) | each instance of the block, named by its path (`top.h.blk`, `top.h.lp[1]`), also an iteration of a loop generate |
| the compilation unit (35.5.2) | every scope: a chain in any scope may call it |

A package's exports have no export service: `svGetScopeFromName` names no
package (H.9.3), so no call chain runs in one, and a context import declared
in a package runs in the instance that calls it (35.5.3). This is the strict
reading; an export of a package is reached through none of the DPI scope
utilities. An export called from a VPI callback outside a context-import call
chain is an error with a diagnostic, also after `svSetScope`.

An export whose callable is not defined in the generate block that declares it
is bound to the module's callable of that name, as before. The generate
block's export is not part of the module's DPI declarations: `--dpi-header`
does not list it.

A `disable` that covers the import call (a `disable fork`, a kill, or a named
block that holds the call) puts the import in the disabled state (35.9). A
`disable` that covers only the exported subroutine the import is suspended in
targets the export itself: it returns 0, `svIsDisabledState()` stays 0, and the
import continues.

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

- Public ABI/runtime: `dpi_plugin_abi.h` (frozen layout) and the installed
  `svdpi.h` standard C surface.
- Runtime implementations: `svdpi.cpp` (standard C layer) and the live call
  service `application_dpi_foreign.cpp`, with C type mapping in
  `dpi_c_type.cpp`.
- Frontend: `verilog_parser_dpi.cpp` plus DPI declaration ownership in
  `design.hpp` and parser integration in the Verilog parser core/units.
- Evidence: `frontend_dpi_tests.cpp`, the ABI probes, and the
  `fsim.foreign.dpi-*` tests (`tests/fixtures/foreign/foreign_dpi_*.sv` with
  their `tests/runtime/foreign_dpi_*_plugin.c` libraries).

The deprecated implementation-representation portion of historical `svdpi.h`
interfaces is intentionally omitted; the canonical representation remains the
portable boundary. Unrestricted foreign profiles, producer-specific extensions,
and foreign source inclusion remain outside this bounded slice. VPI, VHPI and UVM are separate current v3
interfaces documented in `systemverilog-vpi.md`, `vhdl-vhpi.md` and
`systemverilog-uvm.md`; they are not DPI extensions.
