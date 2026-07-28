<!-- SPDX-License-Identifier: Apache-2.0 -->
# Implementation plan and progress

## Purpose

This document records the implementation sequence for fsim v1 and the
evidence-backed progress of the current repository. It complements the
[feature matrix](feature-matrix.md), which tracks individual language and
runtime features.

Last updated: 2026-07-28.

The repository is currently a pre-alpha architecture vertical slice. It is not
the fsim v1 release, and a milestone is not complete merely because its
interfaces or test scaffolding exist.

## Status definitions

- **Complete**: every stated milestone outcome is implemented and its required
  platform/test gate has passed.
- **In progress**: useful implementation and automated evidence exist, but one
  or more milestone outcomes remain.
- **Pending**: implementation is absent or limited to enabling infrastructure.
- **Deferred**: intentionally outside the v1 scope.

## Locked architecture decisions

- SystemC is a peer hierarchy language, not a leaf-only foreign-model
  interface. VHDL and Verilog/SystemVerilog instances can bind to SystemC
  factories, and SystemC factories can declare elaboration-time children bound
  to VHDL or Verilog/SystemVerilog targets.
- Mixed SystemC/HDL hierarchy is recursive in both directions and any supported
  language may be the selected top. Every such instance, port, alias, process,
  and debug-visible object enters the common `DesignIR` and scheduler.
- The native SystemC plug-in ABI is the C++ compilation boundary, not a
  simulation-boundary shortcut. It may register typed factories and foreign
  child placeholders during elaboration, but cannot create HDL hierarchy after
  simulation starts.
- Construction-time generic/parameter actuals must work across a SystemC
  boundary in both directions. HDL overrides/generic maps will be validated
  against a typed factory schema, while `hdl_instance` will supply canonical
  actuals for an HDL child through an append-only C ABI extension. These values
  become ordinary specialization/cache identity; they are never mutable
  runtime configuration.
- Cross-language selection remains explicit through `fsim.toml`; fsim never
  guesses a SystemC, VHDL, or Verilog/SystemVerilog target by name.

## Current validated baseline

The following foundation is implemented:

- C++20 project structure for Linux and Windows x86-64, with CMake 3.28
  presets, warning policy, Apache-2.0 licensing, and dependency-version policy;
- packed 2-, 4-, and 9-state value kernels;
- deterministic single-thread scheduling and a typed SimIR interpreter;
- bounded handwritten VHDL-2008 and Verilog/SystemVerilog frontends;
- a multi-root Verilog/SystemVerilog preprocessor with exact ordered
  compilation-unit/transitive snapshots, manifest macros, object/function
  expansion with default arguments, multiline replacement, token
  concatenation/stringification, conditional compilation, persistent
  timescale context, include/macro diagnostic ancestry, and executable
  `file`/`source-set`/`combined` policies;
