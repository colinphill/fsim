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
| [frontend/frontend_tests.cpp](../frontend/frontend_tests.cpp) | Minimal VHDL entity/architecture/process syntax, SystemVerilog module syntax, non-ANSI Verilog ports, bounded `` `timescale`` context/scaling, and targeted directive/duplicate/initializer/type diagnostics |
| [elaboration/elaborator_test.cpp](../elaboration/elaborator_test.cpp) | Independently selected VHDL/SystemVerilog counters, executable SV-parent and VHDL-parent mixed hierarchies, cross-library VHDL selection, and targeted unsupported-semantics/driver diagnostics |
| [runtime/runtime_tests.cpp](../runtime/runtime_tests.cpp) | Packed values including the checked ≤64-bit `Logic4Word` path, exhaustive standard-logic resolution, phase ordering, delta limit, SimIR waits/operations/update coalescing, shared interpreter/external-executor kernel-boundary validation, force/release, design-stop lifecycle, and VCD core |
| [project/project_config_test.cpp](../project/project_config_test.cpp) | Schema-1 manifest, glob ordering, schema/unknown-key diagnostics, and JSON escaping |
| [app/application_test.cpp](../app/application_test.cpp) | Check/build/cache/run, bounded O0/O2 SystemVerilog and O2 mixed SV/VHDL/SV interpreter-versus-hybrid differential execution with eligible/fallback process counts, native-cache cold/warm telemetry, SystemC compile/ABI/factory validation, bounded CLI/debugger lifecycle, exact time conversion, `` `timescale``-driven `auto` resolution/runtime scaling, scaled VCD, and value parsing |
| [api/api_test.cpp](../api/api_test.cpp) | C ABI lifecycle, generation-checked hierarchy/value handles, force/deposit/release, synchronous callbacks and re-entry guards, and run |
| [compiler/cache_test.cpp](../compiler/cache_test.cpp) | SHA-256 stability, process-aware locking/stale-lock recovery, atomic replacement, and cache store/load/erase |
| [compiler/jit_runtime_c_test.c](../compiler/jit_runtime_c_test.c) | C11 compilation and layout use of the generated-code runtime function table |
| [compiler/llvm_jit_test.cpp](../compiler/llvm_jit_test.cpp) | LLVM O0/O2 CFG operations, caller-owned timed-wait/yield/stop resumption, interpreter differential behavior, persistent native-object cache reuse/recovery and referenced-signal-only width invalidation, initialized value storage, typed capability fallback, and distinct malformed-IR/ABI/layout/runtime rejection |
| [systemc/systemc_header_test.cpp](../systemc/systemc_header_test.cpp) | Standalone SystemC source-facade types, exact time checks, module macros, edge sensitivity, signals, and ports |
| [systemc/plugin_loader_test.cpp](../systemc/plugin_loader_test.cpp) | Native plug-in load/factory registration plus throwing-initializer containment without partial registration |
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
committed-change callbacks, and final values for a bounded SystemVerilog
hierarchy at O0 and O2 and for the vertical SV/VHDL/SV hierarchy at O2. It also
asserts cold native-cache misses/stores and warm hits at O0 and O2. This remains
bounded architecture evidence: assertion metadata, normalized VCD, O0
mixed-language coverage, exhaustive semantic fixtures, and Windows execution
evidence remain outstanding.

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
