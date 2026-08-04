<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final mixed-language and SystemC v1 release audit

This is the Batch 130 Task 4 inspection record, extended by v2 Batch 134, for
all 17 `ML` mixed-language
rows and all 28 `SC` SystemC rows in the
[feature matrix](feature-matrix.md). It narrows evidence claims and preserves
the existing strict-C facade and bounded mixed-language contracts.

## Review partitions

| Review ID | Surface | Required check |
|---|---|---|
| `B130-T4-MIXED-TYPES` | Ordinal/vector/Boolean/integer/state/signedness/width conversions and generic/parameter transfer | Every accepted conversion has exact type/range evidence and every lossy, unsupported, or ambiguous form fails deterministically |
| `B130-T4-MIXED-SCHEDULING` | Boundary drivers, wired resolution, delays, update/NBA phases, hierarchy, debugger, and VCD | Cross-language execution preserves ownership, time/delta order, value domains, source identity, and cache provenance |
| `B130-T4-SYSTEMC-FACADE` | Public C++ facade, strict-C ABI, datatypes, ports/exports/interfaces, object identity, and exceptions | Layout/calling convention, ownership, error containment, and public behavior remain test-owned |
| `B130-T4-SYSTEMC-LIFECYCLE` | Sensitivity, events, cancellation, channel updates, module callbacks, methods/threads, plug-in compiler/cache, and loader lifetime | Linux/Windows command and lifecycle claims remain attached to executable compiler, plug-in, scheduling, and application matrices |
| `B130-T4-NATIVE` | Interpreter, LLVM O0/O2, cold/warm/edit, thread, debugger/VCD, and failure boundaries | Every runtime cell names executable test evidence and no claim relies only on implementation prose |

The machine gate composes the final release audit, then parses all 45 rows
independently. It requires `execute`, rejects empty or em-dash evidence cells,
requires checked P+/P-/E owners, executable test ownership for R, all five
review IDs, and a bounded distinct runtime-owner inventory.

Task 4 performs no sanitizer, Release, full regression, commit, push, or
GitHub Actions inspection. Those accumulated gates remain Task 10 work.

The review is closed with no stale or unowned mixed-language or SystemC row.
The strict gate covers all 45 rows and 14 distinct runtime evidence files. The
complete mixed/SystemC-focused Debug slice passes all nine tests, including the
SystemC integration/scheduling matrix, strict portability contract, datatypes,
resolution, conversions, typed boundaries, and the exact mixed-conversion
matrix.