- executable scalar implicit-net/default-port semantics for
  `` `default_nettype``, including `none`, plus reset, cell,
  keyword-version, and omitted-input pull directive state carried through
  elaboration and shared compilation units;
- recursive VHDL/SV hierarchy in both language directions with explicit
  bindings and boundary checks;
- buffered VCD, a bounded command-line debugger, and a versioned native C API;
- an LLVM 22.1.8 ORC adapter with caller-owned resumable process frames for
  processes whose value-bearing operations are up to 64 bits, with O0/O2
  lowering for update-phase/delayed writes plus dynamic/static sensitivity
  waits, integrated as a hybrid per-process engine for `build`/`run` and a
  forced-O0 hybrid engine for the bounded `debug` path;
- dense bounded specialization records and one LLVM module/native cache object
  per specialization's eligible process group, with per-process interpreter
  fallback;
- ordered Verilog/SystemVerilog integral value parameters and localparams,
  named/positional overrides, dependent constant evaluation, parameterized
  packed ranges, and canonical per-instance specialization values;
- VHDL scalar integer/Boolean/bit generic declarations, defaults,
  positional-then-named maps, dependent constant evaluation, parameterized
  ranges, entity/architecture interface merging, and canonical per-instance
  specialization values;
- parameter/generic-driven VHDL and SystemVerilog conditional, iterative, and
  selection generate regions with executable local signals, assignments,
  processes, and instances, including recursive nesting,
  loop-variable substitution, stable labeled/indexed/alternative hierarchy
  paths, and explicit mixed-language bindings through selected branches,
  realized iterations, and selected alternatives;
- interval-based VHDL case-generate `to`/`downto` choices with null-range and
  scalar/range-overlap handling;
- inline or module-scope SystemVerilog `genvar` loops with declaration-order
  validation and normalized assignment, prefix/postfix increment/decrement,
  and compound add/subtract updates;
- always-selected unguarded VHDL block statements, module-level implicit
  SystemVerilog conditional/iterative/selection generates, and direct or named
  static contents in explicit SystemVerilog generate regions, sharing
  generated-body scoping, interpreter/JIT execution, VCD visibility, and
  native-cache identity;
- declaration-ordered bounded generated VHDL constants and SystemVerilog
  parameters/localparams, including parent-specialization, prior-constant,
  and loop-index dependencies folded before SimIR;
- bounded VHDL constant-only project packages with explicit whole-package or
  selected-constant use visibility, acyclic package-to-package imports,
  direct package/library-qualified constant expressions, declaration-order
  folding, cycle diagnostics, and precise transitive source provenance in
  specialization/native-cache keys;
- bounded reusable VHDL project contexts containing library/use/context items,
  with recursive expansion, cycle diagnostics, package visibility, and
  transitive context-source cache provenance;
- bounded SystemVerilog packages containing immutable integral
  parameters/localparams and packed integral typedef aliases,
  compilation-unit and unit-local wildcard/selected imports, recursive
  case-sensitive visibility, direct scoped constants/types, parameterized
  alias ranges, packed enums with explicit/implicit enumerators and legality
  checks, non-nested parameterized packed structs and equal-width packed
  unions with executable member reads/writes and one-level constant member
  bit/part-selects whose specialized parameter/package-constant bounds use the
  checked constant evaluator, plus normalized constant `+:`/`-:` indexed
  selects for ascending and descending ranges and checked constant replication
  concatenations with logarithmic SimIR expansion, plus signedness-sensitive
  four-state arithmetic shifts and complemented unary reductions/binary XNOR
  spellings through shared interpreter/LLVM kernels, bounded nonnegative
  constant `$clog2` folding for derived parameter widths, and precise
  source/cache provenance;
- bidirectional bounded VHDL/SystemVerilog construction-actual transfer across
  explicit bindings, with parent-language association rules,
  case-insensitive VHDL name matching, ambiguity rejection, specialization
  before boundary checks, and canonical native-cache identity;
- a persistent LLVM native-object cache selected beneath the configured
  application cache, with application-visible cold/warm telemetry and
  identity for scheduled-write kind/delay and wait kind, operands, widths, and
  dynamic/static edge data;
- exact parsed-byte HDL source digests and per-specialization provenance keys
  for owning-source content plus current source-set/elaboration identity;
- a shared checked allocation-free single-word `Logic4` path between generated
  callbacks and the runtime for values up to 64 bits;
- append-only, per-process size-gated v1 runtime-table callbacks for
  `write_update` and `write_after`;
- appended `WaitOn`/`WaitSensitivity`/`WaitForever` resume statuses that
  preserve existing status values and the v1 result layout, with immutable
  SimIR retaining wait operands and edge rules;
- complete validation of the bounded compiled-wait representation and support
  for sensitivity-only signals wider than 64 bits when no value crosses the
  ABI;
- bounded O0/O2 SystemVerilog and O2 mixed SV/VHDL/SV application differential
  coverage comparing status, time, deltas, committed-change callbacks, and
  final values;
- an exact interpreter/O2-hybrid scheduled-write application test covering a
  tick-0 update commit, tick-2 delayed commit, and callback-contained scheduler
  overflow;
- an exact interpreter/O2-hybrid positive-edge test covering tick 0, tick
  1/delta 1, and tick 2 with both application processes compiled;
- a bounded `fsim debug` path that forces O0 hybrid execution and exactly
  matches the interpreter REPL transcript, lifecycle, callback count, and final
  state for a source breakpoint plus statement/process/scheduler stepping;
- source-bearing SimIR statement, wait, assertion, process-entry, and
  process-suspension points, with stable process-ID continuation requeueing;
- append-only runtime debug-point controls: O0 always exposes executable
  points, while O2 uses a size-gated runtime flag;
- statement/process stepping in the REPL and C API, including process-bearing
  C safe-point callbacks;
- exact-state conditional signal breakpoints and live add/remove/all/clear/list
  selection for configured debug VCD output;
- command-scoped SIGINT handler installation/restoration and real-handler
  interpreter/O0 tests proving safe-point stop at tick 0 and resumable
  completion;
- bounded VHDL/SystemVerilog process variables lowered to named persistent
  SimIR registers, interpreter/O0/O2 `CopyRegister`, and engine-neutral
  debugger `locals` reads;
- source-level VHDL bare, `on`, `until`, and `for` wait clauses plus
  SystemVerilog any-change/`posedge`/`negedge` procedural event controls
  lowered to resumable waits, with interpreter/O2 timing and filtered-wakeup
  differentials;
- deterministic simple-expression dependency inference for `always @*` and
  time-zero `always_comb`/`always_latch`, plus dynamic `@*`, with a
  four-process interpreter/O2 delta
  differential;
- ordered Verilog/SystemVerilog `case`/`casez`/`casex`/`default` lowering
  with comma-separated choices, exact four-state matching, distinct
  selector-or-choice `Z` versus `X`/`Z` wildcard policies, width diagnostics,
  and an interpreter/O2 differential;
- bounded SystemVerilog conditional expressions with scalar four-state
  conditions, equal-width alternatives, exact unknown-condition merging,
  and interpreter/O0/O2 evidence;
- vector-aware SystemVerilog logical negation and unsigned
  inequality/relational comparisons with exact four-state unknown
  propagation, exact known-result `===`/`!==` X/Z comparison, one-sided
  right-pattern `==?`/`!=?` wildcard comparison, and interpreter/O0/O2
  evidence;
- mixed-width SystemVerilog logical conjunction/disjunction with controlling
  known-value truth tables and interpreter/O0/O2 evidence;
- SystemVerilog unary reductions, including complemented
  `~&`/`~|`/`~^`/`^~` forms, binary `~^`/`^~` XNOR, and mixed-width logical
  left/right shifts with four-state unknown and oversized-amount semantics
  tested across the interpreter and LLVM O0/O2 paths;
- equal-width signed/unsigned packed arithmetic and relational comparisons for
  VHDL and SystemVerilog, including distinct signed remainder/modulo,
  arbitrary-width interpreter algorithms, guarded LLVM lowering, and
  differential source tests, plus bounded packed VHDL signed `abs` through
  common sign-extract/negate/select operations;
- VHDL packed `sll`/`srl`/`sla`/`sra` and `rol`/`ror` for locally static
  integer counts through common four-state interpreter and LLVM kernels,
  including negative-count direction reversal, end-element arithmetic fill,
  and modulo-width rotation;
- ordered VHDL sequential packed `case` statements with exact multi-choice
  matching, nested statement bodies, and `others`;
- locally static VHDL sequential `for` loops with ascending, descending, and
  null-range semantics, read-only implicit loop constants, bounded
  elaboration-time unrolling, and indexed packed writes;
- bounded SystemVerilog procedural `for` loops with inline integral indices,
  canonical inclusive/exclusive comparison bounds, unit-step updates, null
  ranges, and the same common deterministic unrolling path;
- locally static nonnegative Verilog/SystemVerilog `repeat` counts with
  zero-count semantics and the same bounded common-loop path;
- runtime VHDL/Verilog/SystemVerilog `while` and suspending Verilog/
  SystemVerilog `forever` statements lowered to cyclic SimIR CFGs with
  language-specific condition rules, per-iteration debug-safe points, and
  resumable backedges accepted by LLVM while raw zero-time cycles remain
  rejected;
- nested SystemVerilog `break`/`continue` and VHDL `exit`/`next`, including
  conditional VHDL forms, lowered through an innermost-loop target stack for
  both elaboration-unrolled and cyclic runtime CFGs;
- VHDL opening/end loop labels and targeted `exit`/`next`, with
  case-insensitive label resolution and named outer-loop CFG transfers across
  static and runtime loop kinds;
- unconditional VHDL sequential loops and SystemVerilog post-test `do-while`
  loops, with `continue` edges targeting the trailing condition;
- first-suspending VHDL `wait until` and immediate-test
  Verilog/SystemVerilog condition waits lowered to signal-change suspension
  and condition-recheck CFGs, including attached Verilog statements and
  debugger-visible permanent waits;
- combined VHDL event/condition/timeout waits with engine-neutral wake-reason
  registers and absolute deadlines preserved across false event wakeups;
- nested VHDL `if`/`elsif`/`else` with Boolean literals and typed Boolean
  operators, plus nested SystemVerilog `if`/`else` and immediate assertions
  using packed four-state truth conversion, with O0/O2 differential evidence;
- declared-range-aware constant SystemVerilog bit/part selects and packed
  concatenations with arbitrary-width interpreter operations, LLVM
  single-word lowering, and ascending/non-zero-based range tests;
- VHDL constant indexed names/slices and correct `&` concatenation reuse the
  same range-aware SimIR operations, including packed procedural locals,
  ascending ranges, nine-state source typing, and interpreter/O2 differential
  evidence;
- constant selected SystemVerilog/VHDL assignment targets lower to typed
  `Insert` and partial blocking/update/delayed writes, with stable overlap
  merging and interpreter/LLVM O0/O2 differential evidence;
- a SystemC compatibility header, native plug-in ABI, loader, and cached host
  compiler;
- typed, bidirectional SystemC/HDL hierarchy construction with either side as
  the selected top;
- append-only immutable named scalar actuals from a SystemC `hdl_instance`
  into manifest-selected VHDL generic or SystemVerilog parameter
  specialization, with real plug-in execution and cache identity;
- append-only ordered SystemC factory schemas for integer/natural/positive/
  Boolean/bit construction parameters, transactional loader replay,
  canonical default/explicit validation, and typed constructor lookup;
- on-demand HDL-to-SystemC construction after parent-specialization constant
  evaluation and source-language association checking, with canonical values
  recorded in DesignIR before parameter-dependent port checks;
- a typed `fsim::systemc::hdl_instance` facade extension for constructor-time
  SystemC-to-VHDL/SV child declaration, manifest-selected implementation, and
  append-only named scalar construction actuals specialized into either HDL;
- exact Boost.Context 1.91.0 discovery or checksum-verified source fetching,
  plus fiber-backed `SC_THREAD`/`SC_CTHREAD` timed, delta, and static waits on
  the common scheduler;
- statically sensitive facade-defined `SC_METHOD` callbacks using common
  scheduler/update semantics and contained native exceptions;
- dynamic `SC_METHOD` time/event `next_trigger`, opaque named-event
  elaboration, immediate/delta/timed notifications, earliest-notification
  replacement, cancellation, strict `notify_delayed`, and dynamic OR/AND event
  expressions owned by the common scheduler with interpreter/hybrid
  equivalence;
- registered primitive channels with stable hierarchy metadata and
  deduplicated common-update-phase callbacks;
- typed module-local `sc_signal` elaboration with initial values, committed
  reads, coalesced update-phase writes, static/dynamic sensitivity, and
  delta-scoped `event()` state;
- typed SystemC port-to-signal binding aliases shared with HDL parent
  connections, including direction-aware initial values and conflict
  diagnostics;
- recursive constructor-time native SystemC child modules with direct-parent
  signal aliases and stable common-runtime process registration;
- per-root SystemC elaboration/start/end lifecycle callbacks plus direct-parent
  port chains and standard typed signal-interface export chains;
- explicit SystemC export hierarchy objects resolved into common DesignIR
  signal aliases; and
- a stable catalog covering 603 unique current production diagnostic codes.

Current and most recent aggregate Linux regression snapshots:

| Gate | Result |
|---|---|
| GCC Debug, LLVM disabled | Previous 13/13 baseline passes; the fast expression target was added afterward |
| GCC Release, LLVM disabled | Previous 13/13 baseline passes; the fast expression target was added afterward |
| LLVM 22.1.8 Debug, warnings-as-errors | 15/15 tests pass after the SystemVerilog bit-count/dimension-query and VHDL falling-edge batches (138.81 seconds wall time with four-way CTest parallelism) |
| LLVM 22.1.8 Release, warnings-as-errors | Previous 14/14 baseline passes; the fast expression target was added afterward |
| Concurrent LLVM Debug and Release suites | Previous paired baseline passes; cache-test paths are isolated |
| GCC ASan/UBSan | 14/14 tests pass after the three-feature batch, with no ASan/UBSan findings |
| Boost.Context 1.91.0 fibers | Checksum-verified fetch/configure/build; current LLVM Debug 15/15 and ASan/UBSan 14/14 aggregate suites pass, including compiled-SV/SystemC application tests |
| Verilog/SV preprocessing/directives | GCC Debug and exact LLVM 22 atomic frontend/elaboration plus source-set/combined interpreter/compiled differentials pass, including shared default-net/cell/reset state and omitted-input pulls |
| Conditional instance-generate | GCC Debug and exact LLVM 22 frontend/elaboration/application tests pass for VHDL and SV selection, stable generated mixed-binding paths, interpreter/JIT equivalence, and cold/warm native cache |
| Iterative instance-generate | GCC Debug and exact LLVM 22 frontend/elaboration/application tests pass for VHDL ascending/descending ranges and inline/module-scope SV `genvar` loops with assignment/prefix/postfix/compound updates, nested expansion, mixed indexed bindings, interpreter/JIT equivalence, and cold/warm native cache |
| Selection instance-generate | GCC Debug and exact LLVM 22 frontend/elaboration/application tests pass for labeled VHDL scalar/directed-range and SV multi-choice/default alternatives, mixed selected paths, interval-overlap diagnostics, interpreter/JIT equivalence, and cold/warm native cache |
| Executable generated bodies | GCC Debug and exact LLVM 22 frontend/elaboration/application tests pass for selected and per-iteration local signals, concurrent/process behavior, scoped debug/VCD names, interpreter/JIT equivalence, and specialization-level native cache |
| Static/implicit generated bodies | GCC Debug and exact LLVM 22 frontend/elaboration/application tests pass for unguarded VHDL blocks, all three bounded implicit SV generate forms, direct explicit-generate contents, and named static SV blocks with exact conditional/static interpreter/JIT/VCD/cache behavior |
| Generated constants/parameters | GCC Debug and exact LLVM 22 frontend/elaboration/application tests plus focused ASan/UBSan pass for declaration-order, parent-scope, and loop-index folding, targeted evaluation/type failures, and exact interpreter/JIT/VCD/cache results |
| VHDL package constants | GCC Debug and exact LLVM 22 frontend/elaboration/application tests plus focused ASan/UBSan pass for bounded declarations, `work` and cross-library selected/whole-package visibility, recursive declaration-order folding, interpreter/JIT/VCD equality, targeted import/evaluation/subtype/cycle diagnostics, and transitive package-only native-cache invalidation |
| VHDL selected package constants | GCC Debug and exact LLVM 22 frontend/elaboration/application tests plus focused ASan/UBSan pass for two-/three-part qualified constants in ranges and behavior, recursive folding, missing/malformed diagnostics, interpreter/JIT/VCD equality, and precise native-cache provenance |
| VHDL reusable contexts | GCC Debug and exact LLVM 22 frontend/elaboration/application tests plus focused ASan/UBSan pass for bounded declarations, recursive cross-library context/package visibility, missing/malformed/cycle diagnostics, interpreter/JIT/VCD equality, and context-only native-cache invalidation |
| SystemVerilog package constants and packed types | GCC Debug and exact LLVM 22 frontend/elaboration/application tests plus focused ASan/UBSan pass for bounded declarations, recursive imports, direct scoped constants/types, alias chains, parameterized ranges, explicit/implicit enum values, non-nested packed-struct layouts/member reads/writes, targeted legality failures, interpreter/JIT/VCD equality, unrelated-package reuse, and transitive native-cache invalidation |
| SystemVerilog expression semantics | GCC Debug and exact LLVM 22 frontend/elaboration/application tests plus focused ASan/UBSan pass for four-state logical/reduction/shift/arithmetic operations, complemented reductions/XNOR, and exact `===`/`!==` X/Z comparison |
| SystemVerilog wildcard equality | GCC Debug and exact LLVM 22 frontend/elaboration/runtime/compiler tests plus the fast expression application target pass for SystemVerilog-only `==?`/`!=?`, right-side `X`/`Z` masks, unmasked left-side unknown propagation, known mismatch behavior, interpreter/LLVM equality alignment, O0/O2 scalar/vector tables, and operator-sensitive native-cache identity; the batched ASan/UBSan aggregate passes with local leak detection disabled under ptrace |
| Verilog-2005/SystemVerilog constant `$clog2` | GCC Debug and exact LLVM 22 frontend/elaboration plus the fast expression application target and batched ASan/UBSan aggregate pass for nonnegative integral arguments, zero/one/power/non-power edge values, derived parameter-dependent port widths, distinct specialization/cache identity, and interpreter/LLVM O0/O2 behavior |
| SystemVerilog wildcard case semantics | GCC Debug and exact LLVM 22 frontend/elaboration/runtime/compiler/application tests for ordered `casez`/`casex`, selector- and choice-side wildcards, known-bit mismatch preservation, defensive HIR rejection, interpreter/JIT equivalence, LLVM O0/O2 truth tables, and operator-sensitive native-cache identity |
| VHDL packed shifts and rotates | GCC Debug and exact LLVM 22 frontend/elaboration/runtime/compiler plus the fast expression application target and batched ASan/UBSan aggregate pass for `sll`/`srl`/`sla`/`sra` and `rol`/`ror`, including X/Z data, end-element arithmetic fill, oversized counts, modulo-width rotation, and negative-count direction reversal |
| VHDL packed signed `abs` | GCC Debug and exact LLVM 22 frontend/elaboration plus the fast expression application target pass for known negative, positive, and X/Z-containing values, same-width wrapping, unsigned rejection, and interpreter/LLVM O0/O2 equivalence |
| Mixed-language packed exponentiation | Exact LLVM 22 frontend/elaboration/runtime plus the fast expression application target for left-associative Verilog-2005/SystemVerilog `**`, VHDL factor/sign precedence and parenthesization, checked constant folding, 65-bit interpreter execution, zero/positive/negative/X cases, VHDL negative-exponent rejection, and interpreter/LLVM O0/O2 equivalence |
| SystemVerilog procedural updates | Exact LLVM 22 frontend/diagnostic-catalog plus the fast expression application target for all arithmetic/bitwise/logical-shift/arithmetic-shift compound assignments, standalone prefix/postfix increment/decrement, whole and selected targets, signed shifts, X propagation, Verilog-2005 rejection, and interpreter/LLVM O0/O2 equivalence |
| VHDL clock-edge guards | Exact LLVM 22 frontend/elaboration plus the fast expression application target for sole outer `rising_edge`/`falling_edge` sensitivity refinement, negative-edge scheduling, and interpreter/LLVM O0/O2 equivalence |
| VHDL concurrent assertions | Exact LLVM 22 frontend/elaboration plus the fast expression application target for labeled assertion HIR, stable process naming, inferred condition sensitivity, event-driven interpreter failure metadata, and interpreter/LLVM O0/O2 execution |
| SystemVerilog signedness casts | Exact LLVM 22 frontend/elaboration plus the fast expression application target for bit/width-preserving `$signed`/`$unsigned`, arity rejection, signed/unsigned comparisons, signedness-sensitive `>>>`, and interpreter/LLVM O0/O2 equivalence |
| SystemVerilog unknown detection | Exact LLVM 22 frontend/elaboration plus the fast expression application target for `$isunknown`, known and X/Z-packed operands, arity/language-version rejection, and interpreter/LLVM O0/O2 equivalence |
| SystemVerilog packed width query | Exact LLVM 22 frontend/elaboration plus the fast expression application target for 32-bit `$bits` results over packed objects and concatenations, arity/language-version rejection, and interpreter/LLVM O0/O2 equivalence |
| SystemVerilog packed array queries | Exact LLVM 22 frontend/elaboration plus the fast expression application target for `$left`/`$right`/`$low`/`$high`/`$size`/`$increment`, ascending and descending declared bounds and increments, optional constant dimension `1`, unsupported dimension rejection, and interpreter/LLVM O0/O2 equivalence |
| SystemVerilog dimension counts | Exact LLVM 22 frontend/elaboration plus the fast expression application target for packed-only `$dimensions`/`$unpacked_dimensions`, arity rejection, and interpreter/LLVM O0/O2 equivalence |
| SystemVerilog one-hot queries | Exact LLVM 22 frontend/elaboration/runtime plus the fast expression application target for `$onehot`/`$onehot0`, zero/single/multiple-one inputs, X/Z elements, wide interpreter execution, arity rejection, and LLVM O0/O2 equivalence |
| SystemVerilog one-count query | Exact LLVM 22 frontend/elaboration/runtime plus the fast expression application target for the dedicated `$countones` SimIR operation, zero/single/multiple-one inputs, X/Z elements, wide interpreter execution, arity rejection, and LLVM O0/O2 equivalence |
| SystemVerilog selected-state count query | Exact LLVM 22 frontend/elaboration/runtime plus the fast expression application target for the state-mask-bearing `$countbits` SimIR operation, known/unknown/mixed controls, dynamic-control rejection, wide interpreter execution, and LLVM O0/O2 equivalence |
| Sequential loop control | GCC Debug and exact LLVM 22 frontend/elaboration/compiler/application tests plus focused ASan/UBSan pass for nested SystemVerilog `break`/`continue` and VHDL `exit`/`next`, conditional and labeled VHDL forms, named outer-loop transfers across static/runtime loops, opening/end label validation, guaranteed first execution, trailing-condition continue targeting, interpreter/JIT O0/O2 equivalence, and defensive orphan-HIR rejection |
| Conditional and combined waits | GCC Debug and exact LLVM 22 frontend/elaboration/runtime/compiler/application tests plus focused ASan/UBSan pass for all bounded VHDL wait-clause combinations and Verilog/SystemVerilog `wait (expression)`, first-suspend versus immediate-test semantics, event/timeout races, absolute-deadline rearming, engine-owned wake-result registers, multi-signal dependency rechecks, debugger-visible permanent suspension, append-only status 9, cache identity, and interpreter/JIT O0/O2 equivalence |
| Installed C API | Strict C11 compile/link/run passes; only versioned `fsim_*` symbols are exported |
| Installed SystemC facade | Strict C++20 compile/run passes |
| Mixed-language CLI example | Check/build/run pass; simulation stops at tick 6 and emits VCD |

LeakSanitizer remains enabled in CI. It was disabled only for local execution
because the managed development environment runs under ptrace, which prevents
LeakSanitizer from starting.

Development checkpoints use the smallest relevant frontend, elaboration,
runtime, LLVM, and fast application targets. The aggregate application and
sanitizer suites are batched after several feature additions and before
release-facing changes; this keeps feature feedback sub-second to a few
seconds without weakening the periodic regression gate. Expression work can
select its application gate with
`ctest --test-dir <build> -L expressions --output-on-failure`.

Windows Debug/Release CI and an exact LLVM 22.1.8 Windows ORC matrix are
configured, but Windows execution has not been validated from this Linux
development host. The Clang/libFuzzer job is configured, but was not run
locally because the host lacks Clang/compiler-rt executables.

## Milestone progress

### 1. Platform and semantic spine — In progress

Completed:

- Apache-2.0 license and SPDX coverage;
- CMake 3.28 project and Linux Ninja presets;
- C++20 enforcement, warnings-as-errors gates, and Linux/Windows x86-64
  configuration checks;
- exact LLVM 22.1.8 discovery behind a narrow adapter target;
- central version policy for CLI11 2.6.2, toml++ 3.4.0, Boost.Context 1.91.0,
  and Catch2 3.15.2, with exact installed-or-fetched Boost.Context
  consumption;
- source spans, structured diagnostics, schema-1 manifest loading, glob/order
  handling, and a diagnostic catalog consistency test;
- versioned C API and SystemC plug-in ABI skeletons;
- Linux GCC and Windows MSVC Debug/Release workflow definitions;
- exact LLVM 22.1.8 Linux and Windows Debug/Release workflow definitions; and
- sanitizer and VHDL-parser/Verilog-SV-preprocessor-parser fuzzer workflow
  definitions.

Remaining before completion:

- exercise LLVM 22.1.8 in Windows CI;
- consume the planned support dependencies where their corresponding features
  are implemented, instead of only pinning version policy;
- complete cross-platform Unicode/path and console-interrupt validation; and
- obtain green evidence from the actual Windows runners.

### 2. End-to-end internal vertical slice — In progress

Completed:

- packed values, time conversion for the bounded delay subset, scheduler,
  compact DesignIR, typed SimIR, and interpreter;
- minimal VHDL/SV parsing and executable lowering;
- explicit mixed-language binding with executable examples in both hierarchy
  directions;
- stable process/signal IDs, phased scheduling, delta-limit diagnostics,
  force/deposit/release, VCD, and debugger safe-point callbacks;
- a shared kernel boundary for interpreter and external process executors;
- LLVM lowering for O0/O2 control flow, timed waits, next-delta yields, stop,
  update-phase/delayed writes, dynamic/static sensitivity waits, and loops cut
  by any supported suspension point;
- append-only `write_update`/`write_after` v1 ABI fields with per-process
  structure-size and callback validation;
- allocation-free checked word handoff for generated blocking, update-phase,
  and delayed writes;
- appended `WaitOn`/`WaitSensitivity`/`WaitForever` v1 resume statuses without
  changing the result structure or existing numeric values; immutable SimIR
  owns dynamic operands and dynamic/static edge rules;
- validation of wait lists, signal IDs and widths, edge kinds/scalar edge
  rules, boundary instructions, resume PCs, and frame states, while allowing
  sensitivity-only signals wider than 64 bits;
- LLVM-enabled build/run selection with typed per-process fallback;
- dense instance-specific specialization records containing canonical unit
  identity, instance path, and directly owned process IDs;
- one optimized LLVM module per bounded specialization's eligible process
  group, with atomic group validation, lookup of each generated process, and
  interpreter fallback for an unsupported sibling;
- bounded O0/O2 SystemVerilog and O2 mixed-language application
  interpreter-versus-hybrid differential tests;
- normalized serialized VCD equality in the bounded application differential
  harness, covering declarations, initial values, timestamps, and committed
  changes;
- bounded VHDL/SystemVerilog immediate assertion parsing and scalar SimIR
  lowering, with process/instruction/severity/source/message retained through
  interpreter, O0, O2, CLI, and the synchronous C API assertion callback;
- exact interpreter/O2-hybrid application evidence for tick-0 update and
  tick-2 delayed commits, plus interpreter/compiled overflow exception
  containment;
- exact interpreter/O2-hybrid application evidence for positive-edge wakeup at
  tick 1/delta 1, including tick-0/tick-2 signal changes and two of two
  processes compiled;
- application native-cache telemetry with cold miss/store and warm-hit
  assertions for every compiled specialization module at O0 and O2, including
  a two-process/one-module sensitivity fixture;
- persistent object-cache cold/warm, invalidation, corruption, and
  incompatible-object tests, including scheduled-write kind and delay cache
  identity plus wait kind, operands, referenced widths, and dynamic/static
  edge data;
- O0/O2 grouped-module cache tests proving two functions share one native
  object, a changed group member invalidates that object, and the unchanged
  member retains its per-process frame identity;
- forced-O0 hybrid debugger selection with per-process fallback and exact
  interpreter equivalence for the bounded REPL/scheduler-safe-point script;
- single-read HDL parse/content hashing and per-specialization native
  provenance covering the owning source, source-set semantics,
  bundled-library marker, and represented generic/parameter values, with
  tested reuse across unrelated-source changes and invalidation for owning
  source, separated VHDL entity interface, and standard changes;
- interpreter/O2-hybrid application evidence for bounded construction actuals
  crossing explicit VHDL-to-SystemVerilog and SystemVerilog-to-VHDL bindings,
  including final values and cold/warm specialization-cache behavior;
- checked-in SV-testbench/VHDL-counter/SV-child example.

Remaining before the architecture gate passes:

- add call safe points, complete nested/scoped local-variable semantics, C API
  local objects, and complete source metadata to the current O0 hybrid
  debugger;
- run every supported semantic test through interpreter and JIT and compare
  final state, assertions, scheduler observations, and trace events;
- extend the bounded mixed-language differential to O0 and mixed-language
  assertion failures, broaden normalized trace coverage across semantic
  fixtures, and validate it on LLVM 22.1.8 Windows; and
- complete VHDL generic types and the remaining SystemVerilog parameter
  type/sizing rules; bounded scalar VHDL and integral SystemVerilog values
  already participate in per-specialization native-cache identity and cross
  explicit VHDL/SystemVerilog bindings. Conditional, bounded iterative, and
  bounded selection generate plus local signals, assignments, processes, and
  instances are implemented. Unguarded VHDL blocks, all three bounded implicit
  SV generate forms, direct/named static SV generate contents, and bounded
  generated constants/parameters also execute;
  guarded VHDL blocks, noncanonical SV loop-update expressions, nonintegral
  VHDL choices, and additional generated declarative/module items remain.

### 3. Near-full synthesizable frontend coverage — Pending

Early groundwork:

- handwritten tokenization and recursive-descent/precedence parsing;
- VHDL entity/architecture/port/signal/process nodes and represented
  library/use/context-reference clauses, reusable bounded context declarations,
  bounded constant-only package declarations and use visibility, bounded
  generic specialization, and
  executable conditional/iterative/selection generate and unguarded block
  statements;
- SV modules, common declarations, simple hierarchy, basic procedural and
  continuous statements, bounded integral parameter specialization and
  bounded integral packages/imports/packed typedef aliases, enums,
  non-nested packed structs, equal-width packed unions, constant
  aggregate-member bit/part-selects, constant indexed part-selects, and
  constant replication concatenations, arithmetic shifts, complemented
  reductions, binary XNOR, and bounded scalar built-in gate primitives with
  shared integer delays and multiple instances per declaration,
  executable explicit/implicit conditional, inline/module-genvar iterative,
  selection, and direct/named static generate bodies, and bounded
  `` `timescale`` handling;
