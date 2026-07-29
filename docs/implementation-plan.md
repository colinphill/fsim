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
  and for CI/workflow changes.

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
