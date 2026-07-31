<!-- SPDX-License-Identifier: Apache-2.0 -->
# Implementation plan and progress

## Purpose

This document records the implementation sequence for fsim v1 and the
evidence-backed progress of the current repository. It complements the
[feature matrix](feature-matrix.md), which tracks individual language and
runtime features.

Last updated: 2026-07-29.

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
  Actions is the explicit exception and uses two workers to avoid hosted-VM
  memory pressure; and
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