- domain, width, signedness, duplicate-declaration, driver, and binding checks;
  and
- a checked-in feature matrix with positive, negative, elaboration, and
  runtime evidence columns.

Planned implementation sequence:

1. Separate the compact frontend representation into language-specific typed
   HIR and an explicit elaborated DesignIR.
2. Expand the bounded project-package/context visibility slice into complete
   VHDL libraries, packages/bodies, context semantics, configurations,
   generics, overload/type resolution, constant evaluation, and reviewed IEEE
   packages.
3. Complete the remaining Verilog/SV `` `line``/pragma semantics, parameters,
   nested/general package structs, unions, general enum types, and subprograms,
   general-body generates, broader legal noncanonical genvar/case-choice
   forms, and complete synthesizable types. Bounded parameterized packed
   integral package/module aliases now resolve through imports and scoped
   names.
   Includes, macros with default arguments, conditionals, compilation-unit
   sharing, cache provenance, `` `default_nettype``, and
   reset/cell/keyword/unconnected-drive state are implemented.
4. Complete synthesizable statements, expressions, aggregates, memories,
   arrays, records/structs/unions/enums, functions/tasks, and hierarchy
   specialization.
5. Implement driver objects, VHDL resolution, SV net resolution, and complete
   mixed-boundary conversions.
