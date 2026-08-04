<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final v1 inventory and provenance audit

This is the Batch 130 Task 7 reconciliation of diagnostics, authored source
size, repository licensing, reviewed third-party material, conformance, and
provenance. The inventories are release inputs, not substitutes for the
Task 10 sanitizer and full executable regressions.

| Review ID | Frozen release inventory | Required ownership |
|---|---|---|
| `B130-T7-DIAGNOSTICS` | 1,643 production diagnostic codes | Every emitted `FSIM-*` diagnostic is unique in and exactly matched by `docs/diagnostics.md`; stale catalog entries and undocumented emissions fail the gate |
| `B130-T7-SOURCES` | 445 authored C/C++ source and test files | Every file is at most 2,000 lines with an empty exception allowlist |
| `B130-T7-LICENSES` | 534 authored repository files | Every owned build, workflow, documentation, example, header, source, and test artifact carries an Apache-2.0 SPDX identifier; the root license is Apache-2.0 |
| `B130-T7-THIRD-PARTY` | One reviewed root with 31 files and 26 VHDL sources | IEEE P1076 packages retain the pinned commit, Apache-2.0 license/authorship/provenance files, exact loading inventory, and byte-for-byte SHA-256 checks |
| `B130-T7-CONFORMANCE` | 105 expectations in 28 fixtures owned by 27 CTests | IDs, source identities, expected outcomes, fixture/test ownership, all required evidence modes, and the exact sorted digest remain frozen |
| `B130-T7-PROVENANCE` | 10 reviewed source IDs and 6 explicit exclusions | Every external semantic reference is pinned and license-reviewed; no unrecorded third-party root or imported test text is permitted |

The authored-file count excludes only `LICENSE`, the non-source
`.gitattributes` policy file, four raw fuzz seeds, and the separately governed
31-file IEEE tree. Adding or removing an authored artifact, diagnostic,
conformance expectation, third-party byte, or provenance identity requires an
intentional update to this audit and its machine gate.

## Closure evidence

The eight-worker Debug build required no compilation. The diagnostic catalog,
source budget, IEEE package inventory, conformance audit and exact corpus,
new composed release inventory, SystemVerilog conformance application, and
scoped-local guard passed 8/8 in 5.70 seconds. The composed inventory itself
completed in 0.35 seconds; conformance runtime completed in 4.20 seconds and
scoped locals in 0.85 seconds.

Task 7 performs no sanitizer, Release, full regression, commit, push, or
GitHub Actions inspection. Those accumulated gates remain Task 10 work.

The post-release three-language example adds six SPDX-owned artifacts: its
tutorial, manifest, SystemVerilog top, SystemC factory, VHDL child, and scripted
debugger session. The machine inventory and this reviewed total advance from
500 to 506; the bounded C/C++ and conformance inventories are unchanged.

Corrective Batch 132 adds four SPDX-owned C++ artifacts: the support-library
export registry and three plug-in fixtures covering multi-translation-unit
exports plus a no-factory legacy entry point. The reviewed totals advance to
516 authored artifacts and 432 bounded C/C++ sources. Conformance expectation,
diagnostic, and third-party inventories remain unchanged.

v2 Batch 135 adds five SPDX-owned C++ artifacts: structurally separated
hierarchy-builder state and root-global lowering, plus focused elaboration,
application, and C API multiple-root tests. Four root-list/global-reference
diagnostics advance the reviewed totals to 1,635 diagnostics, 437 bounded
C/C++ sources, 521 authored artifacts, and 190 authored test/control files.
The conformance expectation, third-party, and provenance inventories remain
unchanged.

v2 Batch 136 adds 13 SPDX-owned artifacts: five tutorial/example files, two
public library headers, four library/application implementations, and two
focused tests. Eight new production diagnostic emissions and the two test
owners advance the reviewed totals to 1,643 diagnostics, 445 bounded C/C++
sources, 534 authored artifacts, and 192 authored test/control files. The
conformance expectation, third-party, and provenance inventories remain
unchanged.
