<!-- SPDX-License-Identifier: Apache-2.0 -->
# September 28 simplification audit disposition

The user authorized [packet S1](simplification-implementation-plan.md) on
September 28, 2026. The reviewed source baseline is `6efa4c35` on `codex/v3`.
The input audit is `/tmp/simplification-audit.md`; its recommendations are
paraphrased here so this record remains useful without the temporary file.

Status: all selected implementation and candidate decisions are complete.
S1-S4 qualification and comparisons are recorded below; S5 final cumulative
Release passes 454/454 and fresh affected Debug/Tcl-off pass 21/21 each.
The final qualification, evidence identities and full-plan audit are recorded
in `build/simplification-packet-5/closure-receipt.json` and the closing section.
No performance improvement or hosted-CI result is claimed. The full
mixed-codec optimization loop remains paused; no feature stack was retired.

Execution uses a root architect, GPT-6-Sol/high for integration and executable
checks, and five GPT-6-Luna/max workers for disjoint code, test, infrastructure,
and public-surface review. Build/test execution is coordinated by Sol.

**Baseline and preservation**

`build/simplification-packet-1/baseline/` holds the starting Git index and
worktree patches, untracked inventory, source/tracking identities, configured
build caches, binary identities, and generated CTest JSON. The initial
configured test counts are Release 454, Debug 454, and Tcl-off 445; these are
registration counts, not new test results. The staged performance handoff,
`phase.fst`, `.codebase-memory/`, and `scripts/__pycache__/` are preserved.

**Accepted corrections to the audit**

| Recommendation or premise | Disposition | Invariant / evidence |
| --- | --- | --- |
| All proposed feature stacks have no installed C++ headers | Corrected | `CMakeLists.txt` installs `include/fsim/systemc/`, excluding only `scv.hpp`; `CheckInstalledPublicContract.cmake` checks kernel-backend headers. Reachability and installed/linkable surface require separate evidence. |
| L-08: remove subtype-cache byte accounting | Rejected as mechanical cleanup | Retain the 64-entry, 64-KiB per-entry, 256-KiB accounted-total and traversal-depth bounds. Sharing accounting may simplify implementation without weakening admission. |
| DCO-7: replace concurrent-template matching with a unit/statement key | Deferred performance experiment | Such a key may select a bucket; complete generic, invocation, profile, layout, aliasing and read-only compatibility remains mandatory. Storage needs an independent bound. |
| Native cache keys prove complete runtime identity | Corrected | Keys cover native-code inputs, not complete elaborated-design state such as all initial signal values. Artifact/design and behavioral evidence supplement key comparisons. |
| Old self percentages multiplied by current wall time predict savings | Corrected | Attribute costs to the exact profiled revision and workload. No cache-miss claim follows from the cited sampling evidence. |
| P96's 6.1 CPU seconds is value-name overhead | Corrected | It is approximately combined lowering, optimization and verification CPU; removable name overhead is unmeasured. |
| Current native schema is v170 | Corrected | The source baseline uses v178. |
| Unparsed profile output is unused | Not accepted | Preserve instrumentation used by the unfinished performance campaign unless an equivalent owner and removal decision are established. |
| F3/F4, test self-registration/sharding, broad test-host merging | Outside S1 | Durability, verifier policy, framework and execution-isolation changes require separate justification. |

**Twenty-change execution ledger**

| Change | Scope | Status / evidence |
| --- | --- | --- |
| 1 | Freeze baseline and comparison witnesses | Git, build configuration and CTest baseline captured; SV and VHDL managed-workspace witnesses each archive 16 artifact files with hashes. SV prints `ARTIFACT_WITNESS a=1 y=0`; VHDL generic instances read `result_width=21` through Tcl |
| 2 | Audit corrections and disposition | Corrections, retained surfaces, rejected pilot and separate repair scopes recorded |
| 3 | Installed SystemC/SCV surfaces | Installed headers, plugin libraries and SCV wrapper confirmed; host-backend external linkability remains unproven |
| 4 | Five-stack ownership/reachability | Independent bounded review complete; retain all five stacks and separate live source hooks from test-only host roots |
| 5 | HDL code-coverage reproduction and repair scope | Baseline interpreter/O0/O2 probe executes the branch but saves no database; separate repair required |
| 6 | SDF reproduction and repair scope | CLI annotation has no timing effect; missing and malformed files are accepted. Tcl reports zero applied paths; C API source retains request metadata only |
| 7 | Foreign loading and installed bridges | No product callers found for VPI/VHPI/DPI loaders in the bounded scan; installed TF/VPI bridge and ACC coherence obligations retained |
| 8 | L-01 capability-walker deletion | Nine dead methods and declarations removed; live expression walker retained. Root and independent Luna review approved; focused and closure checks passed |
| 9 | RT-01 unused assertion formatter | 233-line unused copy removed; live `_part2` implementation untouched. Independent review and focused/closure checks passed |
| 10 | RT-02 container-helper pilot | Shared internal inline header replaces duplicated helpers across three files; comparator hot path stays inline. Root and independent review approved; focused and closure checks passed |
| 11 | Assertion/diagnostic test helpers | Shared throwing assertion with call-site location and diagnostic lookup; existing elaboration assertions retain their abort behavior |
| 12 | File/temporary-directory helpers | Shared collision-checked RAII directory and checked file writes, including close failures, used by both SDF application pilots |
| 13 | Elaboration harness pilot | Two VHDL component suites share parsing, design append and specialization lookup; fixtures and assertions remain at call sites |
| 14 | Application differential harness pilot | SDF and VITAL suites share interpreter/compiled setup; timing, diagnostic and engine comparisons remain explicit |
| 15 | Shared inventory parser and first validator | Prototype rejected and reverted: row-only helper adds code and indirection |
| 16 | Second validator and negative witnesses | Coverage/SDF pair prototyped and reverted; broad engine deferred. Negative candidate qualification was not required for the rejected implementation |
| 17 | Redundant token-check disposition | Retain existing checks: no redundant substantive owner proven in the bounded pilot |
| 18 | Preserve and consolidate cache accounting | Three total-byte admission predicates share local `cache_bytes_fit`; all count, entry, byte and depth bounds unchanged. Independent review and focused/closure checks passed |
| 19 | Integrated review and next packet | Root and bounded independent reviews complete; all eight migrated test definitions and 56 HDL fixture bodies retained; deterministic artifact replay passed; proposed coverage packet below |
| 20 | Full local closure and handoff | Release 454/454, focused Debug 9/9 and Tcl-off 8/8 passed; deterministic replay passed; resume checkpoint and closure receipt own the handoff. Publication not authorized |

**Local qualification**

All builds used twelve workers and the existing configured trees. Release
`build/batch188-release-clang22-final` passed an all-target build and its
unfiltered 454-test suite. Affected builds and focused suites passed in
`build/batch188-debug-clang22-final` (9/9) and
`build/batch188k-tcl-off-clang22` (8/8). The final package/documentation gate
is recorded separately in the closure receipt after the final document writes.