6. Add atomic positive, negative, elaboration, interpreter, and JIT tests for
   every promised matrix row.

This milestone remains pending until every required synthesizable row in the
[feature matrix](feature-matrix.md) has executable evidence.

### 4. Procedural testbenches, SystemC, and interactive visibility — In progress

Completed groundwork:

- deterministic scheduler phases for active, inactive, update, and postponed
  work;
- delayed/update and dynamic/static wait operations in the SimIR interpreter
  and bounded LLVM compiled subset;
- bounded VHDL timed/any-change waits with implicit process repetition and
  SystemVerilog any-change/scalar-edge procedural event controls;
- inferred wildcard sensitivities for bounded `always @*`, `always_comb`,
  `always_latch`, and dynamic `@*`;
- bounded Verilog/SystemVerilog `case`, `casez`, and `casex` statements with
  ordered exact/wildcard four-state alternatives and default fallback;
- bounded VHDL sequential `for` loops with locally static `to`/`downto`
  ranges and deterministic common-SimIR unrolling;
- bounded SystemVerilog procedural `for` loops with canonical inline indices,
  comparison bounds, and unit-step updates through the common loop HIR;
- bounded Verilog/SystemVerilog `repeat` statements with locally static
  nonnegative counts and deterministic zero-count behavior;
