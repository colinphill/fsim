<!-- SPDX-License-Identifier: Apache-2.0 -->
# v1 conformance differential audit

This is the Batch 128 inventory and provenance policy for adding public-suite
conformance differentials. The [feature matrix](feature-matrix.md) remains the
release authority: an upstream test does not expand the v1 language profile,
and a passing parser result is not a substitute for positive, negative,
elaboration, and runtime evidence.

This review is an engineering import policy, not a legal opinion. It treats
Apache-2.0, ISC, MIT, BSD-3-Clause, the Tcl license, and Apache-2.0 with the
LLVM exception as permissive candidates. Every exact file considered for
import still requires file-level notice and provenance review. Task 1 imports
no upstream test text.

## Import and derivation rules

1. Record the exact upstream repository, commit, path, license file, and the
   bounded semantic expectation before adding a conformance case.
2. Prefer an original fsim fixture that expresses only the semantic
   expectation. Do not copy upstream identifiers, comments, formatting, or
   fixture structure merely because the repository license is permissive.
3. An exact or adapted import must retain its copyright and license notices,
   carry an fsim modification notice, and enter a checksum inventory alongside
   its pinned upstream commit. Repository-level license metadata alone is not
   sufficient when a subtree contains third-party material.
4. Test text under copyleft, proprietary, missing, ambiguous, or per-file
   unreviewed terms is outside this batch's direct-import policy. It may not be
   copied or translated. Public pass/fail reports may identify an area to
   investigate, but the resulting fixture must be independently authored from
   the supported language contract.
5. The IEEE language standards define behavior but are not a source-code
   corpus. No standard prose or examples are imported. The separately reviewed
   IEEE 1076-2019 package sources retain their existing byte-for-byte snapshot,
   license, authorship, and checksum inventory.

## Checked-in baseline

At the Task 1 baseline, `tests/` contains 184 tracked files: 179 authored C,
C++, header, and CMake test/control files with an Apache-2.0 SPDX notice, four
fuzz corpus seeds, and this inventory's pre-existing feature-matrix README.
The configured LLVM 22 Debug tree exposes 84 named CTests. The release matrix
contains 1,080 required rows, all executable with complete positive, negative,
elaboration, and runtime evidence.

| Checked-in area | Tracked files | Existing bounded strength | Conformance gap routed below |
|---|---:|---|---|
| `tests/frontend` | 19 | SystemVerilog and VHDL preprocessing, parsing, typed HIR, and negative diagnostics | Public-suite traceability and atomic boundary cases |
| `tests/elaboration` | 49 | Types, specialization, callables, hierarchy, bindings, containers, and language legality | Cross-suite expectation IDs and smaller positive/negative pairs |
| `tests/app` | 74 | Interpreter/LLVM applications, scheduling, cache, debug, callbacks, VCD, and language runtime | Public expectation differentials and one corpus-wide runner |
| `tests/runtime` | 9 | Scheduler, values, containers, files, timing, and execution services | Reference-boundary and failure-containment cases |
| `tests/compiler` | 12 | LLVM O0/O2, cache, ABI, logic, scheduling, strings, and display | ORC/debug/cache conformance expectations |
| `tests/api` | 2 | C lifecycle, handles, callbacks, stepping, force/deposit/release, and C header ABI | Foreign-client misuse and callback/re-entry matrix |
| `tests/systemc` | 9 | Facade, strict C ABI, plug-in/compiler/loader, and application matrix | Accellera kernel/facade expectation crosswalk |
| `tests/project`, `tests/semantic` | 2 | Manifest/configuration and semantic/DesignIR ownership | CLI/project/provenance failure matrix |
| `tests/fuzz` | 6 | Frontend fuzz target plus four stable seeds | Corpus provenance and deterministic failure expectations |
| `tests/feature_matrix` | 1 | Human-readable executable inventory | Machine-readable conformance expectation mapping |