Generated CTest metadata remains identical to the frozen baseline in all
three trees: 454, 454 and 445 registrations, respectively, with unchanged
commands, fixtures, labels, timeouts, working directories and resource locks.
The eight migrated test definitions and call inventories, all 56 raw HDL
fixture bodies, and diagnostic-code lists are unchanged. Shared helper
assertions still execute, and checked writes now detect close failures.

The production diff removes 2,934 net lines including the new internal helper
header. Test sources add thirteen net lines including their new shared helper;
the retained benefit is shared setup and stronger failure reporting.
`git diff --check` passed. The pre-existing staged patch is byte-identical
to the frozen baseline; no commit or push was made. The canonical code index
was refreshed at `2026-09-28T16:15:06Z` with 57,543 nodes and 370,512 edges.
No hosted Windows, sanitizer, or performance qualification was run.

**Feature-stack decisions**

| Stack | Current disposition | Boundary |
| --- | --- | --- |
| Native UVM service subsets | Retain; wiring/retirement decision deferred | Report, registry, config DB and object/component paths are live. Their existence does not prove the separately listed native sequence, register-model, phase or TLM services reachable. Preserve the distinction and verify named roots individually. |
| VPI/VHPI/DPI host service subsets | Retain; disconnected loader routes need separate integration scope | Installed ACC/TF headers and `fsim_tf` remain supported. ACC depends on VPI object identity; removing unused plugin loaders does not authorize removing that shared state or installed C link surfaces. |
| SDF pipeline | Retain; selected session integration completed in S3 | Baseline CLI/Tcl evidence below established the missing effect. S3 connects SpecifyPath/IOPATH and TimingCheck through the existing pipeline; broader low-level target families remain separate. |
| SystemC host backends and SCV subsets | Retain public headers; host implementation disposition deferred | Installed headers are a compile contract. The host `fsim_systemc` library is not itself installed/exported; installed plugin exports and Accellera runtime are separate link surfaces. Header exposure alone is not proof every host factory is externally linkable. |
| HDL code-coverage discovery/instrumentation | Retain; selected integration completed in S2 | Baseline coverage controls did not produce a database. S2 connects SV2017 module-procedural statements and conditional arms; functional coverage and other metrics remain distinct. |

No entire-stack deletion is part of S1, and the audit's retirement estimates
are not added to the implemented line reduction. This avoids simplifying
large dormant subsystems before their product direction is settled.

The install distinction is concrete: `CMakeLists.txt:1438` installs the
SystemC header directory; `cmake/FsimScv.cmake:783` separately installs the
excluded `scv.hpp` wrapper. `cmake/CheckInstalledPublicContract.cmake` owns
installed file identities and relocated consumers. Its SystemC consumer
uses upstream SystemC/TLM, not fsim host-backend factories. Its TF consumer
links installed `fsim::tf` and exercises TF/VPI bridge calls. A relocated
consumer calling installed kernel-backend declarations or ACC functions was
not found. These are bounded witness gaps, not removal authority.

The specific native UVM phase, sequencer, register, TLM and checkpoint roots
must be distinguished from live report/registry/config-db source hooks in
`src/app/application_simulation_impl_source.cpp`. Likewise, the loaders in
`src/runtime/{vpi,vhpi,dpi}_plugin.cpp` have test callers but no product entry
route found in this review. `cmake/FsimNativePlugin.cmake` builds the installed
TF/ACC/VPI library, and `src/runtime/acc_vpi_coherence.cpp` preserves shared
handle identity. All those contracts and tests remain.
`src/runtime/uvm_sequence_virtual.cpp` also calls `register_sequencer`
internally: a zero-caller claim for that method is incorrect, although that
edge alone does not establish a CLI/API/Tcl entry route for the native service.

The detailed bounded review and source anchors are archived in
`build/simplification-packet-1/fsim-simplification-surface-report.md`.
Graph parser gaps were checked against source; absence in the graph alone
was not treated as a reachability proof.

**Artifact comparison boundary**

A whole fresh `.fsim` directory is not byte-deterministic at this baseline.
`workspace::detail::new_revision` in `application_workspace_store_io.cpp`
uses a random salt and clock. Revision directory names, SQLite revision
references and `snapshot.index` therefore differ between equivalent runs.
Payload comparison must retain byte checks while identifying those files
by their logical role and validating catalog references separately.

The replay also exposed a pre-existing cache-identity issue:
`application_workspace_store_catalog.cpp:78` reads artifacts `ORDER BY id`.
`Selector` preserves that order, `load_workspace_objects` appends provenance
in the same order, and `application_run.cpp` hashes `checked.objects`
sequentially. Random IDs can reverse otherwise identical object inputs and
change the cache key and its embedded design/UVM references. Frozen-binary
repeats reproduced both orders; compiled HIR, design IR, hierarchy, runtime
and source/object payloads remained equal in these witnesses.

This is a separate cache reproducibility finding, not an accepted source
change in S1. Final deterministic qualification held the library input
fixed, including revision IDs and order, and ran the frozen and modified
binaries at the same executable/workspace paths with fresh snapshots and
caches. All 14 SV and 15 VHDL noncatalog payload files matched byte for byte,
including HIR, design/runtime state and applicable cache/native objects.
SQLite bytes also matched. Only the new randomized snapshot revision and
its index reference differed; the index was checked to point to the sole
matching payload tree. Command status, stdout and stderr matched, including
`ARTIFACT_WITNESS a=1 y=0` and VHDL `RESULT=21`.

Exact commands and comparisons are in
`artifact-replay/fixed-input-commands.json`, `controlled-comparison.json`
and `fixed-input-behavior-comparison.json` under the evidence directory.
The original failed raw comparisons remain archived. Whole-directory byte
equality is not claimed. A later repair should define a
semantic object order and test equivalent libraries with reversed revision
IDs before changing cache identity or artifact compatibility policy.

Additional requests for compiled execution at O0/O2 produced the same SV
output and VHDL final value 21, with identical traces between modes. The
requested fresh native-cache directories were not created for these tiny
fixtures, so these are compiled-engine request smoke checks, not proof that
native code generation occurred. LLVM qualification comes from the retained
LLVM, runtime and application CTests. See `native-witness-results.json` in
the artifact-replay evidence directory.

**Inventory pilot decision**

The prototype selected `CheckCodeCoverageInventory.cmake` and
`CheckSdfInventory.cmake`. Each retains distinct row/header policy, frozen
digest, domain/identity checks, and evidence owners. A shared row splitter and
field-count validator replaced two six-line blocks, but needed includes,
call sites, a new helper, and a diagnostic substitution convention. The
result added sixteen CMake lines and another indirection without sharing
the substantive obligations. Root rejected this prototype for net complexity.
The exact candidate and rejection evidence are retained outside the source
package; the two validators and their token checks remain intact.

A future shared inventory engine needs a broader, explicit obligation model
and a representative migration that actually removes duplicated validation.
S1 does not remove checks or expand the migration merely to justify the helper.