- executable multi-language `while` and timing-controlled
  Verilog/SystemVerilog `forever` loops through suspension-safe SimIR
  backedges;
- nested SystemVerilog `break`/`continue` and VHDL `exit`/`next` control for
  statically unrolled and runtime loops;
- VHDL labeled loops and targeted outer-loop `exit`/`next` control;
- unconditional VHDL loops and post-test SystemVerilog `do-while` loops;
- VHDL bare and combined `on`/`until`/`for` waits with first-suspend,
  dependency-driven re-evaluation, event/timeout races, and non-polling
  permanent suspension, plus immediate-test Verilog/SystemVerilog condition
  waits;
- bounded nested VHDL and SystemVerilog conditional statements, including
  language-specific Boolean/four-state condition rules;
- deterministic project seed handling;
- scope/signal navigation, source/time/signal-change breakpoints including
  exact-state signal conditions, all four step modes, live debug-trace
  selection, value mutation, and forced-O0 hybrid execution in the CLI
  debugger, with bounded interpreter-equivalence evidence;
- hierarchy/value/control/callback operations in `include/fsim/api.h`;
- automated C API statement/process/delta/time stepping and callback-issued
  asynchronous stop/resume coverage, including terminal `$finish` precedence
  when it coincides with an external step stop;
- buffered committed-change VCD with packed and nine-state mapping;
- SystemC values, signals, ports, exports, time/event/process declarations in
  the compatibility facade;
