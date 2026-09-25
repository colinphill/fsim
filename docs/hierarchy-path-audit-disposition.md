<!-- SPDX-License-Identifier: Apache-2.0 -->
# Disposition of the hierarchy-path and simplification audit

## Scope and status rules

This record cross-references the recommendations in the read-only audit
`/tmp/over-engineering-audit.md` (dated 2026-09-22; its source snapshot was
`codex/v3` at `5fc0d428`) with the current Batch 188B–188I record in
[`implementation_plan_v3.md`](implementation_plan_v3.md). Recommendations are
paraphrased here; this file does not reproduce the audit.

The audit describes candidate simplifications, not acceptance criteria. This
disposition uses four statuses:

- **Implemented** means the governing plan records the bounded change complete
  with focused evidence.
- **Partial/adapted** means only a compatible subset was implemented or the
  recommendation was reshaped to preserve an existing contract.
- **Rejected—correctness** means the proposed simplification would erase a
  required semantic distinction or ordering property; the plan records the
  retained behavior.
- **Pending** means completion is not established by the current plan or live
  evidence. Pending is not a rejection.

The live plan records 188B–188H complete and 188I Changes 1–19 complete.
The final-source Release, Debug, and LLVM-enabled ASan/UBSan full suites each
passed 428/428 tests. LeakSanitizer was disabled for the sandbox ptrace
constraint. At the user's direction, 188I performance qualification is
deferred to future work after the local correctness gates; no measured
speedup or performance pass is claimed. Remaining Change 20 closure status
is governed by the plan.

## Verification infrastructure — audit §2.10

| Audit item | Disposition | Evidence and limit |
|---|---|---|
| 1. Remove composed audits and recursive closure replays | **Partial/adapted** | 188B Changes 10, 15, and 16 retire redundant release prose and duplicate execution paths, and make lanes run their required tests once. The plan does not say that every composed audit or every recursive driver was deleted; active closure and inventory validators remain. |
| 2. Retire frozen v1/v2 release gates | **Implemented** | 188B Changes 8–9 retire the generated v1/v2 release-record CTests after migrating current obligations. Historical records and source scripts remain as history. The plan records focused registration and current-owner tests. |
| 3. Use one helper for all test registration | **Partial/adapted** | 188B Changes 13–14 add `fsim_register_simple_test` and `fsim_add_single_source_test_target` for bounded registration patterns. The plan does not claim that all registrations were mechanically routed through one wrapper. |
| 4. Remove the resource-portability token checker | **Partial/adapted** | 188B Change 11 moves its current obligations to boundary and behavioral tests and removes the large umbrella from registration. The checker remains packaged as a historical record; current focused resource validators remain. |
| 5. Delete all test-infrastructure meta-gates | **Partial/adapted** | The misleading source-line-budget registration was removed. Live `tests/CMakeLists.txt` still registers `CheckTranslationUnitStructure.cmake`, `CheckApplicationTestDeduplication.cmake`, and `CheckCTestCommandUniqueness.cmake`; the first enforces a retained structural rule. |
| 6. Replace all inventory validators with one generic checker | **Pending** | The completed batches add required-ID, owner, path, and behavioral evidence checks, but the repository still has domain-specific inventory validators. No generic replacement is recorded. |
| 7. Remove derived numeric pins | **Partial/adapted** | 188B Changes 10 and 17 replace multiple derived prose, test, and archive counts with current required-content/owner checks. Real ABI, schema, archive, and format identities remain intentional freezes. This does not remove every numeric gate. |
| 8. Ungate documentation ledgers | **Pending** | Redundant current-release prose checks were removed, but the plan does not establish that all exact documentation-token gates or restated ledger values are gone. |
| 9. Parameterize mirrored SDF/VITAL test pairs | **Pending** | The completed plan does not claim broad parameterization of the mirrored SDF/VITAL suites. Shared builders elsewhere do not establish this recommendation. |
| 10. Remove test/archive totals and batch-specific log names | **Implemented with retained content checks** | 188B Changes 17–18 replace installed test/archive totals with required-content checks and remove obsolete workflow naming while preserving the four LLVM lanes and their evidence. |

## Structural simplification — audit §3.7

