<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final SystemVerilog v1 release audit

This is the accumulated release inspection record for all 854 `SV` rows and all
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

The machine gate composes the final release audit, then parses all 863 rows
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
diagnostics. V2 Batch 147 adds the ten class-foundation rows with focused
frontend, runtime, application, debugger, engine, and portable-artifact
owners. The strict audit covers all 716 rows; the runtime evidence-file count
is frozen by the release-candidate gate.
Subsequent closure through Batch 162 advances the strict audit to all 848 rows.
The ten Batch 155 coverage rows name exact parser/resolution, bin/cross,
percentage/report, scheduler/callback, debugger/trace, persistence, resource,
artifact, and interpreter/LLVM evidence; their runtime-owner count remains
frozen by the synchronized release-candidate gate.
The ten Batch 156 DPI-C rows name exact declaration/profile, scalar/composite/
open-array marshalling, scope/callback/task, manifest/loader, public C ABI,
lifetime, provenance, and real-symbol engine evidence.
The ten Batch 157 VPI rows advance the strict audit to all 799 rows and name
the versioned public ABI/loader, hierarchy, recursive types, values, time,
callbacks, control, system callables, I/O, checkpoint/remap/invalidation, and
independent repeated/relocated C/C++ image evidence.
The Batch 159-162 UVM rows advance the strict audit to all 848 rows and name
governed source, simulation-owned runtime, exact engine/artifact/cache,
sequence/register, object-policy/synchronization/test-runner, compatibility,
platform, conformance-inventory, and closure-audit owners.

Batch 164 closes the separate Verilog-2005 clause and literal-width ledgers:
34 supported clause rows and 12 preserved width paths have zero active gaps.
The retained 23-witness closure matrix covers 17 direct, interpreter, LLVM,
cache, debug/VCD, artifact, relocation/replay, checkpoint, multiple-root, and
mixed-language stages. Verilog literal width is no longer a deferred language
feature; only explicit host representation and governed resource boundaries
remain.

## Batch 165 SystemVerilog-2017 closure

The governed SystemVerilog inventory contains 30 supported clause/integration
rows, zero active rows, and five explicit later-owned deferrals. Its separate
literal-width inventory contains 21 preserved parser, semantic, specialization,
runtime, LLVM, debugger, trace, public, mixed-language, artifact, and cache
paths with zero active width obligations. Physical addressability, storage,
work, trace, scalar-format, and ABI representation ceilings remain resource
contracts; they are not SystemVerilog legality limits.

The 51-row release-closure matrix maps those 30 supported rows and 21 preserved
width paths to 153 positive, negative, and execution witness cells. Exactly 14
registered tests cover 17 direct/interpreter/LLVM O0/O2/cache/debug/VCD/
artifact/relocation/replay/checkpoint/multiple-root/UVM/mixed/public stages.
The retained serial matrix passes 14/14 in 165.34 seconds beneath a 6-GiB
process address-space, 1,000-delta, 64-trace-signal, 1,200-second per-witness,
and 7,200-second matrix evidence profile. There is no expected-failure, waiver,
allowlist, or suppression route.

The exact SHA-256 identities are:

- clause/integration inventory: `a2f19c56e715ea0f8198a672d96d08d0d9accd8eb7569f16bc6e542fc294ff40`;
- literal-width inventory: `f661b219e251e6369750ab406b19adf9c193cfb9570baaa0fdeab4f7984bad93`;
- release closure: `f4e8dcdfb60362544e6958449fa2a1e852cedcbba2ef6e929d5fb0a3aee1d124`.

The synchronized static baseline is 2,246 production diagnostics, 887 bounded
C/C++ sources, 1,035 SPDX-owned files, and 332 authored test/control files.
Installed documentation, diagnostic/source/license, platform/resource, public
API, artifact/cache, and release gates must compose without waivers before the
accumulated Batch 165 implementation is published.