**Coverage integration finding**

The frozen baseline CLI was run in fresh managed workspaces with a tiny
SystemVerilog module that prints `COVERAGE_WITNESS taken=1 untaken=0`.
Compile and elaborate enable `--code-coverage`; simulation calls
`$coverage_save(SV_COV_STATEMENT, "run.fsimcov")`. Interpreter and LLVM
O0/O2 runs complete successfully but print `COVERAGE_SAVE status=0` and
produce no database. A disabled-coverage control produces the same absence.
Reporting the requested database fails with `FSIM-COV-047`. Source, commands,
and logs are under `build/simplification-packet-1/coverage-repro/`.
A second fixture uses `$time` to drive the branch at runtime and reproduces
the result in all three engines. The consolidated
`build/simplification-packet-1/feature-repro-receipt.json` records commands,
exit status, source hashes, logs, and the frozen executable identity.

The separate repair must connect source discovery to stable elaborated point
identity, attach the inventory, emit coverage-hit operations at the correct
execution sites, and preserve the metadata through managed artifacts. Reuse
the existing inventory, counter and database APIs. Acceptance needs nonempty
statement/branch inventory, exact executed and unexecuted counts for a
runtime-driven branch, interpreter/O0/O2 agreement, disabled behavior, and
cold/warm artifact replay. The reproduction is evidence of a missing product
path, not an expected passing empty-coverage contract.

**SDF integration finding**

The baseline CLI probe contains a `delay_buf` with a 5-ns specify path and
an SDF absolute IOPATH delay of 3 ns. Input changes at 10 and 30 ns produce
output changes at 15 and 35 ns both without annotation and with `--sdf`
under the interpreter and LLVM O2;
effective annotation would move them to 13 and 33 ns. Separate missing and
malformed SDF paths still allow elaboration to succeed and publish a ready
snapshot. Commands, fixtures, and logs are under
`build/simplification-packet-1/sdf-repro/`.

Tcl `fsim::sdf configure` accepts valid and nonexistent filenames, and reports
`paths 0 timing_checks 0 generation 0 effective 0`. Its report contains only
the requested input. The C API review follows `fsim_session_configure_sdf`
through the retained control application and summary/report getters in
`src/api/api.cpp`; this is source evidence, not an executed C API timing probe.

The separate repair must connect accepted control requests to file loading,
parsing, cell/endpoint resolution, annotation planning, application, and
artifact persistence using the existing stages. Start with a precisely
specified absolute IOPATH fixture, retain delay selection and timescale
semantics, and require invalid inputs to fail before snapshot publication.
CLI/Tcl/C API requests must report actual applied paths and affect simulation
consistently. Reloaded snapshots must preserve the applied timing. This
packet records the missing integration; it does not weaken SDF tests or
document a no-op as successful annotation.

**Packet S2: HDL coverage integration**

The user's continuous-execution goal authorizes proceeding with this separate
correctness packet after S1. Its baseline is frozen under
`build/simplification-packet-2/baseline/`, preserving the retained cleanup diff.
Start with SystemVerilog module procedural statements and runtime `if`/`else`
branches; define exclusions explicitly before promising broader language
coverage. Reuse existing point, counter and database representations. Preserve
functional covergroups and the disabled-coverage path. SDF integration remains
the following repair scope, using the timing acceptance criteria above.

The twenty-change sequence is:

1. Freeze the retained S1 revision, configuration and failing reproduction.
2. Map compile/elaborate controls to the existing coverage inventory owner.
3. Specify statement/branch point inclusion, exclusions and source identity.
4. Specify elaborated-instance identity and deterministic point ordering.
5. Connect discovery for the selected procedural statement subset.
6. Connect discovery for both outcomes of runtime conditional branches.
7. Attach discovered points to the elaborated design inventory.
8. Preserve that inventory through the existing managed artifact codec.
9. Emit statement-hit operations at their actual execution sites.
10. Emit branch-hit operations only for the outcome taken.
11. Verify interpreter counter updates, including zero-hit points.
12. Verify the same hit operations through LLVM O0 and O2.
13. Connect the enabled runtime inventory to coverage-save/database output.
14. Verify report totals and source/instance attribution from the saved file.
15. Verify disabled coverage introduces neither points nor hits.
16. Verify cold and warm artifact replay preserves identity and counts.
17. Add positive end-to-end acceptance for the archived runtime-branch fixture.
18. Add focused exclusion/error and distinct-instance witnesses; preserve the
    existing functional-coverage and database regressions.
19. Review exact counts across engines, resource bounds, diagnostics, schema
    compatibility, documentation and the complete diff.
20. Run full local closure and record the retained behavior and next SDF scope.

The first implementation gate is a concrete point-identity and subset design
checked against the existing APIs. If it requires incompatible artifact or
public behavior changes, settle that decision before broadening the packet.

S2 local qualification: implementation passes Release 454/454 (139.59 seconds),
Debug 14/14 (23.22 seconds), and Tcl-off 14/14 (26.82 seconds), using freshly
built affected binaries and twelve build workers. The final receipt captures
the passing 24/24 package/documentation gates, source/executable identities
and full persistent index refresh at `2026-09-28T17:49:31Z`. Root independently
verified cumulative patch, staged patch and all six new-file hashes against
`build/simplification-packet-2/final-diff-identity.json`; S2 is locally closed.
Discovery,
inventory attachment and hit binding reach the real managed-workspace path.
The archived runtime
fixture saves a database with status 1, statement totals 13/10/3
(total/covered/uncovered), and branch totals 2/1/1. Root independently checked
the saved report and HDL output under
`build/simplification-packet-2/repro/interpreter/`. This is intermediate
evidence. A subsequent repair preserves source lines in database records;
`report-3.json` contains nonzero lines. The two-instance witness under
`repro/instances/` gives each leaf four statement points (three covered) and
two branch points (one covered), with opposite arm lines hit. LLVM O0 and O2
reports match the interpreter per instance. The corresponding
`repro/native_o0/` and `repro/native_o2/` JIT profile logs record entries for both
covered leaf processes; because those entries can include cohort preparation,
the final execution proof uses the scheduler's per-process native counts in
each mode's final-binary `simulate-final-native-count.stderr`. Both covered leaves have two actual
native resumes in O0 and O2. Independent statement/branch exclusions and managed
include relocation pass. Four permanent workspace cases cover procedural
per-instance hits and O0/O2 replay, repeated loop updates, the callable/source-
control boundary, and disabled snapshots. A real legacy enabled snapshot from
S1 is rejected with `FSIM-COV-008` and a re-elaboration instruction.
These are correctness results, not timing results. The first full Release run
found manifest ordering errors and three coverage-related failures. Two tests
still assumed absent automatic instrumentation. The VPI fixture exposed parsed
origins on generated concurrent-assertion monitor statements; bounded triage
confirmed their duplicate source span. Discovery and emission now exclude those
monitors and use the same SystemVerilog language boundary. The VPI regression
first checks nine automatically discovered authored points, then retains its
separate manual-inventory contract. Manual observer/selector fixtures likewise
retain their exact assertions. Enabled control expectations are now precise:
27 statement points, five covered at the post-reset query, and successful save;
disabled controls retain zero/no-coverage expectations. No temporary diagnostic
prints remain. The existing
`make_code_coverage_point_identity` defines a source point from source identity,
language, construct and span. It deliberately excludes instance identity.
`make_code_coverage_inventory` orders points within each specialization and
assigns global counters, so discovery order is not a counter-number contract.

