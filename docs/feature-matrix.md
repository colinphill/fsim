<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim feature matrix

This matrix is the source-of-truth inventory for the architecture vertical
slice. It deliberately separates syntax that is represented by a frontend from
syntax that is elaborated and executed. A feature is not a v1 promise merely
because its parser accepts an example.

## Status and evidence

The status column uses these terms:

- **execute**: the precisely bounded form in the row has automated runtime or
  component-execution evidence.
- **parse**: the frontend or manifest loader represents the form, but complete
  legality checking, elaboration, or execution is not demonstrated.
- **v1 target**: required for v1, but the row is not implemented or not
  sufficiently evidenced.
- **deferred**: intentionally outside the v1 release scope.

Evidence columns mean:

- **P+**: a source or input is accepted and its representation is checked.
- **P-**: an invalid or unsupported form is rejected with a targeted
  diagnostic.
- **E**: elaboration/lowering behavior is checked.
- **R**: runtime behavior is checked. An LLVM-only component test is identified
  as such; it is not interpreter/JIT differential evidence.

`—` is an evidence gap, not “not applicable.” An implementation source link
without an automated test is also identified as a gap.

## VHDL-2008 vertical slice

| ID | Precisely bounded feature | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| VH-001 | Case-insensitive identifiers and source spans | parse | [frontend test](../tests/frontend/frontend_tests.cpp) | — | — | — |
| VH-002 | `library`, `use`, and context-reference clauses retained on the following library unit | parse | [frontend representation test](../tests/frontend/frontend_tests.cpp) | [context-declaration rejection](../tests/frontend/frontend_tests.cpp) | — | — |
| VH-003 | Entity plus selected architecture as distinct units | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| VH-004 | `in`/`out` scalar `std_logic` and descending `std_logic_vector` ports | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | [duplicate/default rejection](../tests/frontend/frontend_tests.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| VH-005 | Architecture signal declaration | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | [duplicate/initializer rejection](../tests/frontend/frontend_tests.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| VH-006 | Whole-signal concurrent assignment | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| VH-007 | Process static sensitivity refined by `rising_edge` | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| VH-008 | Simple `if` guard and sequential whole-signal `<=` | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| VH-009 | `elsif`, `else`, and `null` representation | parse | [frontend test](../tests/frontend/frontend_tests.cpp) | — | — | — |
| VH-010 | Identifier and integer-literal unsigned-add expression | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| VH-011 | Logic/string literals and `not`, `and`, `or`, `xor`, and equality expression forms | parse | [parser/lowering implementations; atomic positive tests missing](../src/frontend/vhdl_parser.cpp) | — | — | [common kernels only](../tests/runtime/runtime_tests.cpp) |
| VH-012 | `after` delay syntax with an integer magnitude | parse | [parser implementation; positive test missing](../src/frontend/vhdl_parser.cpp) | — | — | — |
| VH-013 | Call, index, and slice expression nodes | parse | [parser implementation; positive test missing](../src/frontend/vhdl_parser.cpp) | — | — | — |
| VH-014 | Nine-state VHDL frontend typing and literal collapse into the current four-state executable IR | execute | [frontend domain check](../tests/frontend/frontend_tests.cpp) | — | [literal lowering test](../tests/elaboration/elaborator_test.cpp) | [literal mapping test](../tests/elaboration/elaborator_test.cpp) |
| VH-015 | Full nine-state VHDL execution and standard resolution | v1 target | — | — | — | [value-kernel component evidence only](../tests/runtime/runtime_tests.cpp) |
| VH-016 | Direct-entity/component instance nodes with named or positional whole-signal `port map` associations | parse | [frontend instance test](../tests/frontend/frontend_tests.cpp) | [generic-map and complex-actual rejection](../tests/frontend/frontend_tests.cpp) | [VHDL-parent cross-library elaboration](../tests/elaboration/elaborator_test.cpp) | — |

The executable VHDL path currently uses the common four-state SimIR
representation. VH-013 therefore does not imply that `U`, `W`, `L`, `H`, and
`-` survive every operation in an elaborated VHDL design.

## Verilog-2005 and SystemVerilog-2017 vertical slice

| ID | Precisely bounded feature | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| SV-001 | One SystemVerilog module with ANSI ports | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | [duplicate/default rejection](../tests/frontend/frontend_tests.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [application test](../tests/app/application_test.cpp) |
| SV-002 | Verilog-2005 module with basic non-ANSI ports and `output reg` refinement | parse | [frontend test](../tests/frontend/frontend_tests.cpp) | — | — | — |
| SV-003 | Logic/net declarations and constant packed ranges | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | [duplicate/initializer/type rejection](../tests/frontend/frontend_tests.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| SV-004 | Whole-signal continuous assignment with identifier/literal/unsigned-add expression | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| SV-005 | `always_ff @(posedge ...)` with a whole-signal NBA | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [two-edge counter check](../tests/elaboration/elaborator_test.cpp) |
| SV-006 | Verilog `always @(posedge ...)` representation | parse | [frontend test](../tests/frontend/frontend_tests.cpp) | — | — | — |
| SV-007 | `initial begin ... end` with whole-signal blocking assignments | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [application test](../tests/app/application_test.cpp) | [application test](../tests/app/application_test.cpp) |
| SV-008 | Integer `#` delay followed by an assignment or `$finish`, including scaling by an active legal `` `timescale`` | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [application test](../tests/app/application_test.cpp) | [timescale runtime test](../tests/app/application_test.cpp), [C API test](../tests/api/api_test.cpp) |
| SV-009 | Procedural `if`/`else` and nested blocks | parse | [parser implementation; positive test missing](../src/frontend/verilog_parser.cpp) | — | — | — |
| SV-010 | Identifiers, sized literals, and unsigned-add expression | execute | [frontend test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| SV-011 | Unary not, bitwise and/or/xor, and equality expression forms | parse | [parser/lowering implementations; atomic positive tests missing](../src/frontend/verilog_parser.cpp) | — | — | [common kernels only](../tests/runtime/runtime_tests.cpp) |
| SV-012 | Relational, shift, multiply/divide/modulo, logical, ternary, call, index, part-select, and concatenation expression nodes | parse | [parser implementation; positive tests missing](../src/frontend/verilog_parser.cpp) | — | — | — |
| SV-013 | Legal `` `timescale`` magnitudes/units, subsequent-module context, integer-delay scaling, and `auto` precision selection | execute | [frontend context test](../tests/frontend/frontend_tests.cpp) | [coarse-precision directive test](../tests/frontend/frontend_tests.cpp) | [application auto-resolution test](../tests/app/application_test.cpp) | [200-tick scaled-delay test](../tests/app/application_test.cpp) |
| SV-014 | Malformed assignment produces a span-bearing stable diagnostic | parse | — | [frontend test](../tests/frontend/frontend_tests.cpp) | — | — |
| SV-015 | Module instance node with named whole-signal connections | execute | [frontend instance test](../tests/frontend/frontend_tests.cpp) | — | [mixed hierarchy test](../tests/elaboration/elaborator_test.cpp) | [mixed hierarchy test](../tests/elaboration/elaborator_test.cpp) |
| SV-016 | `` `default_nettype`` recognition without silently ignoring unimplemented implicit-net semantics | parse | — | [targeted unsupported diagnostic](../tests/frontend/frontend_tests.cpp) | — | — |

SV-011 has operation-kernel evidence, not complete per-language evidence.
Atomic SystemVerilog tests are still required before those operators can
satisfy the release gate.

SV-013 covers only integer delays under the lexical `` `timescale`` directive.
Fractional delays, `timeunit`/`timeprecision` declarations, precision rounding,
and the general preprocessor remain release-gate work. SV-016 records a
targeted rejection, not implemented `` `default_nettype`` semantics.

## Common IR, runtime, visibility, and tooling

| ID | Precisely bounded feature | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| CM-001 | Packed `Bit2`, `Logic4`, and `Logic9` construction and mutation | execute | [runtime test](../tests/runtime/runtime_tests.cpp) | — | — | [runtime test](../tests/runtime/runtime_tests.cpp) |
| CM-002 | Logic9-to-Logic4 collapse, four-state resolution, and the complete IEEE `std_logic_1164` 9×9 scalar resolution table | execute | [runtime test](../tests/runtime/runtime_tests.cpp) | — | — | [exhaustive golden-table test](../tests/runtime/runtime_tests.cpp) |
| CM-003 | Stable active, inactive, update, and postponed phase order | execute | — | — | — | [scheduler phase test](../tests/runtime/runtime_tests.cpp) |
| CM-004 | Future timestamps, next-delta activation, resumable external stop, and terminal design stop | execute | — | — | — | [scheduler and SimIR lifecycle tests](../tests/runtime/runtime_tests.cpp) |
| CM-005 | `max_deltas` failure reports the timestamp, pending process order, and recent signal | execute | — | [delta-limit test](../tests/runtime/runtime_tests.cpp) | — | [delta-limit test](../tests/runtime/runtime_tests.cpp) |
| CM-006 | SimIR constant loads, reads, blocking/update writes with per-signal update coalescing, common unary/binary operations, jumps, branches, halt, and stop | execute | — | — | [elaborator test covers a subset](../tests/elaboration/elaborator_test.cpp) | [SimIR/coalescing tests](../tests/runtime/runtime_tests.cpp), [application stop path](../tests/app/application_test.cpp) |
| CM-007 | SimIR update-phase and delayed writes through the interpreter, external executor, and LLVM O0/O2, including zero/positive delays and overflow containment | execute | [LLVM scheduled-callback test](../tests/compiler/llvm_jit_test.cpp) | [scheduled-write IR/ABI rejection tests](../tests/compiler/llvm_jit_test.cpp) | [LLVM lowering and cache-identity test](../tests/compiler/llvm_jit_test.cpp) | [external-executor scheduling comparison](../tests/runtime/runtime_tests.cpp), [O0/O2 LLVM differential](../tests/compiler/llvm_jit_test.cpp), [exact application interpreter/hybrid and overflow tests](../tests/app/application_test.cpp) |
| CM-008 | Timed wait and the bounded positive-edge static-sensitivity path, including next-delta observer activation | execute | [frontend positive-edge test](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [SimIR test](../tests/runtime/runtime_tests.cpp), [exact tick-0/tick-1-delta-1/tick-2 application differential](../tests/app/application_test.cpp) |
| CM-009 | Deposit, force masking, and release of the underlying value | execute | — | — | — | [runtime test](../tests/runtime/runtime_tests.cpp), [application test](../tests/app/application_test.cpp) |
| CM-010 | Hierarchical VCD scopes, packed values, nine-state mapping, time markers, and duplicate suppression | execute | — | — | — | [VCD test](../tests/runtime/runtime_tests.cpp) |
| CM-011 | Exact duration-to-resolution conversion for bounded integer time forms and legal VCD scaling for a nonstandard tick multiple | execute | [application test](../tests/app/application_test.cpp) | [inexact-time rejection](../tests/app/application_test.cpp) | — | [scaled VCD test](../tests/app/application_test.cpp) |
| CM-012 | Schema-1 manifest fields, relative paths, source order, and lexicographic glob expansion | parse | [project test](../tests/project/project_config_test.cpp) | [schema/unknown-key test](../tests/project/project_config_test.cpp) | — | — |
| CM-013 | Text and escaped JSON diagnostics | execute | [project diagnostic test](../tests/project/project_config_test.cpp) | — | — | — |
| CM-014 | Content-keyed checksummed cache store/load/erase, process-aware lock recovery, live-lock protection, and atomic replacement | execute | [cache test](../tests/compiler/cache_test.cpp) | [live-lock timeout](../tests/compiler/cache_test.cpp) | — | [cache test](../tests/compiler/cache_test.cpp), [application warm-cache test](../tests/app/application_test.cpp) |
| CM-015 | LLVM 22.1.8 ORC O0/O2 Logic4 CFGs through versioned plain-C runtime/frame/result ABIs and the checked allocation-free ≤64-bit `Logic4Word` path, including size-gated append-only v1 scheduled-write callbacks, layout-preserving appended dynamic/static wait statuses, suspension-safe loops, grouped specialization modules, and cache identity for scheduled writes and wait metadata | execute | [LLVM resume, CFG, scheduled-write, signal-wait, and grouped-module tests](../tests/compiler/llvm_jit_test.cpp), [scheduled word-path runtime test](../tests/runtime/runtime_tests.cpp), [C ABI layout/status/callback test](../tests/compiler/jit_runtime_c_test.c) | [wait-list/signal/width/edge, scheduled-tail, malformed IR, ABI/layout, group/symbol, zero-time-cycle, and cache-corruption rejection](../tests/compiler/llvm_jit_test.cpp) | [LLVM process/group and scheduled/wait-kind/operand/width/static-edge cache tests](../tests/compiler/llvm_jit_test.cpp) | [O0/O2 scheduled-write, signal-wait, and grouped-module tests](../tests/compiler/llvm_jit_test.cpp), [exact application scheduled-write/overflow/posedge differentials](../tests/app/application_test.cpp), [exact-version CI job](../.github/workflows/ci.yml) |
| CM-016 | Interpreter/LLVM differential execution of every semantic simulation test | v1 target | — | — | — | — |
| CM-017 | Application check/build/cache path, per-specialization LLVM module grouping and native-cache telemetry, O0/O2 interpreter-versus-hybrid execution for a bounded SystemVerilog hierarchy, O2 differential execution for the bounded SV/VHDL/SV hierarchy, exact fully compiled O2 scheduled writes, and a two-process/one-module O2 positive-edge comparison at tick 0, tick 1/delta 1, and tick 2; typed capability misses remain on the reference evaluator without blocking supported siblings | execute | [application test](../tests/app/application_test.cpp) | — | [dense specialization ownership test](../tests/elaboration/elaborator_test.cpp) | [O0/O2 application differential, partial specialization-group fallback, O2 mixed-language/scheduled-write/posedge differentials, overflow containment, and per-module cold/warm telemetry test](../tests/app/application_test.cpp) |
| CM-018 | C ABI session lifecycle, generation-checked object handles, bounded hierarchy/value access, deposit/force/release/run, delta/time stepping, asynchronous stop, synchronous callbacks, and callback re-entry guards | execute | [C API test](../tests/api/api_test.cpp) | [stale-handle and callback-mutation rejection](../tests/api/api_test.cpp) | [C API test](../tests/api/api_test.cpp) | [step/stop/resume and terminal-stop precedence test](../tests/api/api_test.cpp) |
| CM-019 | Bounded command driver for `check`, `build`, `run`, `debug`, direct sources, and traditional aliases | execute | [CLI/application test](../tests/app/application_test.cpp) | [JSON argument and standard rejection](../tests/app/application_test.cpp) | [CLI/application test](../tests/app/application_test.cpp) | [CLI/application test](../tests/app/application_test.cpp) |
| CM-020 | Full interactive debugger command set and statement/process/delta/time semantics | v1 target | [bounded REPL commands and forced-O0 hybrid mode](../tests/app/application_test.cpp) | — | [O0 cache-mode identity](../tests/app/application_test.cpp) | [interpreter/O0 transcript, lifecycle, callback-count, and final-state equivalence](../tests/app/application_test.cpp) |
| CM-021 | Ctrl-C handoff through an atomic request to the next safe point | v1 target | — | — | — | — |
| CM-022 | C ABI delta/time stepping and asynchronous stop request | execute | [C API step/stop test](../tests/api/api_test.cpp) | — | — | [delta/time state checks, callback-issued stop/resume, and coincident terminal-stop precedence](../tests/api/api_test.cpp) |
| CM-023 | REPL relative/absolute time runs, delta/time stepping, time/signal breakpoints, scope/signal navigation, value mutation, and finished/poisoned lifecycle | execute | [application REPL test](../tests/app/application_test.cpp) | [unsupported source stepping and poisoned-session checks](../tests/app/application_test.cpp) | — | [application REPL test](../tests/app/application_test.cpp) |
| CM-024 | SimIR `WaitOn`/`WaitSensitivity` O0/O2 suspension using appended v1 statuses while immutable SimIR owns dynamic operands/static edge rules; sensitivity-only signals may exceed 64 bits | execute | [O0/O2 signal-wait test](../tests/compiler/llvm_jit_test.cpp) | [empty-list, invalid-signal/width/edge, and scalar-edge validation](../tests/compiler/llvm_jit_test.cpp) | [wait-kind/operand/referenced-width/static-edge cache invalidation](../tests/compiler/llvm_jit_test.cpp) | [interpreter/external-executor dynamic `WaitOn` differential with duplicate normalization and tick-1/tick-2 delta-1 wakeups](../tests/runtime/runtime_tests.cpp), [O0/O2 suspension-safe loop test](../tests/compiler/llvm_jit_test.cpp), [two-of-two compiled positive-edge application differential](../tests/app/application_test.cpp) |
| CM-025 | Exact parsed-byte source hashing and per-specialization native provenance for source path/content, source-set semantics, bundled-library version, and represented parameter values | execute | [checked-source and specialization metadata tests](../tests/app/application_test.cpp), [elaboration ownership test](../tests/elaboration/elaborator_test.cpp) | — | [owning-source and standard invalidation](../tests/app/application_test.cpp) | [cold/warm reuse, unrelated-source reuse, and comment-only owning-source invalidation](../tests/app/application_test.cpp) |

CM-015 is a bounded compiled-engine slice. `WaitFor`, `WaitOn`,
`WaitSensitivity`, `Yield`, `Stop`, and loops cut by those suspension points
use caller-owned frames. The two signal-wait statuses append values 6 and 7
without changing status values 0 through 5 or the v1 result layout. The result
returns an instruction index while immutable SimIR owns dynamic operands and
static edge rules. Sensitivity-only signals may exceed 64 bits; operations
whose values cross the native ABI remain limited to 64 bits. `WriteUpdate` and
`WriteAfter` lower through append-only, per-process size-gated v1 runtime
callbacks and the checked allocation-free word handoff. Cache identity covers
scheduled-write kind/delay and wait kind, operands, widths, and static edge
data; O0/O2 cache tests vary wait kind, operand order, referenced width, and
static edge. Eligible sibling processes in one bounded elaborated
specialization share an LLVM module and native cache object. O0/O2 tests verify
group execution, warm reuse, whole-object invalidation after one member
changes, and stable frame identity for an unchanged process. LLVM-enabled
build/run selects the adapter cache and falls back only for typed capability
misses. The bounded `fsim debug` path forces O0 for eligible groups and uses
the same fallback rule. CM-015, CM-017, CM-020, and CM-024 provide bounded
differential evidence, including an identical interpreter/O0 debugger
transcript and the exact tick-0/tick-1-delta-1/tick-2 positive-edge case with
two of two processes compiled, but do not satisfy CM-016 or complete HDL
event-control/debug requirements: statement/call instrumentation, the full
semantic fixture set, assertion metadata, normalized VCD, O0 application wait
coverage, and Windows execution evidence remain open. The release build must
use LLVM 22.1.8; a test compiled against another LLVM version is development
evidence only.

## SystemC source subset and plug-in boundary

| ID | Precisely bounded feature | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| SC-001 | `<systemc>` forwarding header, `SC_MODULE`, `SC_CTOR`, `SC_METHOD`, `SC_CTHREAD`, and static/edge sensitivity syntax | execute | [header test](../tests/systemc/systemc_header_test.cpp) | — | — | [standalone facade test](../tests/systemc/systemc_header_test.cpp) |
| SC-002 | Exact `sc_time`, validated `sc_logic`, `sc_bv`, `sc_uint`, `sc_int`, signals, ports, and `sc_export` binding in the standalone facade | execute | [header test](../tests/systemc/systemc_header_test.cpp) | [fractional-femtosecond, invalid-logic, unbound-event, and unbound-export rejection](../tests/systemc/systemc_header_test.cpp) | — | [standalone facade test](../tests/systemc/systemc_header_test.cpp) |
| SC-003 | Versioned plug-in entry point and factory registration through a dynamically loaded library | execute | [sample plug-in](../tests/systemc/sample_plugin.cpp) | — | — | [loader test](../tests/systemc/plugin_loader_test.cpp) |
| SC-004 | Missing symbol, ABI mismatch, initialization failure, transactional registration, and exception containment | v1 target | — | [throwing-initializer test](../tests/systemc/plugin_loader_test.cpp) | — | [exception containment and no partial registration](../tests/systemc/plugin_loader_test.cpp) |
| SC-005 | Direct-argv GCC-like/MSVC planning and checksummed cached host shared-library compilation; GCC-like compiler-emitted dependency closure includes implicit system headers, and publication rejects volatile or concurrently changed inputs | execute | [compiler-plan test](../tests/systemc/plugin_compiler_test.cpp) | [unsafe option, missing source/compiler, volatile-macro, and compile-race tests](../tests/systemc/plugin_compiler_test.cpp) | [source/header/library/dependency-closure key and non-cacheable-input tests](../tests/systemc/plugin_compiler_test.cpp) | [cold/warm compile, implicit-header invalidation, mutation rejection, and stale-lock tests](../tests/systemc/plugin_compiler_test.cpp) |
| SC-006 | Integrated module hierarchy, exports, ports, factories, and common-kernel elaboration | v1 target | — | — | — | — |
| SC-007 | `SC_METHOD`, static/dynamic sensitivity, events, notifications, and channel updates in the common phase lattice | v1 target | — | — | — | — |
| SC-008 | `SC_THREAD`/`SC_CTHREAD` suspension through Boost.Context 1.91 fibers | v1 target | — | — | — | — |
| SC-009 | Common fixed-width signed/unsigned operations and logic-vector semantics needed by signal-level models | v1 target | [facade smoke coverage only](../tests/systemc/systemc_header_test.cpp) | — | — | — |
| SC-010 | Manifest SystemC source sets compiled, cached, and loaded for entry-point/factory validation during an application build | execute | [application SystemC source test](../tests/app/application_test.cpp) | [loader diagnostics implemented; atomic negative application test missing](../src/app/application.cpp) | [application build test](../tests/app/application_test.cpp) | [cold/warm application build](../tests/app/application_test.cpp) |

“Execute” in SC-001 through SC-003, SC-005, and SC-010 means the source facade,
plug-in boundary, compiler component, or build-time validation itself executes.
It does not mean that a SystemC module is instantiated or that a SystemC
process participates in the fsim scheduler.

## Mixed-language behavior

| ID | Precisely bounded feature | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| ML-001 | Schema-1 explicit binding record with instance, language-qualified target, and optional resolver | parse | [manifest test](../tests/project/project_config_test.cpp) | — | — | — |
| ML-002 | Independently selecting and executing a single VHDL or SystemVerilog top | execute | [frontend tests](../tests/frontend/frontend_tests.cpp) | — | [elaborator test](../tests/elaboration/elaborator_test.cpp) | [elaborator test](../tests/elaboration/elaborator_test.cpp) |
| ML-003 | Explicit SV-instance-path binding to a VHDL architecture, recursive elaboration, and named whole-signal port aliases | execute | [SV/VHDL instance frontend tests](../tests/frontend/frontend_tests.cpp) | — | [mixed hierarchy test](../tests/elaboration/elaborator_test.cpp) | [SV top → VHDL counter test](../tests/elaboration/elaborator_test.cpp), [reference/O2-hybrid application differential](../tests/app/application_test.cpp) |
| ML-004 | Equal-width aliased counter output feeding a same-hierarchy SV combinational child | execute | [frontend instance tests](../tests/frontend/frontend_tests.cpp) | — | [shared signal-ID check](../tests/elaboration/elaborator_test.cpp) | [`q=1`, inverted=`FE`](../tests/elaboration/elaborator_test.cpp), [reference/O2-hybrid application differential](../tests/app/application_test.cpp) |
| ML-005 | Width/signedness/lossy-2-state checks and required/conflicting resolver diagnostics | v1 target | [implementation; complete atomic boundary tests missing](../src/elaboration/elaborator.cpp) | [multiple-driver resolver diagnostics](../tests/elaboration/elaborator_test.cpp) | — | — |
| ML-006 | General ordinal vector mapping and Boolean/integer/2-/4-/9-state boundary conversions | v1 target | — | — | [equal-width descending alias only](../tests/elaboration/elaborator_test.cpp) | — |
| ML-007 | Automatic single-driver boundaries and runtime `std_logic`/`sv_wire` multi-driver resolution | v1 target | [single-driver mixed test](../tests/elaboration/elaborator_test.cpp) | [missing/unimplemented resolver tests](../tests/elaboration/elaborator_test.cpp) | [resolution is explicitly rejected, not silently accepted](../tests/elaboration/elaborator_test.cpp) | — |
| ML-008 | Full deterministic cross-language phase lattice | v1 target | — | — | — | [common four-phase spine and one SV→VHDL→SV path](../tests/elaboration/elaborator_test.cpp) |
| ML-009 | Explicit VHDL-parent/SV-child binding with whole-vector port aliases and delta propagation | execute | [both instance ASTs](../tests/frontend/frontend_tests.cpp) | [bounded VHDL association rejection](../tests/frontend/frontend_tests.cpp) | [reverse mixed hierarchy test](../tests/elaboration/elaborator_test.cpp) | [`1010` → inverted `0101`](../tests/elaboration/elaborator_test.cpp) |
| ML-010 | Mixed parameters/generics, broader delay interaction, and complete cross-language failure diagnostics | v1 target | — | — | — | — |

The files under
[examples/vertical_slice](../examples/vertical_slice/README.md) are an
executable demonstration of ML-003 and ML-004. They are not evidence for
general vector-direction conversion, runtime resolver behavior, or the
opposite mixed hierarchy direction. The application suite now uses this
example as bounded O2 interpreter/hybrid differential evidence; it does not
yet compare assertion metadata or normalized VCD, run the mixed case at O0, or
provide Windows execution evidence.

## Required v1 rows not yet implemented

The following rows are part of the v1 contract and remain release-blocking.
They do not become supported when a permissive parser happens to consume them.

### VHDL-2008

| ID | Required feature group | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| V1-VH-01 | Configurations, packages/bodies, contexts, and library semantics | v1 target | — | — | — | — |
| V1-VH-02 | Complete generics, components/direct instantiation, blocks, generates, specialization, and hierarchy beyond the bounded VH-016 form | v1 target | [bounded instance AST only](../tests/frontend/frontend_tests.cpp) | — | — | — |
| V1-VH-03 | Complete synthesizable sequential/concurrent statement set, including case and loops | v1 target | — | — | — | — |
| V1-VH-04 | Arrays, records, aggregates, access types, protected types, and attributes | v1 target | — | — | — | — |
| V1-VH-05 | Name resolution, overload resolution, constant evaluation, legality, and resolution functions | v1 target | — | — | — | — |
| V1-VH-06 | Wait/assert/report, files, and TextIO | v1 target | — | — | — | — |
| V1-VH-07 | Inertial/transport/reject transactions and physical-time normalization | v1 target | — | — | — | — |
| V1-VH-08 | Reviewed Apache-2.0 IEEE logic, numeric, bit, fixed, and floating-point packages | v1 target | — | — | — | — |

### Verilog-2005 and SystemVerilog-2017

| ID | Required feature group | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| V1-SV-01 | Full preprocessor: includes, macros, conditionals, token pasting, directive semantics, and expansion ancestry | v1 target | [bounded timescale context only](../tests/frontend/frontend_tests.cpp) | [unsupported default-nettype check](../tests/frontend/frontend_tests.cpp) | — | — |
| V1-SV-02 | Complete parameters, instances, generates, hierarchy, and specialization beyond the bounded SV-015 form | v1 target | [bounded instance form only](../tests/frontend/frontend_tests.cpp) | — | — | — |
| V1-SV-03 | Interfaces/modports and packages | v1 target | — | — | — | — |
| V1-SV-04 | Packed/unpacked types, structs/unions/enums, memories, and aggregate operations | v1 target | — | — | — | — |
| V1-SV-05 | Gate primitives and complete continuous/procedural assignment semantics | v1 target | — | — | — | — |
| V1-SV-06 | All `always` forms, functions/tasks, `final`, case/loops, and complete expression semantics | v1 target | — | — | — | — |
| V1-SV-07 | Delays/events, fork/join, named events, and complete NBA/delta behavior | v1 target | — | — | — | — |
| V1-SV-08 | Strings, files, dynamic/associative arrays, queues, and `$readmem*` | v1 target | — | — | — | — |
| V1-SV-09 | Deterministic basic random functions, display/stop tasks, and immediate assertions | v1 target | — | — | — | — |

### Common engines and release hardening

| ID | Required feature group | Status | P+ | P- | E | R |
|---|---|---|---|---|---|---|
| V1-CM-01 | Typed language HIR and elaborated DesignIR with dense stable IDs and retained source/debug metadata | v1 target | — | — | — | — |
| V1-CM-02 | Per-specialization resumable LLVM state machines with O2 run/O0 debug semantic equivalence | v1 target | [bounded caller-owned frame ABI, grouped-module API, and forced-O0 debug engine](../tests/compiler/llvm_jit_test.cpp) | [frame/layout/group/capability checks](../tests/compiler/llvm_jit_test.cpp) | [dense instance-specific specialization ownership](../tests/elaboration/elaborator_test.cpp), [O0 cache-mode identity](../tests/app/application_test.cpp) | [bounded O0/O2 resume/interpreter differential](../tests/compiler/llvm_jit_test.cpp), [bounded application interpreter/hybrid, two-process/one-module, and interpreter/O0 REPL differentials](../tests/app/application_test.cpp) |
| V1-CM-03 | Complete native-object cache key, corruption recovery, per-key concurrency, and compatibility eviction | v1 target | [hardened cache primitive, canonical LLVM adapter key, and parsed-byte specialization provenance](../tests/compiler/cache_test.cpp) | [live-lock and LLVM corrupt/incompatible-object rejection](../tests/compiler/llvm_jit_test.cpp) | [owning-source and standard invalidation](../tests/app/application_test.cpp) | [LLVM O0/O2 cold/warm/corrupt/referenced-width, scheduled-write, and wait-kind/operand/width/static-edge tests](../tests/compiler/llvm_jit_test.cpp), [application per-module and specialization-provenance reuse/invalidation tests](../tests/app/application_test.cpp) |
| V1-CM-04 | Global time-resolution selection, SV rounding, exact VHDL/SystemC conversion, and overflow diagnosis | v1 target | [bounded integer conversion, timescale auto precision, and scaled VCD](../tests/app/application_test.cpp) | [inexact conversion](../tests/app/application_test.cpp), [coarse precision](../tests/frontend/frontend_tests.cpp) | — | [integer timescale delay only](../tests/app/application_test.cpp) |
| V1-CM-05 | Driver transactions, multi-driver resolution, update fanout, and committed-change visibility | v1 target | [bounded update/delayed-write scheduling and single-signal coalescing](../tests/runtime/runtime_tests.cpp) | [unimplemented resolver rejection](../tests/elaboration/elaborator_test.cpp) | — | [tick-0/tick-2 committed writes and tick-1/delta-1 posedge fanout comparisons only](../tests/app/application_test.cpp) |
| V1-CM-06 | Full debugger REPL, safe points, stepping, breakpoints, scope/local inspection, trace selection, and Ctrl-C | v1 target | [bounded signal/time REPL with forced-O0 hybrid execution](../tests/app/application_test.cpp) | [unsupported source stepping and poisoned lifecycle](../tests/app/application_test.cpp) | [O0 cache-mode identity](../tests/app/application_test.cpp) | [interpreter/O0 transcript, lifecycle, callback-count, and final-state equivalence](../tests/app/application_test.cpp) |
| V1-CM-07 | Selective buffered VCD, flattening rules, normalized goldens, and trace/debug overhead checks | v1 target | [VCD core and scaled-timescale tests](../tests/runtime/runtime_tests.cpp) | — | — | [bounded application trace test](../tests/app/application_test.cpp) |
| V1-CM-08 | Unicode/path behavior, sanitizer/fuzz jobs, diagnostic catalog, benchmarks, and source-build documentation | v1 target | [ASan/UBSan and bounded frontend libFuzzer jobs](../.github/workflows/ci.yml), [diagnostic catalog](diagnostics.md), and [catalog consistency gate](../cmake/CheckDiagnosticCatalog.cmake) | — | — | [frontend fuzz harness](../tests/fuzz/frontend_fuzz.cpp) |
| V1-CM-09 | Ubuntu x86-64/GCC and Windows x86-64/MSVC Debug/Release CI with LLVM 22.1.8 | v1 target | [Linux/Windows Debug+Release and exact-LLVM workflow definitions](../.github/workflows/ci.yml) | — | — | — |

## Explicitly deferred beyond v1

| ID | Area | Deferred feature | Status |
|---|---|---|---|
| D-VH-01 | VHDL | PSL, VHPI, VHDL-AMS, VITAL/SDF, proprietary packages, and proprietary pragma semantics | deferred |
| D-SV-01 | SystemVerilog | Classes, constraints, UVM, concurrent SVA, covergroups, DPI/VPI, program blocks, and clocking blocks | deferred |
| D-SV-02 | Verilog/SystemVerilog | UDPs, specify/timing checks, strengths, and SDF | deferred |
| D-SC-01 | SystemC | Accellera ABI/kernel compatibility, TLM, AMS, CCI, dynamic processes, and arbitrary custom primitive channels | deferred |
| D-CM-01 | Platform | ARM64 and parallel simulation | deferred |
| D-CM-02 | Products | GUI/waveform viewer, reverse execution, standalone AOT executables, coverage, and standardized plug-in APIs | deferred |

Deferred rows are not release blockers unless the v1 scope is explicitly
changed and the row is promoted to a required section.

## Release-gate rule

**fsim v1 must not be released while any row marked `v1 target` remains, or
while any required language construct lacks P+, P-, E, or R evidence.**

For R evidence, every semantic simulation case must run through both the SimIR
interpreter and the LLVM 22.1.8 JIT and compare final values, assertions,
scheduling observations, and trace events. All required rows must pass on
Ubuntu x86-64/GCC and Windows x86-64/MSVC in Debug and Release. A construct
that is silently accepted, discarded, or only parser-tested fails the gate.
Deferred rows are excluded unless promoted into the v1 scope.
