<!-- SPDX-License-Identifier: Apache-2.0 -->
# SystemVerilog-2023 support

fsim v3 accepts SystemVerilog-2023 as a native, separately identified profile.
Select `23` or `2023` with `--standard` during workspace compilation:

```sh
fsim compile --lang systemverilog --standard 2023 --library work \
  rtl/design.sv tb/testbench.sv
fsim elaborate work.testbench
fsim simulate
```

The selected revision is retained through preprocessing, semantic units,
objects, designs, native caches, checkpoints, traces, coverage, VPI, DPI, and
TF/ACC routing. A 2023-only construct receives a profile diagnostic under an
older mode instead of silently enabling newer behavior.

## Supported families

The 2023 profile covers the revised lexical/preprocessor and design-unit
rules; integral, aggregate, string, event, handle, class, parameterized-class,
process, callable, assignment, streaming, expression, procedural, clocking,
and synchronization semantics; assertions, checkers, constraints, functional
coverage, hierarchy, programs, interfaces/modports, packages, generate,
primitives, timing, SDF, configurations, and protected envelopes.

Foreign integration includes the revised DPI C layer, the complete v3 VPI
model and routine set, assertion and coverage APIs, the data-read API, and
legacy IEEE TF/ACC interoperability. Callback execution retains scheduler
phase and stable-order identities under the interpreter, Debug engine, and
LLVM O0-O3.

## Artifacts and mixed designs

SystemVerilog-2023 compiles into managed workspace library objects and
snapshots. Their underlying formats reject v2 data without a migration path;
fsim chooses artifact filenames and replaces them after successful updates.
Relocated consumers retain source, standard, hierarchy, coverage, foreign
interface, and cache identities. Mixed VHDL/SystemVerilog/SystemC designs use
the same typed boundary and scheduling model.

## Deliberate boundaries

Vendor-only language switches, foreign extensions, encrypted key providers,
and undocumented compatibility behavior are not implied by selecting 2023.
Unsupported or resource-exceeding constructs fail explicitly. See
[Known v3.0 limitations](known-issues-v3.md), [Unified coverage](code-coverage.md),
and [IEEE TF and ACC PLI](legacy-pli.md).

The independently authored ownership ledger is
[`systemverilog_2023_inventory.tsv`](../tests/feature_matrix/systemverilog_2023_inventory.tsv).
