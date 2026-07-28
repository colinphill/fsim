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
| [frontend/frontend_tests.cpp](../frontend/frontend_tests.cpp) | Minimal VHDL entity/architecture/process syntax including Boolean literals/operators, nested conditional trees, bounded process variables, and timed/any-change waits; SystemVerilog module/procedural-variable/nested-conditional/event-control/wildcard/always_comb/always_latch syntax; non-ANSI Verilog ports; single/multi-root include/macro/conditional preprocessing with default arguments, token paste/stringification, persistent timescale/default-net/cell/keyword/unconnected-drive context, and source ancestry; bounded delay scaling; and targeted directive/include/macro/duplicate/initializer/type/wait/implicit-process diagnostics |
| [elaboration/elaborator_test.cpp](../elaboration/elaborator_test.cpp) | Independently selected VHDL/SystemVerilog counters, language-specific nested conditional and assertion truth semantics, specialization-selected VHDL/SV conditional, iterative, and selection generate with recursive labeled/indexed/alternative mixed-binding paths, executable scoped local signals/assignments/processes, per-iteration loop-variable behavior, loop-variable construction actuals, descending ranges, multi-choice/default/no-match behavior, and evaluation/stall/overlap diagnostics, executable SV-parent and VHDL-parent mixed hierarchies, bidirectional bounded VHDL/SV construction-actual specialization plus ambiguous case-folded-name rejection, typed HDL→SystemC→HDL and SystemC-top→HDL hierarchy with shared signal IDs, construction actuals in both SystemC boundary directions including provider-built parameter-dependent ports and missing/unknown/subtype diagnostics, and missing/invalid-child-binding rejection, persistent named VHDL/SV local-register lowering and reads, source-level timed/dynamic wait lowering and wakeups, deterministic static/dynamic wildcard sensitivity inference and latch retention, scalar implicit-net initialization, cell specialization metadata, omitted-input pulls, dense instance-specific specialization ownership with source/language/library metadata, cross-library VHDL selection, and targeted unsupported-semantics/driver diagnostics |
| [runtime/runtime_tests.cpp](../runtime/runtime_tests.cpp) | Packed values including the checked allocation-free ≤64-bit `Logic4Word` path, exhaustive standard-logic resolution, phase ordering, delta limit, SimIR timed/static waits and operations including noninitializing static-process wakeup, source-point ordering and stable continuation requeueing, update coalescing, interpreter/external-executor scheduled writes, an edge-filtered dynamic `WaitOn` comparison covering duplicate normalization, a rejected opposite edge, and tick-2/tick-3 delta-1 wakeups, force/release, design-stop lifecycle, and VCD core |
| [project/project_config_test.cpp](../project/project_config_test.cpp) | Schema-1 manifest, glob ordering, schema/unknown-key diagnostics, and JSON escaping |
| [app/application_test.cpp](../app/application_test.cpp) | Check/build/cache/run, exact parsed-byte compilation-unit/transitive-include digests, included/shared-root specialization provenance and invalidation, file/source-set/combined preprocessing policies with library ownership and shared default-net/cell/reset state, per-specialization provenance reuse/invalidation, VHDL-generic and SystemVerilog-parameter specialized width/value interpreter-versus-LLVM execution, bidirectional bounded VHDL/SV construction-actual behavior, conditional, three-iteration, and selection generated SV→VHDL hierarchies with stable selected/indexed/alternative bindings, executable VHDL/SV generated local signals and processes with scoped VCD names, and cold/warm native-cache identity, selective native-cache invalidation, and separated VHDL entity-interface provenance, bounded O0/O2 VHDL/SystemVerilog nested-conditional state/change/VCD differentials, persistent local-register behavior, O2 mixed SV/VHDL/SV interpreter-versus-hybrid differential execution with normalized serialized VCD equality, exact fully compiled O2 scheduled-write comparison at ticks 0 and 2, interpreter/compiled scheduling-overflow containment, static/dynamic/wildcard SV wakeups and periodic VHDL timed waits, supported-sibling compilation beside a 65-bit fallback process, eligible/fallback process and compiled-module counts, per-module native-cache cold/warm telemetry, forced-O0 hybrid debugger selection with source/conditional-signal breakpoints, all four step modes, packed local reads, live debug-VCD selection, real-SIGINT safe-point stop/resume/handler restoration, and exact interpreter transcript/lifecycle/callback/final-state equivalence, SystemC compile/ABI/typed-factory validation with real plug-in hierarchy in both top-level directions, typed facade `hdl_instance` construction of an SV child, facade `SC_METHOD` execution driven from SV and VHDL, Boost.Context `SC_THREAD` timed/delta/named-event suspension and compiled-SV-clocked `SC_CTHREAD` static waits, native child hierarchy, root-scoped lifecycle phases, direct-parent port chains, standard signal-interface exports with explicit hierarchy metadata, SystemC-method top initialization, `dont_initialize()` wakeup, scalar-edge counting, update-phase propagation, callback exception containment, exact time conversion, `` `timescale``-driven `auto` resolution/runtime scaling, scaled VCD, and value parsing |
| [api/api_test.cpp](../api/api_test.cpp) | C ABI lifecycle, generation-checked hierarchy/value handles, force/deposit/release, synchronous process-bearing safe-point callbacks and re-entry guards, run, statement/process/delta/time stepping, callback-issued asynchronous stop/resume, and terminal-stop precedence |
| [compiler/cache_test.cpp](../compiler/cache_test.cpp) | SHA-256 stability, process-aware locking/stale-lock recovery, atomic replacement, and cache store/load/erase |
| [compiler/jit_runtime_c_test.c](../compiler/jit_runtime_c_test.c) | C11 compilation, preserved resume-status values 0–5, appended wait/debug statuses 6–8, fixed ABI offsets/size, and callback use of the append-only v1 scheduled-write and debug-control runtime-table fields |
| [compiler/llvm_jit_test.cpp](../compiler/llvm_jit_test.cpp) | LLVM O0/O2 CFG, scheduled-write, `WaitOn`, `WaitSensitivity`, and source `DebugPoint` operations; grouped specialization modules with atomic validation, execution, warm reuse, and member invalidation; layout-preserving resume statuses; O0-unconditional/O2-size-gated debug suspension; suspension-safe loops; sensitivity-only 128/257-bit signals; wait-list/signal/width/static-and-dynamic-edge validation; runtime-tail validation; and persistent-cache tests for referenced values, scheduled kind/delay, wait kind/operand/referenced-width/edge, and source-point identity |
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
