<!-- SPDX-License-Identifier: Apache-2.0 -->
# Implementation plan and progress

## Purpose

This document records the implementation sequence for fsim v1 and the
evidence-backed progress of the current repository. It complements the
[feature matrix](feature-matrix.md), which tracks individual language and
runtime features.

Last updated: 2026-08-02.

The repository is currently a pre-alpha architecture vertical slice. It is not
the fsim v1 release, and a milestone is not complete merely because its
interfaces or test scaffolding exist.

The concise entry point for a new implementation session is the
[v1 resume handoff](v1-resume.md).

## Status definitions

- **Complete**: every stated milestone outcome is implemented and its required
  platform/test gate has passed.
- **In progress**: useful implementation and automated evidence exist, but one
  or more milestone outcomes remain.
- **Pending**: implementation is absent or limited to enabling infrastructure.
- **Deferred**: intentionally outside the v1 scope.

Every active feature batch has a checked-in numbered list of exactly ten
implementation tasks, following the Batch 101 record format. Keep that list
and its evidence current while the batch is **In progress**. When all ten
tasks and required gates close, retain the list in the chronological record,
change its status to **Complete**, and add the next batch's ten-task list as
the sole current **In progress** batch.

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
- Tcl is the first scripted automation surface. Interactive and batch Tcl
  commands must wrap the same project, session, debugger, and callback model
  as the CLI and native C API; they do not own a separate simulation kernel.
- The bundled Tcl dependency tracks the latest stable Tcl release available
  when its reproducible source pin is reviewed. The fallback is pinned to Tcl
  9.0.4, which the official Tcl site identified as the recommended stable
  release when reviewed on 2026-07-28. Discovery accepts patchlevel 9.0.4 or
  newer within the 9.0 release series; release-candidate work must recheck the
  upstream recommendation and pin.
- Python automation follows later, after the Tcl command model and opaque
  native session/object ABI are stable. It must reuse those semantics rather
  than introduce a second control path.

## Current validated baseline

The following foundation is implemented:

- C++20 project structure for Linux and Windows x86-64, with CMake 3.28
  presets, warning policy, Apache-2.0 licensing, and dependency-version policy;
- a green 12-job GitHub Actions matrix covering Linux GCC Debug/Release,
  exact LLVM 22.1.8 Debug/Release, ASan/UBSan, and Clang frontend fuzzing,
  plus Windows MSVC Debug/Release and both MSVC and clang-cl with exact
  LLVM 22.1.8 in Debug/Release;
- embedded Tcl command/script/interactive execution with fsim-owned standard
  streams, an installed-or-SHA-256-pinned-source CMake dependency path,
  relocatable bundled standard-library discovery, and a stateful adapter over
  the common O0 debugger for hierarchy/value inspection,
  mutation, run control, breakpoints, all four stepping modes, structured
  diagnostics, live VCD selection, and synchronous safe-point/value/lifecycle
  callbacks with callback-safe stop/resume;
- Tcl 9.0 discovery/fallback policy with a checksum-pinned 9.0.4 source
  archive, derived Unix/MSVC static-library names for both Windows CRT modes,
  legacy/older/other-series rejection, `Tcl_Size` object-command APIs, channel
  version 5 streams, and relocatable installed standard-library discovery;
- composable scheduler safe-point observers that preserve Tcl callbacks while
  the debugger or Ctrl-C control hook is replaced, transactional Tcl project
  replacement, runtime trace configuration, and assertion
  callback/diagnostic metadata;
- packed 2-, 4-, and 9-state value kernels plus a domain-preserving common
  transport value with exact Logic9 signals, registers, projected
  transactions, driver slots, standard resolution, debug/C API reads, and
  VCD mapping, plus bounded direct LLVM O0/O2 execution through four ordinal
  planes and append-only exact runtime callbacks for values up to 64 elements;
- deterministic single-thread scheduling and a typed SimIR interpreter;
- bounded handwritten VHDL-2008 and Verilog/SystemVerilog frontends;
- executable SystemVerilog procedural lexical blocks with named-scope
  validation, same-scope duplicate rejection, nested local/signal shadowing,
  block-entry initialization, stable hierarchical debugger names, and one
  reusable frame object per declaration across static-loop expansion;
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
- buffered VCD, a bounded command-line debugger, and a versioned native C API
  with generation-safe process-variable child objects, typed/full-path
  metadata, canonical retained-value reads, and explicit unavailable state
  before a lexical block first executes;
- an LLVM 22.1.8 ORC adapter with caller-owned resumable process frames for
  processes whose value-bearing operations are up to 64 bits, including
  append-only caller-owned Logic9 register planes, with O0/O2
  lowering for update-phase/delayed writes plus dynamic/static sensitivity
  waits, integrated as a hybrid per-process engine for `build`/`run` and a
  forced-O0 hybrid engine for the bounded `debug` path;
- source-bearing call safe points for executable SystemVerilog built-ins and
  VHDL attribute calls, including nested-call ordering, interpreter/O0/O2
  hooks, and shared debugger/native-C statement stepping;
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
- a stable catalog covering 671 unique current production diagnostic codes.

Current and most recent aggregate regression snapshots:

| Gate | Result |
|---|---|
| GitHub Actions cross-platform matrix | All 12 jobs pass in [run 30398251973](https://github.com/colinphill/fsim/actions/runs/30398251973) on 2026-07-28: Linux GCC Debug/Release, exact LLVM 22.1.8 Debug/Release, ASan/UBSan, Clang frontend fuzz smoke, Windows MSVC Debug/Release, and Windows MSVC/clang-cl exact LLVM 22.1.8 Debug/Release |
| GCC Debug, LLVM disabled | Previous 13/13 baseline passes; the fast expression target was added afterward |
| GCC Release, LLVM disabled | Previous 13/13 baseline passes; the fast expression target was added afterward |
| LLVM 22.1.8 Debug, warnings-as-errors | 20/20 tests pass after the eighteenth post-gate batch, including exact delayed named-event timestamps and next-delta wakeups across interpreter and LLVM O0/O2 (141.16 seconds wall time with four-way CTest parallelism) |
| LLVM 22.1.8 Release, warnings-as-errors | Previous 14/14 baseline passes; the fast expression target was added afterward |
| Concurrent LLVM Debug and Release suites | Previous paired baseline passes; cache-test paths are isolated |
| GCC ASan/UBSan | 14/14 tests pass after the three-feature batch, with no ASan/UBSan findings |
| Boost.Context 1.91.0 fibers | Checksum-verified fetch/configure/build; current LLVM Debug 19/19 and ASan/UBSan 14/14 aggregate suites pass, including compiled-SV/SystemC application tests |
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
| SystemVerilog final procedures | Exact LLVM 22 frontend/runtime/diagnostic-catalog plus the fast expression application target for noninitializing final-process metadata, exactly-once natural-quiescence and `$finish` execution, stable ordering, stopped-identity preservation, suspension/NBA rejection, Verilog-2005 rejection, and interpreter/LLVM O0/O2 equivalence |
| Verilog/SystemVerilog resumable stop | Exact LLVM 22 frontend/runtime/compiler/diagnostic-catalog plus the fast expression application target for `$stop` and `$stop(argument)`, distinct non-design pause identity, next-instruction frame preservation, deferred finals, explicit clear-and-resume, and interpreter/LLVM O0/O2 equivalence |
| SystemVerilog fatal task | Exact LLVM 22 frontend/diagnostic-catalog plus focused source failure execution for standalone and immediate-assertion `$fatal`, optional numeric finish control, literal message retention, Verilog-2005 rejection, and the common interpreter/LLVM O0/O2 failure-severity path |
| VHDL packed-object attributes | Exact LLVM 22 frontend/elaboration/diagnostic-catalog plus the fast expression application target for `'left`/`'right`/`'low`/`'high`/`'length`/`'ascending`, optional dimension `1`, declared ascending/descending ranges, and interpreter/LLVM O0/O2 equivalence |
| VHDL signal event attribute | Exact LLVM 22 frontend/runtime/compiler/diagnostic-catalog plus the fast expression application target for delta-scoped `'event`, classic rising-event guards, appended plain-C runtime callback validation, and interpreter/LLVM O0/O2 equivalence |
| VHDL signal last-value attribute | Exact LLVM 22 frontend/runtime/compiler/diagnostic-catalog plus the fast expression application target for packed `'last_value`, retained effective values across events, appended plain-C value callback validation, and interpreter/LLVM O0/O2 equivalence |
| VHDL signal last-event attribute | Exact LLVM 22 frontend/runtime/compiler/diagnostic-catalog plus the fast expression application target for 64-bit `'last_event`, a nonzero elapsed-time sample, `TIME'HIGH` initialization policy, appended plain-C time callback validation, and interpreter/LLVM O0/O2 equivalence |
| VHDL zero-duration stable attribute | Exact LLVM 22 frontend/elaboration/diagnostic-catalog plus the fast expression application target for `'stable` in and outside an event delta, common event-negation lowering, and interpreter/LLVM O0/O2 equivalence |
| VHDL signal active attribute | Exact LLVM 22 frontend/runtime/compiler/diagnostic-catalog plus the fast expression application target for transaction-scoped `'active`, redundant same-value assignment versus value-changing `'event`, appended plain-C transaction callback validation, and interpreter/LLVM O0/O2 equivalence |
| Design-stop final isolation | LLVM 22 runtime/elaboration and full 15/15 aggregate regression for `$finish` with a timed `forever` process still pending; ordinary queued work is discarded before exactly-once final procedures run at the stop timestamp |
| VHDL conditional assignments | Exact LLVM 22 frontend/elaboration/diagnostic-catalog plus the fast expression application target for concurrent and sequential VHDL-2008 `when`/`else` assignments, chained source-order alternatives, Boolean-condition enforcement, missing-else recovery, and interpreter/LLVM O0/O2 equivalence |
| VHDL selected signal assignments | Exact LLVM 22 frontend/diagnostic-catalog plus the fast expression application target for labeled/unlabeled `with`/`select`, grouped exact choices, final `others`, retained waveform delays, inferred selector/value sensitivity, timed reactive selection, and interpreter/LLVM O0/O2 equivalence |
| VHDL projected output waveforms | Exact LLVM 22 frontend/runtime/elaboration/compiler/C-ABI/diagnostic-catalog plus the focused VHDL projected-waveform application target pass for ordered per-element delays, conditional/selected `unaffected`, implicit/explicit inertial, transport, explicit rejection, sequential/concurrent whole and slice assignments, atomic per-scalar transaction editing, exact pulse boundaries, VCD, interpreter/LLVM O0/O2 equality, and cold/warm native-cache behavior |
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
sanitizer suites are normally batched after ten feature additions and before
release-facing changes; this keeps feature feedback sub-second to a few
seconds without weakening the periodic regression gate. Expression work can
select its application gate with
`ctest --test-dir <build> -L expressions --output-on-failure`.

Feature-batch cadence is intentionally batch-level, not task-level. Tasks 1
through 9 remain in the working tree while their status and concise focused
evidence stay current in this plan and `v1-resume.md`; each uses one
eight-worker development build and only the smallest test selection relevant
to the changed contract. Do not run sanitizer, the full Debug/Release suites,
or routine commit/push checkpoints for each individual task. Task 10 owns the
single sanitizer, source/catalog, full Debug/Release, documentation, commit,
and push closure for the accumulated batch. An intermediate commit is reserved
for an explicitly requested shutdown/restart handoff or a risky structural
transition that cannot safely remain only in the working tree.

The Windows LLVM jobs install the exact full LLVM 22.1.8 development archive
through a repository-owned PowerShell adapter. It verifies the published
SHA-256 digest, uses 7-Zip for the XZ layer and Windows `tar` for the inner
archive, then independently verifies both `llvm-config` and
`LLVMConfig.cmake`. Repository-owned Visual Studio discovery and environment
export replace the former Node 20 setup actions; checkout uses the Node
24-based `actions/checkout@v7`. This avoids both deprecated action runtimes and
accepting a compiler-only package when the ORC development libraries are
required.

### Cross-platform implementation and test rules

The Windows CI repair established rules that apply to every future feature
and test:

- compare existing paths by filesystem identity where possible. Assertions
  over diagnostics or debugger transcripts must check stable semantic
  components such as filename, line, and column rather than an exact absolute
  spelling; separators, drive-letter case, and Windows short/canonical path
  aliases are not semantic differences;
- use the shared environment helper instead of calling `getenv` directly.
  Its Windows implementation owns `_dupenv_s` storage, avoiding deprecated
  insecure-CRT APIs while preserving the same optional-value contract;
- keep CRT boundaries explicit. Ordinary Windows builds use the dynamic CRT,
  while jobs consuming the official LLVM archive use the archive-compatible
  static CRT; fetched Tcl and other native dependencies must use the matching
  mode, and required DLLs must be staged beside test executables;
- do not assume GCC/Clang dependency-file behavior from MSVC. Conservative
  MSVC dependency scanning may mark SDK-backed SystemC plug-ins uncacheable,
  which tests must distinguish from an incorrect cache hit;
- determine cached native-object format from the parsed object file, not from
  a reconstructed target triple whose missing OS may incorrectly default a
  valid Windows COFF object to ELF;
- keep MSVC command and source limits in mind: split oversized generated raw
  string fixtures and avoid relying on shell expansion or platform-specific
  command quoting; and
- run the smallest affected Windows-sensitive test during development, then
  require the full Linux/Windows matrix at the ten-feature regression boundary
  and for CI/workflow changes;
- use at least eight parallel workers for every local project, test-support,
  and fetched-dependency build, including focused interim builds; GitHub
  Actions is the explicit exception and uses four workers after the operation-
  storage footprint repair; and
- at every tenth numbered feature batch, inspect the pushed GitHub Actions
  handoff, fix all actionable failures, rerun the affected local gates, push
  the repair, and confirm the replacement checks before continuing.

## Milestone progress

### 1. Platform and semantic spine — In progress

Completed:

- Apache-2.0 license and SPDX coverage;
- CMake 3.28 project and Linux Ninja presets;
- C++20 enforcement, warnings-as-errors gates, and Linux/Windows x86-64
  configuration checks;
- exact LLVM 22.1.8 discovery behind a narrow adapter target;
- central version policy for CLI11 2.6.2, toml++ 3.4.0, Boost.Context 1.91.0,
  Tcl 9.0.4, and Catch2 3.15.2, with installed-or-pinned-source
  Boost.Context and Tcl consumption;
- Tcl discovery that rejects installed Tcl 8.6, pre-9.0.4, malformed, and
  non-9.0 headers, plus a pinned 9.0.4 fallback using version-derived Linux
  and MSVC library names and a Tcl 9 `Tcl_Size` embedding boundary;
- source spans, structured diagnostics, schema-1 manifest loading, glob/order
  handling, and a diagnostic catalog consistency test;
- versioned C API and SystemC plug-in ABI skeletons;
- Linux GCC and Windows MSVC Debug/Release workflow definitions;
- exact LLVM 22.1.8 Linux and Windows Debug/Release workflow definitions,
  including Windows MSVC and clang-cl builds using the pinned full development
  archive;
- interactive/batch Tcl 9.0.4 fallback, relocation, callback, debugger,
  failure-exit, native-C linkage, and LLVM-backed execution validated on the
  Windows and Linux CI matrix; and
- sanitizer and VHDL-parser/Verilog-SV-preprocessor-parser fuzzer workflow
  definitions.

Remaining before completion:

- consume the planned support dependencies where their corresponding features
  are implemented, instead of only pinning version policy;
- complete cross-platform Unicode/path and console-interrupt validation.

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
- deterministic cache enumeration and hit-refreshed LRU pruning by age,
  entry count, and encoded bytes, with live-lock avoidance, stale-temporary
  cleanup, malformed-file preservation, empty-shard reclamation, and
  best-effort LLVM startup telemetry;
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

- complete source metadata for remaining executable constructs in the current
  O0 hybrid debugger; bounded calls, nested/scoped SystemVerilog packed locals,
  and native-C variable objects now have interpreter/LLVM O0/O2 and
  cache-differential evidence;
- run every supported semantic test through interpreter and JIT and compare
  final state, assertions, scheduler observations, and trace events;
- extend the bounded mixed-language differential to O0 and mixed-language
  assertion failures, broaden normalized trace coverage across semantic
  fixtures, and validate it on LLVM 22.1.8 Windows; and
- complete VHDL generic types and the remaining SystemVerilog string,
  wider-than-64-bit, non-packed type-actual, and full expression-typing rules;
  bounded scalar VHDL plus typed integral SystemVerilog values and type
  parameters already participate in
  per-specialization native-cache identity and cross explicit
  VHDL/SystemVerilog bindings. Conditional, bounded iterative, and bounded
  selection generate plus local signals, assignments, processes, and
  instances are implemented. Unguarded VHDL blocks, all three bounded
  implicit SV generate forms, direct/named static SV generate contents, and
  bounded generated constants/parameters also execute;
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
- embedded Tcl batch commands, script arguments/exit status, and a multiline
  interactive shell with standard streams owned by the fsim invocation,
  including project/check/build, hierarchy/value, mutation, run, and status
  commands over the shared application objects;
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
   expression/wildcard, general event expressions, fork/join, remaining waits,
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
4. Maintain the current Windows execution evidence and complete broader
   event/list/error tests for the Boost.Context 1.91.0
   `SC_THREAD`/`SC_CTHREAD` implementation; Linux timed, delta, and static-wait
   execution is implemented.
5. Complete source metadata for remaining executable constructs and add
   resolved per-driver storage with multi-driver semantics. Native C instance,
   generate-region, process, lexical-scope, packed-variable, signal, port, and
   current single-driver objects now carry retained source metadata.
6. Complete remaining public C API object kinds and richer value metadata.
   Append-only input/output prefixes, legacy/current/future structure sizes,
   strict C11 use, and assertion diagnostic metadata now have automated
   compatibility evidence.

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
- a green 12-job Linux/Windows matrix with exact LLVM 22.1.8 exercised through
  GCC, MSVC, and clang-cl where applicable;
- install rules for the supported C/SystemC public surface;
- strict installed-header/API consumer smoke tests;
- source-build, architecture, language-support, cross-language, SystemC, and
  feature-matrix documentation; and
- deterministic CLI example and normalized VCD checks.

Remaining before v1 release:

- keep Linux and Windows continuously green with LLVM enabled where required;
- run and grow sanitizer/fuzzer corpora until the full frontends and serialized
  SimIR are covered;
- complete every remaining compiler/environment cache fingerprint;
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

1. **Close the architecture gate:** add remaining VHDL generic and
   SystemVerilog parameter semantics, complete remaining executable-source
   metadata, and broaden the interpreter/JIT differential harness.
2. **Build typed semantic layers:** explicit VHDL HIR, SV HIR, DesignIR
   specialization, constant evaluation, and stable source/debug metadata.
3. **Expand synthesizable coverage:** packages/parameters/generics, generates,
   full types and expressions, drivers, and resolution.
4. **Complete mixed-language semantics:** ordinal vector mapping, state-domain
   conversions, explicit resolvers, construction-parameter transfer in both
   SystemC hierarchy directions, and delay/delta/NBA matrices.
5. **Implement procedural testbenches and the SystemC kernel:** dynamic data,
   files/random/events, factories, channels, methods, and fibers.
6. **Complete visibility and automation:** finish source-level debugger
   behavior, trace selection, public API metadata, and normalized differential
   trace/control tests. The embedded interactive/batch Tcl shell is present;
   project/check/build, hierarchy/value access, debug run/step, breakpoints,
   mutation, script arguments, and deterministic batch exit status use the
   common application/debug engines. Structured diagnostics, live trace
   selection, callback-safe stop/resume, and synchronous
   safe-point/value/lifecycle/assertion callbacks are present. Project loading
   is transactional, resets live/poisoned sessions, and retains Tcl callback
   registrations; runtime trace paths and filters may be configured before
   simulation starts.
7. **Harden for release:** preserve the continuous Windows LLVM gates, expand
   fuzzing, Unicode/path behavior, cache eviction/fingerprinting, benchmarks,
   and full feature-matrix closure.

After v1 Tcl and native-C control surfaces stabilize, plan an interactive and
batch Python interface over the same opaque handles and operations. Python
packaging, stable-ABI policy, notebook integration, and async ergonomics are
post-v1 scope.

Each iteration must add or update feature-matrix evidence and run through the
interpreter/JIT differential harness once the affected operation is supported
by both engines.

## Recent ten-feature regression batches

The eighteenth post-gate batch is implementation-complete:

1. nonblocking named-event triggers accept an optional integer delay;
2. the existing timescale-aware delay parser supplies normalized metadata;
3. HIR retains the delayed trigger on the event statement itself;
4. delayed immediate `->` syntax receives a targeted diagnostic;
5. target resolution and event-type checks remain shared across trigger forms;
6. the future event value is computed from current event state;
7. delayed publication lowers to typed `WriteAfter`;
8. the event changes at the exact requested future timestamp;
9. waiters resume in the following delta before later `$finish`; and
10. interpreter, LLVM O0, and LLVM O2 agree on operation kind, delay,
    event/observer timestamps, deltas, values, and compiled-process counts.

Focused frontend, elaboration, and named-event application tests pass. The
interval LLVM 22 Debug regression passed all 20 tests in 141.16 seconds, and
commit `3cfb932` is pushed. A subsequent forced no-system-Tcl probe downloaded,
checksum-verified, built, and linked Tcl 8.6.18; relocation testing then found
and drove the bundled standard-library packaging fix in the next batch.

## Most recent ten-feature regression batch

The nineteenth post-gate batch is complete:

1. CMake now falls back to a checksum-pinned native Tcl 8.6.18 static build
   when development files are absent, installs its standard-library scripts
   and license, locates the scripts relative to the actual fsim executable,
   and supports `FSIM_TCL_LIBRARY` for custom package layouts.
2. Verilog-2005/SystemVerilog literal and empty `$display` now lower to a
   typed synchronous output operation with process/time/delta metadata, route
   through CLI and Tcl-owned streams, and execute directly through the
   append-only LLVM O0/O2 runtime callback.
3. Verilog-2005/SystemVerilog literal and empty `$write` reuse the typed output
   path with explicit no-newline behavior, including deterministic
   concatenation across interpreter, LLVM O0/O2, CLI, and Tcl-owned streams.
4. Verilog-2005/SystemVerilog literal and empty `$strobe` now schedule typed
   output in the current timestamp's postponed phase through interpreter and
   an append-only LLVM callback, retaining stable process ordering.
5. Output-task literals now decode newline, tab, quote, backslash, and
   one-byte octal escapes in the frontend and retain the resulting exact byte
   string through interpreter, LLVM O0/O2, CLI, and Tcl output routing.
6. VHDL-2008 literal `report` at default or explicit `note` severity now
   decodes doubled quotes and reuses the common typed immediate output path;
   higher severities are targeted until stop-threshold policy is implemented.
7. Verilog-2005/SystemVerilog literal or empty `$monitor` now performs its one
   initial postponed publication; value-sensitive monitor lists remain
   targeted until formatting operands and monitor replacement are available.
8. SystemVerilog `$fatal` and immediate-assertion `$error` literal messages
   now use the output-task escape decoder and retain identical decoded
   diagnostics through interpreter and LLVM O0/O2 execution.
9. Verilog-2005/SystemVerilog output tasks now accept one known unsigned
   numeric literal in decimal, binary, octal, or hexadecimal syntax, apply
   declared-width truncation, and emit its default decimal representation.
10. Based output literals marked signed now use declared-width two's-complement
    interpretation and emit the corresponding signed decimal representation.

The forced no-system-Tcl dependency build and its isolated staged relocation
probe pass; the ordinary installed-Tcl application regression also passes.
Focused frontend, elaboration, runtime, C ABI, LLVM, Tcl, and output
application tests pass for the cross-language output slices. The interval LLVM
22 Debug regression passed all 21 tests in 144.72 seconds on 2026-07-28,
including the 144.71-second broad application integration test. The batch ends
at feature commit `7cd9fda`; the gate-record commit is pushed with the batch.

### Twentieth post-gate batch

The twentieth post-gate batch is complete:

1. VHDL-2008 literal reports at `note`, `warning`, and `error` severity now
   lower to a distinct typed operation retaining source metadata, continue
   after synchronous interpreter/LLVM delivery, render through CLI/Tcl, and
   reach the native C assertion callback without terminating the session.
2. Verilog-2005/SystemVerilog `$display` and `$write` now accept one `%b`
   conversion with one packed runtime expression, preserve four-state width,
   prefix/suffix text and `%%`, and execute through a typed interpreter/LLVM
   formatting path.
3. The same runtime formatting operation now supports `%h`, retaining
   lowercase full-width nibbles, uniform X/Z nibbles, and conservative `x`
   output for mixed known/unknown nibbles.
4. Runtime formatting now supports arbitrary-width `%d`, derives signed
   two's-complement behavior from the typed expression, and emits `x` when
   any operand bit is unknown or high impedance.
5. Formatted `$strobe` now evaluates and formats its one operand during active
   execution, captures immutable text, and publishes that text in the
   timestamp's postponed phase even if the operand changes later.
6. Runtime formatting now supports `%o` with full-width three-bit groups,
   uniform X/Z preservation, and conservative `x` output for mixed
   known/unknown groups.
7. Runtime formatting now supports `%c`, consuming the least-significant
   eight bits as one byte and rendering an X/Z-containing byte as `x`.
8. Runtime formatting now supports packed `%s`, emitting bytes
   most-significant first, omitting leading zero padding, and substituting
   `x` for an X/Z-containing byte.
9. `%0b`, `%0h`, and `%0o` now carry typed leading-zero suppression metadata
   through HIR, SimIR, cache identity, interpreter, and LLVM execution while
   retaining at least one digit.
10. VHDL `report ... severity failure` now publishes its severity/source
    metadata once, terminates before any following statement, and uses the
    same typed failure boundary in interpreter and LLVM execution.

Focused frontend, elaboration, runtime, strict C ABI, LLVM, application, Tcl,
native API, and diagnostic-catalog tests pass. The interval LLVM 22 Debug
regression passed all 21 tests in 141.45 seconds on 2026-07-28, including the
141.44-second broad application integration test. The batch ends at feature
commit `65ce752`; the gate-record commit is pushed with the batch.

Development resumed after the completed batch boundary with the Tcl dependency
migration checkpoint below. The twenty-first feature batch has not yet begun.

### Tcl 9 dependency migration checkpoint — Linux complete

The dependency migration is implementation-complete and locally validated:

1. The official Tcl download and version-selection pages were rechecked on
   2026-07-28 and identify Tcl 9.0.4 as the recommended stable release.
2. The fallback pin is Tcl 9.0.4 with SHA-256
   `d0aed49230bc02a65c1e0229e65f34590a4b037ec40d546f32573b467f7551ea`.
3. Release-series parsing derives `libtcl9.0.a`, dynamic-CRT `tcl90s.lib`,
   static-CRT `tcl90sx.lib`, and `share/fsim/tcl9.0`; a CMake script test
   covers those names and accepted/rejected headers.
4. The embedding uses `Tcl_CreateObjCommand2`, `Tcl_Size` for command/list/
   string/script/callback counts, a wide `argc`, channel version 5, and
   `close2Proc`, with a compile-time Tcl 9.0 requirement and no legacy-size
   compatibility casts.
5. The host's installed Tcl 8.6.14 was rejected, the official archive was
   downloaded and checksum-verified, the native static fallback built, and
   the exact LLVM 22.1.8 warnings-as-errors Tcl application, version
   selection, and staged relocation tests passed.
6. A real temporary-prefix install ran without `FSIM_TCL_LIBRARY`, discovered
   its executable-relative `share/fsim/tcl9.0`, and reported patchlevel 9.0.4.
7. The strict native C API and C-header tests pass while linked through the
   Tcl 9 application build.

Windows compile/runtime evidence remains required before this checkpoint is
fully cross-platform; CI runs are not inspected unless the user explicitly
requests it.

### Twenty-first feature batch — SystemVerilog procedural output

All ten implementation features are complete:

1. Accept uppercase output-conversion spellings and the conventional `%x`/
   `%X` hexadecimal aliases without changing lowercase output policy.
2. Carry a decimal minimum field width through HIR, SimIR, cache identity,
   interpreter execution, and LLVM execution.
3. Support `-` left justification for a nonzero field width.
4. Support leading-zero field padding for numeric conversions, including
   sign-aware signed-decimal padding.
5. Support multiple conversions and matching runtime values in one output
   task while preserving evaluation and stream order.
6. Support additional unformatted runtime-expression arguments using the
   language's default decimal rendering.
7. Expand `%m` to the elaborated hierarchical scope without consuming a
   value argument.
8. Expand `%t` from the current global simulation tick at execution or
   postponed-publication capture time.
9. Install value-sensitive `$monitor` output and republish it after a watched
   value changes.
10. Implement `$monitoron` and `$monitoroff` control over the installed
    monitor without changing its registration.

Focused frontend, elaboration, runtime, strict C ABI, LLVM O0/O2, application,
CLI, and diagnostic tests pass. After a completion-audit correction ensured
that literal/empty monitor calls replace an existing watched registration,
the exact LLVM 22.1.8 warnings-as-errors regression passed all 23 tests in
166.34 seconds on 2026-07-28. This includes the frontend, diagnostics catalog,
cache, elaboration, SystemC header/ABI/plugin/compiler, strict JIT C ABI,
LLVM, interpreter/O0/O2 application differentials, Tcl 9 relocation, native
API, and runtime suites. The batch ends at feature commit `5fa32f3`; this gate
record is pushed with the batch and the earlier Tcl 9 migration checkpoint.

### Twenty-second feature batch — deterministic random facilities

All ten implementation features are complete:

1. Carry one effective 64-bit project seed through `BuiltProject`, the
   elaborated interpreter, hybrid LLVM execution, debug execution, and the
   native session path.
2. Make the manifest default seed `1` and numeric manifest/CLI/API overrides
   observable in simulation behavior.
3. Resolve explicit `seed = "random"`/`--seed=random` from host entropy once
   per build and disclose the selected numeric seed for reproducibility.
4. Derive independent deterministic SplitMix64 streams from the project seed
   and dense stable process IDs.
5. Implement SystemVerilog `$urandom` with and without empty parentheses as a
   32-bit unsigned runtime expression.
6. Implement Verilog/SystemVerilog `$random` with and without empty
   parentheses as a 32-bit signed runtime expression.
7. Implement inclusive SystemVerilog `$urandom_range(maximum)` over
   `[0, maximum]`.
8. Implement inclusive SystemVerilog
   `$urandom_range(maximum, minimum)`.
9. Normalize reversed `$urandom_range` bounds deterministically.
10. Return a 32-bit unknown value for an X/Z range bound without consuming
    the process random stream.

Focused frontend, elaboration, runtime, strict C ABI, LLVM O0/O2,
application, CLI, native API, and diagnostic-catalog tests pass. The exact
LLVM 22.1.8 warnings-as-errors regression then passed all 24 tests in 165.52
seconds on 2026-07-28. This includes the frontend, project and diagnostics
catalogs, cache, elaboration, SystemC header/ABI/plugin/compiler, strict JIT C
ABI, LLVM, interpreter/O0/O2 application differentials, Tcl 9 selection and
relocation, native API/C header, and runtime suites. The batch ends at feature
commit `4d078a5`; no GitHub Actions state was queried for this local gate.

### Twenty-third feature batch — immediate assertions and severity tasks

All ten implementation features are complete:

1. Parse and execute standalone SystemVerilog `$info` with bare, empty, and
   literal-message forms.
2. Parse and execute standalone SystemVerilog `$warning` with the same bounded
   forms.
3. Parse and execute standalone SystemVerilog `$error` without terminating an
   otherwise runnable simulation.
4. Route standalone SystemVerilog `$fatal` through the common severity/source
   report path before terminating.
5. Give `assert (condition);` the standard implicit nonfatal error action when
   the condition is false.
6. Execute a simple immediate-assertion pass action only when its condition is
   true.
7. Execute a simple immediate-assertion `else` action only when its condition
   is false.
8. Execute `begin`/`end` pass and failure action blocks through the existing
   lexical-block lowering.
9. Permit `$info`, `$warning`, `$error`, and `$fatal` as immediate-assertion
   actions while retaining their distinct severities and source locations.
10. Make interpreter, LLVM O0/O2, CLI, and native callback behavior agree:
    note/warning/error actions continue, failure publishes once and stops.

Focused frontend, diagnostics, elaboration, runtime, LLVM O0/O2,
application, CLI, Tcl, and native API tests pass. The generated-code semantic
change advances the persistent native-object schema to v12 so older cached
assertion code cannot be reused. The exact LLVM 22.1.8 warnings-as-errors
regression then passed all 25 tests in 171.12 seconds on 2026-07-28. This
includes the frontend, project and diagnostics catalogs, cache, elaboration,
SystemC header/ABI/plugin/compiler, strict JIT C ABI, LLVM, interpreter/O0/O2
application differentials, Tcl 9 selection/relocation/callbacks, native API/C
header, and runtime suites. The batch ends at feature commit `eeeea28`; no
GitHub Actions state was queried for this local gate.

### Twenty-fourth feature batch — Verilog/SystemVerilog `line` provenance

The ten implementation features in this batch are:

1. Accept the exact `` `line <number> "<filename>" <level>`` form.
2. Validate a positive decimal logical line representable by public debug
   metadata.
3. Decode logical filename escapes and safely re-escape `` `__FILE__``.
4. Validate the standard mapping levels `0`, `1`, and `2`.
5. Remap subsequent token, parser, and executable-debug source names.
6. Remap subsequent logical lines while retaining physical offsets/columns.
7. Make `` `__FILE__`` and `` `__LINE__`` observe the active mapping.
8. Retain mapped definition/invocation locations in macro expansion ancestry.
9. Ignore inactive mappings and isolate mapping state across includes and
   ordered compilation-unit roots.
10. Preserve physical ownership/cache provenance separately from logical
    diagnostics, reports, and debug points, with interpreter/LLVM O0/O2 and
    native-cache differential evidence.

Focused frontend, diagnostic-catalog, application, interpreter, LLVM O0/O2,
and native-cache tests pass. The preprocessor cache identity advances to v4
because logical source mapping now affects parsed and generated debug
metadata. The installed-static Tcl path now carries its Unix Threads, zlib,
dynamic-loader, and math link dependencies just as the fetched target does.
The exact LLVM 22.1.8 warnings-as-errors regression then passed all 25 tests in
204.31 seconds on 2026-07-28, including enabled Boost.Context SystemC fibers
and installed Tcl 9.0.4. The fetched-Tcl relocation companion also passed.
The batch ends at feature commit `f8717ce`; GitHub Actions state was not
queried for this local gate.

### Twenty-fifth feature batch — SystemVerilog time declarations and rounding

The planned ten implementation features are:

1. Parse compilation-unit `timeunit` and `timeprecision` declarations.
2. Parse module-local `timeunit` declarations and override inherited
   directive/compilation-unit state.
3. Parse module-local `timeprecision` plus combined
   `timeunit value / value` declarations.
4. Diagnose illegal magnitudes/units, duplicates, late declarations, and
   precision coarser than the effective unit.
5. Preserve fractional decimal delay literals exactly through typed HIR.
6. Support explicitly unit-suffixed SystemVerilog delay literals independent
   of the containing module time unit.
7. Apply SystemVerilog timeprecision rounding before conversion to global
   ticks, including deterministic half-step behavior.
8. Include declared precision and explicit delay units in `auto` global
   resolution selection.
9. Diagnose decimal overflow and values not representable after required
   precision normalization.
10. Require matching interpreter, LLVM O0/O2, callback/time, VCD, and
    cold/warm native-cache behavior for the new source forms.

All ten implementation features are complete. Focused frontend,
diagnostic-catalog, interpreter, LLVM O0/O2, timestamp-callback, normalized
VCD, coarse-resolution/overflow rejection, and cold/warm native-cache tests
pass. The exact LLVM 22.1.8 warnings-as-errors regression then passed all 26
tests in 200.32 seconds on 2026-07-28, including enabled Boost.Context SystemC
fibers and installed Tcl 9.0.4. The fetched-Tcl relocation companion also
passed. The batch ends at feature commit `3ad94ef`; GitHub Actions state was
not queried for this local gate.

### Twenty-sixth feature batch — min/typ/max delay selection

The planned ten implementation features are:

1. Preserve parenthesized Verilog/SystemVerilog
   `minimum:typical:maximum` delay triples in typed HIR.
2. Apply triples to standalone procedural delay controls.
3. Apply triples to supported blocking/NBA and continuous assignment delays.
4. Apply triples to supported built-in gate primitive delays.
5. Apply triples to delayed nonblocking named-event notifications.
6. Add schema-1 `[run].delay_mode = "min" | "typ" | "max"` with `typ` as
   the deterministic default.
7. Add a `--delay-mode` CLI override with targeted invalid-value diagnostics.
8. Select one branch before SystemVerilog precision rounding and global-tick
   normalization.
9. Support exact fractional/scientific and explicitly unit-suffixed values in
   every branch, with targeted malformed-triple diagnostics.
10. Require manifest/CLI, interpreter, LLVM O0/O2, timestamp, VCD, and
    cold/warm/selection-invalidated cache evidence.

All ten implementation features are complete. Focused frontend, project,
diagnostic-catalog, existing time/named-event, manifest/CLI, interpreter, LLVM
O0/O2, callback/timestamp, VCD, branch-specific automatic-resolution, and
cold/warm mode-distinct native-cache tests pass. The exact LLVM 22.1.8
warnings-as-errors regression then passed all 27 tests in 202.09 seconds on
2026-07-28, including enabled Boost.Context SystemC fibers and installed Tcl
9.0.4. The fetched-Tcl relocation companion also passed. The batch ends at
feature commit `efdc0a7`; GitHub Actions state was not queried for this local
gate.

### Twenty-seventh feature batch — transition-specific inertial delays

The planned ten implementation features are:

1. Preserve parenthesized one-, two-, and three-value
   Verilog/SystemVerilog delay lists in typed HIR, with every value retaining
   an independent optional `min:typ:max` triple.
2. Accept rise/fall and rise/fall/turnoff delays on continuous assignments.
3. Accept one or two rise/fall delays on the currently supported
   `buf`/`not`/logic gate primitives, while diagnosing illegal third values.
4. Apply the standard one-value all-transition default and derive an omitted
   turnoff delay as the minimum of the selected rise and fall delays.
5. Select every `min`/`typ`/`max` component before precision rounding and
   include every selected component in automatic global-resolution choice.
6. Add whole-signal and packed-slice transition-delay operations to SimIR
   without changing procedural delayed-NBA transport behavior.
7. Select scalar and packed-vector delays from the actual changed
   transitions, using the shortest applicable delay for mixed transitions
   and transitions to unknown.
8. Give delayed continuous assignments inertial behavior by cancelling
   superseded pending updates, including rejection of pulses shorter than the
   effective delay.
9. Append transition-write callbacks to the versioned plain-C JIT runtime
   table and include all transition delays in validation, native-object
   identity, LLVM O0, and LLVM O2 lowering.
10. Require targeted list diagnostics plus interpreter/LLVM O0/O2,
    scalar/vector/slice, timestamp, VCD, pulse-rejection, and cold/warm cache
    evidence.

All ten implementation features are complete. Focused frontend,
diagnostic-catalog, transition-selection/runtime cancellation, strict C ABI,
LLVM O0/O2 callback/cache-identity, prior time/delay-mode, and
interpreter/LLVM O0/O2 scalar/vector/slice/gate/VCD/pulse-rejection
application tests pass. Cancelled inertial transactions are removed from the
scheduler rather than merely ignored, so a rejected pulse cannot leave a
phantom future timestamp. The Release build also replaced an optimizer-only
GCC `maybe-uninitialized` warning around optional timeprecision state with
explicit validated integer control flow. The exact LLVM 22.1.8
warnings-as-errors Release regression then passed all 29 tests in 70.81
seconds on 2026-07-28, including enabled Boost.Context SystemC fibers,
fetched Tcl 9.0.4, and the fetched-Tcl relocation test. The batch ends at
feature commit `4813460`; GitHub Actions state was not queried for this local
gate.

### Twenty-eighth feature batch — VHDL projected output waveforms

The planned ten implementation features are:

1. Preserve a typed VHDL signal-assignment delay mechanism: implicit
   inertial, explicit `inertial`, explicit `transport`, and an optional
   `reject` time.
2. Parse the common delay mechanism on sequential, concurrent conditional,
   and selected VHDL signal assignments without affecting variable
   assignments.
3. Diagnose malformed mechanisms, a `reject` clause without `inertial`, and
   delay-mechanism keywords in unsupported positions with stable diagnostic
   codes.
4. Normalize rejection limits at the same exact project resolution as
   waveform delays and reject negative, unrepresentable, or greater-than-
   first-delay limits.
5. Add typed whole-signal and packed-slice projected-write operations to
   SimIR, retaining transport/inertial mode, waveform delay, and rejection
   limit.
6. Maintain a projected output waveform per process, signal, and scalar
   subelement, including cancelable scheduled transactions and exact packed
   reconstruction through update-phase slice coalescing.
7. Implement transport deletion/appending and the VHDL inertial
   mark-and-delete algorithm, with the default rejection limit equal to the
   first waveform delay.
8. Preserve a pending SystemVerilog continuous-assignment transaction when a
   sensitivity re-evaluation produces the same source value, while retaining
   cancellation for a changed source value.
9. Append projected-write callbacks to the versioned plain-C JIT runtime
   table and cover validation, serialization/cache identity, LLVM O0, and
   LLVM O2 lowering.
10. Require focused parser/diagnostic, scalar/vector/slice,
    sequential/concurrent/selected, pulse-boundary, transport, interpreter/
    LLVM O0/O2, VCD, and cold/warm-cache evidence before the full local
    regression gate.

All ten implementation features are complete. Focused frontend, runtime,
elaboration, diagnostic-catalog, strict C ABI, native API, LLVM O0/O2, VCD,
rejection-boundary, exact-time-normalization, and cold/warm native-cache tests
pass. VHDL whole and slice writes use cancelable per-scalar projected
transaction queues, while unchanged SystemVerilog continuous RHS evaluations
retain their original pending publication time. The append-only runtime table
is now 216 bytes and the native-object schema advances to v14. The exact LLVM
22.1.8 warnings-as-errors Release regression then passed all 30 tests in 53.63
seconds on 2026-07-28, including Boost.Context fibers, fetched Tcl 9.0.4, and
the fetched-Tcl relocation test. The batch ends at feature commit `9317089`;
no CI state was inspected for this local gate.

### Twenty-ninth feature batch — ordered VHDL waveforms

The planned ten implementation features are:

1. Preserve ordered VHDL waveform elements in typed HIR, with an individual
   value and optional delay for every element plus an explicit `unaffected`
   alternative marker.
2. Parse multiple waveform elements on simple sequential and concurrent
   signal assignments while leaving variable-assignment expressions
   unchanged.
3. Represent conditional signal assignments as ordered conditional control
   flow whose alternatives independently retain waveform lists or
   `unaffected`.
4. Retain waveform lists or `unaffected` independently on every selected
   signal-assignment alternative while sharing the common delay mechanism.
5. Emit stable targeted diagnostics for malformed/empty waveforms,
   `unaffected` mixed with waveform elements, and `null` waveform elements
   until guarded-signal driver disconnection is supported.
6. Normalize every element delay at the elaborated project resolution,
   require nonnegative exactly representable strictly ascending times, and
   require an explicit rejection limit not to exceed the first delay.
7. Add atomic whole-signal and packed-slice projected-waveform operations to
   SimIR, with validation of ordered source/delay pairs and target widths.
8. Apply a complete new waveform atomically to every scalar projected queue,
   marking all new transactions before transport/inertial editing and
   preserving overflow, cancellation, and packed reconstruction behavior.
9. Append plain-C waveform-array callbacks to the runtime table and cover
   serialization/cache identity, LLVM O0 and LLVM O2 lowering, advancing the
   native-object schema without changing existing callback offsets.
10. Require focused parser/diagnostic, conditional/selected/`unaffected`,
    scalar/vector/slice, transport/inertial, ordering, interpreter/LLVM
    O0/O2, VCD, and cold/warm-cache evidence before the full local regression
    and push gate.

All ten implementation features are complete. Focused frontend,
diagnostic-catalog, strict C ABI, runtime, LLVM O0/O2/cache-identity, and
interpreter/LLVM O0/O2/VCD/cold-warm source tests pass. Conditional signal
assignments lower through ordinary branch control flow while retaining their
distinct Boolean-legality diagnostic; each chosen leaf owns its independent
waveform or `unaffected` state. Atomic waveform operations mark every new
transaction before editing per-process/signal/scalar projected queues. The
append-only runtime table is now 232 bytes and the native-object schema
advances to v15. After correcting one stale elaboration negative-test
assumption exposed by the first run, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 30 tests in 72.63 seconds on 2026-07-28,
including Boost.Context fibers, fetched Tcl 9.0.4, and the fetched-Tcl
relocation test. The batch ends at feature commit `81191ab`; no CI state was
inspected for this local gate.

### Thirtieth feature batch — procedural assignment timing and NBA ordering

The planned ten implementation features are:

1. Retain a typed procedural assignment timing-control kind in HIR,
   distinguishing no control, delay control, and event control from
   continuous-assignment and VHDL delay metadata.
2. Parse blocking and nonblocking intra-assignment `#delay` controls,
   preserving min/typ/max selection and existing exact time normalization.
3. Parse blocking and nonblocking intra-assignment `@event` controls with
   any-change, scalar edge, comma/`or` lists, and wildcard RHS dependency
   inference.
4. Emit stable diagnostics for missing, repeated, incompatible, or currently
   unsupported repeated-event assignment controls, and reject timing controls
   where `always_comb`/`always_latch` or `final` forbids suspension.
5. Lower blocking delay controls by evaluating and retaining the RHS before a
   resumable `WaitFor`, then performing the whole, slice, or local write after
   suspension.
6. Lower delayed nonblocking assignments by capturing the RHS immediately and
   scheduling whole or slice update-phase publication without suspending the
   issuing process.
7. Lower event-controlled blocking and nonblocking assignments as a
   debugger-visible dynamic wait followed by RHS evaluation and the selected
   blocking or update-phase write.
8. Verify deterministic source/stable-process ordering and last-assignment
   behavior for same-slot, cross-process, overlapping whole/slice, `#0`, and
   equal-deadline future NBAs.
9. Cover the composed wait/write state machines in LLVM O0/O2, resumable
   frames, native-object identity, and the existing append-only C callbacks
   without changing the runtime ABI.
10. Require focused parser/diagnostic, SimIR scheduling, elaboration, and
    interpreter/LLVM O0/O2/VCD/cold-warm source evidence before the full local
    regression and push gate.

All ten implementation features are complete. Focused frontend, unique
diagnostic-catalog, elaboration, runtime-ordering, and
interpreter/LLVM-O0/LLVM-O2/VCD/cold-warm-cache tests pass. Blocking delay
controls retain RHS registers across a debugger-visible resumable wait;
event-controlled forms wait before reading their RHS; delayed NBAs use the
existing transport update callbacks without an ABI change. Direct runtime
coverage proves stable cross-process ordering independently of HDL
multi-driver legality, while the source differential uses legal single-driver
same-slot/equal-deadline cases.

After feature commit `dfc5014`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 31 tests in 72.31 seconds on 2026-07-28. The
gate includes fetched Boost.Context 1.91.0, fetched Tcl 9.0.4 and relocation,
SystemC plug-in/fiber coverage, LLVM/C ABI tests, interpreter/LLVM
differentials, VCD, cache, debugger-facing application tests, and the new
procedural-assignment target. No CI state was inspected for this local gate.

### Thirty-first feature batch — resolved driver slots

The planned ten implementation features are:

1. Add an explicit unresolved, SystemVerilog-wire, or VHDL-standard-logic
   resolution policy to common signal metadata and elaborated DesignIR.
2. Select resolution from explicit mixed-language bindings and the supported
   native `wire`/`tri` and `std_logic`/`std_logic_vector` type forms, while
   retaining targeted rejection for unresolved variables and unsupported
   wired-net policies.
3. Register dense process-owned driver slots before simulation starts, with
   policy-correct undriven initialization and stable process identity.
4. Route whole and constant-slice blocking writes through the issuing
   process's slot, resolve immediately, and publish only effective changes.
5. Coalesce whole/slice NBA updates independently per driver, apply every slot
   in the common update phase, and resolve each affected signal exactly once.
6. Preserve driver identity through `#0`/future transport, inertial
   continuous, and VHDL projected scalar/slice waveform callbacks.
7. Keep debugger deposit/force/release semantics coherent: forces mask
   resolution, driver updates continue underneath, and release exposes the
   latest resolved value.
8. Expose resolution metadata and each process driver's current value through
   the native C hierarchy without changing generated-code callback signatures.
9. Verify interpreter and LLVM O0/O2 equivalence without a runtime ABI
   extension, including cache identity from existing binding/source
   provenance.
10. Require focused runtime, elaboration, mixed VHDL/SV, C API,
    interpreter/LLVM O0/O2/VCD/cold-warm-cache evidence before the full local
    regression and push gate.

All ten implementation features are complete in feature commit `c2920d0`.
Focused strict-build runtime, elaboration, mixed VHDL/SV
interpreter/LLVM-O0/LLVM-O2/VCD/cold-warm-cache, unique diagnostic-catalog,
and native C API tests pass. Process-owned slots now cover whole and slice
blocking/update/future/inertial/projected paths without extending the generated
runtime ABI. The native C hierarchy exposes resolved-signal metadata and
pre-resolution driver values; force continues to mask effective values while
driver slots advance underneath it.

After feature commit `c2920d0`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 32 tests in 71.87 seconds on 2026-07-28. The
gate includes fetched Boost.Context 1.91.0, fetched Tcl 9.0.4 and relocation,
SystemC plug-in/fiber coverage, LLVM/C ABI tests, interpreter/LLVM
differentials, VCD, cache, debugger-facing application tests, and the new
mixed-resolution target. No CI state was inspected for this local gate.

### Thirty-second feature batch — SystemC fixed-width datatypes

The planned ten implementation features are:

1. Add standard four-state `sc_logic` NOT/AND/OR/XOR behavior with `Z`
   participating as unknown and compound assignment support.
2. Add integral construction, checked mutable bit references, low-word
   conversion, and binary rendering for arbitrary-width `sc_bv<W>`.
3. Add width-preserving `sc_bv<W>` bitwise, logical-shift, compound, and
   reduction operations.
4. Add checked mutable bits plus four-state bitwise, zero-fill shift,
   compound, and reduction behavior for `sc_lv<W>`.
5. Add checked mutable bit references, explicit raw-bit conversion, binary
   rendering, and vector construction for `sc_uint<W>`.
6. Add width-wrapping unsigned arithmetic, division/modulo rejection,
   compounds, and prefix/postfix increment/decrement for `sc_uint<W>`.
7. Add width-wrapping unsigned bitwise, shift, compound, and reduction
   operations for `sc_uint<W>`.
8. Add checked mutable bit references, raw-bit conversion, binary rendering,
   and vector construction for `sc_int<W>`.
9. Add portable width-wrapping signed arithmetic, division/modulo edge
   handling, bitwise, arithmetic/logical shifts, compounds, and increments for
   `sc_int<W>`.
10. Require focused standalone façade plus real compiled SystemC
    interpreter/LLVM-hybrid signal tests for known and unknown vector paths
    before the full local regression and push gate.

All ten implementation features are complete in feature commit `73cfb83`.
The standalone facade now provides bounded four-state/vector behavior and
portable wrapping 1–64-bit signed/unsigned operations without allowing a C++
datatype object to cross the plug-in ABI. Focused strict-build header,
plug-in-loader, plug-in-compiler, and real compiled SystemC
interpreter/LLVM-hybrid O0/O2/VCD/cold-warm-cache tests pass. The support
contract and executable feature matrix explicitly defer concatenation/range
proxies, mixed-width result typing, arbitrary-precision integers, and the full
Accellera overload set.

After feature commit `73cfb83`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 33 tests in 85.26 seconds on 2026-07-28. The
gate includes fetched Boost.Context 1.91.0, fetched Tcl 9.0.4 and relocation,
SystemC plug-in/fiber/datatype coverage, LLVM/C ABI tests,
interpreter/LLVM differentials, VCD, cache, debugger-facing application tests,
and all preceding feature batches. No CI state was inspected for this local
gate.

### Thirty-third feature batch — VHDL integer counts and dynamic shifts

The planned ten implementation features are:

1. Give the bounded VHDL base `integer` subtype a portable 32-bit signed,
   two-state runtime representation for ports, signals, and process variables.
2. Preserve integer signedness and deterministic base-subtype initialization
   through DesignIR, debug locals, signal reads/writes, and VCD visibility.
3. Permit width- and signedness-compatible integer aliases through ordinary
   hierarchy and explicit mixed-language boundaries while retaining the
   no-lossy-conversion rule for four-state sources.
4. Execute in-range base-integer unary, arithmetic, `abs`, `mod`/`rem`, and
   relational expressions through the existing signed common operations,
   while continuing to document overflow checking as incomplete.
5. Require dynamic VHDL shift/rotate counts to have the base `integer` type
   and retain targeted diagnostics for packed-vector or unsupported subtype
   counts.
6. Execute dynamic `sll` and `srl`, including negative-count direction
   reversal and oversized zero-fill behavior.
7. Execute dynamic `sla` and `sra`, including negative-count direction
   reversal and the standard rightmost/leftmost element fill rules.
8. Execute dynamic `rol` and `ror`, including negative-count direction
   reversal and modulo-width normalization.
9. Carry signed-count semantics explicitly in versioned SimIR cache identity,
   the arbitrary-width interpreter, and allocation-free LLVM O0/O2 lowering
   without changing the plain-C generated runtime ABI.
10. Require focused parser, diagnostic, elaboration, runtime-kernel, native
    cache, and VHDL interpreter/LLVM O0/O2/VCD evidence before the full local
    regression and push gate.

All ten implementation features are complete in feature commit `84e7ce5`.
The bounded VHDL base `integer` subtype now executes as a signed 32-bit
two-state port, signal, or process variable with left-bound default
initialization, compatible hierarchy aliasing, and in-range signed expression
behavior. Dynamic base-integer counts reach all six packed VHDL shift/rotate
operators with negative reversal in both the arbitrary-width interpreter and
LLVM O0/O2. Focused parser, unsupported-subtype/non-integer-count diagnostic,
mixed VHDL/SystemVerilog boundary, wide runtime, LLVM truth-table,
signed-count cache-identity, and interpreter/LLVM/VCD/cold-warm application
tests pass. Checked integer overflow, explicit scalar ranges, and runtime
`natural`/`positive` enforcement remain explicitly documented gaps.

After feature commit `84e7ce5`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 34 tests in 86.87 seconds on 2026-07-28. The
gate includes fetched Boost.Context 1.91.0, fetched Tcl 9.0.4 and relocation,
SystemC plug-in/fiber/datatype coverage, LLVM/C ABI tests,
interpreter/LLVM differentials, VCD, cache, debugger-facing application tests,
the new VHDL integer-shift target, and all preceding feature batches. No CI
state was inspected for this local gate.

### Thirty-fourth feature batch — checked VHDL integer subtypes

The planned ten implementation features are:

1. Retain independent scalar constraints for the bounded base `integer`,
   `natural`, and `positive` subtypes without treating value bounds as packed
   vector widths.
2. Parse explicit `integer range ... to|downto ...` subtype indications and
   preserve specialization-dependent bound expressions and source spans.
3. Evaluate scalar constraints per generic-specialized unit, rejecting
   out-of-representation, null, or otherwise invalid bounds with stable
   diagnostics.
4. Default-initialize integer-family signals and process variables from the
   concrete subtype's left bound, including descending constraints.
5. Add typed SimIR operations for checked unary negate/absolute and binary
   add, subtract, multiply, power, divide, remainder, and modulo.
6. Diagnose unknown operands, signed 32-bit overflow, invalid exponents, and
   division by zero identically in the interpreter and generated code.
7. Insert scalar range checks before integer-family local, signal, waveform,
   and mixed-boundary stores while preserving ordinary 32-bit transport.
8. Carry integer operations and concrete constraints through SimIR
   validation, native-object identity, and LLVM O0/O2 lowering without
   changing the plain-C runtime ABI layout.
9. Expose concrete integer constraints through DesignIR/debug metadata and
   retain width-, signedness-, and range-safe hierarchy aliases.
10. Require focused parser, diagnostic, elaboration, interpreter, LLVM
    O0/O2/cache, and VHDL application differential evidence before the full
    local regression and push gate.

All ten implementation features are complete in feature commit `1d1e034`.
The VHDL integer family now retains concrete scalar constraints independently
from packed ranges, specializes generic-dependent bounds, initializes from the
left bound, and checks integer arithmetic and stores consistently in the
interpreter and LLVM O0/O2 generated code. Focused parser, diagnostic,
elaboration, runtime, LLVM/cache, and VHDL application differential tests
pass. The first full regression exposed language-local type information being
lost when a VHDL integer port shared a boundary signal with an SV packed
vector. Corrective commit `1444e4b` preserves each instance's visible port type
on both VHDL-to-SV and SV-to-VHDL paths while retaining the common boundary
signal.

After feature commits `1d1e034` and `1444e4b`, the exact LLVM 22.1.8
warnings-as-errors Release regression passed all 34 tests in 86.48 seconds on
2026-07-28. The gate includes fetched Boost.Context 1.91.0, fetched Tcl 9.0.4
and relocation, SystemC plug-in/fiber/datatype coverage, LLVM/C ABI tests,
interpreter/LLVM differentials, VCD, cache, debugger-facing application tests,
the checked VHDL integer subtype and mixed-direction binding coverage, and all
preceding feature batches. No CI state was inspected for this local gate.

### Thirty-fifth feature batch — exact VHDL nine-state transport

The planned ten implementation features are:

1. Extend the common packed runtime value with an explicit four-state or
   nine-state representation while retaining the allocation-free existing
   low-word path for two-/four-state generated code.
2. Preserve all `std_ulogic` states `U`, `X`, `0`, `1`, `Z`, `W`, `L`, `H`,
   and `-` in scalar/vector literals, rendering, equality, and default
   initialization.
3. Retain signal and register value domains in SimIR so exact nine-state
   values cannot accidentally enter the existing aval/bval LLVM subset.
4. Preserve nine-state values through reads, copies, extracts, inserts,
   concatenations, whole/slice stores, waits, and signal attributes.
5. Execute IEEE `std_logic_1164` elementwise `not`, `and`, `or`, `xor`,
   `nand`, `nor`, `xnor`, equality, and inequality behavior in the reference
   interpreter.
6. Preserve nine-state transactions through blocking/update/future/inertial
   and projected writes, process-owned driver slots, deposits, forces, and
   release.
7. Resolve scalar and packed `std_logic` driver collections with the complete
   nine-state resolution table and publish only exact effective changes.
8. Insert explicit ordinal element conversions at VHDL/SV boundaries so the
   shared signal store retains the owning domain without changing either
   language's internal view.
9. Expose canonical nine-state signal/local/driver values through debugger
   inspection, native C reads, callbacks, and the required VCD collapse rules.
10. Make LLVM capability and cache behavior reject exact nine-state processes
    without lossy compilation, then require focused interpreter/hybrid/VCD
    evidence before the full local regression and push gate.

Completed in feature commit `7360d77` and regression-compatibility commit
`aac0f7f`. The common runtime now retains exact VHDL Logic9 values through
elaboration, typed SimIR, structural and logical operations, projected
transactions, process-owned resolution, mixed-language conversion, debug/C
API reads, and VCD mapping. LLVM compilation explicitly rejects processes
that would expose exact values through the current aval/bval ABI, while
independent four-state and integer-only processes remain eligible within the
same design. The LLVM 22.1.8 Release/Werror regression passed 35/35 tests in
82.35 seconds after updating older collapsed-`X` and compiled-process cache
expectations. No CI state was inspected for this local gate.

### Thirty-sixth feature batch — direct LLVM Logic9 execution

The planned ten implementation features are:

1. Append four-plane Logic9 words and projected-waveform elements to the
   versioned plain-C JIT ABI without changing existing field offsets.
2. Append caller-owned third/fourth register-plane storage to resumable
   process frames and validate it only for processes that require Logic9.
3. Carry register and signal value kinds into lowering and native-object
   cache identity so mixed four-/nine-state modules remain deterministic.
4. Lower exact Logic9 constants, signal reads, last-value reads, copies,
   extracts, inserts, concatenations, shifts, and rotations directly.
5. Lower complete IEEE `std_logic_1164` elementwise NOT/AND/OR/XOR tables and
   exact equality over bit-sliced ordinal planes.
6. Convert explicitly between ordinal Logic9 planes and aval/bval Logic4
   planes at mixed-domain register and signal boundaries without lossy
   implicit access.
7. Add direct Logic9 blocking, update, delayed, inertial, projected, partial,
   and atomic waveform callbacks to the kernel-owned scheduler.
8. Preserve exact compiled locals through debugger reads/writes and retain
   O0 safe-point frame visibility.
9. Exercise exact O0/O2 generated execution, native cache hits, mixed
   ownership, resolution, projected transactions, VCD, and interpreter/JIT
   equivalence in focused tests.
10. Restore compiled-process expectations for newly supported VHDL processes,
    update VH-015 documentation, then run and push the full local regression
    gate.

All ten implementation features are complete in feature commit `403ffc2`.
The append-only v1 JIT ABI now carries exact Logic9 values through
pointer-based four-plane words, waveform elements, and caller-owned resumable
frame planes without changing any existing field offset. LLVM O0/O2 directly
executes exact constants, reads, structural operations, IEEE logic/equality,
mixed-domain conversions, formatted output, locals, and every supported
whole/slice scheduling form. Native-object identity includes only the signal
value kinds referenced by each process, retaining reuse after unrelated
signal-kind changes. Focused differential testing also corrected vacated-bit
encoding in exact logical shifts and preserved the Logic9 result domain for
VHDL shifts, preventing spurious delta-zero `U`-to-`X` changes.

After feature commit `403ffc2`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 35 tests in 89.16 seconds on 2026-07-28. The
gate includes strict C ABI layout checks, direct LLVM O0/O2 Logic9 and cache
tests, exact locals, resolution, shifts, projected waveforms, mixed-language
boundaries, VCD, fetched Boost.Context 1.91.0, fetched Tcl 9.0.4, SystemC, and
all preceding feature batches. No CI state was inspected for this local gate.

### Thirty-seventh feature batch — bounded VHDL record execution

The planned ten implementation features are:

1. Parse architecture-local VHDL `type NAME is record ... end record`
   declarations with retained declaration/member source spans.
2. Support comma-grouped, case-insensitive record element names with stable
   diagnostics for empty records, duplicates, malformed terminators, and
   mismatched optional end names.
3. Admit bounded scalar and statically ranged packed logic/bit/Boolean record
   elements, compute deterministic declaration-order flattened layouts, and
   preserve the strongest element value domain.
4. Resolve local named record types after parsing, with targeted unknown-type,
   duplicate-type, unsupported nested-record, and cyclic-alias diagnostics.
5. Execute record-typed architecture signals and process variables with exact
   VHDL default initialization and persistent debugger-visible local frames.
6. Parse and execute case-insensitive selected record-element reads and
   signal/variable writes.
7. Compose constant bit and slice selection after a record-element selection
   for both reads and writes, retaining declared element direction.
8. Execute width/type-compatible whole-record copies plus exact equality and
   inequality through typed common SimIR without permitting record values
   across mixed-language boundaries.
9. Preserve Logic9 record elements through interpreter and LLVM O0/O2
   execution, native-object caching, committed-change VCD, and debug-local
   reads.
10. Add focused positive, negative, elaboration, interpreter/LLVM/cache/VCD
    evidence, update V1-VH-04 documentation, then run and push the full local
    regression gate.

All ten implementation features are complete in feature commit `1621736`.
The VHDL frontend now accepts case-insensitive architecture-local, non-nested
records containing scalar or statically ranged logic, bit, and Boolean
elements. Elaboration resolves the local named type, computes a deterministic
declaration-order packed layout, distinguishes selected record objects from
package constants, preserves each element's default domain, and composes
constant member bit/slice selections with whole-record copies and equality.
The same typed SimIR executes with exact Logic9 values in the interpreter and
LLVM O0/O2, while debugger local reads, committed VCD changes, and cold/warm
native-cache behavior remain identical. Records are explicitly retained
behind same-language wrappers at mixed-language boundaries.

After feature commit `1621736`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 36 tests in 90.13 seconds on 2026-07-28. The
gate includes the new focused record parser, diagnostic, elaboration,
interpreter/LLVM O0/O2, exact-default, debug-local, VCD, and cache coverage;
fetched Boost.Context 1.91.0 and Tcl 9.0.4; SystemC; the strict native ABI; and
all preceding feature batches. No CI state was inspected for this local gate.

### Thirty-eighth feature batch — package-visible VHDL records

The planned ten implementation features are:

1. Parse bounded record type declarations in VHDL package declarations while
   preserving package/type/member source spans and case-insensitive names.
2. Retain unresolved VHDL subtype indications as semantic named-type
   references so entity and architecture declarations can name project types.
3. Import package record types through `use library.package.all` and
   `use library.package.type_name`, with stable missing-item and conflicting
   direct-visibility diagnostics.
4. Resolve direct `package.type_name` and `library.package.type_name`
   selections without requiring a use clause.
5. Specialize package record layouts before importing them and retain package
   plus transitive context/package source provenance in each consumer.
6. Resolve entity-port record types independently from architecture-local
   declarations, then merge the typed interface into the architecture
   specialization.
7. Connect same-language VHDL record ports by whole-object alias through
   recursive hierarchy while preserving member layout, range, and Logic9
   metadata.
8. Keep package record values behind same-language wrappers at VHDL/SV and
   VHDL/SystemC boundaries with the existing targeted aggregate diagnostic.
9. Execute package-record defaults, whole copies/equality, and selected
   member/index/slice operations identically in the interpreter and LLVM
   O0/O2, including debugger locals and VCD.
10. Add focused parser/visibility/hierarchy/provenance/cache/runtime evidence,
    update V1-VH-01/V1-VH-04/V1-VH-05 documentation, then run and push the
    full local regression gate.

All ten implementation features are complete in feature commit `eab3a53`.
Bounded record declarations now live in either architectures or project
packages. Entity and architecture declarations retain semantic named-type
references, resolve selected/all use visibility plus direct two-/three-part
package type names, and diagnose unknown, missing, or ambiguously visible
types. Entity interfaces resolve outside the architecture-local type region
before being merged into each specialization. Same-language VHDL hierarchy
aliases exact record ports, while foreign boundaries continue to require a
scalar/vector wrapper. Package and transitive source dependencies participate
in every consuming specialization and invalidate each corresponding native
object after a package-only edit.

After feature commit `eab3a53`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 37 tests in 88.29 seconds on 2026-07-29. The
gate includes package-record parser and visibility diagnostics, recursive
record-port hierarchy, source provenance, package-edit cache invalidation,
interpreter/LLVM O0/O2 exact Logic9 execution, debugger-local reads, VCD,
fetched Boost.Context 1.91.0 and Tcl 9.0.4, SystemC, the strict native ABI,
and all preceding feature batches. No CI state was inspected for this local
gate.

### Thirty-ninth feature batch — VHDL record aggregate literals

The planned ten implementation features are:

1. Add an explicit aggregate expression kind to typed HIR with source-ordered
   association values and retained positional/named/`others` choices.
2. Parse parenthesized VHDL positional and named record aggregates without
   changing ordinary parenthesized expressions or call/index ambiguity.
3. Canonicalize named element choices and diagnose malformed association
   order, duplicate `others`, and nonfinal `others` with stable codes.
4. Contextually type record aggregates from whole signal/local assignment and
   process-variable initializer targets.
5. Map positional associations by declaration order and named associations by
   case-insensitive element name, independent of source association order.
6. Expand a final `others` association over every unassigned member and reject
   duplicate, unknown, missing, or multiply assigned elements.
7. Enforce each member's exact width and 2-/4-/9-state assignment domain while
   composing the flattened result through typed SimIR inserts.
8. Contextually type aggregate operands of whole-record equality/inequality
   and propagate record context through conditional assignment alternatives.
9. Preserve aggregate-built Logic9 values through interpreter and LLVM O0/O2,
   debugger locals, committed VCD changes, and cold/warm native caching.
10. Add focused positive/negative/parser/elaboration/runtime evidence, update
    V1-VH-04/V1-VH-05 documentation, then run and push the full local
    regression gate.

All ten implementation features are complete in feature commit `90a67df`.
Typed HIR now distinguishes aggregates from grouping and retains a
source-ordered choice alongside every operand. The VHDL parser accepts
positional, case-insensitive named, and final `others` associations with
targeted recovery diagnostics. Elaboration obtains the bounded record type
from variable initializers, whole signal/local assignments, record
equality/inequality, and conditional alternatives, maps associations onto the
declaration-order flattened layout, and composes an exact typed value through
SimIR `Insert` operations. Missing, duplicate, excessive, unknown,
wrong-width, and lossy two-state associations are rejected. The focused
application differential proves exact Logic9 values, persistent debugger
locals, VCD mapping, LLVM O0/O2 behavior, cold/warm native cache reuse, and
transitive package-edit invalidation against the interpreter.

After feature commit `90a67df`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 38 tests in 88.09 seconds on 2026-07-29. The
gate includes record-aggregate parser recovery and typed diagnostics,
flattened SimIR composition, aggregate comparisons and conditional
alternatives, interpreter/LLVM O0/O2 exact Logic9 execution, debugger-local
reads, VCD, package-edit cache invalidation, fetched Boost.Context 1.91.0 and
Tcl 9.0.4, SystemC, the strict native ABI, and every preceding feature batch.
No CI state was inspected for this local gate.

### Fortieth feature batch — bounded VHDL subtype declarations

The planned ten implementation features are:

1. Represent VHDL subtype declarations explicitly in typed HIR while reusing
   the common named-type graph and retaining source spans and base subtype
   indications.
2. Parse case-insensitive `subtype NAME is SUBTYPE_INDICATION;` declarations
   in package, entity, and architecture declarative regions with stable
   malformed/duplicate diagnostics.
3. Support scalar `bit`/`std_logic`/Boolean, packed vector, signed/unsigned,
   and portable integer-family bases, including constrained built-in bases.
4. Resolve chained local subtype references deterministically, diagnose
   unknown/cyclic bases, and preserve concrete packed direction, signedness,
   element domain, record layout, and integer constraints.
5. Apply a derived integer range only within its resolved base subtype and
   reject null, out-of-base, noninteger, and portable-width violations before
   executable lowering.
6. Apply packed index constraints only to unconstrained packed-array bases,
   reject constraints on scalar/record/already constrained bases, and retain
   specialization-dependent bounds until generic folding.
7. Export/import package subtypes through selected/all use clauses and direct
   package/library selected names with transitive source provenance and exact
   cache invalidation.
8. Resolve package subtypes in an entity's generic/port header independently,
   then expose later entity-declarative subtypes to the associated
   architecture without retroactively changing the interface region.
9. Execute subtype-typed generics, ports, signals, variables, record aliases,
   defaults, assignments, range checks, hierarchy aliases, debugger locals,
   VCD, and interpreter/LLVM O0/O2 paths identically.
10. Add focused positive/negative/parser/elaboration/runtime evidence, update
    V1-VH-01/V1-VH-02/V1-VH-04/V1-VH-05 documentation, then run, record, and
    push the full local regression gate.

All ten implementation features are complete in feature commit `2df6808`.
Typed HIR distinguishes VHDL subtype declarations from records and
SystemVerilog typedefs while retaining the unresolved base indication and
derived constraint. The parser admits package, entity, and architecture
subtypes plus subtype-typed package constants and scalar generics. Elaboration
resolves local, imported, and direct selected subtype chains; exposes
independently resolved entity subtypes to the associated architecture; folds
generic-dependent packed bounds per specialization; and proves derived
integer containment and packed reconstraint legality before lowering.
Scalar, signed/unsigned vector, portable integer, and record aliases reach
the existing typed runtime paths without losing range, direction, domain,
signedness, layout, or debug metadata. The focused three-specialization
application proves two differently sized instances, range-safe integer ports,
record aggregates, interpreter/LLVM O0/O2 equality, debugger locals, VCD,
cold/warm native reuse, and transitive package-edit invalidation.

After feature commit `2df6808`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 39 tests in 89.36 seconds on 2026-07-29. The
gate includes subtype parser recovery, declaration/scoping/constraint
diagnostics, package constants and scalar generics, entity-header versus
entity-declarative visibility, two differently sized child specializations,
range-safe integer hierarchy aliases, scalar/vector/record execution,
interpreter/LLVM O0/O2 exact values, debugger-local reads, VCD,
package-edit cache invalidation, fetched Boost.Context 1.91.0 and Tcl 9.0.4,
SystemC, the strict native ABI, and every preceding feature batch. No CI state
was inspected for this local gate.

### Forty-first feature batch — VHDL user-defined enumeration types

The planned ten implementation features are:

1. Represent VHDL enumeration declarations explicitly in typed HIR with
   nominal type identity, declaration-ordered literals, ordinals, and source
   spans.
2. Parse case-insensitive identifier and character enumeration literals in
   package, entity, and architecture declarative regions.
3. Diagnose empty, malformed, duplicate-literal, duplicate-type, and
   unterminated enumeration declarations with stable diagnostic codes.
4. Store enumeration values as the minimum-width two-state ordinal, default
   objects to the first literal, and retain literal names for visibility.
5. Resolve local, package-visible, and directly selected enumeration types
   plus contextual overloaded literals without injecting untyped constants.
6. Execute enumeration-typed constants, generics, ports, signals, variables,
   initializers, assignments, and conditional alternatives while rejecting
   literals without a usable type context.
7. Execute equality, inequality, ordinal relational comparisons, and case or
   selected choices with exact nominal type checking.
8. Require exact nominal identity at same-language boundaries and retain
   enumeration values behind scalar/vector wrappers at mixed-language
   boundaries.
9. Preserve hierarchy specialization, interpreter/LLVM O0/O2 equivalence,
   debugger locals, VCD ordinals, cold/warm native caching, and package-edit
   invalidation.
10. Add focused positive, negative, frontend, elaboration, and application
    evidence; update the VHDL feature matrix; then run, record, and push the
    full local regression gate.

All ten implementation features are complete in feature commit `e4f21ed`.
Typed HIR now distinguishes nominal VHDL enumeration declarations and retains
identifier/character literal spelling, source spans, declaration-order
ordinals, and minimum-width two-state storage. Package, entity, architecture,
subtype, constant, generic, port, signal, and process-local paths preserve the
nominal type through specialization and hierarchy. Contextual literals execute
in initializers, assignments, conditional alternatives, exact/ordinal
comparisons, and case choices without becoming untyped package constants.
Same-language aliases require identical nominal identity, raw enumerations are
rejected at foreign boundaries, and debugger signal/local views display the
literal name alongside its packed ordinal. The focused application proves
interpreter/LLVM O0/O2 equality, VCD ordinals, cold/warm native reuse, and
package-edit invalidation.

After feature commit `e4f21ed`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 40 tests in 90.11 seconds on 2026-07-29. The
gate includes enumeration parser recovery and typed diagnostics, package
constants and generics, selected literals, nominal hierarchy and mixed-wrapper
checks, assignments, conditional alternatives, equality/ordinal comparisons,
case choices, debugger literal rendering, interpreter/LLVM O0/O2 equality,
VCD, package-edit cache invalidation, fetched Boost.Context 1.91.0 and Tcl
9.0.4, SystemC, the strict native ABI, and every preceding feature batch. No
CI state was inspected for this local gate.

### Forty-second feature batch — VHDL enumeration scalar attributes

The planned ten implementation features are:

1. Retain a resolved VHDL type-mark catalog independently from executable
   object names so local, imported, and subtype enumeration prefixes remain
   available during lowering.
2. Parse enumeration `left`, `right`, `low`, `high`, `length`, `ascending`,
   `pos`, `val`, `succ`, `pred`, `leftof`, and `rightof` attribute forms with
   retained prefix/argument spans.
3. Enforce type-mark versus object-prefix rules and exact zero-/one-argument
   arity with stable targeted diagnostics.
4. Execute declaration-range `left`/`right`/`low`/`high`, `length`, and
   `ascending` with exact enumeration, integer, or Boolean result types.
5. Execute `pos` for literals, signals, locals, constants, and generics by
   widening the checked nominal ordinal to the portable integer result.
6. Execute `val` for static and dynamic integer arguments with range checks
   before narrowing to the nominal enumeration result.
7. Execute `succ`/`pred` and direction-aware `leftof`/`rightof` for static and
   dynamic enumeration arguments without allowing ordinal wraparound.
8. Fold enumeration attributes in package constants and generic defaults,
   diagnose static out-of-range arguments before executable lowering, and
   retain runtime failures at the originating attribute source point.
9. Preserve interpreter/LLVM O0/O2 equivalence, hierarchy specialization,
   debugger literal rendering, VCD ordinals, cold/warm cache reuse, and
   package-edit invalidation.
10. Add focused positive, negative, elaboration, and application evidence;
    update the VHDL feature matrix; then run, record, and push the full local
    regression gate.

All ten implementation features are complete in feature commit `f8625c4`.
Typed lowering now retains enumeration type marks independently from object
names across local declarations, imported package visibility, entity regions,
and nominal subtypes. The parser preserves all twelve scalar attribute forms
and diagnoses missing arguments; elaboration enforces prefix, arity, nominal
argument/result, and integer-family rules with separate legality and range
diagnostics. Declaration bounds, length, direction, position/value conversion,
successor/predecessor, and left/right adjacency execute through checked typed
SimIR without ordinal wraparound. Package constants, generic defaults, and
generic actuals fold the same operations while retaining enumeration identity
inside contextual comparisons and conditional values.

The focused application exercises imported and subtype type marks, package
constant and generic folding, every result kind, debugger-visible literals,
normalized VCD ordinals, interpreter and LLVM O0/O2 success equivalence,
interpreter/JIT dynamic successor failure equivalence, cold/warm object-cache
reuse, and package-edit invalidation. Negative evidence covers missing and
extra arguments, object prefixes, integer-versus-enumeration argument typing,
nominal result mismatch, invalid operators, and static/executable range
failures.

After feature commit `f8625c4`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 40 tests in 91.74 seconds on 2026-07-29. The
gate includes enumeration-attribute parser recovery, static and dynamic
typed diagnostics, package/generic folding, interpreter/LLVM O0/O2 values and
failure behavior, debugger/VCD/cache evidence, fetched Boost.Context 1.91.0
and Tcl 9.0.4, SystemC, the strict native ABI, and every preceding feature
batch. No CI state was inspected for this local gate.

### Forty-third feature batch — constrained VHDL enumeration subtypes

The planned ten implementation features are:

1. Represent a resolved VHDL enumeration subtype constraint independently
   from its nominal base type, retaining left/right ordinals, direction,
   locally static bound expressions, base constraint, and source spans.
2. Parse `subtype S is T range L to R` and `downto` forms for local,
   entity, package-visible, and directly selected enumeration base types
   without treating enumeration literals as integers.
3. Resolve chained and imported constrained enumeration subtypes, including
   bounds supplied by literals, enumeration constants, and prior generics,
   while preserving the base declaration's storage width and literal table.
4. Diagnose unknown or wrong-nominal bounds, null executable ranges,
   out-of-base derived constraints, invalid scalar bases, and illegal values
   with stable parser/elaboration diagnostic codes.
5. Default constrained enumeration signals and locals to the subtype's left
   bound and enforce membership for constants, generic defaults, and generic
   actuals before specialization proceeds.
6. Enforce static and dynamic subtype membership before every whole local,
   signal, delayed, inertial, transport, and multi-element waveform store,
   reusing the checked portable runtime path without ordinal wraparound.
7. Validate same-language hierarchy aliases directionally for input, output,
   and inout ports so every possible driver value is admitted by its reader's
   constrained enumeration subtype.
8. Make `left`, `right`, `low`, `high`, `length`, `ascending`, `val`, `pos`,
   `succ`, `pred`, `leftof`, and `rightof` honor the constrained subtype;
   keep base-type `pos`/`val` ordinals while making adjacency direction-aware.
9. Preserve interpreter/LLVM O0/O2 success and range-failure equivalence,
   debugger literal rendering, VCD ordinals, hierarchy specialization,
   cold/warm native reuse, and package-edit invalidation.
10. Add focused positive, negative, frontend, elaboration, and application
    evidence; update the VHDL feature matrix; then run, record, and push the
    full local regression gate.

All ten implementation features are complete in feature commit `d7bc6fa`.
The typed HIR now retains unresolved and resolved enumeration constraints,
base-subtype constraints, source spans, ordinal bounds, and direction without
changing the base enumeration's width, literal table, or nominal identity.
Elaboration resolves local, imported, chained, direct-object, constant-bound,
and prior-generic-bound constraints; rejects unknown, wrong-nominal, null, and
out-of-base ranges; and applies subtype-left initialization plus checked
constant, generic, local, signal, delayed, projected, and ordered-waveform
stores.

All enumeration scalar attributes now use the resolved subtype bounds and
direction while preserving base-declaration ordinals for `pos` and `val`.
Same-language hierarchy checks prove range containment in the data-flow
direction for input and output ports and exact range equality for inout ports.
The application differential covers interpreter and LLVM O0/O2 successful
execution, interpreter/JIT range failures, debugger literal rendering,
normalized VCD ordinals, cold/warm object reuse, hierarchy specialization, and
package-edit invalidation. Focused negative evidence covers every new
diagnostic and unsafe input, output, and inout boundary case.

After feature commit `d7bc6fa`, the exact LLVM 22.1.8 warnings-as-errors
Release regression passed all 40 tests in 93.65 seconds on 2026-07-29. The
gate includes constrained-enumeration parser recovery, specialization and
typed range diagnostics, direction-aware attributes, checked stores,
interpreter/LLVM O0/O2 success and failure behavior, debugger/VCD/cache
evidence, fetched Boost.Context 1.91.0 and Tcl 9.0.4, SystemC, the strict
native ABI, and every preceding feature batch. No CI state was inspected for
this local gate.

### Forty-fourth feature batch — VHDL user-defined array types

The planned ten implementation features are:

1. Represent a VHDL array declaration independently from an anonymous packed
   vector, retaining nominal identity, index subtype/range form, element
   subtype, direction, constraint state, and source spans in typed HIR.
2. Parse constrained and unconstrained one-dimensional array declarations in
   entity, architecture, and package declarative regions, including
   `integer`, `natural`, and `positive` index subtypes and explicit locally
   static `to`/`downto` ranges.
3. Resolve built-in and visible named scalar element subtypes plus local,
   selected, imported, and chained array type/subtype references without
   losing declaration identity or element state domain.
4. Apply array constraints from subtype declarations and object subtype
   indications, fold package constants and prior generics per specialization,
   and reject illegal reconstraints or constraints outside a constrained
   base.
5. Diagnose malformed dimensions, unsupported multidimensional or composite
   elements, invalid index bases, zero-width executable ranges, unknown
   element types, range overflow, and nominally incompatible values with
   stable parser/elaboration codes.
6. Derive packed storage width and `Bit2`/Boolean/`Logic9` defaults from the
   scalar element subtype while retaining declared array bounds for ordinal
   mapping and debug metadata.
7. Execute whole-array values, equality, constant indexing and slicing, whole
   and selected signal/local writes, and conditional alternatives with
   same-nominal-array type checking in interpreter and LLVM O0/O2 modes.
8. Validate same-language array ports by nominal identity, width, direction,
   and constraint compatibility; require scalar/vector wrappers at
   cross-language boundaries rather than silently erasing VHDL array
   identity.
9. Preserve array identity and declared bounds through specialization,
   debugger/VCD rendering, cold/warm native-object reuse, and package-edit
   cache invalidation.
10. Add focused positive, negative, frontend, elaboration, and application
    evidence; update the VHDL feature matrix; then run, record, and push the
    full local regression gate.

All ten implementation features are complete in feature commit `e0f6c48`.
Typed HIR now distinguishes nominal VHDL arrays from anonymous packed vectors
while retaining index and element subtype metadata, source spans, direction,
bounds, and constraint state. The parser accepts bounded one-dimensional
constrained and `integer`/`natural`/`positive range <>` declarations in
package, entity, and architecture regions and emits targeted diagnostics for
malformed or unsupported declarations.

Elaboration resolves built-in, local, imported, and selected scalar element
subtypes, chained array subtypes, package-constant and prior-generic bounds,
and specialization-dependent object constraints. It enforces index-base and
nominal legality, rejects nonconcrete packed objects, retains transitive
package dependencies, validates same-language hierarchy by nominal identity,
and requires explicit scalar/vector wrappers at mixed-language boundaries.
Whole-array copy and equality, conditional alternatives, constant index and
slice reads, and whole or selected signal/local writes share the existing
packed interpreter and LLVM lowering while preserving declared ordinal
mapping.

The application differential covers interpreter and LLVM O0/O2 execution,
ascending and descending constraints, package constants and generics,
same-language hierarchy, Boolean and nine-state defaults, persistent debug
locals, debugger display, normalized VCD, cold/warm native-object reuse, and
package-edit invalidation. Focused frontend and elaboration evidence covers
selected package element subtypes plus every new declaration, constraint,
nominal-operator, and boundary diagnostic.

The Node-runtime CI detour preceding this batch finished in commit `c6d4071`;
run `30444076195` passed all twelve Linux/Windows jobs and every job reported
zero annotations.

The feature state recorded in commit `e0f6c48` passed the exact LLVM 22.1.8
warnings-as-errors Release regression: all 41 tests completed successfully in
52.48 seconds on 2026-07-29. The gate includes the new array parser,
visibility, constraint, nominal-legality, hierarchy, interpreter/LLVM O0/O2,
debugger, VCD, and cache evidence, fetched Boost.Context 1.91.0 and Tcl 9.0.4,
SystemC, the strict native ABI, and every preceding feature batch. No CI state
was inspected for this local gate.

### Forty-fifth feature batch — contextual VHDL array aggregates

The planned ten implementation features are:

1. Extend expression HIR so every VHDL aggregate association retains its
   ordered choice expressions and source spans independently from its value,
   while preserving existing record-element metadata.
2. Parse positional, discrete-index, locally static `to`/`downto` range,
   `|` choice-list, and final `others` associations in nested aggregate
   expressions without guessing the contextual array or record type.
3. Preserve identifiers inside aggregate choices through qualified package
   discovery, generate-scope qualification, package/generic substitution,
   and specialization.
4. Contextually type aggregates from a concrete nominal one-dimensional
   scalar-element array target and lower every element using the declared
   Boolean, bit, std_logic, or std_ulogic domain.
5. Map positional and named choices by declared ordinal for both ascending
   and descending constraints, including nonzero and negative index bounds.
6. Diagnose a missing contextual array type, malformed choice metadata,
   nonstatic or out-of-range choices, duplicate coverage, missing elements,
   illegal record-style choice lists, width mismatches, and implicit lossy
   four-state-to-two-state element conversion with stable codes.
7. Execute array aggregates in signal and local initializers, whole signal
   and variable assignments, conditional alternatives, and nominal equality
   expressions through the common SimIR path.
8. Fold package constants and prior generics used as choices per hierarchy
   specialization, retain same-language nominal port behavior, and propagate
   transitive package dependencies into native-cache identity.
9. Require interpreter and LLVM O0/O2 equivalence for aggregate results,
   persistent debug locals, debugger rendering, normalized VCD, cold/warm
   object reuse, and package-edit invalidation.
10. Add focused positive, negative, frontend, elaboration, and application
    evidence; update the VHDL feature matrix and support documentation; then
    run, record, and push the full local regression gate.

All ten implementation features are complete in feature commit `9c9a932`.
Expression HIR now retains an ordered AST for every aggregate choice alongside
the association value and the existing record-element marker. The parser
accepts positional, discrete-index, ascending/descending range, `|`
choice-list, and final `others` associations, preserves source spans, recovers
with targeted diagnostics, and leaves record-versus-array interpretation to
contextual semantic analysis.

Qualified-name discovery, generate qualification, enumeration folding, and
package/generic substitution traverse choice expressions. Elaboration
constructs concrete scalar-element arrays by declared ordinal for ascending,
descending, negative, and nonzero index ranges; folds visible and selected
package constants plus prior generics; enforces scalar width and state-domain
safety; and diagnoses inconsistent metadata, nonstatic or out-of-range
choices, duplicate coverage, missing indices, illegal record choice lists,
and unsupported aggregate contexts.

The application differential covers positional and named aggregate
initializers, local and signal assignments, range and choice-list
associations, conditional alternatives, nominal equality, Boolean and
nine-state elements, ascending and descending mapping, selected package
constants, hierarchy generics, persistent debug locals, debugger display,
normalized VCD, interpreter/LLVM O0/O2 equivalence, cold/warm native reuse,
and package-edit invalidation. Focused frontend and elaboration evidence
covers every new parser and semantic diagnostic, including deliberately
malformed HIR robustness cases.

The feature state recorded in commit `9c9a932` passed the exact LLVM 22.1.8
warnings-as-errors Release regression: all 41 tests completed successfully in
51.58 seconds on 2026-07-29. The gate includes the new aggregate parser,
choice-preservation, specialization, legality, ordinal-mapping,
interpreter/LLVM O0/O2, debugger, VCD, and cache evidence, fetched
Boost.Context 1.91.0 and Tcl 9.0.4, SystemC, the strict native ABI, and every
preceding feature batch. No CI state was inspected for this local gate.

### Forty-sixth feature batch — VHDL array type marks and range attributes

The planned ten implementation features are:

1. Parse `range` and `reverse_range` in the bounded VHDL attribute grammar,
   retaining the prefix, optional dimension, source span, and distinct
   direction request in expression HIR.
2. Accept a `range` or `reverse_range` attribute as the complete discrete
   range of a sequential `for` loop without requiring a following explicit
   `to` or `downto`.
3. Resolve attribute prefixes against concrete array signals, locals,
   package-visible type marks, constrained subtype marks, and directly
   selected package subtype marks without confusing object and type
   namespaces.
4. Lower `left`, `right`, `low`, `high`, `length`, and `ascending` for
   concrete user-array type/subtype marks as locally static scalar results,
   matching the existing bounded object-attribute behavior.
5. Unroll `for I in A'range` from the declared left bound to right bound in
   its declared direction for ascending, descending, nonzero, and negative
   index ranges.
6. Unroll `for I in A'reverse_range` over the same bounds in the opposite
   direction, retaining loop-index constant substitution, selected writes,
   `next`, and `exit` behavior.
7. Accept the optional constant dimension `1` on scalar and range attributes;
   reject missing, dynamic, zero, negative, or multidimensional arguments
   with stable diagnostics.
8. Fold scalar array attributes used as locally static array indices, slice
   bounds, aggregate choices, and ordinary integer/Boolean expressions.
9. Diagnose scalar use of `range`/`reverse_range`, nonarray or unknown
   prefixes, unconstrained type marks, unsupported dimensions, and
   unrepresentable results without silently guessing a range.
10. Add focused frontend, elaboration, loop, aggregate, interpreter/LLVM
    O0/O2, debugger, VCD, specialization, and cache evidence; update the
    feature matrix and support documentation; then run, record, and push the
    full local regression gate.

The batch is implemented. The handwritten VHDL parser now preserves
`range`/`reverse_range` attributes as discrete loop ranges, including an
optional dimension expression. Elaboration resolves bounded array objects,
local and package-visible type/subtype marks, and directly selected package
marks; folds scalar bounds attributes in expressions, indices, slices, and
aggregate choices; and unrolls both declared and reversed ranges with
constant loop-index substitution plus working `next`/`exit` control.

Targeted diagnostics cover nonarray, unknown, unconstrained, scalar-range,
and unsupported-dimension uses. The loop-control evidence also exposed and
fixed a pre-existing width-context bug: substituted VHDL integer literals in
Boolean conditions now retain the runtime's 32-bit integer width rather than
being narrowed to one bit. Focused frontend, diagnostic-catalog, elaboration,
interpreter/LLVM O0/O2, debugger, VCD, specialization, and cold/warm-cache
tests pass.

The feature state recorded in commit `b9e4b03` passed the exact LLVM 22.1.8
warnings-as-errors Release regression: all 41 tests completed successfully in
50.02 seconds on 2026-07-29. The gate includes the new array type/subtype
attribute resolution, declared/reversed loop ordering, integer loop-control
width correction, interpreter/LLVM O0/O2, debugger, VCD, specialization, and
cache evidence, fetched Boost.Context 1.91.0 and Tcl 9.0.4, SystemC, the
strict native ABI, and every preceding feature batch. No CI state was
inspected for this local gate.

### Forty-seventh feature batch — dynamic packed indexing

The planned ten implementation features are:

1. Add a typed SimIR dynamic-index descriptor retaining a signed 32-bit index
   register and the elaborated left/right bounds and direction of a
   one-dimensional packed object.
2. Execute dynamic extraction in the reference interpreter with declared
   ascending, descending, negative, and nonzero range normalization.
3. Execute dynamic insertion into packed process-local values without
   changing unselected elements or their exact Logic4/Logic9 state.
4. Lower nonconstant VHDL array/vector and Verilog/SystemVerilog packed-object
   index reads while preserving constant-index lowering and contextual
   element domains.
5. Lower dynamic blocking writes to process locals and signals through the
   same normalized index mapping.
6. Lower dynamic VHDL signal updates and SystemVerilog NBA writes through
   process-owned selected driver slots so update ordering and multi-driver
   resolution remain unchanged.
7. Extend delayed, inertial, projected-waveform, and transition-delay
   selected writes to capture and use the normalized runtime element offset.
8. Diagnose non-integer index expressions statically and fail unknown or
   out-of-range runtime indices deterministically with the process and source
   safe point preserved in both engines.
9. Validate, serialize, hash, and lower every dynamic-index SimIR form in
   LLVM O0/O2 for Logic4 and Logic9 values without widening the public native
   callback ABI unnecessarily.
10. Add focused frontend, elaboration, runtime, LLVM/cache, interpreter/JIT,
    VCD, debugger, range-direction, failure-equivalence, and mixed-language
    evidence; update the feature matrix and support documentation; then run,
    record, and push the full local regression gate.

The batch is implemented. SimIR now carries a typed dynamic-index descriptor
with a signed 32-bit index register, declared left/right bounds, direction,
and normalized base offset. The interpreter performs checked dynamic
extraction and insertion for Logic4 and Logic9 values and captures the
resolved element offset when blocking, common-update/NBA, delayed, inertial,
projected, or projected-waveform writes execute.

VHDL `integer` indices and SystemVerilog packed 32-bit indices lower through
the common path while constant selections retain their existing compact
operations. Static legality rejects noninteger or incorrectly represented
indices and invalid concrete ranges; unknown and out-of-range runtime values
fail deterministically with the process instruction preserved. The
application adapter maps generated LLVM failures to the same interpreter
diagnostics.

LLVM O0/O2 validates every register, signal, range, delay, waveform, and
Logic4/Logic9 kind; serializes every dynamic field into native-object identity;
and lowers variable shifts plus existing static-slice callbacks without
changing the public callback ABI. Focused runtime, LLVM, elaboration, and
VHDL application tests pass. They cover ascending, descending, negative, and
nonzero ranges; exact Logic9 states; all six dynamic write forms; VHDL and
SystemVerilog source lowering; debugger/VCD visibility; cold/warm cache reuse;
and interpreter/compiled runtime-failure equivalence.

The feature implementation is recorded in commit `f052569`; the exact
Release build additionally exposed a GCC `-O3 -Wmaybe-uninitialized` false
positive around a default-constructed optional selected offset. Portability
commit `3bdf73b` replaces that representation with an explicitly initialized
offset and validity flag, retaining the same selection semantics.

The resulting state passed the exact LLVM 22.1.8 warnings-as-errors Release
regression: all 41 tests completed successfully in 51.72 seconds on
2026-07-29. The gate includes dynamic-index SimIR validation and serialization,
interpreter and LLVM O0/O2 Logic4/Logic9 execution, VHDL/SystemVerilog source
lowering, debugger/VCD/cache behavior, runtime failure equivalence, fetched
Boost.Context 1.91.0 and Tcl 9.0.4, SystemC, the strict native ABI, and every
preceding feature batch. No CI state was inspected for this local gate.

## Source-file size refactor program

The repository now treats 2,000 lines as a hard maximum for every authored
C/C++ source, header, and test file. New and extracted files should normally
remain near or below 1,600 lines so ordinary feature growth does not
immediately consume the hard limit. Generated, fetched, build-tree, and
third-party sources are outside this policy.

Splitting implementation text into included fragments does not satisfy this
program. Private implementation headers may contain declarations, state
layouts, and genuinely unavoidable templates only. Ordinary executable
functions and method bodies must move into independently compiled,
responsibility-oriented translation units so the refactor reduces both file
size and per-unit compilation scope.

The initial inventory found fourteen oversized files containing approximately
87,000 lines. The refactor is behavior-preserving: public C++, C, SystemC
plug-in, and JIT callback ABIs remain stable; LLVM types remain private to the
adapter; installed umbrella headers keep their existing include paths; and
test assertions, cache sequencing, interpreter/JIT differentials, labels, and
goldens are retained.

The planned checkpoints are:

1. Add a cross-platform CMake/CTest source-line guard with a temporary explicit
   burn-down allowlist, then decompose the oversized test infrastructure.
2. Split the VHDL and Verilog/SystemVerilog parsers by declarations,
   expressions, statements, design units, and language-specific system forms.
3. Split SimIR execution, SystemC hierarchy/plug-in compilation, the native C
   API implementation, and Tcl command implementation by responsibility.
4. Split application analysis/build/run/debug/simulation services and the LLVM
   validation/cache/frame/IR-lowering/LLJIT adapter layers.
5. Split constant evaluation, specialization, name resolution, generate
   expansion, executable lowering, binding, and hierarchy construction out of
   the monolithic elaborator.
6. Remove the temporary allowlist, require every authored source to pass the
   hard limit, run the full portability regression, and retain the line-budget
   test as a permanent release gate.

Each checkpoint uses focused builds and tests during extraction, followed by
one exact LLVM 22.1.8 warnings-as-errors Release regression. Successful
regression checkpoints are recorded, committed, and pushed. GitHub CI state is
not inspected unless explicitly requested.

Checkpoint 1 has established the permanent `fsim.source-line-budget` CTest.
It scans tracked authored C/C++ sources and headers on every platform, rejects
new files above 2,000 lines, rejects stale allowlist entries as soon as an
oversized file is split, and currently checks 127 files. The explicit
allowlist has fallen from fourteen files to ten.

The runtime, frontend, LLVM, and elaboration monolithic tests have been
replaced by 29 responsibility-oriented translation units plus small runners
and private support headers. The largest extracted test unit is 1,643 lines.
The existing four CTest entry points, assertion order, cache behavior, LLVM
optimization matrix, and diagnostic fixtures remain unchanged. The
application integration monolith retains a 2,200-line shared fixture setup and
will be decomposed with the application implementation checkpoint so that the
fixture becomes a proper reusable test component rather than duplicated
source-writing code. The five focused checkpoint tests pass.

The checkpoint implementation is recorded in commit `fc48bbc`. Its exact
LLVM 22.1.8 warnings-as-errors Release build passed all 42 tests in 51.23
seconds on 2026-07-29. The added test is the permanent source-line-budget gate;
the prior 41 functional tests remain green. The regression includes the split
frontend, elaboration, LLVM, and runtime translation units, fetched
Boost.Context 1.91.0 and Tcl 9.0.4, SystemC, mixed-language execution, cache,
debugger, VCD, and strict native ABI coverage. No CI state was inspected.

Checkpoint 2 decomposes both handwritten HDL parsers without changing the
installed parser interface. Private parser headers now contain parser state,
nested working types, and method declarations only. VHDL parsing is compiled
as core/design-unit, declaration, concurrent/generate, sequential-statement,
and expression units. Verilog/SystemVerilog parsing is compiled as
directive/core, design-unit/generate, declaration/instance, procedural/system
task, and expression/delay units.

The two former 3,325- and 5,271-line implementations have become twelve
responsibility-oriented implementation files plus two private headers and two
small public-entry facades. The largest parser unit is 1,378 lines. Both parser
entries have been removed from the temporary allowlist, which has fallen from
ten entries to eight, and the permanent budget gate now checks 139 authored
sources.

Focused frontend, diagnostic-catalog, and source-budget tests pass. The exact
LLVM 22.1.8 warnings-as-errors Release build passed all 42 tests in 95.85
seconds on 2026-07-29, including the complete application integration,
interpreter/JIT, SystemC, mixed-language, Tcl, C API, debugger, cache, and VCD
coverage. No CI state was inspected.

Checkpoint 3 applies the stricter translation-unit rule to SimIR, the SystemC
runtime/compiler bridge, and the native C API. SimIR is now compiled as
format/output, logic/vector, arithmetic/integer, interpreter state, scheduling,
operation execution, public interpreter, and public value/error units. SystemC
hierarchy registration callbacks are separate from lifecycle and registry
methods; plug-in compilation is separated into planning/facade, source and
cache support, dependency discovery, and platform process execution. Native C
entry points are separate from session, hierarchy-object, callback, and
runtime-failure services.

The four new private headers contain declarations and state layouts only; they
contain no ordinary function or method bodies. The largest new implementation
unit is 1,615 lines. SimIR, SystemC hierarchy, and the plug-in compiler have
been removed from the temporary allowlist, which now contains five remaining
files, and the permanent budget gate checks 155 authored sources.

Focused runtime, LLVM differential, SystemC plug-in/compiler/cache, C/C++ API,
and line-budget tests pass. The exact LLVM 22.1.8 warnings-as-errors Release
build passed all 42 tests in 95.03 seconds on 2026-07-29, including complete
application integration, mixed-language execution, interpreter/JIT
equivalence, Tcl, debugger, VCD, cache, and strict ABI coverage. No CI state
was inspected.

Checkpoint 4 is in progress. The application implementation is now compiled
as executor/callback, analysis, check, build, run, simulation, debugger,
debug-control, time/value, CLI-service, and public-facade translation units.
Its private header contains declarations and state layouts only; it has no
ordinary executable function bodies. The former 5,953-line implementation is
now headed by a 1,713-line executor unit.

The 7,212-line application integration test has also been replaced by a small
runner, three compiled source-fixture units, a compiled source-update helper,
compiled capture support, and five responsibility-oriented test groups. Its
shared header contains fixture/result declarations only. Source text embedded
in raw HDL literals is preserved byte-for-byte so diagnostic source locations
remain stable. The largest application test unit is 1,200 lines.

Both application files have been removed from the temporary allowlist, leaving
only the installed SystemC header, LLVM adapter, and elaborator. The permanent
budget gate now checks 178 authored sources. The exact Debug application
integration test passes in 169.07 seconds after the split, and the line-budget
test passes. The LLVM adapter portion and the exact Release regression remain
to complete this checkpoint.

LLVM checkpoint work has separated the persistent object-cache/LLJIT facade,
process validation, native cache-key construction, module optimization and
verification, and packed-value IR kernels into independently compiled units.
Two private LLVM headers expose only state structures and function
declarations; they contain no ordinary executable bodies. Process emission is
further separated into logic/arithmetic, value selection/indexing, signal
transaction, output/control, and packed-value kernel units. The largest LLVM
unit is the 1,996-line process coordinator. LLVM has been removed from the
temporary allowlist, leaving only the elaborator and installed SystemC header,
and the permanent budget gate now checks 189 authored sources. Focused
warnings-as-errors LLVM compilation and the `fsim.llvm` differential/cache
test pass. The exact LLVM 22.1.8 warnings-as-errors Release build passed all
42 tests in 95.25 seconds on 2026-07-29. This closes the application/LLVM
checkpoint with the interpreter/JIT differentials, SystemC, Tcl 9.0.4, native
API, cache, debugger, VCD, and mixed-language coverage green. No CI state was
inspected.

Checkpoint 5 decomposes the elaborator into independently compiled constant
evaluation, semantic analysis/generate expansion, specialization, target
selection, package visibility, hierarchy/type construction, SystemC hierarchy,
process lowering, assignment/control lowering, expression-family lowering, and
type-inference units. The private elaborator header contains state layouts,
method/function declarations, and only the visitor templates whose concrete
call-site types require definitions in the header.

The former 14,173-line implementation is gone. Its largest replacement is the
1,779-line constant-evaluation unit; process lowering is 1,433 lines and
hierarchy/type construction is 1,432 lines. The former 2,403-line expression
method is a small ordered dispatcher over primary/selection, unary/attribute,
system-function, and binary implementations, preserving the original
diagnostic and side-effect order. Focused warnings-as-errors compilation,
elaboration tests, and the permanent source-budget test pass.

Checkpoint 6 completes the refactor program. The installed `<systemc>`
umbrella is now four lines and routes to responsibility-oriented core,
channel, datatype, marshalling, and plug-in headers. Concrete event, wait,
module lifecycle, primitive-channel, hierarchy-scope, and HDL-child behavior
is compiled in `fsim_systemc_support`; only width/type-generic channel,
datatype, marshalling, and factory behavior remains in installed headers.
Direct CMake consumers link the support archive transitively, generated
plug-ins link it explicitly, its contents participate in the plug-in cache
key, and the configured installed archive is used when the build-tree archive
is unavailable.

The temporary source-budget allowlist is empty. All 214 authored C/C++ source,
header, and test files satisfy the 2,000-line hard limit; the repository
maximum is the 1,998-line LLVM process coordinator, and the largest unavoidable
SystemC template header is 1,507 lines. Focused SystemC header, C ABI, plug-in,
plug-in compiler/cache, datatype application, elaboration, and line-budget
tests pass. The final exact LLVM 22.1.8 warnings-as-errors Release build passed
all 42 tests in 96.58 seconds on 2026-07-29, including the full application
integration, interpreter/JIT differential, mixed-language, SystemC, Tcl 9.0.4,
native API, debugger, VCD, and cache coverage. No CI state was inspected.

### LLVM-disabled portability repair

The application source decomposition initially left the
`LlvmProcessExecutor` implementation and LLVM optimization adapter visible in
an LLVM-disabled compilation even though their declarations and dependencies
were correctly guarded. `application_executors.cpp` now applies the same
`FSIM_HAS_LLVM` boundary to those definitions while continuing to compile the
common SystemC executor and application helpers in both configurations.

The warnings-as-errors GCC Debug build with `FSIM_LLVM_MODE=OFF` completes and
all 41 non-LLVM tests pass in 170.92 seconds. The exact LLVM 22.1.8 Release
application target, compiled-expression test, and source-line-budget test also
pass. No CI state was inspected.

### Windows SystemC CRT linker repair

The exact-LLVM Windows configurations intentionally build fsim and the
prebuilt-compatible support libraries with the static MSVC CRT. Generated
SystemC plug-ins previously hardcoded `/MD`, so linking them with
`fsim_systemc_support.lib` failed with `LNK2038`, `LNK4098`, and `LNK1319`.

The plug-in compiler now derives `/MT`, `/MTd`, `/MD`, or `/MDd` from the CRT
macros used to compile `fsim_systemc`, applies that option exactly once to
every MSVC/clang-cl compile and link command, and includes it in plug-in cache
identity. Raw manifest CRT overrides are rejected because they cannot safely
differ from the required support archive. Command-plan tests cover all four
override spellings and verify consistent generated commands. Focused
LLVM-disabled and exact LLVM 22.1.8 Release SystemC compiler and datatype
application tests pass locally. The supplied Windows test log was analyzed;
no CI state was queried directly.

### Forty-eighth feature batch — SystemVerilog declared parameter sizing

The architecture-gate batch was split at the typed-HIR boundary so the
SystemVerilog value-parameter work could land as one coherent, tested change
without embedding VHDL interface-type semantics in the shared parser model.
Its ten completed features are:

1. Retain the intrinsic 8-, 16-, and 64-bit widths and default signedness of
   `byte`, `shortint`, and `longint` value parameters.
2. Retain the unsigned four-state 64-bit representation of `time` parameters
   within the current signed-64-bit constant envelope.
3. Apply explicit `signed` and `unsigned` modifiers to supported integral
   parameter types.
4. Preserve packed `bit`/`logic`/`reg` ranges and `int`/`integer` width,
   domain, and signedness metadata.
5. Truncate and sign-extend explicitly typed default values at the declared
   parameter boundary.
6. Apply the same declared-type conversion to named and positional override
   values before specialization identity is finalized.
7. Fold prior-parameter-dependent packed ranges before converting dependent
   parameters and localparams.
8. Normalize declaration-ordered localparams and generated parameters while
   preserving original packed-enum fit and duplicate-value legality checks.
9. Carry normalized values into per-instance specialization and native-cache
   identity.
10. Add targeted unsupported type/string diagnostics plus frontend,
    elaboration, interpreter, LLVM O0/O2, and cold/warm-cache evidence.

The handwritten frontend now distinguishes the supported integral atom types
from packed vector types instead of accepting their keywords as unsupported
placeholders. Elaboration performs one declared-type conversion after either
default or override evaluation, using the specialized packed width and
signedness. Enum enumerator constants deliberately bypass this conversion so
an out-of-range declaration cannot be truncated into an apparently legal
value before the existing enum diagnostics run.

The source application fixture instantiates default and overridden typed
parameter specializations, observes exact 32-bit results, and requires
interpreter, LLVM O0, and LLVM O2 agreement across cold and warm cache runs.
Focused frontend and elaboration tests cover intrinsic metadata, explicit
signedness, dependent packed ranges, dependent localparams, normalized
specialization values, enum regression behavior, and targeted rejection of
the still-unsupported type and string parameter forms.

This batch does not claim complete SystemVerilog expression sizing. Full
unsigned-64 constant values, self-determined expression width propagation,
type parameters, and string parameters remain release-gate work. VHDL
interface type generics are intentionally carried into batch 49, where they
can be implemented with an explicit semantic type environment and canonical
type specialization identity.

The implementation is recorded in feature commit `18078f8`. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 42 configured tests in
185.68 seconds, and the exact Release regression passed all 43 configured
tests in 51.73 seconds on 2026-07-29. The gates include intrinsic and packed
parameter typing, default/override/localparam conversion, dependent widths,
enum-legality preservation, interpreter/LLVM O0/O2 equivalence, native-cache
reuse, the permanent source-size check, SystemC, Tcl 9.0.4, mixed-language
execution, debugger, VCD, and the native APIs. No CI state was inspected.

### Forty-ninth feature batch — VHDL-2008 interface type generics

The completed ten-feature slice is:

1. Represent VHDL value and interface type generics as distinct HIR formal
   kinds while preserving declaration order and source spans.
2. Parse the VHDL-2008 unclassified `type T` form and diagnose VHDL-2019
   classified or invalid default-like forms outside the VHDL-2008 grammar.
3. Match named and positional type-mark actuals in the same case-insensitive
   association sequence as existing value generics.
4. Resolve builtin, parent-local type/subtype, and direct package/library
   selected type marks at the association site.
5. Preserve unresolved interface type references through independent entity
   and architecture analysis, then replace them per specialization without
   mutating shared frontend declarations.
6. Specialize dependent ports and internal signals to supported scalar,
   packed-vector, record, enumeration, and one-dimensional array actuals,
   including nested type-formal pass-through.
7. Apply a generic-dependent packed constraint to its actual base type before
   folding later defaulted or overridden value-generic bounds.
8. Diagnose missing, non-type, invisible, wrong-base, namespace-conflicting,
   and non-VHDL-boundary actuals with stable codes.
9. Serialize nominal, range, enumeration, record-member, and array metadata
   into a versioned canonical type identity used by specialization and native
   cache keys.
10. Add frontend, elaboration, source-metadata, interpreter, LLVM O0/O2,
    cold/warm reuse, and changed-type selective-invalidation evidence.

The frontend does not infer a type actual merely because a generic-map
expression is an identifier. It first retains the association designator, and
elaboration interprets it as a type mark only after matching it to a formal
whose HIR kind is `Type`. A formal-aware named-type environment allows entity
ports and architecture declarations to remain unresolved during analysis.
Each occurrence then inserts a concrete specialization-local alias and reruns
the ordinary type resolver before value-generic substitution.

The supported actual matrix covers builtin `bit`, constrained vector
subtypes, bounded records, enumerations, scalar-element arrays, direct package
selected records, nested forwarding, and an unconstrained `bit_vector` actual
made concrete by a value-generic formal range. Required, value-expression,
invisible-name, wrong-base, duplicate/order, namespace, and cross-language
failures are targeted. Type actuals remain same-language VHDL semantics;
explicit wrappers are still required at mixed-language boundaries.

The application differential compiles three differently typed occurrences
from a separate child source, verifies retained child declaration provenance,
and compares exact final values through the interpreter and LLVM O0/O2. Its
file-scoped cache fixture changes one vector actual from four to eight bits:
only that specialization misses and recompiles while the record and scalar
specializations reuse their native objects.

Classified interface types, generic package/subprogram type formals,
type-formal-dependent record/array element declarations, and unconstrained
objects without a concrete formal constraint remain release-gate work. They
are not hidden behind parser placeholders.

The implementation is recorded in feature commit `2185f34`. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 43 configured tests in
183.94 seconds; after the final wrong-base negative fixture was added, its
rebuilt five-test focused gate also passed. The exact Release regression,
built from the final source state, passed all 44 configured tests in 52.28
seconds. An LLVM-disabled focused gate passed all five affected tests, and the
permanent source-size test passed again after the three new translation/test
units became tracked. No CI state was inspected.

### Fiftieth feature batch — SystemVerilog constant-expression sizing

The completed ten-feature architecture-gate slice is:

1. Introduce a typed constant value carrying exact bits, X/Z masks, width,
   signedness, unsized status, and source span independently from checked
   legacy integer consumers.
2. Preserve sized, unsized decimal, unsized based, and unbased unsized literal
   sizing and four-state metadata through constant-expression analysis.
3. Implement explicit self-determined sizing for the bounded unary,
   reduction, concatenation, replication, cast, and shift forms.
4. Implement context propagation for bounded arithmetic, bitwise, logical,
   relational, equality, case-equality, power, and conditional expressions
   without using host-C++ promotion rules.
5. Apply the typed evaluator to defaults, localparams, named and positional
   overrides, non-iterative generated parameters, packed ranges, and
   conditional/case generate choices.
6. Represent and display the complete unsigned 64-bit `longint` and `time`
   range without signed overflow or implementation-defined conversion.
7. Preserve X/Z bits for four-state declarations and reject lossy conversion
   into two-state atom or packed declarations.
8. Preserve enum fit/duplicate legality and stable cycle, division,
   remainder, shift, replication, and integer-consumer diagnostics.
9. Serialize typed constants into a versioned `svconst-v1` identity that
   retains semantic distinctions and drives selective native-cache
   invalidation.
10. Add atomic elaboration/generate/boundary tests plus
    interpreter/LLVM O0/O2, cold/warm, and edited-source cache differentials.

The new evaluator and generate-choice coordinator are independent compilation
units. Their internal header contains declarations and semantic data only;
executable implementation did not migrate into headers. Typed values are
converted back to exact source expressions only at existing substitution
boundaries, while consumers that require ordinary integers use explicit
checked conversion. This keeps the LLVM/runtime C ABI unchanged.

Assignment and expression lowering now make the bounded SystemVerilog width
conversion explicit with existing SimIR extract, concatenate, and
load-constant operations. Unsized integer literals infer 32 bits; mixed-width
binary operands and assignment RHS values are sign- or zero-extended and
truncated according to retained signedness. The application fixture observes
the exact maximum unsigned-64 value, mixed widths, and unknown-state values in
the interpreter and LLVM O0/O2. Its file-scoped cache fixture edits only the
top unsigned parameter and requires two affected specialization misses while
two unrelated children reuse their native objects.

The bounded path covers integral widths from 1 through 64. Widths above 64,
complete SystemVerilog LRM expression typing, type parameters, string
parameters, and genvar-dependent typed constants inside iterative generate
bodies remain release-gate work. The existing signed-64 iterative-generate
path remains supported and tested.

The implementation is recorded in feature commit `cb2da43`.
The exact LLVM 22.1.8 warnings-as-errors Debug build completed cleanly and all
43 configured tests passed in 294.59 seconds. The exact Release build
completed cleanly and all 44 configured tests passed in 111.55 seconds.
Focused LLVM Debug, LLVM Release, and LLVM-disabled gates passed all four
affected tests. The application differential demonstrated exact cold/warm
reuse and two-hit/two-miss selective invalidation. No CI state was inspected.

### Fifty-first feature batch — SystemVerilog type parameters

The completed ten-feature architecture-gate slice is:

1. Represent value and type parameters as distinct HIR formal kinds while
   retaining declaration order, locality, source spans, and separately typed
   defaults.
2. Parse bounded `parameter type` and `localparam type` declarations in module
   parameter-port lists plus module and package bodies.
3. Retain unambiguous builtin type actuals separately while leaving identifier
   and scoped actuals tentative until formal-aware elaboration.
4. Resolve bounded integral builtins, local typedefs, wildcard imports, and
   directly package-selected typedef/type-parameter marks at the association
   site.
5. Preserve unresolved formal type references through independent frontend
   analysis, then replace them with specialization-local aliases without
   mutating shared declarations.
6. Specialize dependent packed ports, signals, typedefs, value parameters,
   localparams, and value-dependent default ranges.
7. Forward type formals through nested same-language hierarchy with checked
   named/positional matching and retained declaration order.
8. Diagnose missing, value/type-mismatched, invisible/unsupported,
   namespace-conflicting, and mixed-language type associations with stable
   codes.
9. Serialize resolved type metadata into versioned `sv-type-v1`
   specialization/native-cache identities.
10. Add frontend, elaboration, source-provenance, interpreter/LLVM O0/O2,
    cold/warm, and changed-type selective-invalidation evidence.

The frontend stores a data-type default or an unambiguous type actual in a
dedicated optional `Type`; it does not manufacture a value expression.
Identifier and `package::name` actuals retain ordinary expression HIR until a
matched formal establishes the type context. A new separately compiled
specialization service resolves those marks against the parent type
environment, installs per-occurrence aliases, and removes type formals before
the integral value-specialization pass.

Formal aliases may initially retain a value-dependent packed range. Named-type
resolution first propagates that alias into dependent declarations, the
ordinary value-specialization pass folds all ranges, and only then does the
builder validate the final 1–64-bit type and construct its canonical identity.
This ordering supports a prior value parameter controlling a default type and
a later value parameter/localparam declared in that type. Public
specialization values and identities are merged back in original formal
declaration order.

The standalone application fixture compiles a top, two differently typed
child specializations, and an unrelated child from file-scoped compilation
units. It compares exact final values through the interpreter and LLVM O0/O2,
requires four cold stores and four warm hits, then edits one packed type from
eight to six bits. Only the top and affected child miss; the default typed
child and unrelated child produce two cache hits.

This slice remains same-language and bounded to existing integral packed type
representations. String parameters, unpacked/interface/class/anonymous
composite actuals, widths above 64 bits, generated type declarations, and
mixed-language type transfer remain release-gate work.

The implementation is recorded in feature commit `a47d924`. The exact LLVM
22.1.8 warnings-as-errors Debug build completed cleanly and all 44 configured
tests passed in 301.82 seconds. The exact Release build completed cleanly and
all 45 configured tests passed in 114.06 seconds. Focused LLVM Debug and
LLVM-disabled gates passed frontend, diagnostics-catalog, source-budget,
elaboration, and standalone type-parameter application tests. The final
tracked-source gate covers 224 authored files with an empty allowlist. No CI
state was inspected.

### Fifty-second feature batch — SystemVerilog string parameters

The completed ten-feature architecture-gate slice is:

1. Represent immutable string values as decoded byte strings with source
   spans, independently from integral constant bits.
2. Parse `parameter string` and `localparam string` declarations in module
   parameter-port lists plus module and package bodies.
3. Decode supported source escapes exactly once and retain embedded zero
   bytes, original spelling, and diagnostic ancestry.
4. Fold bounded string literals, identifiers, concatenation,
   equality/inequality, and integral-selected conditional expressions.
5. Apply the same evaluator to defaults, named/positional overrides,
   localparams, package constants, and non-iterative generated constants.
6. Forward string values through nested same-language hierarchy and use them
   for conditional and exact-case generate selection.
7. Substitute constant strings into bounded `$display`, `$write`, `$strobe`,
   severity-report, and immediate-assertion action messages.
8. Diagnose integral/string mismatches, unsupported operators, invalid
   escapes, cycles, nonconstant messages, and mixed-language transfer with
   stable codes.
9. Serialize exact byte length/content into versioned `svstring-v1`
   specialization/native-cache identity while retaining source/include
   provenance and seed-independent reuse.
10. Require exact interpreter/LLVM O0/O2 output and report ordering, cold/warm
    reuse, edited-string selective invalidation, and the scheduled full
    regression.

The handwritten parser retains original string-token text for diagnostics but
also stores the decoded byte value in expression HIR. Elaboration never
decodes that spelling again. A separately compiled string semantic service
owns constant evaluation, substitution, source-safe display spelling, and
canonical identity; its declarations and data remain in the internal header
without moving executable implementation there.

Declaration-order specialization permits a string parameter to feed later
integral equality results and permits those results to select a later string
conditional. Package constants and generated constants use the same value
type. Cross-language string parameters are rejected explicitly because the v1
boundary contract still requires scalar logic/Boolean/integer or packed-vector
ports and same-language wrappers for richer values.

The standalone source fixture compares exact active and postponed output
ordering plus report ordering through the interpreter and LLVM at O0 and O2.
Three specializations produce three cold stores and three warm hits. Changing
only the project seed preserves all keys and warm hits; editing one top-level
string changes the top and affected child while the default child reuses its
native object.

Runtime mutable strings, string methods, arbitrary formatted argument lists,
files, dynamic containers, iterative-genvar-dependent string constants, and
cross-language string transfer remain release-gate work. This batch does not
claim the complete V1-SV-08 runtime string/file family.

The implementation is recorded in feature commit `cfc54fa`. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 45 configured tests in
287.87 seconds, and Release passed all 46 configured tests in 113.45 seconds
on 2026-07-29. Focused LLVM Debug and LLVM-disabled frontend, elaboration,
diagnostic-catalog, source-budget, and string-application gates also passed.
The final tracked-source gate covers 227 authored files with an empty
allowlist. No CI state was inspected.

### Fifty-third feature batch — synthesizable SystemVerilog functions

The completed ten-feature architecture-gate slice is:

1. Represent function declarations, integral return/formal/local types,
   bodies, lifetime, and source spans explicitly in SystemVerilog HIR.
2. Parse bounded module/package functions in ANSI and classic no-argument
   forms with explicit automatic lifetime, function-name assignment, explicit
   value return, and checked end names.
3. Resolve lexical, wildcard-imported, and directly package-selected function
   names with stable duplicate, ambiguity, visibility, and arity diagnostics.
4. Specialize 1–64-bit integral return/formal/local types and
   parameter-dependent packed ranges through the existing typed constant/type
   environments.
5. Give runtime calls deterministic automatic argument, local, result, and
   return-address storage that is initialized on every invocation.
6. Execute the bounded nonsuspending block, blocking-assignment, conditional,
   exact-case, canonical-loop, break/continue, expression, and return subset
   inside function bodies.
7. Evaluate eligible constant functions in parameters/localparams, packed
   ranges, and generate conditions, including bounded loop/case control.
8. Lower runtime calls to explicit persistent SimIR `Call`/`Return` control
   with debugger call safe points, checked metadata/failures, LLVM O0/O2
   lowering, and cache serialization.
9. Support nested distinct-function calls, diagnose direct/indirect recursion,
   and retain package-function source provenance in specialization/native
   cache identity.
10. Require frontend/negative/elaboration/runtime evidence plus exact
    interpreter/LLVM O0/O2 state and safe-point parity, cold/warm reuse, and
    edited-function invalidation.

The handwritten function parser is isolated in its own compilation unit and
retains only declaration/state interfaces in the internal header. Function
HIR remains language-specific until specialization substitutes parameter
ranges and package imports. Runtime lowering allocates one frame per visible
function in the owning process, reinitializes arguments, locals, and results
for every call, and rejects recursive call graphs; consequently the bounded
call-stack capacity is the number of distinct visible functions.

SimIR now has versioned `CallStack`, `Call`, and `Return` operations. The
reference interpreter and LLVM lowering both keep the return stack in
persistent plain packed registers, so suspension at a debugger safe point
inside a nested call resumes with identical state and no C++ object,
exception, or compiler-specific integer crossing the native ABI. Validation
checks target and stack metadata before compilation, while runtime checks
cover unknown pointers, overflow, underflow, and invalid dynamic return
targets. LLVM lowering for jump/call/return/branch control lives in a separate
159-line implementation unit, keeping the original lowering unit at 1,987
lines.

The standalone application fixture uses a loop/case constant function to
derive a packed width, calls local nested functions and both imported and
directly selected package functions, compares exact safe-point sequences and
final values through the interpreter and LLVM O0/O2, requires one cold store
and one warm hit, then edits only the separate package function source and
requires a new specialization/native object.

Static or implicit function lifetimes, classic body argument declarations,
output/inout/ref/default/unpacked arguments, widths above 64 bits, recursion,
timing/event/task statements, runtime strings, DPI, generated functions, and
all tasks remain release-gate work.

The implementation is recorded in feature commit `9aee9e4`. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 46 configured tests in
304.19 seconds, and Release passed all 47 configured tests in 123.26 seconds
on 2026-07-29. Focused LLVM Debug and LLVM-disabled gates passed all seven and
six affected tests, respectively. The diagnostic catalog covers all 834
production codes. The final tracked-source gate covers 233 authored files
with an empty allowlist and a maximum of 1,987 lines. No CI state was
inspected.

### Fifty-fourth feature batch — synthesizable SystemVerilog tasks

The completed ten-feature architecture-gate slice is:

1. Represent task declarations, directional integral formals, locals, bodies,
   lifetime, and source spans independently from functions.
2. Parse bounded module/package tasks in ANSI and classic no-argument forms
   with explicit automatic lifetime, valueless return, and checked end names.
3. Resolve lexical, wildcard-imported, and directly package-selected task
   names with stable duplicate, ambiguity, visibility, and arity diagnostics.
4. Specialize 1–64-bit input/output/inout formal and local types through the
   existing parameter-dependent packed type environments.
5. Give each invocation deterministic automatic formal/local storage with
   input/inout copy-in and ordered output/inout copy-out.
6. Execute nonsuspending blocks, blocking assignments, conditionals, exact
   case, canonical loops, expressions, function calls, nested task calls, and
   valueless early return inside task bodies.
7. Lower task calls through separate persistent SimIR `CallStack`, `Call`,
   and `Return` control, with copy-out at the caller continuation.
8. Reject task statements inside functions, exclude tasks from constant
   function evaluation, and diagnose direct or indirect task recursion.
9. Retain task-call debugger safe points, addressable formals/locals, and
   transitive package-source specialization/native-cache provenance.
10. Require frontend, negative, elaboration, interpreter/LLVM O0/O2,
    debugger-local, cold/warm, and edited-task invalidation evidence.

The task HIR and parser deliberately remain separate from value-returning
functions. Runtime lowering allocates a per-process task frame for each
visible task; inputs and inouts are copied before the call, output formals are
initialized, and output/inout actuals are updated only after normal or early
return. A distinct task return stack prevents task nesting from perturbing the
existing function ABI. Direct and indirect recursive task graphs are rejected.

The standalone application fixture invokes a local task that calls an
imported package task, exercises input/output/inout semantics and task locals,
and compares final values through the interpreter and LLVM O0/O2. It requires
one cold native-object store, one warm hit, and a miss plus changed
specialization key after editing the task implementation. The elaboration
fixture separately covers direct package selection, nested function/task
calls, early return, arity rejection, and recursion diagnosis.

At the batch-54 boundary, static or implicit task lifetimes, classic body
argument declarations, timing/event controls, `ref`, default/unpacked
arguments, widths above 64 bits, recursion, runtime strings, DPI, generated
tasks, and cross-language task calls remained release-gate work. Batch 55
below supersedes the timing/event-control limitation for the precisely
bounded automatic-task subset.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 47
configured tests in 308.90 seconds, and Release passed all 48 configured
tests in 124.34 seconds on 2026-07-29. The diagnostic catalog covers all 857
production codes. The final tracked-source gate covers 236 authored files
with an empty allowlist and a maximum of 1,987 lines. No CI state was
inspected.

### Fifty-fifth feature batch — suspending automatic SystemVerilog tasks

The completed ten-feature architecture-gate slice is:

1. Admit statement-level delays, named-event waits/triggers, and condition
   waits in otherwise bounded automatic integral task bodies.
2. Preserve the task return continuation, call-stack pointer/entries,
   formals, and locals in persistent process registers across suspension.
3. Leave output and inout copy-out operations at the caller continuation so
   intermediate waits cannot publish formal values.
4. Support nested suspension through distinct local and imported package
   tasks, including package task delay normalization under the package time
   context.
5. Execute suspending calls from `initial`, event-controlled `always`, and
   already-suspending tasks.
6. Diagnose direct or indirect suspending calls from `final`,
   `always_comb`, and `always_latch` through a fixed-point task call-graph
   analysis while retaining the existing function/task separation.
7. Retain task call/wait/process-suspend safe points and live task
   formals/locals across debugger stop, read, and resume in interpreter and
   compiled execution.
8. Preserve a delayed nonblocking named-event deadline scheduled before a
   nested suspension and NBA/update ordering when a condition wait wakes.
9. Unwind valueless early return after suspension through the shared task
   epilogue with exactly one ordered copy-out.
10. Require signal-change and safe-point parity across interpreter, LLVM O0,
    LLVM O2, cold/warm cache reuse, and a task-source edit that changes both
    specialization identity and behavior.

Task frames did not require a runtime ABI change: the existing SimIR task
`CallStack`, formal/local registers, and caller continuation were already
process-persistent. The implementation therefore adds frontend legality,
time traversal, transitive lifecycle checks, and evidence around the
existing resumable process frame. Delays inside package tasks now inherit
and expose package `timescale`/`timeunit` context before project-resolution
normalization.

The standalone application fixture starts a local task from an
event-controlled `always`, schedules a delayed named event, suspends through
an imported package task, observes an output before return, waits on the
event and an NBA-updated condition, returns early, and finally checks
deferred output/inout publication. It compares final time/state,
signal-change observations, and execution points across interpreter and LLVM
O0/O2, stops at the compiled and interpreted suspended boundary to read live
formals before resuming, requires cold/warm cache behavior, and edits the
package task source to require a new key and native object.

General timeout syntax is not claimed: the bounded SystemVerilog subset has
no independent timeout construct without the still-deferred fork/join
family. The fixture instead proves that an absolute delayed named-event
deadline survives nested task suspension. Static or implicit task
lifetimes, classic body argument declarations, `ref`, default/unpacked
arguments, widths above 64 bits, recursion, runtime strings, fork/join and
general event expressions, DPI, generated tasks, and cross-language task
calls remain release-gate work.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 48
configured tests in 313.13 seconds, and Release passed all 49 configured
tests in 129.00 seconds on 2026-07-29. Focused LLVM Debug and LLVM-disabled
task gates also passed. The diagnostic catalog covers all 858 production
codes. The final tracked-source gate covers 237 authored files with an empty
allowlist and a maximum of 1,987 lines. No CI state was inspected.

### Fifty-sixth feature batch — constrained VHDL interface-type actuals

The completed ten-feature architecture-gate slice is:

1. Represent an unambiguously parsed VHDL subtype-indication actual in the
   shared parameter-override HIR while retaining syntactically ambiguous
   parenthesized forms for formal-aware elaboration.
2. Parse named and positional `range` subtype indications with their selected
   type mark, direction, bound expressions, and source spans without changing
   ordinary value-generic expressions.
3. Interpret a parenthesized VHDL slice as an index constraint only after its
   association has matched an interface type formal.
4. Resolve and constrain builtin vector families plus parent-local and direct
   package-selected vector or one-dimensional user-array type marks.
5. Resolve portable integer-family range actuals through the existing
   signed-32-bit range folder and derived-base containment checks.
6. Resolve nominal enumeration-range actuals with literal or correctly typed
   parent-generic bounds while preserving nominal identity and direction.
7. Forward constrained vector and enumeration actuals through a nested
   unclassified interface type formal without losing concrete metadata.
8. Reject null arrays, out-of-base ranges, constrained-array reconstraint,
   wrong-kind/value expressions, type actuals on value formals, invisible
   marks, unconstrained objects, and cross-language associations.
9. Include every folded constraint in the existing versioned structural type
   identity so specialization and native-object cache keys distinguish exact
   actual shapes.
10. Extend frontend, negative, elaboration, interpreter, LLVM O0/O2,
    child-source, cold/warm cache, and edited-constraint selective-invalidation
    evidence.

The parser deliberately does not decide that every `T(L downto R)` expression
is a type actual: the same syntax can denote a value slice. It retains that
expression and the elaborator reinterprets it only when the matched formal has
HIR kind `Type`. The unambiguous `T range L to R` form uses the typed actual
slot immediately. Both paths converge on the ordinary VHDL named-type
constraint merger and constant folder.

Parent specialization now supplies both constant values and their nominal
domains to child type-actual folding. This matters for enumeration bounds:
an integer with the same ordinal is not accepted as an enumeration generic.
Builtin vectors also receive explicit null-object checks at local-object and
hierarchy-port creation rather than silently acquiring scalar width.

The focused matrix covers builtin and generic-dependent vectors, portable
integer ranges, reverse nominal enumeration ranges, local and
package-selected scalar-element arrays, numeric and typed enumeration parent
bounds, and nested forwarding. The application differential changes a direct
`bit_vector(L downto 0)` actual from four to eight bits and requires only that
specialization to miss the cache in interpreter/LLVM O0/O2 runs.

Interface subprogram/package generics, generic package/subprogram units,
type-formal-dependent record/array declarations, access/file/protected types,
multidimensional or composite-element arrays, unconstrained runtime objects,
and mixed-language type actuals remain release-gate work. VHDL-2019 classified
interface type syntax remains outside the VHDL-2008 grammar.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, and VHDL type-generic application gate passed. The catalog now
covers 859 production codes, and the source gate still covers 237 authored
files with an empty allowlist and a maximum of 1,987 lines. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 48 configured tests in
312.06 seconds, and Release passed all 49 configured tests in 127.89 seconds
on 2026-07-29. No CI state was inspected.

### Fifty-seventh feature batch — VHDL-2008 interface function generics

The completed ten-feature architecture-gate slice is:

1. Represent interface functions as a distinct generic-formal HIR kind with
   retained purity, supported parameter/result profile, default, and span.
2. Parse optional `pure`/`impure`, the standard optional `parameter` keyword,
   constant input formals, scalar return type, required form, named default,
   and `is <>`, with bounded recovery and stable diagnostics.
3. Match named and positional function actual designators in the existing
   VHDL generic association order without weakening value/type
   disambiguation.
4. Resolve a unique conforming pure local function or directly visible
   package function, including declaration/body matching across package
   design units.
5. Copy the selected body under the formal name into specialization-local HIR
   and retain versioned profile/source identity plus physical dependencies.
6. Evaluate calls in bounded integer constant expressions and value-generic
   defaults, leaving nonstatic calls for common runtime lowering.
7. Forward a bound formal through another generic entity while preserving its
   original body, source location, identity, and time-free context.
8. Reuse typed SimIR function call frames, explicit returns, locals,
   debugger call points, nonrecursive call-graph checks, and cache machinery.
9. Diagnose malformed profiles, procedures, timing or signal updates,
   non-name actuals, missing, invisible, wrong-profile, ambiguous, undefined,
   impure, conflicting, and non-VHDL bindings.
10. Add frontend/package-body, negative, elaboration, constant-default,
    package visibility, interpreter, LLVM O0/O2, call-point, cold/warm, and
    edited-function cache evidence.

The parser retains VHDL's optional `parameter` keyword before the formal-list
parentheses. Function formals are limited to constant input parameters and
scalar integer/Boolean/bit or visible supported scalar subtypes. Bounded
function bodies are automatic, time-free, use local-variable updates, and
require explicit value return. The same time-free integral evaluator used by
SystemVerilog functions folds VHDL calls only when all inputs are locally
static; dynamic signal inputs remain as call HIR and lower through the common
SimIR call/return path.

Package bodies retain the existing VHDL package unit kind with an explicit
secondary-unit marker. Package specialization merges a matching bounded
function body into its visible declaration, preserves the body source as a
dependency, and imports only defined public declarations. Function actual
identity includes its profile, purity, physical source, and source offset,
while the source dependency hash invalidates callers after an implementation
edit.

Interface procedures/packages, operator-symbol designators,
unconstrained/composite formals or results, generated functions, general
overload sets, generic package/subprogram units, recursive calls, and
mixed-language subprogram actuals remain outside this bounded slice.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, and VHDL function-generic application gate passed. The catalog
now covers 901 production codes, and the source gate covers 240 authored files
with an empty allowlist and a maximum of 1,987 lines. The exact LLVM 22.1.8
warnings-as-errors Debug regression passed all 49 configured tests in 316.94
seconds, and Release passed all 50 configured tests in 128.77 seconds on
2026-07-29. No CI state was inspected.

### Fifty-eighth feature batch — VHDL-2008 interface procedure generics

The completed ten-feature architecture-gate slice is:

1. Represent interface procedures as a distinct generic-formal HIR kind with
   retained constant/variable class, mode, supported scalar profile, default,
   association spans, and source span.
2. Parse the standard optional `parameter` keyword, constant or variable
   `in`/`out`/`inout` formals, required form, named default, and `is <>`, with
   bounded recovery and stable diagnostics.
3. Parse bounded local and declaration/body-matched package procedure bodies,
   procedure-local variables, valueless return, and positional or named
   sequential procedure-call statements.
4. Resolve a unique conforming same-language local or directly visible package
   procedure by the complete supported object-class, mode, and type profile.
5. Copy the selected body under the formal name into specialization-local HIR
   and retain versioned profile/source identity plus physical package-body
   dependencies.
6. Lower time-free sequential procedure calls through independent SimIR call
   frames with deterministic scalar input copy-in and ordered output/inout
   copy-out to writable signal or variable actuals.
7. Forward a bound interface procedure through another generic entity without
   losing its selected body, formal modes, source, or cache provenance.
8. Preserve debugger call points and live procedure formals and locals across
   interpreter and LLVM O0/O2 execution, including a nested procedure call.
9. Diagnose malformed profiles, missing/defaultless, expression, invisible,
   wrong-profile, ambiguous, undefined, function-kind, conflicting,
   generated/scoped, timed, recursive, non-writable, and cross-language
   actuals.
10. Add frontend, package-body, negative, elaboration, runtime, nested-call,
    debugger-local, cold/warm, and edited-procedure selective-cache evidence.

Procedures remain distinct from both value-returning VHDL functions and
SystemVerilog tasks in source HIR and call statements. Only the low-level SimIR
call-stack representation is shared. The bounded body is automatic, time-free,
scalar, and may update procedure formals or local variables. Signal assignment,
waits, suspensions, unconstrained or composite formals, signal/file classes,
operator-symbol designators, generated declarations, general overload sets,
generic package/subprogram units, and mixed-language procedure actuals remain
outside this slice.

Package specialization now merges a matching bounded procedure body into its
public declaration, imports only defined declared procedures, and retains the
separate body source as a transitive dependency. Procedure actual identity
includes the complete supported class/mode/type profile, physical source, and
source offset. Editing a local actual invalidates direct, named-default,
box-default, and forwarded consumers while leaving the package-procedure
specialization reusable.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, and VHDL procedure-generic application gates passed. The
catalog now covers 949 production codes and the source gate covers 244 authored
files with an empty allowlist and a maximum of 1,987 lines. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 50 configured tests in
306.74 seconds, and Release passed all 51 configured tests in 132.71 seconds
on 2026-07-29. No CI state was inspected.

### Fifty-ninth feature batch — VHDL-2008 interface package generics

The completed ten-feature architecture-gate slice is:

1. Represent interface package formals and package instances as distinct HIR
   kinds retaining the selected template, generic-map form, associations,
   defaults, identities, and source spans.
2. Parse bounded generic package declarations plus
   entity/architecture-local package instantiations with explicit,
   individual-`<>`, and whole-`<>` generic maps.
3. Support package templates over the existing scalar value, type, function,
   and procedure generic families, including dependent explicit maps.
4. Specialize local package instances in declaration order with matching
   bounded bodies, declarations, maps, and transitive source provenance.
5. Resolve a unique same-language local package-instance actual against the
   interface formal's template and supported fixed/default/box map profile.
6. Materialize selected constants, types, functions, and procedures beneath
   the interface-package formal prefix for dependent declarations and calls.
7. Forward an exact interface package binding through a nested generic entity
   without losing its instance, selected members, map, source dependencies, or
   specialization identity.
8. Define a versioned canonical package identity over the template, actual
   map, specialized declarations/bodies, and transitive package-body sources.
9. Diagnose malformed maps; missing, expression, invisible, wrong-kind,
   wrong-template, incompatible, unspecialized, ambiguous, cyclic,
   generated/scoped, incomplete-body, nongeneric-target, and cross-language
   package actuals.
10. Add frontend, negative, elaboration, dependent-object/subprogram runtime,
    debugger-frame, interpreter/LLVM O0/O2, cold/warm, and edited-package
    selective-cache evidence.

This slice deliberately keeps package instances acyclic, same-language, and
local to entity or architecture declarative regions. Generic templates use
only the already supported scalar value/type/function/procedure families.
Nested package instantiation, generated/scoped instances, general generic
package or subprogram units, unconstrained/composite declarations, overload
sets, and mixed-language package actuals remain outside the bounded contract.

Package specialization now resolves dependent type and subprogram actuals in
declaration order, merges matching bounded bodies, renames callable members
under the selected formal prefix, and propagates physical declaration/body
dependencies into specialization and native-cache keys. Editing the package
body invalidates its direct and forwarded consumers while an unrelated
package specialization remains reusable. The runtime differential covers a
direct local instance and a forwarded interface instance at interpreter,
LLVM O0, and LLVM O2, including debugger-visible procedure metadata.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, and VHDL package-generic application gates passed. The catalog
now covers 979 production codes and the source gate covers 249 authored files
with an empty allowlist and a maximum of 1,987 lines. The exact LLVM 22.1.8
warnings-as-errors Debug regression passed all 51 configured tests in 320.46
seconds, and Release passed all 52 configured tests in 130.93 seconds on
2026-07-29. No CI state was inspected.

### Sixtieth feature batch — VHDL-2008 generic subprograms

The completed ten-feature architecture-gate slice is:

1. Represent generic function/procedure templates and instantiated
   subprograms as distinct HIR forms retaining generic lists, callable
   profiles, maps, bodies, spans, and identities.
2. Parse bounded generic subprogram clauses over the existing scalar value,
   type, function, and procedure generic families with stable recovery.
3. Parse local and package-visible function/procedure instantiations with
   omitted-default, explicit positional/named, individual-`<>`, and
   whole-`<>` maps.
4. Match declaration/body pairs and specialize instances in declaration
   order while keeping the generic template itself noncallable.
5. Resolve dependent scalar types, values, function calls, and procedure
   calls inside the specialized body from the selected generic map.
6. Bind an instantiated function or procedure as an interface-subprogram
   actual and forward its helper bodies and identity through nested hierarchy.
7. Reuse the independent function and time-free procedure SimIR call frames,
   including deterministic procedure copy-in/copy-out and debugger metadata.
8. Define a versioned canonical identity over the template, map, callable
   profile, declaration/body sources, bound helpers, and transitive package
   provenance.
9. Diagnose malformed and illegal maps; missing, ambiguous, wrong-kind,
   nongeneric, unspecialized, recursive, conflicting, generated/scoped,
   incomplete, mismatched, nested-package, and cross-language cases.
10. Add frontend, negative, elaboration, package visibility, nested
    forwarding, runtime, debugger, interpreter/LLVM O0/O2, cold/warm, and
    edited-source selective-cache evidence.

Generic subprogram templates remain bounded to pure functions or time-free
procedures with scalar callable profiles and the already implemented generic
families. Package declaration/body pairs export instantiated callables, while
local instances retain declaration-order visibility. Each bound function or
procedure generic becomes an instance-local helper, and transitive helper
dependencies follow an instantiated subprogram when it is forwarded as an
interface actual.

Operator-symbol designators, general overload sets, unconstrained/composite
profiles, nested generic templates, generated/scoped instantiations,
suspending generic procedures, interface-package formals inside generic
subprograms, and mixed-language actuals remain outside this bounded slice.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, and VHDL generic-subprogram application gates passed. The
catalog now covers 1,008 production codes and the source gate covers 253
authored files with an empty allowlist and a maximum of 1,987 lines. The exact
LLVM 22.1.8 warnings-as-errors Debug regression passed all 52 configured tests
in 315.70 seconds, and Release passed all 53 configured tests in 129.98
seconds on 2026-07-29. No CI state was inspected.

### Sixty-first feature batch — VHDL-2008 configurations

The completed ten-feature architecture-gate slice is:

1. Represent configuration declarations, architecture blocks, component
   configurations/specifications, binding indications, instantiation
   selections, maps, context, and spans as distinct HIR.
2. Parse bounded named configuration declarations selecting one architecture
   through a top-level `for architecture_name` block.
3. Parse architecture declarative configuration specifications and
   declaration-owned component configurations with explicit
   `use entity library.entity(architecture)` bindings.
4. Support explicit component-label lists plus bounded `all` and `others`
   selection with deterministic overlap, missing-component, and wrong-label
   diagnostics.
5. Index named configurations as VHDL project-top targets and resolve exactly
   one entity and selected architecture in the manifest library.
6. Give configuration-declaration rules precedence at the configured root,
   apply architecture specifications recursively, and keep direct entity
   instances and manifest bindings independent.
7. Compose named generic and port binding maps through named component actuals
   before the existing specialization and boundary-legality paths.
8. Define a versioned configuration identity over its target, selection rules,
   maps, and selected architectures while propagating physical configuration
   sources into configured child cache provenance.
9. Diagnose malformed or incomplete HIR; missing/ambiguous targets,
   components, or labels; conflicting `all`/`others`; unsupported nested
   forms; unresolved positional map composition; recursion; and cross-language
   targets.
10. Add frontend, negative, elaboration, top-selection, nested component,
    runtime, source-debug, interpreter/LLVM O0/O2, cold/warm, and edited-
    configuration selective-cache evidence.

The executable subset is same-language VHDL and applies only to
component-style instances. A configuration declaration contains one selected
architecture block, while an architecture may carry bounded declarative
configuration specifications. Named binding maps may rename component
generic/port formals into the selected entity interface. The configuration
source invalidates only the configured root and selected component consumers;
direct entity, unrelated stable, and nested architecture-specification
specializations remain reusable.

General nested block/generate or incremental configurations, configuration
aspects and `open`, positional binding-indication maps, complete component-
declaration and default-binding rules, and mixed-language configuration
targets remain outside this bounded slice. Batch 62 subsequently added
architecture-local component declarations and normalized positional component
instance associations before this binding-indication composition.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, and VHDL configuration application gates passed. The catalog
now covers 1,042 production codes and the source gate covers 258 authored
files with an empty allowlist and a maximum of 1,987 lines. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 53 configured tests in
320.44 seconds, and Release passed all 54 configured tests in 134.91 seconds
on 2026-07-29. No CI state was inspected.

### Sixty-second feature batch — VHDL-2008 component declarations

The completed ten-feature architecture-gate slice is:

1. Represent architecture-local component declarations, value-generic
   profiles/defaults, scalar/vector port profiles/modes/default metadata,
   declaration order, optional end names, and spans as explicit HIR.
2. Parse bounded component declarations in architecture declarative regions
   using the existing scalar/vector type and value-generic grammar.
3. Diagnose duplicate/conflicting formals and declarations, mismatched end
   names, configuration use before declaration, non-value generics, and
   unsupported component items.
4. Require each component-style instance to resolve exactly one visible
   declaration instead of treating its component name as an implicit entity.
5. Implement deterministic same-library default binding to exactly one entity
   and one uniquely available architecture when no configuration rule applies.
6. Normalize named and positional component generic/port actuals against the
   declaration, enforce ordering/coverage/default rules, and map compatible
   formals by position into the entity interface.
7. Check value-generic kind/type/dependent-width and port mode/type/width
   compatibility, including renamed formals, while materializing component
   defaults independently of entity defaults.
8. Validate configuration binding maps against both component and entity
   profiles and compose them after positional component actual normalization.
9. Diagnose missing/ambiguous declarations, entities, or architectures;
   incompatible profiles; illegal maps; cycles; and implicit cross-language
   default binding.
10. Add frontend, negative, elaboration, configuration-interaction, hierarchy,
    source-debug, interpreter/LLVM O0/O2, cold/warm, and edited-profile
    selective-cache evidence.

The executable subset is architecture-local, same-language VHDL and covers
value generics plus the existing supported scalar/vector port types. Component
and entity formal names may differ when their declaration-order profiles
match. A configuration binding may explicitly rename those formals, while an
unconfigured component requires a unique entity and architecture in the
parent library. Direct entity and explicit manifest bindings remain
independent.

Each bound child carries a versioned component declaration/profile/target
identity and the physical declaration source as semantic cache provenance.
The runtime differential renames only component formals between builds:
component consumers and the parent receive new keys, while an unrelated
direct-entity child remains reusable.

Package/entity-visible or overloaded component declarations, non-value
component generics, composite interfaces, executable omitted port defaults,
mixed-language default binding, and complete LRM default-binding precedence
remain outside this bounded slice.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, configuration, and component application gates passed. The
catalog now covers 1,067 production codes and the source gate covers 263
authored files with an empty allowlist and a maximum of 1,987 lines. The exact
LLVM 22.1.8 warnings-as-errors Debug regression passed all 54 configured tests
in 312.54 seconds, and Release passed all 55 configured tests in 134.16
seconds on 2026-07-30. No CI state was inspected.

### Sixty-third feature batch — nested VHDL-2008 configurations

The completed ten-feature architecture-gate slice is:

1. Represent recursive block/generate configurations, optional explicit
   generate indices, child rules, entity/configuration/open aspects, maps, and
   spans as explicit HIR.
2. Parse nested configurations for existing unguarded labeled static blocks.
3. Parse and elaborate statically selected one-dimensional iterative,
   conditional, and case-generate configuration occurrences over canonical
   generated hierarchy paths.
4. Resolve same-language `use configuration library.name` aspects with `work`
   normalized to the parent library and activate the referenced configuration
   only for that component subtree.
5. Treat `use open` as an explicit request for the bounded unique same-library
   default component binding.
6. Select the deepest matching nested rule, falling back deterministically
   through enclosing configuration blocks and architecture specifications.
7. Compose supported generic and port maps through nested entity and referenced
   configuration aspects after component-formal normalization.
8. Define version-2 recursive configuration/binding identities containing
   nested paths, maps, references, selected targets, and transitive physical
   sources.
9. Diagnose missing, ambiguous, duplicate, or non-static scopes; missing or
   ambiguous references; ambiguous open defaults; cycles; invalid maps; and
   cross-language configuration targets.
10. Add frontend, negative, block/for/if/case elaboration, reference hierarchy,
    source-debug, interpreter/LLVM O0/O2, cold/warm, and edited-reference
    selective-cache evidence.

The executable subset remains same-language VHDL and uses existing unguarded
blocks, already-supported generate forms, one-dimensional explicit generate
indices, and architecture-local bounded component profiles. Incremental
configurations, dynamic or multi-index generate specifications, general
block-specification ranges, package-visible component overloads, and
mixed-language configuration references remain outside this slice.

Each configured child now carries a recursive binding identity. A referenced
configuration is registered at the selected child hierarchy path, so its rules
cannot leak into siblings. Editing the referenced configuration invalidates
that child and its configured descendants while unrelated root/direct/stable
specializations retain their native-cache keys.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, and VHDL configuration application gates passed. The catalog
now covers 1,070 production codes and the source gate covers 263 authored files
with an empty allowlist and a maximum of 1,987 lines. The exact LLVM 22.1.8
warnings-as-errors Debug regression passed all 54 configured tests in 181.60
seconds, and Release passed all 55 configured tests in 64.66 seconds on
2026-07-30. No CI state was inspected.

### Sixty-fourth feature batch — VHDL-2008 component visibility and overloads

The completed ten-feature architecture-gate slice is:

1. Extend component HIR ownership with explicit architecture, entity, package,
   block, and generate declarative-region provenance plus lexical scope paths.
2. Parse bounded component declarations in existing entity, package,
   architecture, unguarded-block, and selected-generate declarative regions.
3. Import package-declared component profiles through existing selected and
   wildcard `use` visibility with owning-package source provenance.
4. Select nearest lexical declarations before architecture, entity, and
   directly visible package declarations without leaking sibling scopes.
5. Select repeated-name component overloads by normalized generic/port
   association shape rather than rejecting every repeated name.
6. Include modes, resolved types, value-generic profiles, and dependent widths
   in bounded overload and entity-profile matching.
7. Select the latest analyzed compatible same-library architecture by default
   while preserving architecture-specification and configuration precedence.
8. Apply architecture specifications and recursive configuration rules after
   choosing the visible component declaration/profile.
9. Preserve declaration region, scope, order, owner/package sources, normalized
   profile, and selected target in version-2 specialization/cache identity.
10. Add frontend, negative, elaboration, lexical/package/overload/default
    precedence, interpreter/LLVM O0/O2, debugger, cold/warm, and edited-package-
    profile selective-cache evidence.

The executable subset remains same-language VHDL and covers existing bounded
value generics and scalar/vector ports. Components declared in selected
generate bodies receive the canonical generated scope path; a block/generate
declaration is visible only in its own subtree. Entity and architecture
declarations shadow imported package declarations, and equally visible
overloads are filtered using the instance association shape and the unique
default entity interface. Package ownership and source dependencies survive
normalization into each selected child.

Default binding now uses deterministic parsed-unit analysis order: the latest
compatible architecture wins when no explicit rule applies. Configuration
specifications no longer depend on textual component-declaration order and
continue to override that default after overload selection. This replaces the
former multiple-architecture error while retaining ambiguity diagnostics for
multiple entity interfaces or equally visible matching declarations.

Non-value component generics, composite component interfaces, executable
omitted port defaults, mixed-language default binding, incremental
configurations, complete library analysis-order semantics, and general overload
resolution remain outside this bounded slice.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, configuration, and component application gates passed. The
catalog now covers 1,069 production codes and the source gate covers 263
authored files with an empty allowlist and a maximum of 1,987 lines. The exact
LLVM 22.1.8 warnings-as-errors Debug regression passed all 54 configured tests
in 318.53 seconds, and Release passed all 55 configured tests in 130.33 seconds
on 2026-07-30. No CI state was inspected.

### Sixty-fifth feature batch — VHDL-2008 composite component profiles

The completed ten-feature architecture-gate slice is:

1. Retain a resolved nominal base identity separately from the selected
   VHDL type or subtype declaration and its physical source provenance.
2. Accept existing bounded enumeration, named scalar/vector subtype,
   non-nested record, and one-dimensional scalar-element user-array type
   indications in component port profiles.
3. Resolve local, directly visible package, and selected package type marks in
   the owning component region without re-exporting package imports.
4. Preserve nominal record/enumeration/array identity, layout, bounds, and
   direction while normalizing component and entity formals by position.
5. Select equally visible component overloads by nominal type, subtype
   constraint/direction, mode, association shape, and value-generic
   specialization.
6. Bind whole-signal composite actuals into compatible same-language entity
   ports while rejecting structurally equal but nominally distinct records.
7. Compose architecture specifications and recursive configuration port maps
   after composite component-formal normalization.
8. Define version-3 component identity over the resolved nominal layout,
   constraints, type/subtype sources, owning packages, profile, and target.
9. Diagnose missing or ambiguous type marks, nominal mismatches, incompatible
   constraints/modes, unsupported composite defaults, illegal maps, and
   implicit mixed-language defaults.
10. Add frontend, negative, elaboration, package visibility,
    overload/configuration, runtime, debugger, interpreter/LLVM O0/O2,
    cold/warm, and edited-type-source selective-cache evidence.

The executable subset remains same-language VHDL and carries whole-signal
values over the existing bounded enumeration, non-nested packed-record,
one-dimensional scalar-element user-array, and named scalar/vector subtype
families. Entity interfaces are resolved in their own declaration visibility
context, component declarations in package imports retain specialized type
profiles, and wildcard or selected package use no longer leaks declarations
imported by that package.

Composite conformance is nominal: records and user arrays must share the
declared family, while scalar, vector, enumeration, and array subtype bounds
and direction participate in overload and target matching. The runtime
differential copies packed record values through two component children,
compares interpreter and LLVM O0/O2 state and debugger execution points,
requires cold/warm cache behavior, and edits only the package record layout to
invalidate component consumers while an unrelated direct child remains
stable.

Composite aggregates as port actuals, non-value component generics,
executable omitted composite port defaults, incremental configurations,
mixed-language default binding, nested records, multidimensional or composite-
element arrays, and general overload resolution remain outside this bounded
slice.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, component, record, package-record, subtype, enumeration, and
array gates passed. The catalog now covers 1,071 production codes and the
source gate covers 263 authored files with an empty allowlist and a maximum of
1,987 lines. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed
all 54 configured tests in 319.51 seconds, and Release passed all 55 configured
tests in 132.56 seconds on 2026-07-30. No CI state was inspected.

### Sixty-sixth feature batch — VHDL-2008 non-value component generics

The completed ten-feature architecture-gate slice is:

1. Retain component generic kinds and profiles for the existing interface
   type, function, procedure, and package families.
2. Parse bounded type/function/procedure/package generics in component
   declarations across architecture, entity, package, block, and selected-
   generate declarative regions, including explicit `<>` actuals.
3. Resolve component interface formals in declaration order so later callable
   profiles and dependent ports can reference earlier type and package formals.
4. Normalize explicit, omitted-default, and box actuals against the selected
   component declaration before target binding.
5. Match same-language component and entity non-value generic profiles by
   kind, callable/package profile, defaults, and formal position.
6. Select equally visible component overloads after specializing dependent
   ports through normalized non-value generic maps.
7. Compose architecture specifications and recursive configuration maps
   through renamed non-value component formals while preserving direct-entity
   isolation.
8. Define version-4 component identity over canonical non-value profiles,
   selected actual identities, physical dependencies, maps, and target.
9. Diagnose missing, ambiguous, wrong-kind, incompatible-profile, illegal
   default/box/map, recursive, and implicit cross-language cases.
10. Add frontend, negative, elaboration, visibility/overload/configuration,
    runtime, debugger, interpreter/LLVM O0/O2, cold/warm, and edited-callable
    selective-cache evidence.

The executable subset remains same-language VHDL and reuses the existing
bounded interface type, pure scalar function, time-free procedure, and generic
package families. Component defaults are materialized independently of entity
defaults, omitted or box callable defaults resolve to their selected parent
actuals before forwarding, and dependent port profiles are specialized before
overload and entity-profile filtering. Configuration maps compose by formal
position after component normalization.

Version-4 identity includes the selected type, function, procedure, and package
actuals plus their declaration/body/template sources. Editing the selected
function source invalidates the owning component consumer while unrelated
value-only component and direct-entity children retain their native-cache keys.

New generic families, general composite expression actuals, nested package or
template forms, executable omitted port defaults, incremental configurations,
mixed-language component binding, and general overload resolution remain
outside this bounded slice.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, configuration, and component application gates passed. The
catalog now covers 1,073 production codes and the source gate covers 264
authored files with an empty allowlist and a maximum of 1,987 lines. The exact
LLVM 22.1.8 warnings-as-errors Debug regression passed all 54 configured tests
in 341.97 seconds, and Release passed all 55 configured tests in 135.92 seconds
on 2026-07-30. No CI state was inspected.

### Sixty-seventh feature batch — VHDL-2008 component port defaults and `open`

The completed ten-feature architecture-gate slice is:

1. Retain component input defaults, ordinary expressions, and explicit
   `open` port actuals as distinct HIR states.
2. Parse omitted and `open` associations across existing component instance
   and configuration-map regions without treating `open` as an unsupported
   expression.
3. Resolve component defaults after generic/type specialization in the
   declaration's direct package visibility and previously declared generic
   context.
4. Normalize omitted and explicit-`open` actuals by selected component-formal
   position after overload filtering.
5. Materialize defaults only for inputs and model omitted/open output-family
   ports as owned disconnected child objects without parent aliases or
   boundary drivers.
6. Keep component defaults independent from entity defaults and reject
   required unassociated direct-entity inputs.
7. Compose architecture specifications and recursive configuration port maps
   after default/open normalization while preserving direct-entity isolation.
8. Define version-5 component identity over canonical defaults, normalized
   default/open state, target mappings, dependencies, configuration identity,
   selected generic/type identities, and target.
9. Diagnose illegal default modes, required open inputs, missing actuals,
   dynamic/incompatible defaults, invalid maps, ambiguous profiles, and
   implicit cross-language default binding.
10. Add frontend, negative, elaboration, declaration-visibility,
    configuration, typed-runtime, debugger, interpreter/LLVM O0/O2,
    cold/warm, and edited-default selective-cache evidence.

The executable subset remains same-language VHDL. Supported statically
foldable defaults cover scalar integer/Boolean/bit/logic, packed vectors,
enumerations, named subtypes, non-nested packed records, and one-dimensional
scalar-element arrays. Positional, named, range, and final-`others` aggregate
forms are materialized into the existing packed runtime representation.
Defaults may depend on earlier component value generics and directly visible
package constants. A dynamic signal reference is rejected when the default is
selected, but unused default metadata does not invalidate an otherwise legal
explicit whole-signal association.

Omitted and explicit-open input associations select the component declaration
default after component overload selection. Omitted and open `out`, `inout`,
or `buffer` formals create no parent alias; the child receives an ordinary
owned port object, so writes are deterministically discarded outside the
child. Required inputs on direct entity instances remain errors because this
batch does not implement entity-port defaults.

Version-5 identity records the canonical declaration default independently
from each normalized actual state and mapped entity formal. The component
application differential executes the selected default through interpreter
and LLVM O0/O2, observes debugger execution points, requires cold/warm cache
behavior, edits only the default-profile source, and verifies that the owning
top/defaulted child keys change while unrelated component and direct-entity
children remain stable.

General expression or aggregate port actuals, dynamic defaults, entity-port
defaults, mixed-language default binding, incremental configurations,
complete library analysis-order semantics, and general overload resolution
remain outside this bounded slice.

The focused warnings-as-errors frontend, elaboration, diagnostic-catalog,
source-budget, configuration, and component application gates passed. The
catalog covers 1,074 production codes and the source gate covers 265 authored
files with an empty allowlist and a maximum of 1,999 lines. The exact LLVM
22.1.8 warnings-as-errors Debug regression passed all 55 configured tests in
70.73 seconds, and Release passed all 55 configured tests in 32.00 seconds on
2026-07-30. No CI state was inspected.

### Sixty-eighth feature batch — bounded SystemVerilog runtime strings

Mutable `string` module objects, automatic locals, function results/formals,
and task formals now use a distinct byte-string runtime domain. Stable
elaborated object and SimIR register IDs drive bounded value-copy storage for
literals, empty initialization, blocking assignment, concatenation,
equality/inequality, byte indexing and replacement, `len()`, `%s` output,
nested automatic functions, and suspending-task copy-out. Values persist
across native wait/resume boundaries and every object is limited to 4,096
bytes. Oversize construction, invalid indices, mixed equality, incompatible
calls, nonblocking or timed writes, and mixed-language boundaries produce
deterministic diagnostics.

The append-only plain-C JIT runtime ABI adds string operations after the
previous 352-byte tail without exposing C++ string or allocator layout.
Generated LLVM O0/O2 processes use executor-owned string registers and
operation callbacks for object access, copies, concatenation, comparison,
length, indexing, replacement, and output. Callback failures remain contained
and return a generated execution status with the current source instruction.
The versioned native-object key records string register layout, exact literal
bytes, every string operation field, the 4,096-byte bound, and transitive
callable/source provenance.

Debugger `locals` and `show` render escaped live string values, including
suspended automatic task locals. Module strings accept bounded `deposit`;
`force` is rejected explicitly because retained strings are objects rather
than resolved signals. The application differential proves interpreter,
LLVM O0, and LLVM O2 output/final-object parity, native compilation without
fallback, two cold stores followed by two warm hits, and selective
invalidation of only the module whose literal changes.

The focused warnings-as-errors frontend, runtime, elaboration, LLVM,
application, diagnostic-catalog, and source-budget gates passed. The catalog
covers 1,085 production codes and the source gate covers 270 authored files
with an empty allowlist and a maximum of 1,999 lines. The exact LLVM 22.1.8
warnings-as-errors Debug regression passed all 56 configured tests in 68.76
seconds, and Release passed all 56 configured tests in 31.25 seconds on
2026-07-30. No CI state was inspected.

### Sixty-ninth feature batch — bounded SystemVerilog text files

Same-language SystemVerilog-2017 now executes bounded `$fopen`, `$fclose`,
`$fdisplay`, `$fwrite`, `$fgets`, `$feof`, and `$ferror`. Module and automatic
`integer` values carry opaque monotonically allocated handles; the simulator
owns every host stream and rejects zero, unknown, closed, and foreign-process
handles without exposing host descriptors. Accepted modes are `r`, `w`, `a`,
`r+`, `w+`, and `a+`. Paths must be relative to the manifest directory and
remain beneath its canonical root, including through existing symlinks.

Writes flush deterministically, explicit close distinguishes ordinary EOF
state from an actual close failure, and the per-simulation table closes all
remaining streams on destruction. `$fgets` copies one text line into a
bounded mutable string, including a present newline, and returns its byte
count; EOF and retained file-error text are queryable separately. Filenames,
lines, and string output share the 4,096-byte runtime-string bound.
`$fdisplay`/`$fwrite` admit literal output or exactly one
`%b`/`%h`/`%o`/`%d`/`%c`/`%s` conversion.

Stable SimIR operations describe open, close, literal/formatted/string writes,
line reads, EOF, and error status. Six append-only plain-C callbacks follow
the mutable-string ABI tail and pass only process/instruction IDs, HDL handle
bits, and scalar results. Generated LLVM O0/O2 code delegates storage and
metadata interpretation to the application executor, contains callback
failures at the current source instruction, and never embeds `FILE*`, C++
stream/filesystem objects, descriptors, or addresses.

File calls retain debugger call safe points and automatic integer handles
remain live across task suspension, stop, inspection, and resume. Native
cache schema 22 records the `simir-text-file-v1` semantic marker and every
operation field, literal byte, register dependency, format, and source
provenance. External file contents are deliberately excluded: editing an
input file produces warm hits while changing the HDL output literal
invalidates only the owning module.

Binary I/O, standard input/output descriptor aliases, seek/tell/rewind,
multichannel descriptors, arbitrary format lists, `$readmem*`, VHDL TextIO,
mixed-language handles, unrestricted host paths, and dynamic containers
remain outside this bounded slice.

The focused warnings-as-errors frontend, runtime, elaboration, LLVM,
application, diagnostic-catalog, and source-budget gates passed. The catalog
covers 1,102 production codes and the source gate covers 276 authored files
with an empty allowlist and a maximum of 2,000 lines. The exact LLVM 22.1.8
warnings-as-errors Debug regression passed all 57 configured tests in 68.89
seconds, and Release passed all 57 configured tests in 30.72 seconds on
2026-07-30. No CI state was inspected.

### Seventieth feature batch — bounded SystemVerilog dynamic arrays and queues

Same-language SystemVerilog-2017 now retains one-dimensional integral dynamic
arrays `[]`, unbounded queues `[$]`, and bounded queues `[$:N]` as typed HIR,
elaborated objects, and automatic callable values distinct from packed vectors.
Supported element types include the existing packed `bit`/`logic`/`reg`
family and integer-family types, preserving width, signedness, and two- or
four-state domain. Multidimensional, static unpacked, associative, nonintegral,
parameter, ref, and static-lifetime forms receive bounded diagnostics.

Dynamic arrays initialize empty and support `new[size]`, whole-value copy,
replacement, element reads and writes, `size()`, and `delete()`. Queues add
`push_front`, `push_back`, `pop_front`, and `pop_back`. Every index must be
known and in range, empty pops fail deterministically, and every container is
limited to 4,096 elements. On insertion into a full bounded queue, the back
element is discarded after insertion, so an over-capacity `push_back` leaves
the prior queue unchanged while `push_front` retains the new front.

Stable elaborated object IDs and SimIR register IDs keep owned vectors,
allocators, and element addresses outside generated code. Immutable operations
cover resize, copy, object/register transfer, size, indexed reads and writes,
delete, and queue insertion/removal. One append-only plain-C callback follows
the file ABI tail and delegates every operation to common runtime storage;
native callback failures are contained at the current source instruction.

Automatic containers copy by value through nonrecursive functions and
input/output/inout task formals. Deferred task copy-out remains ordered, and
container registers remain live across delay suspension, debugger stop/read,
and resume. Debugger `show` renders module containers and `locals` renders
suspended automatic values through shared interpreter/native accessors.

Native-object schema 23 records exact container register types, queue bounds,
the 4,096-element limit, object identities, every operation field, and
transitive callable/source provenance. The standalone application differential
proves interpreter, LLVM O0, and LLVM O2 output/final-object equivalence,
native execution without fallback, task suspension and copy-out, debugger
observation, and safe-point resumption.

Associative arrays, general static unpacked arrays and memories,
multidimensional containers, arrays of strings or aggregates, additional array
methods, mixed-language transfer, and unrestricted heap behavior remain
outside this bounded slice.

The focused warnings-as-errors frontend, runtime, elaboration, LLVM,
application, diagnostic-catalog, and source-budget gates passed. The catalog
covers 1,121 production codes and the source gate covers 284 authored files
with an empty allowlist and a maximum of 2,000 lines. The exact LLVM 22.1.8
warnings-as-errors Debug regression passed all 59 configured tests in 69.02
seconds, and Release passed all 59 configured tests in 30.53 seconds on
2026-07-30. No CI state was inspected.

The required batch-70 GitHub boundary inspection subsequently examined
[run 30542845249](https://github.com/colinphill/fsim/actions/runs/30542845249)
at the then-remote batch-68 handoff. Its LLVM and Windows jobs passed, while
non-LLVM GCC Debug, Release, and ASan/UBSan failed at build time because two
cache-key helpers were compiled without the `FSIM_HAS_LLVM` uses that consume
them. The local repair conditionally compiles those helpers and changes every
workflow build from two to eight parallel workers. That workflow parallelism
was subsequently restored to two after hosted Ubuntu runners shut down under
the higher-memory build load. The formerly failing
targets and their focused tests pass locally under non-LLVM Debug, Release,
and ASan/UBSan plus exact-LLVM Debug and Release; replacement CI confirmation
initially exposed four additional cache-key helpers with the same warning in
[run 30553184827](https://github.com/colinphill/fsim/actions/runs/30553184827).
All six helpers are now conditionally compiled and the additional targets pass
the same five local configurations. The next replacement,
[run 30553851223](https://github.com/colinphill/fsim/actions/runs/30553851223),
reached the full Windows test matrices and exposed two further portability
defects: translated text streams produced CRLF bytes where SystemVerilog file
operations require exact LF bytes, and the accumulated frontend matrix
exhausted Windows' 1 MiB default stack in MSVC-compatible Debug builds. All
supported simulator file modes now use binary stream transport while retaining
their logical read/write modes, and the Windows Debug frontend test receives
the same 8 MiB stack reserve as the accumulated elaboration matrix. Focused
frontend, runtime, and SystemVerilog file tests pass under non-LLVM Debug,
Release, and ASan/UBSan plus exact-LLVM Debug and Release. After returning
GitHub-hosted builds to two workers, replacement
[run 30555745832](https://github.com/colinphill/fsim/actions/runs/30555745832)
passed all 12 Ubuntu and Windows jobs, including GCC Debug/Release,
ASan/UBSan, exact LLVM 22.1.8 Debug/Release, both MSVC-compatible toolchains,
and the frontend fuzz smoke.

### Seventy-first feature batch — bounded SystemVerilog associative arrays

Same-language SystemVerilog-2017 now retains one-dimensional associative
arrays with integral built-in or visible named packed-scalar keys as a
distinct container kind. Element and index types preserve exact width,
signedness, and two- or four-state domain in frontend HIR, elaborated object
metadata, and process-local register metadata. Wildcard and string keys,
packed aggregate keys or elements, multidimensional forms, nonautomatic
callable lifetimes, `ref` formals, and invalid container-kind method uses
receive explicit diagnostics.

Associative arrays initialize empty and support value-copy assignment,
insertion, replacement, noninserting reads, `delete()`, `delete(index)`,
`size()`, `exists(index)`, and `first`, `last`, `next`, and `prev`. Missing
reads return the element type's zero default. Unknown keys fail
deterministically. Keys remain unique in canonical numeric order, including
signed negative-before-nonnegative ordering, and insertion beyond 4,096
entries fails without exposing host maps or allocator state.

The common container value now pairs ordered keys with elements while dynamic
arrays and queues retain an empty key vector. SimIR adds immutable existence
and traversal operations plus optional indexed deletion. Traversal explicitly
models its inout key and return status as two register definitions, and LLVM
validation now tracks multiple definitions per instruction. The existing
append-only plain-C container callback is unchanged: generated code uses two
calls at a traversal instruction to retrieve the selected key and success
status, and callback failures remain contained at the current SimIR
instruction.

Automatic associative arrays copy by value through nested nonrecursive
functions and input/output/inout task formals. Copy isolation, ordered
copy-out, delay suspension, debugger stop/read/resume, and call safe points
share the existing container activation-frame path. Debugger rendering uses
`key=>value` pairs in canonical order without exposing storage identities.

Native-object schema 24 records the index and element types, associative kind,
4,096-entry limit, ordering semantic marker, optional delete index,
existence/traversal fields, and transitive source/callable provenance. The
standalone application differential proves interpreter, LLVM O0, and LLVM O2
equivalence at both optimization settings, native execution without fallback,
copy isolation, suspended task copy-out, traversal mutation, debugger
observation, and cache-backed execution through the established project path.

The focused warnings-as-errors frontend, runtime, elaboration, LLVM,
application, diagnostic-catalog, and source-budget gates passed. The catalog
covers 1,130 production codes and the source gate covers 284 authored files
with an empty allowlist and a maximum of 2,000 lines. The exact LLVM 22.1.8
warnings-as-errors Debug regression passed all 59 configured tests in 70.86
seconds, and Release passed all 59 configured tests in 31.94 seconds on
2026-07-30. Batch 71 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-second feature batch — bounded SystemVerilog static memories

Same-language SystemVerilog-2017 now retains one-dimensional locally constant
static unpacked arrays as a fixed container kind distinct from packed vectors,
dynamic arrays, queues, and associative arrays. Frontend HIR preserves both
specialization-dependent and concrete left/right bounds. Elaboration folds
parameterized bounds to signed 32-bit indices, rejects ranges above 4,096
elements, and carries exact direction plus element width, signedness, and
two-/four-state domain into module objects and process registers.

Fixed storage is materialized densely in declared-index order. Two-state
elements default to zero and four-state elements default to X. Known signed
indices map through the retained left/right bounds, while unknown or
out-of-range indices fail without aliasing another element. Whole-value copy
requires exact range and element metadata. Resize and delete mutation remain
limited to their legal dynamic container kinds.

`$readmemb` and `$readmemh` are explicit HIR and SimIR operations with a
bounded string path, direct fixed-array target, and optional signed 32-bit
start/finish registers. The shared parser accepts whitespace, line and block
comments, hexadecimal `@` address directives, underscores, binary or
hexadecimal data, width truncation/zero extension, and exact X/Z/? states. It
uses the existing manifest-root-confined file service, caps source text at
1 MiB, defaults unaddressed data to numerically increasing indices, follows
explicit start/finish direction, and never exposes paths, streams, storage
addresses, or allocators to generated code.

Interpreter and native execution share the fixed storage and memory-text
semantics. The existing append-only container callback remains ABI-stable;
generated code supplies only optional scalar bounds while the callback reads
the immutable path-register ID from SimIR. Debugger rendering includes each
declared fixed index. Automatic fixed arrays use the existing value-copy
function/task frames, ordered inout copy-out, delay suspension, safe points,
and interpreter/native local inspection.

Native-object schema 25 records fixed kind, signed bounds, every memory-load
operation field, the 4,096-element and 1 MiB limits, semantic markers, and
transitive source/callable provenance while deliberately excluding external
memory-file contents. Frontend, runtime, elaboration, and standalone
application fixtures cover ascending/descending and parameterized ranges,
defaults, whole copy, invalid mutation/index/bounds, binary/hex input,
comments, addresses, exact X/Z states, interpreter/LLVM O0/O2 equivalence,
suspended automatic copy-out, debugger rendering, and cache-backed execution.

The diagnostic catalog covers 1,142 production codes and the source gate
covers 284 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 71.78 seconds, and Release passed all 59 tests in 32.63 seconds on
2026-07-30. Batch 72 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-third feature batch — bounded SystemVerilog static-array ports

SystemVerilog-2017 module headers now retain one-dimensional locally constant
static unpacked integral arrays as typed ANSI ports. Basic non-ANSI header
placeholders are refined by matching body declarations without becoming
module variables. Dynamic arrays, queues, associative arrays, extra unpacked
dimensions, and nonintegral elements remain rejected at this boundary.

Port specialization preserves the packed element width, two-/four-state
domain, signedness, and exact signed 32-bit left/right unpacked bounds.
Same-language direct whole-array actuals must match that complete runtime
profile. Expressions, selected elements, slices, missing objects, mismatched
ranges or directions, mixed-language transfer, and independent sibling
output/inout drivers receive stable diagnostics rather than being flattened or
implicitly converted.

The hierarchy builder now carries a container alias map independently from
packed-signal aliases. Input formals use read-only object aliases; output and
inout formals use deterministic mutable aliases. Nested pass-through aliases
are distinguished from independent multiple drivers, so generated child
instances can read elements, copy whole values, suspend, and write parent
memories without allocating duplicate storage. Attempts to assign, pop,
delete, or load memory through an input formal fail during lowering.

Elaborated designs retain every canonical container hierarchy path and expose
path lookup separately from the unique owned-object inventory. Debugger scope
discovery and `show` therefore render a child formal with its declared indices
while retaining the same opaque container ID as the parent actual. No vector,
allocator, element address, or packed-signal reinterpretation enters SimIR or
the native ABI.

Native-object schema 26 records the changed static-container port-alias
semantics while preserving fixed type, operation, object-ID, callable, source,
and specialization identity. The standalone container fixture executes a
two-module parameterized hierarchy through the reference interpreter and
native LLVM O0/O2, including whole-array input-to-output copy, output/inout
element mutation after delay suspension, debugger child-scope lookup, cache
use, and exact final parent objects without fallback.

Frontend evidence covers ANSI and non-ANSI retention plus unsupported dynamic
port kinds. Elaboration evidence covers independently parameterized
ascending/descending formal and actual ranges, nested generated hierarchy,
opaque alias identity, input protection, incompatible ranges, expressions,
missing objects, multiple drivers, and explicit mixed-language rejection.

The diagnostic catalog covers 1,151 production codes and the source gate
covers 285 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 75.86 seconds, and Release passed all 59 tests in 33.93 seconds on
2026-07-30. Batch 73 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-fourth feature batch — bounded SystemVerilog dynamic-container ports

SystemVerilog-2017 module headers now retain dynamic arrays, unbounded and
bounded queues, and integral-key associative arrays as typed ANSI ports.
Basic non-ANSI body declarations refine matching header placeholders without
becoming module variables. The existing one-dimensional and integral-element
limits remain common to local objects, callable formals, and module ports.

Port specialization preserves the exact element width, two-/four-state domain,
signedness, dynamic/queue/associative kind, optional bounded-queue maximum,
and associative index width/domain/signedness. Value-parameter queue limits
and named type-parameter index aliases forward through nested independently
parameterized modules. Formal and actual runtime profiles must match exactly;
there is no implicit kind, bound, state, width, or signedness conversion.

Direct same-language whole-container actuals reuse the opaque object-ID and
hierarchy-path aliasing introduced for static memories. Input formals are
read-only aliases. Output and inout formals are deterministic mutable aliases
for whole-value assignment, allocation/resizing, element mutation, queue
insertion/deletion, associative insertion/deletion/traversal, and automatic
task copy-out. Nested pass-through output/inout paths remain legal while
independent sibling drivers are diagnosed.

Bounded-queue limit lowering now accepts the complete specialized typed
constant form rather than only simple decimal syntax. Associative reads,
writes, and `exists` resize integral indices to the exact resolved index width,
closing the non-32-bit named-index execution gap exposed by the hierarchy
fixture. Four-bit signed keys execute in canonical numeric order through both
the interpreter and native backend.

Debugger scope discovery and `show` expose dynamic-container formals at every
nested/generated alias path while keeping their parent object IDs opaque.
Native-object schema 27 records the dynamic port-alias semantics and exact
queue/index profiles without changing the append-only runtime callback ABI.
The application fixture executes a parameterized generated hierarchy through
interpreter and LLVM O0/O2, including input reads, bounded-queue and
associative traversal, output/inout mutation, a suspending automatic task,
cold/warm cache reuse, debugger aliases, and exact final parent objects with no
process fallback.

Negative evidence covers expressions/selections, missing actuals, kind,
element, queue-bound, and associative-index mismatches, input allocation or
descendant mutation, independent multiple drivers, and explicit mixed-language
transfer. Frontend evidence covers all four dynamic kinds in ANSI and basic
non-ANSI declarations.

The diagnostic catalog covers 1,150 production codes and the source gate
covers 285 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 78.04 seconds, and Release passed all 59 tests in 35.05 seconds on
2026-07-30. Batch 74 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-fifth feature batch — bounded SystemVerilog unpacked-container queries

SystemVerilog-2017 direct one-dimensional integral static arrays, dynamic
arrays, unbounded and bounded queues, and integral-key associative arrays now
use the existing typed call HIR for `$left`, `$right`, `$low`, `$high`,
`$increment`, `$size`, `$dimensions`, `$unpacked_dimensions`, and `$bits`.
The bounded form accepts an optional locally constant dimension only where the
query permits it and only when its value is the unpacked dimension `1`.

Static-array queries fold from the specialized runtime type without reading
container storage. They preserve the exact signed left/right bounds and
ascending/descending increment convention, derive low/high and element count,
multiply the count by the packed element width for `$bits`, and report two
total dimensions with one unpacked dimension. Both descending parameterized
ports and ascending signed-index arrays have positive runtime evidence.

Dynamic arrays and queues retain left/low `0` and increment `-1`, then compose
the existing `ContainerSize` SimIR operation with signed subtraction for
right/high or unsigned multiplication for `$bits`. Empty right/high is
deterministically `-1`, while populated values track allocation and queue
mutation. Associative arrays expose current entry-count `$size`, entry-width
`$bits`, and dimension counts; finite-bound queries fail rather than inventing
an ordering bound.

The query lowerer resolves the same container register or opaque object ID
already used by mutation and traversal. Module objects, automatic function and
task formals, task locals retained across suspension, static and dynamic port
aliases, and nested/generated child paths therefore observe one coherent
value without duplicated storage. Existing packed query behavior remains
available for packed operands, while visible SystemVerilog typedef/type
parameter marks provide a stable rejection path for unsupported type-only
container forms.

No new SimIR operation or native callback was required. Runtime queries reuse
validated `ContainerSize`, `Binary`, and constant operations already shared by
the interpreter and LLVM lowerer. Native-object schema 28 records the expanded
container-query semantics and prevents reuse of older objects while retaining
canonical type, operation, object, callable, source, and specialization
identity.

Positive evidence covers typed frontend calls, static folding, dynamic empty
and populated results, queue and associative values, ascending/descending
ranges, parameterized port aliases, callable formals, a suspended automatic
local, assertions, formatted output, debugger-visible stored results,
interpreter, LLVM O0/O2, and cold/warm cache execution. Stable diagnostics
cover invalid dimensions and arity, associative finite bounds, and type-only
forms; the existing frontend matrix continues to reject multidimensional and
unsupported container declarations.

The diagnostic catalog covers 1,154 production codes and the source gate
covers 285 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 86.99 seconds, and Release passed all 59 tests in 34.74 seconds on
2026-07-30. Batch 75 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-sixth feature batch — bounded SystemVerilog container assignment patterns

SystemVerilog-2017 apostrophe-brace expressions now retain ordered positional
or keyed association metadata as aggregate HIR rather than being parsed as
packed concatenations. The bounded executable form is contextual: it appears
on the right side of a blocking assignment to a direct whole
one-dimensional integral static array, dynamic array, queue, bounded queue, or
integral-key associative array.

Static patterns require exactly the specialized element count and map members
from the declared left bound toward the right bound, preserving ascending and
descending signed indices. Dynamic arrays resize to the positional member
count. Queues append members in source order and reject counts above the
specialized bounded-queue maximum or common 4,096-element limit.
Associative patterns require keyed members whose expressions fold to known
integral values; keys convert to the exact specialized index width and are
rejected when conversion makes two keys equal.

Every element lowers with the target's exact width, signedness, and
two-/four-state domain. Empty patterns deterministically clear dynamic arrays,
queues, bounded queues, and associative arrays. Positive evidence includes
signed integral members, exact X/Z preservation, static arrays in both
directions, dynamic and bounded queue sizing, canonical associative ordering,
static and dynamic port aliases, automatic function/task values, and a task
local populated after suspension.

Pattern construction is atomic with respect to the destination. The lowerer
allocates a temporary container register of the exact target type, fills it
with existing `ResizeContainer`, `PushContainer`, and `ContainerWrite`
operations, then applies one `CopyContainerRegister` and optional
`WriteContainerObject`. No new SimIR operation or native callback was needed,
and no vector, allocator, element address, or host identity enters generated
code.

Stable frontend or elaboration diagnostics cover malformed braces,
Verilog-2005 use, missing context, keyed members for non-associative targets,
positional members for associative targets, mixed associations, unsupported
`default`, converted duplicate or nonconstant keys, static count and queue
capacity mismatch, and indirect targets. Existing container declaration
checks continue to reject multidimensional, aggregate/string-element, and
unsupported index forms.

Native-object schema 29 records assignment-pattern semantics while existing
canonical operation/type/object/callable/source/specialization serialization
drives cache identity. The standalone container application exercises
patterns through assertions, formatted state, debugger-visible objects,
interpreter, LLVM O0/O2, suspended tasks, nested/generated port aliases, and
cold/warm cache reuse without fallback.

The diagnostic catalog covers 1,161 production codes and the source gate
covers 285 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 87.04 seconds, and Release passed all 59 tests in 34.08 seconds on
2026-07-30. Batch 76 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-seventh feature batch — bounded SystemVerilog unpacked-container reductions

SystemVerilog-2017 direct one-dimensional integral static arrays, dynamic
arrays, unbounded and bounded queues, and integral-key associative arrays now
accept no-argument `sum`, `product`, `and`, `or`, and `xor` methods in
expressions. The parser retains each method as an explicit call HIR, including
the keyword-named bitwise methods, and the expression type remains the exact
packed element width, signedness, and two-/four-state domain.

The runtime representation already stores static elements in declared
left-to-right order, dynamic and queue elements in current index order, and
associative elements paired with canonically sorted numeric keys. A shared
reduction kernel scans that storage without a copy. Empty containers begin
from an exact-width identity: zero for sum/or/xor, one for product, and all
ones for and. Arithmetic accumulation reuses fixed-width common packed
semantics and becomes all unknown after an X/Z operand; bitwise accumulation
uses the common per-bit four-state truth tables.

The new typed `ContainerReduction` SimIR operation carries only operator,
destination register, and source container register. Validation constrains the
result to the source element width and rejects invalid operation tags. The
reference interpreter and LLVM backend share the runtime kernel through the
existing generic container callback, so the append-only public native ABI did
not gain a slot or address-bearing representation. Native-object schema 30
serializes the selected reduction and changed semantics while preserving
canonical container type, object, callable, source, and specialization
identity.

Positive evidence covers all five operations, exact signed byte and integer
results, empty identities, arithmetic and bitwise X/Z propagation, static,
dynamic, queue, bounded-queue, and associative storage, module objects,
static/dynamic port aliases, generated hierarchy, automatic callable formals
and locals after suspension, assertions, formatted output, debugger-visible
stored results, interpreter, LLVM O0/O2, and cold/warm cache reuse. Stable
frontend or elaboration diagnostics cover arguments, `with` clauses,
noncontainer and indirect receivers, standalone result calls, and use outside
SystemVerilog-2017; existing declaration diagnostics retain the bounded
one-dimensional integral scope.

The diagnostic catalog covers 1,166 production codes and the source gate
covers 285 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 41.55 seconds, and Release passed all 59 tests in 14.05 seconds on
2026-07-30. Batch 77 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-eighth feature batch — bounded SystemVerilog unpacked-container ordering

SystemVerilog-2017 direct one-dimensional integral static arrays, dynamic
arrays, unbounded queues, and bounded queues now accept no-argument
`reverse`, `sort`, and `rsort` method statements. The parser retains the
method call explicitly, and elaboration restricts mutation to a direct
writable receiver while preserving exact element type, container size, queue
bound, register type, and object identity. Associative arrays remain excluded.

Static-array storage is already in declared left-to-right order; dynamic
arrays and queues are already in current index order. `reverse` therefore
permutes only the element sequence. `sort` and `rsort` use a stable
most-significant-bit-first comparator. Unsigned and non-sign bits rank
`0 < 1 < X < Z`; a signed sign bit ranks `1 < 0 < X < Z`. Known signed
values consequently retain two's-complement numeric order, unknown sign states
have deterministic positions, and exact duplicates retain source order.

The new `OrderContainer` SimIR operation carries only an ordering enum and
target container register. Validation rejects invalid tags and associative
targets. The reference interpreter and LLVM backend share the runtime kernel
through the existing generic container callback, and module-object or port
receivers use the existing explicit writeback. No public native ABI slot,
allocator identity, address, or host container representation was added.
Native-object schema 31 serializes the selected operation and comparator
policy while preserving canonical type, object, callable, source, and
specialization identity.

Positive evidence covers reversal, ascending and descending stable order,
known signed bytes, duplicates, all four logic states, static arrays, dynamic
arrays, queues, bounded queues, module objects, output/inout port aliases,
nested/generated hierarchy, automatic task formals and locals after
suspension, debugger stop/resume, interpreter, LLVM O0/O2, and cold/warm cache
reuse. Stable frontend or elaboration diagnostics cover arguments, `with`
clauses, associative/noncontainer/indirect/read-only receivers,
expression-result use, `shuffle`, and use outside SystemVerilog-2017.

The diagnostic catalog covers 1,173 production codes and the source gate
covers 285 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 114.15 seconds, and Release passed all 59 tests in 41.30 seconds on
2026-07-30. Batch 78 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Seventy-ninth feature batch — bounded SystemVerilog unpacked-container locators

SystemVerilog-2017 direct one-dimensional integral static arrays, dynamic
arrays, unbounded queues, and bounded queues now support no-argument `min`,
`max`, `unique`, and `unique_index` expressions in compatible whole-queue
assignments. The parser retains all four calls explicitly, including the
reserved-keyword spelling of `unique`, and contextual lowering distinguishes
exact-element value queues from signed two-state 32-bit index queues.
Associative receivers and general expression contexts remain excluded.

Extrema reuse Batch 78's deterministic exact-width comparator and return an
empty queue or one first-occurring minimum/maximum. `unique` compares complete
four-state values and preserves the first occurrence of every distinct value.
`unique_index` emits the corresponding signed declared static index or current
dynamic/queue index in the same stable order. Results obey the common
4,096-element limit and an optional bounded destination capacity. The kernel
copies source elements before clearing the result, making an in-place queue
assignment deterministic.

The typed `LocateContainer` SimIR operation carries locator enum, destination
container register, and source container register. Validation checks the enum,
receiver kind, queue result kind, and exact value/index element profile. The
reference interpreter and LLVM backend share one runtime kernel through the
existing generic container callback, so the public native ABI remains
unchanged. Native-object schema 32 serializes locator and result semantics.
The LLVM runtime-error and symbol validation helpers were split into a focused
compilation unit, reducing the main validator from its 2,000-line ceiling.

Positive evidence covers empty results, signed extrema, exact duplicates,
X/Z identity and ordering, negative declared static indices, dynamic/current
indices, bounded and aliased queues, module objects, generated input and
inout port aliases, automatic task formals and locals after suspension,
debugger stop/resume, interpreter, LLVM O0/O2, and cold/warm cache reuse.
Stable frontend or elaboration diagnostics cover arguments, predicate `with`
clauses, associative/noncontainer/indirect receivers, incompatible and scalar
result contexts, discarded results, and use outside SystemVerilog-2017.

The diagnostic catalog covers 1,180 production codes and the source gate
covers 286 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 130.48 seconds, and Release passed all 59 tests in 43.77 seconds on
2026-07-30. Batch 79 is not a ten-batch CI-inspection boundary, so no GitHub
Actions run was inspected.

### Eightieth feature batch — bounded SystemVerilog predicate locators

SystemVerilog-2017 direct one-dimensional integral static arrays, dynamic
arrays, unbounded queues, and bounded queues now support `find`,
`find_index`, `find_first`, `find_first_index`, `find_last`, and
`find_last_index` with one required parenthesized `with` predicate. The parser
retains receiver and predicate expression explicitly. Contextual lowering
requires an exact-element queue for value results or a signed two-state
32-bit queue for index results; associative and indirect receivers remain
outside this bounded slice.

One lexically scoped integral `item` iterator, element-convertible locally
constant operands, equality/inequality, signedness-aware relations, and
logical `&&`, `||`, and `!` lower to an immutable source-ordered graph of at
most 64 nodes. Function calls, side effects, `item.index`, arithmetic
involving `item`, nonconstant external operands, and broader expression
families remain deferred. Runtime evaluation uses exact four-state operations:
only a scalar one matches, while X/Z predicate results are false.

`find`/`find_index` preserve declared static or current dynamic/queue order.
First and last forms return at most one matching value or signed index, with
last forms scanning from the final source element. Empty inputs return empty
queues, bounded destinations truncate deterministically, and source elements
are copied before result replacement for alias safety. Module objects, direct
port aliases, and automatic callable values use the existing container
register/object paths.

The existing typed `LocateContainer` SimIR operation now carries validated
predicate nodes. The interpreter and LLVM application executor share the same
runtime kernel through the existing generic container callback, so the public
native ABI is unchanged. Native-object schema 33 serializes the locator mode,
every graph operator and edge, and each exact constant; dedicated cold/warm
tests prove that predicate constant or operator changes invalidate identity.
Stable frontend and elaboration diagnostics cover language, syntax, missing
or empty clauses, result typing/context, receiver kind, iterator scope,
constant conversion, unsupported expressions, discarded results, and graph
bounds.

Positive evidence covers all six methods, compound signed predicates, X/Z
false selection, empty and bounded results, negative declared static indices,
current indices, aliased queues, objects, ports, callable values,
nested/generated hierarchy, interpreter, LLVM O0/O2, and cold/warm cache
identity. The focused frontend, elaboration, runtime, LLVM, and application
container gate passed before the full regression.

The diagnostic catalog covers 1,190 production codes and the source gate
covers 286 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 143.25 seconds, and Release passed all 59 tests in 46.82 seconds on
2026-07-30.

Batch 80's required GitHub Actions inspection traced an MSVC Debug
`fsim.application.scoped_locals` timeout to Batch 72's inline recursive AST
growth. Memory-load operands now reuse existing statement slots, and
static-range expressions use vector-backed zero-or-one storage, preserving
deep value-copy semantics while reducing recursive statement and type frames.
The scoped test then passed in 0.36 seconds on the final Windows runner. The
accumulated monolithic application fixture receives the same 8 MiB MSVC Debug
stack budget as the frontend and elaboration matrices and passed in 80.55
seconds. Retained 60-second scoped and 600-second application test bounds keep
future regressions diagnostic. Final non-documentation run `30594329846`
passed all 12 Linux and Windows jobs; its two-worker ASan/UBSan job took 30
minutes 58 seconds, so the 45-minute CI job budget remains necessary. Batch 80
is closed, and Batch 81 is the next implementation batch.

### Eighty-first feature batch — named locator iterators and predicate indices

Predicate locators now accept either the existing implicit `item` or one
explicit iterator identifier in the method argument list. The parser retains a
named iterator as its own source-spanned Identifier operand between receiver
and predicate, removes it from Verilog implicit-net discovery, exposes it only
while parsing the associated `with` predicate, and rejects malformed,
multiple, colliding, or leaking bindings. The recursive `Expression` layout is
unchanged, preserving Batch 80's bounded MSVC Debug parser frames.

Exact iterator value references retain the source element profile. A direct
`.index` reference lowers to a distinct signed two-state 32-bit predicate leaf.
Static arrays project storage offsets back to signed declared indices;
dynamic arrays, unbounded queues, and bounded queues use current zero-based
positions. Equality, inequality, signed relations, and logical `&&`, `||`, and
`!` compose index comparisons with locally constant signed-32 operands.
Unknown iterator references, indirect index selection or calls, and mixed
element/index comparisons fail deterministically. Batch 80's element
predicate conversion, four-state X/Z-false selection, result typing, stable
first/last traversal, bounded capacity, and alias-safe replacement remain
unchanged.

`ContainerPredicateNode` now carries an explicit element, index, or logical
value kind, and the bounded graph adds an index operator. Both the LLVM
validator and common runtime evaluator check every leaf, constant,
comparison, and logical result profile. The evaluator receives the already
canonical declared/current index from the locator kernel; the application
executor continues through the existing generic container callback, so the
public native ABI is unchanged. Native-object schema 34 serializes every value
kind and index-node structure while intentionally excluding iterator spelling.
Elaboration proves two differently named iterators lower to identical graphs,
and cold/warm cache tests distinguish element and index profiles.

Positive evidence covers implicit and named binders, source spans, exact
element typing, negative declared static indices, dynamic/current indices,
unbounded and bounded queues, direct container ports, generated hierarchy,
automatic task formals across suspension, equality/inequality/relations and
logical composition, interpreter, LLVM O0/O2, and cache identity. Negative
evidence covers non-Identifier and multiple binders, collision and leakage,
unknown references, index selection/call misuse, associative receivers, mixed
profiles, malformed SimIR value kinds, and the retained 64-node bound.

The focused seven-test Debug and Release gates passed before the full
regression. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed
all 59 tests in 148.82 seconds, and Release passed all 59 tests in 45.36
seconds on 2026-07-30. The diagnostic catalog covers 1,193 production codes,
and the source gate still covers 286 authored files with an empty allowlist and
a 2,000-line maximum. Batch 81 is not a ten-batch GitHub CI-inspection
boundary, so no Actions run was inspected.

### Eighty-second feature batch — bounded reduction `with` transformations

SystemVerilog `sum`, `product`, `and`, `or`, and `xor` now retain one optional
parenthesized `with` transformation as explicit source-spanned HIR. The
transformation binds only implicit `item` and direct signed two-state 32-bit
`item.index`; parser scope suppression prevents either reference from becoming
an implicit net, while later undeclared use diagnoses leakage. Named reduction
iterators remain outside this bounded slice.

Lowering reuses the bounded typed container-expression graph introduced for
predicate locators. Element, index, and logical value kinds remain explicit;
one added conditional node records condition, true, and false edges, and the
validated final node must match the receiver's exact element width, state
domain, and signedness. The supported pure form admits direct `item`, locally
constant element alternatives, signed index comparisons, logical composition,
and one conditional element selection. Arithmetic or calls involving the
iterator, side effects, nonconstant operands, indirect index selection,
mixed element/index comparison profiles, multiple conditionals, non-element
roots, graphs beyond 64 nodes, associative receivers, and the broader excluded
container/element families fail through stable diagnostics.

The shared reduction kernel evaluates a retained graph once per source element
before applying the existing exact-width reduction. Static arrays project
storage offsets to signed declared indices, while dynamic arrays and queues
use current zero-based positions. Empty reductions preserve their existing
operation identities. Conditional selection applies SystemVerilog truth
conversion and bitwise X/Z alternative merging, then the existing arithmetic
or bitwise kernel preserves four-state propagation. Receivers remain
nonmutating and coherent across module objects, direct ports,
nested/generated hierarchy, automatic callable values, and suspended tasks.

Both the reference interpreter and native executor call that common kernel.
The LLVM validator checks every typed leaf, constant, edge, comparison,
logical result, conditional branch, root, and bound before execution; the
public append-only native ABI remains unchanged. Native-object schema 35
records transformation absence/presence, every typed operator and edge
including the third conditional edge, and exact constant payloads. Dedicated
cold/warm tests distinguish no transformation, exact constants, and
element-versus-index profiles.

Positive evidence covers all five operations, no-`with` compatibility, empty
identities, four-state conditional merging, negative declared static indices,
dynamic and queue current indices, bounded queues, signed byte and integer
elements, direct ports, generated hierarchy, functions, suspended tasks,
interpreter, LLVM O0/O2, and cache identity. Negative evidence covers
malformed/empty clauses, leakage, named/argument forms, unsupported
expressions, calls and nonconstant operands, invalid index selection,
mixed profiles, nested conditionals, wrong roots/branches, associative
receivers, malformed edges, and oversized metadata.

The diagnostic catalog now covers 1,198 production codes and the source gate
still covers 286 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 161.02 seconds, including scoped locals in 0.88 seconds and the
expanded container differential in 91.71 seconds. Release passed all 59 tests
in 48.28 seconds, including scoped locals in 0.79 seconds and containers in
17.85 seconds, on 2026-07-30. Batch 82 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run is required.

### Eighty-third feature batch — bounded ordering `with` keys

SystemVerilog `sort` and `rsort` now retain one optional parenthesized `with`
key as explicit source-spanned method-statement HIR. The clause binds implicit
`item` or one optional named iterator and its direct signed two-state 32-bit
`.index`; parser scope suppression prevents either name from becoming an
implicit net, while stable diagnostics cover malformed, colliding, missing,
empty, and leaked binders. `reverse` remains no-argument, and `shuffle`
remains outside the deterministic subset.

Lowering reuses the bounded typed container-expression graph. Element, index,
and logical profiles remain explicit, the final key node must match the
receiver's exact integral element type, and at most one conditional key
selection is retained. Direct iterator values, locally constant alternatives,
signed index and element comparisons, and logical composition are supported.
Arithmetic or calls involving the iterator, side effects, nonconstant
operands, indirect index selection, mixed profiles, multiple conditionals,
non-element roots, graphs beyond 64 nodes, associative receivers, and the
broader excluded container/element families fail deterministically.

The shared ordering kernel computes and stores every key before sorting. It
uses each element's original signed declared static index or original current
dynamic/queue index exactly once, then stable-sorts `{element, key}` records
ascending or descending. Equal keys therefore preserve original order.
Existing exact-width signed/unsigned four-state comparison supplies the same
deterministic `0/1/X/Z` total order as no-key ordering. No-key `sort`/`rsort`
and no-argument `reverse` preserve Batch 78 mutation, bounds, object/port
coherence, callable state, and suspension behavior.

Both the reference interpreter and native executor call the common kernel.
The LLVM validator checks key presence versus ordering mode plus every typed
leaf, constant, edge, comparison, logical result, conditional branch, root,
and graph bound before execution. The public append-only native ABI is
unchanged. Native-object schema 36 records key absence/presence, ordering
mode, every typed operator and edge including the third conditional edge, and
exact constant payloads while intentionally excluding iterator spelling.
Cold/warm tests distinguish constants, element/index profiles, conditional
edges, key presence, and ascending/descending mode.

Positive evidence covers implicit and named keys, no-key compatibility,
original-index evaluation, stable duplicates, signed declared and current
indices, four-state X/Z keys, static and dynamic arrays, queues and bounded
queues, objects, direct ports, hierarchy, automatic task values across
suspension, interpreter, LLVM O0/O2, validation, and cache identity. Negative
evidence covers malformed/empty clauses, invalid binders and leakage,
collision, unsupported expressions and calls, nonconstant operands, mixed
profiles, nested conditionals, wrong roots/edges, reverse-key metadata,
associative and read-only receivers, and oversized graphs.

The diagnostic catalog now covers 1,204 production codes and the source gate
still covers 286 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 175.86 seconds, including scoped locals in 0.90 seconds and the
expanded container differential in 107.20 seconds. Release passed all 59
tests in 52.64 seconds, including scoped locals in 0.81 seconds and containers
in 22.11 seconds, on 2026-07-30. Batch 83 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run is required.

### Eighty-fourth feature batch — bounded locator `with` transformations

SystemVerilog `min`, `max`, `unique`, and `unique_index` now retain one
optional parenthesized `with` transformation as explicit source-spanned call
HIR. The clause binds implicit `item` or one optional named iterator and its
direct signed two-state 32-bit `.index`. Parser scope suppression prevents
either name from becoming an implicit net, while stable diagnostics cover
malformed, colliding, missing, empty, and leaked bindings.

Lowering reuses the bounded typed container-expression graph under an explicit
locator-transformation purpose, keeping it distinct from `find*` predicates,
reduction transformations, and ordering keys. Element, index, and logical
profiles remain explicit; the final node must match the receiver's exact
integral element type, and at most one conditional key selection is retained.
Direct iterator values, locally constant alternatives, signed index and
element comparisons, and logical composition are supported. Arithmetic,
calls, side effects, nonconstant operands, indirect index selection, mixed
profiles, multiple conditionals, non-element roots, graphs beyond 64 nodes,
associative receivers, and the broader excluded container/element families
fail deterministically.

The shared locator kernel snapshots the source and computes every transformed
key before clearing or replacing the destination. It evaluates each key once
with the element's original signed declared static index or original current
dynamic/queue index. Extrema compare transformed keys through the existing
exact signed/unsigned four-state total order but return the first original
element with the selected key. Uniqueness compares exact transformed-key
identity and returns the first original element or signed original index for
each key. No-`with`, empty, bounded-capacity, exact result typing, source
nonmutation, and aliased queue assignment remain compatible with Batch 79.

`LocateContainer` now carries separate predicate and transformation graphs.
Both the reference interpreter and native executor invoke the common kernel.
LLVM validation rejects malformed graphs and transformations attached to
predicate locators before execution; the public append-only native ABI remains
unchanged. Native-object schema 37 records transformation absence/presence,
locator mode, every typed operator and edge including the third conditional
edge, and exact constant payloads while intentionally excluding iterator
spelling. Cold/warm cache tests distinguish presence, constants,
element/index profiles, conditional edges, and locator mode.

Positive evidence covers all four methods, implicit and named transformations,
no-`with` compatibility, empty sources, exact four-state extrema, transformed
first-occurrence identity, negative declared and current indices, destination
capacity, transformed alias safety, static and dynamic arrays, queues and
bounded queues, objects, direct ports, generated hierarchy, automatic task
values across suspension, interpreter, LLVM O0/O2, validation, and cache
identity. Negative evidence covers malformed/empty clauses, invalid binders
and leakage, collision, unsupported expressions and calls, nonconstant
operands, mixed profiles, nested conditionals, wrong roots/edges,
predicate-locator transformation metadata, associative receivers, and
oversized graphs.

The diagnostic catalog now covers 1,209 production codes and the source gate
still covers 286 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 199.60 seconds, including scoped locals in 0.89 seconds and the
expanded container differential in 131.20 seconds. Release passed all 59
tests in 57.57 seconds, including scoped locals in 0.80 seconds and containers
in 26.89 seconds, on 2026-07-30. Batch 84 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run is required.

### Eighty-fifth feature batch — named reduction transformation iterators

SystemVerilog `sum`, `product`, `and`, `or`, and `xor` transformations now
accept one optional iterator identifier in the method argument list. The
parser retains that binder as an explicit source-spanned HIR operand between
the receiver and transformation, removes it from implicit-net discovery, and
exposes it only while parsing the associated `with` expression. A binder
without `with`, a non-Identifier or multiple binders, collision, and leakage
fail through stable diagnostics. Implicit `item` remains fully compatible.

Lowering binds the named value to the receiver's exact integral element
profile and direct `.index` to the existing signed two-state 32-bit leaf.
Static arrays continue to project signed declared indices, while dynamic
arrays and queues expose current zero-based positions. Named and implicit
spellings use the same bounded typed graph lowerer and produce identical
source-ordered operator, edge, constant, and value-kind metadata. The existing
pure expression limits, one-conditional rule, exact element root, 64-node
bound, and associative/excluded-family rejection remain unchanged.

No runtime or native ABI extension is needed because iterator spelling is
lexical frontend information and does not survive into `ContainerReduction`.
The shared interpreter/native kernel therefore preserves all five operations,
empty identities, exact-width arithmetic and bitwise behavior, four-state
conditional merging, receiver nonmutation, and declared/current index
evaluation. Schema 37 intentionally remains current: it already serializes
the complete semantic transformation graph, operation, and provenance while
excluding nonsemantic binder spelling. Existing cold/warm cache tests continue
to distinguish every semantic graph change.

Positive evidence covers explicit source spans, implicit/named graph equality,
all five operators, exact signed and four-state values, empty sources,
negative declared and current indices, unbounded and bounded containers,
module objects, direct static/dynamic ports, nested/generated hierarchy,
automatic functions, tasks across suspension, interpreter, LLVM O0/O2, and
cold/warm application builds. Negative evidence covers non-Identifier and
multiple binders, a binder without `with`, collision and leakage, unknown or
indirect references, mixed profiles, arithmetic, calls, side effects, wrong
roots, associative receivers, and the retained metadata bounds.

The diagnostic catalog now covers 1,211 production codes and the source gate
still covers 286 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 204.23 seconds, including scoped locals in 0.91 seconds and the
expanded container differential in 132.53 seconds. Release passed all 59
tests in 60.69 seconds, including scoped locals in 0.80 seconds and containers
in 27.12 seconds, on 2026-07-30. Batch 85 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run is required.

### Eighty-sixth feature batch — bounded static assignment-pattern defaults and index keys

SystemVerilog assignment-pattern parsing now retains `default` associations as
explicit source-spanned `DefaultChoice` nodes rather than ordinary
identifiers. This adds no vector or recursively embedded expression to the
common `Expression` footprint: the marker occupies the existing
aggregate-choice expression storage beside ordered `@key` metadata. Stable
parser diagnostics cover a missing colon after `default` and a missing
association value.

Direct whole assignment to a one-dimensional integral static array accepts
either the existing exact-count positional form or exactly one default member
plus zero or more explicit integral index members. A default-only pattern
fills the complete array. Positional associations cannot mix with keyed or
default associations, and keyed static patterns without a default or with
duplicate defaults fail deterministically. Dynamic arrays and queues retain
positional sizing, associative arrays retain their keyed semantics, and
defaults remain excluded from those kinds.

Explicit static keys use bounded SystemVerilog constant evaluation and must be
known. Their low 32 bits convert to the signed declared-index profile before
range and uniqueness checks, so spellings such as unsigned
`32'hffffffff` select declared index `-1` and collide with an explicit `-1`.
Converted keys map through the existing direction-aware fixed-container index
kernel for both ascending and descending declarations.

Member value expressions lower in source order through the destination's
exact element width, signedness, and two-/four-state domain. Construction then
fills every unmentioned declared index from the one default register before
applying explicit members in their retained source order. The existing typed
temporary remains the only construction target; one
`CopyContainerRegister`, followed by ordinary object writeback when needed,
atomically replaces the destination. No new SimIR operation, runtime callback,
allocator identity, or public ABI slot was required.

Positive evidence covers default-only and keyed/default patterns, default
placement before, between, and after keys, parameter keys, signed `-1`
conversion, ascending and descending ranges, exact bit/logic conversion,
four-state X/Z defaults, legacy positional and associative behavior, module
objects, writable direct static ports, nested/generated aliases, automatic
function values, and inout tasks after suspension. A dedicated elaboration
fixture proves three temporary writes precede one whole copy with the exact
fixed type. The standalone application agrees across interpreter and LLVM
O0/O2 with cold/warm cache reuse.

Negative evidence covers missing punctuation and values, missing or duplicate
defaults, nonconstant and unknown keys, converted duplicates, out-of-range
keys, mixed positional/keyed/default associations, defaults on dynamic kinds,
indirect and noncontainer targets, and the existing multidimensional,
aggregate/string-element, and cross-language exclusions. Native-object schema
38 and container semantic revision 14 distinguish the expanded behavior.
Cache evidence separately changes member values, converted keys, source
member order, default-versus-positional construction, destination range,
element width/state profile, and statement source provenance.

The diagnostic catalog now covers 1,216 production codes and the source gate
still covers 286 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 212.40 seconds, including scoped locals in 0.89 seconds and the
expanded container differential in 143.74 seconds. Release passed all 59
tests in 61.92 seconds, including scoped locals in 0.81 seconds and containers
in 30.66 seconds, on 2026-07-30. Batch 86 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run was inspected.

### Eighty-seventh feature batch — bounded one-dimensional static-array slices

SystemVerilog assignment targets and values now contextually distinguish a
direct static-array colon slice from an otherwise identical packed
part-select. The existing parser HIR was already sufficient: both target and
value retain source-spanned `Slice` nodes with explicit base, left, and right
operands. Elaboration admits only direct one-dimensional integral static
arrays, rejects indexed `+:`/`-:` unpacked selections, folds both bounds to
known signed 32-bit indices, and requires an in-range subrange in the
declaration's direction.

Each accepted selection derives a process-local `ContainerType` with the
selected declared left/right bounds and the base array's exact element width,
signedness, and two-/four-state domain. It allocates no module object identity.
Source and destination counts and profiles must match exactly. Elements map by
ordinal left-to-right position, so ascending and descending arrays with
different numeric indices interoperate without packed reinterpretation or
element conversion.

A direct RHS slice first materializes into its selected-range register through
existing `ContainerRead` and `ContainerWrite` operations. An LHS slice then
materializes a destination-selected staging value, copies the current whole
array into a full replacement, merges the selected values, and commits that
replacement with one whole-container copy. The complete RHS therefore
precedes any replacement work, overlapping self-assignment is deterministic,
and object or port writeback cannot expose a partial update. No new SimIR
operation, runtime callback, public ABI slot, or recursively embedded frontend
field was required; the implementation is isolated in
`lowerer_sv_container_slices.cpp` to preserve source-size and MSVC Debug stack
budgets.

Positive evidence covers slice-to-whole, whole-to-slice, slice-to-slice,
descending and ascending declarations, differing declared indices, overlap,
exact X/Z state, signed two-state values, module objects, writable static port
aliases through nested hierarchy, automatic function locals/formals, and
inout tasks after suspension. A dedicated operation-order fixture proves all
source and selected staging reads/writes precede the inverse whole-copy commit.
The expanded application agrees across the interpreter and LLVM O0/O2,
including cold/warm cache reuse and debugger inspection across suspension.

Negative evidence covers runtime and unknown bounds, direction reversal,
out-of-range selections, unequal counts, width/state/signedness mismatches,
dynamic arrays, queues, associative arrays, assignment patterns as slice
values, indexed unpacked selections, indirect targets, input-port writes, and
sliced port actuals. Existing multidimensional, aggregate/string-element, and
cross-language boundary diagnostics remain in force. Six new stable
`FSIM-ELAB-SVSLICE-*` diagnostics identify the contextual failures.

Native-object schema 39 and container semantic revision 15 carry both selected
ranges/directions, exact profiles, canonical snapshot/commit operations, and
statement source provenance. Dedicated cache cases independently vary source
range, destination range, element width, state domain, signedness, overlap
staging, and source line.

The diagnostic catalog now covers 1,222 production codes and the source gate
covers 287 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 220.82 seconds, including scoped locals in 0.89 seconds and the expanded
container differential in 150.97 seconds. Release passed all 59 tests in
63.49 seconds, including scoped locals in 0.80 seconds and containers in
32.84 seconds, on 2026-07-30. Batch 87 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run was inspected.

### Eighty-eighth feature batch — bounded read-only static-array slice consumers

Direct direction-preserving one-dimensional integral static-array slices now
serve as contextual read-only container receivers outside assignment. The
frontend retains the same compact source-spanned colon `Slice` HIR used by
Batch 87, including the direct identifier and explicit bounds. System query,
reduction, and locator calls preserve the slice as their first operand without
adding recursive fields to the common expression footprint.

`$left`, `$right`, `$low`, `$high`, and `$increment` use the selected declared
range and direction. `$size`, `$bits`, `$dimensions`, and
`$unpacked_dimensions` use the selected element count and exact element
profile, and `.size()` measures a materialized selected snapshot. The five
reductions execute over selected left-to-right ordinal order with the existing
exact-width signed/two-/four-state and empty-identity semantics. Their
implicit or named transformation graphs bind the slice element type and expose
the selected signed declared index through `.index`.

`min`, `max`, `unique`, and `unique_index` return compatible queues containing
the original selected elements or signed selected indices. All six `find*`
methods evaluate the existing bounded pure predicate graph over the selected
snapshot, treat X/Z logical results as false, and return values or selected
declared indices according to first/last/all semantics. The shared
`ContainerReduction` and `LocateContainer` interpreter/native kernels required
no new operation, runtime callback, validator rule, or public ABI slot.

Lowering recognizes only a direct static-container colon-slice candidate.
Bound validation and snapshot materialization reuse the Batch 87 helper, while
query-only constants derive from the selected `ContainerType` without
allocating a module object. The general container-expression classifier
remains identifier-only, so element indexing, mutation, associative traversal,
and other unrelated methods do not accidentally become valid on slices.

Positive evidence covers all nine system/method queries, all five reductions,
named transformation indices, all four extrema/uniqueness locators, all six
predicate locators, exact X/Z reduction behavior, input and writable static
ports, module objects, nested/generated hierarchy, automatic functions, and
tasks after suspension. Frontend tests prove direct source-spanned slice
receivers, binders, and graphs remain explicit HIR. Interpreter, LLVM O0, and
LLVM O2 agree through the expanded application differential.

Negative evidence covers runtime/unknown, reversed/out-of-range and indexed
slice bounds, dynamic/queue/associative receivers, indirect nested slices,
string and aggregate consumer syntax, mutating ordering, sliced port actuals,
multidimensional declarations, and cross-language container boundaries.
Existing `FSIM-ELAB-SVSLICE-*`, query, reduction, locator, find, ordering, and
port diagnostics remain stable; no new production diagnostic code was needed.

Native-object schema 40 and container semantic revision 16 carry selected
range/profile, materialization operations, query or method operation,
transformation/predicate graphs, and source provenance. A separate cache test
keeps the existing cache compilation unit below the hard source limit and
independently varies receiver range, element width, operation, transformation
presence/constants, locator mode, predicate constants, and source line.

The diagnostic catalog still covers 1,222 production codes and the source gate
covers 288 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 251.36 seconds, including scoped locals in 0.89 seconds and the expanded
container differential in 181.18 seconds. Release passed all 59 tests in
69.11 seconds, including scoped locals in 0.80 seconds and containers in
38.51 seconds, on 2026-07-30. Batch 88 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run was inspected.

### Eighty-ninth feature batch — bounded static-array slice callable actuals

Direct direction-preserving one-dimensional integral static-array slices now
serve as value actuals for the existing bounded same-language automatic
function and task container formals. The frontend retains each actual as the
same compact source-spanned colon `Slice` HIR used by assignment and
read-only consumers, without adding recursive fields to the common expression
or statement footprint.

For a fixed function input or task input/inout formal, lowering first
materializes the selected actual and adapts it into a formal-typed staging
container. Equal-count compatible ranges map by ordinal left-to-right
position even when their declared indices differ. The adaptation bit-copies
the exact element width, signedness, two-/four-state domain, and X/Z bits
without packed reinterpretation or element conversion, then copies the
snapshot into the callable activation frame.

Task output/inout copy-out reuses the ordinary assignment path after the
callee returns. A direct writable slice actual therefore receives one
selected staging value merged into one whole-array replacement; no partial
caller mutation is observable. Copy-out occurs only after normal or
valueless-early return, including after suspension. Each fixed output formal
is reset on every invocation from an immutable typed default container, so
four-state elements begin as X rather than retaining a preceding call's
values. The per-task default register IDs are vector-backed frame metadata,
preserving the compact Windows MSVC Debug parser/lowerer stack footprint.

Positive evidence covers explicit callable-actual HIR and source spans,
nested function slice inputs, task input/output/inout slices, different
actual/formal ranges, output X reset, exact X preservation, early-return
copy-out, suspension, module objects, writable static ports, and
nested/generated hierarchy. Interpreter, LLVM O0, and LLVM O2 agree through
the expanded cold/warm application differential.

Negative evidence covers runtime or unknown bounds, reversed and indexed
selections, count/width/state/signedness mismatch, dynamic and indirect
receivers, and read-only output/inout actuals through the existing stable
slice and port diagnostics. Existing multidimensional, recursion, sliced
module-port-actual, and cross-language callable boundaries remain diagnosed
by their owning bounded suites.

Native-object schema 41 and container semantic revision 17 carry the formal
mode/range/profile, selected actual type, copy-in/copy-out operation shape,
return provenance, and transitive callable source identity. A dedicated cache
matrix varies actual and formal ranges, element width, function versus task
use, task direction, callable source, and return line while keeping the cache
test compilation units below the source limit. No new SimIR operation,
runtime callback, native ABI slot, or public API was required.

The diagnostic catalog still covers 1,222 production codes and the source gate
covers 289 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 299.83 seconds, including scoped locals in 0.89 seconds and the expanded
container differential in 230.95 seconds. Release passed all 59 tests in
89.83 seconds, including scoped locals in 0.80 seconds and containers in
58.79 seconds, on 2026-07-31. Batch 89 is not a ten-batch GitHub
CI-inspection boundary, so no Actions run was inspected.

### Ninetieth feature batch — bounded static-array slice ordering mutation

Direct direction-preserving one-dimensional integral static-array slices now
serve as writable receivers for the existing bounded `reverse`, `sort`, and
`rsort` method statements. The frontend retains each receiver as the same
compact source-spanned colon `Slice` HIR used by assignment, consumers, and
callable actuals. Optional implicit or named `with` key expressions remain
ordinary source-spanned operands of the method call.

Lowering validates the direct base and locally constant selected range, then
materializes only that range into its exact selected `ContainerType`.
`OrderContainer` mutates this process-local snapshot. `reverse` swaps selected
ordinal positions only; unkeyed `sort` and `rsort` reuse the deterministic
stable two-/four-state element order. Keyed ordering binds the selected
element profile and exposes the selected signed declared index through
`.index`; the existing runtime kernel computes every key once from the
original selected snapshot and retains equal-key order.

After ordering completes, lowering copies the caller's whole array into a
replacement, merges all selected elements into that replacement by declared
ordinal mapping, and commits one whole-container copy. Module-object or
writable-port writeback occurs only after that final commit. Unselected
elements therefore remain unchanged, no partial ordering is observable, and
exact signed, two-/four-state, and X/Z bits survive without element conversion
or packed reinterpretation.

Positive evidence covers explicit receiver, iterator, and key HIR; descending
and ascending selected ranges; `reverse`, stable ascending sort, stable
descending sort, implicit and named index keys, equal-key stability, exact
X/Z order and preservation, surrounding-element nonmutation, module objects,
writable static ports through nested/generated hierarchy, and automatic tasks
after suspension. The dedicated elaboration fixture verifies that every
`OrderContainer` targets a selected-range register and is followed by a
whole-copy/merge/whole-commit sequence. Interpreter, LLVM O0, and LLVM O2
agree through the expanded cold/warm application differential.

Negative evidence covers read-only input ports, runtime or unknown bounds,
reversed/out-of-range and indexed selections, dynamic and indirect receivers,
unsupported key graphs, iterator collisions, and the existing
multidimensional, sliced module-port, and cross-language boundaries through
stable slice, ordering, and port diagnostics.

Native-object schema 42 and container semantic revision 18 carry selected
range/profile, ordering mode, optional key graph, atomic merge/commit
operations, and source provenance. A dedicated cache matrix varies selected
range, element width, ordering direction, key constants, commit shape, and
source line. No new SimIR operation, runtime callback, native ABI slot, or
public API was required.

The diagnostic catalog still covers 1,222 production codes and the source gate
covers 290 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 329.46 seconds, including scoped locals in 0.90 seconds and the expanded
container differential in 260.58 seconds. Release passed all 59 tests in
98.66 seconds, including scoped locals in 0.80 seconds and containers in
67.70 seconds, on 2026-07-31.

The mandatory Batch 90 non-documentation CI inspection began with run
`30611906784` for feature commit `d071d98`. Windows MSVC Debug exposed an
oversized application-test string literal; commit `cb63805` split the
generated SystemVerilog fixture into three compiler-safe writes. Replacement
run `30612954452` passed that original build point, then Windows MSVC LLVM
Debug rejected implicit `int` to `std::uint8_t` optional construction in the
slice-cache matrix. Commit `eaf4842` made every affected byte constant
explicitly typed, and the focused exact-LLVM test passed locally. That run was
cancelled by the subsequent documentation checkpoint under the branch
concurrency policy. Final replacement run `30613827882` passed all 12 jobs:
Windows MSVC Debug completed in 18 minutes 45 seconds, Windows MSVC LLVM
Debug in 28 minutes 45 seconds, and ASan/UBSan in 43 minutes 59 seconds. The
sanitizer result confirms the 45-minute CI budget remains necessary.

### Ninety-first feature batch — static-array slice module-port actuals

Direct named and positional `[left:right]` selections of one-dimensional
integral static arrays now connect to compatible same-language static-array
module ports. The frontend retains every actual as the same compact,
source-spanned colon `Slice` HIR used by assignment, consumers, callables, and
ordering. Specialization substitutes parent parameters before connection, so
locally constant parameter-dependent actual bounds remain exact.

Each sliced connection allocates a formal-typed `ContainerSliceAlias` that
references an earlier parent object and records the selected parent bounds.
Reads recursively materialize the selected range into formal ordinal order;
writes recursively copy the complete parent, merge the complete formal value,
and commit one replacement. Child input ports are read-only. Output ports
replace only their selected parent range, and inout ports see the exact
initial selected value before the same atomic writeback. Different declared
indices and directions are allowed when element count, width, signedness, and
two-/four-state domain match exactly; X/Z bits are copied without conversion
or packed reinterpretation.

Whole aliases below a sliced connection retain the same object identity, and
recursive aliases are valid only when every target precedes its view. This
carries sliced bindings through parameter specialization and nested/generated
hierarchy. Interpreter container operations, native execution callbacks,
public inspection, deposits, debugger paths, and VCD-visible scalar witnesses
share the alias-aware runtime path. No new SimIR operation, callback slot,
public native ABI revision, or partial parent mutation was required.

Boundary-driver ownership now includes an optional selected interval. Two
disjoint output/inout slices may coexist; overlapping slices, a slice plus a
whole writer, or unrelated multiple writers retain deterministic
`SVPORT-008` rejection. Stable checks also reject read-only descendants,
runtime/unknown or wrong-direction/out-of-range bounds, indexed and indirect
actuals, element and nonstatic receivers, unknown objects, count/width/
signedness/state mismatch, and the existing multidimensional, recursive, and
cross-language forms.

Positive evidence spans direct runtime aliases, nested deposits, exact X/Z
ordinal values, specialization-dependent named connections, positional
connections, all three directions, disjoint writers, nested/generated whole
forwarding, debugger formal paths, scalar VCD, interpreter, LLVM O0/O2, and
cold/warm reuse. Native-object schema 43 and container semantic revision 19
record formal profile, object operation/identity, specialization, and source
provenance. Selected parent bounds remain validated runtime alias metadata,
and the cache matrix proves that changing only those external bounds safely
reuses identical machine code while every code-relevant difference misses.

The diagnostic catalog still covers all 1,222 production codes, and the
source gate covers 291 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 412.19 seconds, including scoped locals in 0.90 seconds, the
expanded container differential in 342.15 seconds, and the monolithic
application in 40.03 seconds. Release passed all 59 tests in 116.86 seconds,
including scoped locals in 0.86 seconds, containers in 85.11 seconds, and the
monolithic application in 13.55 seconds, on 2026-07-31. Batch 91 is not a
ten-batch CI-inspection boundary, so no Actions run was inspected.

### Ninety-second feature batch — indexed static-array slices

Direct locally constant `base +: width` and `base -: width` selections of
one-dimensional integral static arrays now use the same selected-container
architecture as colon slices. The frontend retains indexed `Slice` HIR with
its source span and operator, so unpacked selections remain distinguishable
from packed indexed part-selects until contextual elaboration.

Elaboration requires a known signed-32 base and width and rejects a
nonpositive width. The numeric interval is computed with checked arithmetic,
validated against the receiver, and then oriented to the static array's
declared direction. Consequently equivalent plus and minus spellings produce
the same normalized selected `ContainerType`, including exact element count,
element width, signedness, and two-/four-state domain. Both operators are
valid for ascending and descending receivers when their computed interval is
in range.

The normalized type flows through whole-to-slice, slice-to-whole, and
slice-to-slice assignments. Existing source snapshots, selected staging, and
one whole-parent replacement preserve atomic overlap behavior and every X/Z
bit. System queries, `.size()`, reductions and transformations,
extrema/uniqueness, predicate locators, `reverse`, `sort`, and `rsort` consume
the same selected snapshot and expose normalized signed declared indices to
`.index`.

Fixed automatic function inputs and task input/output/inout formals accept
the indexed spellings through ordinal value adaptation. Suspended task
copy-out still commits only after normal or early return. Named and positional
same-language module connections likewise normalize indexed actuals before
constructing the Batch 91 recursive `ContainerSliceAlias`; read-only input,
atomic output/inout merge, nested/generated forwarding, and interval-aware
driver ownership therefore remain unchanged.

Stable negative matrices reject runtime or unknown bases and widths,
zero/negative widths, arithmetic overflow, out-of-range selections,
nonstatic or indirect receivers, read-only writes, shape/profile mismatch,
overlapping drivers, multidimensional containers, recursive boundaries, and
cross-language slices. Indexed slice return values, general expression
receivers/actuals, runtime-variable indexed selections, and element
conversion remain deferred.

Frontend, elaboration, debugger, scalar VCD, interpreter, LLVM O0/O2, and
cold/warm application evidence covers ascending and descending receivers,
both indexed operators, objects, ports, nested/generated hierarchy, automatic
callables, suspension, ordering, queries, and overlapping assignments. Native
object schema 44 and container semantic revision 20 serialize normalized
range, profile, operation graph, specialization, and source identity. The
cache matrix proves equivalent `+:` and `-:` intervals reuse one object while
range, direction, profile, operation, and provenance changes miss; the public
native ABI is unchanged.

The diagnostic catalog still covers all 1,222 production codes, and the
source gate covers 291 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 439.73 seconds, including scoped locals in 1.02 seconds, the
expanded indexed-container differential in 363.49 seconds, and the monolithic
application in 43.07 seconds. Release passed all 59 tests in 128.24 seconds,
including scoped locals in 0.85 seconds, containers in 92.19 seconds, and the
monolithic application in 14.87 seconds, on 2026-07-31. Batch 92 is not a
ten-batch CI-inspection boundary, so no Actions run was inspected.

### Ninety-third feature batch — fixed-array function returns

Bounded automatic SystemVerilog functions may now return one-dimensional
integral fixed unpacked arrays. The parser retains the unpacked result
dimension after the function name, and specialization resolves its exact
declared bounds, element width, signedness, and state domain into a fixed
`ContainerType`. Dynamic, queue, associative, multidimensional, and
runtime-bound result kinds remain diagnosed rather than flattened into packed
values.

Each call owns typed argument, default-result, and destination storage. The
result resets to its exact X default for every activation, then accepts either
whole function-name assignment or explicit value `return`. Nested
nonrecursive calls copy their completed values into isolated destinations, so
later invocations cannot mutate earlier results. Module, package, imported,
directly qualified, and specialization-dependent functions share this path;
qualified result names also bind their lexical short alias for element writes.

Whole arrays and directly compatible colon or locally constant indexed slices
may supply returned values. Equal-count ranges adapt ordinally into the
declared result range while preserving element width, signedness, state
domain, and exact X/Z planes. Returned values assign to compatible whole
arrays or direct slices through the existing snapshot and selected-merge
architecture. Consequently an overlapping returned slice observes one source
snapshot and commits one atomic whole-parent replacement. Element assignments
also resize arithmetic results to their declared element width, closing a
pre-existing container-assignment width hole exposed by this batch.

Positive evidence spans exact HIR, colon and indexed returns, ascending and
descending ranges, X/Z values, per-call defaults, whole and selected targets,
overlap, nested/module/package/imported/qualified calls, parameterized result
bounds, debugger container locals, VCD witnesses, interpreter, LLVM O0/O2,
cold/warm reuse, and package-edit invalidation. Native-object schema 45 and
container semantic revision 21 record result and source profiles, operation
mode, specialization, and transitive provenance; the cache matrix proves that
every code-relevant difference misses without changing the public native ABI.

The diagnostic catalog now covers all 1,223 production codes, and the source
gate covers 291 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 421.42 seconds, including scoped locals in 0.96 seconds, fixed
array functions in 7.90 seconds, the container differential in 339.70 seconds,
and the monolithic application in 39.86 seconds. Release passed all 59 tests in
124.07 seconds, including scoped locals in 0.81 seconds, fixed-array functions
in 7.01 seconds, containers in 87.24 seconds, and the monolithic application
in 13.76 seconds, on 2026-07-31. Batch 93 is not a ten-batch CI-inspection
boundary, so no Actions run was inspected.

### Ninety-fourth feature batch — nonstatic container function returns

Bounded automatic SystemVerilog functions may now return dynamic arrays,
queues or bounded queues, and integral-key associative arrays. The existing
result-dimension HIR retains each kind, and specialization resolves exact
element width, signedness, state domain, queue bound, and associative index
width, signedness, and state. Parameter-dependent queue bounds and imported
package typedef index marks therefore reach one exact runtime `ContainerType`.

The kind-generic function frame allocates an exact empty default result for
every activation. Whole function-name assignment and explicit value `return`
copy elements and associative keys into an isolated destination after the
call. Nested calls, repeated calls, and automatic container locals cannot
alias the shared result frame, while a partially assigned first invocation
cannot leak into the exact empty second invocation.

Returned values assign to compatible whole module objects or automatic locals
and flow directly into compatible function and task input actuals. Every
transfer validates container kind, element profile, queue bound, and
associative index profile before emitting a copy. Fixed/nonstatic,
dynamic/queue/associative, element/index-profile, runtime-slice,
multidimensional, and recursive mismatches retain bounded diagnostics rather
than becoming runtime callback failures.

Positive evidence spans exact frontend HIR, dynamic/queue/associative result
metadata, function-name and explicit returns, nested copy isolation, empty
defaults, module/package/imported/qualified calls, parameter-bound queues,
typedef-indexed associative values, automatic locals, returned function/task
inputs, debugger metadata, VCD witnesses, interpreter, LLVM O0/O2, cold/warm
reuse, and package-edit invalidation. Native-object schema 46 and container
semantic revision 22 record kind, bound, element/index profiles, operation
graph, specialization, and transitive provenance without a public ABI change.

The diagnostic catalog now covers all 1,225 production codes, and the source
gate covers 291 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 430.23 seconds, including scoped locals in 0.96 seconds, the
expanded function differential in 9.48 seconds, containers in 351.17 seconds,
and the monolithic application in 40.34 seconds. Release passed all 59 tests in
128.98 seconds, including scoped locals in 0.81 seconds, functions in 8.48
seconds, containers in 89.85 seconds, and the monolithic application in 13.91
seconds, on 2026-07-31. Batch 94 is not a ten-batch CI-inspection boundary, so
no Actions run was inspected.

### Ninety-fifth feature batch — container function-result consumers

Fixed, dynamic-array, and queue-valued SystemVerilog function calls now retain
their exact container type while nested directly inside supported queries,
element indexing, reductions, extrema, uniqueness, predicate locators, and
container-valued conditional expressions. The shared resolver carries fixed
ranges, current nonstatic bounds, element width/signedness/state, queue bounds,
and source spans from the visible specialized function declaration into each
consumer without admitting unrestricted container expressions.

`$bits`, `$dimensions`, `$unpacked_dimensions`, the six bound queries,
`$size`, and `.size()` use static profile results where the language permits
and evaluate one isolated runtime snapshot where current nonstatic size is
required. Plain and transformed reductions, max/unique, find/find-index, and
direct indexing likewise evaluate each returned call once in lexical order.
Nested module and package calls therefore cannot alias the shared function
result frame or observe a later invocation's elements.

The new `ConditionalContainerSelect` SimIR operation accepts exactly compatible
fixed, dynamic, or queue alternatives. Known conditions copy the selected
snapshot. X/Z conditions merge equal-shape four-state elements with the scalar
conditional bit policy, coerce unknown bits to zero for two-state elements,
and reset unequal nonstatic shapes to their exact empty default. Interpreter
and LLVM callback execution share one semantic helper. Associative conditional
values, incompatible profiles, and mutating methods on temporary results retain
bounded diagnostics.

Positive evidence covers nested source-spanned HIR, fixed and nonstatic query
families, transformed reductions, extrema/uniqueness/predicate locators,
known/X and unequal-shape conditionals, one-evaluation call counts,
module/package/imported/qualified calls, debugger metadata, scalar VCD
witnesses, interpreter, LLVM O0/O2, cold/warm reuse, and package-edit
invalidation. Native-object schema 47 and container semantic revision 23
serialize the conditional operation and exact consumer/profile/provenance
graph; the dedicated matrix now contains 14 distinct native objects without a
public ABI change.

The diagnostic catalog now covers all 1,230 production codes, and the source
gate covers 291 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 443.17 seconds, including scoped locals in 0.96 seconds, functions
in 11.55 seconds, containers in 358.54 seconds, and the monolithic application
in 42.38 seconds. Release passed all 59 tests in 136.97 seconds, including
scoped locals in 0.89 seconds, functions in 10.08 seconds, containers in 94.70
seconds, and the monolithic application in 14.53 seconds, on 2026-07-31. Batch
95 is not a ten-batch CI-inspection boundary, so no Actions run was inspected.

### Ninety-sixth feature batch — bounded whole-container equality

Exactly compatible one-dimensional SystemVerilog fixed arrays, dynamic arrays,
queues, bounded queues, and integral-key associative arrays now lower as typed
operands of `==`, `!=`, `===`, and `!==`. Compatibility is deliberately exact:
kind, fixed declared range or queue bound, element width/signedness/state, and
associative index width/signedness/state must agree before an executable
comparison is emitted. Relational and wildcard operators retain targeted
diagnostics rather than silently flattening containers into packed values.

The new `CompareContainers` SimIR operation writes a width-one scalar result.
Logical equality first rejects size or key-set mismatches, then returns false
for any known unequal element, X when no known mismatch exists but an element
comparison is unknown, and true otherwise. A two-state element profile produces
a two-state result. Case equality compares every value and X/Z plane exactly
and always returns a known bit. Inequality reuses the same comparison followed
by scalar negation. Function-result and compatible conditional operands are
evaluated once into isolated snapshots in lexical order.

Interpreter execution and compiled LLVM O0/O2 callbacks call the same runtime
semantic helper. Positive evidence spans fixed/dynamic/queue/associative
values, exact and unknown elements, size/key mismatch, returned and conditional
operands, module/package/imported/qualified calls, one-evaluation counts,
debugger metadata, scalar VCD witnesses, cold/warm cache reuse, and package-edit
invalidation. Native-object schema 48 and container semantic revision 24
serialize the comparison mode and exact operation/type/provenance graph; the
cache matrix contains 15 distinct objects without changing the public ABI.

The diagnostic catalog now covers all 1,233 production codes, and the source
gate covers 291 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests in
423.91 seconds, including scoped locals in 0.93 seconds, functions in 11.00
seconds, containers in 343.67 seconds, and the monolithic application in 40.03
seconds. Release passed all 59 tests in 129.03 seconds, including scoped locals
in 0.82 seconds, functions in 9.92 seconds, containers in 88.69 seconds, and
the monolithic application in 13.64 seconds, on 2026-07-31. Batch 96 is not a
ten-batch CI-inspection boundary, so no Actions run was inspected.

### Ninety-seventh feature batch — bounded SystemVerilog membership expressions

SystemVerilog scalar integral `inside` expressions now retain one
source-spanned left operand and a nonempty ordered braced list in HIR. Exact
value members remain ordinary expressions, while `[low:high]` members use an
explicit two-bound range node. Parsing diagnoses use outside SystemVerilog,
empty lists, missing braces, missing range colons, and missing range brackets
without admitting `case inside` or general set syntax.

Elaboration requires every operand to be scalar integral with exactly matching
width and signedness. It evaluates the left operand once, then tests value and
range members in source order. Value members use right-operand X/Z wildcard
equality. Ascending closed ranges are inclusive; known reversed ranges are
empty. Unknown comparisons propagate X unless a later member definitely
matches, and a definite match branches past every remaining member. The
universal result is one four-state bit, including for two-state operands.
Module, package, imported, and qualified calls may supply the left value,
members, or bounds, and every reached call executes once.

Typed constant folding implements the same value, range, wildcard, reversed,
and unknown semantics. Runtime lowering reuses shared scalar comparison,
logical, and branch operations, so no public runtime or LLVM callback ABI was
added. Positive evidence covers parameter folding, mixed lists, signed and
two-state values, unknown and later-match cases, skipped failing members,
dynamic call bounds, call-operation counts, debugger metadata, VCD,
interpreter, LLVM O0/O2, cold/warm reuse, and package-edit invalidation.
Native-object schema 49 records the ordered membership graph and transitive
provenance; container semantic revision 24 remains unchanged. The cache matrix
contains 16 distinct objects.

The diagnostic catalog now covers all 1,244 production codes, and the source
gate covers 292 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests in
428.53 seconds, including scoped locals in 0.91 seconds, functions in 11.93
seconds, containers in 348.00 seconds, and the monolithic application in 39.94
seconds. Release passed all 59 tests in 127.57 seconds, including scoped locals
in 0.83 seconds, functions in 10.12 seconds, containers in 87.04 seconds, and
the monolithic application in 13.85 seconds, on 2026-07-31. Batch 97 is not a
ten-batch CI-inspection boundary, so no Actions run was inspected.

### Ninety-eighth feature batch — bounded SystemVerilog case-inside statements

SystemVerilog `case (selector) inside` now retains a distinct source-spanned
matching mode with ordered alternatives. Each alternative keeps its
comma-separated exact values and explicit `[low:high]` range nodes, while the
existing final-default and duplicate-default rules remain common to all case
modes. Parsing diagnoses non-SystemVerilog use, malformed ranges, empty or
trailing choices, `casez`/`casex` combinations, deferred `case matches`, and
unsupported case qualifiers without losing following process recovery.

Elaboration requires one scalar integral selector and exact width/signedness
compatibility for every value and range bound. The selector executes once.
Exact choices use right-choice X/Z wildcard equality; ascending ranges use
inclusive signed or unsigned comparisons and known reversed ranges are empty.
Unknown comparisons fall through, allowing a later wildcard to match
definitely; otherwise the final default executes. Alternatives and choices
retain source order, the first definite match exits the statement, and later
choice calls—including deliberately failing calls—are skipped.

Constant-function execution now applies the same ordered membership semantics
without eagerly evaluating choices after a definite match. Runtime lowering
reuses shared scalar comparisons, logical operations, branches, and jumps, so
no public runtime or LLVM callback ABI was added. Positive evidence covers
mixed lists, wildcard and unknown selectors, later matches, reversed/signed/
two-state ranges, first selection, module and package calls, exact execution
points, constant selection, debugger metadata, VCD, interpreter, LLVM O0/O2,
cold/warm reuse, and package-edit invalidation. Native-object schema 50 records
the exact choice/range operations and control-flow targets; container semantic
revision 24 remains unchanged and the cache matrix contains 17 objects.

The diagnostic catalog now covers all 1,255 production codes, and the source
gate covers 292 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests in
420.22 seconds, including scoped locals in 1.02 seconds, functions in 12.74
seconds, containers in 338.48 seconds, and the monolithic application in 39.71
seconds. Release passed all 59 tests in 125.07 seconds, including scoped locals
in 0.83 seconds, functions in 10.46 seconds, containers in 84.69 seconds, and
the monolithic application in 13.56 seconds, on 2026-07-31. Batch 98 is not a
ten-batch CI-inspection boundary, so no Actions run was inspected.

### Ninety-ninth feature batch — bounded SystemVerilog case qualifiers

SystemVerilog `unique`, `unique0`, and `priority` now retain distinct compact
`CaseQualifier` HIR metadata independently of exact, `casez`, `casex`, and
bounded `case inside` matching modes. The parser accepts one qualifier only in
SystemVerilog, begins the statement span at that qualifier, and diagnoses
duplicate, misplaced, and wrong-language forms while retaining following-
statement recovery.

Qualified lowering evaluates the selector once and computes one definite
two-state match bit per source alternative. Comma-separated choices are ORed
within their alternative, so a single item can never count twice. Unknown
comparison results are nonmatches for qualifier checks. Exact, wildcard-Z,
wildcard-XZ, and inside value/range matching otherwise retain their established
semantics. All alternatives are checked before execution; the first matching
body or final default then executes in ordinary source order.

`unique` emits a source-aware warning for more than one matching alternative
and for no match when no default exists. `unique0` warns only for multiple
alternatives, while `priority` warns only for no match without a default and
does not diagnose overlap. A default suppresses the applicable no-match
warning. Shared `Report` SimIR operations carry warning severity and the
qualifier source location through interpreter, LLVM O0/O2, callbacks, and CLI
rendering without changing the public runtime ABI.

Positive evidence covers alternative-level counting, first-body/default
selection, exact/casez/casex/case-inside modes, unknown fallthrough,
constant-function selection, report ordering/source metadata, interpreter,
LLVM O0/O2, cold/warm reuse, source-edit invalidation, CLI rendering, and
malformed-HIR diagnostics. Native-object schema 51 records the added report and
control-flow identity in an 18-object matrix; container semantic revision 24
and the public ABI remain unchanged.

The diagnostic catalog now covers all 1,259 production codes, and the source
gate covers 294 authored files with an empty allowlist and a 2,000-line
maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all
59 tests in 427.12 seconds, including scoped locals in 0.90 seconds, functions
in 12.52 seconds, containers in 341.61 seconds, qualified assertions in 6.07
seconds, and the monolithic application in 39.64 seconds. Release passed all
59 tests in 131.20 seconds, including scoped locals in 0.79 seconds, functions
in 10.50 seconds, containers in 86.11 seconds, qualified assertions in 5.89
seconds, and the monolithic application in 13.47 seconds, on 2026-07-31. Batch
99 is not a ten-batch CI-inspection boundary, so no Actions run was inspected.

### One-hundredth feature batch — bounded SystemVerilog case-pattern matching

SystemVerilog `case (selector) matches` now retains a distinct compact
`CaseMatchKind::Matches` HIR mode. Each nondefault item contains exactly one
bounded scalar-integral constant pattern or the unconditional `.*` wildcard.
Constant patterns require the selector's exact width and signedness and lower
to definite two-state case-equality results, preserving exact X/Z planes.

The selector executes once, the first matching body or final default executes
in source order, and `unique`, `unique0`, and `priority` reuse their established
alternative-level warning rules. Constant-function selection and runtime
interpreter/LLVM O0/O2 behavior share the same exact matching contract.
Application evidence covers module/package values, exact unknown patterns,
reports, debugger metadata, VCD, cold/warm reuse, and package-source edits.

The parser recognizes `&&&` guards for targeted deferral and diagnoses
non-SystemVerilog use, `casez`/`casex` combinations, comma pattern lists,
malformed dot patterns, variable binding, tagged/structured patterns, and
guards. Elaboration independently rejects nonconstant or nonintegral patterns,
selector/profile mismatches, unsupported markers, wrong-language HIR, and
zero-or-multiple-pattern malformed HIR.

Native-object schema 52 distinguishes exact-pattern and unconditional-wildcard
operation graphs in a 19-object cache matrix. Container semantic revision 24
and the public ABI remain unchanged. The diagnostic catalog covers 1,269
production codes and all 295 authored sources pass the 2,000-line gate.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 433.09 seconds, including scoped locals in 0.89 seconds, functions in 12.90
seconds, containers in 345.04 seconds, qualified pattern assertions in 8.19
seconds, and the monolithic application in 39.83 seconds. Release passed all
59 tests in 131.62 seconds, including scoped locals in 0.77 seconds, functions
in 10.63 seconds, containers in 84.72 seconds, qualified pattern assertions in
7.75 seconds, and the monolithic application in 13.41 seconds, on 2026-07-31.

The mandatory non-documentation Batch 100 inspection began from feature commit
`4730516`. Initial run
[`30640792808`](https://github.com/colinphill/fsim/actions/runs/30640792808)
passed all 11 ordinary jobs but exposed a 1,500.27-second sanitizer container
timeout before the 45-minute job cap. Repairs `7400061` and `c669187` expanded
the sanitizer job/test budgets to 70/40 minutes; replacement runs `30647570353`
and `30651503050` then reproduced exact 1,800.14- and 2,400.12-second container
timeouts without sanitizer findings while every ordinary job remained green.
The root repair `d2f216e` removes redundant compiled-engine and O2 aliases only
when LLVM is disabled, retaining one complete semantic, suspension, debugger,
cache, and VCD pass under ASan/UBSan and the full interpreter/JIT O0/O2 matrix
in LLVM builds. It restores a diagnostic 1,200-second container limit. Those
runs used parallelism two; current GitHub builds use four after the operation-
storage footprint repair.

Final replacement run
[`30656887493`](https://github.com/colinphill/fsim/actions/runs/30656887493)
passed all 12 jobs. ASan/UBSan passed all 58 tests in 876.53 seconds, including
scoped locals in 1.44 seconds, containers in 588.70 seconds, and the monolithic
application in 154.76 seconds; its complete job took 35 minutes 46 seconds.
Windows MSVC Debug passed all 58 tests in 336.76 seconds, including scoped
locals in 0.33 seconds, containers in 219.86 seconds, and the monolithic
application in 78.62 seconds; its complete job took 17 minutes 16 seconds.
Batch 100 and its CI boundary are closed.

### One-hundred-first feature batch — SystemVerilog expression sizing and selection closure

Supported SystemVerilog scalar expression lowering now retains immutable
source-spanned `ExpressionProfile` metadata with resolved width, signedness,
self- or context-determined sizing, and two-/four-state domain. Assignment,
argument, return, condition, and read-selection contexts propagate their
bounded 1–64-bit width through sized, unsized, unbased-unsized, unary,
arithmetic, bitwise, comparison, shift, power, conditional, concatenation, and
replication expressions. Exact zero/sign extension and truncation are applied
after the language's common signedness decision, while concatenation operands,
shift amounts, and the power exponent retain their self-determined widths.

Logical `&&` and `||` now branch past the complete right-hand graph when the
left operand is definitely controlling. SystemVerilog `?:` has separate true,
false, and X/Z paths: a known condition evaluates one alternative, while the
unknown path evaluates both alternatives exactly once and performs the common
four-state bit merge. Time-free bounded functions may write nonlocal variables
so application counters prove skipped and reached call execution; writes to
input formals, nonblocking assignments, and timing controls remain rejected.

Runtime-base packed `base +: width` and `base -: width` reads lower through a
new fixed-width `DynamicPartSelect` operation shared by the interpreter and
LLVM. The operation retains signed base, exact declared direction, selection
direction, result width, and state policy. It maps ascending and descending
sources identically by declared index, fills only unavailable bits with X or
two-state zero, and returns all X/zero for an unknown base. Dynamic procedural
targets remain explicitly diagnosed for Batch 102.

Bounded integral streaming concatenation accepts left/right directions,
default or positive locally constant slice sizes, nested ordinary
concatenation operands, and a final partial chunk. Runtime lowering composes
existing extract/concatenate operations; the typed constant evaluator uses the
same chunk order and a 64-bit-safe concatenation helper. Parser, elaboration,
and native validation reject wrong-language or malformed forms, dynamic or
invalid slice sizes, aggregate/container operands, widths beyond the bounded
runtime contract, malformed selection metadata, and zero/invalid expression
profiles through stable diagnostics.

Native-object schema 53 records all expression profiles and every
`DynamicPartSelect` field in a 12-object profile/selection cache matrix.
Container semantic revision 24 and the public ABI remain unchanged. The first
full Debug gate exposed that a deliberate 65-bit reference-only partial-group
process must retain its exact expression profile and enter the established JIT
fallback rather than fail profile validation; the validator now rejects only
zero widths and invalid sizing/domain enums, and the corrected partial-group
application again compiles its eligible sibling process.

The diagnostic catalog covers all 1,279 production codes, and the source gate
covers 296 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 430.48 seconds, including scoped locals in 0.86 seconds, parameter sizing
in 2.79 seconds, functions in 12.79 seconds, containers in 340.50 seconds,
assertions in 8.06 seconds, and the monolithic application in 39.44 seconds.
Release passed all 59 tests in 132.39 seconds, including scoped locals in 0.80
seconds, parameter sizing in 1.89 seconds, functions in 10.62 seconds,
containers in 84.09 seconds, assertions in 7.71 seconds, and the monolithic
application in 13.12 seconds, on 2026-07-31. Batch 101 is not a CI-inspection
boundary, so no Actions run was inspected for feature commit `5e713cc`.

### One-hundred-second feature batch — SystemVerilog procedural-lvalue closure

SystemVerilog compound, prefix, and postfix updates now retain explicit
source-spanned update-kind and operator metadata independently of assignment
kind and delay/event control. The normalized binary expression remains the
single arithmetic source of truth, while elaboration checks that the metadata
and expression agree. Prefix/postfix `++`/`--` are also valid expressions:
the target is updated once and the expression returns the converted new value
or the captured old value, respectively.

Procedural assignment lowering now unwinds and composes whole, packed-member,
static bit/part, chained static packed, runtime bit, and runtime-base indexed
part targets. Read-modify-write operations capture the selected lvalue once,
so function-valued indices and bases execute exactly once. Runtime-base
`+:`/`-:` writes update only the representable contiguous portion, preserve
surrounding bits, and perform no write for unknown or wholly out-of-range
bases. Blocking, NBA, and delayed writes retain the same checked metadata;
event-controlled compound assignments capture their target before suspension
and defer RHS evaluation until the event, while delayed forms compute before
waiting.

Procedural `force` and `release` now support whole packed signals, packed
members, and static bit/part selections. Per-bit force masks overlay the
effective signal value while every underlying driver continues to update;
partial release immediately reveals only the current driven bits in the
released region. Automatic locals, runtime-selected force targets, and a
selection following any runtime-selected procedural target remain explicitly
diagnosed. Interpreter and LLVM Logic4/Logic9 paths share the same selected
force and dynamic-part semantics.

The version-1 native runtime structure remains append-only and is now 512
bytes, adding Logic4 force-slice, Logic9 force-slice, and release-slice
callbacks with capability-size validation. Native-object schema 54 records
dynamic-part read/write base offsets, inserts, all three dynamic signal-write
kinds, and force/release fields. A 13-object expression-selection matrix and
seven-object procedural-update matrix prove semantic cache misses; the
procedural application proves cold/warm reuse, exact interpreter/O0/O2 final
state and scheduling, and normalized VCD agreement.

Stable frontend, elaboration, and native diagnostics cover wrong-language and
malformed update/force syntax, inconsistent update HIR, runtime-selection
chaining, unsupported force targets, two-state force conversion, invalid
dynamic-part metadata/ranges, and malformed force/release operations. The
diagnostic catalog covers all 1,289 production codes, and the source gate
covers 298 authored files with an empty allowlist and a 2,000-line maximum.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 432.69 seconds, including scoped locals in 0.86 seconds, functions in 13.39
seconds, containers in 338.32 seconds, procedural assignments in 2.47 seconds,
and the monolithic application in 39.94 seconds. Release passed all 59 tests
in 138.83 seconds, including scoped locals in 0.81 seconds, functions in 11.00
seconds, containers in 86.13 seconds, procedural assignments in 1.90 seconds,
and the monolithic application in 14.17 seconds, on 2026-07-31. Batch 102 is
not a ten-batch CI-inspection boundary, so no Actions run was inspected.

### One-hundred-third feature batch — SystemVerilog function/task closure

SystemVerilog function and task HIR now retains explicit or implicit
automatic/static lifetime, ANSI or classic body formal declarations,
input/output/inout/reference mode, typed default expressions, and positional
or named actual-association metadata. Classic header names are reconciled with
body declarations in header order, and selected generated regions may publish
scope-qualified functions and tasks with parameter substitution and exact
source provenance.

One common association service binds every actual once in formal order,
supplies omitted input defaults, rejects malformed or ambiguous associations,
and performs typed ordered copy-out. Packed integral function output/inout and
bounded direct-caller-local automatic reference formals execute around the
ordinary value result. Automatic nonsuspending tasks admit the same bounded
direct-local reference transfer. Existing fixed/dynamic/queue/associative
callable values retain their prior copy isolation and suspended-task behavior
through the normalized association layer.

Automatic activations continue to restore per-call defaults. Static and
implicit-lifetime functions plus nonsuspending tasks instead allocate bounded
packed body-scope locals once in each process preamble, preserving their state
across sequential calls from that process. Nested or nonintegral static
locals, suspending static tasks, nonlocal or suspending references,
nonintegral writable function formals, unsafe recursion, and malformed HIR are
rejected through stable checked paths. Constant evaluation accepts the same
bounded named/default association rules but leaves static or writable calls to
runtime execution.

The dedicated callable-closure application proves static function/task state,
named/default associations, function output and reference transfer, task
reference transfer, selected generated declarations, safe points, explicit
post-run debugger-local reads, VCD witnesses, interpreter/LLVM O0/O2 parity,
cold/warm reuse, and generated-source invalidation. Native-object schema 55
records the static preamble, normalized association/copy graph, generated
callables, operations, and transitive source/debug identity without changing
the public runtime ABI.

The diagnostic catalog covers all 1,303 production codes, and the source gate
covers 301 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 60 tests
in 441.89 seconds, including scoped locals in 0.89 seconds, functions in 13.34
seconds, tasks in 0.74 seconds, callable closure in 1.63 seconds, containers in
344.88 seconds, and the monolithic application in 39.71 seconds. Release
passed all 60 tests in 141.23 seconds, including scoped locals in 0.80 seconds,
functions in 11.22 seconds, tasks in 0.65 seconds, callable closure in 1.46
seconds, containers in 87.28 seconds, and the monolithic application in 13.72
seconds, on 2026-07-31. Batch 103 is not a ten-batch CI-inspection boundary,
so no Actions run was inspected.

### One-hundred-fourth feature batch — SystemVerilog always/procedural-control closure

SystemVerilog process/control HIR now retains body-timed versus header-event
`always` lifecycle, exact general packed event expressions, runtime loop and
repeat operands, inline-loop-variable lifetime, and repeated intra-assignment
event counts. `always_ff` requires exactly one edge-qualified header event and
rejects nested timing, while inferred combinational and latch processes retain
their time-zero execution.

Plain body-timed `always` starts at time zero and returns to the body only after
a proven suspension. Cycle-safe path analysis admits timed/event-controlled or
terminating `forever` paths and deterministic `break`, while rejecting every
reachable nonprogressing backedge, including unsafe `continue` and partial
conditional paths. Procedural `for` accepts inline signed integers or existing
packed variables, direct integral comparison bounds, and positive nonunit
constant steps; runtime repeat counts are evaluated once, with negative or
unknown counts producing zero iterations. `break` and `continue` target the
correct loop exit and update/increment points.

General packed any-change controls lower to exact expression snapshots: the
process waits on transitive signal dependencies and filters operand wakes that
do not change the expression value. Repeated intra-assignment event controls
evaluate their integral count once and wait exactly that many qualifying
events before the deferred assignment. Edge-qualified general expressions and
general expressions mixed with another event-list item remain bounded,
explicitly diagnosed exclusions. Wildcard inference now follows visible
function and task bodies transitively and cycle-safely while excluding their
formals, locals, and default-only names.

The dedicated named-event application proves lifecycle, runtime `for` and
`repeat`, safe `forever`, exact expression filtering, repeated controls,
transitive wildcard dependencies, callback/delta ordering, safe points, and
normalized VCD equality across interpreter and LLVM O0/O2. Native-object
schema 56 records the exact lowered control graph; an O0/O2 cold/warm matrix
and branch-target edit prove reuse and invalidation without changing the public
runtime ABI. Stable frontend and elaboration diagnostics cover illegal
lifecycle paths, loop shapes, event forms, and malformed HIR.

The diagnostic catalog covers all 1,315 production codes, and the source gate
covers 304 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 60 tests
in 447.24 seconds, including scoped locals in 0.86 seconds, LLVM in 3.61
seconds, named events in 0.92 seconds, containers in 348.34 seconds, and the
monolithic application in 39.88 seconds. Release passed all 60 tests in 139.06
seconds, including scoped locals in 0.81 seconds, LLVM in 2.46 seconds, named
events in 0.78 seconds, containers in 85.25 seconds, and the monolithic
application in 13.45 seconds, on 2026-07-31. Batch 104 is not a ten-batch
CI-inspection boundary, so no Actions run was inspected.

### One-hundred-fifth feature batch — SystemVerilog fork/process and NBA ordering closure

SystemVerilog process HIR now retains named and anonymous `fork` scopes,
fork-scope declarations, ordered branches, exact `join`, `join_any`, or
`join_none` kind, matching closing labels, `wait fork`, and `disable fork`.
The checked lowerer initializes the lexical fork scope once, emits explicit
parent continuation and child-entry PCs, and rejects fork escape from the
bounded callable frame model.

The common kernel creates dense dynamic child identities with independent
program counters and a shared lexical frame. `join` waits for every child,
`join_any` resumes on the deterministic first completion while retaining the
other children, and `join_none` continues immediately. `wait fork` observes
all live immediate children; `disable fork` recursively cancels descendants,
including grandchildren whose intermediate child already completed, and
clears their waits and group ownership. One live activation per lexical fork
site remains the explicit bounded lifetime rule. Interpreter and compiled
children share the same typed frame storage while retaining independent native
resume state.

Named-event evidence now fixes trigger/wait races in source order: a blocking
trigger before its waiter is missed, a waiter armed before a blocking trigger
is caught, and ordinary or zero-delay nonblocking triggers wake already armed
waiters. Dynamic fork children prove stable same-slot NBA ordering, while the
active, inactive `#0`, update/NBA, and postponed regions preserve their exact
order. `$strobe` now samples supported direct packed-signal operands in the
postponed region, after same-slot NBA publication, rather than formatting an
active-region snapshot.

Dynamic child execution points carry both runtime and static design-process
identity, so debugger process names and local schemas remain bounds-safe while
local reads address the child's live shared frame. The dedicated fork
application proves join variants, cancellation, shared scoped locals, child
safe points, callbacks, normalized VCD, interpreter/LLVM O0/O2 parity, and
cold/warm native reuse. Append-only native resume statuses 11 through 14 cover
fork creation/end/wait/disable; schema 57 records ordered branches, join kind,
one-shot postponed observation, operations, and provenance. Branch and join
edits prove cache invalidation without changing the runtime-v1 callback
structure.

Stable frontend, elaboration, SimIR, and LLVM checks cover language modes,
labels, terminators, callable lifetime, ownership, branch targets, duplicate
entries, parent continuation, join kind, and boundary status. The diagnostic
catalog covers all 1,324 production codes, and the source gate covers 310
authored files with an empty allowlist and a 2,000-line maximum. The exact
LLVM 22.1.8 warnings-as-errors Debug regression passed all 61 tests in 450.47
seconds, including scoped locals in 0.91 seconds, LLVM in 4.99 seconds, named
events in 1.01 seconds, fork in 0.14 seconds, procedural assignments in 2.55
seconds, containers in 347.55 seconds, and the monolithic application in 40.41
seconds. Release passed all 61 tests in 140.49 seconds, including scoped
locals in 0.81 seconds, LLVM in 3.33 seconds, named events in 0.83 seconds,
fork in 0.08 seconds, procedural assignments in 1.90 seconds, containers in
83.33 seconds, and the monolithic application in 14.11 seconds, on
2026-07-31. Batch 105 is not a ten-batch CI-inspection boundary, so no Actions
run was inspected.

### One-hundred-sixth feature batch — SystemVerilog delay, primitive, and continuous-assignment closure

SystemVerilog delay HIR now retains locally constant integral expressions in
single, min/typ/max, and rise/fall/turnoff alternatives while preserving each
module's time-unit scale. Parameter and localparam substitution occurs per
specialization before checked tick normalization. Unknown, negative, and
overflowing values fail through stable diagnostics rather than being silently
accepted or truncated.

Scalar and packed net declarations now retain declaration delays and lower
initializers as continuous drivers. Generated declarations follow the same
path. Net delay is applied after specialization and combines transition by
transition with any explicit continuous-assignment delay, including checked
overflow. A delayed net can establish automatic project precision even when
it is the only timed object.

Named, statically bounded arrays of the supported logic and tri-state
primitives expand into stable indexed continuous processes. Direct packed
terminals map ordinally across differing declared directions while scalar
terminals broadcast. `bufif0`, `bufif1`, `notif0`, and `notif1` implement
four-state enable behavior, Z turnoff, and one-, two-, or three-value delays.
MOS, bidirectional-switch, resistive, pull, strength, and path-delay families
remain explicitly deferred rather than partially parsed.

Continuous-driver evidence covers whole and disjoint slice ownership,
generated net initialization, explicit-plus-net delay composition,
rise/fall/turnoff selection, inertial pulse cancellation, preservation of an
already pending same-value transaction, zero-delay update-region publication,
multiple parameter specializations, callbacks, debugger reads and indexed
process names, and normalized VCD. Interpreter and LLVM O0/O2 cold/warm runs
agree, while parameter, delay, primitive, range, and generated-source edits
invalidate schema-58 objects without changing the public native ABI.

Stable parser, semantic, elaboration, and unsupported-feature diagnostics
cover malformed arrays, missing instance names, nonstatic or oversized ranges,
terminal-width mismatches, delayed variable declarations, invalid constant
delays, normalization/composition overflow, and the explicitly deferred
primitive families. The diagnostic catalog covers all 1,335 production codes,
and the source gate covers 310 authored files with an empty allowlist and a
2,000-line maximum.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 61 tests
in 455.69 seconds, including scoped locals in 0.90 seconds, LLVM in 2.94
seconds, transition delays in 1.46 seconds, named events in 1.01 seconds, fork
in 0.12 seconds, procedural assignments in 2.53 seconds, containers in 356.83
seconds, and the monolithic application in 39.68 seconds. Release passed all
61 tests in 141.87 seconds, including scoped locals in 0.79 seconds, LLVM in
2.58 seconds, transition delays in 0.57 seconds, named events in 0.83 seconds,
fork in 0.07 seconds, procedural assignments in 1.91 seconds, containers in
87.40 seconds, and the monolithic application in 13.58 seconds, on
2026-07-31. Batch 106 is not a ten-batch CI-inspection boundary, so no Actions
run was inspected.

### One-hundred-seventh feature batch — SystemVerilog interface, modport, and package visibility closure

SystemVerilog interfaces are now retained as distinct parameterizable design
units with time context, packed members, continuous and procedural behavior,
functions, tasks, modports, and stable source identity. Bounded static interface
arrays expand in declared order, and explicit interface/modport module ports
bind named or positional whole-interface and indexed-array actuals through
nested generated hierarchy with exact member aliases.

Modports retain input/output/inout/ref signal views and function/task
import/export entries. Input views are read-only after direct or forwarded
binding; writable views participate in path-aware ownership so one nested
forwarding chain is not mistaken for multiple drivers. Imported callables are
qualified per specialized interface port and execute through ordinary callable
frames and safe points. Export entries require a matching module callable.

Package exports now filter imported constants, packed types, functions, and
tasks through selective, `package::*`, and `*::*` forms. Qualified and wildcard
imports traverse explicit re-exports without leaking private imports, while
collision, malformed export, missing item, and recursive visibility paths fail
deterministically with transitive package/interface source provenance.

The dedicated application differential covers parameterized interface arrays,
positional and named ports, nested generated forwarding, function/task access,
`ref`, export validation, debugger aliases, callbacks, normalized VCD,
interpreter and LLVM O0/O2, cold/warm reuse, and callable/export edits. Native
schema 59 records the resulting specialized SimIR graph without changing the
public ABI. Feature-matrix rows SV-631 through SV-640 are the detailed release
evidence.

The diagnostic catalog covers 1,363 production codes, and the source gate
covers 316 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 62 tests
in 450.73 seconds, including scoped locals in 0.90 seconds, interfaces in 1.89
seconds, LLVM in 3.64 seconds, containers in 346.90 seconds, and the monolithic
application in 40.34 seconds. Release passed all 62 tests in 144.44 seconds,
including scoped locals in 0.83 seconds, interfaces in 1.60 seconds, LLVM in
3.50 seconds, containers in 85.63 seconds, and the monolithic application in
13.64 seconds, on 2026-07-31. Batch 107 is not a ten-batch CI-inspection
boundary, so no Actions run was inspected.

### One-hundred-eighth feature batch — SystemVerilog preprocessing, directives, and generate specialization closure

The preprocessor now expands complete include-argument token sequences, so
quoted or angle paths may be assembled from multiple nested macros. Identical
macro redefinitions remain legal, conflicting definitions retain the prior
location in a targeted diagnostic, and `` `undefineall`` clears compilation-
unit macro state. Conditional frames opened by an include must close there,
and an include cannot continue or close its parent's frame; unmatched,
duplicate, inactive, and post-else recovery remains deterministic.

Parser-visible timescale, default-nettype, reset, cell, keyword-version, and
unconnected-drive state retains source ordering across existing file,
source-set, and combined compilation policies. Duplicate cell entry and an
unmatched drive reset are now checked. Preprocessor identity advances to v5;
exact physical/logical source ancestry, ordered roots, includes, mappings, and
macro expansion stacks remain cache and diagnostic inputs.

Unlabeled generated blocks now receive stable source-ordered `genblkN` names.
Generated localparams evaluate in declaration order for each concrete genvar
iteration before dependent ranges, typedefs, signal objects, function/task
profiles, net delays, process expressions, instance overrides, and connections
specialize. Nested generated type environments preserve lexical visibility and
shadow restoration, and user-defined function return types remain unambiguous
before a function-name parenthesis.

The dedicated application differential covers macro-composed includes,
`` `undefineall``, varying-width generated typedefs, function/task calls,
delayed nets, child overrides, deterministic hierarchy paths, debugger signal
lookup, normalized VCD, interpreter and LLVM O0/O2, cold/warm reuse, and an
include edit. Native schema 60 records the specialized graph without changing
the public ABI. Feature-matrix rows SV-641 through SV-650 are the detailed
release evidence.

The diagnostic catalog covers 1,368 production codes, and the source gate
covers 317 authored files with an empty allowlist and a 2,000-line maximum.
The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 63 tests
in 443.90 seconds, including scoped locals in 0.89 seconds, preprocessing and
generate specialization in 1.24 seconds, LLVM in 2.83 seconds, containers in
342.96 seconds, and the monolithic application in 39.11 seconds. Release
passed all 63 tests in 145.66 seconds, including scoped locals in 0.81 seconds,
preprocessing and generate specialization in 1.12 seconds, LLVM in 2.66
seconds, containers in 88.05 seconds, and the monolithic application in 13.59
seconds, on 2026-07-31. Batch 108 is not a ten-batch CI-inspection boundary,
so no Actions run was inspected.

### One-hundred-ninth feature batch — SystemVerilog aggregate, multidimensional array, pattern, cast, and nominal-legality closure

Named packed structs, unions, and enums now retain recursive member type
structure, enum literals, declaration order, layout, source provenance, and
nominal identity through typedef aliases and type-parameter substitution.
Chained constant member reads/writes accumulate exact nested offsets.
Recursive positional, member-keyed, and default assignment patterns build a
fresh contextual value before one atomic update, while packed unions require
exactly one selected member. Bounded unpacked structs admit scalar, enum, and
nested packed or unpacked-struct members through deterministic flattened
storage while retaining nominal metadata.

Locally constant static arrays now retain one through four declaration-ordered
dimensions and up to 4,096 dense elements. Direction-aware row-major
flattening supports full-rank constant and runtime signed-32 indexing,
per-dimension bounds checks, system queries, whole-value copies, exact-rank
same-language hierarchy ports, and automatic function/task boundaries.
Recursive positional, integral-keyed, and default patterns initialize ranked
arrays atomically. A selected one-dimensional slice normalizes its dimension
metadata after selection; multidimensional subarray slices remain explicitly
deferred.

Visible named or bounded builtin SystemVerilog casts now retain a target type.
Explicit aggregate casts authorize compatible conversion, while direct
assignment and equality diagnose distinct nominal aggregate types. The
dedicated differential transports nested packed/unpacked aggregates and a
multidimensional array through generated hierarchy, a type parameter,
functions, tasks, debugger container reads, callbacks, VCD, interpreter, LLVM
O0/O2, cold/warm reuse, and source-edit invalidation. Native schema 61 and
container semantic revision 25 record recursive type identity, ordered
dimensions, and linearized access without changing the public ABI.
Feature-matrix rows SV-651 through SV-660 are the detailed release evidence.

The optimized gate exposed and repaired a GCC maybe-uninitialized warning in
SystemC hierarchy range materialization. The Debug gate also exposed stale
one-dimensional slice-dimension metadata, which was repaired before the final
container regression. The diagnostic catalog covers 1,393 production codes,
and the source gate covers 319 authored files with an empty allowlist and a
2,000-line maximum. The exact LLVM 22.1.8 warnings-as-errors Debug regression
passed all 64 tests in 446.15 seconds, including scoped locals in 0.89 seconds,
LLVM in 3.86 seconds, containers in 342.38 seconds, the aggregate/
multidimensional differential in 0.70 seconds, and the monolithic application
in 39.75 seconds. Release passed all 64 tests in 145.46 seconds, including
scoped locals in 0.80 seconds, LLVM in 2.70 seconds, containers in 87.53
seconds, the aggregate/multidimensional differential in 0.32 seconds, and the
monolithic application in 13.64 seconds, on 2026-07-31. Batch 109 is not a
ten-batch CI-inspection boundary, so no Actions run was inspected.

### One-hundred-tenth feature batch — SystemVerilog string, file, container, memory, and release-row closure

The final bounded SystemVerilog v1 audit closes ten related slices. Mutable
byte strings now execute the standard non-real methods and integer
conversions, bounded `$swrite`/`$sformat`/`$sformatf`, and direct same-language
input/output/inout module-port aliases with input read-only and path-aware
writer ownership. Character pushback, formatted scanning, binary `$fread`,
position/query/rewind/flush operations, and binary mode aliases extend the
manifest-confined file service without exposing host descriptors.

`$writememb` and `$writememh` serialize exact one-dimensional fixed memories.
Dynamic-array `new[size](initializer)` snapshots aliased initializers and
applies domain-correct tail defaults, while queues execute checked indexed
`insert` and `delete`. One-through-64-bit nominal packed struct, union, and
enum elements now traverse every supported static/dynamic/queue/associative
container and one-dimensional memory-file operation. The aggregate,
multidimensional, generated-hierarchy, type-parameter, callable, debugger,
callback, VCD, interpreter, LLVM O0/O2, cold/warm, HDL-edit, and external-file-
edit matrices retain exact nominal identity and deterministic behavior.

Feature-matrix rows SV-661 through SV-670 are the detailed evidence. The
release-row audit moves V1-SV-01 through V1-SV-08 beside V1-SV-09 in the
completed v1 group. The bounded contract explicitly defers real/Unicode and
wider-than-64-bit runtime data, string-element containers, multidimensional
memory-file operands, standard/multichannel and postponed-monitor file
extensions, unsupported primitive families, and the listed general control/
callable extensions; none is silently accepted. Native-object schema 68 and
container semantic revision 28 record the final supported graph without a
public runtime ABI change.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 64 tests
in 485.70 seconds, including scoped locals in 0.92 seconds, LLVM in 3.10
seconds, mutable strings in 0.94 seconds, files in 2.11 seconds, aggregate/
multidimensional execution in 2.03 seconds, containers in 375.83 seconds, and
the monolithic application in 40.66 seconds. Release passed all 64 tests in
159.27 seconds, including scoped locals in 0.79 seconds, LLVM in 2.71 seconds,
mutable strings in 0.41 seconds, files in 0.68 seconds, containers in 99.00
seconds, and the monolithic application in 14.10 seconds, on 2026-08-01. The
diagnostic catalog covers 1,407 production codes, and the source gate covers
332 authored files with an empty allowlist and a 2,000-line maximum.

The mandatory Batch 110 inspection first ran as Actions run `30709270776`.
Windows MSVC Debug proved `fsim.application.scoped_locals` remained quick at
0.28 seconds, but the operation/test-host refactor exposed insufficient stack
reservation across other MSVC-compatible test executables: MSVC jobs reported
early access violations or stopped making progress in later monolithic tests.
The run was cancelled once every remaining Windows job was nonproductive.
Repair commit `a5d69e0` moves the 8 MiB Windows stack reserve into the common
test-target configuration for Debug and Release, including split and merged
hosts, and adds bounded API and Windows application timeouts. Focused local
frontend, source-gate, elaboration, container-elaboration, application, and API
tests passed after an eight-worker rebuild.

Replacement Actions run `30710421676` passed all 12 jobs. Windows MSVC Debug
passed 63/63 in 359.78 seconds, including container elaboration in 0.32
seconds, scoped locals in 0.25 seconds, and the API in 0.29 seconds. Windows
MSVC LLVM Debug passed 64/64 in 1,092.19 seconds, including scoped locals in
1.44 seconds, containers in 887.18 seconds, and the API in 0.54 seconds.
ASan/UBSan passed 63/63 in 516.76 seconds. Batch 110 and its CI boundary are
closed.

#### Post-Batch-110 Debug-footprint and test-link repair

The 115-alternative SimIR `Operation` is now stored as an outer variant of
eight semantic groups with flat construction, query, and visitation helpers.
Five high-instantiation visitors use one typed dispatch body apiece, and the
standalone application sources link into four selector-driven hosts while
retaining every named CTest as a separate process. GCC Debug compilation uses
`-Og` plus compressed DWARF, and Ninja link/archive concurrency remains eight.

Exact single-action measurements reduced `lowerer_process.cpp` peak Debug
compilation memory from 5,003,456 KiB to 911,748 KiB,
`runtime_execution_tests.cpp` from 7,789,344 KiB to 954,596 KiB, and one GNU
ld.bfd application-host link from 5,309,296 KiB to 1,398,856 KiB. The final
Debug tree is 3,748,237,353 bytes; four application hosts total 338,591,400
bytes, and the elaboration archive is 230,510,732 bytes.

The optimized gate exposed an invalid-free path in GCC 13 nested-variant
temporary construction; the selected group is now constructed in place. It
also made a latent fork
runtime use-after-free deterministic: child insertion could relocate the
process vector before the next read through the parent reference, so immutable
parent metadata is snapshotted before growth. Focused LLVM-disabled ASan/UBSan
elaboration and file-application tests pass. The exact LLVM 22.1.8 Debug and
Release regressions pass 64/64 in 114.51 and 95.40 seconds, with scoped locals
in 1.07/1.17 seconds. The source gate covers 334 authored files with an empty
allowlist and a 2,000-line maximum. GitHub Actions build parallelism is four;
local builds and the Ninja linker pool remain eight.

### One-hundred-eleventh feature batch — VHDL analysis order, packages/bodies, contexts, and configurations

VHDL files remain independently parsed, including with eight parser workers,
but their design units now enter semantic analysis in exact source-set, file,
and within-file order. A project-level validator requires an earlier
same-library entity for each architecture, an earlier declaration for each
package body, earlier project packages and contexts for use/context clauses,
and earlier entity, architecture, or configuration targets for explicit
configuration bindings. `work` is resolved relative to each referencing unit,
external `ieee`/`std` visibility remains available, and recursive block
configurations receive the same checks. Eight stable `FSIM-FE-VHORDER-*`
diagnostics distinguish the dependency classes.

The audit also found that package-body context clauses were retained in HIR
but context expansion used only the declaration's context. Declaration and
body contexts now compose before package specialization. Body-local imports
therefore participate in callable evaluation, and body plus transitive package
source dependencies follow exported functions, procedures, and generic
subprogram templates through callable binding into native-cache provenance.
The function-package differential imports a helper constant only from its
body, edits that helper, proves the runtime result changes in the interpreter
and LLVM O0/O2, and proves only the dependent specialization key changes.

Previously permissive configuration/component application fixtures were
reordered into legal analysis sequences; the recursive configuration fixture
was split so its referenced wrapper configuration precedes the architecture
specification that names it. Feature-matrix rows VH-190 through VH-199 are the
detailed positive, negative, elaboration, runtime, cache, debug, and
portability evidence. The diagnostic catalog covers 1,415 production codes,
and the source gate covers 336 authored files with an empty allowlist and a
2,000-line maximum.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 65 tests
in 112.30 seconds, including analysis order in 0.02 seconds, package-body
function provenance in 0.32 seconds, configurations in 0.27 seconds,
components in 0.40 seconds, scoped locals in 0.98 seconds, and containers in
112.30 seconds. Release passed all 65 tests in 97.27 seconds, including
analysis order in 0.02 seconds, package-body function provenance in 0.32
seconds, configurations in 0.25 seconds, components in 0.39 seconds, scoped
locals in 1.03 seconds, and containers in 97.27 seconds, on 2026-08-01. Batch
111-focused LLVM-disabled ASan/UBSan elaboration, analysis-order, package-body,
configuration, and component tests also pass. Batch 111 is not a ten-batch
CI-inspection boundary, so no Actions run was inspected.

### One-hundred-twelfth feature batch — VHDL semantic resolution closure — Complete

The current ten implementation tasks are:

1. Retain every ordinary VHDL function and procedure declaration in an ordered
   overload set keyed by its canonical designator and exact source identity,
   while preserving SystemVerilog's existing single-visible-function rules.
2. Resolve local function and procedure calls by positional/named association
   shape, defaults, formal class/mode, actual base type, and contextual function
   result type without mutating or prematurely lowering rejected candidates.
3. Preserve overload sets through selected and wildcard package visibility,
   direct selected package names, package bodies, contexts, generic-package
   materialization, and declaration-level source provenance/cache invalidation.
4. Complete case-insensitive simple, selected, indexed, slice, attribute,
   enumeration-literal, and subprogram-name lookup with deterministic hiding,
   homograph, ambiguity, and missing-name diagnostics at the correct region.
5. Complete contextual universal-literal, conversion, aggregate, attribute,
   operator, nested-call, and expected-result typing so overload resolution uses
   VHDL base-type and nominal-composite rules rather than width-only heuristics.
6. Extend locally static constant evaluation through visible/package constants,
   enumeration and array bounds, aliases, conversions, attributes, operators,
   and eligible pure time-free user functions with overflow/range diagnostics.
7. Add a dedicated VHDL legality pass for duplicate or nonconforming subprogram
   profiles and bodies, purity violations, incomplete declarations, illegal
   object classes/modes/defaults, and context-dependent call restrictions.
8. Represent and validate scalar and composite VHDL resolution indications,
   including visible resolution-function selection, required parameter/result
   profiles, resolved-subtype identity, and illegal unresolved multi-driver use.
9. Execute supported user resolution functions deterministically for independent
   process drivers in the interpreter and LLVM O0/O2, retaining exact Logic9,
   update/delay semantics, VCD/debug views, native ABI validation, and cache
   identity/provenance.
10. Add focused positive/negative frontend and elaboration fixtures plus
    interpreter/LLVM O0/O2, cache-edit, debug/VCD, sanitizer, source-budget,
    diagnostic-catalog, full Debug/Release, documentation, commit, and push
    evidence before marking Batch 112 complete.

Batch status is **complete**. The implementation replaces
one-name/one-index callable maps with ordered overload sets, preserves same-name
package imports by declaration identity, adds a separate contextual overload
selector, and keeps wildcard sensitivity conservative across all candidates.
VHDL function-call parsing now retains positional and named associations, and
bounded function and input-mode procedure formals retain executable defaults.
The selector recursively validates nested function profiles without lowering
rejected candidates and uses the assignment result context to distinguish
otherwise identical argument profiles. Named-formal shape and omitted defaults
also participate in function and procedure selection; a non-input procedure
default and a positional function actual after a named actual have dedicated
legality diagnostics.
Callable imports now carry their package visibility owner. Directly visible
local function/procedure designators hide use-visible package overloads, while
same-profile homographs from different wildcard-imported packages remain
distinct candidates and report call-site ambiguity instead of a false local
duplicate. Context-expanded and locally instantiated generic-package overload
sets are included in the focused fixture.
Explicit integer-family subtype conversions and nominal enumeration
conversions now provide their selected type to overload filtering and execute
with the destination range checks. Package specialization diagnoses
nonconforming and missing ordinary function/procedure bodies. The callable
legality slice also rejects mismatched formal defaults and pure functions that
read signals or call procedures; the frontend retains those procedure calls so
the semantic diagnostic is issued at the function declaration.
The exact LLVM Debug elaboration target builds with eight workers. The focused
elaboration suite passes in 0.14 seconds with local, same-package,
cross-package wildcard, and two-/three-part direct-selected integer/Boolean
function and procedure selection plus no-match, ambiguity, and
duplicate-profile diagnostics. It additionally covers result-only, named,
defaulted, nested-call, direct-over-use hiding, imported-homograph ambiguity,
context, generic-package overloads, conversions, body conformance, purity, and
default legality. The integer-family precheck admits an
integer-returning overload candidate before contextual call selection, and
qualified ordinary packages materialize their callable set with exact body and
transitive source dependencies. The focused application differential passes in
1.78 seconds across the interpreter, LLVM O0/O2 cold/warm reuse, explicit
integer/nominal-enumeration conversions, and an overloaded pure package
function used during locally static constant evaluation. Declaration-ordered
package constants now fold integer-subtype conversions, scalar and array type
attributes, arithmetic operators, dependent array bounds, and nested
same-designator overloaded pure-function calls. Aggregate, indexed-name, and
slice actuals select their array/scalar profiles in the same runtime matrix. A
scalar `bit`
resolution indication now retains and validates its pure array-input/base-result
function. The same bounded OR/AND resolver model now covers exact Logic9
drivers, arrays of resolved scalar elements, and a resolver plus resolved
subtype imported from a visible package. Independent delayed-driver values,
including exact `H` and `L`, elementwise composite results, and VCD timelines
agree across the interpreter and LLVM O0/O2 cold/warm runs. Unsupported,
invisible, ambiguous, and wrong-profile resolution functions have distinct
diagnostics. Missing expression-context function/type marks, missing or
wrong-context procedure calls, same-width nominal aggregate ambiguity, static
conversion range violations, and integer overflow now have focused negative
evidence. The differential also retains a package-body edit that changes
results and specialization keys. The synchronized diagnostic catalog covers
1,435 production codes, and all 340 authored sources pass the 2,000-line gate.
All ten tasks are complete. The LLVM-disabled ASan/UBSan focused gate passed
all six selected tests in 3.38 seconds with LeakSanitizer disabled because the
local ptrace environment cannot run it. The exact LLVM 22.1.8 warnings-as-errors
Debug regression passed all 66 tests in 172.72 seconds; the overload application
passed in 1.73 seconds and the scoped-locals application passed in 0.79 seconds.
The corresponding Release regression passed all 66 tests in 155.90 seconds;
the overload application passed in 1.76 seconds and scoped locals passed in
0.78 seconds. The diagnostic catalog retained 1,435 production codes and the
source gate accepted all 340 authored sources.
Native-object schema 69 prevents reuse across the changed overload/static
lowering contract; the public runtime ABI and container semantic revision 28
remain unchanged.

### One-hundred-thirteenth feature batch — VHDL generic and instantiation closure — Complete

The current ten implementation tasks are:

1. Complete entity, architecture, component, and directly instantiated design
   unit generic interfaces while retaining declaration order, class, subtype,
   mode, default, and source identity.
2. Resolve positional and named generic associations, `open` defaults, mixed
   ordering, duplicate/unknown formals, missing required actuals, conversions,
   and locally static legality deterministically.
3. Evaluate generic defaults and actuals in declaration order, including
   references to earlier generics, visible package constants, attributes,
   conversions, aggregates, and bounded pure functions.
4. Complete component declaration conformance and default/explicit binding plus
   direct entity, architecture, and configuration instantiation with exact
   generic and port profile checking.
5. Give every effective value, type, subprogram, and package generic binding a
   stable specialization identity with complete transitive cache provenance and
   deterministic sharing between equivalent instances.
6. Admit input-port expressions, conversions, qualified expressions, slices,
   concatenations, and aggregates while enforcing writable-name legality for
   output, buffer, and inout actuals.
7. Implement `open` port actuals, input defaults, unconnected output/buffer
   behavior, association-order rules, and mode-specific missing/illegal-open
   diagnostics.
8. Propagate generic-dependent scalar and composite constraints through formal
   ports, component views, direct instances, hierarchy aliases, and boundary
   range/direction checks.
9. Execute multiple differently specialized component/direct instances
   identically in the interpreter and LLVM O0/O2 with scheduling, VCD, debugger,
   hierarchy, cache reuse, and source-edit invalidation evidence.
10. Add focused positive/negative parser, elaboration, and runtime differentials;
    update the matrix and diagnostics; then pass sanitizer, source/catalog, full
    Debug/Release, documentation, commit, and push gates before closing Batch 113.

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 114 below.

Current Batch 113 evidence: the frontend retains `open` generic actuals as
explicit default selections and retains indexed/general expression port actual
HIR instead of issuing the former parser-only unsupported diagnostics.
Component and direct-entity value generics now materialize valid selected
defaults, while direct `open` on a required generic reports the existing
generic-actual diagnostic. Entity input-port defaults are retained and
materialized for omitted or explicit-`open` direct associations; non-input
defaults have a dedicated legality diagnostic. Static literals and array
aggregates plus dynamic arithmetic, slice, and concatenation input actuals use
owned boundary signals and per-instance VHDL drivers, while output/buffer/inout
expressions require writable signal names. The partitioned port-boundary
implementation keeps `hierarchy_types.cpp` at 2,000 lines. The focused frontend,
elaboration, component interpreter/LLVM O0/O2 cache differential, diagnostic
catalog, and 343-source line-budget evidence passes in the LLVM Debug tree.
All ten implementation tasks and the complete closure gates now pass.

Task 1 now has focused implementation evidence. Implicit and explicit VHDL
value-generic interfaces normalize to retained constant class and input mode;
illegal classes and modes have dedicated frontend diagnostics. Component
profile conformance and specialization identity include that metadata, while
the existing ordered type/function/procedure/package generic families remain
unchanged. The Task 3 slice now proves declaration-ordered dependent defaults
for direct and component instances, including an earlier overridden generic
followed by `open`, across interpreter and LLVM O0/O2 cold/warm execution.
Task 3 now also has focused implementation evidence and is complete. Bounded
up-to-64-bit packed value generics accept contextual string/literal/aggregate
defaults and actuals with width/state/range checks. A second direct-instance
matrix folds a visible package constant, array `'length`, a built-in `positive`
conversion, earlier generic references, and a visible pure package function;
an earlier override followed by three `open` associations recomputes the
dependent chain. Both matrices agree across interpreter and LLVM O0/O2
cold/warm cache execution. The packed generic validation is partitioned into
`hierarchy_generic_interfaces.cpp` so the protected hierarchy source remains
under budget.

Task 4 now has focused implementation evidence and is complete. Direct
`configuration [library.]name` instances retain a distinct HIR marker, accept
the standard optional generic and port maps, and resolve one previously
analyzed configuration to its selected root architecture before ordinary
generic specialization and port connection. The selected configuration's
recursive rules, physical source, and canonical identity propagate beneath the
direct instance and into its specialization/cache key. Missing and ambiguous
configuration declarations use the existing targeted configuration
diagnostics; direct use before analysis is rejected by the project-order gate,
including instances nested in generate bodies. The runtime configuration
differential now exercises direct configuration instances across interpreter
and LLVM O0/O2 cold/warm builds and proves selective invalidation after the
selected configuration changes. The focused frontend, elaboration, analysis
order, configuration application, 1,436-code catalog, and 343-source line
budget gates pass in the LLVM Debug tree.

Task 2 now has complete focused evidence. Parser HIR and diagnostics cover
positional associations followed by named associations, forbidden
named-then-positional ordering, case-normalized names, repeated names, and
explicit `open`. Elaboration independently rejects unknown and excessive
actuals, duplicate formal coverage, invalid association order, missing or
defaultless `open` formals, and non-static expressions with stable
`FSIM-ELAB-GENERIC-001` through `004` diagnostics. Positive direct/component
execution covers positional, named, mixed positional-then-named, default/open,
integer-subtype conversion, and declaration-dependent maps. Task 2 is
focused-complete.

Task 6 now has complete focused evidence. In addition to literals, aggregates,
arithmetic, slices, and concatenations, the frontend retains VHDL
`type_mark'(expression)` qualification distinctly from a conversion call.
Same-language input boundaries validate the qualification mark against the
formal type, materialize qualified static literals/aggregates, and synthesize
drivers for qualified dynamic slices and converted concatenations. A
mismatched qualification has a deterministic `FSIM-ELAB-VHPORT-001`
diagnostic, while every output/buffer/inout expression still requires a
writable signal through `FSIM-ELAB-VHPORT-002`. The component interpreter and
LLVM O0/O2 cold/warm differential proves the qualified and converted dynamic
paths. Task 6 is focused-complete.

Task 7 now has complete focused evidence. Port maps accept positional followed
by named associations and reject duplicate names or a positional actual after
a named one with stable `FSIM-VHDL-SEM-078/079` diagnostics; component
normalization independently rejects malformed retained association order.
Omitted and explicit-`open` input ports materialize a retained default, while a
required input reports the component- or direct-boundary diagnostic. Output
and buffer formals may be omitted or explicitly opened, retain child-local
signals, and execute their assignments without manufacturing a parent driver.
Non-input expression actuals remain illegal. The focused interpreter and LLVM
O0/O2 cold/warm differential observes exact output/buffer values. The latest
catalog has 1,438 production codes and all 343 authored sources pass the line
budget. Task 7 is focused-complete.

Task 8 now has complete focused evidence. Generic-selected scalar and packed
array constraints specialize entity ports, component views, direct instances,
and hierarchy aliases before connection. The elaboration matrix proves that
4-element component aliases and a 9-element defaulted direct alias share the
exact parent signal identities. Same-language packed-array boundaries now
reject a width mismatch with `FSIM-ELAB-BIND-020` and reject equal-width bound
or direction mismatches with `FSIM-ELAB-BIND-031`, preventing silently
misindexed aliases. The application executes separate 4-bit component and
8-bit direct specializations across interpreter and LLVM O0/O2 cold/warm runs
with exact results and cache keys. The latest catalog has 1,439 production
codes and all 343 authored sources pass the line budget. Task 8 is
focused-complete.

Task 5 now has complete focused evidence. Versioned
`vhdl-component-binding-v6` identity retains the complete component profile,
effective value/type/function/procedure/package actual identities, defaults,
selected target, binding/configuration identity, and transitive physical-source
provenance. Direct signal actuals are represented as specialization-neutral
aliases because their parent-local spelling is connected after child
specialization. Two width-4 component instances connected to differently named
parent signals therefore share one deterministic specialization key, while an
otherwise equivalent width-8 direct instance remains distinct. Existing
default, callable, profile, and configuration edits continue to invalidate only
their affected consumers, and warm native-cache reuse remains exact.

Task 9 now has complete focused evidence. The component application elaborates
two equivalent width-4 component instances and a distinct width-8 direct
instance, proves their exact hierarchy aliases and final values, records source
debugger execution points, and emits deterministic VCD for all three specialized
outputs. Interpreter, LLVM O0, and LLVM O2 cold/warm runs produce identical
values and VCD, reuse warm native objects, and preserve the existing selective
default, callable, component-profile, and configuration source-edit
invalidation evidence.

Task 10 is complete. The exact LLVM 22.1.8 warnings-as-errors Debug regression
passed all 66 tests in 170.07 seconds; the component application passed in 0.87
seconds and `fsim.application.scoped_locals` passed in 0.83 seconds. Release
passed all 66 tests in 156.91 seconds; the component application passed in 0.91
seconds and scoped locals passed in 0.84 seconds. The LLVM-disabled ASan/UBSan
focused gate passed frontend, catalog, source-budget, elaboration, component
application, and runtime tests in 3.13 seconds with LeakSanitizer disabled only
because the managed ptrace environment cannot start it. The catalog retains
1,439 production codes, all 343 authored sources pass the 2,000-line gate, and
`elaboration_vhdl_components.cpp` is exactly 2,000 lines. Component binding
identity version 6 partitions the alias-neutral identity contract; native-object
schema 69, the public runtime ABI, and container semantic revision 28 remain
unchanged. All ten Batch 113 tasks are complete.

### One-hundred-fourteenth feature batch — VHDL generated and local declarative-region closure — Complete

The current ten implementation tasks are:

1. **Complete.** Retain guarded block syntax, guard expressions, optional `is`, opening/end
   labels, and exact source regions without silently accepting unsupported
   guarded forms.
2. **Complete.** Elaborate the implicit Boolean `GUARD` signal, guard-expression sensitivity,
   activation changes, nested scope identity, and deterministic diagnostics for
   invalid or non-Boolean guards.
3. **Complete.** Complete block generic/port clauses and maps, defaults, `open` behavior,
   profile legality, hierarchy aliases, and specialization/cache provenance.
4. **Complete.** Parse and elaborate declarative parts for `if`, `for`, and `case` generate
   alternatives before their `begin`, retaining alternative and iteration scope.
5. **Complete.** Support generated type and subtype declarations with declaration-ordered
   visibility, specialized constraints, nominal identity, and source provenance.
6. **Complete.** Support generated function/procedure declarations, bodies, and bounded
   instantiations with local overload visibility and scope-qualified identity.
7. **Complete.** Complete generated constants, signals, aliases, component declarations,
   package instantiations, and nested declarative items with deterministic
   collision and unsupported-item diagnostics.
8. **Complete.** Complete architecture, block, generate, process, and subprogram local
   declarative regions for bounded constants, types/subtypes, objects, aliases,
   packages, and non-suspending local callables.
9. **Complete.** Complete remaining locally static `if`/`for`/`case` generate choices,
   including enumeration/character choices, grouped choices, ranges, `others`,
   overlap/null handling, labels, hierarchy, and specialization identity.
10. **Complete.** Add focused positive/negative parser, elaboration, and runtime differentials;
    update matrix/diagnostics/docs; then pass sanitizer, source/catalog, full
    Debug/Release, documentation, commit, and push gates before closing Batch 114.

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 115 below.

Tasks 1 and 2 are focused-complete. The frontend now retains a guarded block's
expression in `GenerateRegion::condition`, preserves optional `is`, scope/end
labels, and exact source spans, and reports `FSIM-VHDL-PARSE-231` for a missing
closing parenthesis instead of issuing the retired blanket unsupported error.
Elaboration synthesizes a scope-qualified Boolean `GUARD` signal plus a
reactive implicit-inertial driver from the enclosing expression; parent-local
names are qualified before the implicit declaration shadows them. A statically
non-Boolean expression reports `FSIM-ELAB-GEN-013`. Focused elaboration proves
the exact `guarded_scope.guard` hierarchy and false-to-true activation. The
application differential agrees across interpreter and LLVM O0/O2 cold/warm
runs, including scheduling, normalized VCD, and cache reuse. Batch 114 remains
**in progress** with Tasks 3 through 10 outstanding.

Task 3 is focused-complete. Block generic and port clauses/maps retain
positional-then-named associations, declaration-ordered defaults, explicit
`open`, and dependent port constraints. Expansion is delayed until enclosing
value/type/subprogram/local-package specialization completes, then binds block
value, type, function, procedure, and package formals before resolving the
body and ports. Elaboration creates scoped owned signals for defaulted/open
formals and exact hierarchy aliases for direct enclosing-signal actuals,
records scoped callable/package identities and source dependencies, and emits
stable association, formal-mode, type, subprogram-profile, package-profile,
writable-actual, and port-profile diagnostics. The interpreter and LLVM O0/O2
cold/warm application differential agrees on values and VCD, and a block
package-map edit changes the selected package identity, behavior, and native
cache key. The final focused frontend, 1,445-code catalog, 344-source budget,
elaboration, and merged-application gate passes in 15.91 seconds. Tasks 1
through 3 are focused-complete; Batch 114 remains **in progress** with Tasks 4
through 10 outstanding.

Task 4 is focused-complete. Conditional then/else, iterative, and labeled
case-alternative bodies retain bounded constants, signals, and components in
their pre-`begin` declarative part. A nonempty part now requires `begin` and
reports `FSIM-VHDL-PARSE-234` when it is omitted, while declaration-free
generate bodies retain the legal optional form. Selected expansion folds
declarations in source order and qualifies them under the exact branch,
alternative, or `label[index]` iteration scope; unselected declarations never
enter DesignIR. Focused elaboration and the merged interpreter/LLVM O0/O2
cold/warm/VCD differential cover conditional, case-alternative, and indexed
loop declarations. The final five-test focused gate passes in 16.10 seconds
with all 1,446 production diagnostics cataloged and all 344 authored sources
within the line budget. Tasks 1 through 4 are focused-complete; Batch 114
remains **in progress** with Tasks 5 through 10 outstanding.

Task 5 is focused-complete. Selected conditional, iterative, and labeled case
bodies retain bounded array, enumeration, record, and subtype declarations.
Named types, constants, signals, aliases, components, and callables are merged
by physical source offset for resolution, so a preceding object cannot see a
later type. Concrete branch and loop environments specialize prior-constant
and iteration-dependent constraints before expansion. Every realized type is
scope-qualified beneath its branch or `label[index]` path, while its nominal
identity retains the exact source declaration and gains the realized scope;
same-spelled types in separate alternatives remain independent. Focused HIR
and negative evidence covers all supported declaration kinds, same-region
duplicates, and forward visibility. Elaboration proves distinct per-iteration
widths and nominal identities, and the merged interpreter/LLVM O0/O2 cold/warm
application differential executes generated subtypes without behavior or VCD
drift. The final five-test focused gate passes in 15.30 seconds with all 1,446
production diagnostics cataloged and all 345 authored sources within the
2,000-line limit. Tasks 1 through 5 are focused-complete; Batch 114 remains
**in progress** with Task 6 current and Tasks 7 through 10 pending.

Task 6 is focused-complete. Selected generate bodies retain bounded ordinary
function/procedure declarations, conforming bodies, overloads, value-generic
subprogram templates, and `is new` instances. Physical source ordering limits
each ordinary callable body to its own and earlier local designators; later
callables and later generic templates remain invisible. Expansion merges a
conforming declaration/body pair, qualifies every ordinary/template/instance
name under the exact branch or iteration scope, and reruns bounded generic
materialization only after selected declarations enter the unit. Duplicate
function/procedure profiles, malformed purity prefixes, ordinary forward calls,
and generic forward instantiations retain stable diagnostics. The application
differential executes an ordinary function/procedure through generated generic
function/procedure instances in the interpreter and LLVM O0/O2, proves scoped
instance identities and cold/warm reuse, then edits one generic map and observes
the exact behavior and cache-key change. The generated substitution partition
keeps all 346 authored sources under 2,000 lines. The final five-test focused
gate passes in 15.66 seconds with all 1,447 production diagnostics cataloged.
Tasks 1 through 6 are focused-complete; Batch 114 remains **in progress** with
Task 7 current and Tasks 8 through 10 pending.

Task 7 is focused-complete. Every selected or recursively nested generate body
now carries the bounded constant, signal, type/subtype alias, block-port alias,
component, ordinary/generic callable, and local generic-package families.
Package instance names and generic actuals are qualified beneath the exact
branch or concrete `label[index]` scope, lexical package-selected references
are withheld from premature external-package discovery, and a post-expansion
materialization pass publishes selected package constants, types, callables,
identity, and transitive provenance. Physical-source sorting produces a stable
`FSIM-VHDL-SEM-081` cross-family collision diagnostic, while recognized but
unsupported generated declarations use `FSIM-VHDL-UNSUPPORTED-053` instead of
falling through to a generic missing-`begin` error. Focused frontend and
elaboration evidence covers direct, nested, and iteration-dependent local
packages plus collisions and unsupported items. The merged application test
executes package-selected values in the interpreter and LLVM O0/O2, verifies
scope-qualified package identity, normalized VCD, and cold/warm cache reuse.
The final five-test focused Release gate passes in 14.68 seconds with all 1,449
production diagnostics cataloged and all 346 authored sources within the
2,000-line limit. Tasks 1 through 7 are focused-complete; Batch 114 remains
**in progress** with Task 8 current and Tasks 9 and 10 pending.

Task 8 is focused-complete. Architecture constants and explicit typed object
aliases now join the existing type, signal, package, and callable regions;
generated, process, ordinary-subprogram, and generic-subprogram declarative
parts retain bounded constants, types/subtypes, variables, typed aliases,
local generic-package instances, and nested non-suspending callables. Local
constants specialize in physical order, while generic-template locals defer
until their own actuals are bound. A recursive materialization pass performs
source-ordered package, alias, and callable qualification, hoists executable
callables, reruns local-package specialization after generic instantiation,
and visits nested types plus transitive qualified dependencies. Typed aliases
execute over architecture/generated signals and callable formals/variables;
locally static outer constants remain visible to nested callables.
`FSIM-VHDL-SEM-082`/`083` target local cross-family and duplicate-alias
collisions, while `FSIM-VHDL-PARSE-236` and
`FSIM-VHDL-UNSUPPORTED-054` target malformed or untyped aliases. Frontend and
elaboration evidence covers every retained region, and the merged application
differential executes ordinary and generic local packages, aliases, functions,
and procedures through the interpreter and LLVM O0/O2 with cold/warm reuse,
scoped hierarchy, normalized VCD, and edit-sensitive specialization identity.
The final five-test focused Debug/Release gates pass in 15.93/15.12 seconds
with all 1,453 production diagnostics cataloged and all 347 authored sources
within the 2,000-line limit.
Tasks 1 through 8 are focused-complete; Batch 114 remains **in progress** with
Task 9 current and Task 10 pending.

Task 9 is focused-complete. Selection-generate elaboration now derives the
selector's enumeration domain from retained nominal metadata or its directly
named generic, port, or signal, then resolves identifier and character
enumeration literals, constants, grouped choices, ascending/descending
ranges, null ranges, and `others` to ordinal intervals. Existing
`FSIM-ELAB-GEN-008`/`009`/`010` diagnostics reject non-static selectors,
unknown or nominally mismatched choices, duplicate defaults, and overlapping
integer or enumeration intervals. Focused frontend HIR preserves exact labels,
grouping, range direction, and character spelling. Elaboration and the merged
interpreter/LLVM O0/O2 application differential select the labeled branch,
retain `selected.selected_value` hierarchy, agree on behavior and process
count, reuse cold/warm specialization identity, and change the key and selected
behavior when the enum generic default is edited. The final five-test focused
Debug/Release gates pass in 15.81/15.14 seconds with all 1,453 production
diagnostics cataloged and all 348 authored sources within the 2,000-line
limit. Tasks 1 through 9 are focused-complete; Batch 114 remains **in
progress** with Task 10 current.

Task 10 is complete. The exact LLVM 22.1.8 warnings-as-errors Debug regression
passed all 66 tests in 183.65 seconds, including the merged application in
16.03 seconds and `fsim.application.scoped_locals` in 0.89 seconds. Release
passed all 66 tests in 161.11 seconds, including the merged application in
14.84 seconds and scoped locals in 0.82 seconds. The LLVM-disabled ASan/UBSan
focused six-test gate passed in 46.77 seconds with LeakSanitizer disabled only
because the managed ptrace environment cannot start it. The catalog retains
1,453 production codes and all 348 authored sources pass the 2,000-line gate.
Feature-matrix row VH-229, language-support status, and generate diagnostic
descriptions record the completed enumeration/character choice contract. All
ten Batch 114 tasks are complete.

### One-hundred-fifteenth feature batch — VHDL statement and dynamic-selection closure — Complete

The retained ten implementation tasks are:

1. **Complete.** Audit and retain every remaining synthesizable sequential and concurrent
   statement form in typed HIR, with exact labels, spans, and targeted
   unsupported-form diagnostics.
2. **Complete.** Complete sequential signal/variable assignments, procedure calls, `null`,
   conditionals, loops, and case statements across nested labeled scopes.
3. **Complete.** Add VHDL-2008 matching case statements and matching selected/conditional
   assignments with exact wildcard semantics and deterministic legality checks.
4. **Complete.** Complete discrete case choices with grouped literals, locally static ranges,
   `others`, null ranges, overlap, duplicate, and coverage diagnostics.
5. **Complete.** Complete concurrent simple, conditional, and selected signal assignments,
   including guarded/delay-mechanism interaction and driver identity.
6. **Complete.** Preserve process, loop, case-alternative, and labeled statement scopes in
   hierarchy, name lookup, debugger metadata, and specialization provenance.
7. **Complete.** Lower dynamic packed/composite indices, slices, and chained selections for
   supported expression and assignment targets with checked bounds and direction.
8. **Complete.** Complete sensitivity, scheduling, delta/update ordering, and exact
   interpreter/LLVM behavior for the newly retained statement and selection forms.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cache-edit, hierarchy, debugger, and normalized-VCD differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 115.

Batch status is **complete**. Keep this exact completed ten-task list retained
in both the official plan and resume handoff while Batch 116 is current.

Task 1 is focused-complete. A generalized sequential-label path retains exact
canonical labels and full source spans on assignments, selected assignments,
procedure calls, assertions/reports, waits, `null`, `if`, `case`, and loop
statements; optional `end if`, `end case`, and `end process` labels are checked
against their openings by `FSIM-VHDL-SEM-084`. Sequential VHDL-2008 selected
signal and variable assignments retain distinct signal/blocking assignment
kinds. Architecture and generated regions now retain labeled simple/
conditional/selected assignments and concurrent procedure calls instead of
rejecting or dropping their labels. `process(all)` normalizes to wildcard
sensitivity while `FSIM-VHDL-SEM-085` rejects a mixed explicit list. The final
five-test focused Debug/Release gates pass in 16.21/15.30 seconds with all
1,455 production diagnostics cataloged and all 348 authored sources within
the 2,000-line limit; the late Batch 114 negative fixture is compacted from
2,010 to exactly 2,000 lines. Batch 115 remains **in progress** with Task 2
current and Tasks 3 through 10 pending.

Task 2 is focused-complete. A dedicated VHDL statement application uses a
process-local scalar procedure and selected variable assignment, then executes
a labeled ascending `for` loop containing nested labeled `if`/`else`, exact
`case`, procedure-call, and `null` statements before a sequential selected
signal assignment and labeled permanent wait. The resulting integer value 10
agrees across the interpreter and LLVM O0/O2 cold/warm paths with one process
and stable cache reuse. This directly exercises the generalized Task 1 HIR
through lowering without adding label-dependent behavior ahead of Task 6. The
final five-test focused Debug/Release gates pass in 15.90/14.99 seconds with
all 1,455 production diagnostics cataloged and all 348 authored sources within
the 2,000-line limit. Batch 115 remains **in progress** with Task 3 current
and Tasks 4 through 10 pending.

Task 3 is focused-complete. The parser and compact HIR distinguish VHDL-2008
`case? ... end case?`, matching `select?`, and the two-token `?=` operator.
The appended `vhdl_match_equal` SimIR operator treats `-` symmetrically as a
wildcard, equates 0/L and 1/H, rejects other meta-value comparisons with a
known false result, and retains exact bit matching. Selector/operand domain,
literal width/staticness, wildcard-overlap, and matching-end-marker legality
use stable cataloged diagnostics. Native-object schema 70 prevents stale
reuse. The statement application reaches integer 13 through matching case,
selected-variable, and conditional forms identically in the interpreter and
LLVM O0/O2 cold/warm paths. The final seven-test focused Debug/Release gates
pass in 19.04/18.11 seconds, and scoped locals remains quick at 0.84/0.83
seconds. All 1,460 production diagnostics are cataloged and all 350 authored
sources remain within the 2,000-line limit. Batch 115 remains **in progress**
with Task 4 current and Tasks 5 through 10 pending.

Task 4 is focused-complete. Grouped exact choices and ascending/descending
discrete ranges retain compact HIR for sequential cases and selected
assignments. Validation accepts locally static integer, Boolean, enumeration,
and packed choices of the selector type, treats null ranges as empty, checks
subtype bounds, duplicate/overlap legality, and complete subtype coverage, and
uses stable `FSIM-ELAB-VHDLCASE-001` through `-006` diagnostics. Runtime range
selection uses signed or unsigned inclusive comparisons. The statement
application reaches integer 14 through a descending choice and proves a null
range inert across interpreter and LLVM O0/O2 cold/warm execution. The final
five-test focused Debug/Release gates pass in 15.83/15.19 seconds, with scoped
locals at 0.85/0.88 seconds. The catalog covers 1,466 production diagnostics
and all 352 authored sources pass the 2,000-line gate. Batch 115 remains **in
progress** with Task 5 current and Tasks 6 through 10 pending.

Task 5 is focused-complete. The parser and typed HIR retain `guarded` on
concurrent simple, conditional, and selected signal assignments plus explicit
`null` waveform elements. Generated-block expansion binds each statement to
the block's implicit Boolean `GUARD`; every concurrent statement remains a
separate reactive process and therefore keeps a stable driver identity. The
active branch preserves its inertial/transport mechanism and projected
waveform. For the bounded nine-state target subset, an inactive guard or an
explicit `null` element schedules a resolution-neutral `Z` transaction through
that same driver, using the retained waveform delay; targeted diagnostics
reject guarded assignments outside Boolean-guarded blocks and disconnection
on other target domains. The merged application proves three independent
base/guarded drivers: activation produces `XXX`, selected explicit-null
alternatives produce `X11`, and deactivation restores all three base values
across interpreter and LLVM O0/O2 execution. The final five-test focused
Debug/Release gates pass in 16.88/16.26 seconds, with scoped locals at
0.82/0.83 seconds. The catalog covers 1,469 production diagnostics and all 354
authored sources pass the 2,000-line gate. Batch 115 remains **in progress**
with Task 6 current and Tasks 7 through 10 pending.

Task 6 is focused-complete. Every lowered debug point and runtime execution
point now retains a canonical hierarchy-qualified scope rooted at its process,
with user labels for labeled statements and loops plus deterministic
source-derived identities for unlabeled loops and every case alternative.
The debugger includes those scopes and their ancestors in hierarchy
navigation, and name lookup from a statement scope walks outward through its
lexical parents. The existing VHDL statement application navigates
`worker.iterations.choice` and resolves the architecture-level `observed`
signal from that nested scope. Native-object schema 71 serializes execution
scope, and a scope-only cache test proves a deterministic miss without an
operation or source-coordinate change. The final six-test source/runtime/
elaboration/LLVM/application/scoped-locals gates pass in 19.47/19.01 seconds
for Debug/Release, with scoped locals at 0.82/0.81 seconds. The catalog still
covers 1,469 production diagnostics and all 354 authored sources pass the
2,000-line gate. Batch 115 remains **in progress** with Task 7 current and
Tasks 8 through 10 pending.

Task 7 is focused-complete. Bounded VHDL runtime slices now use a statically
known 1-through-64-bit contextual width and lower both integer-family bounds,
declared-range checks, direction-aware exact-length validation, and the
normalized fixed-width selection through common integer and dynamic-part
SimIR. Ascending and descending reads, process-variable targets, and packed
record-member read/write chains preserve exact four-/nine-state values. Stable
diagnostics reject noninteger bounds, incompatible direction/profile, unknown
assignment width, and a further target selection; execution fails
deterministically when runtime bounds leave the declared range or do not match
the required direction and length. `lower_condition` moved to its own source
partition so `lowerer_assignment.cpp` and the internal header remain within
the hard limit. Focused diagnostic/source/elaboration/LLVM/runtime/scoped-
locals gates pass in 4.09/3.93 seconds for Debug/Release, with scoped locals
at 0.83/0.82 seconds. The catalog covers 1,472 production diagnostics and
all 356 authored sources pass the 2,000-line gate. Batch 115 remains **in
progress** with Task 8 current and Tasks 9 through 10 pending.

Task 8 is focused-complete. Checked VHDL runtime slices now schedule single
and atomic multi-element projected signal waveforms through the existing
dynamic projected operations widened to their statically validated packed
source width. The explicit slice right bound is the normalized runtime offset
anchor in both directions, so selection is captured when the assignment
executes and delayed/update transactions retain exact scalar driver identity,
delta ordering, transport/inertial cancellation, and Logic4/Logic9 state.
Concurrent target-bound expressions contribute dependencies without making
the written signal self-sensitive. Interpreter tests cover delayed single and
multi-waveform values; LLVM O0/O2 tests cover four-bit dynamic projected
callbacks and cache identity. Native-object schema 72 records the widened
operation contract without changing the public runtime ABI. `lower_assert`
is structurally partitioned into its own source. The final eight-test
diagnostic/source/elaboration/LLVM/runtime/projected/Logic9/scoped-locals
gates pass in 4.44/4.24 seconds for Debug/Release, with scoped locals at
0.85/0.81 seconds. All 1,472 diagnostics remain cataloged and all 357 authored
sources pass the hard line limit. Batch 115 remains **in progress** with Task
9 current and Task 10 pending.

Task 9 is focused-complete. The frontend/elaboration fixture covers positive
ascending, descending, packed-record-member, read, local-target, signal-target,
single-waveform, and multi-waveform forms plus malformed syntax, incompatible
direction, noninteger bounds, and runtime length failure. The merged VHDL
array application now carries dynamic slice reads, persistent debug-visible
locals, projected single/multi signal targets, exact Logic9 results, hierarchy
paths, debugger inspection, and normalized VCD through interpreter and LLVM
O0/O2 cold/warm runs. Package-source edits invalidate both specializations and
native objects, while runtime length failures have identical interpreter and
compiled instruction/message behavior at O0/O2. The final seven-test
diagnostic/source/elaboration/LLVM/application/runtime/scoped-locals gates pass
in 4.99/4.67 seconds for Debug/Release, with the array differential at
0.85/0.78 seconds and scoped locals at 0.83/0.80 seconds. All 1,472 diagnostics
and 357 authored sources remain within their gates. Batch 115 remains **in
progress** with Task 10 current.

Task 10 is complete. Feature-matrix row VH-230, the architecture and language
support contracts, and the evidence inventory record fixed-width dynamic VHDL
slice reads, local writes, projected signal waveforms, bounds, sensitivity,
cache, hierarchy, debugger, Logic9, and normalized-VCD behavior. The full gate
also corrected the Tcl debugger regression to expect the process scope added
by Task 6. The LLVM-disabled ASan/UBSan focused seven-test gate passed in 3.91
seconds. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 66 tests in
182.10 seconds, with scoped locals at 0.83 seconds and VHDL arrays at 0.84
seconds; Release passed all 66 in 160.13 seconds, with scoped locals at 0.82
seconds and VHDL arrays at 0.80 seconds. All 1,472 production diagnostics are
cataloged and all 357 authored sources pass the 2,000-line gate. All ten Batch
115 tasks are complete.

### One-hundred-sixteenth feature batch — VHDL multidimensional and composite-array closure — Complete

The current ten implementation tasks are:

1. **Complete.** Audit and retain multidimensional and composite-element array declarations,
   constraints, objects, aggregates, selections, ports, and callable boundaries
   in typed HIR with exact spans and targeted diagnostics.
2. **Complete.** Complete type/subtype layout for multidimensional and composite arrays,
   preserving every index range, direction, null range, element subtype, nominal
   identity, and deterministic flattened storage mapping.
3. **Complete.** Complete contextual array aggregates with positional, named, discrete-range,
   choice-list, and final `others` associations, including nested aggregates,
   coverage, overlap, duplicate, and subtype legality.
4. **Complete.** Lower multidimensional indexing, slicing, and supported chained selections for
   reads and assignment targets with checked ordinal mapping, bounds, direction,
   and shape compatibility.
5. **Complete.** Execute null arrays and slices through object initialization, aggregates,
   assignments, loops, copies, equality, debugger inspection, and trace behavior
   without allocating or updating phantom elements.
6. **Complete.** Complete same-language entity/component port and generic boundaries for
   multidimensional and composite arrays with exact constraint adaptation,
   aliases, copy direction, driver ownership, and specialization identity.
7. **Complete.** Complete function/procedure parameter, result, local, package, and generated
   callable boundaries for supported array shapes with deterministic copy-in,
   copy-out, return, lifetime, and provenance behavior.
8. **Complete.** Complete sensitivity inference, partial/composite signal scheduling, driver
   resolution, delta/update ordering, and interpreter/LLVM parity for array
   element and slice targets.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cache-edit, hierarchy, debugger, normalized-VCD, null-range, port,
   and callable differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 116.

Batch status is **complete**. This exact completed ten-task list is retained in
both the official plan and resume handoff now that work has moved to Batch 117.

Task 1 is focused-complete. Typed HIR now retains every source-ordered
integer-family array dimension, mixed constrained/unconstrained ranges, the
complete direct or named composite element type, and multidimensional subtype
constraints on objects and ports. Nested aggregates, multi-index reads,
subarray-slice operands, and array-typed function/procedure boundaries retain
their exact expression and source-span structure. Existing one-dimensional
execution reads a compatibility mirror of the first dimension. At this
checkpoint `FSIM-ELAB-VHARRAY-008` staged multidimensional types behind the
then-pending Task 2 layout. The richer recursive Type layout exposed and
repaired an O3 GCC
optional-profile constructor false positive without a warning suppression.
Final frontend/catalog/source/elaboration/type-generic/component/array/scoped-
locals gates pass in 3.18/3.05 seconds for Debug/Release, with scoped locals
at 0.86/0.83 seconds. All 1,473 diagnostics are cataloged and all 357 authored
sources remain within the 2,000-line gate.

Task 2 is focused-complete. Concrete array layout now preserves every evaluated
dimension range, direction, and null state, records rightmost-fastest packed-bit
strides, and computes a checked total width without erasing nominal array or
element identity. Scalar, vector, record, nested-concrete, constrained-open,
and interface-type-generic element paths share the layout engine; one-dimensional
arrays keep their declared packed-range compatibility mirror. Rank mismatch,
illegal reconstraint, index-base, indefinite-element, and overflow failures
remain deterministic. The layout engine is structurally partitioned in
`elaboration_vhdl_array_layout.cpp`; all 358 authored sources remain within the
2,000-line gate, with `elaborator_internal.hpp` at exactly 2,000 lines. Focused
Debug and Release seven-test gates passed in 2.29 and 2.08 seconds, with scoped
locals at 0.86/0.83 seconds and VHDL arrays at 0.95/0.80 seconds. The
LLVM-disabled ASan/UBSan gate passed the same seven tests in 4.25 seconds. All
1,473 production diagnostics remain cataloged. Batch 116 remains **in
progress** with Task 3 current and Tasks 4 through 10 pending.

Task 3 is focused-complete. The contextual aggregate lowerer now walks one
source dimension at a time, using its exact range and packed-bit stride to map
positional, discrete, range, choice-list, and final-`others` associations.
Nested multidimensional subaggregates, nested named arrays, vector values, and
nominal record-element aggregates lower recursively with exact width,
two-/four-/nine-state, coverage, overlap, duplicate, bounds, and record-subtype
checks. Application evidence covers concurrent assignments, conditional
alternatives, process-local initialization, interpreter execution, LLVM O0/O2,
cold/warm cache reuse, and package-edit invalidation. Focused Debug and Release
seven-test gates passed in 2.20 and 2.21 seconds, with VHDL arrays at 0.94/0.93
seconds and scoped locals at 0.80/0.83 seconds. The LLVM-disabled ASan/UBSan
gate passed the same seven tests in 4.33 seconds. All 1,473 production
diagnostics are cataloged and all 358 authored sources remain within the
2,000-line gate. Batch 116 remains **in progress** with Task 4 current and Tasks
5 through 10 pending.

Task 4 is focused-complete. Multidimensional calls and comma-separated targets
normalize into source-ordered selection chains. Static and runtime reads and
local/signal targets use the concrete rightmost-fastest dimension strides;
runtime indices are signed-32-bit checked, converted to right-relative packed
ordinals, and combined before fixed-width extract/insert operations. Static and
runtime partial-dimensional slices retain exact direction and contextual shape,
while nested named arrays, vector elements/slices, and nominal record elements
remain typed through supported chains. Concurrent sensitivity discovery now
retains multi-index array prefixes. Positive frontend/application evidence
covers scalar, vector, nested-array, and record elements, local and signal
targets, interpreter execution, LLVM O0/O2, cold/warm cache reuse, and package
edits; negative evidence covers type, bounds, direction, shape, and identical
interpreter/compiled runtime range failures. Focused Debug and Release
seven-test gates passed in 2.46 and 2.30 seconds, with VHDL arrays at 1.18/1.14
seconds and scoped locals at 0.90/0.81 seconds. The LLVM-disabled ASan/UBSan
five-test gate passed in 4.51 seconds after exposing and repairing a selected-
type lifetime defect. All 1,477 production diagnostics are cataloged and all
359 authored sources remain within the 2,000-line gate. Batch 116 remains **in
progress** with Task 5 current and Tasks 6 through 10 pending.

Task 5 is focused-complete. Concrete null VHDL array signals and
locals now retain their declared ranges and nominal types while using true
zero-bit packed values. Null initialization, contextual `others` aggregates,
whole-object and null-slice assignments, copies, equality/inequality,
zero-iteration `'range` loops, and `'length = 0` lower without emitting phantom
loads, writes, or scheduled updates. Interpreter and LLVM debugger reads render
the values as `<null>`; application tracing omits zero-width declarations because
VCD has no legal zero-width net. The merged VHDL array application covers
interpreter and LLVM O0/O2 execution, cold/warm cache reuse, package-edit
invalidation, debugger inspection, and application VCD behavior. The focused
eight-test Debug and Release gates each passed in 3.04/2.96 seconds, with VHDL
arrays at 1.40/1.40 seconds and scoped locals at 0.87/0.93 seconds. The
LLVM-disabled ASan/UBSan six-test gate passed outside the ptrace sandbox in
2.37 seconds. All 1,477 production diagnostics are cataloged and all 359
authored sources remain within the 2,000-line gate. Batch 116 remains **in
progress** with Task 6 current and Tasks 7 through 10 pending.

Task 6 is focused-complete. Same-language VHDL entity and component boundaries
now adapt unconstrained multidimensional and composite-array formals to the
actual signal's exact ranges, directions, null state, flattened strides, and
element profile before specialization. Port aliases retain the parent signal
ID and existing driver ownership, while the adapted shape participates in a
dedicated specialization identity. Component compatibility and the
`vhdl-component-binding-v7` identity now profile every array dimension,
generic-dependent constraint, flattened width, nested element type, and record
member; equal-width arrays with different shapes can no longer collide or bind
silently. The merged VHDL array application covers direct-entity,
component-bound, and generic-constrained multidimensional/composite ports,
input/output copies, signal aliases, equal-width/different-shape cache keys,
interpreter and LLVM O0/O2 execution, cold/warm reuse, and package-edit
invalidation. Negative evidence reports `FSIM-ELAB-BIND-031` for an incompatible
direct boundary and `FSIM-ELAB-VHCOMP-007` for an incompatible component/entity
profile. Focused eight-test Debug and Release gates passed in 5.57 and 5.53
seconds, with VHDL arrays at 1.51/1.46 seconds and scoped locals at 0.86/0.83
seconds. The LLVM-disabled ASan/UBSan seven-test gate passed outside the ptrace
sandbox in 4.71 seconds. All 1,477 production diagnostics are cataloged and all
361 authored sources remain within the 2,000-line gate. Batch 116 remains **in
progress** with Task 7 current and Tasks 8 through 10 pending.

Task 7 is focused-complete for concrete supported array shapes. Callable
overload matching now requires same-nominal VHDL arrays to have compatible
rank, element subtype, bounds, directions, null state, and flattened strides;
same-width but differently constrained multidimensional actuals no longer pass
profile selection. The merged VHDL array application executes constrained
multidimensional and record-element arrays through package function parameters,
locals, and results; package procedure constant/inout parameters and
deterministic copy-in/copy-out; nested local function calls; and a function
declared inside a selected generate body. Interpreter and LLVM O0/O2 runs,
cold/warm cache reuse, package-edit invalidation, and signal-value comparisons
cover return lifetime and source provenance. Negative evidence reports
`FSIM-ELAB-VHOVER-002` for an incompatible callable shape. Focused eight-test
Debug and Release gates passed in 5.88 and 5.75 seconds, with VHDL arrays at
1.88/1.76 seconds and scoped locals at 0.83/0.81 seconds. The LLVM-disabled
ASan/UBSan seven-test gate passed outside the ptrace sandbox in 5.79 seconds.
All 1,477 production diagnostics are cataloged and all 361 authored sources
remain within the 2,000-line gate. Batch 116 remains **in progress** with Task
8 current and Tasks 9 through 10 pending.

Task 8 is focused-complete. Lowered processes now retain explicit static packed
driver regions; whole and runtime-selected targets remain conservatively marked
as whole-object drivers. VHDL unresolved multidimensional/composite arrays may
therefore use disjoint process-owned elements or slices while overlapping
regions still report `FSIM-ELAB-DRV-001`; the existing SystemVerilog variable
single-process rule is unchanged. Resolved `std_logic` driver slots initialize
owned static regions to the subtype default and unrelated regions to neutral
`Z`, so disjoint partial drivers no longer inject phantom `U` values. Runtime
registration validates every retained region before execution. The merged VHDL
array application covers disjoint multidimensional rows, record-element array
updates from separate processes, resolved partial drivers, common update-phase
coalescing, chained-selection sensitivity, interpreter and LLVM O0/O2 parity,
cold/warm cache reuse, and package-edit invalidation; an overlapping-slice
design supplies negative elaboration evidence. Focused eight-test Debug and
Release gates passed in 6.18 and 5.93 seconds, with VHDL arrays at 2.02/1.89
seconds and scoped locals at 0.83/0.82 seconds. The LLVM-disabled ASan/UBSan
seven-test gate passed outside the ptrace sandbox in 5.99 seconds. All 1,477
production diagnostics are cataloged and all 362 authored sources remain
within the 2,000-line gate. Batch 116 remains **in progress** with Task 9
current and Task 10 pending.

Task 9 is focused-complete. The merged VHDL array application now places
hierarchy-port copies, package-callable results, disjoint resolved and
unresolved partial drivers, composite array elements, and chained-selection
sensitivity values in the same normalized custom-VCD stream used for exact
interpreter/LLVM and cold/warm comparisons. Debugger evidence covers the same
hierarchy, callable, multidimensional resolved, and composite objects, while
the application VCD proves their declarations and continues to omit the legal
zero-width null array. Existing positive parsing and elaboration are paired
with aggregate/type/bounds/direction/shape, dynamic range, hierarchy-port,
component-profile, callable-profile, and overlapping-driver negative cases;
runtime failures are compared exactly between interpreter and compiled modes.
Both O0/O2 loops retain package-edit cache invalidation and distinct equal-
width hierarchy specialization keys. Focused eight-test Debug and Release
gates passed in 6.11 and 5.84 seconds, with VHDL arrays at 2.04/1.92 seconds and
scoped locals at 0.84/0.81 seconds. The LLVM-disabled ASan/UBSan seven-test gate
passed outside the ptrace sandbox in 5.10 seconds. All 1,477 production
diagnostics are cataloged and all 362 authored sources remain within the
2,000-line gate. Batch 116 remains **in progress** with Task 10 current.

Task 10 is complete. Feature-matrix rows VH-231 through VH-238 and the compact
language-support contract now record bounded multidimensional/composite array
HIR, layout, aggregates, selections, null values, hierarchy and callable
boundaries, partial drivers, sensitivity, and differential evidence. The full
gate found and repaired missing static-expression traversal of retained array
dimension constraints, then updated legacy resolved partial-driver expectations
to the neutral `Z` values required outside each owned region. The
LLVM-disabled ASan/UBSan eleven-test frontend/catalog/source/elaboration,
expression, scoped-local, overload, component, array, projected-waveform, and
runtime gate passed outside the ptrace sandbox in 10.87 seconds. Exact LLVM
22.1.8 warnings-as-errors Debug passed all 66 tests in 182.13 seconds, with
VHDL arrays at 1.97 seconds and scoped locals at 0.84 seconds; Release passed
all 66 in 160.21 seconds, with arrays at 1.91 seconds and scoped locals at 0.84
seconds. All 1,477 production diagnostics are cataloged and all 362 authored
sources pass the 2,000-line gate. All ten Batch 116 tasks are complete.

### One-hundred-seventeenth feature batch — VHDL nested composites and expression closure — Complete

The current ten implementation tasks are:

1. **Complete.** Audit and retain nested record/enumeration declarations, qualified
   expressions, aggregate choice forms, composite attributes, and composite
   operations in typed HIR with exact spans and targeted diagnostics.
2. **Complete.** Complete recursive bounded record layout and legality for nested record,
   array, enumeration, vector, and scalar members with nominal identity,
   defaults, constraints, and deterministic flattened storage.
3. **Complete.** Complete enumeration visibility and overload candidate behavior inside nested
   composites, aggregates, selections, comparisons, choices, conversions, and
   hierarchy/callable profiles.
4. **Complete.** Lower VHDL qualified expressions and supported subtype conversions with exact
   contextual type, constraint, state-domain, bounds, and nominal checks.
5. **Complete.** Complete record and array aggregate element-choice, range, choice-list,
   qualified, nested, and final `others` forms with exact order, coverage,
   overlap, duplicate, and subtype legality.
6. **Complete.** Complete scalar and composite type/object attributes across nested records,
   arrays, and enumerations, including static folding, executable results,
   dimensions, bounds, ranges, positions, and checked failures.
7. **Complete.** Complete supported composite equality, inequality, matching, concatenation,
   selection, assignment, conditional/case choice, and conversion operations
   with interpreter/LLVM parity.
8. **Complete.** Complete nested-composite hierarchy ports, generic and callable boundaries,
   aliases/copies, driver ownership, sensitivity, scheduling, debugger/VCD,
   provenance, and specialization/cache identity.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cold/warm/edit, hierarchy, callable, debugger, normalized-VCD,
   null/constraint, and exact-failure differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 117.

Batch status is **in progress**. Keep this exact ten-task list current in both
the official plan and resume handoff. Change it to complete only after all ten
tasks and their gates close and work moves to Batch 118.

Task 1 is focused-complete. VHDL record declarations now retain named record,
array, enumeration, and other composite member subtype indications recursively
in `PackedMember::nested_types` with exact member and type-name spans instead
of discarding them as unsupported syntax. The frontend fixture combines
record-of-record, record-of-array, record-of-enumeration, and array-of-record
declarations with qualified nested record/array aggregates, discrete and
`others` choices, composite equality, and a composite type attribute. Integer
record members retain targeted `FSIM-VHDL-UNSUPPORTED-026` rejection while
their later bounded type families remain outside this task. Focused nine-test
Debug and Release gates passed in 3.61 and 3.35 seconds, with VHDL arrays at
2.04/1.86 seconds and scoped locals at 0.86/0.81 seconds. The LLVM-disabled
ASan/UBSan nine-test gate passed outside the ptrace sandbox in 5.93 seconds.
All 1,477 production diagnostics are cataloged and all 362 authored sources
remain within the 2,000-line gate. Batch 117 remains **in progress** with Task
2 current.

Task 2 is focused-complete. Named record members now resolve recursively before
layout, propagate the strongest nested state domain, preserve nominal subtype
identity, and defer composite array-element width validation until the checked
layout pass has materialized its record element. Recursive defaults now descend
through records and arrays, retaining `U` for nine-state vector leaves and the
left/default ordinal for enumeration and two-state leaves. The focused
elaboration fixture proves a constrained subtype of an unconstrained
array-of-record, 6/12/20-bit record/array/envelope widths, exact nested member
offsets and nominal identities, and `UUUU00`-family default values. Indefinite,
unknown, and cyclic member types receive `FSIM-ELAB-VHRECORD-001`,
`FSIM-ELAB-VHTYPE-001`, and `FSIM-ELAB-VHTYPE-002`; recursive width overflow
is cataloged as `FSIM-ELAB-VHRECORD-002`. The default builder moved to
`elaboration_defaults.cpp`, restoring the hard source-size gate. Focused
ten-test Debug and Release gates passed in 3.68 and 3.50 seconds, with VHDL
arrays at 2.01/1.89 seconds and scoped locals at 0.83/0.83 seconds. The
LLVM-disabled ASan/UBSan ten-test gate passed outside the ptrace sandbox in
6.45 seconds. All 1,479 production diagnostics are cataloged and all 364
authored sources remain within the 2,000-line gate. Batch 117 remains **in
progress** with Task 3 current.

Task 3 is focused-complete. VHDL object-type lookup now descends retained
record members for nested enumeration selections, and component profile
matching uses the same declaration-aware traversal. Selected enum reads,
writes, comparisons, case choices, nested aggregates, and callable actuals
retain exact nominal type and subtype context. Ordinary calls whose first
actual is enum-typed no longer enter the apostrophe-attribute path. A selected
input component actual materializes a bounded expression driver by aliasing
the parent record signal and retaining its member suffix; whole-signal
connections retain direct aliases. The focused fixture disambiguates function,
procedure, and component overloads whose result/entity context cannot choose
the candidate, executes exact nested aggregate and selected-member values, and
rejects equality between distinct nested enumeration types with
`FSIM-ELAB-VHENUM-002`.

The twelve-test LLVM 22.1.8 Debug and Release gates passed in 9.86 and 9.34
seconds. Scoped locals remained quick at 0.82/0.80 seconds; VHDL arrays passed
in 1.96/1.82 seconds, components in 0.94/0.91 seconds, and overloads in
2.01/1.83 seconds. The LLVM-disabled ASan/UBSan eleven-test gate passed in
8.63 seconds with leak detection disabled because the managed ptrace sandbox
prevents LeakSanitizer initialization; the requested unsandboxed execution was
denied by environment policy. All 1,479 diagnostics remain cataloged and all
364 authored sources pass the 2,000-line gate, with
`lowerer_expression.cpp` exactly at 2,000 lines. `git diff --check` passes.
Batch 117 remains **in progress** with Task 4 current and is not a CI-inspection
boundary.

The 2026-08-02 clean-context checkpoint is based on local and remote commit
`87eff95d800d88c2221f6a309acf1c54c38df86e`. The validated Task 3 work is an
intentional unstaged ten-file checkpoint; its exact path inventory, focused
gate evidence, and recovery instructions are recorded in `docs/v1-resume.md`.
The workspace-write sandbox keeps `.git` read-only, but on 2026-08-02 the user
installed a deterministic outside-sandbox allow rule for `git add`,
`git commit`, and `git push`. The exact recorded ten-path `git add` then
completed without a prompt, restoring the routine checkpoint workflow while
retaining the sandbox for other commands. The subsequent Task 3 commit and
push establish the clean base for Task 4; verify local/remote identity before
editing it. Batch 117 is not a CI-inspection boundary.

The Task 4 audit found that the parser already distinguishes qualified
expressions (`@vhdl-qualified:<canonical-type>`) from ordinary type-conversion
calls, but current lowering shares a target-width resize/domain-copy path that
does not sufficiently distinguish qualification-as-context from legal
conversion. The first Task 4 change is therefore a structural extraction from
the exactly 2,000-line `lowerer_expression.cpp` into
`lowerer_vhdl_conversion.cpp`, with a narrow declaration in
`elaborator_internal.hpp` and CMake registration. The extracted logic will
then enforce separate nominal/base identity, subtype, bounds, array shape and
direction, record, enumeration, width, and state-domain contracts, with
dedicated qualification/conversion diagnostics. A new focused VHDL conversion
test component will cover positive enum, integer-subtype, bounded-array,
nested-record, assignment, call, and return contexts plus negative invisible,
nominally incompatible, wrong-width/shape/direction, indefinite, overflow, and
out-of-subtype cases. No Task 4 implementation edits had begun at this
checkpoint.

Task 4 is focused-complete. Qualification/conversion lowering is structurally
isolated in the 266-line `lowerer_vhdl_conversion.cpp`, restoring
`lowerer_expression.cpp` to 1,925 lines while the narrow helper declaration
leaves `elaborator_internal.hpp` exactly at the 2,000-line gate. Qualified
expressions lower in their exact contextual type without truncating resizes or
state-domain copies. Supported conversions admit the integer family or exact
bounded nominal, array-shape/direction/element-profile, record, enumeration,
width, and state-domain matches, with target subtype checks retained at
runtime. Contextual integer and logic-array literals now use the selected
VHDL execution domain, and integer-family qualification/conversion results are
recognized by assignment type analysis. Seven dedicated qualification and
conversion diagnostics replace the former generic
`FSIM-ELAB-VHOVER-007` route.

The new focused fixture executes enum/subtype, integer/natural/positive,
sibling-array-subtype, logic-vector, nested-record, assignment, call, and
qualified-return cases. Its negative matrix covers invisible, indefinite,
oversized, wrong-width, state-domain, nominal enum/record, array rank, bounds,
direction, element-profile, contextual-result, and runtime integer/enum
subtype failures. The eight-worker LLVM 22.1.8 Debug and Release 12-test gates
passed in 8.00 and 7.72 seconds, with elaboration at 0.17/0.12 seconds, arrays
at 2.14/2.05 seconds, and scoped locals at 0.92/0.87 seconds. The LLVM-disabled
ASan/UBSan 12-test gate passed in 9.86 seconds with leak detection disabled
because the managed ptrace sandbox prevents LeakSanitizer initialization. The
catalog now covers 1,485 production diagnostics and all 366 authored sources
pass the 2,000-line gate. Batch 117 remains **in progress** with Task 5
current; Batch 117 is not a CI-inspection boundary.

Task 5 is focused-complete. Ordered record element-name choice lists now share
the existing positional, named, duplicate/overlap, coverage, and final
`others` machinery. Record and array aggregate elements require their exact
contextual nominal subtype and state domain, and integer/enumeration element
subtype checks execute before packed insertion. Qualified nested record and
array aggregates remain context-determined through recursive lowering.
Bounded built-in `bit_vector` and `std_logic_vector` aggregate targets receive
an exact one-dimensional array profile, allowing the existing discrete,
range, choice-list, coverage, and final-`others` path to handle standard vector
subtypes. New `FSIM-ELAB-VHAGG-009` and
`FSIM-ELAB-VHARRAYAGG-009` diagnostics separate contextual subtype/domain
failures from wrong-width and lossy-state cases.

The dedicated 288-line fixture executes record choice-list/final-`others`,
nested qualified record/array and array-of-record forms, declared and built-in
vector ranges/lists, and exact packed ordering. Negative evidence covers
record and array overlap, outside and missing coverage, unknown/discrete
record choices, wrong nominal/domain elements with exact source spans, and
runtime enumeration-subtype failure. The eight-worker LLVM 22.1.8 Debug and
Release 11-test gates passed in 7.88 and 7.13 seconds, with elaboration at
0.16/0.11 seconds, arrays at 2.27/1.98 seconds, record aggregates at
0.16/0.16 seconds, and scoped locals at 0.87/0.85 seconds. The LLVM-disabled
ASan/UBSan 11-test gate passed in 10.14 seconds with leak detection disabled
because the managed ptrace sandbox prevents LeakSanitizer initialization. The
catalog covers 1,487 production diagnostics and all 367 authored sources pass
the 2,000-line gate; `lowerer_expression.cpp` is 1,993 lines. Batch 117
remains **in progress** with Task 6 current and is not a CI-inspection
boundary.

Task 6 is focused-complete. The dedicated 466-line
`lowerer_vhdl_attributes.cpp` now owns executable scalar/enumeration and array
attribute lowering. Built-in integer, natural, positive, Boolean, and bit type
marks are always visible; integer-family, Boolean, bit, and declared
enumeration bounds, direction, length, position/value, adjacency, and checked
successor/predecessor results retain their exact domains. Scalar
`range`/`reverse_range` loops preserve integer, Boolean, bit, or nominal
enumeration loop-parameter typing. Concrete array attributes select every
locally static dimension from the complete rank, including null ranges, and
nested record-selected signal/local array objects use the same declared
dimension metadata as type and subtype marks. Pre-layout static folding now
evaluates multidimensional constraints instead of guessing from a flattened
first-dimension packed range.

The 249-line elaboration fixture covers scalar bounds/positions, Boolean/bit
results, both dimensions and directions, nested record array objects, null
lengths, architecture-constant folding, scalar/array/enumeration range loops,
invalid ranks, dynamic dimensions, indefinite arrays, prefix legality, scalar
range misuse, and static/runtime checked failures. The new 236-line
application differential proves interpreter and LLVM O0/O2 cold/warm equality
plus dynamic scalar-bound failure parity. The eight-worker seven-test Debug
and Release gates passed in 3.86 and 4.14 seconds, with attributes at
0.16/0.17 seconds, arrays at 1.89/2.14 seconds, enumerations at 0.62/0.63
seconds, and scoped locals at 0.84/0.88 seconds. The LLVM-disabled ASan/UBSan
seven-test gate passed in 6.54 seconds with leak detection disabled under the
managed ptrace sandbox. The catalog covers 1,490 production diagnostics and
all 370 authored sources pass the 2,000-line gate;
`lowerer_expression.cpp` is 1,984 lines. Batch 117 remains **in progress**
with Task 7 current and is not a CI-inspection boundary.

Task 7 is focused-complete. The dedicated 456-line
`lowerer_vhdl_composite_operations.cpp` now owns bounded VHDL record/array
comparison and contextual concatenation plus nominal composite-assignment
validation. Equality and inequality require one record or array base and exact
recursive element profiles; differently constrained arrays of that base
compare by sequence length instead of being resized or rejected. Matching
equality and the newly retained matching inequality stay restricted to bit or
`std_ulogic` scalars and one-dimensional arrays. VHDL `&` no longer reaches
the generic bitwise-AND path: chained scalar/array concatenands are flattened,
checked against the contextual one-dimensional element profile, and retain
two-/nine-state execution domains, including arrays of records and legacy
`bit_vector`/`std_logic_vector` contexts.

Record and array assignments, conditional alternatives, case-selected
values, and supported conversions now preserve nominal base, rank, recursive
element profile, and per-dimension lengths rather than accepting unrelated
same-width packed values. The parser retains a member selected after an array
index as typed HIR; read and assignment lowering carry the array element's
record type and exact member offset through chains such as
`Pair_Value(0).Mode`. Four dedicated `FSIM-ELAB-VHCOMPOP-*` diagnostics cover
comparison, assignment, concatenation, and chained-selection failures while
the established array and overload diagnostics retain their prior contracts.

The 191-line elaboration fixture executes nested record/array equality and
inequality, matching equality/inequality, scalar/array/record-element
concatenation, unequal-length comparison, whole and selected assignment,
conditional/case values, conversion, and chained member reads/writes. Its
negative matrix covers unrelated records and arrays, conditional and length
mismatches, invalid matching domains, concatenation length, and unknown
post-index members with exact diagnostic codes. The 202-line application
differential proves interpreter and LLVM O0/O2 cold/warm equality and cache
reuse. The eight-worker 11-test Debug and Release gates passed in 4.15 and
3.98 seconds, with composite operations at 0.10/0.09 seconds, arrays at
1.92/1.87 seconds, and scoped locals at 0.84/0.80 seconds. The LLVM-disabled
ASan/UBSan 11-test gate passed in 6.93 seconds with leak detection disabled
under the managed ptrace sandbox; composite operations took 0.32 seconds and
scoped locals 0.49 seconds. The catalog covers 1,494 production diagnostics
and all 373 authored sources pass the 2,000-line gate;
`lowerer_expression.cpp` is 1,922 lines. Batch 117 remains **in progress**
with Task 8 current and is not a CI-inspection boundary.

Tasks 8 and 9 are focused-complete in the accumulated Batch 117 working tree.
VHDL read and assignment names now parse arbitrarily interleaved bounded array
indices and record selections rather than stopping after the first post-index
member. The merged array application carries an array of nested records whose
inner record contains a nominal enumeration and vector through direct,
component, generic-dependent, package/local function, and procedure
boundaries. Whole-object aliases/copies, disjoint nested member drivers,
selected sensitivity, scheduling, debugger and normalized-VCD views, and
interpreter/LLVM O0/O2 cold/warm/edit behavior agree. Adapted unconstrained
array identities now use recursive `vhdl-array-shape-v2` serialization, so
nested record, array, enumeration literal/range, nominal, offset, and shape
metadata all participate in specialization/cache identity. The same
differential compares an out-of-range runtime index followed by nested
record/array selections exactly between the interpreter and compiled engines.
One eight-worker Debug development build and the single
`fsim.application.vhdl_arrays` test passed in 2.01 seconds. Per the corrected
batch cadence, no Task 8/9 sanitizer, Release, commit, or push checkpoint was
run; Task 10 owns those accumulated gates. Batch 117 remains **in progress**
with Task 10 current and is not a CI-inspection boundary.

Task 10 is complete. Feature-matrix rows VH-239 through VH-246, the language
support and architecture contracts, the diagnostics wording, and the test
evidence inventory now record recursive nested composite HIR/layout,
enumeration context, qualification/conversion, aggregate choices, attributes,
operations, interleaved selections, boundaries, runtime/debug views, and cache
identity. The single LLVM-disabled ASan/UBSan 13-test frontend, catalog,
source, elaboration, scoped-local, record, enumeration, array, attribute,
composite-operation, and runtime gate passed in 8.16 seconds with leak
detection disabled under the managed ptrace environment. Exact LLVM 22.1.8
warnings-as-errors Debug passed all 68 tests in 176.22 seconds, with arrays at
1.88 seconds and scoped locals at 0.85 seconds. Release passed all 68 tests in
158.10 seconds, with arrays at 1.81 seconds and scoped locals at 0.84 seconds.
The diagnostic catalog covers 1,494 production codes and all 373 authored
sources pass the 2,000-line gate. A final coverage review restored the prior
multidimensional runtime-bound failure beside the new nested-chain failure;
the affected array application then passed Debug, Release, and sanitizer in
2.09, 2.04, and 1.90 seconds. All ten Batch 117 tasks are complete; Batch 117
is not a CI-inspection boundary.

### One-hundred-eighteenth feature batch — VHDL access, protected, and physical types — Complete

The current ten implementation tasks are:

1. **Complete.** Audit and retain bounded access type declarations, allocators, null
   values, dereference selections, protected type declarations/bodies and
   methods, physical type ranges/units/literals, and exact source spans with
   targeted diagnostics.
2. **Complete.** Complete access designated-subtype resolution, recursive legality,
   nullable storage metadata, deterministic object identity, initialization,
   ownership, and bounded lifetime representation.
3. **Complete.** Lower supported allocators, qualified/aggregate initialization,
   dereference reads and writable targets, null checks, assignment, and
   deterministic allocation failures in the interpreter and LLVM.
4. **Complete.** Complete access equality/null operations, aliases/copies, callable
   parameters/results, copy-in/out rules, lifetime escape checks, and explicit
   diagnostics for dangling or unsupported deallocation paths.
5. **Complete.** Complete protected type/body conformance, private member layout,
   method visibility/profiles, shared-variable object construction, and
   encapsulation or purity legality.
6. **Complete.** Execute supported protected methods with deterministic mutual exclusion,
   re-entry policy, process scheduling, suspension restrictions, and
   interpreter/LLVM state parity.
7. **Complete.** Complete bounded physical type ranges, primary/secondary units,
   literal scaling, static folding, comparison/arithmetic/conversion, overflow,
   and exact time-family interoperability where legal.
8. **Complete.** Complete supported hierarchy, generic, callable, alias/copy, debugger,
   VCD, provenance, specialization, and cache behavior for access, protected,
   and physical objects without exposing host pointers in persistent identity.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus interpreter,
   LLVM O0/O2, cold/warm/edit, hierarchy, callable, scheduling, debugger,
   normalized-VCD, lifetime, locking, unit-scaling, and exact-failure
   differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer, source/catalog, full
    Debug/Release, commit, and push gates before closing Batch 118.

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 119 below. Tasks 1
through 9 used the corrected accumulated working-tree cadence; Task 10 owned
the single batch sanitizer, full-regression, commit, and push gate.

Task 1 adds nominal access, protected declaration/body, and physical type HIR;
retains designated subtypes, protected private state and complete method
profiles/bodies, source-ordered physical ranges and unit scales; and gives
`null`, allocator, dereference read/write, and physical-literal syntax distinct
source-spanned expression nodes. The accumulated Debug development build used
eight workers, and `fsim.frontend` passed in 0.02 seconds after a
physical-literal terminator regression was caught and repaired. No sanitizer,
Release, full regression, commit, push, or CI inspection was run at this task
boundary.

Task 2 resolves designated subtypes recursively through local, imported, and
specialized type environments and carries that resolution through generated
qualification, static folding, local nominal scoping, and canonical type
identity. Access values now have a bounded 32-bit opaque-handle contract with
zero reserved for `null`, monotonically assigned object identities capped at
4,096, owning simulation-lifetime semantics, and zero-default packed storage
for signals and persistent locals. Access cycles and protected designated
types receive access-specific elaboration failures. The eight-worker Debug
development build and focused `fsim.elaboration` test passed in 0.14 seconds;
no batch-final gates, commit, push, or CI inspection were run.

Task 3 maps each process-local nominal access type to a hidden bounded queue
whose source-level handles remain one-based packed IDs. Scalar, integer, and
packed-record allocators now support default or qualified/aggregate
initialization; whole designated values and record members can be read, and
whole dereference targets can be written. Zero handles and invalid IDs fail
through checked container indexing, while a pre-mutation capacity assertion
gives exact deterministic exhaustion. The implementation reuses existing
container SimIR, validation, interpreter, native callback, and LLVM lowering
rather than adding a host pointer or ABI surface. The eight-worker Debug build
passed focused `fsim.elaboration` and `fsim.llvm` in 0.14 and 2.50 seconds,
including interpreter execution, null failure, and a two-object exhaustion
fixture. No batch-final gates, commit, push, or CI inspection were run.

Task 4 implements nominal access equality and inequality with `null`,
same-nominal aliases and copies, access-valued function arguments/results, and
variable-class procedure `inout` copy-in/out. Access handles remain valid for
their owning process lifetime; nonnull signal escape, cross-nominal copies,
unsupported operators, and explicit `Deallocate` receive access-specific
diagnostics. A named vector subtype keeps the callable fixture within the
existing bounded callable profile. The eight-worker Debug build passed focused
`fsim.elaboration` and `fsim.llvm` in 0.15 and 2.68 seconds. No batch-final
gates, commit, push, or CI inspection were run.

Task 5 merges protected package declarations with exactly conforming bodies,
resolves bounded private member and method-profile types, and retains stable
source-ordered offsets without exposing private state as ordinary signals.
Each architecture-level `shared variable` of protected type now constructs one
stable protected-object record whose initialized private members use grouped
global container storage; nonprotected shared variables, missing or mismatched
bodies, invalid private storage, and object initializers receive protected-type
diagnostics. The conformance implementation was extracted to
`hierarchy_vhdl_protected.cpp`, leaving `hierarchy_packages.cpp` at 1,999
lines. The eight-worker Debug build passed focused `fsim.frontend` and
`fsim.elaboration` in 0.02 and 0.15 seconds. No batch-final gates, commit,
push, or CI inspection were run.

Task 6 executes supported protected procedures and direct-return functions by
loading their grouped private member snapshots, binding public formals and
private names inside an encapsulated method scope, and committing procedure
updates before the process can yield. Because protected methods are inlined as
wait-free scheduler segments, cooperative process execution supplies
deterministic mutual exclusion without a host mutex or persistent pointer.
Nested calls/re-entry, suspending bodies, unsupported profiles, unavailable
storage, ambiguous methods, and non-direct function bodies receive exact
protected-method diagnostics. A two-process test deterministically advances
the shared counter from 7 to 21; the eight-worker Debug build passed focused
`fsim.elaboration` and `fsim.llvm` in 0.16 and 3.94 seconds. No batch-final
gates, commit, push, or CI inspection were run.

Task 7 resolves bounded physical ranges to the portable signed 32-bit scalar
representation and assigns source-ordered primary-unit scale 1 plus checked,
positive secondary-unit scales expressed in earlier units. Physical literals
now fold in constants and lower contextually with exact unit, range, and
overflow failures; same-nominal addition, subtraction, scaling, division,
comparison, and explicit conversion reuse the checked VHDL integer runtime so
dynamic arithmetic overflow remains deterministic in the interpreter and
LLVM. Cross-nominal assignment or operands and unsupported operators receive
physical-type diagnostics. The positive fixture covers a statically folded
unit-valued constant, micrometer/millimeter scaling, arithmetic, division,
comparison, and conversion; negative fixtures cover unknown units, nominal
mismatch, scale overflow, and runtime arithmetic overflow. The eight-worker
Debug build and focused `fsim.elaboration` test passed in 0.15 seconds. No
batch-final gates, commit, push, or CI inspection were run.

Task 8 carries access and physical nominal metadata into public signal records,
including bounded access ownership and complete resolved physical unit/range
views, while protected shared objects retain a stable object ID and private
member container paths. Canonical type identity now includes access designated
types, physical scales/ranges, and protected layouts/method profiles; generated
qualification, static folding, dependency collection, parameter substitution,
and local nominal scoping recurse through the new type families. Tests cover
stable signal/container hierarchy paths, access and physical debug-local
records, protected member visibility, physical signal aliases and callable
returns, and two independently specialized child instances whose generic
values pass through explicit physical conversions without host pointers in
persistent identity. The eight-worker Debug build passed focused
`fsim.elaboration` in 0.16 seconds. No batch-final gates, commit, push, or CI
inspection were run.

Task 9 adds the merged `fsim.application.vhdl_advanced_types` differential.
One fixture jointly exercises access allocation/null/dereference and debug
locals, protected shared-state construction and serialized method updates, and
physical literals/arithmetic/debug locals. Interpreter, compiled LLVM O0, and
compiled LLVM O2 agree on final signals, private protected storage, completion
time/delta, debugger reads, and normalized VCD; each compiled optimization
proves cold miss/store and warm hit behavior, while an edited physical and
protected increment invalidates analysis plus specialization/native identity
and produces the expected new values. Existing focused parser/elaboration
negative cases retain exact failures for access lifetime/ownership, protected
conformance/suspension/re-entry, and physical units/range/nominal/overflow.
The eight-worker Debug focused gate passed `fsim.frontend`,
`fsim.elaboration`, `fsim.llvm`, and the new application in 3.49 seconds. No
batch-final sanitizer, Release, full regression, commit, push, or CI inspection
was run.

Task 10 is complete. The focused LLVM-disabled ASan/UBSan gate passed all 14
selected frontend, elaboration, application, runtime, catalog, and source tests
in 8.54 seconds with LeakSanitizer disabled because it cannot run under the
managed ptrace environment. The exact LLVM 22.1.8 warnings-as-errors Debug
regression passed all 69 tests in 182.47 seconds, including the advanced-types
differential in 0.59 seconds and scoped locals in 0.82 seconds. Release passed
all 69 tests in 162.84 seconds, including advanced types in 0.54 seconds and
scoped locals in 0.83 seconds. The diagnostic catalog covers 1,571 production
codes, and all 379 authored sources pass the 2,000-line gate. Static VHDL value
and physical-literal helpers now live in `elaboration_vhdl_values.cpp`, leaving
`elaboration_constants.cpp` at 1,714 lines and `elaborator_internal.hpp` at
1,998 lines. All ten Batch 118 tasks are complete; Batch 118 is not a
CI-inspection boundary.

### One-hundred-nineteenth feature batch — VHDL waits, reports, files, time, and transactions — Complete

The current ten implementation tasks are:

1. **Complete.** Audit and retain nested wait forms, assertions and reports,
   file declarations and operations, TextIO profiles, physical time literals,
   and inertial, transport, and reject waveform syntax with exact source spans
   and diagnostics.
2. **Complete.** Complete wait legality and lowering in nested procedures, loops,
   conditionals, and process-local call chains, including sensitivity, timeout,
   condition, resume, and forbidden-context behavior.
3. **Complete.** Complete general assertion and report expressions, severity
   evaluation, message formatting, failure policy, source provenance, and
   interpreter/LLVM parity.
4. **Complete.** Complete bounded VHDL file types, file objects, open modes,
   status, close, lifetime, aliasing, and deterministic manifest-confined I/O.
5. **Complete.** Complete the supported `std.textio` line, read, write, endfile,
   and formatting profiles with exact cursor, whitespace, conversion, and
   failure behavior.
6. **Complete.** Complete physical `time` units, literals, conversions,
   resolution limits, static folding, arithmetic, comparison, timeout, and
   scheduling interoperability.
7. **Complete.** Complete inertial and transport waveform scheduling, reject
   limits, pulse cancellation, transaction ordering, delta behavior, and
   multi-driver resolution.
8. **Complete.** Complete supported hierarchy, callable, debugger, VCD,
   provenance, specialization, and cache behavior across waits, reports,
   files/TextIO, time, and transaction modes.
9. **Complete.** Prove positive/negative parser and elaboration coverage plus
   interpreter, LLVM O0/O2, cold/warm/edit, scheduling, debugger,
   normalized-VCD, I/O, time-resolution, and exact-failure differentials.
10. **Complete.** Update matrix/diagnostics/docs and pass sanitizer,
    source/catalog, full Debug/Release, commit, and push gates before closing
    Batch 119.

Batch status is **complete**. All ten tasks and their gates are closed, and the
current in-progress ten-task record has moved to Batch 120 below. Tasks 1
through 9 used the corrected accumulated working-tree cadence; Task 10 owned
the single batch sanitizer, full-regression, commit, and push gate. Batch 119
was not a CI-inspection boundary.

Task 1 audits and retains the complete Batch 119 frontend surface. Nominal
`file of` types now preserve their element subtype, and file objects in
package, architecture, process, and block regions preserve source spans,
open-kind expressions, logical-name expressions, and file-interface object
classes. Ordinary `file_open`, `file_close`, `readline`, `writeline`,
`read`/`write`, and `endfile` calls remain exact selected-name/call HIR.
Assertions and reports retain general report and severity expressions while
literal text and predefined severities continue to mirror the legacy compact
metadata. Wait, rejection-limit, and waveform delays retain general physical-
time expressions while integer/unit literals preserve the established compact
form. The stale parser-only nested-wait rejection was removed after confirming
that the complete statement tree was already retained; the independent
process-sensitivity-list legality diagnostic remains. The new positive and
malformed frontend fixture covers nested waits, TextIO-like profiles/calls,
file declarations/open information, physical literals and expressions,
inertial/transport/reject waveforms, exact spans, and targeted diagnostics.
Eight-worker warnings-as-errors Debug builds of the frontend and elaboration
targets succeeded; focused frontend, elaboration, catalog, and source gates
passed in 0.40 seconds. The catalog covers 1,577 production codes, and all 380
authored sources pass the 2,000-line gate. No sanitizer, Release, full
regression, commit, push, or CI inspection was run at this task boundary.

Task 2 completes nested wait execution and legality. VHDL procedures may now
retain supported waits, and the ordinary SimIR `Call` frame remains live while
the process suspends inside a procedure. Lowering records exact overload-
resolved process-to-procedure, procedure-to-procedure, and function-to-
procedure dependencies, then chooses implicit process repetition only after
all reachable callable bodies have been lowered. This admits waits nested
through conditionals, static loops, and multi-level procedure chains without
misclassifying a same-name nonsuspending overload. Sensitized processes and
functions diagnose direct or transitive suspension. A focused elaboration
fixture proves event, condition, absolute timeout, resume, overload, and
forbidden-context behavior in the interpreter; a dedicated application
differential proves the same procedure-chain schedule in the interpreter and
LLVM O0/O2. Eight-worker Debug builds and focused `fsim.elaboration` and
`fsim.application.vhdl_procedure_waits` tests pass. The catalog covers 1,578
production codes, and all 382 authored sources pass
the 2,000-line gate with `elaborator_internal.hpp` exactly at the limit. No
sanitizer, Release, full regression, commit, push, or CI inspection was run at
this task boundary.

Task 3 completes general VHDL assertion and report execution. The frontend
recognizes `string` and `severity_level`, decodes doubled-quote string
literals, and retains runtime string concatenation. Lowering evaluates report
and severity expressions at the statement execution point, but branches over
both for a passing assertion. Static forms keep the compact `Assert`/`Report`
operations; dynamic forms use typed string and two-bit severity registers in
a `StringReport` operation. Interpreter and LLVM paths validate the runtime
severity ordinal, preserve exact source metadata, continue after note through
error, report a standalone failure exactly once before terminating, and avoid
double-reporting a failed assertion. `FSIM-ELAB-VHREPORT-001` and `-002`
diagnose non-string report expressions and non-`severity_level` severities.
The elaboration fixture covers dynamic messages, severities, skipped passing
assertions, and both diagnostics. The merged display application proves exact
messages, severities, source lines, failure policy, and interpreter/LLVM O0/O2
parity. Eight-worker warnings-as-errors Debug builds succeeded; focused
frontend, elaboration, application, catalog, and source gates passed in 0.59
seconds. The catalog covers 1,580 production codes, and all 382 authored
sources pass the 2,000-line gate. No sanitizer, Release, full regression,
commit, push, or CI inspection was run at this task boundary.

Task 4 completes bounded executable VHDL file objects in process, procedure,
and nested block regions. Nominal file objects use opaque 32-bit service
handles rather than host descriptors, remain confined to the manifest root,
and support declaration opens plus status and nonstatus `file_open`, static
`read_mode`/`write_mode`/`append_mode`, `file_close`, lookahead-preserving
`endfile`, and direct signed-integer element `read`/`write`. Status-form opens
return `open_ok`, `status_error`, `name_error`, or `mode_error` without
terminating the process; reopening an open object preserves its handle.
Procedure file formals alias state through copy-in/out, lexical fallthrough
closes block-owned objects, and a common procedure epilogue closes every owned
file even after an early nested return. Direct reads require one complete
conversion and fail deterministically otherwise. `FSIM-ELAB-VHFILE-001`
through `-012` reject invalid declarations, modes, objects, profiles, element
types, and targets. The merged file application proves status, close/alias,
lookahead, input/output bytes, and interpreter/LLVM O0/O2 parity. An
eight-worker warnings-as-errors Debug build succeeded; focused diagnostics,
source, elaboration, file-application, and runtime gates passed in 1.15
seconds. The catalog covers 1,592 production codes, and all 382 authored
sources pass the 2,000-line gate. No sanitizer, Release, full regression,
commit, push, or CI inspection was run at this task boundary.

Task 5 completes the bounded `std.textio` execution profile. Built-in `line`
objects use the existing 4,096-byte string-register plane and `side` retains
the `right`/`left` ordinals. `readline` strips LF or CRLF and fails at true
EOF; `writeline` appends one newline and clears the line. Integer, Boolean,
and bit `read` skip leading whitespace, consume exactly the parsed cursor
prefix, update an optional Boolean `good`, preserve the value on conversion
failure, and otherwise fail deterministically. Integer, Boolean, bit, and
string `write` append to the line with static `left`/`right` justification and
a bounded static field width. All paths reuse `FileReadLine`, `FileScan`,
`StringMethod`, and `FileWriteString`; scan cursor, success, line-clear, and
TextIO mode metadata participate in native schema 73. Nine exact
`FSIM-ELAB-VHTEXTIO-*` diagnostics bound unsupported profiles. The merged
file application proves whitespace/cursor behavior, success and failure,
formatting, line clearing, exact bytes, and interpreter/LLVM O0/O2 parity.
An eight-worker warnings-as-errors Debug build succeeded; focused catalog,
source, elaboration, application, and runtime gates passed in 1.29 seconds.
The catalog covers 1,601 production codes, and all 384 authored sources pass
the 2,000-line gate. No sanitizer, Release, full regression, commit, push, or
CI inspection was run at this task boundary.

Task 6 completes bounded predefined VHDL physical time. `time` is a
nonnegative signed-64-bit tick type, and standard `fs`, `ps`, `ns`, `us`,
`ms`, `sec`, `min`, and `hr` literals are recursively normalized before
elaboration. Exact rational cancellation handles coarse resolutions without
intermediate femtosecond overflow; qualifications, declaration-ordered static
arithmetic/comparison, expression waits, and procedure timeouts therefore
share one integer SimIR representation. Expression-valued units contribute to
automatic resolution selection. `FSIM-ELAB-VHTIME-001` through `-003`
separate nonstatic, final-representation overflow, and inexact-resolution
failures. The focused application proves all units, conversion, arithmetic,
comparison, timeout, automatic resolution, exact failures, scheduling, and
interpreter/LLVM O0/O2 parity. Native schema 74 isolates the changed time
semantics. Eight-worker warnings-as-errors Debug builds succeeded; focused
frontend, catalog, source, elaboration, and application gates passed in 0.46
seconds. The catalog covers 1,604 production codes, and all 384 authored
sources pass the 2,000-line gate. No sanitizer, Release, full regression,
commit, push, or CI inspection was run at this task boundary.

Task 7 completes bounded VHDL projected-output transaction semantics. Whole,
static-slice, and runtime-slice assignments atomically replace each
process-owned scalar driver's ordered future transaction list. Transport
truncates at the first new timestamp; inertial mode applies the LRM marking
algorithm with either the explicit static rejection expression or the first
waveform delay. Cancellation is independent per scalar, same-time updates
coalesce deterministically, zero-delay cascades advance exact delta cycles,
and resolved `std_logic` drivers publish one effective value after every
update phase. The focused projected-waveform application now combines
implicit and explicit pulse rejection, transport preservation, ordered
multi-element whole/slice/conditional/selected waveforms, expression-valued
rejection and delay, a two-driver `0`/`1`/`Z` resolution sequence, a two-delta
zero-time cascade, exact VCD, cold/warm native cache reuse, and
interpreter/LLVM O0/O2 parity. Eight-worker warnings-as-errors Debug builds
succeeded; focused frontend, catalog, source, elaboration, application, and
runtime gates passed in 0.60 seconds. The catalog remains at 1,604 production
codes, and all 384 authored sources pass the 2,000-line gate. No sanitizer,
Release, full regression, commit, push, or CI inspection was run at this task
boundary.

Task 8 closes the supported cross-feature metadata and specialization paths.
A time-generic VHDL child now accepts an exact 64-bit physical-time actual and
suspends through its nested procedure chain while driving parent-visible
ports. The hierarchy differential compares interpreter, compiled O0/O2, and
forced-O0 debug execution points including source, lexical child scope,
process/instruction identity, final values, scheduler changes, and normalized
VCD. Its two specialization modules prove cold stores and warm hits without
changing canonical specialization keys. VHDL report runs now prove cold/warm
cache identity while retaining exact severity/source metadata; VHDL file and
TextIO runs do the same while retaining source-bearing execution points and
deterministic bytes. The projected-waveform differential already covers
VCD, cold/warm cache reuse, time normalization, and resolved transactions.
Eight-worker warnings-as-errors Debug builds succeeded; the focused
frontend, catalog, source, elaboration, report, file/TextIO, wait/time,
projected-waveform, and runtime gates passed in 2.05 seconds. The catalog
remains at 1,604 production codes, and all 384 authored sources pass the
2,000-line gate. No sanitizer, Release, full regression, commit, push, or CI
inspection was run at this task boundary.

Task 9 closes the accumulated Batch 119 differential matrix. The frontend and
elaboration suites retain positive and malformed waits, reports, file/TextIO,
physical-time, and waveform cases with exact diagnostics. Runtime applications
now compare interpreter, LLVM O0/O2, and forced-O0 debug results across nested
callable suspension, source/severity reports, manifest-confined I/O, standard
time units and resolution failures, projected scheduling, delta cycles,
resolved drivers, and normalized VCD. Cold/warm runs cover every native path;
the wait/time hierarchy also edits its VHDL source, changes both canonical
specialization keys, misses and stores both native modules, and proves the new
six-tick schedule. TextIO conversion failures execute in both engines, while
file-input and source edits remain independently distinguished. The focused
nine-test accumulated gate passed in 1.83 seconds with eight-worker builds,
1,604 cataloged production diagnostics, and all 384 authored sources under the
2,000-line limit. No sanitizer, Release, full regression, commit, push, or CI
inspection was run at this task boundary.

Task 10 is complete. Feature-matrix rows `V1-VH-06` and `V1-VH-07` now carry
executable evidence for nested waits, reports, files/TextIO, physical time,
and projected inertial/transport/reject transactions. The LLVM-disabled
ASan/UBSan suite passed all 67 tests in 340.81 seconds outside the ptrace-
restricted sandbox. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 70
tests in 188.12 seconds, and Release passed all 70 tests in 164.46 seconds.
`fsim.application.scoped_locals` remained quick at 0.58 seconds under the
sanitizers and 0.87 seconds in both Debug and Release. The diagnostic catalog
covers 1,604 production codes, and all 384 authored sources pass the
2,000-line gate. The single Batch 119 commit and push close the accumulated
gate; no CI inspection is required at this non-tenth-batch boundary.

### One-hundred-twentieth feature batch — reviewed Apache-2.0 IEEE packages and VHDL v1 audit — Complete

The current ten implementation tasks are:

1. **Complete.** Audit the existing standard-library loader, bundled-source
   inventory, licenses, package dependencies, VHDL revisions, and current IEEE
   package coverage before selecting the reviewed Apache-2.0 source bundle.
2. **Complete.** Review, bundle, analyze, and execute the supported
   `ieee.std_logic_1164` declarations, bodies, tables, conversions, resolution,
   edges, and vector operations with retained license and provenance.
3. **Complete.** Review, bundle, analyze, and execute the supported
   `ieee.numeric_std` and `ieee.numeric_bit` signed, unsigned, conversion,
   resize, comparison, arithmetic, shift, rotate, and boundary profiles.
4. **Complete.** Complete the bundled bit and logic utility package profiles,
   including vector/string conversions, matching values, edge behavior,
   overload visibility, and exact unsupported-profile diagnostics.
5. **Complete.** Review, bundle, analyze, and execute bounded
   `ieee.fixed_generic_pkg` and `ieee.fixed_pkg` types, generics, conversions,
   resize, rounding, overflow, arithmetic, comparison, and slice behavior.
6. **Complete.** Review, bundle, analyze, and execute bounded
   `ieee.float_generic_pkg` and `ieee.float_pkg` types, generics, conversions,
   classification, rounding, arithmetic, comparison, and exceptional values.
7. **Complete.** Complete dependency-ordered implicit/explicit library,
   context, `use`, package-body, overload, generic-package, and type-identity
   integration for every bundled package without host-install dependencies.
8. **Complete.** Complete hierarchy, callable, debugger, VCD, provenance,
   specialization, cold/warm/edit cache, interpreter, and LLVM O0/O2 behavior
   for designs consuming the reviewed packages.
9. **Complete.** Audit every required VHDL v1 feature-matrix row and prove the
   accumulated positive, negative, elaboration, runtime, portability, license,
   and package-conformance differential matrix.
10. **Complete.** Update matrix/diagnostics/docs, pass sanitizer,
    source/catalog, full Debug/Release, commit, and push gates, then inspect and
    repair every non-documentation GitHub CI failure at the mandatory Batch 120
    boundary.

Batch status is **complete**. This exact ten-task list remains the retained
record in both the official plan and resume handoff. Tasks 1 through 9
use the corrected accumulated working-tree cadence; Task 10 owns the single
batch sanitizer, full-regression, commit, push, and mandatory non-documentation
CI inspection gate. Local builds use at least eight workers; GitHub Actions
builds use parallelism four.

Task 1 selects and retains the official IEEE-P1076 `1076-2019` package tag at
commit `16a012320947d378611cc7457f64ed76cb52bac4`. The upstream `ieee` and
`std` VHDL directories, Apache-2.0 license, and authorship file are bundled
byte-for-byte; checked SHA-256 values cover all 28 imported files. A separate
fsim-authored inventory records the only supported dependency order and review
stage for the predefined, TextIO, environment, reflection, logic, numeric,
math, fixed, and floating packages. CMake installs the complete reviewed-source
snapshot, while the application activates only stages with executable evidence
and a corresponding standard-library cache version. An eight-worker exact LLVM
22.1.8 Debug regeneration succeeded;
the new integrity/license test plus the diagnostic-catalog and source-line
gates passed all three tests in 0.23 seconds. The catalog remains at 1,604
production codes, and all 384 authored C/C++ sources remain within the
2,000-line limit. No sanitizer, Release, full regression, commit, push, or CI
inspection was run at this task boundary.

Task 2 activates the checksum-pinned `ieee.std_logic_1164` declaration and
body only for VHDL contexts that explicitly consume that package. The exact
upstream bytes remain separate compiler-supplied checked sources; their paths,
contents, compilation-unit digests, and semantic dependency identity enter the
design and specialization cache keys without changing project-manifest source
counts or ordering. A compact intrinsic projection preserves the existing
nine-state runtime type identity while recording the reviewed upstream
revision and declaration inventory. The focused application proves all nine
input states through NOT/AND/OR/XOR and derived NAND/NOR/XNOR vector tables,
two-driver standard resolution, `rising_edge`/`falling_edge`, bounded
same-domain `std_logic_vector`/`std_ulogic_vector` conversions, projected
transactions, exact locals, VCD, cold/warm native reuse, and interpreter versus
LLVM O0/O2 parity. It also proves the pinned declaration/body SHA-256 values
and rejects a project redeclaration with `FSIM-FE-VHSTD-004`. An eight-worker
Debug build succeeded; frontend, elaboration, runtime, package-integrity,
diagnostic-catalog, source-line, and focused application tests passed all seven
tests in 0.80 seconds, with the application itself at 0.33 seconds. Task 3 is
now current; the batch worktree remains intentionally uncommitted and no
sanitizer, Release, full regression, push, or CI inspection was run.

Task 3 activates the exact reviewed `numeric_std` or `numeric_bit` declaration
and body on explicit use, with `numeric_std` also bringing its pinned
`std_logic_1164` dependency into deterministic analysis and cache order. The
parser distinguishes two-state `numeric_bit` signed/unsigned objects from the
nine-state `numeric_std` profiles. Bounded intrinsic lowering now executes
`to_integer`, `to_signed`, `to_unsigned`, `resize`, `shift_left`,
`shift_right`, `rotate_left`, and `rotate_right`; direct signed/unsigned
conversion preserves bits while changing arithmetic interpretation. The
focused application covers add, subtract, multiply, divide, modulo, absolute
value, comparison, sign extension, truncation/conversion, shift/rotate,
integer conversion, exact package hashes and declaration metadata, O0/O2
interpreter/LLVM parity, and cold/warm native reuse for both packages. Invalid
result sizes and out-of-profile integer-conversion widths produce
`FSIM-ELAB-VHNUM-002` and `FSIM-ELAB-VHNUM-003`. After returning the shared
elaborator header from 2,004 to exactly 2,000 lines, the eight-worker Debug
build and all nine focused frontend, elaboration, runtime, integer-shift,
logic9, numeric, package-integrity, catalog, and source-budget tests passed in
1.44 seconds; the numeric application took 0.52 seconds. Task 4 is current;
the accumulated batch remains uncommitted, and no sanitizer, Release, full
regression, push, or CI inspection was run.

Task 4 completes the bounded bit/logic utilities without adding another
backend operation family. Scalar and vector conversions lower into existing
extract, exact-compare, conditional-select, concatenate, and typed-copy SimIR,
covering `to_bit`, `to_bitvector`, bit-to-logic promotion, `to_01`, `to_x01`,
`to_x01z`, `to_ux01`, and `is_x` across all nine states. Static one- through
64-bit `to_string`, `to_ostring`, and `to_hstring` profiles produce exact
binary/octal/hex strings; dynamic or unknown octal/hex profiles fail with
`FSIM-ELAB-VHLOGIC-003`. The checksum-pinned `std_logic_textio` declaration is
now loaded after `std_logic_1164` and retains its reviewed alias inventory.
The expanded logic application proves scalar/vector overloads, xmap behavior,
matching-known/unknown predicates, edges, exact reports, interpreter/LLVM
O0/O2, VCD, and cold/warm cache behavior. The eight focused frontend,
elaboration, runtime, logic, numeric, package-integrity, catalog, and source
tests passed in 2.17 seconds; the logic application took 1.20 seconds. Task 5
is current, with no batch commit, sanitizer, Release, full regression, push,
or CI inspection yet.

Task 5 activates the checksum-pinned `math_real`, `fixed_float_types`,
`fixed_generic_pkg`, and `fixed_pkg` dependency chain while retaining the
reviewed default fixed-package rounding and overflow profiles. Constrained
`ufixed` and `sfixed` objects carry their descending binary-point ranges over
the common exact nine-state packed representation. Bounded lowering executes
locally static integer `to_ufixed`/`to_sfixed` conversions with saturation,
same-range add/subtract and comparison through the shared signed/unsigned
arithmetic kernels, unsigned fractional `resize` with nearest rounding,
scale-preserving resize, and fixed-point slices. The focused application
proves all ten exact compiler-supplied source dependencies, declaration
inventory/revision, O0/O2 interpreter/LLVM parity, cold/warm native reuse,
positive and negative conversion, rounding, saturation, arithmetic,
comparison, and slicing. Ascending contextual ranges and widths above 64
produce `FSIM-ELAB-VHFIX-004` and `FSIM-ELAB-VHFIX-002`. The eight-worker
Debug builds succeeded and all nine focused frontend, elaboration, runtime,
logic, numeric, fixed, package-integrity, catalog, and source-budget tests
passed in 2.53 seconds; the fixed application took 0.33 seconds. Task 6 is
current; the accumulated batch remains intentionally uncommitted, and no
sanitizer, Release, full regression, push, or CI inspection was run.

Task 6 activates the checksum-pinned `float_generic_pkg` declaration/body and
`float_pkg` instance after their complete 13-source logic, numeric, math,
fixed, and floating dependency chain. The bounded default generic profile is
IEEE-754 binary32: constrained `float(8 downto -23)` values retain their exact
32 nine-state bits, while locally static package calls fold before SimIR into
ordinary typed constants. Integer conversion, default binary32 rounding,
`add`, `subtract`, `multiply`, `divide`, `sqrt`, named comparisons,
`to_integer`, and raw standard-logic-vector conversion are covered alongside
finite, NaN, unordered, and sign classification. Canonical positive/negative
zero, infinity, signaling/quiet NaN constructors preserve exact bits.
Nonstatic operations, non-binary32 ranges, and exceptional integer conversion
produce `FSIM-ELAB-VHFLT-001`, `FSIM-ELAB-VHFLT-002`, and
`FSIM-ELAB-VHFLT-004`. The eight-worker Debug builds succeeded and all ten
focused frontend, elaboration, runtime, logic, numeric, fixed, float,
package-integrity, catalog, and source-budget tests passed in 2.95 seconds;
the float application took 0.26 seconds. Task 7 is current; no sanitizer,
Release, full regression,
commit, push, or CI inspection was run.

Task 7 completes the dependency/visibility integration boundary for all ten
activated package declarations and their six bodies. One reusable project
context imports `std_logic_1164`, `std_logic_textio`, `numeric_bit`,
`numeric_std`, `fixed_pkg`, and `float_pkg`; their implicit math/fixed/generic
dependencies expand into 16 exact compiler-supplied sources before the three
manifest units without changing manifest counts or order. The integration
fixture proves declaration-before-body and complete package dependency order,
context-reference visibility, default generic-package instances, independent
two-state `ieee.numeric_bit.unsigned` and nine-state
`ieee.numeric_std.unsigned` identity, and simultaneous overload dispatch for
logic mapping, numeric conversion/resize, fixed resize, and floating
arithmetic. Fully qualified reviewed intrinsic declarations now count as
package exports in package-reference specialization, retain source closure,
and pass unchanged to their intrinsic lowerers; unknown ordinary exports keep
the existing `FSIM-ELAB-PKG-010` path. After compressing the qualified-export
change from 2,003 to 1,999 lines, the eight-worker Debug build and all 11
focused frontend, elaboration, runtime, per-package, integration,
package-integrity, catalog, and source-budget tests passed in 3.54 seconds;
the integration application took 0.52 seconds. Task 8 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection was run.

Task 8 extends the all-package context fixture through the complete consumer
execution boundary. The top specialization retains the exact numeric-bit,
numeric-standard, fixed, and floating package source closure; stable hierarchy
paths expose every result signal. A waiting VHDL process retains independently
typed two-state numeric, nine-state numeric, fixed, and binary32 debugger
locals with exact values. The interpreter and LLVM O0/O2 runs produce the
same signal values, locals, and normalized VCD, while cold/warm cache telemetry
proves module stores and hits. A comment-only edit to the reusable context
invalidates every affected O2 native module and preserves behavior. The
eight-worker Debug builds and all 11 focused frontend, elaboration, runtime,
per-package, integration, package-integrity, catalog, and source-budget tests
passed in 3.43 seconds; the expanded integration application took 0.69
seconds. Task 9 is current; the batch remains intentionally uncommitted,
with no sanitizer, Release, full regression, push, or CI inspection yet.

Task 9 closes the VHDL release-authority audit. Aggregate rows `V1-VH-01`
through `V1-VH-05` now state the bounded contracts actually completed by
Batches 111–118 and join `V1-VH-06` through `V1-VH-08` at executable status;
every row carries positive, negative, elaboration, and runtime evidence. The
new `fsim.v1-vhdl-matrix` gate requires exactly eight ordered executable rows
with no empty evidence column. After building the previously untouched third
application shard with eight workers, the complete Debug VHDL-labeled suite
passed 28/28 tests in 12.99 seconds across analysis order, contexts, overloads,
all generic kinds, components/configurations, statements, composites,
advanced types, transactions, reviewed packages, interpreter/LLVM, hierarchy,
debugger, VCD, and cache behavior.

Task 10's local release gates pass. The LLVM-disabled ASan/UBSan regression
passed all 73 tests in 340.64 seconds with LeakSanitizer disabled for the
managed ptrace environment; `fsim.application.scoped_locals` took 0.51
seconds. Exact LLVM 22.1.8 warnings-as-errors Debug passed all 76 tests in
186.74 seconds, and Release passed all 76 in 161.14 seconds; scoped locals
took 0.82 seconds in each. The reviewed IEEE integration application took
0.73 seconds in Debug and 0.62 seconds in Release. The diagnostic catalog
covers 1,623 production codes, all 393 authored sources pass the 2,000-line
gate, and the IEEE inventory and exact eight-row VHDL v1 matrix gates pass.
This record is the single accumulated commit/push checkpoint. Initial CI run
`30763877162` exposed an MSVC oversized string literal and a clang-cl deleted
defaulted comparison warning. Repair run `30764329695` exposed one signed/
unsigned comparison plus Windows newline conversion of checksum-pinned IEEE
sources. Repair run `30765075997` then exposed one CRLF-sensitive generated-
source edit locator. Commits `deb27c4`, `15ac189`, and `4ad6153` repair those
failures while preserving exact IEEE bytes and newline-neutral source edits.
Final non-documentation run `30765734570` passed all 12 jobs. Standard MSVC
Debug passed 75/75 tests in 372.25 seconds with scoped locals in 0.27 seconds;
MSVC plus LLVM Debug passed 76/76 in 1,160.90 seconds with scoped locals in
1.77 seconds. Batch 120 and its mandatory CI boundary are complete.

### One-hundred-twenty-first feature batch — mixed-language value-boundary conversions — Complete

The current ten implementation tasks are:

1. **Complete.** Audit existing VHDL/SystemVerilog/SystemC boundary type
   metadata, shared-signal aliases, direction rules, diagnostics, and ML-005/
   ML-006 evidence; define the bounded conversion and failure matrix.
2. **Complete.** Implement equal-count ordinal vector mapping across differing
   ascending/descending VHDL and SystemVerilog packed ranges in both hierarchy
   directions, with stable conversion ownership and source metadata.
3. **Complete.** Implement bounded input/output width adaptation with explicit
   truncation, zero extension, sign extension, and inout/lossy-width rejection
   rules instead of requiring every boundary width to be identical.
4. **Complete.** Implement signed/unsigned integral boundary adaptation after
   each language's width rules, including direction-aware legality and exact
   diagnostics for unsafe aliases.
5. **Complete.** Implement VHDL Boolean to/from one-bit SystemVerilog bit/logic
   conversions with canonical false/true ordinals and checked noncanonical
   incoming values.
6. **Complete.** Complete VHDL integer-family to/from 32-bit signed
   SystemVerilog integral conversion, subtype range checks, and both hierarchy
   directions without conflating integer and packed-vector identity.
7. **Complete.** Complete two-state VHDL bit/bit_vector and SystemVerilog bit
   scalar/vector boundaries, including ordinal range conversion and explicit
   rejection of state-losing reverse flows.
8. **Complete.** Complete four-state SystemVerilog logic and nine-state VHDL
   std_logic/std_ulogic scalar/vector conversion tables, exact legal collapse,
   unknown/high-impedance handling, and lossy-domain diagnostics.
9. **Complete.** Prove the complete conversion matrix through recursive mixed
   hierarchy, interpreter, LLVM O0/O2, cold/warm/edit cache, debugger, VCD,
   provenance, and positive/negative elaboration evidence; close ML-005 and
   ML-006 in the feature matrix.
10. **Complete.** Update matrix/diagnostics/docs, pass sanitizer, source/catalog,
    and full Debug/Release gates, then create and push the single Batch 121
    checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. Keep this exact ten-task
list current in both the official plan and resume handoff. Tasks 1 through 9
use one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 121 does not require a
non-documentation CI inspection.

Task 1 confirms that every ordinary boundary reaches one shared validator and
that `SignalInfo` already retains width, source domain, signedness, packed
range/direction, integer subtype range, nominal identity, aggregate shape, and
declaration metadata. HDL and SystemC connections currently bind the formal
and actual names directly to one scheduler signal ID after validation. The
validator rejects unknown domains, cross-language aggregates/arrays/
enumerations, unequal widths, multi-bit signedness differences, unsafe integer
subtype aliases, state-losing flows into two-state destinations, and unresolved
cross-language inouts. Existing common-domain selection and language-local
reads provide equal-width bidirectional Logic9/four-state collapse, while the
integer fixture proves exact signed 32-bit aliases and range checks. The
remaining ML-005/ML-006 gaps are structural: no boundary conversion object or
process owns provenance/cache identity, cross-language packed bounds and
directions do not produce ordinal remapping, width and signedness adaptation is
rejected rather than executed, and Boolean/integer/state conversions lack one
complete atomic matrix. Exact LLVM Debug elaboration, the mixed application,
and the Logic9 application passed 3/3 focused tests in 17.36 seconds (0.17,
15.98, and 1.22 seconds). Task 2 is current; the Batch 121 worktree is now the
intentional accumulated dirty checkpoint, with no sanitizer, Release, full
regression, commit, push, or CI inspection at this task boundary.

Task 2 makes equal-count ordinal vector boundaries explicit without adding a
redundant runtime copy. Both frontends already normalize the leftmost declared
packed element to the most-significant canonical ordinal, so opposite numeric
bounds and directions can safely share one scheduler signal. The new
`BoundaryConversionInfo` DesignIR record is emitted only after successful
cross-language packed validation and retains the owning port path, canonical
signal ID, direction, formal/actual domains and signedness, both declared
ranges, connection span, formal declaration span, and actual declaration span.
Bidirectional VHDL/SystemVerilog fixtures use explicit per-index reads/writes
to prove `"10XZ"` across `[1:4]` to `7 downto 4`, `[9:6]` to `20 to 23`, and
the reverse hierarchy direction while checking stable metadata and physical
source identities. The exact LLVM Debug elaboration target built with eight
workers; diagnostics catalog, source-line budget, and elaboration passed 3/3
tests in 0.34 seconds. The internal elaborator header remains exactly 2,000
lines. Task 3 is current; no sanitizer, Release, full regression, commit, push,
or CI inspection ran at this task boundary.

Task 3 implements one- through 64-bit width-changing input, output, and buffer
boundaries while retaining targeted rejection of unequal-width inouts. A
width-changing connection owns a separate formal signal and one deterministic
adapter process in the parent specialization. The process reads the source on
initialization and any-change sensitivity, truncates least-significant
ordinals with `Extract`, zero-extends unsigned values or replicates the dynamic
sign ordinal before `Concatenate`, writes in the common update phase, and
retains an exact whole-signal driver region. Its process ID, formal/actual
signal IDs, widths, ranges, domains, signedness, path, and source spans are
retained in `BoundaryConversionInfo`, so ordinary process/cache identity sees
the conversion rather than hiding it in an alias. Five adapters in each
VHDL-parent/SV-child and SV-parent/VHDL-child direction prove input zero/sign
extension and truncation plus output zero extension and truncation; the
observed values are `00001010`, `11111010`, `0110`, `00001010`, and `0110`.
An unequal-width resolved inout remains rejected by `FSIM-ELAB-BIND-020`.
Eight-worker LLVM Debug builds succeeded; diagnostics catalog, source budget,
and elaboration passed 3/3 in 0.35 seconds. Authored files remain within 2,000
lines. Task 4 is current; no sanitizer, Release, full regression, commit, push,
or CI inspection ran at this task boundary.

Task 4 permits explicit signed/unsigned adaptation for one- through 64-bit
cross-language input, output, and buffer ports while retaining the same-
language and inout safety checks. Equal-width signedness changes receive a
bit-preserving `CopyRegister` adapter rather than an unsafe shared type alias.
When width also changes, truncation remains ordinal and widening follows the
source side's signedness: unsigned sources zero-extend and signed sources
replicate their dynamic most-significant ordinal before the destination type
view is applied. DesignIR distinguishes `signedness_adapter` from
`width_signedness_adapter`, with both signal identities and the adapter process
retained. Each VHDL-parent/SV-child and SV-parent/VHDL-child fixture proves two
signedness-only and two combined adapters: unsigned `1010` widens into a signed
destination as `00001010`, a signed `1010` source widens into an unsigned
destination as `11111010`, and equal-width conversions preserve `1010`.
Resolved inout width and signedness mismatches remain targeted
`FSIM-ELAB-BIND-020`/`FSIM-ELAB-BIND-021` failures. The exact eight-worker LLVM
Debug build succeeded; diagnostics catalog, source-line budget, and elaboration
passed 3/3 in 0.34 seconds, with the largest touched test at 1,730 lines. Task
5 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 5 gives every one-bit VHDL Boolean/SystemVerilog `bit` or `logic`
boundary its own `boolean_adapter` process and formal signal instead of
conflating the nominal Boolean with a packed alias. Boolean-to-SystemVerilog
copies preserve the canonical false/true ordinals. SystemVerilog-to-Boolean
adapters widen the incoming scalar to the existing checked 32-bit integer
representation and require the exact range zero through one before committing
the original bit; four-state `X`/`Z` therefore raises the existing unknown or
high-impedance runtime failure. A Logic4-to-Boolean checker arms its sensitivity
before the first read so an undriven time-zero `logic` default is not mistaken
for a driven noncanonical value. VHDL-parent/SV-child and SV-parent/VHDL-child
fixtures each prove Boolean-to/from both `logic` and `bit`; a post-start `X`
transition proves checked rejection. Scalar conversion metadata retains both
domains, signals, process identity, direction, and source spans with absent
packed ranges. The conversion tests now have a separate 235-line translation
unit, preserving the existing 1,730-line mixed test. The exact eight-worker
LLVM Debug build succeeded; diagnostics catalog, source-line budget, and
elaboration passed 3/3 in 0.36 seconds. Task 6 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 6 gives each exact 32-bit signed VHDL integer-family/SystemVerilog `bit`
or `logic` boundary a distinct `integer_adapter` process and formal signal, so
integer nominal identity is no longer conflated with packed-vector identity.
The destination VHDL subtype's bounds are retained in DesignIR and enforced by
`IntegerCheck` before the original 32-bit value is committed; four-state
unknown or high-impedance operands fail the same checked path. Logic4 sources
arm sensitivity before their first read to avoid inspecting an undriven
time-zero default. Both VHDL-parent/SV-child and SV-parent/VHDL-child fixtures
prove signed `bit` and `logic` flow in both directions, distinct signal
ownership, negative values, destination range metadata, a deliberate
out-of-range transition, and an all-`X` transition. Unsigned SystemVerilog
profiles are rejected by `FSIM-ELAB-BIND-021` and the generalized range-safe
conversion diagnostic `FSIM-ELAB-BIND-051`; same-language integer subtype
aliases retain their direction-aware containment checks. The exact
eight-worker LLVM Debug build succeeded; diagnostics catalog, source-line
budget, and elaboration passed 3/3 in 0.38 seconds. The new conversion test is
458 lines and the largest touched test remains 1,730 lines. Task 7 is current;
no sanitizer, Release, full regression, commit, push, or CI inspection ran at
this task boundary.

Task 7 completes equal-width VHDL `bit`/`bit_vector` and SystemVerilog `bit`
boundaries as canonical two-state ordinal aliases. Vector ports retain both
declared ranges and directions, while scalar ports now also emit an explicit
`ordinal_alias` DesignIR record with absent packed ranges; both forms retain
one scheduler signal and require no adapter process. VHDL-parent/SV-child and
SV-parent/VHDL-child fixtures each prove vector transfer across opposing and
differently numbered ranges plus scalar transfer, exact Bit2 domains, alias
identity, and `1010`/`0101`/`1` runtime values. Separate negative fixtures
prove both output-directed and input-directed Logic4-to-Bit2 state loss remains
an elaboration-time `FSIM-ELAB-BIND-022` failure. The exact eight-worker LLVM
Debug build succeeded; diagnostics catalog, source-line budget, and
elaboration passed 3/3 in 0.36 seconds. The accumulated conversion test is 729
lines and `hierarchy_types.cpp` is 1,627 lines. Task 8 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 8 makes equal-width Logic4/Logic9 coercion explicit in DesignIR as a
`state_domain_alias` while retaining the canonical shared scheduler signal.
The owning signal's value kind and each language-local register kind already
perform the conversion at every read and write, so no redundant process is
needed; width/signedness adapters retain their structural kind and now carry a
separate `state_domain_changed` flag. The locked collapse table is
`U/X/W/- -> X`, `0/L -> 0`, `1/H -> 1`, and `Z -> Z`; reverse expansion is
`0/1/X/Z -> 0/1/X/Z`. VHDL-parent/SV-child evidence drives every Logic9 value
through `std_logic_vector`, `std_ulogic_vector`, `std_ulogic`, and `std_logic`
ports and observes `XX01ZX01X` plus `1`. SV-parent/VHDL-child evidence expands
and returns `01XZ` plus scalar `Z`. Both hierarchy directions retain exact
domains, alias identity, source metadata, and optional scalar/vector ranges.
Logic9-to-SystemVerilog-`bit` state loss remains a targeted
`FSIM-ELAB-BIND-022` elaboration failure, complementing Task 7's Logic4-to-Bit2
input/output failures. The exact eight-worker LLVM Debug build succeeded;
diagnostics catalog, source-line budget, and elaboration passed 3/3 in 0.36
seconds. The accumulated conversion test is 965 lines and
`hierarchy_types.cpp` is 1,638 lines. Task 9 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection ran at this task boundary.

Task 9 adds `fsim.application.mixed_conversions`, a selector-hosted recursive
SV-to-VHDL-to-SV application covering width, combined signedness/width,
Boolean, integer, Bit2, and Logic4/Logic9 boundaries in one hierarchy. Each
build retains exactly 28 source-spanned conversion records and 11 owned adapter
processes; an empty specialized declaration span discovered by the provenance
assertions now falls back to the exact connection-identifier span. Interpreter,
LLVM O0, and LLVM O2 agree on `00001010`, `00001010`, `11111010`, `1`, signed
`-1`, `0110`, and `01XZ`, as well as a VHDL `boundary_probe` debugger local and
normalized VCD. Both optimization levels prove cold misses/stores, exact warm
hits, stable specialization keys, then a leaf-only source edit changes the
first result to `00001011`, changes cache identity, incurs native misses, and
again matches the interpreter. The application completes in 0.75 seconds.
ML-005 and ML-006 are now `execute` rows with positive, negative, elaboration,
and runtime evidence; the new `fsim.v1-mixed-conversion-matrix` gate requires
both ordered rows and forbids empty evidence columns. Diagnostics catalog,
source-line budget, the matrix gate, elaboration, and the recursive application
passed 5/5 in 1.12 seconds. The application test is 445 lines, the accumulated
conversion elaboration test is 965 lines, and `hierarchy_types.cpp` is 1,641
lines. Task 10 is current; no sanitizer, Release, full regression, commit, push,
or CI inspection ran at this task boundary.

Task 10 closes the batch on the final lifetime-safe implementation. The first
sanitizer pass exposed a heap use-after-free in `connect_ports`: appending an
owned adapter signal could reallocate `signal_info_` while the conversion path
retained a reference to the actual signal metadata. Copying that small metadata
record across adapter construction removes the invalid vector reference; the
focused sanitizer elaboration test then passed in 2.34 seconds. Final exact
eight-worker builds succeeded, LLVM Debug passed 78/78 tests in 181.04 seconds,
LLVM Release passed 78/78 in 161.04 seconds, and ASan/UBSan with leak detection
disabled for the managed ptrace environment passed 75/75 in 331.67 seconds.
Scoped locals remained quick at 0.81, 0.83, and 0.50 seconds respectively, and
the recursive mixed-conversion application passed in 0.76, 0.70, and 1.08
seconds. The full suites include the diagnostics catalog, source-line budget,
VHDL inventory, and both v1 matrix gates. Batch 121 is the single accumulated
commit/push checkpoint and, because it is not a tenth-batch boundary, requires
no GitHub Actions inspection.

### One-hundred-twenty-second feature batch — mixed-language construction, drivers, and phase semantics — Complete

The current ten implementation tasks are:

1. **Complete.** Audit VHDL/SystemVerilog/SystemC construction actuals,
   cross-language driver ownership and resolution, delay propagation,
   scheduler phases, diagnostics, and ML-007/ML-008/ML-010 evidence; define the
   bounded positive and failure matrix.
2. **Complete.** Complete SystemVerilog parameter overrides transferred into
   VHDL value generics, including named/ordered association, type conversion,
   defaults, dependent port shapes, and specialization identity.
3. **Complete.** Complete VHDL generic maps transferred into SystemVerilog value
   parameters, including case rules, explicit/named values, defaults, width and
   signedness semantics, dependent generates, and specialization identity.
4. **Complete.** Complete supported Boolean, integer, packed logic, string, and
   SystemC construction-actual transfer in every hierarchy direction with
   canonical typed provenance and cold/warm/edit cache behavior.
5. **Complete.** Make cross-language input/output/buffer/inout driver ownership
   explicit through recursive aliases and adapters, admitting one logical
   forwarded writer while rejecting sibling, overlapping, and read-only writes.
6. **Complete.** Complete mixed VHDL resolved-signal and SystemVerilog wired-net
   multiple-driver behavior, including Logic9/Logic4 collapse, high impedance,
   update fanout, resolver selection, and deterministic conflict diagnostics.
7. **Complete.** Preserve zero and positive boundary delays plus VHDL
   inertial/transport/reject and SystemVerilog transition-delay behavior across
   adapters without duplicate, lost, or prematurely visible transactions.
8. **Complete.** Complete the cross-language active, inactive, NBA/update, and
   postponed phase lattice, including recursive feedback, same-slot races,
   stable source order, debugger stops, callbacks, and VCD observation.
9. **Complete.** Prove the combined construction/driver/timing matrix through
   recursive mixed hierarchy, interpreter, LLVM O0/O2, cold/warm/edit cache,
   debugger, VCD, provenance, and exact positive/negative diagnostics; close
   ML-008 and ML-010 in the feature matrix.
10. **Complete.** Update matrix/diagnostics/docs, pass sanitizer, source/catalog,
    and full Debug/Release gates, then create and push the single Batch 122
    checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. Keep this exact ten-task
list current in both the official plan and resume handoff. Tasks 1 through 9
use one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 122 does not require a
non-documentation CI inspection.

Task 1 confirms that the shared specialization path already transfers bounded
scalar integer construction actuals before port-shape checks in both HDL
directions. Existing applications prove SystemVerilog parameters into VHDL
generics and VHDL generic maps into SystemVerilog parameters, including mixed
named/positional language rules, defaults and derived widths, specialization
values, interpreter/O2 behavior, and cold/warm native keys. The append-only
SystemC schema likewise supports signed 64-bit integer, natural, positive,
Boolean, and bit construction values in both hierarchy directions. The
remaining construction gaps are complete target-typed Boolean/integer/packed
logic handling, cross-language string transfer, typed provenance, exact
edit-cache matrices, and unified failures; non-value VHDL interface generics
and SystemVerilog type parameters intentionally remain same-language.

Driver validation currently groups per-process whole/slice regions by final
scheduler signal. Native `std_logic`/`std_logic_vector`, `wire`/`tri`, explicit
resolver selection, and four-state resolution execute, but conversion adapters
own separate formal signals and processes without a retained logical-driver
chain. Recursive converted writers, read-only aliases, resolved adapters, and
cross-language inout ownership therefore lack one proof, while `wand`/`wor`
families still end in `FSIM-ELAB-DRV-002`. The runtime has stable active,
inactive, update, and postponed phases, and zero/positive delayed writes use
the common update scheduler; however ML-008 lacks an atomic recursive boundary
matrix combining delayed VHDL transactions, SystemVerilog NBA/update work,
adapter deltas, feedback, debugger stops, callbacks, and VCD observation.
Exact LLVM Debug elaboration, the main application, resolution, mixed
conversions, and runtime passed 5/5 focused tests in 16.89 seconds (0.17,
15.86, 0.09, 0.76, and 0.01 seconds). Task 2 is current; this begins the
intentional accumulated Batch 122 dirty worktree, with no sanitizer, Release,
full regression, commit, push, or CI inspection at this task boundary.

Task 2 confirms and locks the shared target-specialization path for
SystemVerilog-parent/VHDL-child construction rather than adding a parallel
foreign-parameter mechanism. A dedicated 176-line elaboration matrix proves
case-insensitive named overrides, ordered positional overrides, omitted
defaults, SystemVerilog one-bit values converted to VHDL Boolean, signed values
checked against a VHDL integer subtype, a dependent `Last := Width - 1` generic
and port shape, canonical specialization values/identities, and exact runtime
outputs for three independently specialized children. A noncanonical Boolean
actual is rejected by `FSIM-ELAB-GENERIC-008`. The existing mixed application
continues to prove interpreter/O2 execution, dependent port width, and cold/
warm native specialization keys. Direct packed VHDL value-generic syntax
remains deliberately in Task 4's typed construction slice. The exact
eight-worker LLVM Debug build succeeded; diagnostics catalog, source-line
budget, elaboration, and the main application passed 4/4 tests in 15.95 seconds
(0.08, 0.13, 0.17, and 15.57 seconds). Task 3 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 3 locks the reverse VHDL-parent/SystemVerilog-child path in the same
dedicated construction matrix. Named and positional VHDL generic maps now have
evidence against target-typed SystemVerilog `int`, one-bit `bit`, unsigned
four-bit, and signed eight-bit parameters. The target conversion truncates 18
to four-bit 2, preserves signed -3, applies omitted defaults, derives
`LAST = WIDTH - 1`, selects the matching generate branch, materializes the
dependent output shape, and retains distinct canonical `svconst-v1` identities
for all value and local parameters. Three child specializations execute exact
bit-vector and generated outputs. A VHDL name that case-insensitively matches
both `WIDTH` and `width` on a foreign SystemVerilog target is rejected by
`FSIM-ELAB-PARAM-009`. The combined construction test remains 357 lines. The
exact eight-worker LLVM Debug build succeeded; diagnostics catalog,
source-line budget, elaboration, and the main interpreter/O2 construction
application passed 4/4 tests in 16.01 seconds (0.08, 0.12, 0.17, and 15.64
seconds). Task 4 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 4 completes the bounded typed-construction slice without changing the
public SystemC C ABI. Direct VHDL `bit_vector`, `std_logic_vector`, and
`std_ulogic_vector` value generics up to 64 known bits now pass the frontend's
existing specialization path; wider, composite, or unknown/high-impedance
construction values remain checked failures. A VHDL string literal may now
target a SystemVerilog string parameter and retains its existing
`svstring-v1` byte identity, while VHDL-target Boolean, integer, and packed
values receive cross-language-only `vhdlconst-v1` identities carrying domain,
width, signedness, nominal type, declared range, and value. This avoids
perturbing same-language non-value generic identities. SystemC construction
continues to use its append-only signed-64-bit schema for integer, natural,
positive, Boolean, and bit values; HDL-parent construction in both languages
now retains `systemcconst-v1` type/value identities beside the ABI-neutral
integer values. The 446-line construction test proves packed values, strings,
all five SystemC scalar kinds, subtype failures, dependent shapes, generated
behavior, and runtime results in both HDL directions. The exact eight-worker
LLVM Debug build succeeded; frontend, diagnostics catalog, source-line budget,
elaboration, the main mixed/SystemC construction application, and the string
parameter application passed 6/6 tests in 15.83 seconds (0.02, 0.08, 0.12,
0.17, 15.30, and 0.14 seconds). Task 5 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 5 makes connected scalar/vector input ports read-only inside the child
specialization while leaving boundary adapter processes as the only legal
writers of their owned formal signals. A dedicated mixed-driver elaboration
matrix proves one recursive SystemVerilog-to-VHDL-to-SystemVerilog writer
through two narrowing adapters, exact interpreter propagation from two to four
to eight bits, and one conversion process per boundary. Two sibling VHDL
writers converted into the same SystemVerilog actual are rejected by the
existing logical-driver validation, and a VHDL child assignment through an
input port is rejected by `FSIM-ELAB-SVIFACE-006`; its catalog text now covers
both input ports and modport input members. The exact eight-worker LLVM Debug
build succeeded; diagnostics catalog, source-line budget, and elaboration
passed 3/3 tests in 0.37 seconds (0.08, 0.12, and 0.17 seconds). Task 6 is
current; no sanitizer, Release, full regression, commit, push, or CI inspection
ran at this task boundary.

Task 6 adds native `wand`/`triand` and `wor`/`trior` resolver kinds to SimIR
and admits the complete SystemVerilog net-type family at declaration parsing.
Wired resolution retains `Z` when every process releases a bit, otherwise
treats `Z` as the AND/OR identity and applies four-state logical dominance per
bit. Native resolver selection now admits multiple mixed-language boundary
drivers without requiring a redundant binding resolver, while unresolved
variables continue to receive the existing deterministic multiple-driver
diagnostics. The mixed-driver matrix proves VHDL Logic9 `0`, `1`, and `Z`
drivers collapsing into six SystemVerilog wired nets with conflict, release,
and all-high-impedance outcomes; the reverse VHDL `std_logic` matrix proves
SystemVerilog Logic4 conflict/release resolution and concurrent output fanout.
The obsolete `FSIM-ELAB-DRV-002` unsupported-policy diagnostic was removed.
The exact eight-worker LLVM Debug build succeeded; frontend, diagnostics
catalog, source-line budget, elaboration, the existing compiled resolution
application, and runtime passed 6/6 tests in 0.48 seconds (0.02, 0.08, 0.11,
0.16, 0.09, and 0.01 seconds). Task 7 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 7 extends the mixed resolution application with a recursive
SystemVerilog/VHDL/SystemVerilog timing hierarchy and six width-conversion
adapters. Zero-delay VHDL transport reaches the eight-bit SystemVerilog actual
in the same timestamp without duplicate publication. Positive VHDL default
inertial, explicit `reject 2 ps inertial`, and transport transactions preserve
their exact 5/15/16/25 ps cancellation or pulse histories through the outer
adapter. A nested SystemVerilog transition-delay leaf preserves its 3 ps fall,
12 ps rise, and 24 ps turnoff publications through one-to-four and four-to-eight
mixed adapters, including final high impedance. The application locks the
initial partial-domain publications as well as the absence of premature pulse
visibility. The exact eight-worker LLVM Debug build succeeded; diagnostics
catalog, source-line budget, elaboration, resolution application, and runtime
passed 5/5 tests in 0.45 seconds (0.08, 0.11, 0.16, 0.09, and 0.01 seconds).
Task 8 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 8 extends that hierarchy with a SystemVerilog active blocking write,
inactive `#0` write, and two same-slot NBA writes whose later source-order value
wins in the update phase. Exact callback histories prove `00`, `01`, then `11`
on the leaf in delta zero, one-to-four and four-to-eight adapter publications in
deltas one and two, and a VHDL zero-delay transport fanout in delta three.
`$strobe` observes the NBA winner once in the postponed phase. A second
two-adapter SystemVerilog/VHDL feedback loop deterministically advances from
unknown through 0, 1, 2, and 3 and quiesces at delta 16. Independent signal
observers agree exactly, VCD records all external and internal boundary nodes,
and `$stop` pauses at 1 ps for debugger inspection before a successful resume
to the 30 ps design finish. The exact eight-worker LLVM Debug build succeeded;
diagnostics catalog, source-line budget, elaboration, resolution application,
and runtime passed 5/5 tests in 0.48 seconds (0.08, 0.12, 0.17, 0.10, and 0.01
seconds). Task 9 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 9 combines an explicit SystemVerilog Boolean construction override with
the complete driver/timing hierarchy and verifies its canonical Boolean
`vhdlconst-v1` identity. The application now captures the entire phase,
feedback, delay, postponed-output, debugger, independent-callback, final-value,
VCD, specialization-key, and boundary-conversion state under the interpreter
and LLVM O0/O2. Each optimization proves a cold native-cache fill and exact
warm hits, then edits the VHDL source, observes the changed zero-delay result,
and requires a changed specialization-key set plus at least one native miss
while retaining construction identity and boundary topology. The feature
matrix now marks ML-008 and ML-010 executable and expands ML-007 for native
wired-AND/OR resolution; the language-support inventory no longer lists wired
resolution as a gap. The exact seven-test focused gate passed in 1.59 seconds:
diagnostics catalog 0.08, source-line budget 0.11, mixed matrix 0.01,
elaboration 0.17, resolution 0.45, mixed conversions 0.75, and runtime 0.01
seconds. Task 10 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 10 is complete. The LLVM-disabled ASan/UBSan regression passed all 75
tests in 329.57 seconds with no sanitizer findings; leak detection alone was
disabled for the locally traced run because LeakSanitizer cannot operate under
the workspace tracer, while the CI preset retains leak detection. The exact
LLVM 22.1.8 warnings-as-errors Debug regression passed all 78 tests, including
`fsim.application.scoped_locals` in 0.81 seconds and the expanded resolution
application in 0.45 seconds. The corresponding Release build and 78-test
regression passed in 168.86 seconds, with scoped locals in 0.81 seconds and
resolution in 0.53 seconds. Both full suites include the diagnostics catalog,
source-line budget, IEEE inventory, and v1 matrix gates. Batch 122 closes as
one accumulated commit/push checkpoint and, because it is not a tenth-batch
boundary, has no GitHub Actions inspection.

### One-hundred-twenty-third feature batch — SystemC named hierarchy and interfaces — Complete

The current ten implementation tasks are:

1. **Complete.** Audit SC-001 through SC-022 and the SystemC facade/ABI,
   hierarchy registry, DesignIR, debugger/API, and application evidence for
   named-object hierarchy, ports, exports, standard interfaces, and bounded
   custom metadata; define the exact positive and failure matrix.
2. **Complete.** Add bounded `sc_object` identity and introspection for modules,
   ports, exports, signals, primitive channels, events, and processes,
   including stable `name`, `basename`, `kind`, and parent ownership.
3. **Complete.** Preserve deterministic fully qualified names and construction
   order across native children, foreign HDL placeholders, factory roots, and
   repeated `sc_gen_unique_name` use, rejecting duplicate or invalid sibling
   names transactionally.
4. **Complete.** Complete parent/child object traversal and lookup through the
   append-only plug-in ABI and common hierarchy, with stable handles and no
   cross-build or destroyed-object leakage.
5. **Complete.** Complete typed `sc_in`, `sc_out`, and `sc_inout` binding policies
   across direct interfaces, signals, parent/child port chains, and HDL aliases,
   including direction, cardinality, cycle, skipped-parent, and unbound checks.
6. **Complete.** Complete `sc_export` binding and transitive resolution for the
   supported standard signal interfaces, including export-to-interface,
   export-to-export, port-to-export, read/write capability, and exact failures.
7. **Complete.** Materialize every supported SystemC named object in common
   DesignIR/API/debugger/VCD hierarchy with consistent source, kind, parent,
   signal identity, and lookup behavior across mixed-language boundaries.
8. **Complete.** Add metadata-only registration for bounded custom interface and
   primitive-channel kinds permitted by v1, preserving names and hierarchy
   while rejecting unsupported custom binding, value, or asynchronous-update
   behavior explicitly.
9. **Complete.** Prove the combined named hierarchy/port/export/interface matrix
   in compiled SystemC with interpreter and LLVM O0/O2 HDL peers, cold/warm/edit
   cache, lifecycle, callbacks, debugger, VCD, and exact negative diagnostics.
10. **Complete.** Update matrix/subset/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 123 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. Keep this exact ten-task
list current in both the official plan and resume handoff. Tasks 1 through 9
use one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 123 does not require a
non-documentation CI inspection.

Task 1 confirms that SC-001 through SC-022 already execute the bounded source
facade, typed factories, native and foreign child modules, per-category object
registration, standard signal interfaces, direct-parent port chains, typed
export chains, common signal aliases, lifecycle, process/event/channel
scheduling, and construction actuals. Registry descriptions retain stable
handles, local names, module parents, and declaration order, but the facade
exposes a name only for `sc_module`; there is no common `sc_object`, basename,
kind, parent, child traversal, or lookup surface. Duplicate detection is
category-local rather than one sibling object namespace, and
`sc_gen_unique_name` is a process-thread counter rather than a hierarchy-scoped
collision-aware allocator. DesignIR retains SystemC instance, port, event,
primitive-channel, signal, export, and process records, but only modules,
processes, and signal aliases reach the common API/debug/VCD object model.
Standard `sc_signal_in_if`/`sc_signal_inout_if` bindings are typed and
executable; arbitrary custom-interface calls and values remain outside v1, so
the bounded closure is metadata-only registration with exact rejection of
unsupported behavior. The exact eight-worker Debug build required no work;
elaboration, facade header, strict C ABI, plug-in loader, plug-in compiler,
main application, and SystemC datatype application passed 7/7 focused tests
in 20.17 seconds. Task 2 is current; this starts the intentional accumulated
Batch 123 dirty worktree with no sanitizer, Release, full regression, commit,
push, or CI inspection at this task boundary.

Task 2 adds a common facade `sc_object` base with stable `name()`, `basename()`,
`kind()`, and `get_parent_object()` identity. `sc_module`, `sc_in`, `sc_out`,
`sc_inout`, `sc_export`, `sc_signal`/`sc_prim_channel`, and `sc_event` now carry
that identity, and process registration retains a stable owned object with the
exact method/thread/cthread kind. Member ports and registered processes receive
fully qualified module-relative names and the module parent; standalone named
objects retain null parents. The append-only native ABI is unchanged. Header
tests prove the base relationships and exact module, port, signal, event,
channel, export, and method-process identities. The exact eight-worker Debug
build succeeded; facade header, strict C ABI, plug-in loader, main application,
diagnostics catalog, and source-line budget passed 6/6 focused tests in 16.42
seconds. Task 3 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 3 makes `sc_gen_unique_name` deterministic per module and base, skips
explicit sibling collisions, and preserves independent counters for separate
module instances. Facade construction now rejects invalid child basenames and
cross-kind duplicate sibling objects before registration. The native host
independently enforces the same one-namespace rule across ports, foreign/native
children, processes, events, primitive channels/signals, and exports, while
recognizing a typed `sc_signal` as the metadata promotion of its existing
primitive-channel handle. Qualified mixed-language root paths retain their
full `name()` and expose only the last component as `basename()`; native child
names remain local inputs and receive exactly one parent-qualified path.
Header tests cover explicit-name skipping, per-instance counters, invalid
names, and cross-kind duplicates, while the application retains its native
duplicate-child failure and compiled mixed hierarchy. The exact eight-worker
Debug build succeeded; elaboration, facade header, strict C ABI, plug-in loader,
and main application passed 5/5 focused tests in 16.20 seconds, followed by a
2/2 diagnostics/source gate in 0.20 seconds. Task 4 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 4 adds declaration-ordered `sc_object::get_child_objects()`, domain-scoped
`sc_find_object()` and top-level enumeration, and destructor removal so facade
lookups never retain destroyed pointers. Hierarchy domains use the active host
context, isolating simultaneously loaded build roots while keeping standalone
facade objects usable. The common `HierarchyRegistry` now returns copied,
ABI-neutral object metadata by stable handle, direct children in monotonically
allocated registration order, and absolute or root-relative path lookup whose
parent chain must reach the requested root. Modules, ports, foreign children,
processes, events, standalone primitive channels, promoted signals, exports,
and native modules are classified; signal promotion retains one handle rather
than duplicating its primitive channel. Header tests prove child order, lookup,
top-level membership, process discovery, and destruction cleanup. Loader tests
prove root/port/process/foreign-child metadata, nested roots, child ordering,
relative/absolute lookup, missing paths, and stable parent handles. The exact
eight-worker Debug build succeeded; facade header, strict C ABI, plug-in loader,
and main application passed 4/4 focused tests in 16.04 seconds, followed by a
2/2 diagnostics/source gate in 0.21 seconds. Task 5 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 5 completes the typed port policy independently in the facade and native
host. Existing templates retain exact interface value types and prevent
cross-direction chains at compile time; host object metadata now also retains
port direction and rejects a child input→parent output, child output→parent
input, or any inout→non-inout chain even for a raw ABI plug-in. Encoding and
width equality, same/direct-parent signal scope, direct-parent port/export
scope, one-target cardinality, idempotent rebinding, and structural cycle/
skipped-parent rejection remain enforced. Common elaboration now emits
`FSIM-ELAB-BIND-058` for every unbound native-child port while leaving selected
root factory ports and HDL-connected ports as external aliases. A raw compiled
plug-in probe must observe rejection of an output→input bind before accepted
input→input and output→output binds can build two SystemC levels. A separate
native fixture requires exactly two unbound-port diagnostics; existing direct
port, signal, export, HDL alias, conflicting-target, cycle, and deep-parent
cases remain green. The exact eight-worker Debug build succeeded, and the
expanded main application passed in 16.57 seconds after the preceding 7/7
SystemC/elaboration/catalog/source focused gate passed in 16.64 seconds. Task 6
is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 6 appends an optional `set_export_writable` host callback without changing
the v1 ABI prefix. Current facade exports set read-only capability for
`sc_signal_in_if<T>` and writable capability for `sc_signal_inout_if<T>` before
binding; legacy plug-ins retain the former writable default. Registry binding
now rejects a writable export targeting a read-only export and an output/inout
port targeting a read-only export, while permitting read-only narrowing onto a
writable signal/export. Existing exact encoding/width, same/direct-parent,
one-target, cycle, unknown-handle, and unbound checks remain active. Capability
travels through immutable factory descriptions and elaborated DesignIR export
records. The raw compiled probe requires writable→read-only export and
output→read-only-export rejection before read-only/writable signal bindings and
an input→read-only-export alias can build successfully. The real four-export
hierarchy proves two read-only and two writable records plus unchanged common
signal identity and execution. The exact eight-worker Debug builds succeeded;
diagnostics, source budget, elaboration, facade, strict C ABI, loader, and main
application passed 7/7 focused tests in 16.60 seconds. Task 7 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection ran at this
task boundary.

Task 7 materializes modules, foreign HDL children, ports, processes, events,
primitive channels, signals, and exports as immutable named DesignIR objects.
Each record retains its stable native handle, exact common path and parent,
SystemC kind/type, optional process, and optional dense signal identity. The C
API appends event, channel, and export kinds, preserves distinct alias handles
while reading and writing their shared signal, and enumerates native SystemC
modules in the same scope tree as HDL instances. Debugger scope discovery now
retains value-less native hierarchy, while common signal lookup and production
VCD declarations include every value-bearing SystemC alias, including native
child ports. A compiled API factory proves exact root/child traversal and
lookup for all supported object categories; the mixed application proves
DesignIR identity, debugger navigation, VCD scopes, and nested alias values.
The exact eight-worker Debug build succeeded, and the main application plus C
API tests passed 2/2 in 18.82 seconds. Task 8 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 8 appends metadata-object registration and primitive-channel kind labeling
to the v1 host ABI while preserving its complete prefix. The facade adds a
metadata-only generic `sc_port<IF>`, records unsupported-interface
`sc_export<IF>` objects, and permits derived primitive channels to supply a
bounded explicit kind. Custom interfaces may expose a stable kind through
`IF::fsim_kind()`; otherwise they use `sc_interface`. These objects retain
stable native handles, exact names/parents, port/export/channel categories,
and custom type names through the registry, immutable factory description,
DesignIR, and C API, but intentionally have no dense signal. A compiled
factory proves positive custom port/export/channel metadata and exact
`FSIM-SC-A004` construction failure for custom binding. Runtime attempts at
custom value access or asynchronous primitive-channel update throw exact
unsupported errors and poison the simulation rather than silently executing.
The exact eight-worker Debug build succeeded; facade, strict C ABI, loader,
main application, C API, diagnostics catalog, and source budget passed 7/7
focused tests in 18.92 seconds. Task 9 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 9 adds one compiled mixed-language matrix that combines root and native-
child ports, standard read-only and writable exports, promoted signals, a
named event and method process, metadata-only custom port/export/channel
objects, and ordered lifecycle callbacks. The same fixture now proves
interpreter reference behavior, LLVM O0/O2 cold and warm native-cache parity,
source-edit invalidation, debugger traversal, and production VCD aliases while
retaining the exact custom binding, value-access, and asynchronous-update
failures from Task 8. The exact eight-worker Debug build succeeded; the main
application passed in 28.10 seconds, and the expanded application, C API,
C-header, facade, strict C ABI, loader, diagnostics-catalog, and source-budget
gate passed 8/8 in 29.61 seconds. Task 10 is current; no sanitizer, Release,
full regression, commit, push, or CI inspection has run yet.

Task 10 is complete. The LLVM-disabled ASan/UBSan regression passed all 75
tests in 375.25 seconds with no sanitizer findings; leak detection alone was
disabled for the locally traced run because LeakSanitizer cannot operate under
the workspace tracer, while the CI preset retains leak detection. Its initial
warnings-as-errors build exposed and repaired three stale aggregate fixtures
that omitted the newly retained foreign-child handles and trailing identity
field. The exact LLVM 22.1.8 warnings-as-errors Debug regression then passed
all 78 tests in 200.08 seconds, including `fsim.application.scoped_locals` in
0.82 seconds and resolution in 0.47 seconds. The corresponding Release build
and 78-test regression passed in 177.78 seconds, with scoped locals in 0.84
seconds and resolution in 0.44 seconds. Both full suites include the
diagnostics catalog, source-line budget, IEEE inventory, and v1 matrix gates.
Batch 123 closes as one accumulated commit/push checkpoint and, because it is
not a tenth-batch boundary, has no GitHub Actions inspection.

### One-hundred-twenty-fourth feature batch — SystemC scheduling and lifecycle — Complete

The current ten implementation tasks are:

1. **Complete.** Audit SC-007, SC-008, and SC-012 through SC-017 across the
   facade, append-only ABI, hierarchy registry, common scheduler, lifecycle,
   fibers, and application evidence; define the exact missing sensitivity,
   timeout, event, update-phase, and failure matrix.
2. **Complete.** Complete static sensitivity for ports, internal signals, named
   events, and positive/negative edge finders across methods, threads, and
   clocked threads, including deterministic deduplication and exact invalid or
   unbound-object diagnostics.
3. **Complete.** Complete dynamic `next_trigger` for time, event, OR/AND event
   lists, and timed event/list timeouts, with one replacement wait per method
   invocation and deterministic tie handling.
4. **Complete.** Complete `wait` for time, zero time, event, OR/AND event lists,
   timed event/list timeouts, and plain static sensitivity across `SC_THREAD`
   and `SC_CTHREAD`, including repeated suspension and teardown.
5. **Complete.** Close immediate, delta, timed, delayed, replacement,
   cancellation, duplicate, and same-timestamp named-event scheduling across
   method/thread waiters with exact pending-state and tick-conversion failures.
6. **Complete.** Close primitive-channel update ordering and deduplication,
   including self/cross-channel requests, port reads/writes, event notification,
   callback containment, and requests made from process and update phases.
7. **Complete.** Complete root/native-child lifecycle ordering and isolation for
   cold/warm builds, natural quiescence, `$finish`, explicit stop/resume,
   teardown, callback state, and exact structural or scheduling rejections.
8. **Complete.** Prove deterministic common-kernel phase interactions among
   nested native methods/threads/channels/events and SystemVerilog/VHDL peers,
   including stable process/channel order and no recursive native execution.
9. **Complete.** Prove the combined scheduling/lifecycle matrix in compiled
   SystemC with interpreter and LLVM O0/O2 HDL peers, cold/warm/edit cache,
   debugger, VCD, callbacks, teardown, and exact negative diagnostics.
10. **Complete.** Update matrix/subset/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 124 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. The ten tasks used one accumulated worktree and
one Task 10 sanitizer, full-regression, documentation, commit, and push gate.
GitHub builds use parallelism four, and Batch 124 does not require a non-
documentation CI inspection.

Task 1 confirms that the common kernel already executes initialized and
`dont_initialize()` methods, basic thread/clocked-thread fibers, scalar static
port/signal/event sensitivity with edge qualifiers, exclusive time/event/
OR-list/AND-list dynamic waits, immediate/delta/timed/delayed event scheduling
and cancellation, deduplicated channel updates, module-local signal updates,
and ordered root/native-child lifecycle callbacks. Static registration already
deduplicates identical object/edge pairs and rejects cross-module objects or an
edge-qualified named event. The missing v1 closure is timed event/list timeout
selection, explicit method-only versus thread-only API checks, expanded static
method/thread/CTHREAD coverage, complete same-timestamp event/channel phase
ordering, and lifecycle stop/teardown/rejection evidence. The exact eight-
worker Debug build required no work; elaboration, facade, strict C ABI, loader,
compiler, main application, and runtime passed 7/7 focused tests in 30.19
seconds. Task 2 is current; this starts the intentional accumulated Batch 124
dirty worktree with no sanitizer, Release, full regression, commit, push, or CI
inspection at this task boundary.

Task 2 expands the real compiled fiber fixture so a method is statically
sensitive to one named event registered twice, a clocked thread retains its
positive-edge finder, and a `dont_initialize()` thread waits on one negative-
edge finder registered twice. DesignIR canonicalizes each repeated object/edge
pair to one sensitivity, and the HDL-driven run proves two named-event method
invocations, two positive-edge CTHREAD resumptions, and one negative-edge
thread resumption. Manual immutable descriptions separately prove exact
`FSIM-ELAB-BIND-043`, `-044`, and `-045` rejection for an unknown object, an
invalid edge encoding, and a nonscalar edge target. Existing internal-signal,
port, unbound-native-port, and cross-module registry cases remain green. The
exact eight-worker Debug build succeeded; elaboration, application, facade,
strict C ABI, loader, diagnostics catalog, and source budget passed 7/7 in
29.55 seconds. Task 3 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 3 appends one `wait_event_timeout` host callback and carries an optional
timeout through the ABI-neutral SystemC suspension and common alternate-
executor boundary. Event/list waits register their canonical dynamic fanout
and reuse the scheduler's generation-checked timeout machinery; an event wake
invalidates its deadline, a timeout removes its event registrations, and stale
heap entries remain deterministic no-ops. The facade adds timed single-event,
OR-list, and AND-list `next_trigger` overloads, restricts every `next_trigger`
form to `SC_METHOD`, and preserves last-call replacement within one callback.
A real compiled method matrix proves event wins, timeout wins, a same-timestamp
tie, duplicate list canonicalization, AND progress, replacement, and
interpreter/compiled parity at exact observed producer states. The host ABI
offset assertion remains append-only. The exact eight-worker Debug build
succeeded; application, runtime, facade, strict C ABI, diagnostics catalog,
and source budget passed 6/6 in 27.98 seconds. Task 4 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this boundary.

Task 4 exposes the same timed event/list host suspension to `SC_THREAD` and
`SC_CTHREAD` through new time-plus-event, OR-list, and AND-list `wait`
overloads, while retaining exact rejection from `SC_METHOD`. The compiled fiber
fixture now interleaves an event-won single wait, a timeout-won single wait, an
event-won OR wait, and a timeout-won partially satisfied AND wait across four
ordinary C++ stack resumptions. Existing finite time, zero-time, repeated named
event, plain positive/negative static sensitivity, and clocked-thread waits
remain green, and the host stops only after every new suspension has resumed.
All event fanout and timeout generations are cleared before fiber teardown.
The exact eight-worker Debug build succeeded; application, runtime, facade,
strict C ABI, loader, diagnostics catalog, and source budget passed 7/7 in
28.09 seconds. Task 5 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 5 closes named-event scheduling across the existing generation-checked
runtime replacement/cancellation tests, the dynamic method matrix, and the
expanded fiber fixture. Immediate notification, next-delta notification,
earliest timed replacement, later timed no-op, explicit cancellation, strict
single-pending `notify_delayed`, and stale heap generations now have joint
method/thread evidence. Stable process IDs make same-timestamp producer,
method, fiber, and timeout work deterministic without recursive native entry.
Two compiled negative factories additionally prove the exact unrepresentable-
project-tick failure and duplicate-pending delayed-notification failure poison
only their sessions. The exact eight-worker Debug build succeeded;
application, runtime, facade, strict C ABI, loader, diagnostics catalog, and
source budget passed 7/7 in 28.63 seconds. Task 6 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this boundary.

Task 6 combines the kernel's stable handle-ordered channel test with two real
compiled primitive channels. Process-phase duplicate requests coalesce; a
self-request made during `update()` is ignored; a request from the later
channel to the earlier channel is deferred to the next delta; and port writes
commit through the shared update phase. The later channel also performs an
immediate named-event notification during update, deterministically waking a
method without recursive native entry and canceling the event's earlier timed
notification. Exact final value/update/event counters agree between the
interpreter and compiled engine. A separate throwing channel preserves its
original exception diagnostic, poisons only that simulation, and cannot escape
the native ABI. The exact eight-worker Debug build succeeded; application,
runtime, facade, strict C ABI, loader, diagnostics catalog, and source budget
passed 7/7 in 29.08 seconds. Task 7 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 7 retains exact parent-before-child elaboration/start and child-before-
parent end ordering while extending the real lifecycle fixture to three
independent cold/warm roots. Interpreter and compiled `$finish` runs agree; a
pre-requested external stop starts SystemC once without ending it, then clear/
resume reaches the same terminal state and calls end exactly once; and a
direct SystemC top reaches natural quiescence at time zero. Destruction still
ends any started nonterminal root after shutting down fibers. Runtime access
from lifecycle callbacks is now explicitly context-checked: event notify,
delayed notify, and cancel cannot dereference a missing process context, and a
raw lifecycle suspension is rejected. Compiled negative roots prove event
scheduling and `next_trigger` rejection poison only their simulations, while
the existing elaboration/end exception cases remain isolated. The exact
eight-worker Debug build succeeded; application, runtime, facade, strict C
ABI, loader, diagnostics catalog, and source budget passed 7/7 in 30.15
seconds. Task 8 is current; no sanitizer, Release, full regression, commit,
push, or CI inspection ran at this task boundary.

Task 8 drives the same compiled nested method, thread, clocked-thread, named-
event, timeout, and channel phase interactions from both SystemVerilog and
VHDL peers. The VHDL host now supplies matching positive and negative clock
edges through the common scheduler and naturally drains at tick 5; its exact
static, named-event, event-won, timeout-won, and clocked-thread results match
the SystemVerilog host. Existing stable handle ordering, deferred cross-channel
updates, and update-phase event notification prove that neither peer can cause
recursive native execution. The exact eight-worker Debug build succeeded;
application, runtime, facade, strict C ABI, loader, diagnostics catalog, and
source budget passed 7/7 in 31.78 seconds. Task 9 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 9 adds one combined SystemVerilog and VHDL peer around the fiber-thread,
primitive-channel, and ordered-lifecycle roots. Interpreter references and
LLVM O0/O2 cold/warm pairs agree on all eleven observable signals, terminal
time, status, and callback count. A SystemC comment edit invalidates the
project/plugin cache while retaining exact native HDL cache hits; the debugger
and VCD expose the nested thread plus peer-visible channel and lifecycle
signals. Compiled channel-update, lifecycle-suspension, and terminal-lifecycle
failures retain their exact messages, poison only their sessions, and a fresh
post-teardown interpreter run proves isolation. The new 322-line scheduling
partition leaves the existing integration and generated-source partitions at
1,844 and 1,619 lines. The exact eight-worker Debug build succeeded;
application, runtime, facade, strict C ABI, loader, diagnostics catalog, and
source budget passed 7/7 in 53.28 seconds. Task 10 is current; it owns the
single accumulated sanitizer, full regression, documentation, commit, and
push gate, with no CI inspection at this non-boundary batch.

Task 10 is complete. The release authority and SystemC subset now record timed
event/list waits, method replacement, stable channel phases, lifecycle stop/
resume and teardown, and the combined SV/VHDL differential. The diagnostics
catalog covers 1,622 production codes, and all 398 authored sources pass the
2,000-line gate. The LLVM-disabled ASan/UBSan suite passed 75/75 in 438.21
seconds with leak detection disabled only for the locally traced run; the main
application, containers, and scoped locals took 140.54, 219.83, and 0.49
seconds. The exact LLVM 22.1.8 warnings-as-errors Debug suite passed 78/78 in
225.27 seconds, with the application at 54.43 seconds, containers at 111.04,
resolution at 0.54, and scoped locals at 0.84. Release passed 78/78 in 205.80
seconds, with the application at 52.34 seconds, containers at 94.22,
resolution at 0.53, and scoped locals at 0.86. Batch 124 closes as one
accumulated checkpoint and has no GitHub Actions inspection because it is not
a tenth-batch boundary.

### One-hundred-twenty-fifth feature batch — SystemC compiler, cache, and portability — Complete

The current ten implementation tasks are:

1. **Complete.** Audit the SystemC source compiler, dependency scanner,
   persistent plug-in cache, loader, registry, native-cache composition, fiber
   backends, and existing Linux/Windows evidence; define every missing compiler,
   cache, lifecycle, error, and portability case.
2. **Complete.** Make plug-in compile fingerprints canonical across ordered
   sources, content, include directories, definitions, language mode, compiler
   identity/version/target, options, ABI version, and selected fiber backend.
3. **Complete.** Complete transitive dependency fingerprints and invalidation for
   edited, added, removed, generated, missing, and system headers across GNU-
   style and MSVC dependency discovery, including paths containing spaces.
4. **Complete.** Make persistent cache lookup/publication transactional and
   concurrency-safe, with deterministic recovery from missing, truncated,
   corrupted, stale, or incompatible metadata and shared-library artifacts.
5. **Complete.** Prove loaded-image, registration, factory, root, fiber, and
   callback ownership across cold/warm/edit builds, concurrent independent
   sessions, terminal and nonterminal teardown, rebuild, and unload ordering.
6. **Complete.** Close exact compile, link, dependency, load, entry-point, ABI,
   initialization, registration, construction, destruction, and callback error
   containment without partial registration or stale cache publication.
7. **Complete.** Compose plug-in/factory/construction/hierarchy identity into
   interpreter, LLVM O0/O2, and debug native-cache behavior, preserving valid
   reuse while preventing stale code, objects, callbacks, or native handles.
8. **Complete.** Harden Windows process invocation, quoting, response paths,
   DLL/PDB/runtime discovery, compiler diagnostics, PE/MASM fiber selection,
   and thread teardown while retaining portable Linux behavior.
9. **Complete.** Add a combined compiler/cache/lifecycle matrix with Linux
   execution and Windows-targeted Debug/Release/LLVM/thread regression coverage,
   cold/warm/edit/corruption/concurrency cases, and exact negative diagnostics.
10. **Complete.** Update matrix/subset/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 125 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **complete**. This exact ten-task list remains the durable
Batch 125 record in both the official plan and resume handoff. Tasks 1 through
9 used one accumulated dirty worktree with focused eight-worker Debug builds
and tests; Task 10 owns the sanitizer, full regressions, documentation, commit,
and push gate. GitHub builds use parallelism four, and Batch 125 does not
require a non-documentation CI inspection.

Task 1 confirms that the current compiler already hashes ordered source
contents, explicit include/define sequences, ABI/host format, compiler binary,
explicit linked libraries, and GCC-emitted transitive dependency closures. It
uses per-key locks, checksum-validated atomic artifacts, post-compile identity
verification, unique MSVC object/PDB paths and response files, buffered plug-in
initialization, and fiber-before-module-before-library teardown. Existing tests
cover spaces and literal arguments, stale locks, header/source/library edits,
volatile macros, implicit GCC roots, malformed options, missing compilers,
changed-during-compile rejection, initialization exceptions, PE/MASM source
selection, and the four Windows LLVM/MSVC build shapes. The remaining closure
is compiler/frontend/linker plus environment identity, compiler-emitted MSVC
dependency closure, corruption/concurrent publication, transactional registrar
rollback, complete load/factory/destructor errors, native-cache coupling, and
executable Windows lifecycle/thread evidence. The exact eight-worker Debug
build required no work; facade, loader, compiler, application, diagnostics
catalog, and source budget passed 6/6 in 55.95 seconds. Task 2 is current; this
starts the accumulated Batch 125 dirty worktree with no sanitizer, Release,
full regression, commit, push, or CI inspection at this task boundary.

Task 2 versions the compile fingerprint as `systemc-compiler-v2` and records
the fixed C++20 source contract, runtime/SystemC ABI, host toolchain/format,
MSVC CRT choice, and exact configured Boost.Context backend. Persistent reuse
now requires hashing the resolved compiler executable rather than falling back
to size/mtime alone. Ordered compiler-relevant environment values cover PATH,
GCC include/library/program roots and reproducible-time input on GCC-like
hosts, plus MSVC include/library/toolset/SDK/CL state on Windows. Existing
ordered include, definition, raw-option, explicit-library, and source content
sequences remain part of the key; raw options remain deliberately
noncacheable. Tests prove stable repeats plus source-order, definition, and
toolchain-environment divergence, including restoration to the original key.
The exact eight-worker Debug builds succeeded; compiler, application,
diagnostics catalog, and source budget passed 4/4 in 54.51 seconds, with the
application at 52.72 seconds. The compiler implementation and test remain at
393 and 860 lines. Task 3 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 3 adds compiler-emitted MSVC dependency closure through
`/sourceDependencies` JSON while retaining the conservative manifest scanner
when the selected compiler lacks the option, compilation cannot produce a
report, or the report is malformed. The bounded parser accepts UTF-8 BOMs,
JSON escapes, absolute paths containing spaces, and the documented `Source`,
`Includes`, PCH, imported-module BMI, and imported-header-unit Header/BMI
inputs. Every readable dependency is normalized, sorted, deduplicated, and
content-hashed; source files are excluded from the duplicate dependency list,
and binary IFC/PCH images are hashed without loading them into the volatile
predefined-macro scanner. The shared GNU/MSVC finalizer now applies the same
volatile-macro and deterministic ordering rules to both compiler-emitted
formats. A portable fake `cl.exe` fixture proves escaped/BOM JSON, header and
BMI edits, paths with spaces, a removed dependency, and malformed-report
fallback on Linux, while native Windows plans now require system-header
closure to remain cacheable. The exact eight-worker Debug build succeeded;
compiler, application, diagnostics catalog, and source budget passed 4/4 in
55.86 seconds, with the compiler at 1.61 seconds and application at 54.05
seconds. The dependency implementation and compiler test remain at 848 and
1,003 lines. Task 4 is current; no sanitizer, Release, full regression,
commit, push, or CI inspection ran at this task boundary.

Task 4 replaces the loose checksum sidecar with a versioned SystemC artifact
commit record containing the exact cache key, shared-library size, and SHA-256.
The library is atomically installed first and the metadata rename is the final
commit marker, so readers accept only one complete matching pair; a failed
metadata commit removes the uncommitted library. Under the existing process-
aware per-key lock, rebuild preparation removes abandoned build outputs,
objects/PDBs, metadata temporaries, and legacy checksum temporaries, while a
successful publication retires the legacy checksum. Missing libraries or
metadata, zero/truncated libraries, truncated/corrupt/incompatible metadata,
size or checksum mismatches, and stale staging are deterministic misses and
self-repair through one writer. The compiler fixture forces every recovery
shape and launches three simultaneous callers after deleting the committed
pair; exactly one reports a cold build, both waiters report validated hits,
and the following lookup remains warm. The exact eight-worker Debug build
succeeded; compiler, application, diagnostics catalog, and source budget
passed 4/4 in 56.24 seconds, with the compiler at 2.42 seconds and application
at 53.61 seconds. The compiler facade, cache implementation, dependency
implementation, and compiler test remain at 390, 1,293, 848, and 1,099 lines.
Task 5 is current; no sanitizer, Release, full regression, commit, push, or CI
inspection ran at this task boundary.

Task 5 closes loaded-image and executable-state ownership. Live-registry
move-assignment no longer lets `unique_ptr` destroy an old implementation
without its lifecycle protocol: the shared reset path first resumes and stops
suspended fibers, gives each still-started root one reverse-order best-effort
`end_of_simulation`, destroys module objects in reverse construction order,
then unloads the plug-in before releasing its host context and registry state.
Explicitly ended and poisoned roots are not repeated. `Plugin` itself is now
nonmovable so default member assignment cannot discard its retained host table
before unloading the old image. A read-only platform query provides executable
proof of image residency without changing its loader reference count.

The sample plug-in now exports a bounded lifecycle event probe and a minimal
suspending thread factory. The loader test proves explicit terminal ordering
for parent/nested roots, two simultaneously live independent registries, a
suspended fiber stopped before implicit terminal/destruction during move-
assignment, safe adoption and teardown of the second registry, and image
residency until the final owner is released followed by actual unload. The
existing application matrix continues to cover cold/warm native reuse and an
edited SystemC source image. The exact eight-worker Debug build succeeded;
loader, compiler, application, diagnostics catalog, and source budget passed
5/5 in 56.27 seconds, with the loader at 0.00, compiler at 2.25, and application
at 53.82 seconds. The dynamic-loader, hierarchy, plug-in loader test, and
sample plug-in remain at 162, 938, 453, and 321 lines. Task 6 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection ran at
this task boundary.

Task 6 makes registration replay transactional for the fsim hierarchy. Buffer
callbacks validate unique factory names, factory-before-parameter order,
parameter uniqueness, construction types, and default ranges, and remember
every rejected callback. A plug-in that ignores rejection and returns success
is rejected before any caller registrar callback runs. Hierarchy loading now
replays into a separate staging implementation, swaps the factory table only
after complete success, and explicitly permits the image to unload when that
discardable staging transaction fails. Arbitrary external registrars retain
the conservative image quarantine when their accepted callbacks cannot be
rolled back, preventing stale code pointers.

Dedicated negative images and tests cover missing images and entry points,
host/registrar ABI mismatch, initialization exceptions, ignored duplicate
registration, construction status/null/exception failures with pending-handle
rollback, escaped process callbacks, throwing destructors, and image unload
after containment. The compiler fixture now executes portable preprocessing/
compile and link failures and requires exact `FSIM-SC-C007` diagnostics and
captured compiler output; existing missing compiler, option, dependency,
publication, and changed-during-compile cases remain intact. The exact eight-
worker Debug build succeeded; loader, compiler, application, diagnostics
catalog, and source budget passed 5/5 in 56.05 seconds, with the loader at
0.00, compiler at 2.42, and application at 53.41 seconds. The plug-in loader,
hierarchy, loader test, negative image, empty image, and compiler test remain
at 352, 941, 558, 149, 6, and 1,128 lines. Task 7 is current; no sanitizer,
Release, full regression, commit, push, or CI inspection ran at this task
boundary.

Task 7 versions specialization provenance as
`fsim-specialization-provenance-v4` and composes the exact SystemC plug-in
compile key with every stable factory, typed construction, instance-path,
port/event/channel/signal/export, process, and debugger-visible object mapping.
Transient image addresses, callback pointers, and native handles stay out of
persistent objects: each fresh interpreter binds those values through its
owned hierarchy registry, while LLVM O0/O2 and debug modules reuse native code
only when the stable common-runtime mapping is unchanged. Identical builds
retain the same specialization keys; selecting a compatible alternative
factory or a native-child hierarchy changes them.

The SystemC scheduling matrix proves interpreter semantics, cold/warm LLVM O0
and O2 reuse, and cold/warm debug O0 reuse. Editing the loaded plug-in source
now produces native-cache misses and stores rather than the formerly accepted
hits, preventing reuse across changed callbacks or hierarchy construction. The
exact eight-worker Debug build succeeded; the application passed in 54.39
seconds, and plug-in, compiler, diagnostics catalog, and source budget passed
4/4 in 2.48 seconds. The identity implementation, SystemC integration test,
and scheduling matrix remain at 828, 1,886, and 352 lines. Task 8 is current;
no sanitizer, Release, full regression, commit, push, or CI inspection ran at
this task boundary.

Task 8 hardens the MSVC-compatible source-compiler contract without changing
the portable direct-argv path. Compile commands select UTF-8 C++20 diagnostics,
the host's exact CRT and Debug/Release optimization/debug mode, unique object
and source-PDB paths, and full paths in captured diagnostics. The x86-64 DLL
link uses explicit nonincremental PE output, link-PDB, import-library, and
export paths; raw options cannot override those outputs, machine type, or
incremental policy. The fetched Windows Boost.Context path now rejects
non-64-bit or known non-x86-64 targets before selecting its three PE/MASM
fcontext sources.

Windows process launch validates inherited-handle-list sizing, passes only
stdin and the merged diagnostic pipe, quotes a resolved executable separately
from its mutable command line, uses a UTF-16 response file before the process
limit, and distinguishes launch, pipe-read, wait, and exit-status failures.
Windows-only execution forces a greater-than-command-line-limit argument set
through dependency and real compilation in a path containing spaces, then
requires cold/warm success and response-file cleanup. DLL loading now resolves
the absolute image and searches its directory plus safe system defaults,
independent of the caller's current directory. Suspended SystemC threads are
drained through bounded repeated stop resumes before callbacks or images can
be destroyed; the sample thread deliberately yields three times to prove that
ordering on every fiber-enabled host.

The exact eight-worker Debug build succeeded; plug-in, compiler, application,
diagnostics catalog, and source budget passed 5/5 in 56.47 seconds, with the
application at 53.84 and compiler at 2.43 seconds. The process, compiler-plan,
thread-callback, dynamic-loader, compiler-test, and sample-plug-in sources
remain at 510, 1,333, 1,730, 185, 1,181, and 348 lines. Task 9 is current; no
sanitizer, Release, full regression, commit, push, or CI inspection ran at this
task boundary.

Task 9 adds a dedicated 231-line `fsim.systemc.matrix` that compiles a copied
real plug-in through the public source compiler, proves cold/warm publication,
loads factories and roots, suspends a thread where the fiber backend is
available, edits the still-loaded image's source into a distinct artifact,
repairs a corrupt DLL/shared object, races three concurrent rebuild callers,
proves old/new image residency and unload, and requires exact compile-failure
diagnostics. It runs in every configured Linux and Windows Debug/Release job.

The merged application host now exposes the existing integration/scheduling
coverage as `fsim.application.systemc_matrix`, labelled for SystemC, compiler,
cache, lifecycle, threads, LLVM, debug, and portability. This retains one
physical test executable while giving Windows MSVC and MSVC/clang-cl LLVM O0,
O2, debug, interpreter, edit, trace, and thread coverage an independently
selectable process. Nonmerged builds retain the original core invocation. The
named application matrix passed in 44.67 seconds; the compact matrix passed in
0.70 seconds; diagnostics catalog, source budget, and the newly shortened core
application passed 4/4 in 10.06 seconds with core at 9.15 seconds. The new
matrix and selector remain at 231 and 15 lines, and `tests/CMakeLists.txt`
remains at 1,765 lines. Task 10 is current; no sanitizer, Release, full
regression, commit, push, or CI inspection ran at this task boundary.

Task 10 closes the compiler/cache/portability batch. The release authority,
SystemC subset, and diagnostics reference now describe canonical compiler and
dependency fingerprints, transactional artifact publication, transactional
registration, retained executable ownership, specialization provenance, exact
failure containment, and the Windows compiler/process/DLL/fiber contract. The
diagnostics catalog covers all 1,622 production codes, and all 402 authored
C/C++ sources pass the 2,000-line gate. The LLVM-disabled ASan/UBSan suite
passed 77/77 in 437.42 seconds with leak detection disabled only because the
local sandbox denies LeakSanitizer's ptrace operation; strict ASan string
checks and halt-on-error UBSan remained enabled. The named SystemC matrix,
containers, and scoped locals took 131.90, 212.10, and 0.51 seconds.

The exact LLVM 22.1.8 warnings-as-errors Debug suite passed 80/80 in 230.94
seconds, with the named SystemC matrix at 45.81 seconds, containers at 111.06,
and scoped locals at 0.83. Release passed 80/80 in 206.43 seconds, with the
named SystemC matrix at 43.36 seconds, containers at 92.83, and scoped locals
at 0.83. Batch 125 closes as one accumulated checkpoint and has no GitHub
Actions inspection because it is not a tenth-batch boundary.

### One-hundred-twenty-sixth feature batch — Typed HIR and DesignIR boundaries — In progress

The current ten implementation tasks are:

1. **In progress.** Audit every VHDL and SystemVerilog parse-tree dependency in
   analysis, elaboration, specialization, execution, diagnostics, cache, and
   debugger paths; define the exact typed-HIR and elaborated-DesignIR boundary
   gaps, identity requirements, and migration order.
2. **Pending.** Introduce shared stable semantic identities, owning source-file
   and expansion provenance, exact source spans, type/value references, and
   deterministic traversal contracts without retaining parser-owned storage.
3. **Pending.** Complete typed VHDL HIR for design units, declarations, scopes,
   names, overload sets, subtypes, constraints, aliases, attributes, generics,
   ports, components, packages, configurations, and generated declarations.
4. **Pending.** Complete typed VHDL HIR for expressions, aggregates, sequential
   and concurrent statements, call associations, waits, assertions, files,
   protected/access operations, and waveform transactions.
5. **Pending.** Complete typed SystemVerilog HIR for compilation units, modules,
   packages, interfaces, declarations, scopes, nets/variables, parameters,
   types, ports/modports, callables, classes permitted by v1, and generates.
6. **Pending.** Complete typed SystemVerilog HIR for expressions, selections,
   assignment patterns, statements, processes, timing/event controls, forks,
   assertions, system tasks, strings, files, and containers.
7. **Pending.** Complete elaborated DesignIR for hierarchy, specializations,
   objects, drivers, ports/exports, callables, processes, conversions,
   sensitivities, transactions, and mixed-language/SystemC boundaries using
   stable semantic identities only.
8. **Pending.** Migrate lowering, interpreter, LLVM, cache/provenance,
   diagnostics, debugger, VCD, and API consumers to the explicit boundaries;
   prove no downstream consumer depends on parse-tree addresses or lifetimes.
9. **Pending.** Add a combined VHDL/SystemVerilog/mixed/SystemC boundary matrix
   covering positive and negative legality, stable identities, source and
   macro provenance, cold/warm/edit cache behavior, interpreter/O0/O2/debug
   agreement, serialization-order independence, and Windows portability.
10. **Pending.** Update matrix/architecture/diagnostics/docs, pass sanitizer,
    source/catalog and full Debug/Release gates, then create and push the single
    Batch 126 checkpoint. This is not a mandatory CI-inspection boundary.

Batch status is **in progress** with Task 1 current. Keep this exact ten-task
list current in both the official plan and resume handoff. Tasks 1 through 9
use one accumulated dirty worktree with focused eight-worker Debug builds and
tests; Task 10 owns the sanitizer, full regressions, documentation, commit, and
push gate. GitHub builds use parallelism four, and Batch 126 does not require a
non-documentation CI inspection.

## Forward language-closure feature batches

The following sequence is the authoritative planning baseline for closing the
remaining v1 language rows. Each batch remains bounded by its checked-in
positive, negative, elaboration, interpreter, LLVM O0/O2, cache/provenance,
debug/trace, and portability evidence. A permissive parse does not complete a
feature. If implementation evidence requires a batch to split or reorder, this
section and the resume handoff must be amended explicitly before proceeding.

| Batch | Planned feature scope |
|---|---|
| 99 | Complete bounded SystemVerilog `unique`, `unique0`, and `priority` case qualifiers and deterministic warning reports. |
| 100 | Add bounded `case matches`, pattern matching, and tagged-pattern diagnostics; complete the mandatory non-documentation CI inspection. |
| 101 | Complete SystemVerilog expression sizing and conversions, short-circuit and side-effect behavior, dynamic part-selects, and streaming concatenation. |
| 102 | Complete procedural lvalues, chained selections, dynamic targets, timed compound assignments, expression-form increments/decrements, and procedural force/release. |
| 103 | Complete remaining function/task lifetimes, formals, results, defaults, references, unpacked values, local declarations, and generated subprograms. |
| 104 | Complete remaining `always` and procedural-control forms, noncanonical loops, dynamic repeat/forever behavior, and general event controls. |
| 105 | Add `fork`/`join` variants, process completion/control, event races, and the complete NBA/delta ordering matrix. |
| 106 | Complete parameterized and net delays, gate arrays, remaining nondeferred primitives, and continuous-assignment semantics. |
| 107 | Add interfaces, modports, interface ports, package exports, and complete package/interface visibility. |
| 108 | Close preprocessor/directive semantics plus genvar-dependent constants, generated declarations, and specialization legality. |
| 109 | Complete nested structs/unions/enums, unpacked aggregate members, multidimensional arrays, assignment patterns, casts, and nominal legality. |
| 110 | Complete general strings, files, containers, and memories; audit every SystemVerilog v1 row; run full gates and the mandatory CI inspection. |
| 111 | Complete VHDL library analysis order, packages and bodies, contexts, and configuration binding. |
| 112 | Complete VHDL name/overload resolution, visibility, constant evaluation, legality, and resolution functions. |
| 113 | Complete VHDL generics, components/direct instantiation, expression/aggregate/`open` port actuals, defaults, and specialization. |
| 114 | Complete guarded blocks, generated types/subprograms/declarations, local declarative regions, and remaining generate choices. |
| 115 | Complete synthesizable sequential/concurrent statements, matching selections, case ranges, scopes, and dynamic selections. |
| 116 | Complete multidimensional/composite arrays, array aggregates, null ranges, slicing, ports, and callable boundaries. |
| 117 | Complete nested records, enumerations, qualified expressions, aggregate choice forms, attributes, and composite operations. |
| 118 | Complete access, protected, and physical types with their legality, storage, scheduling, and debugger metadata. |
| 119 | Complete nested waits, general assertions/reports, files/TextIO, physical time, and inertial/transport/reject transactions. |
| 120 | Review and bundle Apache-2.0 IEEE logic/numeric/bit/fixed/floating packages; audit every VHDL v1 row; run full gates and the mandatory CI inspection. |
| 121 | Complete mixed-language ordinal vector, Boolean, integer, signedness, width, and state-domain conversions. |
| 122 | Complete cross-language generic/parameter transfer, driver ownership, wired resolution, delay, NBA/update-phase, and failure matrices. |
| 123 | Complete SystemC named hierarchy, ports, exports, standard interfaces, and the custom metadata permitted by the v1 subset. |
| 124 | Complete SystemC static/dynamic sensitivity, events and cancellation, channel updates, lifecycle, and thread interactions. |
| 125 | Complete SystemC compiler/cache fingerprints, plug-in lifecycle/error coverage, and Windows LLVM/thread evidence. |
| 126 | Finish explicit typed VHDL HIR, SystemVerilog HIR, and elaborated DesignIR boundaries with stable identities and complete source/debug metadata. |
| 127 | Perform a language-wide positive/negative legality sweep and eliminate every silently accepted, discarded, or parser-only required construct. |
| 128 | Add license-reviewed conformance cases and close interpreter/O0/O2/cache/debug/VCD differential evidence. |
| 129 | Close Linux and Windows Debug/Release language portability and repair all remaining platform-specific failures. |
| 130 | Perform the final language-matrix audit, full release regression, mandatory CI inspection, and reclassify every completed v1 row. |

Batches 100, 110, 120, and 130 are mandatory non-documentation GitHub CI
inspection boundaries. Local builds use at least eight workers; GitHub Actions
builds use parallelism four. Documentation-only Actions runs do not
require monitoring. The explicitly deferred rows in the feature matrix remain
outside this sequence unless the v1 contract is deliberately amended.

## v1 release condition

fsim v1 may be declared only when:

- every required VHDL, Verilog/SV, SystemC, mixed-language, debugger, API, and
  runtime row has positive, negative, elaboration, and runtime evidence where
  applicable;
- interpreter, O2 `run`, and O0 `debug` semantics agree;
- Ubuntu x86-64/GCC and Windows x86-64/MSVC Debug and Release gates pass;
- LLVM 22.1.8, cache, SystemC plug-in, VCD, debugger, sanitizer, and fuzz gates
  are green;
- interactive and batch Tcl behavior passes on Windows and Linux, including
  script arguments, diagnostics, stop/resume, callbacks, and nonzero batch
  exit status after a command failure;
- the bundled Tcl pin is the latest stable release reviewed for the release
  candidate, its Tcl 9 embedding is free of legacy-size narrowing, and an
  installed Tcl 8.6 cannot silently select the legacy runtime;
- recursive VHDL/SystemVerilog/SystemC hierarchy passes elaboration and runtime
  matrices in every parent-to-child language direction, including mixed
  hierarchy rooted at SystemC;
- no promised syntax is silently ignored; and
- remaining unsupported features are explicitly documented as deferred rather
  than implied to be part of v1.