The selected architecture keeps the compiled-HIR pipeline AST-free. Enabled
elaboration supplies validated source identities and source-control information
to HIR discovery/lowering. Discovery retains supported zero-hit points even
when lowering does not emit an executable path. Lowering emits provisional
hits at statement entry and the chosen conditional arm; a final binding step
uses specialization, point identity and metric to select the inventory's
counter before design validation/publication. Existing interpreter/LLVM hit
operations, inventory validation and persistence remain the owners.

The implementation must preserve per-instance ownership across lowerer caches,
source exclusions, resource limits, disabled behavior and managed-source
relocation. Previously enabled but uninstrumented cache artifacts need an
explicit compatibility/invalidation decision. The initial implementation slice
does not redefine statement coverage as assignments alone: module procedural
executable kinds retain the existing discovery semantics, while callable and
synthetic regions and branch metrics beyond conditional outcomes require
explicit scope treatment. No broader metric-family support is inferred from
passing the selected statement/conditional witnesses. CTest names, commands and
all properties remain identical to the S2 baseline in all three configurations;
root's comparison is recorded in `root-ctest-surface-review.json` under the
packet evidence directory. Hosted CI and sanitizer qualification are not claimed.

Progress review after the first hour of continuous execution: S1 is locally
closed, and S2 has moved the previously disconnected HDL path to real saved
coverage with exact per-instance/native witnesses and four permanent workspace
cases. The remaining dependency at that review was S2 full local qualification
and its receipt. Keep source changes bounded to failures found there; do not add
more speculative coverage variants or begin SDF implementation concurrently.
The full objective remains open for S3, reproducibility and the selected
performance candidate dispositions.

**Completed packet S3: SDF request integration**

S2 is closed and the S3 baseline is frozen at
`build/simplification-packet-3/baseline/manifest.json`. The first acceptance
slice is a parsed absolute IOPATH
annotation changing the existing 5-ns specify path to 3 ns. Keep the existing
SDF IR, scope/cell/endpoint resolution, mapping validation, target planning,
precedence, scheduling and reannotation owners. Add a bounded request adapter;
do not introduce another timing representation. Initial annotation must finish
before AOT/native artifact preparation and workspace snapshot publication, and
live reannotation must use the existing safe-point commit and rollback policy.
Effective annotation identity must reach the derived artifacts through the
existing identity owner.

First S3 executable milestone: the unchanged S1 `top.sv`/`valid.sdf`
fixture now publishes a snapshot whose interpreter replay changes the two
transitions from 15/35 ns to 13/33 ns. Root independently checked
`build/simplification-packet-3/first-cli/valid-success.json`; missing and malformed
inputs fail before snapshot publication in `first-cli/results.json`. The latter
also retains the initial valid-file endpoint failure. That failure exposed a
resolver lookup using canonical signal names without retained HDL port aliases.
The connection now uses the existing signal-path map. This is intermediate
CLI timing evidence. The subsequent `first-cli/persisted-replay.json` retains
13/33-ns behavior with the SDF file hidden. `first-cli/provenance-replay.json`
then verifies successful replay after adding an indexed portable SDF payload.
Atomic candidate design application is implemented; selected-target filtering,
explicit precedence, effective control state, live surfaces and full
qualification remain unfinished. The integrated Release build and original
fixture pass again in `build/simplification-packet-3/integrated-cli/receipt.json`.
Five existing endpoint/mapping/reannotation/control/Tcl workspace checks pass;
fresh final affected-executable qualification remains required after lifecycle
fixes and new real-surface regressions.

S3 convergence review: the initially disconnected CLI now applies and persists
the real annotation. Source review and real C/Tcl regression work exposed that
`run(until)` completion does not leave the scheduler at a safe point. Preserve
that runtime rule: direct post-run configuration fails; only SDF configuration
may mutate timing from a verified scheduler safe-point callback. Other callback
mutation rules remain unchanged. The real C API and Tcl regressions now pass 2/2 with fresh executables
(`surface-build-5.log`, `surface-ctest-3.log`). An authored marker at 11 ns
provides the postponed safe point after the path producer has queued its
13-ns event; the initial 10-ns callback was too early in same-time propagation.
The tests commit exactly once, observe queued-event retention at 12.5/13.5 ns
and the new path at 31.5/32.5 ns, and preserve missing-file rollback and
outside-safe-point rejection. Earlier failing triage logs remain archived. The shortest remaining path
is to finish those witnesses, native/multi-file/persistence checks and full
local closure; additional SDF families and unrelated refactors remain excluded.

The permanent workspace `sdf-session` case passes after a fresh target build
(`cli-case-build.log`, `cli-case-run.log`). It checks plain 15/35 ns, annotated
1/3/5-ns min/typ/max delays, missing/malformed publication failure, indexed SDF
payloads and reload with the source file removed. Its two-instance subcase
selects only u0 while leaving u1 at 5 ns, applies a wildcard to both, and applies
ordered u0/u1/u0 files yielding final 4-ns/2-ns delays without a disjoint reset.
Selected/all target sets and delay modes produce distinct saved cache identities.
Root reviewed these assertions. Integrated-binary native proof now passes in
`integrated-cli/native-receipt.json`: compiled O0 and O2 preserve 13/33 ns and
the actual specify-path producer `top.u0.concurrent_0` (runtime ID 2) records
three native resumes in both modes. Root independently checked both stderr logs
and the binary SHA `35d5c741103fb18e1c11d13ffe5fdb484902cf0b467f0cddb1c0641805f56a02`.
Repeated saved-snapshot replay with the original SDF hidden also passes in
`integrated-cli/native-warm-receipt.json`. The requested native-cache directories
contain no native object files for this tiny fixture, so object-cache hits and
native object-byte stability are unproven; actual native execution and source-free
persisted timing are verified. Source is frozen for full local
qualification; refresh this proof only if a subsequent production fix changes
the final executable.

Live native-cache policy is explicit: module identities are created from the
initial built-project/cache/specialization keys and captured in materialization
jobs before simulation. Deferred executors capture those original jobs/futures.
`LlvmJit::add_process_module` consumes process entries, signal widths and value
kinds, not mutable specify-path/check tables. The topology-preserving session
commit therefore keeps immutable code identities while updating runtime timing
and effective control generation. Root and Sol verified these source boundaries;
retagging an existing JIT is unnecessary. Fresh filename relocation can still
change session control/native identity; only stored-snapshot relocation/replay,
not fresh cross-path key equality, is claimed for S3.