| Audit item | Disposition | Evidence and limit |
|---|---|---|
| 1. Consolidate the reflective JIT cache-key walk | **Implemented** | 188H Changes 1–4 add exhaustive operation traits and a canonical semantic-field encoder, version the native cache to v169, and test field/dependency mutations. `fsim.llvm` and schema/package gates are recorded as passing. |
| 2. Split the hierarchy package monolith | **Implemented, preserving one engine** | 188D Changes 2–12 extract language-specific construction work into cohesive units and retain a single elaboration engine. The plan records Debug/Release closure. |
| 3. Consolidate duplicated binary codecs | **Partial/adapted** | 188C shares bounded little- and big-endian primitives and migrates applicable codecs under frozen byte fixtures. Text formats, partial-read behavior, and scheduled-for-deletion transports remain distinct. No universal codec or wire-endian change is claimed. |
| 4. Replace the resource-limit apparatus with a common framework | **Pending** | No completed 188B–I change establishes a common limits framework. Existing boundary limits remain. |
| 5. Replace the SystemC island transport with direct calls | **Implemented with ABI boundary retained** | 188E replaces the in-process wire loop with typed kernel operations and removes its loopback framing. Lifecycle and failure behavior remain tested; the native plugin C ABI stays unchanged. |
| 6. Delete per-language coverage shims | **Rejected—correctness/contract** | 188C Change 14 found no redundant forwarder under existing error contracts. Verilog and VHDL paths differ in unsupported-language and standard-mode validation, so they remain separate. |
| 7. Inline every trivial out-of-line accessor | **Partial/adapted** | 188C Change 15 inlines leaf accessors only where it introduces no implementation dependency. No blanket migration is recorded. |
| 8. Recombine all split/support files | **Partial/adapted** | 188C Change 16 recombines selected artificial splits and preambles along cohesive boundaries. The larger coverage translation unit and other splits remain where merging would enlarge or entangle ownership. |
| 9. Replace result structs with one universal outcome type | **Pending** | No common result abstraction is recorded. |
| 10. Replace all local diagnostic wrappers | **Partial/adapted** | 188C Change 13 shares nineteen constructors with matching code, span, severity, and stack semantics. Different diagnostic contracts remain separate. |
| 11. Introduce one stored 128-bit ID type | **Partial/adapted** | 188C Changes 9–10 share formatting and digest helpers while retaining each domain's stored identity type and values. |
| 12. Add a universal string/path/file module | **Partial/adapted** | 188C Changes 11–12 share equivalent case, trim, path, and file helpers while preserving language-specific grammar and error behavior. 188I adds project-owned hierarchy paths; it does not make every local string helper universal. |
| 13. Simplify the governance layer | **Partial/adapted** | See §2.10 above; selected stale wrappers and repeated checks were retired, while substantive inventories and structural checks remain. |
| 14. Fold frontend/design and DesignIR representations together | **Rejected—correctness/ownership** | The accepted path work shares interned path spelling and table ownership while retaining distinct elaborated, DesignIR, compiled-HIR, and runtime projections. Their IDs are table-local and their records carry different semantics and validation responsibilities. 188I Change 11 explicitly retains distinct semantic projections; it does not merge the projections. |

## Build-time algorithmics — audit §4

| Audit item | Disposition | Evidence and limit |
|---|---|---|
| 4.1. Index DesignIR construction lookups | **Implemented** | 188D Changes 13–14 index parent/specialization, declaration/value/direction, and SystemC projection lookups. The plan records hierarchy-shadowing and scaling coverage. |
| 4.2. Reuse sorted hierarchy path views | **Partial/adapted** | 188D Change 15 reuses builder-local sorted path views with explicit invalidation and lifetime rules. 188I adds interned path IDs. A permanently retained public span over all sorted paths is not claimed. |
| 4.3. Index compiled-HIR references by owner | **Implemented, bounded scope** | 188D Change 17 records an owner index and avoids unnecessary index rebuilds. It does not claim removal of every validation or lookup pass. |
| 4.4. Avoid whole-design copies for source relocation | **Implemented** | 188D Change 18 relocates source names during serialization and removes the cold-build relocation copy and duplicate validation. |
| 4.5. Remove repeated trusted-state validation | **Partial/adapted** | 188C Change 17 removes repeated validation of unpublished trusted state; 188D Change 18 removes a duplicate pass. Mutable public models and external boundaries retain validation and budgets. |
| 4.6. Avoid filesystem syscalls in source identity scans | **Implemented** | 188D Change 16 canonicalizes source identities once, with Windows case and relocation behavior covered. |
| 4.7. Reuse process sharing and JIT preflight work | **Implemented with semantic equality** | 188H Changes 5–8 carry process summaries and preflight keys forward and use typed grouping plus exact operation checks. The `sv_hierarchy` differential regression includes changed `UnaryNot` operands. Pointer-only body identity is rejected; see correctness dispositions below. |
| 4.8. Remove dead hierarchy rollback code and copied parent maps | **Implemented** | 188D Changes 1–3 remove unused checkpoints/journals and replace copied parent binding maps with scoped views and overlays. The plan records construction and hierarchy tests. |