- versioned plug-in registration, exception containment, dynamic loading, and
  cached GCC/Clang/MSVC command construction;
- typed SystemC factory construction with owned native-module lifetime,
  ABI-neutral port metadata, and append-only foreign-child registration;
- recursive HDL-to-SystemC and SystemC-to-HDL elaboration with either language
  family as the selected top;
- facade-defined `SC_METHOD` registration, time-zero initialization,
  `dont_initialize()`, static any-change/scalar-edge sensitivities, packed
  runtime port access, common update-phase writes, dynamic time/event
  `next_trigger`, and immediate/delta/timed named-event notification,
  replacement, cancellation, strict `notify_delayed`, OR/AND list
  sensitivity, and registered primitive-channel updates through the common
  scheduler;
- module-local `sc_signal` objects backed by common DesignIR signals,
  including initial values, coalesced writes, committed reads, static/dynamic
  sensitivity, and delta-scoped change events;
- typed `sc_in`/`sc_out`/`sc_inout` bindings to internal signals represented
  as single DesignIR aliases across HDL/SystemC instance boundaries;
- native `sc_module` child members with recursive descriptions, stable
  hierarchy handles, child process execution, and direct-parent signal
  binding;
- isolated per-build-root lifecycle phases, direct child-to-parent port
  chains, standard signal interface classes, and typed export hierarchy
  chains resolved to common signals; and
