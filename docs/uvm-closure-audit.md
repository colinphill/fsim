<!-- SPDX-License-Identifier: Apache-2.0 -->
# UVM supported-boundary closure audit

This Batch 162 Change 18 audit consolidates the implemented UVM 1.2 and UVM
2020-3.1 boundary. It proves the documented supported subset; it does not claim
exhaustive conformance outside that subset.

## Release and behavior matrix

The authoritative
[`uvm_release_closure.tsv`](../tests/feature_matrix/uvm_release_closure.tsv)
contains 21 unique contracts. It freezes the canonical versions, all nine
public compatibility switches, 53/56 governed and 27 project class identities,
17 active supported families, ten exact stages, memory and trace baselines,
artifact/cache provenance, and zero unresolved supported gaps. The application
parses the matrix for the selected release after every direct build or portable
design load. The static closure gate separately composes its source, diagnostic,
inventory, provenance, and evidence owners.

## Diagnostics and rollback ownership

The exact production catalog contains 2,071 unique codes, including 82
`FSIM-UVM-*` codes. The consolidated audit freezes representative and owned
coverage for:

- race/deadlock and callback containment (`FSIM-UVM-PHASE-008`,
  `FSIM-UVM-OBJ-004`);
- cancellation and lifecycle transitions (`FSIM-UVM-PHASE-007`,
  `FSIM-UVM-SEQ-010`, `FSIM-UVM-SEQ-011`, `FSIM-UVM-REG-007`);
- stale state (`FSIM-UVM-DEBUG-001`, `FSIM-UVM-FOREIGN-001`);
- cross-owner handles (`FSIM-UVM-COPY-002`, `FSIM-UVM-TLM1-001`,
  `FSIM-UVM-TLM2-001`, `FSIM-UVM-SEQ-002`);
- resource ceilings (`FSIM-UVM-PHASE-004`, `FSIM-UVM-STATE-002`,
  `FSIM-UVM-REG-014`); and
- transactional rollback (`FSIM-UVM-COPY-004`, `FSIM-UVM-REG-003`,
  `FSIM-UVM-REG-005`, `FSIM-UVM-REG-009`).

The conformance inventory assigns exact positive, negative, and execution
owners for every supported family. There is no xfail, expected-failure, waiver,
allowlist, or suppression entry.

## Source and complexity audit

The composed repository inventory covers 826 bounded C/C++ sources, all below
the 2,500-line hard limit with a 2,000-line refactor target; 937 authored files
carry Apache-2.0 SPDX ownership, and the authored test/control inventory is 313.

The refreshed code graph audits the eight new Batch 162 core runtime/application
sources. The highest cognitive complexity is 64 in the transactional deep
compare and copy paths, each with maximum loop depth 2. Transaction replay is
36 with loop depth 1, and the executable inventory parser is 33 with loop depth
2. The graph's linear-scan heuristic reports 4/4/2 flags for compare, replay,
and copy respectively; there is no recursion-in-loop or unguarded recursion.
Those paths are deliberately bounded by maximum depth, object, field, trace,
attribute/link, text, and output budgets. Copy snapshots destination objects and
releases every newly created object on failure; compare caps retained mismatch
text; replay caps records and per-transaction attributes/links. Focused resource
and rollback tests own each ceiling.

## Retained resource and provenance baseline

The exact 20-stage Change 17 matrix peaks at 4,299,292 KiB for UVM 1.2 and
4,861,336 KiB for UVM 2020-3.1 with zero swaps. Each release produces seven
28,345-byte traces with SHA-256
`62df6d6de11f33dcea64ae245060ebc70f578b4e54e326a911aa44ccb9797057`.
Each child retains its 6 GiB address-space ceiling, 1,200-second stage limit,
and 7,200-second serial-matrix limit.

Release plus exact source and artifact identity remains in object/design
metadata, loaded projects, checkpoints, replay, and native-cache keys. Cold
execution requires a miss/store, warm execution requires a hit without a miss,
and mismatched release/source/artifact state rejects before publication.

## Closure result

The complete supported inventory observed no mismatch. All 21 closure rows and
both 17-family conformance selections therefore report zero unresolved
supported gaps. Any future missing evidence, class, diagnostic, provenance
token, changed baseline, or nonzero gap fails a registered release audit.

Change 19 binds this technical record to the public README, UVM/version guide,
architecture, language-support and diagnostic contracts, feature/evidence
matrices, source/resource provenance, installed producer-independent tutorial,
release inventories/audits, and restart handoff. The registered documentation
gate requires all 15 synchronized owners and the installed Markdown surface.

Batch 169 Change 19 refreshes only the composed repository inventory beneath
this historical UVM evidence: 2,389 cataloged diagnostics, 974 bounded C/C++
sources, 1,139 SPDX-owned artifacts, and 363 authored test/control files. The
UVM release/source, memory, trace, class, family, and stage baselines above are
unchanged.