## Runtime and memory proposals — audit §§5–6

| Audit item | Disposition | Evidence and limit |
|---|---|---|
| 5.1. Add packed-value word fast paths | **Partial/adapted** | 188G Changes 13–14 add allocation-free Logic4 resolution through 64 bits and word-based force/release. Strength, charge, Logic9, and wide resolution retain their required paths; no blanket word conversion of every helper is claimed. |
| 5.2. Index named-event members and switch connectivity | **Implemented** | 188G Changes 10–11 replace design-wide scans with maintained event membership and incremental switch connectivity. Alias lifecycle, resolution, and switch tests are recorded as passing. |
| 5.3. Replace per-signal driver maps | **Partial/adapted** | 188G Change 12 uses an ordered inline-first driver table with overflow storage and removes the parallel strength map. The established ProcessId ordering and force/release behavior remain. |
| 5.4. Make signal-name lookup heterogeneous | **Implemented/adapted** | 188I Change 3 converts elaborated hierarchy path lookups to table-local IDs and uses `HierarchyPathTable::find(string_view)` for probes. This avoids constructing owning path strings for lookup. The coordinated Debug build and 16 focused hierarchy/artifact/runtime/trace/frontend/SystemC/VPI/Tcl/API tests passed. |
| 5.5. Replace scheduler storage with a calendar queue and tagged tasks | **Partial; calendar queue deferred** | Typed internal task descriptors, generation-safe cancellation, and reusable scratch are implemented in 188F/188G. The ordered future-time map remains. A calendar replacement was neither implemented nor qualified against complete time, delta, region, and insertion order; process-ID bitmap ordering is separately rejected. |
| 5.6. Replace interpreter dispatch with a flat opcode table | **Pending** | No flat-opcode interpreter or pre-resolved remap implementation is recorded. |
| 6.1. Store hierarchy paths once | **Implemented, bounded scope** | 188I Changes 1–13 establish table-local IDs and explicit owners across elaboration, DesignIR, tracing, external adapters, and SystemC inventories, then share and validate a canonical serialized path table. Public/plugin consumers still receive owned strings where their lifetime requires them. |
| 6.2. Materialize runtime packed mirrors on demand | **Implemented** | 188G Changes 16–17 defer eligible packed mirrors and update all readers while retaining distinct driven and published values. A stale fallback regression and focused reader suites are recorded. |
| 6.3. Persist one design projection | **Partial/adapted; projection folding rejected—correctness** | The completed 188I artifact changes share canonical path storage while retaining runtime, DesignIR, and compiled-HIR semantic sections. Those sections are not interchangeable: shared path IDs remove repeated spelling, not non-path state or validation. Changes 9–13 passed focused schema, corruption, and artifact-phase tests. |
| 6.4. Add a constant pool for operation payloads | **Pending** | The 188B–188I governing contract explicitly excludes a speculative constant pool. No implementation or measured benefit is claimed. |

## Simulation hot-path recommendations — audit §7.7

The entries below overlap §§4–6; the statuses identify the corresponding
188B–188I work rather than count it as a second implementation.

### Measure-first step

| Audit item | Disposition | Evidence and limit |
|---|---|---|
| 7.7.0. Establish a mixed runtime benchmark, profiling and LTO comparison | **Partial; performance qualification deferred** | 188B adds baseline harnesses and long-running runtime, trace, observer, and history workloads. At the user's direction, the 188I matrix is deferred to future work; candidate speedups and the suggested LTO comparison are not established. No benchmark-based prioritization claim is made here. |

### Tier 0

| Item | Disposition | Evidence and limit |
|---|---|---|
| 1. Snapshot diagnostic/profiling environment flags | **Implemented** | 188H Change 10 samples settings once per simulation construction; the focused runtime test mutates the environment afterward. |
| 2. Remove redundant value-kind copies and cache signal lookup | **Pending** | No completed change in the plan establishes both edits. |
| 3. Remove the extra driven-value copy before commit | **Pending** | No completed change specifically establishes this copy removal. |
| 4. Skip empty timing-check evaluation | **Pending** | No completed change specifically establishes this guard. |
| 5. Simplify signal-change modulus and queue access | **Pending** | No completed change specifically establishes both local edits. |
| 6. Skip empty buffered-update work and duplicate Logic9 flushing | **Implemented, related behavior** | 188H Change 11 consolidates buffered-update flushing and avoids empty virtual-access sequences; procedural-assignment and Logic9 app tests passed. |
| 7. Avoid empty observer copies and hoist Logic9 publication eligibility | **Partial/adapted** | 188F Changes 15–16 reuse observer snapshots and install observation hooks only when needed; 188G Change 16 handles eligible Logic9 publication. The exact empty-vector and caller-hoist edits are not separately evidenced. |
| 8. Skip wide-mirror copies for narrow signals | **Pending** | Lazy narrow mirrors are implemented in 188G Change 16, but this exact mirror-copy optimization is not established by the plan. |
| 9. Skip empty callable-context set construction | **Pending** | No completion evidence found. |
| 10. Reduce safe-point polling and hook calls | **Partial/adapted** | 188G Change 5 optimizes polling and composes safe-point hooks while preserving hook order and mutation behavior. The exact load-before-exchange and phase-mask policy are not separately claimed. |

