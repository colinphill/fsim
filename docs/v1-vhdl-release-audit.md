<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final VHDL v1 release audit

This is the Batch 130 Task 3 inspection record for all 252 `VH` rows and all
8 `V1-VH` release-contract rows in the
[feature matrix](feature-matrix.md). It narrows evidence claims; it does not
broaden the VHDL-2008 v1 subset.

## Review partitions

| Review ID | Surface | Required check |
|---|---|---|
| `B130-T3-VHDL-SYNTAX` | Design units, contexts, declarations, names, expressions, statements, and source order | Every accepted form has a checked frontend/implementation owner and every rejection has a test, catalog, or checked rejection owner |
| `B130-T3-VHDL-SEMANTICS` | Types, constants, overloads, generics, subprograms, components, configurations, hierarchy, and legality | Every row names checked elaboration or implementation evidence with exact nominal/profile behavior |
| `B130-T3-VHDL-PACKAGES` | Reviewed IEEE packages, project packages/bodies, TextIO, contexts, and provenance | Package checksums, license/source provenance, analysis order, visibility, and callable dependencies remain owned |
| `B130-T3-VHDL-EXECUTION` | Processes, waits, assertions, files, scheduling, projected waveforms, composites, and mixed boundaries | Every runtime cell names executable test evidence rather than implementation-only or prose-only support |
| `B130-T3-VHDL-RELEASE` | Eight `V1-VH` rows | The complete VHDL promise remains executable with positive, negative, elaboration, and runtime ownership |

The machine gate composes the final release audit, then parses all 260 rows
independently. It requires `execute`, rejects empty or em-dash evidence cells,
requires a checked test-or-implementation P+ owner, a test/catalog/checked
rejection P- owner, an implementation-or-test E owner, and executable test
ownership for R. Every row therefore has executable evidence even where
another cell identifies the exact implementation seam.

Task 3 performs no sanitizer, Release, full regression, commit, push, or
GitHub Actions inspection. Those accumulated gates remain Task 10 work.

The review is closed with no stale or unowned VHDL matrix row. The strict gate
covers all 260 rows and 40 distinct runtime evidence files. The complete
VHDL-labeled Debug slice passes all 30 tests, including packages, analysis
order, overloads, configurations, components, waits, numeric/fixed/floating
packages, records, arrays, attributes, projected waveforms, and typed mixed
boundaries.