Release qualification: the full all-target build passes. The 454-case CTest run
passes 451 cases in 149.92 seconds; the only failures are three composed audit
expectations for the old 2,783-diagnostic total. The actual catalog now contains
2,786 after adding SESSION-001/002/003. Narrow expected-token/display updates in
`CheckUvmClosureAudit.cmake`, `CheckVerilogClosureAudit.cmake` and
`CheckVhdlPslClosureAudit.cmake` preserve every substantive audit step. Their
rerun passes 3/3 in 1.91 seconds. This qualifies 454 distinct Release cases via
the full run plus targeted repair, not one clean 454/454 invocation. Logs are
`full-release-build.log`, `full-release-ctest.log` and
`closure-audits-rerun.log`; root checked the failures, exact diff and rerun.
Fresh affected Debug qualification passes 8/8 in 0.99 seconds after the
833-step target build. Root also compared all generated CTest names, commands
and properties: Release 454, Debug 454 and Tcl-off 445 remain identical to the
frozen baseline, recorded in `root-ctest-surface-review.json`. Fresh Tcl-off
qualification passes 7/7 after its 824-step affected-target build
(`focused-tcl-off-build.log`, `focused-tcl-off-ctest.log`). All secondary targets
were selected from generated CTest executable commands and rebuilt with twelve
workers. Final docs/package gates, persistent reindex and identity receipt remain
before S3 closes and the prepared S4 baseline is frozen.

S3 closes locally with final docs/package 24/24 and canonical persistent graph
refresh at `2026-09-28T18:55:01Z` (57,627 nodes, 371,792 edges; existing 325
partial parses and one unusable include remain best-effort gaps). Nine relevant
S3 paths have no recorded issue. Root independently verified the closure receipt
SHA `a7411a76b40891ba0aa14d2179a354a02d4e87c232907313d048255f3d5a5b21`,
the cumulative and original staged patches, all 68 recorded source/document
hashes, and the final executable. Evidence is under
`build/simplification-packet-3/`, including `closure-receipt.json` and
`final-source-identities.json`. S4 is the next packet; full-plan completion has
not been claimed.

The twenty-change sequence is:

1. Freeze the S2-qualified state and S1 failing SDF reproduction.
2. Pin file-input semantics and resource limits for CLI, Tcl and C API adapters.
3. Map request roots, cell selectors, delay selection and file precedence.
4. Implement bounded file loading and parser diagnostics.
5. Connect scope binding and cell/endpoint resolution to the loaded design.
6. Connect mapping validation and target planning without partial publication.
7. Apply existing precedence and selected timing to the elaborated design.
8. Connect CLI elaboration before managed snapshot publication.
9. Preserve effective timing and annotation provenance in existing artifacts.
10. Verify reload uses persisted timing without requiring the original file.
11. Connect CLI simulation-time annotation before execution begins.
12. Connect Tcl requests through the existing reannotation safe point.
13. Connect C API requests through the same adapter and commit owner.
14. Populate summaries/reports from effective application, not requested inputs.
15. Add the 13/33-ns transition witness and unchanged 15/35-ns control.
16. Verify interpreter and actual native execution agree on annotation effect.
17. Verify missing and malformed inputs fail without replacing prior state;
    preserve existing resolver policies for unmatched and invalid selectors.
18. Verify repeated inputs, delay selection and live pending-event semantics
    using the retained focused SDF witnesses.
19. Review public contracts, persistence, limits, documentation and final diff.
20. Run full local closure and record the next cache-reproducibility packet.

S3 adapter decisions: preserve CLI's existing process-current-directory base
for `--sdf`. Direct Tcl and C API file inputs use that same base at configuration
time; freeze the resolved I/O path even for a pending request, while retaining
the caller's source identity for reports. Reading remains deferred until a
design is available. Project-load order must not change relative-path meaning.
Existing source-identity spelling remains part of session/native identity;
freshly annotating a relocated filename is not claimed to produce the same key.
Persisted snapshot replay retains stored identities without reading that path.
This preserves the existing identity owner and avoids a new portability layer.
Reuse the parser's 64-MiB per-file limit and bound aggregate file bodies per
request to 64 MiB. The existing control's 1-MiB source-byte budget counts
identity/root/pattern strings, not file bodies; validate input count before
allocation at session boundaries. These adapter decisions are implemented.
The full-plan audit identified two explicit evidence obligations beyond the
original S3 receipt. S4 follow-up tests now pass in focused Release: a pending
relative C API input survives a current-directory change before project load
and build, and single-file/aggregate inputs of 64 MiB plus one byte fail with
`FSIM-SDF-SESSION-001` without publishing a snapshot. The aggregate fixture uses
two individually sub-limit valid files. Carry these permanent tests through
S4's full and secondary qualification; the original S3 receipt alone does not
establish these boundary cases.

The installed C structure calls its input `source_identity`, while the Tcl
documentation and existing C API witness use filenames. The adapter contract
now explicitly documents that field as a file input at the session boundary;
low-level C++ control/IR APIs retain their existing identity semantics. Preserve
the installed ABI layout and the existing ability to register a pending request
before a design is loaded. Such a request has no effective timing yet; resolve
and validate its files when applying it to the loaded design, before execution
or publication. With a live design, configuration must apply atomically or
retain the prior effective state on failure. Keep the pending-request contract
test and add actual application/rollback witnesses. No broad SDF format support
is inferred solely from the absolute-IOPATH witness.

**Completed packet S4: managed-cache reproducibility**

S3 is closed. The S4 baseline is frozen in
`build/simplification-packet-4/baseline/manifest.json`, including the closed-S3
checkpoint docs and unchanged original staged patch. The fixed-payload SV
reproduction is archived in `build/simplification-packet-4/reversed-id-sv-baseline.json`.
Swapping only catalog revision IDs and their matching artifact directory names
preserves successful elaboration, simulation and linked HIR/runtime/design bytes,
but changes the UVM provenance payload and derived `.fobj` key. This is the
pre-repair baseline, not evidence that S4 is complete. The twenty-step sequence is:

1. Freeze the S3-qualified worktree, index, binaries and evidence.
2. Reproduce the fixed-payload/reversed-revision-ID cache divergence from S1.
3. Pin provider-selection, ambiguous-provider and catalog-group invariants.
4. Retain passive catalog rank on managed selections after selection completes.
5. Define a validated content key excluding revision IDs and directories.
6. Resolve trace-archive merge compatibility before changing merge order.
7. Prepare bounded selected-object descriptors without changing active-unit order.
8. Canonicalize only within explicit managed catalog groups.
9. Preserve direct/manual selection order where no managed group is supplied.
10. Feed canonical order through source, environment, provenance and bundle folds.
11. Add the derived-cache ordering-version identity without persisted schema churn.
12. Define exact-content tie handling without revision-ID fallback.
13. Add the fixed-payload/reversed-ID SV regression.
14. Check the VHDL environment/provenance counterpart with fixed inputs.
15. Retain ordered-library and duplicate-provider negative expectations.
16. Compare linked payloads, keys and cold/warm behavior under the new order.
17. Verify cache compatibility policy and disabled coverage/SDF paths remain valid.
18. Build and run affected Debug/Tcl-off checks with fresh executables.
19. Review final diff, documentation, manifests and preserved index identity.
20. Complete Release/metadata/package/index closure and record the performance packet.

