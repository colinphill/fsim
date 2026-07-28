<!-- SPDX-License-Identifier: Apache-2.0 -->
# Feature-matrix test contract

This directory is reserved for atomic tests that satisfy
[the v1 feature matrix](../../docs/feature-matrix.md). The current vertical
slice keeps most evidence in subsystem test executables, listed below. This
README is a test-authoring contract; it is not itself feature evidence.

## Required evidence for each v1 construct

Every required feature ID must eventually have all four kinds of automated
evidence:

1. **Positive (P+)**: the smallest valid source is accepted and its typed
   representation is checked.
2. **Negative (P-)**: a nearby invalid or unsupported source is rejected with
   the expected stable diagnostic code and useful source span.
3. **Elaboration (E)**: hierarchy, specialization, types, drivers, and emitted
   SimIR are checked as applicable. Acceptance without semantic checking does
   not count.
4. **Runtime (R)**: observable semantics are compared through the SimIR
   interpreter and LLVM JIT, including final values, assertions, scheduler
   observations, and trace events.

A path counts as evidence only when it is built and registered as an automated
test. Example designs, parser implementation paths, and documentation do not
count.

## Intended atomic layout

New tests should use the stable feature ID from the matrix and keep one
construct or one tightly coupled rule per fixture:

```text
tests/feature_matrix/
  vhdl/
    V1-VH-03/
      positive.vhd
      negative.vhd
      elaborate.expected.json
      runtime.expected.json
  systemverilog/
    V1-SV-07/
      positive.sv
      negative.sv
      elaborate.expected.json
      runtime.expected.json
  mixed/
    ML-004/
      fsim.toml
      ...
  systemc/
    SC-007/
      fsim.toml
      ...
```

Fixture names may be more descriptive when a feature needs several atomic
cases, but each case must remain attributable to one matrix row. Golden output
must normalize temporary paths, platform path separators, VCD identifiers, and
other non-semantic variation.

## Existing vertical-slice evidence inventory

These suites are the evidence currently referenced by the matrix:

| Suite | Present evidence |
|---|---|
| [frontend/frontend_tests.cpp](../frontend/frontend_tests.cpp) | Minimal VHDL context/package/entity/architecture/process syntax including reusable context declarations/references, constant-only packages, selected package-constant names, retained visibility, rising/falling-edge sensitivity refinement, Boolean literals/operators, packed shifts/rotates, conditional and selected assignments, nested conditional and sequential-case trees, static for, runtime while, and unconditional loops with conditional/unconditional `exit`/`next`, bounded process variables, and timed/any-change/condition waits; Verilog-2005/SystemVerilog `$clog2` constant-call HIR plus SystemVerilog package/import/scoped-constant/packed-typedef/enum/struct/union/member-select/indexed-select/replication-concatenation, complemented reduction/XNOR, bounded built-in gate primitives, `final`, all procedural compound assignments and standalone prefix/postfix updates, and module/procedural-variable/nested-conditional/event-control/wildcard/always_comb/always_latch syntax plus canonical bounded procedural for loops, locally static repeat, runtime while/post-test do-while/timed forever, nested `break`/`continue`, procedural condition waits with attached statements, and inline/module-scope genvar loops with assignment, prefix/postfix, and compound updates; non-ANSI Verilog ports and repeat/while/forever/wait statements; single/multi-root include/macro/conditional preprocessing with default arguments, token paste/stringification, persistent timescale/default-net/cell/keyword/unconnected-drive context, and source ancestry; bounded delay scaling; and targeted context/package/directive/include/macro/duplicate/initializer/type/wait/implicit-process diagnostics |
| [elaboration/elaborator_test.cpp](../elaboration/elaborator_test.cpp) | Independently selected VHDL/SystemVerilog counters, bounded recursive VHDL context/package-constant visibility/transitive provenance and missing/ambiguous/cyclic visibility diagnostics, language-specific nested conditional and assertion truth semantics, SystemVerilog `$clog2` edge values/parameter dependencies/arity and unsupported-negative diagnostics, static/runtime/nested loop-control execution with innermost `break`/`continue` and `exit`/`next` targeting, unconditional VHDL and post-test SystemVerilog loops with guaranteed-first-iteration and trailing-condition `continue` evidence, dependency-driven VHDL/SV condition waits with false/X rechecks, attached statements, and constant-false terminal suspension, specialization-selected VHDL/SV conditional, iterative, and selection generate with recursive labeled/indexed/alternative mixed-binding paths, external-genvar ascending mixed hierarchy and postfix-decrement descending behavior, VHDL ascending/descending selection ranges with bound-evaluation and interval-overlap diagnostics, always-selected VHDL block and SV direct/named static generate bodies, implicit SV generate behavior, declaration-ordered and loop-index-dependent generated constant/parameter folding with evaluation/subtype failures, executable scoped local signals/assignments/processes, per-iteration loop-variable behavior, loop-variable construction actuals, descending ranges, multi-choice/default/no-match behavior, and evaluation/stall/overlap diagnostics, executable SV-parent and VHDL-parent mixed hierarchies, bidirectional bounded VHDL/SV construction-actual specialization plus ambiguous case-folded-name rejection, typed HDL→SystemC→HDL and SystemC-top→HDL hierarchy with shared signal IDs, construction actuals in both SystemC boundary directions including provider-built parameter-dependent ports and missing/unknown/subtype diagnostics, and missing/invalid-child-binding rejection, persistent named VHDL/SV local-register lowering and reads, source-level timed/dynamic wait lowering and wakeups, deterministic static/dynamic wildcard sensitivity inference and latch retention, scalar implicit-net initialization, cell specialization metadata, omitted-input pulls, dense instance-specific specialization ownership with source/language/library metadata, cross-library VHDL selection, and targeted unsupported-semantics/driver diagnostics |
| [runtime/runtime_tests.cpp](../runtime/runtime_tests.cpp) | Packed values including the checked allocation-free ≤64-bit `Logic4Word` path, exhaustive standard-logic resolution, wide arithmetic including 65-bit exponentiation, wide logical/arithmetic shifts and rotations with four-state end-element fill, oversized fill, and modulo-width wrap, wide exact X/Z case equality, selector-/choice-side `casez`/`casex` wildcard vectors, and one-sided `==?` vectors plus logical-equality unknown-dominance regression, phase ordering, delta limit, SimIR timed/static waits and operations including resumable non-design Pause and noninitializing static-process wakeup and exactly-once natural/design-stop final-process execution after ordinary pending work is discarded, source-point ordering and stable continuation requeueing, update coalescing, interpreter/external-executor scheduled writes, an edge-filtered dynamic `WaitOn` comparison covering duplicate normalization, a rejected opposite edge, and tick-2/tick-3 delta-1 wakeups, force/release, design-stop lifecycle, and VCD core |
| [project/project_config_test.cpp](../project/project_config_test.cpp) | Schema-1 manifest including deterministic `min`/`typ`/`max` delay-mode configuration and default, glob ordering, schema/unknown/invalid-value diagnostics, and JSON escaping |
| [app/expression_application_test.cpp](../app/expression_application_test.cpp) | Fast source-to-runtime interpreter/compiled differential for expression checkpoints, currently covering VHDL/Verilog-2005/SystemVerilog fixed-width exponentiation; SystemVerilog final-procedure lifecycle, procedural compound/standalone updates, wildcard equality/inequality and logical-equality unknown dominance, derived `$clog2` widths and cache-distinct specializations, `$signed`/`$unsigned` comparison/shift semantics, `$isunknown`, `$onehot`/`$onehot0`, `$countones`/`$countbits`, packed `$bits`, one-dimensional bound/size/increment queries with optional dimension `1`, and packed dimension counts; plus VHDL conditional and selected assignments, concurrent assertions, delta-scoped signal `'event`, signed `abs`, `sla`/`rol`/`ror`, negative-count reversal, oversized arithmetic fill, modulo-width rotation, and LLVM O0/O2 execution |
| [app/application_test.cpp](../app/application_test.cpp) | Check/build/cache/run, exact parsed-byte compilation-unit/transitive-include digests, included/shared-root and imported VHDL-context/package specialization provenance and invalidation, file/source-set/combined preprocessing policies with library ownership and shared default-net/cell/reset state, per-specialization provenance reuse/invalidation, VHDL-generic/package-constant and SystemVerilog-parameter specialized width/value interpreter-versus-LLVM execution, bidirectional bounded VHDL/SV construction-actual behavior, external-genvar/postfix-increment three-iteration and conditional/selection generated SV→VHDL hierarchies with stable selected/indexed/alternative bindings, descending VHDL case-generate range selection, executable conditional, implicit, direct/named-static SV and unguarded-block VHDL local signals/processes with declaration-ordered generated constants/parameters, scoped VCD names, and cold/warm native-cache identity, selective native-cache invalidation, and separated VHDL entity-interface provenance, bounded O0/O2 VHDL/SystemVerilog nested-conditional, static/runtime loop-control, unconditional VHDL-loop, and post-test SV-loop state/change/VCD differentials, persistent local-register behavior, O2 mixed SV/VHDL/SV interpreter-versus-hybrid differential execution with normalized serialized VCD equality, exact fully compiled O2 scheduled-write comparison at ticks 0 and 2, interpreter/compiled scheduling-overflow containment, static/dynamic/wildcard SV wakeups and periodic VHDL timed waits, supported-sibling compilation beside a 65-bit fallback process, eligible/fallback process and compiled-module counts, per-module native-cache cold/warm telemetry, forced-O0 hybrid debugger selection with source/conditional-signal breakpoints, all four step modes, packed local reads, live debug-VCD selection, real-SIGINT safe-point stop/resume/handler restoration, and exact interpreter transcript/lifecycle/callback/final-state equivalence, SystemC compile/ABI/typed-factory validation with real plug-in hierarchy in both top-level directions, typed facade `hdl_instance` construction of an SV child, facade `SC_METHOD` execution driven from SV and VHDL, Boost.Context `SC_THREAD` timed/delta/named-event suspension and compiled-SV-clocked `SC_CTHREAD` static waits, native child hierarchy, root-scoped lifecycle phases, direct-parent port chains, standard signal-interface exports with explicit hierarchy metadata, SystemC-method top initialization, `dont_initialize()` wakeup, scalar-edge counting, update-phase propagation, callback exception containment, exact time conversion, `` `timescale``-driven `auto` resolution/runtime scaling, scaled VCD, and value parsing |
| [app/tcl_application_test.cpp](../app/tcl_application_test.cpp) | Embedded Tcl repeatable-command and script batches, Tcl argument variables, fsim-owned standard streams, deterministic embedded exit codes, multiline interactive command completion, prompts/results, project/check/build dictionaries, signal hierarchy/value access, deposit/force/release semantics, time-limited and terminal runs, status dictionaries, and targeted evaluation/incomplete-input diagnostics |
| [app/display_application_test.cpp](../app/display_application_test.cpp) | Verilog/SystemVerilog literal, empty, constant numeric, and one-value `%b`/`%h`/`%o`/`%d`/`%c`/`%s` runtime `$display`/`$write` ordering, `%0` leading-zero suppression, formatted `$strobe` capture-before-mutation, literal-only `$monitor` initial publication, and all-severity source-aware VHDL reports including callback-before-failure termination; decoded language escapes, signed/unknown formatting, newline/no-newline and immediate/postponed policy, process/time/delta metadata, CLI routing, and interpreter versus LLVM O0/O2 equivalence |
| [app/random_application_test.cpp](../app/random_application_test.cpp) | Default, numeric, entropy-selected, and native-session seeds; stable independent per-process streams; bare/parenthesized `$random` and `$urandom`; one-/two-bound and reversed `$urandom_range`; X-bound behavior; signed formatting; and interpreter versus LLVM O0/O2 equivalence |
| [app/assertion_application_test.cpp](../app/assertion_application_test.cpp) | Standalone SystemVerilog severity tasks; implicit immediate-assertion errors; selected simple and lexical-block pass/failure actions with locals; nonfatal continuation; fatal single publication; source/severity metadata; CLI rendering; and interpreter versus LLVM O0/O2 equivalence |
| [app/line_directive_application_test.cpp](../app/line_directive_application_test.cpp) | SystemVerilog logical `` `line`` filename/line propagation into typed report/debug locations; distinct physical specialization/cache ownership; interpreter versus LLVM O0/O2 semantic and callback equivalence; native-object cold/warm reuse and logical-remap invalidation |
| [app/time_application_test.cpp](../app/time_application_test.cpp) | Compilation/module SystemVerilog `timeunit`/`timeprecision`, exact fractional/scientific HIR, explicit unit suffixes, precision-first half-up rounding, `auto` 1 ps resolution, timestamp callbacks, normalized VCD, coarse-resolution rejection, and interpreter versus LLVM O0/O2 plus cold/warm native-cache equivalence |
| [app/delay_mode_application_test.cpp](../app/delay_mode_application_test.cpp) | Manifest and CLI `min`/`typ`/`max` delay selection, typical default, selection-before-rounding timestamps, branch-specific `auto` resolution, interpreter versus LLVM O0/O2 semantic equivalence, normalized VCD, and cold/warm mode-distinct native-cache behavior |
| [api/api_test.cpp](../api/api_test.cpp) | C ABI lifecycle, generation-checked hierarchy/value handles, force/deposit/release, synchronous process-bearing safe-point callbacks and re-entry guards, ordered VHDL/SystemVerilog note/warning/error/failure callbacks without fatal duplication, run, statement/process/delta/time stepping, callback-issued asynchronous stop/resume, and terminal-stop precedence |
| [compiler/cache_test.cpp](../compiler/cache_test.cpp) | SHA-256 stability, process-aware locking/stale-lock recovery, atomic replacement, and cache store/load/erase |
| [compiler/jit_runtime_c_test.c](../compiler/jit_runtime_c_test.c) | C11 compilation, preserved resume-status values 0–5, appended wait/debug/permanent-wait statuses 6–9, fixed ABI offsets/size, and callback use of the append-only v1 scheduled-write, debug-control, signal-event, signal-last-value, elapsed-event-time, signal-transaction, language-output, report, and runtime-formatting fields |
| [compiler/llvm_jit_test.cpp](../compiler/llvm_jit_test.cpp) | LLVM O0/O2 CFG, logical/reduction/arithmetic-shift/rotate truth tables, exhaustive scalar four-state case-equality, `casez`, `casex`, and one-sided wildcard-equality kernels plus vector unknown-dominance cases, scheduled-write, `WaitOn`, `WaitSensitivity`, `WaitForever`, and source `DebugPoint` operations; grouped specialization modules with atomic validation, execution, warm reuse, and member invalidation; layout-preserving resume statuses; O0-unconditional/O2-size-gated debug suspension; suspension- and debug-safe cyclic CFGs with raw zero-time-cycle rejection; sensitivity-only 128/257-bit signals; wait-list/signal/width/static-and-dynamic-edge validation; runtime-tail validation; and persistent-cache tests for referenced values, wildcard comparison policy, scheduled kind/delay, wait kind/operand/referenced-width/edge, and source-point identity |
| [systemc/systemc_header_test.cpp](../systemc/systemc_header_test.cpp) | Standalone SystemC source-facade types, exact time checks, module macros, edge sensitivity, signals, and ports |
| [systemc/systemc_abi_c_test.c](../systemc/systemc_abi_c_test.c) | Strict C11 compilation plus append-only host/registrar layout and opaque-handle checks |
| [systemc/plugin_loader_test.cpp](../systemc/plugin_loader_test.cpp) | Native plug-in load/transactional factory registration, typed module/port/foreign-child construction, method/static-edge sensitivity and initialization metadata with opaque-handle connectivity, and throwing-initializer containment without partial registration |
| [systemc/plugin_compiler_test.cpp](../systemc/plugin_compiler_test.cpp) | Direct-argv compiler plans, transitive dependency keys, tracked linked inputs, non-cacheable dependency gaps, cache locks, and cold/warm compilation |