- compiler-emitted dependency closure for GCC-like SystemC builds.

Planned implementation sequence:

1. Complete procedural SV/VHDL testbench data, files, random facilities,
   expression/wildcard and named events, fork/join, remaining waits,
   assertions, and display/report behavior.
2. Complete inertial/transport/reject, NBA, named-event, and cross-language
   zero-delay scheduling semantics.
3. Complete arbitrary custom-interface metadata, multi-hop hierarchical port
   policies, and broader standard channel behavior; standard signal
   interfaces, export metadata, module lifecycle phases, native construction,
   direct-parent port chains, typed port-to-signal aliases, module-local
   `sc_signal`, primitive-channel update dispatch, `notify_delayed`, dynamic
   method sensitivity, OR/AND event expressions, pending-notification rules,
   and cancellation now use the common scheduler.
4. Complete Windows execution evidence and broader event/list/error tests for
   the Boost.Context 1.91.0 `SC_THREAD`/`SC_CTHREAD` implementation; Linux
   timed, delta, and static-wait execution is implemented.
5. Complete nested/scoped debug locals and add call safe points.
6. Complete public C API metadata, remaining object kinds, and
   forward-compatibility tests; the bounded assertion callback now carries
   process, severity, source location, and message.

TLM, AMS, CCI, dynamic SystemC process creation, arbitrary custom primitive
channels, and Accellera ABI/kernel compatibility remain deferred.
Foreign HDL children are elaboration-time hierarchy objects, not dynamic
SystemC process-time module creation.