Before S4, `WorkspaceObjectSelection` contained only path and active units.
`Selector::finish` knew each catalog rank but dropped it. Retain that rank as
passive internal metadata after all matching/closure work; do not infer catalog
ownership from revision paths or conflate catalogs with a metadata library name.
Direct callers without managed rank keep their established order.

Canonicalize the selected managed objects before linked-HIR construction and
design cache keying, after provider selection has resolved library scope. Keep
the existing library/catalog precedence groups and the unit order inside each
object. Sorting catalog candidates before selection could change which provider
is chosen and is outside this repair. A semantic ordering key must exclude
random revision IDs, revision directories and insertion order; derive it from
validated logical ownership, compilation configuration and content identities.
Review exact ties against payload equality rather than using revision IDs as a
fallback.

The compatibility decision is a versioned derived-cache identity for the new
canonical order. Existing library/object formats and revision records remain
readable. The decisive regression reverses only revision IDs in two copies of
the same fixed managed library, then compares selected semantic inputs, cache
keys, linked payloads, diagnostics and cold/warm behavior. An unchanged
ordered-library/duplicate-provider witness must separately prove that provider
precedence is preserved. The selector's source-order and environment-merging
semantics still require implementation-time review; the read-only seam audit
is preparation, not completed repair evidence.

S4 selection review confirms `Selector` intentionally retains all in-scope
providers; package and elaboration tests require ambiguous duplicates to remain
errors. Keep selection and catalog/library group order unchanged. The narrow
canonicalization seam is after selection and before per-object source,
VHDL-environment, UVM provenance and cache-key folds. Linked HIR already sorts
its semantic inputs and rejects duplicate definitions. A candidate key uses
validated language/profile, active-unit identity set, compilation digest,
compiled-HIR checksum and logical metadata content; verify that serialized
metadata contains no revision/path instability before using its digest. Preserve
active-unit vector order within each selected object.

S4 trace-merge review is resolved. Current managed producers write only
`Configured` trace archives, but valid imported archives can use other lifecycle
states. Restore normalizes every non-disabled lifecycle to `Configured`;
`publish_trace_control` derives runtime enablement solely from disabled versus
non-disabled. Root checked that trace attachment consumes selected objects,
output and effective compression rather than archived surface/phase/generation.
Retain existing profile/declaration/output-intent compatibility, additionally
reject mixed replay lifecycle classes, and choose a deterministic complete
snapshot/archive representative within a compatible same-class set. Keep accepted
requested-format/compression variants that resolve to the same effective profile;
full semantic-identity equality would reject valid provenance differences.
Existing ordered-library and ambiguous-provider tests remain unchanged.

The first S4 implementation slice passes the three focused Release cases.
Managed selections now retain optional catalog rank; the loader sorts only
contiguous managed groups by validated content and selected-unit identities.
Unranked callers retain their order, and empty selections remain ignored.
Ordering descriptors retain only selection pointers and digests; exact-key ties
reload at most two metadata records for equality checking instead of retaining
every serialized metadata body. Trace lifecycle checks and deterministic archive
selection are local to object merging; the shared profile-compatibility helper
keeps its prior contract. A derived-cache version label affects object-backed
design keys, including manual callers. Specialization keys receive the same
version for both source and object builds to preserve their equivalence. These
intentional one-time misses do not change persisted object/library schemas.

Permanent workspace tests cover real SQLite revision-ID/directory permutation,
manual ordering, catalog groups and trace representatives/lifecycle rejection.
The first fixed-input replay receipt,
`build/simplification-packet-4/reversed-id-post1-comparison.json`, records equal
SV 14/14 and VHDL 15/15 selected payloads, derived cache paths and behavior across
the two permutations. These initial focused results are supplemented by the
cold/warm and full-qualification evidence below.

Root and an independent Luna review found no blocker in the settled production
diff. Both managed constructors emit contiguous rank groups; unranked entries
stay in place, active-unit sequence is preserved, and metadata bytes are hashed
immediately rather than retained across a group. The permanent revision-swap
test now verifies cold native cache misses before the swap and warm hits after
it with matching signal behavior. Its first run failed because the test expected
`completed` after `$finish`; the corrected `stopped` expectation passes. Preserve
the initial triage log and final passing log as distinct evidence.
The final focused Release follow-up passes 5/5 in `focused-ctest-4.log`;
`native-cross-order-final.stdout` records exactly two cold misses and two warm
hits after revision permutation. The tests read the four-state signal as logic;
the interim scalar-accessor assertion failure is retained as separate triage.
The first full Release run passes 453/454. The mixed-conversion five-path
differential exposed an object-only specialization version tag: equivalent
source and object builds then had different keys. The tag is now applied
consistently to both specialization routes; the existing equality assertion
is retained. The targeted mixed-conversion/revision-order rerun passes 2/2,
and a fresh all-target Release build followed by the final full suite passes
454/454 in 134.06 seconds (`final-release-ctest.log`). Fresh Debug and Tcl-off
builds with twelve workers pass all sixteen affected cases each in 7.17 and
7.07 seconds respectively; `secondary-qualification.json` records the exact
names and executable targets. The runner's initial unsupported regex selected
zero tests and is retained as harness triage. The corrected runner requires
the exact count and uses `--no-tests=error`; only its sixteen-case runs count.
SDF relative-path and aggregate/per-file-limit follow-up witnesses pass across
all three configurations. Final fixed-payload replay also passes:
`reversed-id-final-comparison.json` records SV 14/14 and VHDL 15/15 selected
payloads, matching cache paths, logical catalog rows and behavior. The final
Release executable SHA-256 is
`7578bc9511bf289b557fc40279725bbd15a507a8e52cbb47a163102b784dfbb8`.
Raw catalog IDs intentionally differ; only the compared logical catalog rows
are normalized. Final docs/package checks pass 24/24. The canonical full
persistent graph is ready at `2026-09-28T19:40:10Z`, with 57,638 nodes and
372,164 edges; 325 partial parses and one unusable include remain disclosed.
Root verified the closure receipt, all 22 referenced evidence hashes, 77 live
source/document identities, cumulative patch, Release executable and original
staged-patch preservation. The closure receipt is
`build/simplification-packet-4/closure-receipt.json`, SHA-256
`bd24176c3c3529725889937fe2763e817a6bac2b75048a1d7ccfc0ad41025cf5`.
Independent reviews are archived as `root-closure-review.json`,
`root-source-identity-review.json` and `root-ctest-surface-review.json`.

**Prepared performance invariants after S4**