Several current executables cover many features at once. They are useful
architecture-gate tests, but they do not replace the atomic positive, negative,
elaboration, and runtime matrix required for v1.

## Differential runtime rule

Runtime evidence must start from the same elaborated DesignIR and deterministic
seed, then execute once with the interpreter and once with the LLVM 22.1.8
engine. The harness must compare:

- exit/stop status, final time, and delta observations;
- every debug-visible final value;
- assertion severity, location, and message;
- committed value-change callbacks; and
- normalized VCD events when tracing is relevant.

An LLVM adapter unit test by itself is not differential evidence. An
interpreter-only language test by itself is not differential evidence.

The current application differential test compares status, time, delta,
committed-change callbacks, final values, and normalized serialized VCD for a
bounded SystemVerilog hierarchy at O0 and O2 and for the vertical SV/VHDL/SV
hierarchy at O2. It also
compares a fully compiled O2 scheduled-write process with an update commit at
tick 0 and delayed commit at tick 2. The overflow companion runs through both
engines and checks that a scheduler exception raised in a generated callback
does not cross the C ABI, is rethrown to the application, poisons the
simulation, and publishes no write. A separate O2 positive-edge fixture
compiles both processes and exactly matches the initial trigger at tick 0, the
rising trigger at tick 1/delta 0, the observer update at tick 1/delta 1, and
the falling trigger at tick 2/delta 0. Those two processes share one
specialization module and therefore require exactly one cold cache miss/store
and one warm hit. The suite also asserts per-module cold/warm telemetry at O0
and O2. A VHDL assertion fixture separately requires identical process,
instruction, severity, source location, and message from the interpreter and
compiled O0/O2 engines. This remains bounded architecture evidence: complete
HDL event controls, generic/parameter specialization identity, exhaustive
assertion fixtures, O0 mixed-language/application scheduled-write/application
sensitivity coverage, exhaustive semantic and trace fixtures, and Windows
execution evidence remain outstanding.