### Tier 1

| Item | Disposition | Evidence and limit |
|---|---|---|
| 1. Reuse per-delta work buckets | **Partial/adapted** | 188G Change 18 reuses scheduler batch, cohort, phase-staging, and region scratch buffers. It does not claim every scheduler bucket became a persistent double buffer. |
| 2. Enable eligible direct Logic9 reads | **Implemented** | 188H Change 9 enables narrow Logic9 direct reads and tests all nine states. |
| 3. Decode edges once and split transaction fanout | **Implemented** | 188G Change 9 adds category spans and one transition decode while preserving same-value transaction dispatch and ordered full spans. |
| 4. Avoid copying dynamic fanout | **Partial/adapted** | 188G Change 8 adds generation-safe dynamic-wait registrations and bounded tombstone compaction. The plan does not claim that every dynamic fanout copy is gone. |
| 5. Add narrow resolution and word operations | **Partial/adapted** | See §5.1: Logic4 ≤64-bit resolution and force/release transforms are covered; special and wide value kinds retain their paths. |
| 6. Replace observer probes with an observation bitmap | **Partial/adapted** | 188H Change 12 caches per-signal native-update eligibility, and 188F manages optional observation hooks. A single observer-required bitmap with mutation generations is not claimed. |
| 7. Install hooks lazily and preserve in-flight observer behavior | **Implemented** | 188F Changes 15–16 reuse observer snapshots and install optional hooks only while required, retaining callback mutation semantics. |
| 8. Index events and maintain switch connectivity | **Implemented** | See §5.2; 188G Changes 10–11 and their alias/switch regressions cover the proposal. |
| 9. Replace driver maps and add transparent signal-name hashing | **Implemented/adapted** | 188G Change 12 replaces primary driver maps with ordered inline-first records. 188I Change 3 converts hierarchy name/path maps to IDs and probes the path table by `string_view`; the 16-test focused hierarchy/application slice is recorded as passing. |
| 10. Reuse cohort-dispatch scratch | **Implemented** | 188G Change 18 reuses cohort and batch buffers, clears retained references on every exit, and tests reentrant large cohorts. |
| 11. Shrink internal wakeup callback captures | **Partial/adapted** | 188G uses typed internal task descriptors for runtime wake/timer work. No claim is made about a particular lambda capture or standard-library small-buffer threshold. |
| 12. Reclaim cancellation state with generation slots | **Implemented** | 188F Changes 1–6 add owner/slot/generation handles, payload release, queue compaction, and reuse stress coverage. |
| 13. Index VPI value-change registrations | **Implemented** | 188F Change 17 indexes registrations by signal; focused VPI/runtime tests are recorded. |
| 14. Cache trace identity, selection, declaration, and encoding state | **Implemented** | 188F Changes 7–14 stream by default, cache trace identities/lookups/selection metadata, reuse encoding buffers, and bound FST work while preserving bytes and event ordering. |
| 15. Compose hook tokens and remove empty PSL work | **Implemented** | 188F Changes 15–18 preserve hook composition and observer mutation, avoid empty PSL observation, and use stable binding identities. |

### Tier 2