All four selected candidates remain present; this source review is not cost
attribution or acceptance evidence. T9 snapshot callers precede the existing
observer checks in `application_simulation_impl_core.cpp`, setup callbacks and
UVM callbacks. Any guard must treat class/static hooks independently and preserve
class-before-static notification order. `application_test_classes.cpp` retains
both-hook behavior evidence.

T9 preparation also found a hook-lifetime constraint. Public observer setters
replace callbacks without a run-state restriction. `invoke_class_method`
captures both before-states, invokes the method, then notifies class observers
before static observers. A class callback can therefore install a previously
absent static observer before static notification; skipping its before-snapshot
would report unchanged properties as new changes. Wrapped user callbacks may
also replace observers during an operation. A simple entry-time absent-hook
guard is not yet behavior-preserving; preserve this contract when deciding T9.
The existing multiple-roots class fixture provides a small absent-observer
workload, while the larger class fixture checks both notifications and their
time/delta/value sequence. These source findings are not cost measurements or
a final candidate disposition.

T3 reads `FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION` during direct-slot setup
and buffered flush in `application_executors.cpp`. A hoist must explicitly choose
executor-construction lifetime, recapture across sequential executors, and keep
forked executors' suppression-disabled rule. Existing direct-update and connected
remap tests do not themselves cover environment lifetime.

DCO-7 remains a concurrent-template vector scan. A bucket can narrow candidates
but must preserve all generic, invocation, profile, layout, read-only and alias
checks, insertion-order selection and ordinary-lowering fallback. There is no
independent entry/retained-byte budget on the current vector; a new retained
index needs explicit working-set-informed bounds. Existing phase counters expose
hits, misses, rejections and lowering CPU, while VHDL concurrent semantic tests
remain the baseline behavioral shield.

F2 would act where each LLVM process module creates its context. The
`FSIM_DUMP_LLVM_PROCESS` raw and optimized IR files currently retain useful local
names; preserve that diagnostic contract. Structural IR counts do not depend on
names. Native object keys are constructed before module generation, so generated
objects/cache compatibility still need explicit comparison. Fresh attribution
and the selected campaign qualification policy remain mandatory before any
candidate disposition; no speedup is inferred from these source anchors.

**Packet S5: selected performance dispositions and full-plan closure**

An independent requirement audit found phase 1 ownership decisions, phase 2's
selected coverage/SDF repairs and foreign-loader findings, phase 3 deletions,
and phase 4 accepted/rejected pilots accounted for by the S1-S3 ledgers and
receipts. Its bounded review found no additional selected requirement beyond
S4, the four performance dispositions, the SDF boundary witnesses above and
final closure. Installed host-backend linkability and a relocated ACC consumer
remain explicit witness limitations; they are not feature-retirement authority.
This preliminary audit does not replace final verification of the retained diff.

S4 local closure is verified; S5 began with baseline capture and a fresh
profile. It evaluated the four selected candidates;
it does not resume the broader under-15-second campaign. Candidate execution order
follows fresh absolute-cost evidence. The twenty obligations are:

1. Freeze the S4-qualified source, staged patch, binary and validation identities.
2. Rebuild the existing preferred ThinLTO host configuration from that source;
   record configuration and dependencies without changing project IPO defaults.
3. Profile the full retained mixed-codec workload with an explicit current binary,
   validating all saved input hashes, fingerprints and correctness lines.
4. Rank the four selected opportunities by current applicability and credible
   absolute end-to-end savings; distinguish inclusive costs from removable work.
5. Resolve material attribution gaps with bounded existing tools and a relevant
   class workload where necessary; keep instrumented runs separate from timing.
6. Record T9's independent observer guards and notification-order invariants.
7. Record T3's executor-construction environment lifetime and fork behavior.
8. Record DCO-7's complete matching/fallback contract and measured working set;
   choose bounds before adding any retained index.
9. Record F2's dump contract and generated-code/native-cache comparison policy.
10. Give T9 an individual evidence-backed retention or rejection disposition.
11. Give T3 an individual evidence-backed retention or rejection disposition.
12. Give DCO-7 an individual evidence-backed retention or rejection disposition.
13. Give F2 an individual evidence-backed retention or rejection disposition.
14. Remove rejected trial changes and temporary instrumentation; retain only
   justified production changes and meaningful regression coverage.
15. Verify interpreter/native behavior and applicable design, artifact and cache
   compatibility across retained changes.
16. Build and run affected Debug and Tcl-off checks using fresh executables.
17. Complete the final Release qualification for the retained cumulative source.
18. Verify CTest obligations, docs/package checks, graph coverage and preservation
   of the original staged handoff and unrelated worktree artifacts.
19. Audit every selected requirement across all six plan phases against its
   evidence, including explicit retained/rejected/deferred decisions.
20. Archive the final diff/identities/receipts and update authoritative checkpoints;
   mark the goal complete only when no selected deliverable remains unfinished.

Each implemented candidate gets focused correctness checks and the campaign's
single cold uninstrumented CPU-0/O2 screening observation with the same explicit
host configuration. Such observations do not establish a statistical speedup or
prove absence of regression. A rejected candidate needs a concrete evidence-backed
reason; merely lacking an implementation is not a disposition. No commit, push,
hosted CI monitoring, seven-pair qualification or Vivado comparison is included.

S5 baseline is frozen at `build/simplification-packet-5/baseline/manifest.json`
(SHA-256 `f322a57bee6e78a413ae3ea469bccd1083f3e650941fcc38ca6bfd354e67e765`).
The existing preferred ThinLTO tree was rebuilt from the retained S4 source at
twelve workers without changing its configuration. The frozen current binary
is `build/simplification-packet-5/current-thinlto-fsim`, SHA-256
`72d156a5049ea0e2a504afc4ed49c7baa33635b024b9fb1267eb6ffcb770063a`.

The fresh full mixed-codec profile and separate cold wall run both validate all
19 inputs, 75 fingerprints and 87 correctness lines at CPU 0 and LLVM O2. The
single uninstrumented baseline is 33.186 seconds: compile 1.343, elaborate
7.344, native setup/simulation 24.498; peak RSS is 794,856 KiB. Native objects
are absent before simulation and number 308 afterward. This is a screening
baseline, not a statistical result or a comparison against historical binaries.
Evidence is in `full-mixed-profile.log`, `full-mixed-wall.log` and the linked
campaign result directories beneath `build/simplification-packet-5/` references.

Initial attribution shows 308 LLVM modules with summed CPU costs of 0.694
seconds lowering, 4.802 optimization and 0.547 verification. These are stage
costs, not removable name overhead. Simulation has 2,252 samples, 510 unresolved
(22.65%), and zero reported lost samples; five resolved self samples name LLVM
value-name operations. The concurrent-template counters show 30 misses, 1,143
hits and 922 ineligible fallbacks, with 0.102 seconds of miss lowering CPU.
At most one retained template is appended per miss, so this run retains at most
30 entries. The sampled helper has 1.03 percent inclusive elaboration cost,
which includes work a bucket cannot remove. Candidate-specific attribution and
final dispositions follow below.