### 5. Release hardening — In progress

Completed groundwork:

- exact diagnostic catalog and consistency gate;
- cache locking, atomic publication, checksums, corruption recovery, and
  dependency invalidation tests;
- Debug/Release, ASan/UBSan, LLVM 22, and frontend-fuzzer CI definitions;
- install rules for the supported C/SystemC public surface;
- strict installed-header/API consumer smoke tests;
- source-build, architecture, language-support, cross-language, SystemC, and
  feature-matrix documentation; and
- deterministic CLI example and normalized VCD checks.

Remaining before v1 release:

- keep Linux and Windows continuously green with LLVM enabled where required;
- run and grow sanitizer/fuzzer corpora until the full frontends and serialized
  SimIR are covered;
- complete cache size/age eviction and every compiler/environment fingerprint;
- finish Windows Unicode path, Ctrl-C, DLL, plug-in compiler, and cache tests;
- add benchmark runners and track cold build, warm startup, events/second,
  memory, wide values, resolved nets, crossings, trace overhead, and debug
  overhead;
- import only license-reviewed external tests and package sources with
  preserved notices; and
- close every required v1 feature-matrix row in both Debug and Release on
  Ubuntu/GCC and Windows/MSVC.

## Immediate next execution plan

The next development iterations should occur in this order:

1. **Close the architecture gate:** extend the bounded O0 hybrid debugger with
   call instrumentation and complete scoped locals, add remaining VHDL generic
   and SystemVerilog parameter semantics, and broaden the
   interpreter/JIT differential harness.
2. **Build typed semantic layers:** explicit VHDL HIR, SV HIR, DesignIR
   specialization, constant evaluation, and stable source/debug metadata.
3. **Expand synthesizable coverage:** packages/parameters/generics, generates,
   full types and expressions, drivers, and resolution.
4. **Complete mixed-language semantics:** ordinal vector mapping, state-domain
   conversions, explicit resolvers, construction-parameter transfer in both
   SystemC hierarchy directions, and delay/delta/NBA matrices.
5. **Implement procedural testbenches and the SystemC kernel:** dynamic data,
   files/random/events, factories, channels, methods, and fibers.
6. **Complete visibility:** source-level debugger behavior, trace selection,
   public API metadata, and normalized differential trace tests.
7. **Harden for release:** Windows LLVM gates, fuzzing, Unicode/path behavior,
   cache eviction/fingerprinting, benchmarks, and full feature-matrix closure.

Each iteration must add or update feature-matrix evidence and run through the
interpreter/JIT differential harness once the affected operation is supported
by both engines.

## v1 release condition

fsim v1 may be declared only when:

- every required VHDL, Verilog/SV, SystemC, mixed-language, debugger, API, and
  runtime row has positive, negative, elaboration, and runtime evidence where
  applicable;
- interpreter, O2 `run`, and O0 `debug` semantics agree;
- Ubuntu x86-64/GCC and Windows x86-64/MSVC Debug and Release gates pass;
- LLVM 22.1.8, cache, SystemC plug-in, VCD, debugger, sanitizer, and fuzz gates
  are green;
- recursive VHDL/SystemVerilog/SystemC hierarchy passes elaboration and runtime
  matrices in every parent-to-child language direction, including mixed
  hierarchy rooted at SystemC;
- no promised syntax is silently ignored; and
- remaining unsupported features are explicitly documented as deferred rather
  than implied to be part of v1.
