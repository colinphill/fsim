<!-- SPDX-License-Identifier: Apache-2.0 -->
# Implementation plan and progress

## Purpose

This document records the implementation sequence for fsim v1 and the
evidence-backed progress of the current repository. It complements the
[feature matrix](feature-matrix.md), which tracks individual language and
runtime features.

Last updated: 2026-07-27.

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
- parameter/generic-driven VHDL and SystemVerilog conditional instance-generate
  regions, including nested selections, stable labeled hierarchy paths, and
  explicit mixed-language bindings through the selected branch;
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
- appended `WaitOn`/`WaitSensitivity` resume statuses that preserve existing
  status values and the v1 result layout, with immutable SimIR retaining wait
  operands and edge rules;
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
- source-level VHDL `wait for`/`wait on` and SystemVerilog
  any-change/`posedge`/`negedge` procedural event controls lowered to
  resumable `WaitFor`/`WaitOn`, with interpreter/O2 timing and filtered-wakeup
  differentials;
- deterministic simple-expression dependency inference for `always @*` and
  time-zero `always_comb`/`always_latch`, plus dynamic `@*`, with a
  four-process interpreter/O2 delta
  differential;
- ordered exact Verilog/SystemVerilog `case`/`default` lowering with
  comma-separated choices, four-state `X`/`Z` matching, width diagnostics,
  and an interpreter/O2 differential;
- bounded SystemVerilog conditional expressions with scalar four-state
  conditions, equal-width alternatives, exact unknown-condition merging,
  and interpreter/O0/O2 evidence;
- vector-aware SystemVerilog logical negation and unsigned
  inequality/relational comparisons with exact four-state unknown
  propagation and interpreter/O0/O2 evidence;
- mixed-width SystemVerilog logical conjunction/disjunction with controlling
  known-value truth tables and interpreter/O0/O2 evidence;
- SystemVerilog unary reductions and mixed-width logical left/right shifts
  with four-state unknown and oversized-amount semantics tested across the
  interpreter and LLVM O0/O2 paths;
- equal-width signed/unsigned packed arithmetic and relational comparisons for
  VHDL and SystemVerilog, including distinct signed remainder/modulo,
  arbitrary-width interpreter algorithms, guarded LLVM lowering, and
  differential source tests;
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
- a stable catalog covering 410 unique current production diagnostic codes.

Current Linux validation:

| Gate | Result |
|---|---|
| GCC Debug, LLVM disabled | 13/13 tests pass |
| GCC Release, LLVM disabled | 13/13 tests pass |
| LLVM 22.1.8 Debug, warnings-as-errors | 14/14 tests pass |
| LLVM 22.1.8 Release, warnings-as-errors | 14/14 tests pass |
| Concurrent LLVM Debug and Release suites | Both pass; cache-test paths are isolated |
| GCC ASan/UBSan | 13/13 tests pass with no findings |
| Boost.Context 1.91.0 fibers | Checksum-verified fetch/configure/build; GCC Debug full suite 13/13, ASan/UBSan focused application, and LLVM 22 compiled-SV/SystemC application tests pass |
| Verilog/SV preprocessing/directives | GCC Debug and exact LLVM 22 atomic frontend/elaboration plus source-set/combined interpreter/compiled differentials pass, including shared default-net/cell/reset state and omitted-input pulls |
| Conditional instance-generate | GCC Debug and exact LLVM 22 frontend/elaboration/application tests pass for VHDL and SV selection, stable generated mixed-binding paths, interpreter/JIT equivalence, and cold/warm native cache |
| Installed C API | Strict C11 compile/link/run passes; only versioned `fsim_*` symbols are exported |
| Installed SystemC facade | Strict C++20 compile/run passes |
| Mixed-language CLI example | Check/build/run pass; simulation stops at tick 6 and emits VCD |

LeakSanitizer remains enabled in CI. It was disabled only for local execution
because the managed development environment runs under ptrace, which prevents
LeakSanitizer from starting.

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
- appended `WaitOn`/`WaitSensitivity` v1 resume statuses without changing the
  result structure or existing numeric values; immutable SimIR owns dynamic
  operands and dynamic/static edge rules;
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
  explicit VHDL/SystemVerilog bindings. Conditional instance-generate is
  implemented; loop/case generate and general generate bodies remain.

### 3. Near-full synthesizable frontend coverage — Pending

Early groundwork:

- handwritten tokenization and recursive-descent/precedence parsing;
- VHDL entity/architecture/port/signal/process nodes and represented
  library/use/context-reference clauses plus bounded generic specialization
  and conditional instance-generate;
- SV modules, common declarations, simple hierarchy, basic procedural and
  continuous statements, bounded integral parameter specialization,
  conditional instance-generate, and bounded `` `timescale`` handling;
- domain, width, signedness, duplicate-declaration, driver, and binding checks;
  and
- a checked-in feature matrix with positive, negative, elaboration, and
  runtime evidence columns.

Planned implementation sequence:

1. Separate the compact frontend representation into language-specific typed
   HIR and an explicit elaborated DesignIR.
2. Implement VHDL libraries, packages/bodies, contexts, configurations,
   generics, overload/type resolution, constant evaluation, and reviewed IEEE
   packages.
3. Complete the remaining Verilog/SV `` `line``/pragma semantics, parameters,
   packages, interfaces/modports, loop/case and general-body generates, and
   complete synthesizable types.
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
- bounded exact Verilog/SystemVerilog `case` statements with ordered,
  four-state alternatives and default fallback;
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