The evidence-backed candidate order was F2, then T3. F2 received an
isolated small trial: discard local LLVM value names during normal compilation,
retain useful raw/optimized IR dumps, and verify generated code/cache behavior
before one same-configuration cold full-workload observation. The five named
leaf samples support only a small opportunity; the 4.802-second optimization
stage is not an estimate of removable naming work.

| Candidate | Disposition | Evidence and preserved boundary |
| --- | --- | --- |
| DCO-7 | Reject added bucket index; retain current matching | Fresh working set is at most 30 templates and 35,190 candidate visits. The helper's 1.03% inclusive elaboration attribution includes lowering/replay that a bucket cannot remove. No supported benefit justifies added retained state, bounds and fallback complexity. Existing complete matching and order remain unchanged. |
| T9 | Reject simple absent-observer snapshot guard | Observer setters are mutable during an operation. Class notification precedes static notification, allowing a class callback to install a static observer after the before-snapshot. An empty snapshot would spuriously report unchanged static properties. Preserve current notification behavior; no performance saving is claimed. |
| F2 | Rejected and restored byte-identically | The single cold screen changes 33.186348 to 33.134897 seconds; native setup/simulation changes by only about -0.00175 seconds, while almost all total difference is unrelated elaboration. This does not support useful benefit from the added context policy. Trial and compatibility evidence are preserved. |
| T3 | Retain as implementation simplification; no speedup claim | Capture the private switch once in the existing executor Boolean and remove two runtime environment predicates. O0/O2 regression proves actual suppression, fixed lifetime for an existing executor and recapture for a new executor in both directions. Fork suppression remains disabled. The single cold screen is 34.137321 seconds, 0.950973 seconds above baseline; performance neutrality is not established. |

F2 compatibility passed before rejection: six focused Release tests; byte-equal
O0/O2 output and native objects on a frozen source-free snapshot; prior O2
cache reuse without module lowering; byte-identical raw/optimized dumps with
local names; and all 308 full-workload native objects with identical paths and
bytes. Both timing observations retain exact 19/75/87 correctness parity.
Receipts under `build/simplification-packet-5/f2/` preserve these findings.
The trial provides no statistical speedup or no-regression conclusion.

T3 changes the lifetime of the private
`FSIM_DISABLE_STABLE_DIRECT_UPDATE_SUPPRESSION` diagnostic switch: presence is
captured when each normal `LlvmProcessExecutor` is constructed. Changing the
environment afterward does not reconfigure that executor; subsequent executors
read it afresh. Fork clones retain their existing explicit suppression-disabled
rule. No field, cache, global initialization or installed API is added. The
permanent connected-remap regression runs the first native segment before
changing the switch, then repeats an identical write. Runtime `word_unchanged`
counts distinguish the enabled and disabled routes while final signal values
match the interpreter, for both toggle directions at O0 and O2.

The T3 cold full-workload screen passes all 19 input hashes, 75 fingerprints
and 87 correctness lines. All 308 native objects retain identical relative paths
and bytes; caches are cold before simulation. The observed total is 34.137321
seconds versus 33.186348, an increase of 0.950973 seconds (2.87 percent).
Native setup/simulation increases from 24.497626 to 25.097123 seconds. A single
observation cannot establish the cause, dispersion or performance neutrality.
Retention rests on the smaller implementation and qualified behavior under the
plan's simplification rule; no speed benefit is claimed. Final cumulative
Release passes 454/454 and fresh affected Debug/Tcl-off pass 21/21 each.

Procedure deviation: F2 and T3 were source-reviewed and built, but their trial
batches were not manually reindexed immediately before the wall screens as
the campaign requests. Final integrated indexing and changed-path coverage
are recorded at closure. This does not change the measured executables;
the historical ordering is disclosed rather than represented as compliant.

**Full-plan requirement audit and local closure**

Independent review against all six plan phases found no additional selected
requirement beyond the completed packets and their final qualification.

| Plan requirement | Final outcome | Evidence owner |
| --- | --- | --- |
| Correct audit premises and assign public-surface ownership | Completed; all five stacks retained, with explicit integration/link-witness limitations | S1 disposition, surface report and closure receipt |
| Reproduce missing behavior before selecting repairs | Completed; baseline coverage and SDF failures recorded; foreign-loader findings bounded | S1 feature reproduction receipt and surface report |
| Connect selected HDL coverage behavior | Completed for the documented SV2017 procedural subset, including native execution, inventory, persistence and disabled behavior | S2 closure receipt and permanent coverage tests |
| Connect selected SDF behavior | Completed for SpecifyPath/IOPATH and TimingCheck through CLI, C and Tcl, including safe-point rollback and replay | S3 closure receipt; S4 relative-path/body-limit regression qualification |
| Delete verified unreachable implementations | Completed L-01 and RT-01; shared live roots retained | S1 deletion reviews, focused tests and closure |
| Consolidate helpers/harnesses and bounded accounting | Accepted small pilots completed; count/byte/depth bounds and substantive assertions preserved | S1 ledger, helper reviews and unchanged fixture/case inventory |
| Pilot inventory-parser consolidation | Rejected and restored because the prototype added code and indirection; validators retained | S1 rejected diff and inventory report |
| Preserve deterministic artifacts and native-cache behavior | Fixed-input comparisons passed; fresh managed revision-ID ordering repaired while preserving catalog precedence, ambiguity and manual order | S1 comparisons and S4 final replay/closure receipt |
| Evaluate T9, T3, DCO-7 and F2 independently | All four dispositioned above; F2 restored, only T3 retained, with no speedup claim | S5 fresh attribution and candidate receipts |
| Close retained cumulative source and preserve obligations | Full Release 454/454 in 146.72 seconds; fresh Debug/Tcl-off 21/21 each; final docs/package and metadata checks recorded separately | S5 closure receipt, source identities and root reviews |

The final secondary selection retains the sixteen S4 affected cases and adds
five native, fork, class, observer and LLVM shields. Its runner derives build
targets from generated CTest commands, requires the exact selected-name set,
uses twelve build workers and rejects an empty test selection. The final
source includes the F2 restoration and T3 lifetime regression.

The final full persistent canonical index is
`home-colin-projects-fsim`, generation `2026-09-28T20:21:27Z`, with 57,647
nodes and 372,200 edges. The recorded limits remain 325 partial files and one
unusable diagnostic include; no files are skipped and one VCD is ignored.
All three T3 changed source/test paths have matching metadata and no recorded
parse issue. The index remains best-effort evidence; source review covers
the disclosed gaps.

Deferred product integration, host-backend linkability and relocated ACC
consumer witnesses remain explicit scope limits. No feature stack was retired.
Batch 189, the full under-15-second optimization campaign, seven-pair/Vivado
qualification, hosted CI and publication are outside this completed plan.
Changes remain uncommitted; the original staged handoff and unrelated
worktree artifacts are preserved.