A separate two-process fixture places a supported scalar process beside a
65-bit value-bearing process in one specialization. The hybrid application
compiles the scalar sibling into one module and leaves only the wide sibling
on the interpreter, with exact final-state and scheduler equivalence.

The runtime suite separately compares an interpreted dynamic `WaitOn` process
with an alternate executor at the shared kernel boundary. It verifies that a
duplicate signal operand is normalized, wakeups occur at ticks 1 and 2 in
delta 1, and final state and resume PCs agree. This is SimIR/kernel evidence,
not frontend evidence for general HDL event-control syntax.

## Matrix maintenance

When adding or changing a construct:

1. update the row in `docs/feature-matrix.md`;
2. add all missing evidence without broadening an existing fixture
   accidentally;
3. link the registered test paths from the row; and
4. change a status to `execute` only when the row's exact supported boundary is
   truthful.

Unsupported syntax must receive a targeted diagnostic. A recovery parser may
continue after that diagnostic, but it must not silently discard the construct.

## Release gate

The v1 release is blocked if any required row remains `v1 target`, any required
P+/P-/E/R cell is empty, any interpreter/JIT comparison differs, or any required
test fails on Ubuntu x86-64/GCC or Windows x86-64/MSVC in either Debug or
Release. Deferred rows are excluded unless they are explicitly promoted into
the v1 scope.