| Item | Disposition | Evidence and limit |
|---|---|---|
| 1. Consolidate all hot signal arrays into one record | **Partial/adapted** | 188G Change 15 separates dense hot/cold signal state while preserving public `Signal` construction and validated accessors. The plan does not claim the exact cache-line record layout or its projected traffic reduction. |
| 2. Store static fanout in CSR form | **Implemented** | 188G Change 7 uses one flat fanout array and per-signal offsets, rebuilding on topology changes while preserving order and duplicate sensitivities. |
| 3. Split hot process state from cold execution metadata | **Implemented** | 188G Change 6 separates compact inline process state from program and cold metadata; root/fork initialization and runtime tests passed. |
| 4. Use per-region ready bitmaps ordered by process ID | **Rejected—correctness** | Keep complete scheduler order, including time, delta, region, insertion sequence, and heterogeneous tasks. Process-ID order alone is insufficient to reproduce every dispatch sequence. Batch 188G explicitly retains the ordered future-time map and rejects process-ID bitmap order. |
| 5. Use a calendar queue and internal tagged task descriptors | **Partial; calendar queue deferred** | Typed internal task descriptors are used. The ordered future-time map remains, and no calendar-queue replacement has been qualified against scheduler order and deterministic interleaving. See §5.5. |
| 6. Add native write slots and remove remap/prologue work | **Partial/adapted** | 188H Change 14 adds eligible direct blocking and delayed writes with source-order preservation; Change 17 checks Logic9 remapping. The proposed constant-index and single-process-prologue changes are not independently evidenced. |
| 7. Replace hot boundary exits with direct callbacks and plane-word Logic9 handling | **Implemented, bounded set** | 188H Changes 11, 15, and 17 consolidate flushing, add guarded callbacks, and use checked Logic9 plane marshalling. The plan names coverage, class-property, and event-query callbacks; it does not claim every boundary operation was converted. |
| 8. Narrow design-wide native-execution cliffs | **Implemented** | 188H Changes 12–16 add per-signal eligibility, dependency-scoped history, direct writes, and individual retries for unsupported packs while retaining conservative unknown handling. |
| 9. Add a constant pool and flat interpreter opcode table | **Pending; speculative constant pool explicitly out of scope** | The governing 188B–188I contract excludes a speculative constant pool and a flat-opcode interpreter. No performance benefit is claimed. |
| 10. Correct the architecture statement about generated nine-state execution | **Pending** | Current `docs/architecture.md` lines 730–731 still describe exact generated nine-state code as reference-runtime work. The audit's proposed correction is not recorded as complete. |

## Correctness-grounded decisions

These are deliberate constraints, not deferred optimizations:

1. **Scheduler ordering.** Do not replace the full ordered future-time map with
   process-ID bitmap order. Equal-time dispatch also observes delta, region,
   insertion sequence, and heterogeneous task ordering. Typed task descriptors
   and reusable storage are compatible with retaining that order. Evidence:
   Batch 188G ordering tests and the explicit retained-order note following its
   Change 20 record.
2. **JIT process identity.** Do not use a shared operation-storage pointer as
   proof that two effective process bodies are equal. Overrides, signal
   remapping, operation operands, and compilation context affect semantics.
   Batch 188H uses typed grouping followed by exact operation equality; its
   changed-register `UnaryNot` regression exercises the distinction.
3. **Design projections.** Do not fold runtime, DesignIR, and compiled-HIR
   records into one representation solely to deduplicate path text. They carry
   distinct records and validation roles. Batch 188I shares a table of path
   spellings/IDs while explicitly retaining distinct semantic projections.
4. **Table-local IDs and external APIs.** A `HierarchyPathId` is valid only
   with its owning frozen table; it is not a process-independent or plugin ABI
   identity. 188I retains owned strings at C/plugin and other public lifetime
   boundaries, and the SystemC inventory change leaves plugin C ABI strings
   unchanged.
5. **Language-specific contracts.** Do not erase language-specific coverage,
   whitespace, diagnostics, or standard-mode validation when their observable
   rejection behavior differs. 188C shares only helpers with matching
   contracts and retains the non-equivalent adapters.
6. **Serialized bytes.** Share codec primitives without silently changing
   frozen archive/object/cache bytes or making all formats use one byte order.
   188C pins pre-migration fingerprints and uses explicit-endian primitives;
   format/schema changes require their own governed migration.

## Evidence boundary and remaining work

The batch/change references above point to the current execution record in
[`implementation_plan_v3.md`](implementation_plan_v3.md). That plan is the
source for claimed implementation and focused-test evidence; the 2026-09-22
audit is not treated as a current measurement. Its line counts, timing samples,
allocation estimates, and projected gains describe its original snapshot and
remain historical until remeasured.

Recommendations tagged **Pending** and any remaining 188I Change 20 closure
items are governed by the plan. The final-source Release, Debug, and
LLVM-enabled ASan/UBSan full suites passed 428/428 each; LeakSanitizer was
disabled for the sandbox ptrace constraint. The cumulative performance
qualification is explicitly deferred to future work and has no passing result.
Audit §§8–9 provide
cross-cutting sequencing and verification guidance; the batch evidence cited
above records the portions already used. This disposition is itself the
deliverable for completed 188I Change 17; the governing plan records its
bounded crosswalk and review evidence.