The only checked-in third-party tree is
`third_party/ieee-1076-2019`: 26 unmodified VHDL files plus five license,
authorship, provenance, checksum, and loading-inventory files. Its independent
`fsim.ieee-package-inventory` gate remains authoritative for those bytes.

## Reviewed permissive sources

The commit identities below freeze this audit. They do not cause network
access during builds and do not vendor any upstream test.

| Source ID | Upstream snapshot and reviewed license | Bounded use in Batch 128 | Import decision |
|---|---|---|---|
| `SRC-FSIM` | fsim `ee17e3999addf3c4c61b985a037be6afd5a2fa81`; [Apache-2.0](../LICENSE) | Existing language/runtime/API/debug/tool corpus and all 1,081 required matrix rows | Original baseline; every authored test/control file retains an Apache-2.0 SPDX notice |
| `SRC-IEEE-P1076` | IEEE-P1076 Packages `16a012320947d378611cc7457f64ed76cb52bac4`; [Apache-2.0](../third_party/ieee-1076-2019/LICENSE) | Reviewed VHDL standard packages, package analysis order, overloads, values, and I/O | Already vendored byte-for-byte with authorship and SHA-256 inventory; no new Task 1 import |
| `SRC-SV-TESTS` | CHIPS Alliance sv-tests [`d0cee26833c9138d81e7502a6ca0bdb12ddc4502`](https://github.com/chipsalliance/sv-tests/commit/d0cee26833c9138d81e7502a6ca0bdb12ddc4502); [ISC](https://github.com/chipsalliance/sv-tests/blob/d0cee26833c9138d81e7502a6ca0bdb12ddc4502/LICENSE) | Chapter-indexed SystemVerilog accept/reject and selected simulation expectations within fsim's v1 subset | Primary Task 2/3 crosswalk; independently author compact fsim cases, with file-level review before any exact import |
| `SRC-SURELOG` | CHIPS Alliance Surelog [`014608ae5145ed38d28b7536cd330433ee50548a`](https://github.com/chipsalliance/Surelog/commit/014608ae5145ed38d28b7536cd330433ee50548a); [Apache-2.0](https://github.com/chipsalliance/Surelog/blob/014608ae5145ed38d28b7536cd330433ee50548a/LICENSE) plus NOTICE | Preprocessor, parser, elaboration, packages/interfaces, generate, and large-project failure shapes | Reference-only by default because the large test tree includes heterogeneous material; exact files require their own notice review |
| `SRC-SLANG` | slang [`99197ea10f8d7a476af46718eaacf1b5e93b5e74`](https://github.com/MikePopoloski/slang/commit/99197ea10f8d7a476af46718eaacf1b5e93b5e74); [MIT](https://github.com/MikePopoloski/slang/blob/99197ea10f8d7a476af46718eaacf1b5e93b5e74/LICENSE) | SystemVerilog parsing, typing, constant evaluation, elaboration, and diagnostic boundaries | Reference-only by default; independently authored fsim cases avoid coupling to an implementation-specific diagnostic suite |
| `SRC-UVVM` | UVVM [`90d56e93c542bf0d5e2ab9f791cc7395bd1aa896`](https://github.com/UVVM/UVVM/commit/90d56e93c542bf0d5e2ab9f791cc7395bd1aa896); [Apache-2.0](https://github.com/UVVM/UVVM/blob/90d56e93c542bf0d5e2ab9f791cc7395bd1aa896/LICENSE) | VHDL package, process, wait, time, transaction, reporting, hierarchy, and error-handling patterns | Behavioral reference; do not import the framework or verification components |
| `SRC-SYSTEMC` | Accellera SystemC [`adb09b1e3f998db9cce702fb8dce22a302c58001`](https://github.com/accellera-official/systemc/commit/adb09b1e3f998db9cce702fb8dce22a302c58001); [Apache-2.0](https://github.com/accellera-official/systemc/blob/adb09b1e3f998db9cce702fb8dce22a302c58001/LICENSE) plus NOTICE | `tests/systemc` compliance, kernel, datatype, communication, tracing, and lifecycle expectations bounded by fsim's facade | Primary Task 7 crosswalk; independently authored facade cases, with per-file review before exact import |
| `SRC-COCOTB` | cocotb [`3ea222d265c71584539040c24764f27f42616f5f`](https://github.com/cocotb/cocotb/commit/3ea222d265c71584539040c24764f27f42616f5f); [BSD-3-Clause](https://github.com/cocotb/cocotb/blob/3ea222d265c71584539040c24764f27f42616f5f/LICENSE) | Simulator-facing value, time, callback, handle-lifetime, and failure expectations that overlap fsim's C API | Conceptual Task 8 reference only; cocotb/VPI APIs are not fsim's public ABI |
| `SRC-LLVM` | LLVM Project [`5daadaa0264a350c3faa4dd0759fec7ef5fe8a75`](https://github.com/llvm/llvm-project/commit/5daadaa0264a350c3faa4dd0759fec7ef5fe8a75); [Apache-2.0 WITH LLVM-exception](https://github.com/llvm/llvm-project/blob/5daadaa0264a350c3faa4dd0759fec7ef5fe8a75/LICENSE.TXT) | ORC/JIT validation, object-cache identity, debug locations, ABI containment, and optimization parity | Behavioral reference for Task 8; no LLVM test text is imported |
| `SRC-TCL` | Tcl [`b2295663629e1b93173ef17b057ffecea7f1326e`](https://github.com/tcltk/tcl/commit/b2295663629e1b93173ef17b057ffecea7f1326e); [Tcl license](https://github.com/tcltk/tcl/blob/b2295663629e1b93173ef17b057ffecea7f1326e/license.terms) | Command parsing, result/error lifetime, and interpreter isolation for fsim's bounded Tcl adapter | Behavioral reference; independently authored adapter cases retain fsim semantics |

## Sources outside the direct-import policy

| Source ID | Review result | Batch 128 rule |
|---|---|---|
| `NO-GHDL` | GHDL repository and tests are GPL-2.0 at the reviewed root | Do not copy or translate test text; external tool results may be used only as a non-authoritative comparison |
| `NO-IVERILOG` | Icarus Verilog repository and tests are GPL-2.0 at the reviewed root | Do not copy or translate test text |
| `NO-NVC` | NVC repository and tests are GPL-3.0 at the reviewed root | Do not copy or translate test text |
| `NO-VUNIT` | VUnit is MPL-2.0 except separately identified redistributed projects | Do not import VUnit test text; review any separately sourced Apache-2.0 project from its own repository before use |
| `NO-VERILATOR` | The repository reports no single SPDX license and carries multiple licensing paths | No direct test import without a separate path/file-level review |
| `NO-PROPRIETARY` | Commercial simulator suites, paid standards text/examples, issue attachments without grants, and unlicensed snippets | Never import; create original cases from fsim's documented contract |

## Open conformance queues

These are coverage/provenance gaps, not claims that the corresponding v1
features are absent. Each queue must end with compact original fixtures,
stable bounded expectations, and source IDs in the conformance manifest.

| Queue ID | Task | Uncovered semantic families at the Task 1 baseline | Required closure |
|---|---:|---|---|
| `B128-T2` | 2 | **Closed.** SystemVerilog compilation-unit directive state, nested macro boundaries, declarations and port forms, signed/type/string parameters, package/import conflicts, interfaces/modports, generate identity, and exact negative ownership | Seventeen `CF-SV-*` expectation markers in the new frontend/elaboration suites cross-reference `SRC-SV-TESTS`, `SRC-SURELOG`, or `SRC-SLANG`; compact original P+/P-/E pairs include `macromodule` and checked end labels |
| `B128-T3` | 3 | **Closed.** SystemVerilog width/sign/four-state expressions, lvalues and selects, case/wildcards, callable copy semantics, process/event/delta ordering, assertions, strings/files, containers, memories, and failure paths | Eleven `CF-SV-*` expectation markers in the merged application case cross-reference `SRC-SV-TESTS`, `SRC-SURELOG`, or `SRC-SLANG`; one original vertical slice proves exact interpreter/O0/O2 runtime, cold/warm cache, timing, signal, file-byte, and memory-byte agreement |
| `B128-T4` | 4 | **Closed.** VHDL analysis order and contexts, declarations/types/subtypes, visibility and overloads, generics, packages, components/configurations, generate branches, and negative binding legality | Seventeen `CF-VHDL-*` expectation markers in the compact structural/analysis-order application cross-reference `SRC-IEEE-P1076` or `SRC-UVVM`; original P+/E cases prove structural elaboration and all eight exact analysis/binding order failures without importing standard prose |
| `B128-T5` | 5 | **Closed.** VHDL expression typing, aggregates/attributes, callable modes, sequential/concurrent scheduling, waits/reports, files/TextIO, access/protected/physical values, transactions, and reviewed packages | Sixteen `CF-VHDL-*` expectation markers cross-reference `SRC-IEEE-P1076` or `SRC-UVVM` across the existing compact merged applications; direct interpreter/O0/O2 cold/warm/edit runtime differentials retain exact values, timing, reports, file bytes, VCD, debugger, cache, and runtime-failure expectations |
| `B128-T6` | 6 | **Closed.** Bidirectional mixed construction, parameter/generic conversion, ownership, resolved drivers, timing, hierarchy/source identity, and unsupported-boundary failures | Nine `CF-MIX-*` expectation markers map the original bidirectional topology, conversion, resolution/timing, provenance, and exact missing-binding cases to `SRC-FSIM`; no external suite is treated as owning fsim's binding contract |
| `B128-T7` | 7 | **Closed.** SystemC facade and C ABI, named hierarchy, datatypes, method/thread lifecycle, delta/event/update ordering, signals/channels, plug-in compilation/cache, and exception containment | Sixteen `CF-SC-*` expectation markers cross-reference original facade, strict-C ABI, loader, compiler/cache, and scheduling-matrix cases to bounded `SRC-SYSTEMC` expectations, including exact unsupported and transactional failure paths |
| `B128-T8` | 8 | **Closed.** Common scheduler and SimIR validation, LLVM/cache parity, C handle/callback lifecycle, debugger stepping, VCD normalization, project/CLI/Tcl behavior, diagnostics/source provenance, and failure containment | Nineteen `CF-COMMON-*` expectation markers cross-reference the original common-contract cases to `SRC-COCOTB`, `SRC-LLVM`, `SRC-TCL`, or `SRC-FSIM` where semantics overlap, including exact ABI, diagnostic, Tcl, LLVM-validation, and containment failures |
| `B128-T9` | 9 | **Closed.** The exact machine-readable mapping joins all 105 conformance expectations in 28 fixtures to 27 owning CTests and their frontend/elaboration, interpreter/O0/O2, cold/warm/edit cache, debugger, callbacks, normalized VCD, source-map, and portable-path evidence | `fsim.v1-conformance-corpus` pins the sorted ID/source/expectation/file set by SHA-256 and rejects missing fixture/test mappings, duplicate or malformed IDs, unsupported source/license states, changed count/digest, invalid per-expectation evidence, or any absent required corpus mode |

Task 10 performs the final license/notice/checksum audit. Any exact upstream
file introduced in Tasks 2 through 9 must be present in that inventory; an
unrecorded import is a release-gate failure.

The exact corpus mapping is
[`tests/feature_matrix/v1_conformance_corpus.txt`](../tests/feature_matrix/v1_conformance_corpus.txt).
Markers remain adjacent to independently authored expectations in their owning
fixtures; the manifest maps each marker-bearing fixture to one registered CTest
and its evidence modes. The checked marker digest prevents silent ID, source,
expectation, or ownership changes while allowing the mapping to remain compact.
