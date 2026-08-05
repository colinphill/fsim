<!-- SPDX-License-Identifier: Apache-2.0 -->
# Final v1 inventory and provenance audit

This is the Batch 130 Task 7 reconciliation of diagnostics, authored source
size, repository licensing, reviewed third-party material, conformance, and
provenance. The inventories are release inputs, not substitutes for the
Task 10 sanitizer and full executable regressions.

| Review ID | Frozen release inventory | Required ownership |
|---|---|---|
| `B130-T7-DIAGNOSTICS` | 1,659 production diagnostic codes | Every emitted `FSIM-*` diagnostic is unique in and exactly matched by `docs/diagnostics.md`; stale catalog entries and undocumented emissions fail the gate |
| `B130-T7-SOURCES` | 463 authored C/C++ source and test files | Every file is at most 2,000 lines with an empty exception allowlist |
| `B130-T7-LICENSES` | 553 authored repository files | Every owned build, workflow, documentation, example, header, source, and test artifact carries an Apache-2.0 SPDX identifier; the root license is Apache-2.0 |
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

v2 Batch 137 adds 15 SPDX-owned artifacts: public object/design/phase headers,
artifact and state-codec implementations, focused metadata/application tests,
and the manifest-free tutorial. Ten artifact diagnostics and four additional
test/control owners advance the reviewed totals to 1,653 diagnostics, 459
bounded C/C++ sources, 549 authored artifacts, and 196 authored test/control
files. The C ABI, conformance expectation, third-party, and provenance
inventories remain unchanged.

v2 Batch 138 adds four SPDX-owned C++ artifacts: the public incremental
SystemC header, object and compiler implementations, and focused phase test.
Six incremental diagnostics advance the reviewed totals to 1,659 diagnostics,
463 bounded C/C++ sources, and 553 authored artifacts. The authored
test/control inventory advances to 197 through the new focused source. The C
ABI, conformance expectation, third-party, and provenance inventories remain
unchanged.

v2 Batch 139 adds five SPDX-owned C++ artifacts for the VHDL transaction,
driver, stable/quiet, and delayed signal substrate plus focused execution.
Eight diagnostics advance the totals to 1,667 diagnostics, 468 bounded C/C++
sources, 558 authored artifacts, and 198 authored test/control files.

v2 Batch 140 adds the clean-room VITAL intrinsic lowerer as one SPDX-owned
bounded C++ source. Nine cataloged VITAL diagnostics advance the reviewed
totals to 1,676 diagnostics, 469 bounded C/C++ sources, and 559 authored
artifacts. Existing focused integration and artifact-phase owners carry the
new positive and negative cases, so the test/control count remains 198. The
conformance expectation, reviewed IEEE third-party snapshot, and provenance
inventories remain unchanged because no external VITAL source is imported.

v2 Batch 141 adds three SPDX-owned bounded C++ sources and one internal header
for timing lowering, persistent timing execution, and state-table lowering.
Seven VITAL diagnostics advance the reviewed totals to 1,683 diagnostics, 473
bounded C/C++ sources, and 563 authored artifacts. Existing focused integration
owners carry the positive, negative, artifact, debugger, callback, and trace
matrix, so the test/control count remains 198. No external VITAL body or new
third-party provenance is imported.

v2 Batch 142 adds the path/wire lowerer, split compiled-callback adapter, and
focused VITAL delay application as three SPDX-owned bounded C++ sources. Five
delay diagnostics advance the reviewed totals to 1,688 diagnostics, 476
bounded C/C++ sources, 566 authored artifacts, and 199 authored test/control
files. The path/wire/pulse matrix adds no external VITAL body or third-party
provenance.

v2 Batch 143 adds the split VITAL memory lowerer, memory path runtime, and
focused path runtime matrix as three SPDX-owned bounded C++ sources. Four
memory-declaration diagnostics advance the reviewed totals to 1,692
diagnostics, 479 bounded C/C++ sources, 569 authored artifacts, and 200
authored test/control files. The complete memory/vendor compatibility matrix
uses only clean-room interfaces and implementations; no external VITAL body,
vendor source, third-party artifact, or provenance entry is imported.

v2 Batch 144 adds the split UDP parser, matcher, hierarchy lowerer, and focused
frontend/elaboration owners as five SPDX-owned bounded C++ sources. Forty-three
declaration, table, instance, resolver, and artifact diagnostics advance the
reviewed totals to 1,735 diagnostics, 484 bounded C/C++ sources, 574 authored
artifacts, and 202 authored test/control files. The UDP implementation and
fixtures are clean-room code; no external Verilog model, table, third-party
artifact, or provenance entry is imported.

v2 Batch 145 adds the split strength and switch parsers, public SimIR signal
model, and focused frontend/elaboration owners as five SPDX-owned bounded C++
sources. Twenty drive, charge, switch, and binding diagnostics advance the
reviewed totals to 1,755 diagnostics, 489 bounded C/C++ sources, 579 authored
artifacts, and 204 authored test/control files. The strength, topology, charge,
and decay implementation and fixtures are clean-room code; no external
Verilog model, proprietary strength table, third-party artifact, or provenance
entry is imported.

v2 Batch 146 adds the split specify parser, hierarchy/expression normalizers,
module-path runtime, focused frontend/elaboration/runtime/application owners,
and structural public/private header and hierarchy partitions as twelve
SPDX-owned bounded C++ sources. Twenty-two specify semantic and elaboration
diagnostics advance the reviewed totals to 1,777 diagnostics, 501 bounded C/C++
sources, 591 authored artifacts, and 208 authored test/control files. Module
paths, pulse controls, timing checks, fixtures, and documentation are
clean-room code; no external Verilog timing model, SDF file, third-party
artifact, or provenance entry is imported.

v2 Batch 147 adds class resolution, inheritance, specialization, heap,
container, method, and static-state owners plus focused frontend, runtime, and
application tests as 17 SPDX-owned bounded C++ sources. Fifty-two parser,
semantic, resolution, inheritance, specialization, and unsupported-member
diagnostics advance the reviewed totals to 1,829 diagnostics, 518 bounded
C/C++ sources, 608 authored artifacts, and 211 authored test/control files.
The class object model and fixtures are clean-room code; no UVM source,
constraint solver, external class library, third-party artifact, or provenance
entry is imported.

v2 Batch 148 adds class-expression resolution, source lowering, class
inspection, SimIR class/debug partitions, class JIT validation, and container-
type lowering as eight SPDX-owned bounded C++ sources. Twenty-one source class
resolution and lowering diagnostics advance the reviewed totals to 1,850
diagnostics, 526 bounded C/C++ sources, 616 authored artifacts, and 211
authored test/control files. The source-executable class fixture, service
boundary, artifact codec repair, and documentation are clean-room code; no UVM
source, constraint solver, external class library, third-party artifact, or
provenance entry is imported.

v2 Batch 149 adds semantic constraint projection/lowering, object and scope
randomization services, the finite-domain solver and expression evaluator,
portable randomization operation/state headers, and a focused solver owner as
eleven SPDX-owned bounded C++ sources. Twenty parser, class-resolution, and
elaboration diagnostics advance the reviewed totals to 1,870 diagnostics, 537
bounded C/C++ sources, 627 authored artifacts, and 212 authored test/control
files. The constraint solver, deterministic stream/cycle algorithms, source
fixtures, and documentation are clean-room code; no UVM source, external
solver/RNG library, third-party constraint corpus, or provenance entry is
imported.
