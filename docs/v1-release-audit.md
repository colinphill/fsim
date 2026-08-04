<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final v1 release audit

This is the Batch 130 release-candidate inventory and closure queue. The
[feature matrix](feature-matrix.md) remains the release authority.
Task 1 is a static local audit: it does not claim fresh hosted execution and
does not inspect GitHub Actions. The mandatory non-documentation CI inspection
remains the final Task 10 boundary.

## Matrix baseline

The current matrix contains exactly 1,081 required rows. Every row is classified
`execute`, has nonempty positive parse, negative diagnostic, elaboration, and
runtime evidence, and names evidence paths that exist in the checkout.

| Surface | Prefix | Required rows | Final review owner |
|---|---|---:|---:|
| SystemVerilog language | `SV` | 671 | Task 2 |
| SystemVerilog release contract | `V1-SV` | 9 | Task 2 |
| VHDL language | `VH` | 252 | Task 3 |
| VHDL release contract | `V1-VH` | 8 | Task 3 |
| Mixed-language behavior | `ML` | 17 | Task 4 plus v2 Batch 134 |
| SystemC behavior | `SC` | 28 | Task 4 |
| Common implementation behavior | `CM` | 86 | Tasks 5 through 8 |
| Common release contract | `V1-CM` | 10 | Tasks 5 through 9 |

The Task 1 baseline matrix digest is
`c6b806f2c640727519b90d3a932441ef25e75054382213a5c8b8be893618a2db`.
Changing any row requires an intentional audit update and the owning focused
gate; the digest is evidence of review, not a substitute for semantic tests.
Corrective Batch 131 re-reviewed the affected container rows after replacing
the accidental 4,096-element cap with a representation-derived owning-storage
budget; required-row counts and evidence ownership remain unchanged.
Corrective Batch 132 re-reviewed the SystemC export, proxy hierarchy,
construction-actual, named-object, and recursive mixed-language rows; row
counts remain unchanged.

## Existing release evidence

The composed local gates currently establish:

- 1,631 production diagnostics are cataloged;
- 437 authored C/C++ sources remain within the 2,000-line limit;
- all 1,081 required matrix rows are executable with no explicit evidence gap;
- 105 independently authored conformance expectations in 28 fixtures are
  owned by 27 CTests;
- 20 exact portability rows cover Debug/Release, interpreter/LLVM O0/O2,
  cold/warm/edit, API/ABI, plug-in, debugger/VCD, paths/newlines, and bounded
  resources; and
- the reviewed IEEE package inventory, conformance provenance, and
  Linux/Windows portability contracts remain machine checked.

These are inventory facts, not fresh release-candidate execution. Tasks 2
through 9 must re-read the owning rows and tests, repair any mismatch found,
and retain focused executable evidence. Task 10 owns the accumulated local
release gates and hosted proof.

## Exact final closure queue

| Queue ID | Task | Surface | Required closure |
|---|---:|---|---|
| `B130-T2-SV` | 2 | **Closed.** 671 `SV` plus 9 `V1-SV` rows | All 680 rows pass the strict owner audit with 48 distinct runtime evidence files; stale expression, triple-delay, and classic-callable evidence was repaired and its focused executable gate passes |
| `B130-T3-VHDL` | 3 | **Closed.** 252 `VH` plus 8 `V1-VH` rows | All 260 rows pass the strict owner audit with 40 distinct runtime evidence files; the complete VHDL-labeled Debug slice passes without a stale or unowned row |
| `B130-T4-MIXED-SYSTEMC` | 4 | **Closed.** 17 `ML` plus 28 `SC` rows | All 45 rows pass the strict owner audit with 14 distinct runtime evidence files; the complete mixed/SystemC focused slice passes across facade, compiler/cache, lifecycle, scheduling, datatypes, conversions, and typed boundaries |
| `B130-T5-DIFFERENTIAL` | 5 | **Closed.** Interpreter, LLVM, cache, debugger, VCD, scheduling, and failures | The composed audit freezes 94 runtime evidence files, 36 corpus CTests, all required differential modes, and exact overlapping claim counts; the 19 unique portability owners plus the new gate pass locally |
| `B130-T6-PUBLIC` | 6 | **Closed.** CLI, C/C++ API, ABI, Tcl, runtime, installed/package use | The static public audit and fresh Unicode-prefix staged install protect all five commands, five header groups, two libraries, exact API/ABI version 1, CLI status 0/1/2/3, native path/environment seams, and installed command behavior; all 11 focused owners pass locally |
| `B130-T7-INVENTORIES` | 7 | **Closed.** Diagnostics, source size, licenses, conformance, and provenance | The composed gate covers 1,635 diagnostics, 437 bounded sources, 521 SPDX-owned artifacts, the 31-file/26-VHDL IEEE snapshot, 105 conformance expectations in 28 fixtures owned by 27 CTests, and 10 reviewed plus 6 excluded provenance identities; v2 Batch 135 owns the current increase |
| `B130-T8-RESOURCES` | 8 | **Closed.** Linux/Windows Debug/Release build and test bounds | The composed static audit freezes 12 hosted configurations, six four-worker CI build steps, an eight-link local pool, compact Debug objects, 8 MiB stacks, bounded job/test timeouts, phase traces, 14 platform files, and 20 portability rows; all 11 focused owners pass locally |
| `B130-T9-RECLASSIFICATION` | 9 | **Closed.** Final feature matrix and release corpus | All 1,081 required rows are `execute`; all 4,324 P+/P-/E/R cells link to checked-in owners; the 323-path evidence identity (159 test, 148 production, 16 release/build paths), 94 runtime files, and 36 corpus CTests are exact and digest-pinned |

No queue is evidence of a confirmed defect. A queue closes only after its
complete bounded surface has been inspected and its focused local gate passes.
The Task 10 local gate passed ASan/UBSan 102/102, exact-LLVM Debug 105/105,
and exact-LLVM Release 105/105, together with source/catalog, documentation,
and staged-install checks. The single checkpoint and push follow this record.
Mandatory non-documentation CI inspection remains the only pending release
action. Stop before CI monitoring until that boundary is explicitly resumed.
