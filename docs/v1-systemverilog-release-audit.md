<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final SystemVerilog v1 release audit

This is the Batch 130 Task 2 inspection record for all 671 `SV` rows and all
9 `V1-SV` release-contract rows in the
[feature matrix](feature-matrix.md). It narrows evidence claims; it does not
broaden the v1 subset.

## Review partitions

| Review ID | Surface | Required check |
|---|---|---|
| `B130-T2-SV-SYNTAX` | Lexing, preprocessing, declarations, expressions, statements, and source provenance | Every accepted form has an atomic or containing checked frontend owner and every rejected form has a test or cataloged diagnostic owner |
| `B130-T2-SV-SEMANTICS` | Types, constants, parameters, packages, interfaces, hierarchy, callables, and legality | Every row names checked elaboration or implementation evidence and rejects unsupported or malformed forms deterministically |
| `B130-T2-SV-EXECUTION` | Processes, scheduling, delays, events, failures, files, strings, containers, memories, and mixed boundaries | Every runtime cell names executable test evidence rather than implementation-only or prose-only support |
| `B130-T2-SV-NATIVE` | Interpreter, LLVM O0/O2, cache, debugger, VCD, runtime ABI, and source edits | Differential claims remain owned by their exact application/compiler/runtime tests and do not rely on parser acceptance |
| `B130-T2-SV-RELEASE` | Nine `V1-SV` rows | The complete language promise remains executable with positive, negative, elaboration, and runtime ownership |

The machine gate composes the final release audit, then parses all 680 rows
independently. It requires `execute`, rejects empty or em-dash evidence cells,
requires checked test-or-implementation ownership for P+, test/catalog/checked
rejection ownership for P-, implementation-or-test ownership for E, and
executable test ownership for R. Every row therefore has executable evidence
even where another cell identifies the exact checked implementation seam.
It also rejects stale “tests missing” and implementation-only runtime wording,
validates all five review IDs, and freezes the distinct runtime evidence-file
inventory after repair.

Task 2 performs no sanitizer, Release, full regression, commit, push, or
GitHub Actions inspection. Those accumulated gates remain Task 10 work.

The review is closed. Stale SV-011/SV-012 “tests missing” claims now name the
existing atomic frontend and runtime owners. SV-143 through SV-145 and SV-153
now name executable assignment, gate, and named-event delay differentials; the
gate fixture covers `min:typ:max` selection in all three modes and the event
fixture covers a typical-branch nonblocking notification. Classic callable
headers now have atomic missing, extra, duplicate, delimiter, and end-name
diagnostics. The strict audit covers all 680 rows and 48 distinct runtime
evidence files.
