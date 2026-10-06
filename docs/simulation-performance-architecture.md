<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim simulation-performance architecture

Date: 2026-09-29. Source: `codex/v3` HEAD `ede7c24e` ("Fuse static process
chains…", preceded by `eb8a5042`). Scope: the simulation engine end to end —
event model, scheduling, value/driver state, host↔native boundary, memory,
elaboration and setup, LLVM JIT code generation, and the measurement process.

**Method.** Six read-only reviews ran in parallel: event model and semantics;
the fusion machinery (V7–V23); value, driver and publication state; memory,
elaboration and measurement; workload speed-of-light; and LLVM JIT code
generation. The JIT plan (§6) also includes every IR-level proposal from
`docs/simplification-audit.md` §12, re-anchored at this HEAD. The reviewers used:

- the V23 profile categories and counters recorded in
  `docs/performance-throughput-language-comparison-2026-09-29.md` and
  `docs/performance-resume.md`;
- the local RTL copy at `C:\vprojects\rsdec4` (`rtl/`, `rtl_vhdl/`, `tb/`),
  which matches the anchors cited in the campaign docs;
- hand-written LLVM IR/C models compiled with LLVM 22.1.8
  (`opt` + `llc -O0 -fast-isel`, which matches the production backend) for
  instruction counts.

The original proposal involved no fsim builds or benchmarks. Its **[verified]**
labels mark coordinator source checks. Everything labelled "est." is an estimate
from code reading or counter arithmetic, not a timing. Implementation validation
and measurements performed since adoption are recorded in the ledger below.

---

## Adopted contract and implementation status



> **R36 structural range batch — 2026-10-06 10:02 UTC; local validation passed.**
> Ordinary current-value reads now carry precise graph ranges when all value uses
> are proven static Extracts through uniquely defined copy chains. Full, dynamic,
> debug-visible, sampled/gated, unsupported and ambiguous uses remain whole;
> explicit sensitivity and alias projection are preserved. WaitFor/WaitOn/WaitOrder
> conservatively reject narrowing. Certificate grouping connects overlapping
> writer/reader intervals within the existing scheduling domain/update class;
> unknown, observed, aliased and resolved ownership retains conservative grouping.
> This changes dependency inventory and grouping, not runtime operation semantics.
> Tests include 32 deterministic graphs against an independent pairwise oracle,
> plus wait effects, alias ranges and shared-body instance signal/Extract overrides.
> The bounded final census prioritizes existing native dispatch work; that counter
> covers the retained runtime lifetime, not all historical component work.
> One reused 12-worker build passed 2,070 steps after three recorded mechanical
> compile fixes. All 13 affected checks passed, including O0/O2 region execution,
> native publication, allocation/failure, stop/resume and source packaging.
> Cache 272, ABI v2 and artifact 73 remain unchanged. No R36 workload capture or
> performance improvement is established yet; full throughput targets remain open.
> Evidence: `.local-artifacts/simulation-performance/r36-range-aware-build-gates/qualification.json`
> (SHA-256 `8998564c5125f8ca272041bf5c1719c2a58543158d070416ad67a1fb47cbbb91`).

> **Qualified r35 structural-evidence checkpoint — 2026-10-06 09:08 UTC capture.**
> Publication reads one nonpromoting program view and inspects typed writes while
> preserving operation overrides and signal remaps. A bounded final-destructor
> census records complete component accesses, ownership and internal outputs.
> Cache 272 / shared-body v6, ABI v2 / artifact 73 and tier limit 16,384 remain.
> GPT-6.1-Sol/high owns builds/profiles. Two reused 12-worker builds passed
> 2,066 and 2,065 steps; 11 behavioral gates and the complete graph selector
> passed without failures or repairs. Compact/override views and native
> publication are covered separately; a combined fixture is not established.
> One capture exited 0 with owned cleanup in 50.202 s, first profile marker at
> 22.269 s and tick 2,455,000. The actual supervisor selected CPU 1 at 100% idle,
> checked 0.311 ms before spawn. All 66 source records, 39 dependencies and 315
> original inputs match afterward. Capture receipts remain immutable; docs refresh
> separately. All 517 native objects are byte-identical to r34; 49 shared objects
> retain 287,220 optimized IR instructions and 1,270,957 executable bytes.
> The final snapshot has 642 candidate components / 19,122 members, 627 prepared
> program components and 233 frontier runtimes. The 2,048-row bound captures
> three complete components: 610 (332 members, prepared without frontier),
> 1082 (134, unprepared), and 400 (6, frontier). Their 472 members, 1,233 accesses
> and 338 outputs reconcile to 2,046 rows. The dominant 81-member family is
> unsampled; these findings do not establish its range shape or partition safety.
> All 2,067,101 trusted entries use descriptor shapes. Both generations retain
> 2,679 unique SimIR bodies / 352,156 operations / 25,355,232 outer-vector bytes.
> Arena capacity/used remain 9,224,582 / 9,196,312 bytes. Glibc G2 in-use chunk
> bookkeeping is 1,159,536,576 bytes; sampled RSS/HWM are 1,265,238,016 /
> 1,323,950,080 bytes. These scopes establish neither isolated RSS savings nor
> a Wall speedup. No standalone publication effect or full throughput is claimed.
> Next: precise read dataflow ranges and range-aware connectivity, subject to
> semantic review. Prioritize large structural savings; no further capture/build
> is authorized before the next reviewed batch.
> Analysis: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-publication-views-range-census-normal-pmu-r35-analysis-receipt.json`.
> Strict range report: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-publication-views-range-census-r35-analysis.json`.

> **Qualified r34 diagnostic checkpoint — 2026-10-06 08:20 UTC capture.**
> Region certificates separate readers that share only an input; candidate
> writer-reader and conflict connections remain. The binder directly initializes
> the borrowed span array, removing the authenticated 64-byte temporary/copy.
> Cache 272 / shared-body v6, public ABI v2 / artifact 73 and tier limit 16,384
> remain unchanged. Build/profile ownership is GPT-6.1-Sol/high. The reused
> 12-worker default build passed 2,066 steps. All 12 focused checks passed,
> including the complete graph/wave selector and allocation/failure gates.
> Two existing fixtures relied on read-only sibling coalescing: real join and
> dormant-writer connections preserve their forwarding and 65/64 selective-wake
> behavior; both failures and reviewed repairs are archived. No commit/push.
> One capture exited 0 with owned cleanup in 49.601 s, first marker at 18.570 s
> and tick 3,375,000. CPU 9 was 100% idle with no competing lane; the actual
> supervisor checked immediately before spawn. All 66 source records, 39
> dependencies and 315 original inputs match afterward. The immutable freeze
> includes graph, selector, wave and application fixtures; docs refresh separately.
> Current materialized plans are 233 / 16,485 members, with maximum 82 instead of
> 2,625; 198 plans have 81 members. Snapshot activation programs are 627.
> All 2,834,267 trusted entries use descriptor shapes. Full sync visits 4,733,721
> members in 58,441 passes; selected sync visits 8,987,970 of 248,354,991
> candidates in 3,066,111 passes. Boundary decisions reconcile at 2,311,754
> eligible / 58,057 conservative. Retained alias reuse is 2,702,736 / 2,818,151
> binds; full collectors are 346,053. These denominators have separate scopes.
> Postwave weighted self: dispatch family 7.08%, eligibility 2.99%, lease
> acquisition 3.40%, all lease accessors 0.94%, publication family 3.60%.
> All native bodies/local helpers total 5.71%; adding host pending validation
> gives 8.20%. Binder plus bulk/legacy/backing/atomic paths is 6.91% self /
> 9.56% inclusive union, including ordinary copy callees. Operation access is
> 5.71% self; current caller evidence points to expanded operations and publication.
> Sampling has 4,432 samples, zero LOST and 1.781 s of recognized throttling;
> period weighting does not establish unbiased CPU or a Wall speedup.
> Native objects are 517: 285 overlapping keys remain byte-identical, 232 are
> new and 16 disappear. Shared objects increase from 19 to 49; optimized LLVM IR
> grows from 203,205 to 287,220 and helper-inclusive executable bytes from
> 1,182,188 to 1,270,957. ELF bytes fall from 3,096,680 to 2,242,064; rodata falls
> from 1,498,536 to 728,248. More wrappers/IR accompany smaller component tables;
> LLVM accounts for 81.74% of sampled plan-to-first-marker CPU.
> Both generations retain 2,679 unique SimIR bodies / 352,156 operations /
> 25,355,232 outer-vector capacity bytes. Frontier arenas use 9,196,312 of
> 9,224,582 bytes; index backing 2,000 and headers 7,456 are existing-total subsets.
> Planner release is cumulative 14,641,536 capacity bytes across 233 lifetimes.
> Glibc G2 in-use chunk bookkeeping is 1,159,537,616 bytes. Actual sampled RSS
> peaks at 1,265,291,264 bytes; reported HWM is 1,323,925,504, last sample 48.453 s.
> These measurements do not establish an isolated RSS change. The partial tick
> is about 0.755% of the historical full-stimulus endpoint; no codeword fraction,
> extrapolated Wall acceptance or full throughput/external parity is established.
> Next: use current dispatch, operation access, publication and alias call paths
> to rank the next bounded source change. No second capture is authorized.
> Analysis: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-writer-connected-components-normal-pmu-r34-analysis-receipt.json`.

> **Qualified r33 control diagnostic checkpoint — 2026-10-06 07:53 UTC capture.**
> The synchronous binder borrows all packed value planes through one internal A4
> getter. Versioned values acquire one backing block; owning, pinned and
> unversioned values retain their existing accessor paths. Canonical span checks
> remain. Cache 272 / shared-body v6, public ABI v2 / artifact 73 and tier limit
> 16,384 stay unchanged. Build/profile ownership remains GPT-6.1-Sol/high.
> The reused default build passed 2,167 steps with 12 workers; the test-only target
> rebuild passed 326 steps. All eight focused behavioral gates passed, including
> exact all-plane offsets/data, fresh post-publication borrow and retained old
> snapshots for wide Logic4/Logic9, plus narrow unversioned values. There were no
> failures or repairs. No commit/push.
> One capture exited 0 with owned cleanup in 49.802 s, first wave at 13.384 s and
> tick 1,095,000. CPU 9 was 100% idle with no competing lane; the actual supervisor
> checked 0.314 ms before spawn. All 62 source records, 39 dependencies and 315
> original inputs match afterward, including the private A4 header and ownership
> test. Capture/source/tool identities are immutable; documentation hashes are
> refreshed separately. All 607,715 trusted entries used descriptor shapes.
> Full sync visited 35,152,704 members in 13,562 passes; selected sync visited
> 2,128,614 of 1,617,903,072 candidates in 624,191 passes. Boundary decisions
> reconcile at 548,229 eligible / 13,552 conservative, all container-element aliases.
> Retained alias reuse is 593,337 / 595,010 binds, with 5,014 full collectors.
> Postwave weighted self is binder 14.16%, new bulk getter 4.83%, residual backing
> lookup 0.22%, native body 9.63%, host pending helper 8.34%, dispatch 6.38% and
> publication 5.75%. Complete getter self is 5.04% / inclusive union 5.44%.
> Binder plus getters is 19.20% self / 23.24% inclusive union, versus r32's
> 16.00% / 24.58%. This sampled aggregate does not establish improvement.
> Inlined atomic acquisition is included in helper self; no standalone atomic leaf
> samples appear. The complete lease accessor family is 2.89% self / 4.41% union.
> Fresh disassembly proves one logical versioned acquire. Binder code shrinks
> from 3,348 to 3,261 bytes but its stack reservation grows by 64 bytes; four loads
> and four stores copy the returned span array. Samples at those post-call loads
> may include skid/return latency, so they do not isolate copy cost.
> Sampling has 4,695 samples, zero LOST and 1.905 s of recognized throttling;
> period weighting is not a complete correction. All 301 native objects are
> byte-identical to r32. The 19 shared objects retain 203,205 optimized LLVM IR
> instructions, 1,182,188 helper-inclusive executable bytes and 3,096,680 ELF bytes.
> Both generations retain 2,307 unique SimIR bodies / 354,050 ops / 25,491,600
> outer-vector capacity bytes and unchanged frontier arena/index totals.
> Glibc G2 in-use chunk bookkeeping is 1,130,842,736 bytes. Sampled actual process
> RSS peaks at 1,233,707,008 bytes; reported HWM is 1,292,369,920, with the last
> sample at 48.450 s. These scopes do not establish isolated RSS savings.
> Existing creation rows show 47 runtimes / 16,626 members; six components of
> 2,625 or 2,592 members contain 93.74% of them. Per-component signal/internal
> counts and completed-codeword fraction are not established by this capture.
> Next: read-only shared-input connectivity and downstream scheduling/sharing
> safety audit before another bounded change. No paired Wall, extrapolated target
> acceptance or full throughput/external parity claim is made.
> Analysis: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-bulk-plane-borrow-normal-pmu-r33-analysis-receipt.json`.

> **Qualified r32 control diagnostic checkpoint — 2026-10-06 07:32 UTC capture.**
> The binder omits a descriptor validator already entailed by its construction
> checks. Lease acquisition omits duplicate support/owner checks already entailed
> by the retained complete packed-slot binding check. All other validation,
> geometry and lifetime guards remain. Cache 272 / shared-body v6, public ABI v2 /
> artifact 73 and tier limit 16,384 stay unchanged. Build/profile ownership remains
> GPT-6.1-Sol/high. The reused default build passed all 2,066 steps with 12 workers;
> all eight focused behavioral gates passed without repairs, including boundary,
> A4 ownership/rebase, core simulation, allocation, preparation failure, retained
> stop/resume and Generic update. The boundary gate took 5.475 s. No commit/push.
> One capture exited 0 with owned cleanup in 49.601 s, first wave at 13.219 s and
> tick 1,125,000. CPU 9 was 100% idle with no competing lane; the actual supervisor
> checked 0.302 ms before spawn. All 62 source records, 39 dependencies and 315
> original inputs match afterward. Capture/source/tool identities are immutable;
> documentation refresh hashes are recorded separately.
> All 629,040 trusted entries used descriptor shapes. Full sync visited 36,391,680
> members in 14,040 passes; selected sync visited 2,203,110 of 1,674,652,320
> candidates in 646,085 passes. Boundary decisions reconcile at 567,475 eligible /
> 14,030 conservative, all container-element aliases. Retained alias reuse is
> 614,604 / 616,335 binds (99.719%), with 5,188 full collectors, 434 plane misses
> and 1,292 unconfirmed-count misses. These have a retained-runtime scope.
> Authenticated host disassembly confirms zero direct binder validator calls and
> zero duplicate support/owner calls in lease acquisition; the retained packed-slot
> check still performs its transitive validation. Remaining validator self is
> 0.124%, sampled only beneath staged issue, versus r31's 4.54% overall self.
> Current postwave weighted self is binder 9.05%, native body 8.74%, host pending
> helper 8.01%, backing plane lookup 6.95%, dispatch 6.43% and publication 5.62%.
> Body plus host helper is 16.75%. The complete lease accessor family is 2.62%
> self / 4.37% inclusive union; binder inclusive is 24.37%. Inclusive paths overlap.
> Sampling has 4,695 samples, zero LOST and 1.888 s of recognized throttling;
> period weighting does not establish unbiased CPU or isolated runtime gains.
> All 301 native objects are byte-identical to r31. The 19 shared objects retain
> 203,205 optimized LLVM IR instructions, 1,182,188 helper-inclusive executable
> bytes and 3,096,680 ELF bytes. Both generations retain 2,307 unique SimIR bodies /
> 354,050 ops / 25,491,600 outer-vector capacity bytes. Configured frontier arenas
> remain 7,630,336 capacity / 7,624,552 used bytes; index backing 2,000 bytes and
> vector headers 1,504 bytes remain subsets of existing totals.
> Glibc G2 in-use chunk bookkeeping is 1,130,855,232 bytes. Sampled process RSS
> peaks at 1,233,850,368 bytes; reported HWM is 1,292,488,704, with the last sample
> at 48.453 s. These are separate accounting scopes, without isolated RSS claims.
> The next action is read-only proof of a single-snapshot all-plane borrowed getter
> for the binder's backing lookup cost; implementation and capture are not yet
> authorized. No paired Wall or full throughput/external parity claim is made.
> Analysis: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-binder-construction-lease-checks-normal-pmu-r32-analysis-receipt.json`.

> **Qualified r31 control diagnostic checkpoint — 2026-10-06 07:09 UTC capture.**
> The batch indexes raw activations once per dispatch across the complete offered
> frontier and lets confirmed alias geometry cover shorter task prefixes.
> Both workspace budgets include the preallocated SV summary; Generic storage
> remains empty. Cache 272 / shared-body v6, public ABI v2 / artifact 73 and tier
> limit 16,384 stay unchanged. Build/profile ownership remains GPT-6.1-Sol/high.
> The reused default build passed with 12 workers. All six focused gates passed;
> the final boundary run took 5.440 s and exercises real scheduler duplicates
> beyond the selected prefix, valid outside-prefix work/reentry and post-seed
> corrupted-map rejection. Narrow test repairs removed alias-type/unused-capture
> compilation errors, restored admitted member bodies and moved map corruption
> into its live callback. All failure evidence and exact assertions are recorded.
> Alias witnesses retain shorter-prefix success, larger-unconfirmed decline and
> peer-plane invalidation. Allocation, preparation failure, retained stop/resume,
> Generic update and manifest checks remain green. No commit/push.
> One capture exited 0 with owned cleanup in 49.601 s, first wave at 13.270 s and
> tick 1,055,000. CPU 9 was 100% idle with no competing lane; the actual supervisor
> checked 0.321 ms before spawn. All 62 source records, 39 dependencies and 315
> original inputs match afterward. The source/tool receipts are immutable;
> documentation refresh hashes are separate.
> All 574,536 trusted entries used descriptor shapes. Full sync visited 33,504,192
> members in 12,926 passes; selected sync visited 2,023,958 of 1,529,523,648
> candidates in 590,094 passes. Boundary decisions reconcile at 518,137 eligible /
> 12,916 conservative, all container-element aliases. Retained alias reuse is
> 560,246 / 561,831 binds (99.718%), with 4,750 full collectors, 399 plane misses
> and 1,181 unconfirmed-count misses. These counts have a retained-runtime scope.
> Postwave weighted self is dispatch 5.74% versus r30's 13.79%; the native body is
> 9.78%, host pending validation 8.46%, binder 7.92%, backing plane lookup 6.30%
> and publication 5.63%. Body plus host helper is 18.24%; do not omit moved host
> work. All three lease accessors total 2.45% self / 4.22% inclusive union; binder
> inclusive is 25.37%. Fresh authenticated host bands support current read-only
> pending-loop, binder/backing and dispatch audits before the next bounded change.
> Sampling has 4,698 samples, zero LOST and 1.841 s of recognized throttling;
> inclusive paths overlap and period weighting is not a complete sample correction.
> All 301 native objects are byte-identical to r30. The 19 shared objects retain
> 203,205 optimized LLVM IR instructions, 1,182,188 helper-inclusive executable
> bytes and 3,096,680 ELF bytes. Retained SimIR remains 2,307 unique bodies /
> 354,050 ops / 25,491,600 outer-vector capacity bytes in both generations.
> The summary increases configured arena capacity by 133,337 bytes to 7,630,336;
> used bytes increase by 133,008 to 7,624,552. Existing index/header subsets are
> unchanged. Glibc G2 in-use chunk bookkeeping is 1,130,868,048 bytes; sampled
> actual process RSS peaks at 1,233,850,368 and reported HWM is 1,292,537,856,
> with the last sample at 48.451 s. These are separate accounting scopes.
> No paired Wall, isolated RSS saving or full throughput/parity qualification is
> claimed. Analysis: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-task-summary-alias-prefix-normal-pmu-r31-analysis-receipt.json`.

> **Qualified r30 control diagnostic checkpoint — 2026-10-06 06:02 UTC capture.** The
> combined batch adds lease ordinal lookup with caller-owned indices, sorted
> boundary descriptor lookup with the exact unordered fallback, and removes the
> redundant retained executor frame layout. The reused default build passed with
> 12 workers; all 12 focused gates passed, including ordinal identity/lifetime and
> zero-allocation guards, unordered/duplicate/missing boundary descriptors, manual
> wide/Logic9 acquisitions, preparation failure and required-read/remap behavior.
> Cache 272 / shared-body v6, public ABI v2 / artifact 73 and tier limit 16,384
> remain unchanged. Build/profile ownership remains GPT-6.1-Sol/high; no commit/push.
> One capture exited 0 with owned cleanup in 49.601 s, first wave at 13.371 s and
> tick 935,000. CPU 9 was 100% idle with an empty executable lane; the supervisor
> checked 0.343 ms before spawn. All 62 source records, 39 dependencies and the
> 315 inputs match afterward. Immutable capture/source/tool identities remain
> archived; this documentation refresh is recorded separately.
> All 490,989 trusted entries used descriptor shapes. Full sync visited 28,553,472
> members in 11,016 passes; selected sync visited 1,734,551 of 1,307,495,520
> candidates in 504,435 passes. Boundary decisions reconcile at 442,735 eligible /
> 11,006 conservative, all container-element aliases. Retained alias reuse was
> 475,145 / 478,284 binds (99.34%), with 9,412 full collectors.
> Postwave weighted self for the complete lease family (ordinal entry, common
> role/extent helper and residual general entry) is 1.97%, versus r29's 5.38%; its
> inclusive union is 3.20%. Publication self is 3.71%, versus 9.39%, with 11.97%
> inclusive. Fresh disassembly confirms direct ordinal checks and the sorted
> lookup branch bypassing the retained nested unordered scan. Current ordered
> publication checks/lookup account for 0.64% of postwave sampled period; no
> samples fall in the unordered scan. These are diagnostic shares, not path counts.
> Dispatch now ranks at 13.79% self; binder is 8.17% self / 22.26% inclusive.
> Two authenticated per-member task-match/count scan cores account for 7.80%
> postwave; source audits are checking dispatch, binder and alias geometry before
> choosing the next change. Native body plus host pending helper remains 15.44%.
> Sampling has 4,696 samples, zero LOST and 1.857 s of recognized throttle intervals;
> inclusive paths overlap and the weighted ranking does not establish unbiased CPU.
> All 301 native objects are byte-identical to r29. The 19 bodies retain 203,205
> optimized LLVM IR instructions, 1,182,188 helper-inclusive executable bytes and
> 3,096,680 ELF bytes. Retained SimIR remains 2,307 unique bodies / 354,050 ops /
> 25,491,600 outer-vector capacity bytes in each generation. Index backing is
> 2,000 bytes inside configured arena capacity; 47 vector headers add 1,504 bytes
> inside runtime shell totals. Do not add either subset again. Glibc G2 in-use
> chunk bookkeeping is 1,130,609,696 bytes, 7,999,424 below r29 for this combined
> batch; it is not payload/RSS accounting or isolated attribution. Sampled actual
> process RSS/HWM is 1,292,656,640 bytes, with the final sample at 48.451 s.
> No paired Wall, causal RSS saving or full throughput/parity qualification is
> claimed. Analysis: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-ordinal-publication-metadata-normal-pmu-r30-analysis-receipt.json`.

> **Qualified r29 control diagnostic checkpoint — 2026-10-06 05:02 UTC capture.** Cache
> 272 / shared-body v6 moves initial-slot validation into a registered noexcept
> host helper and reuses the synchronous lease's captured block. The reused default
> build passed with 12 workers and all ten focused gates passed, including lease
> release/COW reacquire, builtin sparse parity, O0/O2 dynamic flags/keys/origin/site
> negatives, wide Logic4/Logic9, allocation, cache and schema. Both declaration and
> call carry NoUnwind; both compiler no-unwind verifiers remain. Public ABI v2 /
> artifact 73 and tier limit 16,384 stay unchanged. Build/profile workers always
> use GPT-6.1-Sol with high reasoning. No commit/push.
> One bounded capture exited 0 with owned cleanup in 49.601 s and tick 875,000.
> CPU 9 was 100% idle with no competing executable lane; the same supervisor
> checked 4.430 ms before spawn. All 55 source records, 39 dependencies and 315
> inputs match afterward. Capture/source identities are immutable; this checkpoint
> refresh has separate hashes. All 448,412 trusted entries used descriptor shapes.
> Full synchronization visited 26,072,928 members in 10,059 passes; selected sync
> visited 1,581,336 of 1,193,761,152 candidates in 460,556 passes. Boundary decisions
> reconcile at 404,485 eligible / 10,049 conservative, all container-element aliases.
> Retained runtime alias reuse is 432,869 / 435,707 binds (99.35%).
> Current postwave weighted self is dispatch 12.44%, publication 9.39%, v6 body
> 8.08%, host helper 6.58%, binder 5.82%, lease lookup 5.38% and backing lookup
> 4.74%. Body plus helper is 14.66%, versus r28 body 14.99%; moving work out of
> JIT does not establish an isolated runtime improvement. Atomic shared-pointer
> load has no sampled postwave self/inclusive cost. Binder inclusive is 21.21%,
> publication 18.85% and lease lookup 7.26%; inclusive paths overlap.
> Authenticated lease disassembly attributes 2.83% of postwave sampled period to
> signal-ID binary search, 0.59% to writable-binding linear search, 0.89% to role/
> plane extent construction and 0.37% to owner-record lookup. Helper site/row
> lookup and active testing is 2.64%; member-strided origin comparisons are 2.76%.
> These bands do not establish active/inactive row frequency or search lengths.
> Exact v6 relocation proves the helper call with 2,592 sites; its captured PMU
> ancestry lacks a caller. Remaining native samples are diffuse and only selectively
> mapped. Next work should follow the binder/publication and lookup proof, without
> removing dynamic guards or starting another capture before review.
> All 301 native objects authenticate. The 19 shared objects have 1,182,188
> helper-inclusive executable bytes (11,823 fewer than r28), 203,205 optimized
> LLVM IR instructions (3,142 fewer), and 3,096,680 ELF bytes (9,896 fewer).
> Read-only data stays 1,498,536 bytes; relocation bytes rise by 576 to 374,736.
> The host helper is separately 608 bytes; its startup registrar is 1,634 bytes.
> Retained SimIR remains 2,307 unique bodies / 354,050 operations / 25,491,600
> outer-vector capacity bytes in both generations. Cumulative planner release is
> 14,767,424 bytes across 47 materializations, not simultaneous resident savings.
> Sampled fsim RSS peaks at 1,275,994,112 bytes; reported HWM is 1,300,185,088.
> Glibc in-use chunks are 891,694,960 / 1,138,609,120 bytes at generations 1 / 2;
> these are allocator bookkeeping, not requested payload or RSS. Sampling has
> 4,689 samples, zero LOST and 1.887 s of recognized throttling. Period weighting
> does not fully correct missing samples. Instrumented progress and sample shares
> do not prove paired Wall/throughput gains; canonical parity and full qualification
> remain open. Receipt:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache272-pending-helper-lease-capture-normal-pmu-r29-analysis-receipt.json`.

> **Qualified r28 control diagnostic checkpoint — 2026-10-06 04:26 UTC.** Cache 271
> sealed descriptor-shape admission and in-place binder filling are integrated.
> The reused incremental build passed all 2,182 steps with 12 workers; all eight
> focused gates passed, including genuine builtin one-use receipts, sparse/full
> parity, O0/O2 dynamic pending negatives, repeated wide Logic4/Logic9, allocation,
> cache and schema coverage. Public ABI v2 / artifact 73 and tier limit 16,384
> remain unchanged. The new entry skips only signal/pending descriptor-shape
> scans; dynamic pending flags, keys, origins and site identity remain checked.
> Binder disassembly confirms its 192-byte aggregate copy is gone.
> One bounded r28 capture exited 0 with owned cleanup in 49.802 s and reached tick
> 825,000. All 418,776 trusted entries used the new shape proof. Full synchronization
> visited 24,007,104 members in 9,262 passes; selected synchronization visited
> 1,465,215 of 1,114,886,592 candidates in 430,126 passes. Boundary continuation
> admitted 378,005 and conservatively handled 9,252, all container-element aliases.
> Retained runtimes reused alias binding 403,436 / 406,071 times (99.35%).
> Fresh v5 object mapping attributes the retained dynamic initial-slot loop
> `[0x3000c, 0x302f2)` to 8.40% of postwave sampled period. The hottest body is
> 14.99%, dispatch 11.76%, atomic shared-pointer load 8.49%, publication 8.08%,
> binder 5.52% and backing-plane lookup 4.98%. Atomic loads are called by lease
> plane lookup; memmove self is now 0.03%, sampled in alias-certificate staging.
> These are current diagnostic rankings, not isolated savings or unbiased CPU.
> All 19 helper-inclusive shared objects contain 1,194,011 executable bytes
> (+511 versus r27), 206,347 optimized IR instructions (+19), and 3,106,576 ELF
> bytes (+504). Read-only data stays 1,498,536 bytes; relocations are 374,160.
> Retained SimIR stays 2,307 unique bodies / 354,050 operations / 25,491,600 outer
> vector capacity bytes in both generations. Cumulative planner release is
> 14,767,424 bytes across 47 materializations; it is not simultaneous RSS savings.
> Actual fsim sampled RSS peaks at 1,262,751,744 bytes; reported HWM is
> 1,300,160,512. Glibc in-use chunks are 891,507,664 / 1,138,420,608 bytes at
> generations 1 / 2, allocator bookkeeping rather than requested payload or RSS.
> All 50 captured source records, 39 dependencies and 315 inputs match after the
> run. Capture/source receipts remain immutable; documentation refresh is separate.
> CPU 9 was 100% idle with an empty executable lane at precheck, but compaction
> delayed actual launch roughly three minutes and that precheck was not refreshed.
> Sampling has 4,704 samples, zero LOST, and 1.893 s of recognized throttling.
> Instrumented tick progress and changed sample shares do not establish paired
> Wall improvement. Full canonical parity and paired throughput remain open.
> Build/profile workers always use GPT-6.1-Sol with high reasoning. No commit/push.
> Next bounded work is source proof for the dynamic pending loop and lease-local
> backing acquisition reuse, ranked by this same-run evidence; no new build or
> capture before review. Receipt:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache271-descriptor-shapes-binder-fill-normal-pmu-r28-analysis-receipt.json`.

> **Qualified r27 control checkpoint — 2026-10-06 03:43 UTC.**
> Cache 270 compiler compaction, pool COW reuse/reserve, planner storage retirement,
> builtin sparse synchronization, certified alias-bind reuse and narrow known-effects
> multiowner slice boundary continuation and shared COW operation overrides are
> integrated. Public JIT ABI v2 / artifact 73 and Less
> limit 16,384 are unchanged. Build/profile workers always use GPT-6.1-Sol with high
> reasoning. No commit/push is authorized. Full canonical parity and paired throughput
> remain open; profiling is separate from Wall qualification.
> Historical r18→r19 authenticates all 19 shared-body objects including helpers:
> executable bytes 3,566,831 → 1,193,500 (66.54% smaller), optimized LLVM IR
> 455,681 → 206,328 and ELF bytes 4,989,704 → 3,106,072 (37.75% smaller).
> Read-only data grows 1,156,232 → 1,498,536; relocations 245,424 → 374,184 bytes.
> Largest old/new structures match by rank and creation counts; r18 lacks exact
> module identities. Report: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-r18-r19-shared-body-comparison.md`.
> Singleton bodies remain direct. Retained region operations remain expanded;
> outlining alone claims no SimIR reduction. Repeated helpers pass O0/O2 at 8-bit
> Logic4 and widths 65/129/256/1024 Logic4 checked/trusted plus Generic Logic9
> checked across all nine states. Internal SystemVerilog Logic9 still falls back.
> Original generation-1 source views number 85,547, with only 1,425 shared backed
> bodies / 45,982 operations; 34,455 startup banks remain unmaterialized. Pool COW
> reuse removes 463 duplicated backend bodies / 15,778,944 vector-capacity bytes;
> exact reserve removes another 11,101,536 bytes. Both r27 generations retain
> 2,307 unique bodies / 354,050 operations / 25,491,600 outer-vector bytes, plus
> 1,529,224 boxed-group bytes and 55,410 common direct nested-vector bytes.
> Body accounting uses const immutable spans, excluding instance overrides and
> avoiding detachment/materialization. String buffers, nested element payloads
> and RareVector wrapper shells are excluded. Planner release is 14,767,424
> vector-capacity bytes cumulatively across 47 materialized lifetimes, not
> simultaneous resident savings. These lower bounds do not establish total RSS.
> The r27 default consumer rebuild passed 2,746 steps with 12 workers; the C++
> OperationList layout changed, while public JIT ABI/cache/artifact semantics did not.
> Seven focused gates passed: codec 0.05 s, manifest 2.03 s, steady allocation
> 1.29 s, preparation failure 0.43 s, generic update 2.74 s, generic failure 0.76 s,
> and final boundary 4.99 s. Test-only repairs fully qualify private casts, retain the
> runtime shared_ptr and detach parity snapshots instead of pinning writable planes.
> Original failure logs are preserved. No production guard was relaxed for fixtures.
> Checked/O0/O2 witnesses prove exact disjoint A4 owner masks and overlapping slice
> resolution to ZZZZXXXX without an A4 target slot, including signal/owner/history/
> transaction/process metadata. An otherwise identical unsealed current writer
> retains native execution and conservative full sync. Whole multiowner publications,
> unknown effects and unsupported routes remain conservative. Codec witnesses prove
> const override sharing, replace/mutable-copy isolation, nested/boxed payload
> isolation and unchanged artifact bytes. Earlier wide/Logic9 and real timing-hook
> callback witnesses remain gated; internal SV Logic9 still uses scheduler fallback.
> Freeze: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache270-multiowner-slice-override-cow-qualified-candidate-20261006/RECEIPT.capture-original.json`.
> All 50 source files are archived beside that immutable capture receipt; the 39
> dependency hashes and 315 original input paths/bytes are authenticated. Documentation
> refresh records are separate. Historical r24 original raw documentation hashes
> were unavailable after refresh; reconstructed docs are recovery evidence only.
> Bounded r27 exited 0 at tick 715000; owned cleanup 49.601 s, CPU 9 initially 100%
> idle. First wave was 13.369 s. Progress and startup are instrumented and unpaired,
> so neither proves Wall/throughput improvement. All 301 object paths/bytes match
> r26 exactly, retaining the 19-object helper-inclusive code/IR/section totals.
> Boundary decisions reconcile: 303,511 eligible / 7,499 conservative (97.59%
> eligible). Every remaining first rejection is container_element_alias; writer-count
> and all new owner/shape/seal rejection buckets are zero on this workload. The
> first-rejection map derives 69 reasons from the immutable archived header.
> Full sync is 7,509 passes / 19,463,328 visits: seed 10 / 25,920, boundary-only
> 7,499 / 19,437,408. Selected sync is 345,760 passes / 1,192,516 visits from
> 896,209,920 candidates. Overall avoided visits are 97.74%; only 2.13% of passes
> remain full. These are work counters, not elapsed-time savings. Historical r26
> rejected all 226,885 measured multiple-owner slices and avoided 11.86% of visits.
> Retained-only alias reuse is 321,663 / 323,792 attempts (99.34%) across 47 objects;
> full collectors 6,382, plane misses 230, unconfirmed-count misses 1,894. Retired
> generations are excluded; do not divide by global entry totals.
> Postwave weighted self ranking: one shared body 29.33%, dispatch 9.78%, boundary
> publication 7.06%, atomic shared_ptr load 6.91%, memmove 5.55%, backing plane_words
> 3.19%, alias sift 2.89%, component eligibility 2.24%, binder 1.54%, sync 0.737%.
> Inclusive host ranking: dispatch 61.32%, binder 18.13%, publication 17.49%,
> eligibility 4.61%, sync 0.955%; inclusive groups overlap and must not be summed.
> Sampled atomic load cost comes through FrontierWriteLease::plane_words; 98.56%
> of memmove self cost comes through the binder. Current lease lifetime proof and
> exact native hot-offset mapping are the next bounded source investigations.
> Sample quality: 4,690 samples, zero LOST, 1.877 s recognized throttle intervals.
> Period weighting does not fully correct missing samples; rankings are diagnostic.
> One authenticated fsim PID has 48 RSS samples, sampled peak 1,241,251,840 /
> reported HWM 1,300,025,344 bytes; last sample 48.452 s. No isolated RSS claim follows.
> Glibc pre-census G1 arena in-use/free is 891,515,520 / 69,365,120 bytes and mmap
> extent 64,442,368; G2 is 1,138,423,424 / 25,627,008 and mmap extent 64,450,560.
> Relative to r26, returned in-use chunk space is lower by 39,593,856 at G1 and
> 40,145,632 at G2. This batch combines runtime admission and override COW; counters
> include allocator bookkeeping/fragmentation and are neither requested payload nor
> causal RSS attribution. Original/cold state, instance sidecars and plane storage
> outside the existing census remain incompletely accounted. Do not sum generations.
> Both generations still have 47 runtime arenas, capacity 7,494,670 / used 7,489,544.
> Artifacts use `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache270-multiowner-slice-override-cow-normal-pmu-r27` with
> `-receipt.json`, `-sparse-planner-census.json`, `-r26-object-identity.json`,
> `-shared-body-section-census.json`, `-weighted-self.json`, `-sample-quality.json`,
> `-hot-symbol-ranking.json` and `-plane-and-memmove-callers.json`. The source-hashed
> map is `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-r27-boundary-reason-index-map.json`; reason combinations and
> arena/allocator/metadata generations strictly reconcile from non-truncated stderr.
> The separately instrumented Heaptrack raw attempt reached G2 at 33.756 s and
> drained cleanly in 35.091 s, exit 0. Wrapper, compressor and fsim share CPU 9;
> exact PID/start time was verified before SIGINT. All source/dependency identities
> match before/after. Raw trace is 191,051,280 bytes; zstd integrity passed in
> 0.95 s and interpretation in 9.89 s. The bounded whole-trace report did not finish:
> heaptrack_print hit its 45 s work deadline and cleaned up at 45.027 s, exit -15,
> with no residual group. Raw/interpreted traces and partial output are preserved.
> A single approved peak-only retry disabled Massif and other reports, explicitly
> disabled backtrace merging, and completed in 3.041 s. Whole-capture peak consumers
> include deferred LLVM executor construction 159.19M, snapshot rebuild 59.55M,
> process registration 44.96M, distinct operation-override copy stacks 24.88M,
> 24.88M, 23.57M and 23.29M, and two scheduler queue reserve stacks of 22.02M.
> These are Heaptrack's rounded display units, not exact byte counts; the table is
> a stack ranking, not a census of the G2 live set. Instance-override copies were
> excluded from the earlier immutable-body census. Printed heap peak is 1.17G;
> reported RSS 1.37G includes Heaptrack overhead. Exact G2 ownership and complete
> Massif output remain unavailable. No recapture or extended report is authorized.
> Artifacts use `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache270-heaptrack-g2-r26` with `-receipt.json`,
> `-qualification.json`, `-allocations-offline-analysis.json`,
> `-allocations-peak-only-analysis.json` and `-allocations-peak-summary.json`.
> Already-queued ANY fanout is idempotent under the full-trigger sentinel. Selective
> static-trigger regions and non-ANY edges are excluded; ranged ANY edges are admitted.
> Unknown callbacks, timing effects, wide targets and unsealed executors remain
> conservative. No further production edit or capture before the next bounded review.


> **Earlier source checkpoint — 2026-10-05 22:13 UTC.**
> Resumed by merging upstream `0ba4beb6` → `e4077991` with a fast-forward,
> then reconfiguring and fully rebuilding the reused Release Clang 22 tree
> with `--clean-first --parallel 12`; all 3748/3748 build steps passed. Upstream
> now builds the subsystems and four application components as shared libraries.
> All 23 tracked local changes and the intended canonical-values test remain;
> 22 tracked files and all unrelated untracked files are byte-identical to the
> premerge backup, while the manifest retains both upstream additions and the
> local test entry. Backup: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-upstream-merge-backup-20261005-154808`.
> Boundary and steady-allocation passed 2/2 in 5.49 seconds; compiler frontier,
> cache, schema and public ABI v1/v2 layouts passed 5/5 in 25.46 seconds.
> Build log: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-upstream-full-rebuild-20261005.log`. Qualified control:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache269-upstream-e4077991-qualified-control-20261005/RECEIPT.json`.
> The control copies six executables and all fifteen loaded build-tree DSOs,
> and hashes their full dependency closure. New launcher hash `af678854…`
> identifies only part of the program; every DSO identity is in the receipt.
> The permanent worker direction is GPT-6.1-Sol with high reasoning for all
> build and profile workers. Public JIT ABI v2/artifact 73/native cache 269.
> Canonical-value private compiler/runtime admission is integrated. Authentic
> O0/O2 boundary routes pass widths 1/65/129/256/1024 across all nine Logic9
> boundary states, including ordinary process-JIT observer demotion without
> extra native entry calls. Hidden internal Logic9 signals retain scheduler
> fallback at every width. Canonical test source hash `f6610c6d…` is unchanged.
> The earlier premerge control is
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-canonical-values-cache269-qualified-freeze.json`, binary `0e2d8d9d…`;
> its boundary gate passed in 4.05 seconds (owned cleanup 4.077).
> Premerge r17 is inconclusive under user-reported external VM host CPU
> contention: terminal -15, owned cleanup 55.400871 seconds, no first-wave
> marker or stdout. Census/plan/first-body markers arrived at 27.017/45.277/
> 51.160 seconds. Analysis: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-r16-r17-startup-attribution.json` and
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-r16-r17-artifact-identity.json`; all 315 input paths and bytes match.
> This establishes no paired Wall claim. Fresh-cache r18 on the merged build
> used CPU9 sampled 97% idle and exited zero at tick 455000/delta0 after the
> 49-second SIGINT; owned cleanup completed in 49.801505 seconds. All 39
> executable/DSO/dependency hashes matched the qualified control before and
> after capture. Census/plan/body/first-wave markers arrived at 6.448/8.609/
> 11.171/18.119 seconds. Receipt:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache269-upstream-normal-pmu-r18-receipt.json`; captured JIT map
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache269-upstream-normal-pmu-r18-jit-maps/perf-10.map` contains
> 1039 symbols. These are bounded instrumented partial-work observations.
> R18 calibrated postwave self is private native v4 27.537%, synchronization
> 14.423%, alias-range collection 8.248%, publication 4.493%, component entry
> 4.256%, and ProcessTable promotion 3.891%. The qualified fsim DSOs together
> account for 69.030%; the shared launcher is not the project CPU classifier.
> All 158796 entries use combined alias/canonical admission; all 955 geometry
> confirmations succeed, with zero checked, alias-only or canonical-only calls.
> RAW/MONO drift is 66.273 ppm; the after reading is 0.832 seconds after stop.
> There are 4705 samples, no lost records, and frequent throttle/unthrottle
> records (4705/4704, 1.927 seconds of recognized intervals). Period weighting
> is not a complete correction; rankings do not establish unbiased CPU or Wall.
> Attribution and quality use the r18 `-attribution.json` and `-sample-quality.json`
> artifacts. The next bounded action is exact fresh v4 object/disassembly
> remapping; historical v3 intervals must not be reused.
> Full canonical original/mixed parity, throughput and ratio qualification,
> and hosted CI remain open; no new commit or push is authorized by this resume.

> **Earlier source checkpoint — 2026-10-05 18:54 UTC.**
> Cache 268 now proves exact data/frame geometry in the builtin host with a
> preallocated index heap sort and exact stored/owner alias exception. Every
> unsupported geometry uses the public checked entry; frame/frame and all
> canonicality, shape, key, and mutation guards remain generated. The quadratic
> oracle, genuine checked decline/no-mutation witness, and all three optional
> allocation failures are covered. Reused production build passed 870/870;
> two repaired owning tests passed in 2.55 seconds (cleanup 2.573), completing
> all five affected gates with the three unchanged green gates. Receipt:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-sorted-proof-integration-receipt.json`; matching control:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-sorted-proof-cache268-control-r1/RECEIPT.json`.
> R16 used original inputs and a fresh cache on CPU9 sampled 96.97% idle,
> exited zero at tick 465000/delta0, and completed owned cleanup in 50.202 seconds.
> Binary `a28e8f7a…`; calibrated postwave self is native 31.719%, synchronization
> 14.249%, collection 7.119%, and heap sift 1.564%. Exact alias-inner intervals
> have zero samples, versus r15 5.696%; pending-write shape guards are 4.892%,
> pending value tails
> 5.332%, and initial-slot checks 3.199%, with native 18.295% still unclassified.
> Mapping correction: `[0x2ecdd,0x2ee5f)` is the 2592-site structural pending-write
> shape loop, previously mislabeled signal-tail in historical attribution.
> Actual signal-value tails occupy `[0x2f93c,0x2fbce)` and are within the residual
> share above. Those structural shape guards remain mandatory; the earlier
> canonical-value savings ceiling was overstated.
> Checked/trusted calls are 0/164702 and every one of 993 confirmations succeeds.
> Bounded initial rows prove actual successful host geometry admission.
> Attribution stem `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-sorted-proof-normal-pmu-r16`; RAW/MONO drift
> 51.61 ppm, after-read 1.263 seconds after SIGINT. Different partial work and
> instrumentation preclude a paired Wall claim. Public JIT ABI/artifact 73 and
> native cache 268 remain unchanged. Full canonical parity and final throughput
> qualification remain open. Canonical-value private-entry compiler/runtime
> packages are reviewed scratch work only; integration and authentic Logic9
> route validation are the next source action after this commit boundary.
> The user authorized commit/push after r16 and waived additional checks;
> no further builds, tests, captures, or hosted CI monitoring are part of closure.
> **Earlier source checkpoint — 2026-10-05 18:22 UTC.**
> The exact debug token now lives in ProcessState, preserving all writer clears
> and generation-first target matching while avoiding cold-sidecar loads on hits.
> Measured private sizes are ProcessState 88→104 and ProcessColdState 896→880;
> total storage is unchanged. Reused build 932/932 and all five affected gates
> passed in 6.93 seconds (cleanup 6.986), including the migrated authentic token
> witness and complete O0/O2 steady windows. Binary `5c9bbe37…`; composition
> receipt `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-debug-token-inline-integration-receipt.json`, matching control
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-debug-token-inline-cache268-control-r1/RECEIPT.json`.
> Fresh CPU9-at-100%-idle r15 exited zero at tick 455000/delta0 with cleanup
> 49.601 seconds. Calibrated state-sync self is 13.366% (19.580 billion sampled
> user cycles), versus r14 20.198% (30.101 billion); memcmp is 0.099% and
> SourceLocation equality has no samples. Private native is 37.138%, collection
> 7.638%, dispatch 5.061%. Mapped native alias inner is 5.696%,
> 5.286% for pending-write shape guards (historically mislabeled signal tails),
> pending value tails 4.706%, initial slots 2.407%; 19.044% remains unclassified.
> Checked/trusted calls are 947/151530 with every confirmation successful.
> R15 attribution uses `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-debug-token-inline-normal-pmu-r15`; calibration
> drift is 62.3 ppm with after-read 0.661 seconds after stop. R14/r15 perform
> different partial work, so this is call-path evidence without a paired Wall
> result. Sorted geometry source is reviewed but remains unintegrated pending
> its oracle/fallback witness; private canonical-value admission is under source
> review. Public JIT ABI/artifact 73/cache 268 unchanged. Full canonical parity
> and final throughput/ratio qualification remain open.

> **Earlier source checkpoint — 2026-10-05 18:01 UTC.**
> Cache 268 now skips repeated debug source/scope comparisons only when a
> process holds the exact immutable native debug target and runtime generation.
> Every ordinary debug writer and lifecycle replacement clears that private
> token before changing metadata; native sync records it after validated copy.
> Public JIT ABI, artifact schema 73 and emitted code are unchanged. Reused
> build passed 932/932; five affected gates passed in 6.84 seconds. The authentic
> token witness then passed the boundary gate in 2.10 seconds (cleanup 2.121):
> real native priming/repeated sync, checked DebugPoint invalidation before its
> hook, unchanged signal/frame/queue state and subsequent native restoration.
> Its optimized process executor skips DebugPoints, so the probe uses the real
> checked boundary handler with the retained instruction rather than executing
> signal writes. Final test source `d6d53193…`; binary `c704c066…`.
> Source/witness receipts: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-debug-sync-token-integration-receipt.json`
> and `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-debug-token-witness-r2-integration-receipt.json`. Matching
> executable/DSOs/tests control:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-debug-sync-token-cache268-control-r1/RECEIPT.json`.
> Corrected-startup r13 and token r14 are bounded instrumented diagnostics.
> Fresh r14 used CPU9 at 100% idle, exited zero after SIGINT at tick 435000,
> and completed owned cleanup in 49.802 seconds. Calibrated post-wave self
> memcmp fell from 12.961% to 0.164% (395 to 5 samples); SourceLocation
> equality fell from 4.030% to zero (123 to 0). State sync is now 20.198%,
> dominated by two token-generation comparisons (6.811% and 4.584%); their
> dependent cold loads require source-backed locality work, not guard removal.
> Private native is 32.912%: mapped alias inner 5.386%, signal-tail loop 5.001%,
> pending-tail loop 4.027% and pending-slot preflight 2.557%; 15.941% remains
> unclassified. Checked/trusted calls are 852/143136 and all 852 confirmations
> succeeded. RAW/MONO drift is 67.7 ppm, after-clock captured 0.862 seconds
> after stop. Exact attribution uses `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-debug-sync-token-normal-pmu-r14`
> and `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-r13-r14-debug-token-attribution.json`. R13/r14 perform different
> partial work (ticks 415000/435000); no paired Wall or full canonical parity
> result is established. Next coherent candidates are hot token-field locality
> and conservative sorted host geometry proof with focused fallback/oracle
> tests. Full parity, both <=15-second medians and the Verilog/mixed ratio
> qualification remain open.

> **Earlier source checkpoint — 2026-10-05 17:20 UTC.**
> Cache 268 now retains independently checked exact task-count keys 0–64
> only while context and every non-task range remain identical. Unknown counts
> use the checked entry; any other geometry change clears all keys. Immutable
> output indexes replace repeated eligibility scans only for authenticated
> kernel identities, retaining the original fallback and mutable guards.
> The combined reused build passed 932/932. Runtime, boundary and both
> optional-capability app gates passed; boundary coverage includes an actual
> checked zero-task companion, multiple exact keys, unknown-key preservation
> and simultaneous task/other-range invalidation. All O0/O2 width 1/65/129
> steady allocation/parity windows passed. The final generic bridge assertion
> initially failed on a preexisting startup/LAST defect, also reproduced with
> both core changes reversed. Passive diagnostics showed initialized direct
> input 0 with pending/public X and output X before any native dispatch. The
> source fix materializes pending direct values before grouped static fanout
> compares packed current/previous, including external boundary inputs.
> Natural bridge RED became GREEN without assertion changes: reused build
> 867/867, runtime/entire steady gate passed in 1.86 seconds (cleanup 1.870),
> boundary gate passed in 2.10 seconds (cleanup 2.121). Fix source `fd31ebae…`,
> binary `e56cbae0…`; receipt `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-startup-static-wake-integration-receipt.json`.
> Permanent initial-frame parity then passed all O0/O2 widths 1/65/129
> in the full steady gate (1.28 seconds, cleanup 1.319); temporary observation
> diagnostics were removed. Green control:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-startup-static-wake-cache268-control-r1/RECEIPT.json`.
> Source receipts: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache268-exact-count-output-index-integration-receipt.json`
> and `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-steady-bridge-parity-diagnostic-source-receipt.json`.
> Fresh r12 used CPU9 at 100% idle, exited zero after SIGINT at tick 415000,
> and completed owned cleanup in 50.602 seconds. Binary `9282048f…`; frozen
> control `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-exact-count-output-index-cache268-control-r1/RECEIPT.json`.
> Checked/trusted calls are 759/129019 (r11 2989/84887); all 759 confirmations
> succeeded. Misses are 10 unprimed, 669 task-only and 80 task-plus-other;
> first-16 detail rows contain task-only cases, leaving mixed tuple causes unknown.
> Calibrated post-wave self CPU is private native 28.904%, state sync 13.996%,
> memcmp 11.789%, range collection 5.359%, SourceLocation equality 4.436%
> and eligibility 1.285%. Resolved stacks charge 11.624 percentage points of
> memcmp to string equality inside state sync, plus all SourceLocation equality.
> Sixteen budget samples show actual site fanout 64, matching the global bound;
> per-site refinement would not help those samples. Proven pair inner/resolver
> samples are 3.639% of post-wave self CPU; remaining native paths are not
> charged to aliases without mapping. Next: repeated exact debug-state
> comparisons have the largest proven opportunity (23.505% sampled ceiling),
> while mapping residual native guards before changing geometry validation.
> Receipt and calibrated attribution use the
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-count-set-output-index-normal-pmu-r12` stem. RAW/MONO
> calibration measured 60.6 ppm drift; the after reading was 46.5 seconds after
> stop. R11/r12 perform different instrumented work; no paired Wall, full parity
> or final throughput result is established. Public JIT ABI/artifact 73 unchanged.

> **Earlier source checkpoint — 2026-10-05 16:05 UTC.**
> Cache 268 adds a source-private SV trusted entry and a runtime-owned exact
> range certificate. Checked/public/custom entries retain full alias geometry;
> trusted entries skip only nested geometry after exact current-inventory
> equality. Frame/frame, generation, identity, shape, tail, key and mutation
> guards remain active. Zero pre-call staged events and a post-geometry status
> plus unchanged inventory are required to prime; mismatches and invalidation
> clear the certificate. Public JIT ABI and artifact schema 73 are unchanged.
> The reused source build passed after a fixture namespace qualification;
> five focused gates passed, then the compiler gate passed in 23.96 seconds
> (cleanup 23.991) after correcting its padded-tail word and private-thunk
> signature fixtures. Checked/trusted O0/O2 full-state and rejection parity,
> an authentic runtime trusted hit, task-extent fallback and early-event
> non-priming are covered. Integration pins/repairs:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache268-alias-integration-receipt.json`; binary `d371dce7…`.
> Frozen executable/DSOs: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-certificate-cache268-control-r1/RECEIPT.json`.
> Fresh r10 used CPU9 at 95.0% idle, cleanly stopped after SIGINT at tick
> 355000/delta0, exited zero and completed owned cleanup in 49.801 seconds.
> First body/snapshot/wave markers arrived at 9.555/14.150/16.048 seconds.
> Actual trusted entries are proven by bounded rows. Two full snapshots and
> 13 successful recertification attempts are distinct counts; the second
> snapshot followed signal observation. Calibrated post-wave self CPU is
> 42.272% private native body, 9.706% state synchronization, 8.813% eligibility
> and 3.893% exact-range collection. Native hot IPs still cluster in the alias
> pair neighborhood, requiring mapping and aggregate miss attribution before
> any certificate policy change. The interrupted instrumented runs perform
> different work and establish no paired Wall, final parity or throughput result.
> Receipt/attribution: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-validation-normal-pmu-r10-receipt.json`
> and `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-validation-normal-pmu-r10-weighted-self.json`.
> Profile-only aggregate miss hooks and the source/scope equality trial are
> now integrated; reused fsim/runtime/boundary build passed 918/918 and two
> affected gates passed in 2.85 seconds, cleanup 2.874; binary `de64b128…`.
> Fresh r11 used CPU9 at 100% idle and exited zero after SIGINT at tick
> 355000/delta0, cleanup 49.801 seconds. Aggregate calls are checked 2,989
> and trusted 84,887; all 2,989 confirmations succeeded. Misses are unprimed
> 10, task-extent-only 2,929, task extent plus other inventory changes 7,
> and pointer changes 43; context/collector/other-extent/tag misses are zero.
> Task-only classification compares every remaining already-collected tuple;
> earlier tuples are equal. Thus 97.993% of checked calls change only actual
> task bytes, despite a 96.599% overall trusted-call rate. Simultaneous other
> changes must invalidate any future count cache. Census:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-miss-normal-pmu-r11-alias-census.json`.
> Calibrated r11 self CPU is native body 39.694%, eligibility 9.055%, state
> synchronization 7.973% and collector 4.924%. The equality trial adds visible
> memcmp 9.283% and SourceLocation equality 3.138%; no saving is established.
> Next: exact validated-task-count reuse with unchanged non-task inventory,
> subject to source review; retain checked fallback and all geometry guards.
> Canonical parity and targets remain open. Earlier checkpoints preserve evidence.

> **Earlier source checkpoint — 2026-10-05 14:40 UTC.**
> Cache 267 adds private shape-specific stage-write helpers while retaining
> original site order, scalar coordinates, payloads and the inline path for
> payloads above 16 words. Its whole-module IR/startup gain is not yet measured.
> Public JIT ABI and artifact schema 73 remain unchanged.
> Host dispatch now selects a contiguous prefix within the certified staged
> event budget; every original offered token still passes full authentication,
> and the untouched suffix remains queued. The natural 31-commit regression
> failed on the old host, then passed checked/O0/O2 parity with the same valid
> native runtime after selecting 21 commits within the 64-event frame.
> The reused combined build passed 948/948 and six focused gates passed.
> Control: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-prefix-budget-stage-helper-cache267-control-r1/RECEIPT.json`.
> The subsequent instrumented production r8 exited with a staged-key issuance
> error after 18.204 seconds, so it provides no throughput or parity result.
> A fresh CPU9-at-100%-idle exact-term capture proved the first failing event
> was a valid 1-bit SV boundary site into an 8-bit signal: all nine terms were
> checked and only width equality failed (`checked_mask=511`, `failed_mask=32`).
> Receipt: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-staged-issue-rejection-capture-r2-receipt.json`, cleanup
> 16.926 seconds. The host now accepts the certified boundary slice envelope,
> retaining exact internal widths and every site/payload/shape/origin guard.
> The affected reused build passed 863/863; runtime gates passed 2/2 in
> 2.34 seconds, cleanup 2.372 seconds. Binary: `ce24199e…`.
> Fresh r9 passed the former rejection without an issue error, but required
> supervisor TERM at 55 seconds after SIGINT at 49 seconds; cleanup was
> 55.201 seconds and no final tick or parity result was produced. It used
> CPU9 at 100% idle and reached the first wave at 18.479 seconds.
> Calibrated post-wave samples place 73.094% self CPU in the generated body,
> with hot IPs mapping to its alias pair loop, and 18.000% in eligibility.
> Five-second bins show repeated returns
> between these paths through the interrupt interval, rather than one stuck
> generated call. Attribution: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-partial-boundary-host-normal-pmu-r9-weighted-self.json`.
> The real-host slice witness now passes checked/O0/O2 full-state parity and
> retains the native runtime for nonzero-offset 8→1-bit and 129→65-bit writes.
> Its reused boundary build passed 743/743; the owning gate passed in
> 2.03 seconds, cleanup 2.071 seconds (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-partial-boundary-host-test-ctest.log`).
> Eligibility now uses the existing bounds-checked snapshot component map
> instead of searching certificate members for every fanout edge. The graph
> and partition map publish together; mutable eligibility guards and callback
> rechecks are retained. Its reused build passed 866/866 and three affected
> gates passed in 5.45 seconds, cleanup 5.483 seconds; binary `747ff859…`.
> No new simulation or timing claim accompanies this host-only change.
> Generated alias validation is the next measured runtime target. Canonical
> parity and paired Wall targets remain open. Earlier checkpoints preserve evidence.

> **Earlier source checkpoint — 2026-10-05 13:16 UTC.**
> Cache 266 factors signal identity/shape/pointer validation and canonical
> value-plane checks into immutable descriptor loops. Exact host range order,
> alias exceptions, physical binding authentication, Logic4 tails and Logic9
> reserved-code checks remain intact before any frame mutation. New witnesses
> cover every internal role, boundary and pending plane at partial widths
> 65/129, with positive controls and unchanged-state declines; aligned width
> 64 is covered for both value families. Public JIT ABI and artifact schema 73
> are unchanged. The reused seven-target build passed 966/966; six focused
> gates passed in 28.29 seconds, cleanup 28.303 seconds.
> First-body raw instructions fell 1,070,394→929,809 (13.134%) and bytes
> 61,320,913→53,276,069 (13.119%); blocks increased 13,017→13,068.
> Known/four-state member instruction families are unchanged. Raw diagnostic
> selected CPU9 at 100% idle and cleaned up in 8.472 seconds; comparison:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-validation-loops-cache266-raw-ir-comparison.json`.
> The opt-in operation-body census found 131 candidate templates among the
> first 2,625 members: 65 groups of 32, 65 singletons and one group of 480.
> All 2,625 strict body-coordinate shapes are distinct. Template keys are
> diagnostic evidence and provide no admission or sharing proof.
> One normal PMU/cause capture used CPU9 at 98.020% idle, exited orderly
> with cleanup in 50.002 seconds at tick 495001, and reached 28 snapshots.
> Plan/first-body/first-wave markers arrived at 7.704/12.872/22.576 seconds.
> These partial instrumented phases establish no paired Wall result.
> All 26 value-only declines reported frontier-runtime-or-frame-state at
> component 152; 26 resulting full-snapshot requests came from recertification
> and one additional request came from its scheduler path. Every transition
> reported authoritative waiting zero, and all 28 seed summaries reported
> zero failures. The failed-seed/global-waiting hypothesis is unsupported in
> this capture. Cause census:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-validation-causes-normal-pmu-r7-cause-census.json`.
> A narrow profile-only detail row now records the failed guard's concrete
> state without changing its predicate or querying virtual layout again.
> Its reused fsim/frontier/runtime build passed 879/879; two focused gates
> passed in 24.08 seconds, cleanup 24.140 seconds. Current fsim SHA-256:
> `e9813cefd67e532d9a173383647ed07589596e3cc5b155893bb86c7cc9ac6ee4`.
> Current executable and DSOs are frozen at
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-native-entry-rejection-cache266-control-r1/RECEIPT.json`.
> The preceding cache266 executable `960e2206…` and DSOs are frozen at
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-validation-loops-cache266-control-r1/RECEIPT.json`.
> A fresh CPU9-at-100%-idle capture stopped on the first detailed rejection
> and cleaned up in 22.843 seconds. Component 152 was invalidated; backend,
> executor, owner, state and runtime/frame generations matched, in-use/stop
> were zero, task cursor was zero with 32 tasks, and all pending/staged/
> committed/ack/current-changed counts were zero. The predicate still declined
> exactly as before. Receipt:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-frontier-rebind-detail-capture-r1-receipt.json`.
> Private first-transition tracing now covers all 39 typed V2 runtime
> invalidation writes, preserving every guard and mutation order. It adds no
> object field, cache identity or environment read; readiness helpers forward
> the consuming callsite. The origin build passed 935/935, focused gates 2/2
> in 24.04 seconds, cleanup 24.094 seconds. Its first-detail capture used
> CPU9 at 100% idle and cleaned up in 22.760 seconds. Components
> 152/297/450/620/807 all invalidated at the generated-entry rejection branch,
> before readiness retirement. Runtime-only replacement is therefore parked.
> A dispatch-only status row then rebuilt successfully (879/879), with two
> focused gates in 24.03 seconds, cleanup 24.092 seconds. Its capture selected
> CPU3 at 100% idle because CPU9 was only 81.818% idle; cleanup took 22.601
> seconds. All five returns were decline-before-mutation (status 5), with
> exact generation pairs matching and zero physical-binding mismatches.
> Immediate rejection had 32 pending writes, cursor zero/count 32, no staged
> or committed entries, invalid current-member/current-pending indices, and
> saved body PC 2511. Checked fallback later drained pending count to zero;
> these are distinct observation points. Expected runtime/scheduler labels
> in the row denote owner/frontier context; direct generated comparisons use
> frame runtime/bound and scheduler/cut pairs, both recorded.
> Receipt: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-native-entry-rejection-capture-r1-receipt.json`.
> A temporary, separately keyed predecessor-status diagnostic then joined
> all five status-5 returns to block 17, `validate.write.capacity`. Component
> 152 mapped to a 2,592-write shared body; immediate pending/task counts were
> both 32. First active-write and prefix-head task scalars agreed on the
> current internal-commit key, but those context rows do not identify the
> failing descriptor. CPU9 was 97% idle; owned cleanup took 22.459 seconds.
> Receipt: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-native-guarddiag-capture-r1-receipt.json`.
> The exact scratch patch was reversed and all three normal source hashes
> verified. The reused restoration build passed 882/882, reproduced the
> normal `e9813cef…` executable hash, and frontier/runtime gates passed 2/2
> in 24.03 seconds, cleanup 24.044 seconds. Scratch cache objects remain
> isolated, retained solely as non-reusable diagnostic evidence.
> Source bounds identify the cumulative staged-event term: event capacity
> is capped at 64, while each offered internal commit adds the global maximum
> fanout bound. Pending capacity and committed capacity are 2,592; the
> initial 32 pending entries and at most 32 offered tasks exclude their
> overflow/underflow terms after prior successful preflight checks.
> Next dependency is a certified contiguous native prefix that fits the
> staged-event budget, leaving its suffix in the scheduler and preserving
> all generated guards. Full canonical
> Verilog/mixed parity, paired Wall, 15-second/five-percent targets, hosted
> CI and the recorded `fixed_array_write.sv` XSim divergence remain open.

> **Historical source checkpoint — 2026-10-05 12:03 UTC.**
> Cache 265 replaces the per-write-site initial-slot CFG expansion with one
> ascending loop over immutable descriptors. Active/inactive behavior, flags,
> full site identity, commit/origin keys, SystemVerilog active phase, local
> slot/count effects, and no-mutation declines remain exact. New witnesses
> cover unknown/opposite/committed internal flags, a valid all-active baseline,
> and corrupted later-row rejection with unchanged owned state.
> The reused seven-target build passed after a narrow test-only owned-task
> setter repair (890/890). Six focused gates passed in 26.14 seconds with
> complete cleanup in 26.200 seconds. Public JIT ABI and artifact schema 73
> are unchanged. Current fsim SHA-256 is
> `23324ba03f9dd16d6b356106ea4fc5b003ccaf7f7f18b7a688911711eabad311`;
> executable and DSOs are frozen at
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-initial-site-loop-cache265-control-r1/RECEIPT.json`.
> A strictly idle CPU9 raw-IR diagnostic stopped at the first closed dump,
> with cleanup in 9.317 seconds. Exact first-body raw instructions fell
> 1,464,048→1,070,394 (26.888%), blocks 23,510→13,017 (44.632%), and bytes
> 82,884,568→61,320,913 (26.016%). Initial-slot labels fell from 10,500
> blocks/393,844 instructions to six blocks/191 instructions; known/four-state
> member families are unchanged. Comparison:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-initial-site-loop-cache265-raw-ir-comparison.json`.
> One normal fresh-cache PMU capture used CPU9 at 100% idle, exited orderly
> after SIGINT49, and completed cleanup in 50.002 seconds at tick 455001.
> Plan/first-body/first-wave markers arrived at 8.808/14.980/26.206 seconds;
> plan-to-first-wave spans 17.398 seconds versus 24.948 in the busier r5 run.
> Those phase locations are attribution cues, not paired Wall evidence.
> The calibrated 20.796-second post-wave window resolves recertification
> at 53.693% inclusive sampled CPU, full snapshot build at 48.382%, snapshot
> preparation at 28.521%, component construction at 21.513%, graph build at
> 12.562%, and process materialization at 11.847%. These shares overlap.
> Runtime reaches 24 snapshots versus 16 previously. Hot native body self is
> 2.652%; its native size is 3,889,152 bytes. First startup body size is
> 3,928,958 bytes; it remains a separate identity.
> Attribution: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-initial-site-loop-normal-pmu-r6-{receipt.json,weighted-self.json,inclusive-attribution.json,short-helper-callers.json}`.
> Next work traces why full recertification is required, retaining all rebind
> and invalidation guards. Signal/tail loops and member-shape diagnostics are
> scratch proposals pending review. Full canonical Verilog/mixed parity,
> paired Wall, the 15-second/five-percent targets, hosted CI, and the recorded
> `fixed_array_write.sv` XSim divergence remain open.

> **Historical source checkpoint — 2026-10-05 11:33 UTC.**
> The existing opt-in raw-frontier IR dump now also covers the verified
> shared body before optimization. It reuses the same closed-file helper and
> emits a generic shared-body completion marker; normal IR, cache identity,
> native cache 264, public JIT ABI, and artifact schema 73 are unchanged.
> The reused fsim/compiler-frontier build passed 763/763; the existing
> compiler-frontier gate passed with cleanup in 23.042 seconds.
> Current fsim SHA-256 is
> `237b5e473408cec13a4ebee5b87002fba07a661c6ac483b43127c881d1f5b724`.
> The preceding candidate is frozen with its DSOs at
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-graph-debugpoint-skip-control-r1/RECEIPT.json`.
> One no-perf fresh-cache diagnostic selected CPU9 at 99.010% idle, observed
> the first successful shared-body dump at 10.370 seconds, and terminated
> its owned group immediately; cleanup completed in 10.493 seconds.
> Receipt: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-shared-raw-ir-census-r1-receipt.json`.
> The 82,884,568-byte verified raw IR has SHA-256
> `38634596a93f1278d722ead593b84cd0eccefd211212df07ef803a7bae77e7e9`:
> one function, 23,510 blocks, and 1,464,048 instructions. Explicit labels
> identify 2,625 known-Logic4 blocks containing 327,548 instructions and
> 2,625 four-state blocks containing 380,058. Initial-slot active blocks
> contain another 354,471 instructions; their per-member ownership remains
> unclassified by labels. Census: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-shared-raw-ir-census-r1-text.json`.
> This is the first 2,625-member startup body `f597f8f1…`, present in the r5
> map at 5,393,389 native bytes; it differs from the hot runtime body
> `3715b764…` at 5,335,767 bytes. Do not transfer proportions between them.
> Next work source-maps duplicated compilation work before choosing an
> optimization. This diagnostic establishes no parity or paired Wall result;
> all qualification targets and the recorded XSim divergence remain open.

> **Historical source checkpoint — 2026-10-05 11:16 UTC.**
> Graph construction borrows the effective Fork operation for branch-index
> discovery and skips DebugPoint copies in ownership and inventory scans.
> Component construction skips DebugPoint copies in eleven analysis scans;
> original instruction indices, width resets, emission, and final debug
> metadata remain intact. The debug-marker witness now shares immutable
> operation bodies before graph construction and checks instance source/scope
> overlays in final debug state and retained kernel markers.
> Native cache 264, public JIT ABI, and artifact schema 73 are unchanged.
> The reused seven-target Release Clang22 build passed 892/892; six focused
> CTests passed in 27.49 seconds with complete cleanup in 27.551 seconds
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-graph-debugpoint-skip-ctest.log`). Current fsim SHA-256 is
> `254ef492ae44b1c9a6bce14d191cd4e7fcc7a1a1efc96df9e51a6b298a48ee4a`.
> One original-snapshot fresh-cache CPU9 PMU capture exited orderly after
> SIGINT49 in 49.801 seconds at tick 375001, reaching 16 snapshots versus
> 14 previously. CPU9's one-second precheck was only 79.6% idle, materially
> busier than the prior screen; this capture supports attribution only.
> Calibrated post-wave DebugPoint copy self fell from 7.011% to 2.182%;
> all 32 matching remaining copy samples reach component construction
> (1.141%) or snapshot process materialization (1.040%), with none reaching
> graph inventory. DebugPoint destruction is 1.952% versus 3.418%.
> malloc remains 9.299%, free 5.747%, and the shared native body 3.116%.
> Plan to first simulation marker still spans 24.948 seconds. Its guarded
> CPU window is 85.069% libLLVM, 9.678% fsim, and 4.729% libc; unresolved
> startup symbols mostly map to libLLVM, and bounded DWARF stacks generally
> do not recover the fsim caller. First shared-body materialization arrives
> 9.741 seconds after the plan census. Next work targets the largest absolute
> JIT/startup cost while retaining the remaining copy paths as measured leads.
> Attribution: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-graph-debugpoint-skip-normal-pmu-r5-{receipt.json,weighted-self.json,caller-attribution.json}`.
> Different work, instrumented execution, and the busy-core precheck prevent
> paired Wall conclusions. Future screens require a freshly idle nonzero
> core, preferably at least 95% idle over one second, without host changes.
> Full canonical Verilog/mixed parity, paired Wall, the 15-second/five-percent
> targets, hosted CI, and `fixed_array_write.sv` XSim divergence remain open.

> **Historical source checkpoint — 2026-10-05 10:57 UTC.**
> The host lookup batch records private RegionGraph topological ranks and
> sorts validated component members by those ranks. It replaces two unseeded
> per-member layout searches with the existing current-generation readiness
> map, retaining complete row, bounds, identity, epoch, and ordering checks.
> Existing non-ID topology and cycle witnesses pass; new duplicate/swapped
> layout-row admission witnesses reject malformed providers before runtime
> publication. Native cache remains 264; public JIT ABI and artifact schema 73 are
> unchanged. The reused seven-target Release Clang22 build passed 983/983;
> six focused CTests passed in 25.54 seconds with cleanup in 25.595 seconds
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-host-topology-member-map-ctest.log`). Current fsim SHA-256 is
> `fdf4817f5aec776c5e517dee7443252f3d0184ab52812ab702d218222e77d9ff`.
> The prior cache-264 candidate and this host candidate are frozen at
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache264-pending-collapse-control-r1/RECEIPT.json` and
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-host-topology-member-map-control-r1/RECEIPT.json`.
> One original-snapshot fresh-cache CPU9 PMU capture exited orderly after
> SIGINT49 in 49.801 seconds at tick 365000. Startup was delayed relative to
> the prior capture: census 6.555 versus 5.306 seconds, plan 8.980 versus 7.436,
> and first simulation marker 34.390 versus 31.662. The separately calibrated
> post-wave phase places base graph program construction at 1.978% versus
> 9.861% sampled CPU, and frontier dispatch at 0.742% versus 8.883%; the two
> measured repeated searches have been removed. The shared body retains
> the same identity and 5,335,767-byte size, at 3.026% of sampled CPU.
> Allocation and operation copying now lead: malloc 7.579%, free 7.466%, and
> DebugPoint copying 7.011%. All 88 matching copy samples unwind to component
> construction/expanded operations (3.038%), graph inventory/expanded
> operations (2.501%), or snapshot process materialization (1.472%).
> The candidate reaches 14 snapshots versus 15 previously; phase shares and
> interrupted ticks are not equal-work or paired Wall evidence. Attribution:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-host-topology-member-map-normal-pmu-r4-{receipt.json,weighted-self.json,debugpoint-copy-callers.json}`.
> Next bounded source work targets allocation and these proved copy paths.
> Full canonical Verilog/mixed parity, paired Wall, the 15-second/five-percent
> targets, hosted CI, and `fixed_array_write.sv` XSim divergence remain open.

> **Historical source checkpoint — 2026-10-05 10:34 UTC.**
> Cache 264 certifies the generated V2 pending-plane descriptor suffix in
> linear time, then checks its bounding range against every signal and frame
> range. A successful proof selects the signal prefix for the existing exact
> alias checks; any failed proof selects the original full table. All public
> shape, pointer, alignment, stored/owner alias, and no-mutation guards remain.
> Contiguous, padded-gap fallback, reverse-order fallback, overlap, and aligned
> pending-pointer overflow witnesses passed. Public ABI and artifact schema 73
> are unchanged. The reused six-target Release Clang22 build passed 893/893;
> the five focused CTests passed in 24.13 seconds, with complete process-group
> cleanup in 24.193 seconds (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v2-pending-collapse-cache264-ctest.log`).
> Current fsim SHA-256 is
> `78f8c479e87353acfb99d5150e58e740c306aeab9069bd0790b4ac0d4f8d89bd`;
> cache-263 fsim and its in-tree shared libraries are frozen at
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache263-preamble-cursor-control-r1/RECEIPT.json`.
> One original-snapshot fresh-cache CPU9 PMU capture exited orderly after
> SIGINT49 in 49.801 seconds at tick 375000; cache 263 reached tick 335001.
> First materialization, snapshot, and simulation markers arrived at
> 17.030, 29.790, and 31.662 seconds, versus 16.535, 29.010, and 30.867.
> In the separately calibrated post-wave phase, the formerly hot shared
> body accounts for 3.164% of period-weighted sampled CPU versus 31.26% in
> cache 263. Graph program construction accounts for 9.861%, frontier
> dispatch 8.883%, malloc 5.976%, and free 5.469%; DebugPoint operation
> copying and destruction account for 4.842% and 3.425%. Exact host IPs map
> to the topological-order member-set search and the unseeded layout-member
> lookup. The candidate reaches 15 snapshots versus 12, so these phase
> shares and interrupted ticks are not equal-work or paired Wall evidence.
> Receipt and attribution:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v2-pending-collapse-normal-pmu-r3-{receipt.json,weighted-self.json}`.
> Full canonical Verilog/mixed parity, paired Wall, the 15-second/five-percent
> targets, hosted CI, and the separate `fixed_array_write.sv` XSim divergence
> remain open. Local candidate/control Release builds have no IPO.

> **Historical source checkpoint — 2026-10-05 09:53 UTC.**
> Cache schema 263 adds a shared V2 member state/origin/readiness preamble
> and a monotonic output-binding cursor. The reused Release build passed,
> as did five focused compiler, runtime, app, cache, and schema gates.
> Current fsim is
> `d719c729`; the cache-262 executable `078addfd` and its shared libraries
> are frozen separately. One matched-stimulus, fresh-cache, interrupted PMU
> screen reached its first simulation marker at 30.867 seconds versus
> 39.028 seconds for cache 262, but it does not establish Wall throughput or
> full parity. Saved native-object disassembly attributes the largest
> post-wave body self-sample cluster to quadratic pointer-range/alias-pair
> validation; that is the next measured runtime target. Exact receipts and
> limitations are in `docs/performance-resume.md`. The newly frozen cache-262
> comparison and cache-263 candidate are both local no-IPO Release builds;
> the separate `ede7c24e` architecture control uses ThinLTO.

> **Historical source checkpoint — 2026-10-05 09:11 UTC.**
> Certified V2 plans now share one native body under an immutable per-plan
> physical binding and exact public entry wrapper. Public V2 ABI and provider
> vtables are unchanged; native object cache schema is 262. Separate certified
> body identity authorizes sharing while exact wrapper identity, final frame
> authentication, and checked fallback remain. The reused feature build and
> app 65/129-component witness passed; six cache/schema/ABI/observer/boundary
> controls and the expanded compiler frontier suite passed. The compiler
> suite took 19.66 seconds after a test-only Generic fanout-mask correction.
> The fresh original-snapshot, SV-wave-instrumented run materialized 47 V2
> plans using 19 native bodies (28 reuses), then reached tick 275001 before
> orderly SIGINT49 termination in 50.401 seconds. This is interrupted
> progress, not full canonical parity or a Wall result. Exact receipts,
> source pins, remaining qualification, and the pending phase-aware PMU are
> recorded in `docs/performance-resume.md`.

> **Historical source checkpoint — 2026-10-05 07:42 UTC.**
> V2 preparation is now staged through an additive optional provider
> capability. An owning compiler plan exposes its typed layout for pure
> runtime preflight before LLVM materialization; final binding still
> authenticates the real entry, and legacy providers retain their factory
> path. No JIT C ABI layout or physical cache identity changed; cache schema
> remains 261. The reused build passed 777/777 after test-only repairs and
> eight focused gates passed. Current Release `fsim` is `93d62464`; exact
> sources and receipts are in `docs/performance-resume.md`. A fresh original
> snapshot diagnostic stopped at a complete pre-JIT census line in 7.640
> seconds: 47 staged plans, no preflight declines, and 38 candidates across
> 10 repeated structural groups (largest five components, 108,165 operations).
> The identity is observational only; executable sharing, full completion,
> canonical parity, and matched Wall remain unverified. The previous full
> bounded original screen still stopped in startup after 55.201 seconds.

> **Historical source checkpoint — 2026-10-05 07:03 UTC.**
> Generic V2 pending-write shape validation now follows authenticated
> `pending_slot` mappings, including noncontiguous layouts; native cache
> schema is 261. The reused build passed 774/774 and five focused compiler,
> cache, schema, ABI-layout, and native-boundary gates passed. Current Release
> `fsim` SHA-256 is `d23fe6d3`; exact hashes and receipts are in
> `docs/performance-resume.md`. This was a correctness-only batch with no new
> throughput run. The last original-stimulus screen remains incomplete in
> startup at the 55.201-second cache-260 cutoff. Additive staged V2 plan
> preparation and a pre-JIT structural census are under scratch review; no
> sharing, timing gain, canonical parity, or matched Wall is established.

> **Historical source checkpoint — 2026-10-05 06:31 UTC.**
> Native object cache schema 260 covers the reviewed None-tier oversized-block
> split and two V2 write-site descriptor tables. The reused affected build
> passed 961/961 and eight compiler, cache, runtime, and application gates
> passed; exact source/binary and logs are in `docs/performance-resume.md`.
> Current Release `fsim` is `8ddd7d96`. The fresh-cache original screen
> remained in startup at the 55.201-second cutoff, with no workload output.
> A separate module-profiled PMU shows two repeated 2,625/2,592-member V2
> kernels each taking about eight seconds in O2 passes and about six more
> after optimization. The first raw module fell to 1.662 million
> instructions from the earlier 1.864 million, but incremental IR reductions
> have not cleared startup. The next dependency is a source-backed way to
> avoid repeated large V2 compilation while preserving exact guards and
> native/checked behavior. These instrumented partial runs are not full
> canonical parity or matched Wall evidence. The local build has no IPO;
> the frozen timing control uses ThinLTO. The 60-second diagnostic cap,
> original/mixed parity, hosted CI, and `fixed_array_write.sv` XSim issue
> remain open.

> **Historical source checkpoint — 2026-10-05 05:23 UTC.**
> The coherent private internal SV Active ranged-sensitivity V2 path and
> native cache schema 258 are integrated on top of the V2-only SV policy.
> The reused affected build passed 969/969, and 12 focused gates passed,
> including a real LLVM O0/O2 V2 witness with disjoint range filtering,
> exact 1/2/2/2 native dispatch counts, and checked signal/state parity.
> Current Release `fsim` SHA-256 is `afa7eb52`; source and test receipts are
> in `docs/performance-resume.md`. A fresh-cache original-stimulus screen
> stopped in startup at 55.201 seconds, before any workload output or
> frontier preparation summary. The previous policy-only screen reached
> tick 2,975,000 under the same bounded diagnostic contract. A bounded PMU
> and module profile identified a 2,625-member V2 kernel with 1.864 million
> raw IR instructions, 12.655 seconds of O2 passes, and unfinished LLVM
> materialization at the cutoff. Its 105.4 MB raw IR is captured; repeated
> per-member blocks account for about 1.36 million instructions, with a
> 225,000-instruction preflight block. A source-backed lowering reduction is
> the next dependency; no current full parity, Wall speedup,
> or throughput-target qualification is claimed. The 60-second diagnostic
> limit, non-IPO local build versus frozen ThinLTO control distinction,
> original/mixed parity, hosted CI, and `fixed_array_write.sv` XSim issue
> remain open.

> **Historical source checkpoint — 2026-10-05 04:33 UTC.**
> Optional SV Active V1 activation compilation is retired for the production
> LLVM provider. A V2-capable provider without a current V1 backend now falls
> through to the existing ordered cohort after V2 and forwarding declines;
> pure-interpreter and custom V1 routes retain grouped checked activation.
> Source pins and the focused 8/8 runtime/application gate receipts are in
> `docs/performance-resume.md`; current Release `fsim` is `14ab08fb` with
> cache schema 257. A fresh-cache instrumented 49.601-second original-route
> screen started normally and reached tick 2,975,000 with every offered ordered
> member accepted and no grouped-kernel failures. The prior V1-skip-only
> diagnostic reached tick 655,000 and the additional grouped-bypass diagnostic
> reached tick 2,965,000 in comparable capped windows. All were partial
> diagnostics, not canonical parity or Wall qualification. Private internal
> ranged-sensitivity V2 support and cache schema 258 remain the next coherent
> feature batch; its packages are still under review. The existing 60-second
> cleanup-inclusive diagnostic cap, ThinLTO timing-control separation, and
> unresolved full original/mixed parity, paired Wall, hosted CI, and
> `fixed_array_write.sv` XSim divergence remain in force.

> **Historical source checkpoint — 2026-10-05 03:33 UTC.**
> Cache schema 257 and the coherent SV Active partial-boundary slice path
> are integrated. The reused affected build passed 966/966, the restored
> test relink passed 860/860, and ten focused gates passed 10/10, including
> exact native/fallback, checked A4, compiler, and local-wave slice
> publication witnesses. Current Release `fsim` SHA-256 is `6609ff9f`;
> detailed source and gate receipts are in `docs/performance-resume.md`.
> The first local-wave activation is explicitly proved to fall back to
> interpreted execution; later accepted local-wave publications preserve
> exact 65-bit partial state. A fresh-cache original-route screen did not
> complete startup within 55.201 seconds. Bounded startup PMU attributes
> 58.9% of pre-interrupt self samples to LLVM, including 21.6% in an
> overflow-intrinsic use-list scan. A disposable selector captured a 55.5 MB
> raw legacy V1 activation module; its source and executable were restored
> byte-exactly. Replaying the exact LLVM pass sequence offline spent 10.814
> of 12.182 pass seconds in InstCombine on that module. The still-conservative
> ranged-sensitivity V2 guard and
> eager large V1 compilation are the next measured runtime limits. These
> interrupted diagnostics establish neither full canonical parity nor
> Wall performance. The prior coherent 92-test run, original/mixed full
> parity, paired Wall, hosted CI, and the separate `fixed_array_write.sv`
> XSim divergence remain open. Diagnostics are capped at 60 seconds
> including cleanup. The local diagnostic build has no IPO; the frozen
> timing control uses ThinLTO.

> **Historical source checkpoint — 2026-10-05 01:44 UTC.**
> Logic4 alias-family publication now copies packed word planes, and the
> wide-disjoint commit probe no longer repeats an owner-slot binding check.
> The reused affected build passed 898/898 and eight focused runtime, A4,
> allocation-failure, and app gates passed 8/8; exact receipts are in
> `docs/performance-resume.md`. Current Release `fsim` SHA-256 is `02491138`.
> The PMU and borrow-route evidence below used the preceding executable, so
> this batch has no measured runtime gain, final canonical parity, or Wall
> qualification. Current original/mixed parity, paired Wall, hosted CI, and
> the separate `fixed_array_write.sv` XSim divergence remain open.
> The current diagnostic Release build uses Clang 22 `-O3` without IPO;
> the frozen timing control uses ThinLTO. Its reusable configured ThinLTO
> candidate tree has a stale executable. Profile route counts remain valid,
> but small sampled call costs from the non-IPO build cannot be treated as
> matched-control Wall savings.
> A restored, disposable weighted census ran for 49.601 seconds on the
> pinned normal-original snapshot; its exact receipt is in
> `docs/performance-resume.md`. The 450 structural candidate components
> contain 19,155 members, while 416 compute programs contain only 1,035.
> A separate startup-only recorder captured all 34 structural rejections:
> 28 components/18,018 members fail SV Active nonzero-offset
> `WriteUpdateSlice` boundary-output mapping, and six/102 members fail alias
> reclassification. Offline artifact inspection confirmed exact partial
> driver ranges. Temporary source/executable were restored byte-exactly; the
> receipt is in `docs/performance-resume.md`. Coherent partial-boundary
> compilation is the next implementation scope. Of the 416 programs,
> 390/780 members fail the V2 output
> predicate; 26/255 members install. Candidate V2 prefixes cover 188,903
> tasks against 6,368,694 SV offered members, and no V2 native entry was
> reached in the bounded run. All temporary source and the live `fsim`
> executable were restored byte-exactly. This is route evidence from an
> interrupted diagnostic, not canonical parity or Wall qualification.

> **Cache256 historical source checkpoint — 2026-10-05 00:49 UTC.**
> Cache schema 256 and the certified native-frame packed-container borrow
> hint are integrated, along with local `set_driver` program-view reuse.
> The reused 12-worker affected-target build passed 975/975, and the focused
> union passed 19/19 after two new test fixtures were corrected to initialize
> their packed result registers before a call. The Release `fsim` SHA-256 is
> `64143445`; exact source and gate receipts are in
> `docs/performance-resume.md`. In a fresh normal-original, instrumented
> 49.601-second run, all 2,791,447 packed 64-bit-index reads borrowed and
> object-read callbacks stayed at zero; the previous fixed 2,617,674
> nonborrow-copy gap was absent. The run was interrupted at tick 6,605,000
> without a final summary or fingerprint. It is route evidence, not a Wall
> or full parity result. Current original/mixed canonical parity, paired
> Wall, hosted CI, and the separate `fixed_array_write.sv` XSim divergence
> remain open. Further runtime diagnostics have a 60-second cleanup-inclusive
> cap.
> A separate 50.002-second PMU sample confirms all 2,792,842 packed reads
> borrowed. In a conservative pre-interrupt activity window,
> `commit_driver_slice` is 46.86% inclusive, with
> `publish_container_alias_family` at 11.20%; the packed-index callback is
> 1.19%. The old container-object copy subtree is absent. These are sampled
> call-path shares from an interrupted run, not a Wall-speedup measurement.

> **Cache255 historical source checkpoint — 2026-10-05 00:19 UTC.**
> The pristine-preseed fallback, single-use container borrowing R2, indexed
> A4 role lookup, and 64-bit packed ContainerRead compiler/callback/ABI path
> are integrated. The reused Release all-target build completed 1990/1990;
> freshly linked affected targets passed 16/16 focused runtime, app, LLVM,
> cache, layout, and contract gates. Current `fsim` SHA-256 is `d574ee1c`;
> exact logs and source pins are in `docs/performance-resume.md`. The O0/O2
> deferred-peer lifecycle witness passes, but its fail-before control also
> passed with only the preseed patch reversed, limiting that witness to a
> checked-to-native lifecycle regression.
> In a fresh normal-original 49.601-second diagnostic, the new 64-bit route
> handled 2,763,945 packed reads, including 146,271 borrows; full object reads
> fell to zero. A separate 50.002-second PMU sample attributed 16.18% of
> pre-interrupt simulation cycles inclusively to packed reads, including a
> 15.25% `copy_container_object` subtree for nonborrowed reads. A subsequent
> bounded census attributed all 2,617,674 nonborrowed reads to the clear
> whole-program single-use flag at simulation time zero; each actually copied
> the object. Later 154,096 reads borrowed. The hot sites each have one object
> definition, one packed read, and no debug local. Offline artifact decoding
> found four earlier callable-frame push references to each hot register;
> a def-local lifetime change still needs call-frame snapshot safety proof.
> Temporary instrumentation was reversed and the production executable
> restored byte-exactly. These runs had no canonical summary or fingerprint.
> They are instrumented
> partial evidence, not a Wall-speedup or parity claim. The prior coherent
> 92/92 predates this batch; current original/mixed canonical parity, paired
> Wall, and hosted CI remain open. Runtime diagnostics remain capped at
> 60 seconds including cleanup.

> **Cache253 historical source checkpoint — 2026-10-04 22:24 UTC.**
> Production remains the trusted-stdout plus epoch/domain candidate, with
> 16/16 focused affected gates passing and Release `fsim` SHA-256 `46469ad8`.
> A bounded normal-stimulus census found the first 24 actual value-only rebind
> declines all failed because their frontier runtime was invalidated; a
> separate fork callback-observation path also requested a full snapshot.
> Exact receipt is `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-normal-recert-census-r3-analysis.md` (SHA-256
> `0ad4436d`). The diagnostic source and executable were restored byte-exactly;
> no guard has been relaxed. Lifecycle attribution is required before a safe
> recovery change. The 53-second partial run has no canonical summary or
> fingerprint. Current original/mixed parity, paired Wall, and hosted CI
> remain unqualified; each runtime diagnostic is capped at 60 seconds.

> **Cache253 historical source checkpoint — 2026-10-04 21:48 UTC.**
> Explicit trusted built-in stdout is followed by the O(1) component-epoch
> status map and cached scheduling-domain lookup. Reused Release builds passed
> 994/994 and 792/792; 16 affected focused gates passed 16/16 on freshly
> linked executables. Current `fsim` SHA-256 is `46469ad8`; exact logs and
> source pins are in `docs/performance-resume.md`. A matching 53.52-second
> normal-stimulus PMU diagnostic measured epoch-current at 0.02% self cycles
> versus 9.86% before this batch. It progressed farther but produced no final
> canonical summary/fingerprint; recertification remains 21.28% inclusive in
> this incomplete run. This is diagnostic evidence only. The prior coherent
> 92/92 predates these changes; current original/mixed parity, paired Wall,
> and hosted CI remain unqualified. Runtime probes have a 60-second cap
> including cleanup.

> **Cache253 historical source checkpoint — 2026-10-04 21:32 UTC.**
> The built-in CLI stdout path now uses an explicit text-only output hook;
> arbitrary hooks and custom streams retain full observation, and reserved
> markers request the barrier before handling. Runtime source was restored
> from the temporary recertification probes before this change. Focused
> runtime, CLI, marker/display, assertion, and installed-public-contract
> gates pass on freshly linked targets; exact split receipts and source pins
> are in `docs/performance-resume.md`. A 54-second preflight diagnostic
> preserved a byte-identical 17,407-event prior prefix and reached accepted
> sequence 425 without a final summary. A separate 53-second normal-stimulus
> PMU sample lost no samples and attributes 15.95% inclusive to region
> recertification, with epoch-current checks at 9.86% self. Both are
> instrumented, incomplete diagnostics, not throughput qualification. The
> previous coherent 92/92 predates the trusted sink; current full canonical
> parity, paired Wall, and hosted CI remain unqualified. Each further runtime
> diagnostic is capped at 60 seconds.

> **Cache253 historical source checkpoint — 2026-10-04 20:13 UTC.**
> Static owned-Logic4 adapter retirement, generated constant-slice retirement,
> and row-backed SDF scans are integrated. The former multi-owner static
> candidate remains checked fallback; certified 65-bit A4 group publication
> is the positive route. Fresh runtime #450/#471 pass the retired-route and
> checked-prefix/preflight-failure controls. The reused Release all-target
> relink passed 2087/2087 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-static-retirement-all-build.log`,
> SHA-256 `b44b3728`); the current 92-name coherent gate passed 92/92
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-static-retirement-coherent92-ctest.log`, `e72cb9e9`), and six
> additional SDF/VITAL gates passed 6/6
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-static-retirement-sdf6-ctest.log`, `4477a1d3`). Current `fsim`
> SHA-256 is `09923167`. Finite M3/M4/A5/ABI audits have found no further
> concrete feature gap; performance savings remain unmeasured. Full current
> original/mixed canonical parity, paired Wall, and hosted CI are pending.
> The separate `fixed_array_write.sv` XSim mismatch remains unresolved.
> A fresh original full preflight hit its 900-second diagnostic cutoff without
> a final summary/fingerprint; an independent mixed attempt was stopped when
> the diagnostic cap tightened to 60 seconds. Both remain unqualified. The
> original's separate 30-second CPU sample lost zero samples and attributes
> 68.53% inclusive to region recertification, with
> `RegionGraph::build_compute_program` at 20.36% self. Exact logs/PMU artifacts
> are named in `docs/performance-resume.md`; the trigger for repeated
> recertification is still under bounded investigation.

> **Cache253 historical source checkpoint — 2026-10-04 19:14 UTC.**
> M2 Verilog-2005 Active scheduling and artifact schema 73 are integrated
> (patch `499d7b9f`), along with row-only replay for admitted SV/Verilog
> concurrent templates (`da51f61e`) and the bounded A4 grouped-fallback
> repair (stage `5f1d1e2a`). The reused Release all-target build passed
> 2110/2110 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-m2-row-wide-twins-all-build.log`, SHA-256
> `0f3b9817`). The 92-name coherent run passed 89/92; each of its three
> test-fixture failures #307/#342/#474 passed after a narrow fixture repair
> and fresh relink, with no intervening production change. Exact logs are
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-m2-row-wide-coherent92-ctest.log` (SHA-256 `47b88ce1`),
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-m2-cache-scalar-capture-ctest.log` (`51679cba`),
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-m2-core-static-guard-ctest.log` (`7ba39337`), and
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-m2-cache-callback-fixture-ctest.log` (`3ca5c56a`). These are
> passing split receipts for all 92 names, not one all-green CTest log.
> Interpreter/O0/O2 scheduling witnesses pass, and outside-sandbox XSim
> matched all three new `.v` twin transcripts exactly
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-m2-verilog-twins-vivado.log`, SHA-256 `6684d76d`). A separate
> outside-sandbox XSim run of `fixed_array_write.sv` reached `$finish` but
> differed on three of eleven transcript lines involving invalid indices,
> a local write, and a wide read (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-fixed-array-write-vivado.log`,
> SHA-256 `87c35be7`); the expected transcript is unchanged. Subsequent
> constant-slice retirement passed focused elaboration #241
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-constant-slice-elaboration-ctest.log`, SHA-256 `cd892e76`),
> while row-backed SDF scans passed six affected tests and two package/inventory
> contracts (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-sdf-row-scans-focused6-ctest.log`, SHA-256 `3dcc3205`;
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-sdf-row-scans-contract2-ctest.log`, SHA-256 `f182a88f`). These
> production changes have no later coherent all-target relink. The corrected
> 129-bit A4 preflight allocation companion passes on freshly linked runtime
> #471 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-p35-static-adapter-r2-ctest.log`, SHA-256 `05d82f0d`);
> its earlier 65-bit fixture could not allocate because the value stayed
> inline. Static-adapter retirement is not integrated. Full canonical
> parity, paired Wall, and hosted CI remain unqualified.

> **Cache253 historical source checkpoint — 2026-10-04 17:48 UTC.**
> Direct SV/VHDL rows, coverage replay, and fused-container normalizer
> retirement are integrated. Generic disjoint-owner UpdateSlice admission,
> static A4 group publication, and the grouped-fallback repair are integrated
> (region runtime `7638d0f6`, stage `5f1d1e2a`). The reused Release all-target
> build passed before the final runtime/elaboration changes
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-generic-sv-static-all-build-r2.log`, SHA-256 `b237c49e`). Fresh
> #450/#458/#459 runtime tests pass 3/3 and #290/#303/#306/#471 affected
> route/failure tests pass 4/4 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-fallback-logic9-focused3-ctest.log`,
> SHA-256 `abc2446c`; `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-stage-affected4-ctest.log`, SHA-256
> `1b4c90e2`). The SV type-overlay comparator ignores only two source-span
> fields; #241 passes strict cache counts, typed-peer row identity, and
> artifact roundtrip (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-sv-overlay-equality-ctest.log`, SHA-256
> `96a51745`; builder `93528409`). Earlier #352/#461/#423/#424 and row/
> normalizer receipts remain valid for their tested source. A separate source
> audit found `.v` Verilog2005 lowering still selecting Generic scheduling;
> the IEEE-aligned fix is pending. No current coherent 92/92, full canonical
> parity, paired Wall, or hosted CI claim.

> **Cache253 historical source checkpoint — 2026-10-04 15:30 UTC.**
> S3 Stage2, A5 bound operands, P1 ordered/authoritative publication, P2
> persisted driver inventory (schema 72), private-bridge/V22 global frontier
> retirement, and the V22 runtime facade are integrated. The reused Release
> all-target build completed after a mechanical test-variable fix
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v22-facade-logic9-convert-all-build-r2.log`, SHA-256 `c654dcba`).
> Eighteen affected replacement/failure controls pass, including native
> Generic/V2 paths, callback observation, fused staging, runtime facade,
> artifact, VHDL projected, and frontier ABI layouts
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v22-facade-replacement-focused-ctest.log`, SHA-256 `5d620f20`).
> Runtime V22 planner/dispatch/marker/masked facade paths are retired; static
> masked compiler support remains. Two extra strict focused tests are red:
> structural remap #352 now catches the intended interpreter container error
> but shares two modules instead of one (`WriteContainerObjectElement` is its
> next rejection), and A4 ownership #459 fails a new Logic9 fixture's A4
> component-admission premise. The existing Logic4 native +1 revision path
> and callback invalidation #461 pass on fresh executables. Current coherent
> 92/92, full canonical parity, paired Wall, and hosted CI remain unqualified.
> The 14:23 checkpoint below is historical.

> **Cache253 historical source checkpoint — 2026-10-04 14:23 UTC.**
> S3 Stage2, A5 bound operands, P1 ordered/authoritative publication, P2
> persisted driver inventory (schema 72), private-bridge retirement, and V22
> queue retirement were integrated. The reused Release all-target build passed
> 2656/2656 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a5-p1-p2-retirement-all-build-r2.log`, SHA-256
> `5e5ec88c`). The focused set passed 30/33
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a5-p1-p2-retirement-focused-ctest.log`, SHA-256 `37dfeec5`;
> name-list SHA-256 `5a589151`). Later source/test changes supersede its
> failure attributions. Campaign phase parser tests passed 51/51 and artifact
> phase on/off smoke passed; these were diagnostic results only.

> **Cache252 historical source checkpoint — 2026-10-04 12:14 UTC.**
> P1's epoch-local dirty-word commit walk and S3 Stage1's contiguous
> runtime-owned V2 frame workspace are integrated (`simir_scheduling_stage.cpp`
> `587f4e85`, `simir_internal.hpp` `9e65a0bf`,
> `simir_native_frontier.cpp` `b17082a0`). The affected-target Release build
> passes (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-p1-s3-focused-build-r3.log`, SHA-256 `f8e28bff`).
> The clean-current reused `fsim` link passes (binary SHA-256 `8943e4eb`;
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-p1-s3-fsim-link.log`, SHA-256 `f2742887`).
> Ten focused gates pass by split receipts: nine in
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-p1-s3-focused-ctest.log` (SHA-256 `ce007ee9`), followed by
> `fsim.runtime.a4_wide_slot_ownership` in
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-p1-s3-a4-wide-fixed-ctest.log` (SHA-256 `57187eef`). The latter
> required a test-only correction to the existing atomic family callback
> order: proxy observer before deferred leaf observers. Values, revisions,
> events, transactions, and production code were unchanged. S3 A4 role-word
> ownership remains Stage2; P1 authoritative/lazy publication and redundant
> native-word materialization/compare work remain follow-up. P2 persisted
> elaboration driver classes and A5 full compact template-role binding remain open.
> Current-source coherent 92/92, full canonical original/mixed parity,
> paired Wall, and hosted CI remain unqualified. The 11:49 checkpoint below
> is historical.

> **Cache252 historical source checkpoint — 2026-10-04 11:49 UTC.**
> The alias proxy-slice runtime and scoped cache tag are integrated.
> The public artifact-phase compatibility witness passes in the parsed
> native-frontier application target after its authenticated runtime-SimIR
> transformation (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-artifact-public-phases-ctest.log`, SHA-256
> `72b7a19c`); this does not claim that the current HDL frontend emits the
> transformed proxy operation. The independent checked proxy boundary/fallback
> gate also passes (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-proxy-fallback-r4-ctest.log`, SHA-256
> `7a59bb15`). Reused Release fsim/app built 807/807
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-local-wave-artifact-phases-build.log`, SHA-256 `11242472`).
> A fresh-cache 20-second cooperative diagnostic on the retained canonical
> snapshot found that all six restored semantic components attempt A2 with
> authenticated two-member receipts, then attempt V2; the exact first V2
> decline is `local-wave-packed-slots-unbound`, with zero native dispatch
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-boundary-component-route-r3/simulate.stderr`, SHA-256
> `f6612f64`). A subsequent A4 census found four packed slots staged for
> each of the six, but unbound at the early callback. Later A2 selection for
> two of those same components proves packed-slot eligibility recovered after
> the quiet-point transition; the guard remains unchanged. All four temporary
> route/census diagnostic layers were reversed to their clean production pins. A current-source audit
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-a4-followup-audit-20261004.md`) also leaves adopted work
> open: S3 needs contiguous ownership for both V2 frame workspace and A4 role
> words; A4 P1 records dirty signals but does not yet consume them in an
> ordered commit walk; A4 P2 driver classes are built at runtime snapshot
> rather than persisted from elaboration. A5 supports bounded dense remaps
> (spans up to 64), while full compact template-role binding remains a design
> task. These are implementation gaps, distinct from route qualification.
> Current-source coherent 92/92, full canonical
> original/mixed parity, paired Wall, and hosted CI remain unqualified.
> The 10:59 checkpoint below is historical.

> **Cache252 historical source checkpoint — 2026-10-04 10:59 UTC.**
> The reviewed alias proxy-slice classifier (`847873f4`), checked publication
> (`d5ea111d`), and scoped cache tag (`c383d477`) remain integrated.
> The proxy boundary R4 gate now passes with independent retirement/no-replay
> checks before public projected-container observation
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-proxy-fallback-r4-ctest.log`, SHA-256 `7a59bb15`).
> On the retained canonical snapshot, six restored semantic components
> each reached an authenticated two-member A2 attempt, then V2; A2 declined
> execution and V2's first false operand was local-wave eligibility, with
> zero native dispatch (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-boundary-component-route-r2/simulate.stderr`,
> SHA-256 `f90bbe93`). This 20-second cooperative run is diagnostic only.
> The artifact-backed app witness remains red before publication: its
> relative source fixture hits `FSIM-ART-HIR-001` because a relative
> producer identity with a digest collides with an absolute identity with
> an empty digest (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-source-identity-diagnostic-ctest.log`,
> SHA-256 `dbafbe38`); both temporary collision print edits were restored.
> The bounded V2 route diagnostic remains until the eligibility cause is
> established. Current-source coherent 92/92 and full canonical
> original/mixed transcript parity have not run; paired Wall and hosted CI
> remain unqualified. The 10:06 checkpoint below is historical.

> **Cache252 historical source checkpoint — 2026-10-04 10:06 UTC.**
> Alias proxy-slice classification (`847873f4`), checked publication
> (`d5ea111d`), and the scoped frontier cache tag (`c383d477`) are
> integrated. Release fsim built 801/801
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-proxy-slice-production-build-r1.log`, SHA-256 `d11f4979`).
> A fresh-native-cache canonical O2 startup probe on retained snapshot
> revision `rf304559559edff2f` restored six large kernels and
> found 26 active V2 runtimes / 255 members with no preparation decline,
> but stopped before native member dispatch. A separate 60-second run reached
> real callbacks and stopped at tick 4465001 with 228,362 region-kernel
> attempts, zero kernel runs/native member dispatches, and 3,934 A2 selected
> prefixes (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-proxy-slice-callback-r1/simulate.stderr`, SHA-256
> `659b9118`). This is route evidence only. Compiler, graph, and native
> boundary gates pass on this
> source; the reviewed proxy precommit successor reaches a later checked
> preparation allocation failure and passes #531
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-proxy-slice-precommit-r2-ctest.log`, SHA-256 `504e98bd`).
> The parsed app witness remains open: its child port bridge lowers to
> `WriteBlocking`, and a continuous packed-element syntax trial also fails
> the strict proxy `WriteUpdateSlice` owner check
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-proxy-slice-selected-actual-ctest-r1.log`, SHA-256
> `49943816`).
> Current-source coherent 92/92, full original/mixed parity, paired Wall,
> and hosted CI remain unqualified. The 09:19 checkpoint below is historical.

> **Cache252 historical source checkpoint — 2026-10-04 09:19 UTC.**
> The alias-leaf classifier and exact-full-width planner are integrated.
> A fresh-cache canonical O2 startup diagnostic found 12 large kernels
> rejected at original proxy `WriteUpdateSlice` operations, before V2
> preparation; native V2 member dispatch remains zero
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-boundary-startup-route-r2/simulate.stderr`, SHA-256
> `cfa5976b`). The guard remains pending authenticated full-leaf proxy-slice
> publication. The reviewed classifier successor `847873f4` awaits its
> runtime publication companion. Focused graph, wide-boundary failure/reentry,
> and parsed application gates pass 3/3
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-boundary-focused3-clean-r1.log`, SHA-256 `6af0b448`).
> The prior 92/92
> coherent run predates this source; full canonical parity, paired Wall, and
> hosted CI remain unqualified. The 08:58 checkpoint is historical.

> **Cache252 historical source checkpoint — 2026-10-04 08:58 UTC.**
> The reviewed alias-leaf boundary classifier (`9f71e1c9`, with mechanical
> compile repair `62050795` to live `simir_region_program.cpp` `81075e51`),
> SV exact-full-width boundary planner (`72174a09`, candidate `df7eb80c`),
> and compiler companion (`55f3401a`, candidate `36d2731f`) are integrated.
> Release fsim/compiler targets built 767/767 and
> `fsim.llvm.region-frontier` passed (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-boundary-compiler-test-r1.log`,
> SHA-256 `72d63391`). The new graph/app/failure companions have not yet run.
> A separate fresh-cache canonical O2 startup diagnostic on this source
> found zero preparation declines, but activation programs fell 422 to 410,
> backend pool entries 26 to 20, and SV V2 runtimes remained 20/39 with zero
> native member dispatches (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-alias-boundary-startup-route-r1/simulate.stderr`,
> SHA-256 `0d03c8f5`). The six formerly rejected kernels now disappear before
> preparation; this is an unresolved admission regression, not V2 route
> success. The preceding 92/92 coherent gate predates this alias change and
> is not current-source qualification. A bounded classifier rejection census
> is pending. The 08:21 checkpoint below is historical. Full canonical
> original/mixed parity, paired Wall, and hosted CI remain unqualified.

> **Cache252 historical source checkpoint — 2026-10-04 08:21 UTC.**
> Host full-width admission, the packed-owner binding index, local
> wide-disjoint eligibility context, and the synthetic full-width native
> route witness are integrated. Release all-target built 2066/2066 and the
> exact coherent selector passed 92/92 on that semantic source
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-host-index-context-coherent92/test.log`, SHA-256 `3c65fd60`).
> Later profile-only probes found the six remaining 36-member V2 preparation
> declines at `prepared-successor-mapping-invalid`. Every first failing edge
> is an aliased 8-bit internal leaf whose access inventory is complete but
> whose grouped and prepared successor maps are empty
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-frontier-prepare-reason-startup-r3/simulate.stderr`, SHA-256
> `eb5d0d19`). Source inspection attributes this to structural alias leaves
> entering `kernel.internal_signals` while grouped-fanout publication excludes
> aliased signals. The guard remains intact; this establishes a missing V2
> route, not a simulation-value failure. The two verbose diagnostic patches
> were removed exactly; the basic bounded decline-reason census remains.
> The 08:00 checkpoint below is historical. Canonical original/mixed
> transcript parity, paired Wall, and hosted CI remain unqualified.

> **Cache252 historical source checkpoint — 2026-10-04 08:00 UTC.**
> Reviewed host full-width predicates (`6c754258`), packed-owner index
> (`2b728049`), and local wide-disjoint eligibility context (`1ff11476`)
> are integrated. The synthetic pre-registration `(0,9)` host witness
> (`18fbb0ab`) passed in `fsim.application.native-frontier-random-dag`;
> it asserts actual native dispatch and strict parsed `(0,8)` fallback,
> without claiming a parsed exact-full-width tuple. Release all-target built
> 2066/2066 and the exact coherent 92-name selector passed 92/92
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-host-index-context-coherent92/test.log`, SHA-256 `3c65fd60`).
> The profile-only preparation-reason patch (`2c8eb049`) then built and
> passed an observation smoke test. A separate fresh-cache, 15-second
> cooperative startup diagnostic found the same six 36-member components
> declining at `prepared-successor-mapping-invalid`
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-frontier-prepare-reason-startup-r1/simulate.stderr`, SHA-256
> `41861158`). V2 native dispatch on the canonical workload therefore remains
> unproven; this diagnostic is not a full-parity or speed result. The earlier
> owner-slot sample changed from 25.48% to 3.71% self across different
> prefixes, which is call-path evidence only. The 07:43 checkpoint below is
> historical. Full canonical original/mixed parity, paired Wall, and hosted
> CI remain unqualified.

> **Cache252 historical source checkpoint — 2026-10-04 07:43 UTC.**
> The reviewed host exact-full-width predicates (`6c754258`), packed-owner
> binding index (`2b728049`), and stack-local wide-disjoint eligibility
> context (`1ff11476`) are integrated. The context source passed a Release
> fsim build and 14/14 affected A4, native, alias, observation, blocking,
> and failure gates (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-wide-disjoint-context-focused14-test-r1.log`,
> SHA-256 `653a359d`). The prior 92/92 coherent run predates these changes;
> the parsed host full-width route companion is still pending.
> A pre-context ThinLTO host/index executable is frozen at
> `cache252-host-fullwidth-owner-slot-candidate-20261004T0735Z` (receipt
> SHA-256 `c83cd976`, binary `4e5ed7aa`). Its fresh-cache, 60-second
> cooperative diagnostic still found six 36-member V2 preparation declines,
> 20 SV runtimes, and zero native V2 member dispatches; A2 forwarding stayed
> active (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-host-fullwidth-owner-slot-quiet-profile-r1/route-summary.json`).
> The sampled owner-slot self share was 3.71% versus 25.48% in an earlier,
> different-prefix sample. That supports call-path attribution only, not a
> throughput gain. The 06:55 checkpoint below is historical. Full canonical
> original/mixed parity, paired Wall, and hosted CI remain unqualified.

> **Cache252 historical source checkpoint — 2026-10-04 06:55 UTC.**
> The reviewed fork-child access attestation repair (`93295d61`) and its
> recertification witness (`df690891`) are integrated. A bounded pre-fix
> census identified all 12 incomplete records as concrete fork children with
> inherited bindings (`registered_match=0`, `forked_match=1`). The post-fix
> census finds 0 incomplete records among 85,559 processes, complete graph
> access inventory, 450 structural candidates, and 1,249 candidate internal
> signals (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-fork-access-postfix-census-r1/postfix-summary.json`).
> Current Release all-target build passed 2116/2116 and the exact 92-name
> coherent selector passed 92/92 in 203.47 seconds
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-fork-access-coherent92/test.log`, SHA-256 `a3aea8fd`);
> `fsim.application.fork` also passed separately. The ThinLTO executable
> built successfully (binary SHA-256 `b660c46c`) but is not yet frozen as a
> timing candidate. In the bounded post-fix run A2 forwarding remained
> active while V2 native member dispatches were still 0; the remaining
> preparation/precedence gate is under source audit. The 06:15 checkpoint
> below is historical and its fork-mismatch hypothesis is now measured and
> repaired. Full canonical original/mixed parity, paired Wall, and hosted CI
> remain unqualified.

> **Cache252 historical source checkpoint — 2026-10-04 06:15 UTC.**
> Exact-full-width internal `Any` sensitivity is now admitted under a scoped
> frontier cache key; strict internal subranges remain rejected. The reviewed
> compiler positive/equivalence/negative companion passed. The pre-Slot source
> built all Release targets (2087/2087) and passed the exact coherent 92-name
> selector (92/92; `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-scheduling-origin-fullwidth-coherent92/test.log`).
> The later Slot-domain accessor cache is the current live runtime source
> (`simir_internal.hpp` `1100081f`, `simir_state.cpp` `0eaba4ca`): its targeted
> build passed 1009/1009 and focused semantic selector passed 10/10.
> Its ThinLTO executable is frozen at `cache252-slot-domain-candidate-20261004T0611Z`
> (receipt `6da392e4`, binary `5351f74c`). A fresh-cache, profiled 30-second
> cooperative stop on that binary saw zero frontier planner refusals but also
> zero V2 native member dispatches. Graph access inventory was incomplete for
> all 1030 components, with 12 unknown-dependency process exclusions; A2
> forwarding remained active. A fork-child binding mismatch is a source-level
> hypothesis under audit, with no repair integrated yet. This diagnostic is
> partial route evidence, not an end-to-end speedup or correctness verdict.
> The older 05:21 internal-width refusal and 05:08 in-progress preflight
> statements below describe superseded source and completed historical runs.
> That prior unprofiled canonical attempt ended at its 601.7-second cutoff
> with five exact-control summaries, instance 0 incomplete, and mixed unrun.
> Current-source full canonical parity, paired Wall, and hosted CI remain
> unqualified.

> **Cache252 frozen-candidate preflight and quiet profile — 2026-10-04 05:21 UTC.**
> The unprofiled canonical original-case preflight on frozen binary
> `41a8333f` stopped at the approved 601.7-second supervisor cutoff.
> Its 9,420,800-byte partial transcript (SHA-256 `b520ddc97fe883579b6f614cf5a2aa1474af1144165fcf4a42dee9d8dd0d67bb`)
> contains five completed stimulus summaries, all exactly matching the
> canonical control; instance 0 remained incomplete near ready cycle 36,427,
> and mixed was not run. This is incomplete parity, not a semantic failure.
> A separate 90-second quiet diagnostic using the same frozen ThinLTO binary,
> copied libraries and fresh native cache omitted `+PERF_PREFLIGHT` and
> captured 20 seconds of userspace cycles with a JIT map. Its strongest
> sampled self costs were `ProcessTable::program_view` 7.57%,
> `queue_static_next_delta` 7.47%, `PackedLogic4` storage initialization
> 7.11%, and native executor resume 7.06%
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-geometric-boundary-quiet-profile-r1/active-20s.caller.txt`).
> The gated frontier refusals now identify intentionally unsupported
> internal partial sensitivity (offset 0, width 8), distinct from the
> external ranged boundary newly admitted. The quiet diagnostic has no
> completed output or phase-summary verdict; it is not paired Wall evidence.
> Full original/mixed canonical parity and paired throughput remain pending.

> **Cache252 geometric queue and ranged-boundary gate — 2026-10-04 05:08 UTC.**
> The reviewed WorkQueue geometric-growth repair and scoped external
> ranged-boundary frontier admission are integrated. Current pins are
> `scheduler_internal.hpp` `1d8cf179`, frontier planner `275b2d4c`, and
> frontier compiler `364947be`; the new app witness is `f2dac8b5`.
> The reused Release all-target build passed 2046/2046. An exact 18-name
> scheduler/order/failure/allocation selector passed 18/18, the independent
> compiler frontier and queue-growth failure suites passed, and the
> deduplicated coherent selector passed 91/91 with only the then-failing
> boundary app witness excluded (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-geometric-boundary-coherent91-test-r1.log`,
> SHA-256 `6f6e910850771518e1d5bfd70a017dfb6061500c5ed528083b20d700185c99cc`).
> The corrected app witness then passed separately, establishing passing
> receipts for all 92 distinct selected names on unchanged production
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-boundary-range-readiness-r4-app-test-r1.log`). It retains exact
> two-member native dispatch, off-slice non-dispatch, four-state and metadata
> parity. The frontier cache key adds
> `whole-any+read-only-boundary-ranges-v1`; global cache252, runtime schema71,
> and HIR revision9 remain unchanged.
>
> The rebuilt ThinLTO candidate is frozen at
> `build/performance-campaign/simulation-architecture/cache252-geometric-boundary-candidate-20261004T0505Z/receipt.json`
> (receipt SHA-256 `33b9e22047c0c57307a4fc9df88c6b0552e469650c971d740556a13e0f60abd3`,
> binary SHA-256 `41a8333f8d3b17c3561b7674c2d21274784209e6f8cad32e8ff04a690e791832`).
> Its untimed full original/mixed canonical preflight is in progress with fresh
> caches and a 600-second candidate-simulation cap. No current candidate
> transcript parity, paired Wall, or hosted-CI result is claimed. The
> separate A1 invalid-index oracle remains **partial external deviation**:
> XSim reached `$finish` with 12/14 normalized lines matching; IEEE
> 1800-2023 §7.4.5/Table 7-1 supports the retained X expectations for the
> two invalid X/Z or out-of-range lines, so expected outputs are unchanged.

> **Cache252 observation and alias checkpoint — 2026-10-04 04:15 UTC.**
> The reviewed completed-observation cache, alias-family plane slab, and
> value-only recertification clear are integrated. The corrected native
> invalidation witness passed, the reused all-target build passed 1937/1937,
> and the exact current-source coherent selector passed 90/90
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-observation-cache-alias-slab-recert-coherent90-test-r1.log`,
> SHA-256 `901332684f9e19bdc686095726f4b3ab7df317dfb262d37689419fe857b6bef6`).
> Current runtime pins include `simir_internal.hpp` `d13c8caf`,
> `simir_region_graph_runtime.cpp` `e0a71034`, and the native-observation
> test `1310b9a2`. The opt-in compiler refusal diagnostic changes logging
> only: all six canonical original-case candidate kernels reject a ranged
> boundary sensitivity at offset 128, width 8, against a present 136-bit
> Logic4 input; no admission change is claimed.
>
> The frozen combined ThinLTO candidate (`cache252-observation-slab-candidate-20261004T0327Z`,
> binary SHA-256 `7430393b`) predates the later recertification and diagnostic
> source. Its untimed canonical original-case preflight reached the approved
> 600-second cutoff: five completed summaries matched the passing control,
> instance 0 and the mixed case remain incomplete. A separate quiet
> ThinLTO 20-second CPU sample attributes 32.93% self to scheduler
> `vector<Entry>::reserve` and 29.22% to variant movement on the
> SystemVerilog update-enqueue path; it is diagnostic evidence, not paired
> Wall or full transcript parity. Hosted CI is unqualified.

> **Cache252 recycler coherent gate — 2026-10-04 03:02 UTC.**
> The exact frozen 90-name selector passed 90/90 on the reviewed recycler and
> immediate slot-reuse companion source (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-readiness-targeted-recycle-coherent90-test-r1.log`,
> SHA-256 `4a74f71aa2b2eeb527c3575ea1569bee7359d12ca6eeef4f306e03a9d21d3ded`).
> The preceding all-target build, focused 20-name scheduler/native selector,
> and companion runtime suite also pass. The existing ThinLTO candidate freeze
> predates this scheduler repair and its full candidate preflight remains
> incomplete after the bounded cutoff; rebuild/refreeze and canonical parity
> are required before any paired throughput decision. No Wall or hosted-CI
> qualification is claimed.

> **Cache252 targeted-readiness recycle checkpoint — 2026-10-04 02:45 UTC.**
> The reviewed scheduler change releases an exhausted readiness ticket at its
> exact cursor-advancement site while retaining the SV phase-drain sweep. It
> built across the reused all-target tree (2054/2054;
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-readiness-targeted-recycle-alltarget-build-r1.log`) and passed the
> exact 20-name scheduler/Generic/SV/native focused selector (20/20;
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-readiness-targeted-recycle-focused20-test-r1.log`). The approved
> immediate slot-reuse companion then built and passed its containing
> `fsim.runtime` suite (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-readiness-targeted-recycle-companion-test-r1.log`).
> No JIT ABI or cache identity changed. The prior compact-validator 90/90
> receipt and ThinLTO candidate freeze predate this scheduler source change;
> the frozen candidate's full canonical preflight remains incomplete after a
> 600-second diagnostic cutoff. A subsequent candidate must be rebuilt and
> refrozen before paired throughput, and no current-source coherent90 or Wall
> result is claimed for the recycler revision.

> **Cache252 canonical-preflight diagnostic — 2026-10-04 02:22 UTC.**
> The configuration-matched ThinLTO `fsim` candidate linked 966/966 and was
> frozen with source, build, dependency, and cache-identity provenance at
> `build/performance-campaign/simulation-architecture/cache252-compact-candidate-20261004T0204Z/receipt.json`
> (receipt SHA-256 `4114d9d68a3295ef8b365496091859a9528e111c5969c61c726059b31ff3d06e`).
> Untimed full-fixture preflight of the frozen `ede7c24e` control passed
> original and mixed fsim/Vivado canonical stimulus and correctness parity
> (`cache252-control-full-preflight-20261004T0206Z`). Candidate original
> fsim simulation produced a growing partial transcript but was stopped at
> the approved 600-second diagnostic cutoff before a verdict; mixed was not
> run (`cache252-candidate-full-preflight-20261004T0208Z`). This is an
> incomplete candidate preflight, not a correctness failure or paired Wall
> result. A 20-second userspace cycle sample during that run localized this
> interval mainly to observation preparation and readiness-ticket recycling
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-compact-candidate-original-active-20s.callgraph.txt`). An
> opt-in no-task generated-entry screen measured the current validator at
> about 85 microseconds for 65 members and 321 microseconds for 129 members
> per call, with no baseline-relative claim; its test source was restored
> byte-for-byte. Current-source coherent90 and strict allocation remain PASS.
> Canonical candidate parity, call-path attribution and paired throughput
> remain open; no hosted-CI or throughput claim is made.

> **Cache252 compact-validator coherent gate — 2026-10-04 01:59 UTC.**
> The compact runtime-pair validator passed the reused Release all-target
> build (1930/1930; `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-compact-validator-alltarget-build-r1.log`),
> the unprofiled 65/129-member Generic O0/O2 fixture (29.05 seconds;
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-compact-validator-large-unprofiled-test-r1.log`), and the exact
> current-source 90-name coherent selector (90/90, zero failures;
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-compact-validator-coherent90-test-r1.log`, SHA-256
> `39d8957151805bc24a21bed4e7216d1cea6d5c27d7ae6fc1f6d19b43a52aa06f`).
> The strict warmed allocation gate passed separately on the mirror repair
> before this codegen change, and passed again among the five affected focused
> compact-validator gates. Raw-IR and LLVM pass timings remain diagnostic,
> separate from uninstrumented paired throughput. The ThinLTO candidate link,
> canonical transcript/fingerprint preflight, runtime-cost comparison and
> hosted CI remain pending; no throughput result is claimed.

> **Cache252 compact-validator trial checkpoint — 2026-10-04 01:54 UTC.**
> The reviewed compact runtime-pair validator and mechanical unused-overload
> removal built in the reused Release tree. Five affected LLVM, native,
> Generic-failure, strict-allocation, and region-route gates passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-compact-validator-focused-test-r1.log`). The unprofiled
> `fsim.application.native-frontier-v2-first-generic` fixture passed its
> 65/129-member O0/O2 cases in 29.05 seconds
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-compact-validator-large-unprofiled-test-r1.log`). A separate
> profiled raw-IR diagnostic reduced the 65-member frontier from 507,260 to
> 56,350 instructions and recorded 163.6 ms O2 optimization; the 129-member
> frontier recorded 109,534 instructions and 315.4 ms O2 optimization. The
> earlier 120-second baseline process cutoff occurred while 65-member O2
> InstCombine was active; it did not reach 129 members. These are compile
> diagnostics, not paired throughput timings. The earlier 90/90 coherent
> selector and strict warmed allocation gate passed before this codegen trial;
> broader post-trial regression and runtime-cost comparison are pending. The
> scoped frontier validator identity changed without a global cache252 bump.
> No benchmark or hosted-CI claim is made.

> **Cache252 coherent correctness checkpoint — 2026-10-04 01:38 UTC.**
> The reviewed A4 mirror role-source repair and COW-failure companion built
> cleanly, and strict `fsim.application.native-frontier-steady-allocation`
> passed its unchanged warmed width 1/65/129 O0/O2 zero-allocation windows
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-generic-bridge-route-test-r1.log`). A bounded bridge census
> showed one actual A2 forwarding-member consumption after its saved SV
> receipt; the test now credits either successful A2 or V2 native work while
> retaining exact key and interpreter parity. The reused all-target build
> passed 2119/2119 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-mirror-alltarget-build-r1.log`), followed by
> the exact 90-name coherent regression selector at 90/90 PASS
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache252-coherent90-run-20261004/results.log`; selection proved
> by the pinned name list and CTest index file). The newly added independent
> range-validation adversarial tests then passed on unchanged codegen via
> `fsim.llvm.region-frontier`
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-frontier-range-validator-baseline-test-r1.log`). A separate
> 120-second profiling run captured 65-member O2 raw IR and stopped in LLVM
> InstCombine; it did not reach the 129-member O2 case
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-generic129-o2-short-large-diagnostic-r1.log`). The long 129 O2
> qualification and paired throughput remain outstanding; no timing or
> hosted-CI claim is made.

> **Cache252 wide-role qualification checkpoint — 2026-10-04 01:05 UTC.**
> Reviewed A4 owned-snapshot rebinding and its exact wide-slot ownership
> companion are integrated; `fsim.runtime.a4_wide_slot_ownership` passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-snapshot-focused-test-r1.log`). The diamond, multioutput,
> wide failure/retry, and unequal-depth native-region capture helpers now
> account for deferred private role rows; the clean
> `fsim.application.native-region-route` gate passed 1/1
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-wide-passive-final-test-r1.log`, source SHA
> `594ebd546c8d9a6c047c056fc11c9249c328d54fd2c838257ccf7b5527931155`).
> Strict steady allocation remains red: width 65 O0 now has zero allocations
> with value-only rebind, but width 129 O0 window 0 has 45. A fixed-buffer
> census and first-allocation LLDB stack locate repeated wide
> `mirror_region_owner` materialization during private-output publication
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-steady-width129-sites-test-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-steady-width129-first112-lldb-r2.log`). The next bounded action is
> a reviewed reuse/ownership repair and unchanged strict gate. The previous
> 86/90 selector predates these source changes; Generic 129-member O2
> optimization remains incomplete. No coherent green or throughput claim.

> **Cache252 allocation repair checkpoint — 2026-10-04 00:30 UTC.**
> The reviewed Generic queue and A4 scratch changes built across all targets
> with -j12 (2881/2881; `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-generic-alloc-batch-build-r1.log`).
> The exact focused selector passed 10/11, including strict Generic 129-member
> zero-allocation and five-cut failure gates; #289 reached a later diamond
> assertion after its original O0/O2 native zero-allocation windows passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-generic-alloc-focused-r1.log`). The diamond helper read
> passive roles while five private rows remained; a public read flushed all
> five and produced the expected value/parity
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-diamond-public-census-test-r1.log`). Strict steady-allocation
> #290 passes width 1 but fails the first O0 width-65 window with 1,490
> allocations (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-steady-recert-census-test-r1.log`). Its quiet
> value-only rebind rejects a retained snapshot on wide_stage0 LAST, then
> publishes a full generation-6 snapshot
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-steady-rebind-guard-test-r2.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-steady-a4-binding-test-r1.log`). Temporary probes were restored
> byte-exactly and their affected targets rebuilt cleanly. The prior 86/90
> selector remains historical; Generic 129-member O2 optimization is still
> incomplete. No coherent green, throughput, timing, or hosted-CI claim is
> made.

> **Cache252 focused repair checkpoint — 2026-10-03 23:32 UTC.**
> The clog2 startup-bank assertion was corrected from the measured zero
> compiled processes/modules/cache entries, retaining its exact O0/O2
> values and scheduling; `fsim.application.expressions` passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-clog2-startup-bank-test-r1.log`). The masked-route control now
> explicitly tests the legacy route with region admission disabled, while
> default VHDL projected widths 1/65/129 require accepted native backend
> completions and metadata parity. A reused -j12 build passed 826/826 and
> `fsim.application.core_simulation` plus `fsim.application.vhdl_projected`
> passed (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-341-route-split-build-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-341-route-split-test-r1.log`). The preceding 86/90 selector is
> historical and has not been rerun on these edits. Strict warmed allocation
> gates #289 (18 per O0 wave) and #456 (65 queue reserves) remain red; the
> Generic 129-member O2 optimization gate remains incomplete. No coherent
> green, throughput, timing, or hosted-CI claim is made.

> **Cache252 bounded regression checkpoint — 2026-10-03 23:12 UTC.**
> Current-source consumer relinking passed 2373/2373
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache252-postfocused-alltarget-build-r1.log`). The reviewed A1
> physical-net aggregate read witness was integrated and rebuilt; its existing
> `sv_containers` case passed. The exact 90-name selector ran 86 passes and
> four failures (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache252-coherent90-r1.log`): native-region route
> #289 and Generic 129-ticket #456 violate strict warmed zero-allocation
> checks; core simulation #341 has a masked-route counter premise; expressions
> #350 expects compiled clog2 processes although O0 reports zero. Bounded
> diagnostic evidence pins #456 to 65 one-step Generic queue reserves after
> capacity 64, #289 to 18 A4 plane/copy allocations per measured O0 wave,
> #341 to accepted projected-ticket native commits, and #350 to zero O0
> compiled modules/processes/cache entries with correct output values
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-coherent90-diagnostics-test-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-masked-dispatch-diagnostics-test-r2.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-generic-queue-capacity-r2.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-native-route-alloc-sites-r2.log`). Temporary diagnostics were
> restored byte-exactly. The Generic 129-member O2 optimization gate remains
> incomplete after its approved 900-second cutoff. No coherent green,
> throughput, timing, or hosted-CI claim is made.

> **Cache252 focused checkpoint — 2026-10-03 22:36 UTC.**
> The clean core prewrite and Generic future-sidecar gates passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-core-generic-vhdl-companions-focused3-r1.log`). The selected
> VHDL projected-output guard passed the full projected application test,
> including the foreign-cut and metadata witness
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-vhdl-selected-reentry-r4-focused2-r1.log`). The blocking
> immediate, precommit-failure, and later native-reentry tests passed together
> after the scheduler-key/journal-ordinal fixture correction
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-blocking-reentry-r5-focused3-r1.log`). These are focused
> receipts for certified subsets; unsupported cones and observed effects
> remain on checked execution. The 90-name bounded coherent selector is
> staged but unrun pending current-source consumer relinking. The Generic
> 129-member O2 LLVM optimization gate is incomplete after its approved
> 900-second cutoff; strict warmed allocation gates remain red at their last
> measured counts. The J-E audit found no further safe deletion
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-a4-je-retirement-audit-addendum-r2-20261003.md`). No
> coherent green, throughput, timing, or hosted-CI claim is made.

> **Cache252 focused checkpoint — 2026-10-03 22:12 UTC.**
> The clean core prewrite gate and Generic future-sidecar gate passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-core-generic-vhdl-companions-focused3-r1.log`). The VHDL basic
> projected ticket and Logic9 gates retain their passing receipts, but the
> new foreign-cut VHDL witness is red. A bounded trace shows root process 0
> executes natively, then the retained middle process 1 declines at
> `valid_outputs`: the whole-kernel scan treats root signal 1's pending update
> as a collision even though the selected middle output has none
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-vhdl-firstdecline-test-r1.log`). Diagnostic source was restored
> byte-exactly; a selected-output guard repair is under review. Blocking
> positive, precommit failure, and fused staging pass. The blocking reentry
> probe now accepts both private rows after completion cancellation cleanup,
> but its two-row safe-point cut is still missed by the current scheduler
> ordering (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-blocking-reentry-postcancel-census-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-blocking-reentry-helper-stage-census-r1.log`). A5 artifact and
> startup-bank gates passed at their earlier focused integration checkpoint.
> The Generic 129-member O2
> gate remains incomplete after the approved 900-second LLVM optimization
> cutoff, and strict allocation gates remain red at their last measured
> counts. The J-E retirement audit found no additional safe deletion
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-a4-je-retirement-audit-addendum-r2-20261003.md`). The
> 90-name bounded coherent selector is staged but unrun; there is no coherent
> green, throughput, timing, or hosted-CI claim.

> **Cache252 A2/A3/A5 focused checkpoint — 2026-10-03 21:33 UTC.**
> VHDL projected cycle-ticket admission passed after the reviewed scheduling
> precedence repair, and the existing VHDL Logic9 gate passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-core-r10-vhdl-precedence-focused6-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-vhdl-precedence-logic9-r1.log`). Blocking immediate and precommit
> failure, fused staging, and Generic update-failure controls also passed that
> focused run. Generic future-sidecar R2 built, but its existing interleaving
> test is red: a root now executes natively before the foreign key, while the
> old fixture expects checked word staging; its two failure controls passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-generic-future-sidecar-focused3-r1.log`).
> Core prewrite R9 and the checked-control provider flag built, but the control
> never advances either root resume counter at the safe point, so its planned
> boundary update does not queue. The new blocking reentry test reached time 2
> and observed one private root row followed by journal retirement before the
> middle row; its required two-row cut was absent. Blocking immediate/failure
> remained green (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-core-prewrite-reentry-focused4-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-core-reentry-census-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-blocking-reentry-gate-census-r1.log`). Diagnostic source was
> restored to pinned bytes. The Generic 129-member O2 optimization gate remains
> incomplete after its approved 900-second cutoff; strict allocation gates
> remain red at their last measured counts. No coherent green, timing, or CI
> claim is made.

> **Cache252 A2/A3/A5 focused checkpoint — 2026-10-03 21:04 UTC.**
> A5 Bit2-to-Logic4 startup CopyRegister and its runtime companion are
> integrated. The reused affected-target build passed 1107/1107
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a5-vhdl-core-diagnostic-build-r3.log`); artifact phases and
> constant-driver startup bank passed the exact focused gate
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a5-vhdl-core-focused4-r1.log`). Core runtime remains red in the
> later stale-role witness; its reviewed successor is pending.
> The blocking-failure role-baseline repair and VHDL cycle-ticket fixture R7
> built 774/774 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-blocking-r7-vhdl-r7-build-r1.log`).
> Blocking positive, the authentic precommit allocation-failure cut, and the
> shared Generic update-failure control passed 3/4 focused tests
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-blocking-r7-vhdl-r7-focused4-r1.log`). VHDL projected still
> fails: the standalone VPI-omitted run certifies a three-member component and
> matches interpreter values and metadata, but physical ticket insertion,
> elision, and direct-dispatch counters all remain zero. The default VPI-enabled
> control correctly stays on the observed ordinary route. A ticket scheduling
> source diagnosis is pending; no VHDL route success is claimed.
> The 65-member Generic interpreter/O0/O2 and 129-member interpreter/O0
> subcases reached their assertions, but the 129-member O2 LLVM optimization
> run was interrupted at the approved 900-second diagnostic cutoff, so the
> full Generic app gate is incomplete. Strict warmed Generic allocation remains
> red at 65 measured allocations; older native/steady strict gates last
> measured 18/46. No coherent post-A2/A3 green, throughput, timing, or
> hosted-CI result is claimed.

> **Cache252 A2/A3 diagnostic checkpoint — 2026-10-03 20:02 UTC.**
> Generic logical components beyond 64, the parsed blocking precommit-failure
> witness, and the core receipt correction are integrated. The reused six-target
> build passed 875/875 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-generic-logical-core-blocking-build-r6.log`).
> Its exact focused run passed 2/6: parsed blocking positive and the scheduler
> compact-ticket test passed; Generic native consumption, the later core
> stale-role witness, the blocking failure cut, and strict warmed Generic
> zero-allocation remained red
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-generic-logical-core-blocking-focused6-r1.log`).
> Bounded diagnostics found that Generic native admission compared its 64-event
> staging frame with a 65-event layout maximum, so every member fell back to
> checked execution. The reviewed bound correction (`d58690a7`) and core
> stale-role test correction (`c7d7c933`) have since built successfully
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-generic-framecap-core-r7-build-r1.log`); their focused run is
> in progress. The blocking fault fixture did not capture its root-only row
> cut; its repair remains under review. The strict Generic allocation gate is
> still red; the earlier measured count was 65 allocations from queue growth.
> Diagnostic source was restored byte-exact before the latest build. No
> coherent post-A2/A3 green, throughput, timing, or hosted-CI result is claimed.

> **Cache252 A2/A3 focused checkpoint — 2026-10-03 19:11 UTC.**
> The driverless blocking alias, shared absence-aware fixture helper, and
> exact core receipt assertions are integrated. The reused all-target build
> passed 2432/2432 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v252-alias-core-helper-all-build-r1.log`).
> Focused validation passed 5/7: source manifest, A4 state/wide/rebase, and
> A2 role journal pass; core runtime and parsed blocking positive remain red
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v252-alias-core-helper-focused7-r1.log`). Core's new
> after-foreign receipt flag is false while the authentic child receipt is
> captured, retired, and checked exactly once. Parsed blocking now certifies
> and enters forwarding but its first callback sees bank.active=0 before
> retirement, so publication declines without private rows
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v252-core-blocking-cut-diag-test-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v252-blocking-publish-stage-test-r2.log`). The diagnostic source
> is restored byte-exact; the next qualifying build must recompile it.
> Generic 65/129 auto-sizing and VHDL Logic9 passed focused gates before this
> alias batch (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-generic-auto-counter-test-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-v252-vhdl-ufringe-test-r1.log`). The strict warmed Generic
> 129-ticket gate remains red at 65 allocations from one-entry queue growth;
> older native/steady allocation gates last measured 18/46. No coherent
> post-alias full-suite, throughput, timing, or hosted-CI result is claimed.

> J-B source audit confirms current direct-read, lease, frame, tier, and
> cache-key contracts; older notes saying wide Logic9 is unsupported or tier
> is absent from the cache key are superseded historical statements.

> **Cache251 A2/A3/A4 focused checkpoint — 2026-10-03 16:54 UTC.**
> Single-root DAG, arbitrary-root forest, readiness groups beyond 64 members,
> quiet value-only A4 rebinding (`72e07e7f`), and the physical grouped-ticket
> pool (`1ccac4ca`) are integrated. The reused all-target build passed
> 2865/2865 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-pool-a4-fixture-all-build-r1.log`). The current
> focused selector passed 10/12: core runtime including pool coverage, A4
> state/Logic9/wide/rebase, observation invalidation, owned-driver demotion,
> A2 applied-prefix, scheduler group failure, and source manifest pass
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-pool-a4-fixture-focused12.log`). Only the two strict
> allocation gates fail. Before this batch, bounded raw counts found 18 C++
> allocations in the first O0 native-region window and 46 in the first width-1
> steady-frontier window (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-native-region-alloc-diag-test.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a4-quiet-steady-raw-test.log`); the current focused run did not
> recount them. The steady fixture now executes its hook-policy control before
> the allocation loop, and that control passed. A pre-pool diagnostic selector
> passed 80/83, including random DAG on quiet-rebind source; its VHDL
> compiled-module-count fixture still awaits the reviewed correction
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a3-a4-quiet-rebind-diagnostic83.log`). The next bounded batch is
> the VHDL count correction plus reviewed pool/native companions, followed by
> their exact gates. Native cache v251, compiled-HIR producer 9, and artifact
> schema 71 are unchanged. No coherent post-A2 green, throughput, or hosted-CI
> result is claimed.
>
> **Cache251 A2 focused repair checkpoint — 2026-10-03 12:06 UTC.**
> A2 role journaling, checked barriers, V2-authenticated flattened forwarding,
> narrow pure-cone admission, single-root fanout, and preallocated wide role
> seeds are integrated without a native-cache, compiled-HIR, or artifact-schema
> bump. The reused all-target build passed 1920/1920
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-width-fanout-wide-reset-build-r2.log`). Its initial affected
> selector passed 15/20 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-width-fanout-wide-reset-focused20-r1.log`);
> the repaired six-case selector then passed four cases, including fanout and
> both A4 state/ownership cases (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-six-fixture-repair-focused-r1.log`).
> The expanded force/deposit app witness passed after capturing the interpreter's
> actual child scheduler key, and the width 1/64/65/129/256/1024 X/Z and
> boundary-deposit role-journal witness passed after accounting for resolved-wire
> startup LAST=Z (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-interpreter-width-repair-focused2.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a2-width-z-baseline-test.log`). These are split passing receipts
> for 19 of those 20 names; `fsim.runtime` remains red at its unequal-depth
> join route assertion. The earlier coherent 78/78 receipt predates A2. No
> throughput or hosted-CI result is claimed.
>
> **Cache251 A4 checked-rebase prerequisite — 2026-10-03 08:51 UTC.**
> Versioned A4 storage now has a checked whole-signal group rebase and
> publication primitive with exact role baselines, late read-pin COW, and
> no-mutation decline. Narrow-only storage stays on its existing route; A2
> private admission is not enabled by this prerequisite. The reused all-target
> build passed 2114/2114, and the focused A4/consumer selector passed 12/12,
> including the new fault-sweep witness, existing A4 state and owner tests,
> native sync/boundary, steady allocation, core runtime, and source-package
> manifest (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-a4-checked-rebase-full-build-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-a4-checked-rebase-focused-r1.log`).
> `git diff --check` passed. This is focused qualification; the coherent 78/78
> receipt below predates this A4 source change. No coherent 79-test, throughput,
> or hosted-CI result is claimed.
>
> **Cache251 Generic compact-readiness qualification — 2026-10-03 08:42 UTC.**
> Eligible Generic V2 components now use one scheduler-owned Active ticket for
> multiple logical members, with exact receipt keys, foreign-key prefix cuts,
> checked fallback, and allocation retry preserved. Native cache v251,
> compiled-HIR producer 9, and artifact schema 71 are unchanged. The reused
> all-target build passed 2483/2483; after test-only fixture corrections, the
> shared-support rebuild passed 1786/1786. The repaired focused gate passed
> 3/3 and one coherent selected gate passed 78/78, including source-package
> manifest, random DAG, steady allocation, SystemC, compiler frontier, and
> compact-ticket failure witnesses
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-generic-compact-full-build-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-generic-compact-repair6-full-build-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-generic-compact-repair3-r5.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-generic-compact-consolidated78-r1.log`).
> `git diff --check` passed. Ordinary routes remain for ineligible components;
> no throughput or hosted-CI result is claimed. The next major A2 dependency
> remains default whole-cone register dataflow with private materialization.
>
> **Cache251 parked Generic V2 qualification — 2026-10-03 06:53 UTC.**
> The host-only V2-first construction, A5 constant-slice startup bank, A1 X/Z
> sensitivity witness, parked Generic completion, and effectful checked-route
> fallback are integrated. Native cache v251, compiled-HIR producer 9, and
> artifact schema 71 remain unchanged. The borrowed-process fixture lifetime
> and SV net-owner setup were corrected without weakening the native gates.
> The reused-tree all-target build passed 2101/2101
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-parked-effect-full-build-r2.log`); focused Generic and
> runtime tests passed 6/6, then one coherent selected gate passed 76/76,
> including the source-package manifest, random DAG, steady allocation,
> mutation, SystemC, and compiler frontier
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-parked-effect-focused-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-parked-effect-consolidated76-r1.log`).
> `git diff --check` passed. This is the latest one-shot selected-source
> receipt; no throughput or hosted-CI result is claimed.
>
> The adopted A1 static/range and A5 shared lowering, program/artifact, and
> compact constant slices have bounded passing gates; unknown dynamic indices
> retain conservative fallback and the external X/Z index oracle caveat.
> The next A3 gap is compact Generic region tickets in place of one scheduler
> entry per member. A2 still lacks default whole-cone register dataflow with
> private materialization. No further J-E deletion is justified by current
> replacement evidence.

> **Cache251 Generic Logic9 split qualification — 2026-10-03 05:58 UTC.**
> Native object cache schema is v251; compiled-HIR producer 9 and artifact
> schema 71 are unchanged. The approved Generic Logic9 compiler/ABI/runtime,
> typed positive and allocation-failure, partial-prefix, local-wave policy,
> and VHDL occurrence-sharing changes were integrated. The official
> source-package manifest contains 2405 ordered files and 29 exclusions;
> its CTest gate and the reused-tree all-target build passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-final-source-manifest-test.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-full-build-r4.log`, 1933/1933).
>
> The focused selector first passed 10/12; the two failures were fixture
> premises, repaired without changing production semantics. Executor metadata
> vectors now outlive borrowed spans; resolved Logic9 boundary descriptors
> expect their authentic sentinel layout owner; the region-disabled checked
> path verifies its queued Update scratch and exact values instead of native
> packed pending rows. The three affected Generic tests then passed 3/3
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-generic-three-checked-stage-test.log`). The other 60
> consolidated cases passed 60/60, including random DAG, steady allocation,
> mutation, SystemC, and runtime gates
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache251-consolidated-remaining-r1.log`). Together with the
> nine unchanged focused passes, these are split current-source receipts for
> all 72 selected cases, not a one-shot 72/72 run. `git diff --check` passed.
> No throughput result is claimed; cache246 below remains the latest fully
> passing one-shot consolidated 55/55 receipt.

> **Cache250 Generic V2 split qualification — 2026-10-03 05:17 UTC.** Native
> cache schema is v250; compiled-HIR producer 9 and artifact schema 71 remain.
> The official source-package manifest gate passed with 2401 owned files and
> 29 exclusions. The initial all-target build passed 1870/1870
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-full-build-r4-20261003.log`). Startup Generic frontier
> admission and generated zero-mask preflight defects were fixed; the O0/O2
> positive and compiler gates passed (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-generic-zero-mask-positive-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-generic-zero-mask-compiler-r1.log`). SystemC and restored
> elaboration gates passed; the scheduler pre-pop repair, 130-task ordering,
> cold suffix failure, steady allocation, and Generic positive gates passed
> their focused checks. The separate-input O0/O2 Generic allocation sweep
> passed with its actual one-task borrowed frontier and ordinary checked
> suffix (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-one-task-shape-test.log`).
>
> The host error channel and direct input-plane, forwarding, prepared-output,
> and direct-ready failure witnesses passed. The separate-input and shared-input
> O0/O2 Generic allocation sweeps passed, including propagated failures,
> exact-key retries, and the optional recovered reservation cut
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-backend-companion-runtime-test-r1.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-shared-r5-test.log`). The coherent all-target host build
> passed 2010/2010 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-final-host-full-build-r1.log`). The
> last one-shot consolidated selector passed 69/71
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-consolidated-r2.log`). Its two test-only stale fallback
> expectations were repaired and passed separately: Generic runtime #441
> proved propagated failure, unchanged state, exact-key retry, and native parity
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-final-two-focused-r1.log`); native-region-route #289
> proved the full cut sweep and runtime-level same-deadline retries while
> preserving public Simulation fatal-latch semantics
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-native-route-r3-test.log`). All other direct consumers
> of the changed allocation interposer plus ordinary Generic runtime passed
> 19/19 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache250-interposer-remaining-19-r1.log`). This is a split
> current-source qualification, not a one-shot consolidated 71/71 or throughput
> claim. Cache246 below remains the latest fully passing one-shot 55/55 receipt.

> **Pending cache249 / producer9 checkpoint — 2026-10-03 01:41 UTC.** The
> approved A1 unpacked-struct index, A5 startup-write bank, unsigned-add
> forwarding, SystemC callback witness, and narrow A4 disjoint-owner changes
> are integrated. Native cache schema is v249, compiled-HIR producer revision
> is 9, and artifact schema remains 71. The reused Release tree passed its
> all-target build (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache249-full-build-r5-20261002.log`,
> 1763/1763). The initial focused selector passed compiled-HIR cache,
> frontier, and SV containers (3/6); corrected startup-bank and A4 ownership
> gates then passed individually (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache249-startup-equal-z-test-20261002.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache249-a4-stimulus-test.log`). The SystemC executor has no
> certified signal-access binding, so its opaque callback boundary remains
> checked; the test's stop/resume receipt assertion is being corrected to that
> route. The VHDL cycle gate passed with a
> homogeneous unresolved native-positive companion and the existing mixed
> resolved checked-parity route (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache249-vhdl-native-fixture-test.log`).
> No cache249 consolidated or throughput
> claim is recorded. Cache246 below remains the latest fully passing
> consolidated receipt.

> **Pending cache248 V2/J-E checkpoint — 2026-10-03 00:31 UTC.** Native
> cache schema v248 contains the frontier V2 codegen/ABI and bounded J-E
> static-fuser replacement; artifact schema71 and compiled-HIR producer8 are
> unchanged. The reused build tree passed its full all-target build
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache248-v2-je-full-build-resume6-20261002.log`), and the
> official source-package generator and manifest gate passed with 2391 owned
> files and 29 exclusions. After the initial 6/15 focused result, the exact
> prior consolidated selector plus new V2 tests, excluding native-region-route,
> passed 64/65 (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache248-consolidated-incomplete-65-20261002.log`).
> Its sole stale-boundary test failure was an executor-only sampling assumption:
> the forwarded root reexecuted at fresh native scheduler keys with exact final
> parity; the repaired `fsim.runtime` focused gate passed
> (`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache248-final-two-fixture-focused-20261002.log`). J-E cache,
> frontier V2, random DAG, steady allocation, mutation, preparation, retained
> stop/resume, Logic9, adapter, boundary, and manifest gates passed in that
> selector. Native-region-route remains open at a VHDL cycle fixture assumption
> that a procedurally written SV source has a raw DriverRecord; phase-cut
> values and metadata already match the interpreter. Cache246 below remains
> the latest fully passing consolidated 55/55 receipt; no cache248 green or
> throughput claim is recorded.

> **Qualified cache246 checkpoint — 2026-10-02 19:34 UTC.** The frontier
> compiler now lowers Concatenate and certified known Logic4 bodies, handles
> bounded event capacity above 64 entries, authenticates retained next-round
> commits, and records the pending boundary write through ACK. Native object
> cache schema v246 is active at `codex/v3` HEAD `751ac08f`; artifact schema71
> and compiled-HIR producer8 are unchanged. The reused
> `build/batch188-release-clang22-final` completed its incremental all-target
> build (1860/1860), focused frontier/schema/ABI/runtime gate (5/5), and fresh
> consolidated selector (55/55, 42.00 s). Receipts:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache246-full-build-20261002.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache246-focused5-20261002.log`, and
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache246-consolidated55-20261002.log`. The native runtime
> adapter/provider and scheduler target-round addition remain in scratch
> validation; no throughput result is claimed for this compiler batch.

> **Qualified cache245 checkpoint — 2026-10-02 18:38 UTC.** The compiler-
> owned native frontier plan, generated entry, cache identity and adversarial
> frame tests, scheduler capacity/key handling, and A4 write leases are
> integrated at `codex/v3` HEAD `751ac08f`. Native cache245 is active;
> artifact schema71 and compiled-HIR producer8 are unchanged. The generated
> entry passed actual O0/O2 frame execution across Logic4 widths 1, 65, 129,
> 256, and 1024; the default Simulation runtime adapter remains a separate
> implementation slice. The reused Release tree built 2717/2717, focused
> checks passed 5/5, and the exact serial consolidated gate passed 55/55 in
> 72.68 seconds. The first A4 test run exposed a width-one fixture helper
> overrun; bounding its phase digits to the signal width made the test pass
> without a production change. Receipts: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache245-full-build-20261002.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache245-focused5-final-20261002.log`, and
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache245-consolidated55-20261002.log`. This is correctness
> qualification; no paired throughput claim was made.

> **Qualified cache244 checkpoint — 2026-10-02 16:35 UTC.** Direct
> template/instance artifact rows, lazy facade materialization, corruption and
> SystemC retention witnesses, and scheduler native-frontier key tests are
> integrated at `codex/v3` HEAD `751ac08f`. Runtime artifact schema71 and
> native cache244 are active; compiled-HIR producer8 is unchanged. The lazy
> allocation-failure sweep exposed an invalid `noexcept` on allocating
> `OperationList` copies; the corrected copy path now retries successfully.
> The reused Release tree built 2288/2288, focused validation passed 7/7,
> and the exact serial consolidated gate passed 53/53 in 67.02 seconds.
> Receipts: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache244-full-final-build.log`,
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache244-focused7.log`, and
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-cache244-consolidated53.log`. This is correctness qualification;
> no paired throughput claim was made.

> **Qualified cache243 checkpoint — 2026-10-02 15:33 UTC.** Unresolved
> SystemVerilog Active owner admission and direct-row runtime registration are
> integrated at `codex/v3` HEAD `751ac08f`, with native cache243, compiled-HIR
> producer8, and artifact schema70. The existing Release tree built 2102/2102;
> after the direct-row storage test and true outside-reader Active boundary
> witness, affected runtime and A4 tests passed, and the full incremental
> relink passed 1923/1923. Focused validation passed 6/6. The exact serial
> consolidated gate passed 51/51 in 64.58 seconds. Receipts are
> `build-all-cache243.log`, `build-all-cache243-final-refresh.log`,
> `ctest-focused6-cache243-final.log`, and
> `ctest-consolidated51-cache243.log` under the active campaign build log
> directory. No throughput timing is claimed.

> **Qualified cache242 checkpoint — 2026-10-02 14:59 UTC.** ConditionalSelect
> region lowering and private compiled-HIR revision isolation are integrated at
> `codex/v3` HEAD `751ac08f`, with native cache242, compiled-HIR producer8, and
> artifact schema70. The existing Release tree built 2046/2046 for cache241;
> its first focused gate exposed a missing graph admission for the select
> consumer. After the reviewed graph and test correction, the affected target
> build passed 367/367, focused region/schema tests passed 2/2, and the full
> incremental relink passed 1926/1926. The exact serial consolidated gate
> passes 51/51 in 62.31 seconds. Receipts are `build-all-cache241.log`,
> `build-region-schema-cache242.log`, `ctest-region-schema-cache242.log`,
> `build-all-cache242.log`, and `ctest-consolidated51-cache242.log` under the
> active campaign build log directory. No throughput timing is claimed.

> **Qualified cache240 checkpoint — 2026-10-02 14:36 UTC.** At uncommitted
> `codex/v3` HEAD `751ac08f`, source includes callable-template and
> generated-callable reuse, the custom-net output adapter, guarded and typed
> dynamic-index reads, projected constant slices, and generic Update regions.
> Boundary-only generic admission and per-member value fanout now reach the
> native backend; canonical integral domains guard invalid indexed reads, and
> exact partial-driver regions preserve Logic4 and Logic9 registration. Native
> cache is 240 and compiled-HIR producer revision is 8. The existing Release
> build tree completed its incremental resume at 1929/1929 steps after one
> mechanical optional comparison repair. Focused validation passes 12/12;
> the isolated cold-entry allocation-failure gate and resolved-driver selector
> pass; the fresh serial consolidated gate passes 51/51 in 64.51 seconds.
> Receipts are `build-all-cache240-resume1.log`,
> `ctest-focused12-cache240.log`,
> `ctest-generic-allocation-warm-cache240.log`, and
> `ctest-consolidated51-cache240.log` under the active campaign build log
> directory. The shared runtime test target was then refreshed (430/430 steps,
> one CXX compile and one link); `ctest-consolidated51-cache240-refreshed.log`
> passes the same exact selector on the refreshed binaries (51/51, 63.29
> seconds). This is functional qualification, not a throughput measurement.

> **Qualified cache235 checkpoint — 2026-10-02 12:24 UTC.**
> Live uncommitted source on `codex/v3` HEAD `751ac08f` carries native cache
> 235 and compiled-HIR producer revision 3. Cache234 added static `always`
> template reuse, ordered Active lowering for supported continuous concatenated
> assignments, and multi-output A2 forwarding. Cache235 fixes output-variable
> formals connected to resolved parent nets and adds mixed-sensitivity A2
> forwarding. The merged Python campaign suite passes 49/49. The current full
> Release build passes 2217/2217; focused validation passes 8/8, including
> schema identity, artifact design, compiled-HIR cache, LLVM region kernel,
> connected remap, native application routing, runtime, and elaboration. The
> fresh serial consolidated gate passes 49/49 in 62.15 seconds. The common
> output-port source matches Vivado xsim's four startup/edge/contention/release
> rows in the interpreter and compiled O0/O2. Receipts are
> `build-all-cache235.log`, `ctest-focused8-cache235-final.log`, and
> `ctest-consolidated49-cache235-final.log` under the active campaign build log
> directory. A final full-target refresh then passes 1692/1692 steps in
> `build-all-cache235-final-refresh.log`; the refreshed binaries pass the same
> exact 49-test selector with `--no-tests=error` in
> `ctest-consolidated49-cache235-refreshed.log` (49/49, 64.15 seconds). The dated
> cache233 checkpoint below remains historical evidence.

> **Integration checkpoint — 2026-10-02 10:03 UTC.**
> Live source is uncommitted on `codex/v3` at `ede7c24e`, native cache 233,
> native metadata 8, and runtime artifact 70. The refreshed Release build
> passes 1915/1915 steps. Focused validation passes 9/9, and the fresh serial
> consolidated gate passes 49/49.
>
> Cache230 gives wide projected writes a dedicated native callback, preserving
> same-owner pending-waveform cancellation and equal-value transactions. The
> 65/129-bit VHDL application compares interpreter, O0 and O2 behavior; native
> callback buffers now cover Logic4 and Logic9 success/failure at 129 bits.
> Wide fused strict reads retain nonidentity mapped leases, and the connected
> remap direct application gate passes. The A3 private stage uses compact
> scheduler tickets with lazy fallback descriptors; its positive runtime gate
> passes after restoring the prior multi-output eligibility rule. Parsed
> five-operation VHDL compact startup and guarded projected constant facts
> remain covered by the elaboration gate. The expression fixture now expects
> `UUUU0110` for an uninitialized upper half; interpreter and compiled values
> agree. Retained named-time delay expressions still did not evaluate in the
> selected HIR trace, so the projected fixture uses literal 2 ps/5 ps timing.
>
> Resolved wide `sv_wire` and `std_logic` projected authority remains covered.
> No-resolution wide `bit_vector` now has a three-role current/LAST/stored
> backing with a sole-owner alias; ambiguous owner provenance takes the checked
> route, and quiet-point recertification restores eligible ownership. A3
> compact tickets also coalesce repeated reservations while retaining exact
> foreign-key order. A certified two-member forwarding path now keeps its
> parent's internal output in a private stage record while retaining the
> original scheduler key, late-observation fallback, stop/retry, and child
> boundary result. One VHDL process can own two unrelated whole no-resolution
> `bit_vector` outputs with separate three-role aliases; delayed sibling writes
> still decline as checked work. No-resolution wide Logic9
> `std_ulogic_vector` now uses the same exact-kind sole-owner alias. The A2
> private parent route also covers selected singleton internal outputs in a
> longer certified forwarding chain; a stopped chain retains its queued parent
> while a released bank lets later members use ordinary update slots.
> Generic projected Logic9 cones now build exact four-plane kernels and run
> natively at widths 1, 65, and 129 under both O0 and O2, with per-frame
> interpreter value and metadata parity. Resolved, delayed, ambiguous, and
> mismatched-kind cases retain checked fallback. Cache232 admits a certified
> multi-parent forwarding join without inserting a scheduler barrier. A parsed
> five-member SystemVerilog diamond consumes all members on settled 0/1 cuts
> under O0/O2; the unequal-depth runtime witness captures the genuine
> intermediate and settled join keys and retains the 1→0→1 output glitch,
> boundary revision, original owners, and event stamps. The default eight
> warmed application windows remain allocation-free with positive forwarding
> consumption. Separate delegating activation-only fixtures preserve native
> region completion, private-state reuse, prepared direct-ready publication,
> and successor-mask coverage. Cache233 extends certified forwarding to wide
> Logic4 values. Graph tests cover widths 65/129/256/1024 and cross-word
> sensitivities; parsed O0/O2 application tests require native 65/129-bit
> execution and complete interpreter metadata parity. The provider validates
> all full-width outputs before publication, and a retained-output allocation
> failure witness proves unchanged results, checked fallback, and native retry.
> Wide internal publication preserves original scheduler keys, full-value and
> revision guards, and the partial-cut stop/late-observation behavior. Explicit
> A4 wide-owner opt-out still takes the checked route. General zero-allocation
> for wide forwarding has not been established. The full architecture and
> throughput qualification remain open. No throughput gain, commit, push, or
> hosted CI is claimed.
>
> Evidence: `build/performance-campaign/simulation-architecture/`
> `candidate-feature-next217-combined-20261001/`, especially
> `build-all-cache233-final.log`,
> `ctest-focused9-cache233-final.log`, and
> `ctest-consolidated49-cache233-final.log`.

> **Next38 feature gate — 2026-10-01.**
> S2 masked static admission now shares a conservative branch-knownness
> proof across runtime and compiler: known case-equality conditions can enter
> the native route, while unknown-capable error branches decline. Experimental
> A4 wide state binds exact SystemVerilog Active single-owner boundary roles,
> including Logic9 `std_logic` storage without a compute kernel; Graph cone
> admission is unchanged. Concurrent publication, historical snapshots,
> allocation failure and checked fallback retain value and owner identity.
> Retired-role reuse plus owned wide UPDATE callback images prove zero
> allocations for warmed 129-bit O0/O2 deposit-plus-run windows with positive
> native completions. A5 integral identity memoization passes semantic gates.
> Native cache schema 208 and runtime artifact schema 70 pass 46/46 freshly
> linked Release CTests across 39 targets. Evidence is in
> `candidate-feature-next38-20261001/`. Wider projected/VHDL authority,
> hidden A2 cones, mapped strict-read leases, broad A3 compaction and
> replacement-proven retirement remain open. Performance work is deferred.

> **Next37 feature gate — 2026-10-01.**
> A3 component readiness now preserves frozen foreign-work ordering when a
> residual ticket advances to a later key; native mask images are proved in
> the actual O0/O2 application route, while checked C++ singleton fallback
> retains its original task and wait PC. JN-3 selects constant-input native
> kernel variants with dynamic inputs still read and validated. JP-3 explicitly
> pins LLVM 22 pipeline tuning defaults. Native cache schema 207 and runtime
> artifact schema 70 pass 45/45 freshly linked Release CTests across 38
> targets. A pre-start constant-bank fixture now builds native dependency
> masks before graph certification. Evidence is in
> `candidate-feature-next37-20261001/`. Hidden A2 cones, wider A4 runtime
> admission, mapped fused strict-read leases, broad A3 storage compaction and
> replacement-proven retirement remain open. Performance work is deferred.

> **Next36 feature gate — 2026-10-01.**
> Certified narrow SystemVerilog local waves are default-enabled with an
> explicit checked-route off switch. A3 direct publication tickets retain
> ordered callbacks with bounded storage. JM-2 protects signal and container
> callback input planes with owned scratch, including wide Logic4 and Logic9
> values. Native cache schema 206 and runtime artifact schema 70 pass 45/45
> freshly linked Release CTests across 38 targets, including the new LLVM
> callback-buffer test and interpreter/O0/O2 scheduling. Compile-only fixture
> fixes and corrected rendered-error fragments are recorded in
> `candidate-feature-next36-20261001/`. Hidden A2 cones, broad A3 storage
> compaction, wide A4 runtime admission, mapped fused strict-read leases and
> replacement-proven retirement remain open. Performance work is deferred.

> **Next35 feature gate — 2026-10-01.**
> Certified narrow SystemVerilog local waves retain internal current, LAST
> and raw-owner state in a private bank, including equal-update transaction
> behavior and late-observation revocation followed by quiet recertification.
> The O0/O2 application fixture uses a SystemVerilog boundary adapter to prove
> private dispatch and warmed zero-allocation deposit-plus-run; generic-origin
> host deposits remain an explicit ordinary boundary path. Certified singleton
> native activations now run, and declined singletons retain their saved
> ordinary task. JP-8 read deduplication, ordinary required-direct-read leases
> and versioned wide ingress guards are implemented. Strict reads require exact
> runtime map identity; remapped fused instances use guarded reads pending a
> separately validated mapped lease. Native cache schema 205, metadata 7 and
> runtime artifact schema 70 pass 44/44 freshly linked Release CTests across
> 37 targets, including 19 scheduling cases per interpreter/O0/O2 mode.
> Evidence: `candidate-feature-next35-20261001/`. Hidden A2 cones, broad A3
> storage compaction, wide A4 admission and replacement-proven retirement
> remain open. Performance work is deferred.

> **Next34 feature gate — 2026-10-01.**
> Wide A4 plane blocks now support owning read leases and versioned current,
> LAST, stored and owner snapshots for Logic4 and Logic9. Wide admission is
> still off. The new focused fixture corrected its complete-owner description
> and found a real Logic9 detachment fault: the second and third Logic9 planes
> are allocated before wide snapshot copying. S0 removes only an unused test
> wrapper; fork tracking and sparse container override storage allocate lazily.
> Native cache schema 204 and 44/44 freshly linked Release CTests pass. Full
> source and binary receipts are in `candidate-feature-next34-20261001/`.
> Performance work remains deferred.

> **Next33 feature gate — 2026-10-01.**
> A1 dynamic multidimensional reads now retain full signed index width,
> left-to-right side effects, exact row-major ordinals and invalid-index X
> results. The new runtime process slab preserves addresses through overflow,
> and suspended callable context is allocated only when captured. JP-5
> prepares supported LLVM intrinsics and substantial switches before codegen;
> explicit signed-min poison and a corrected 32-bit Logic4-only container
> fusion predicate pass direct O0/O2 tests. Certified SystemVerilog regions
> are default-enabled with an explicit off comparison path. Native cache schema
> 203 and 43/43 freshly linked Release CTests pass, including controlled and
> unprofiled default scheduling in interpreter/O0/O2. Vivado xsim completed the
> new array case, but its X/Z and wide-index selections disagree with IEEE
> 1800-2023 section 7.4.5; this is partial external evidence, not full oracle
> parity. Full source/binary receipts are in
> `candidate-feature-next33-20261001/`. Performance work remains deferred.
> The full persistent codebase index was refreshed after the gate.

> **Next32 feature gate — 2026-10-01.**
> JN-5 region kernels use a checked known-Logic4 body with four-state fallback;
> raw LLVM IR, exact values and X/Z decline pass focused tests. A3 bounded
> SystemVerilog component tickets preserve stable ordering and frozen Active
> rounds through late insertions, observation, stop and failure. A5 VITAL
> per-instance maps move to lazy storage. Native cache schema 202 and 43/43
> freshly linked Release CTests pass, including the SDF/VITAL reannotation
> suite and interpreter/O0/O2 scheduling. The runtime fixture now distinguishes
> undriven Z before first SV-wire publication and an optional startup singleton
> from the required two-member native activation and quiet recertification.
> Hidden A2 cones, broader A3/A4 authority and replacement-proven retirement
> remain open. Performance regression work remains deferred. The full
> persistent codebase index was refreshed at 03:53:04 UTC (63,245 nodes;
> 417,224 edges).

> **Next31 feature gate — 2026-10-01.**
> Ordinary host JIT reads require certified direct planes or the checked
> callback route. Narrow Logic9 A4 signal and owner planes retain exact
> current, LAST, stored and raw values until public observation demotes them.
> Native cache schema 201 and 37/37 freshly linked Release CTests pass,
> including a zero-allocation Logic9 slot witness. Logic9 fixture metadata,
> initial U LAST and LLVM cache metadata schema 6 were corrected without
> weakening their value, binding or allocation assertions. JN-5, actual A3
> queue compaction, wide Logic9 A4 authority and hidden A2 cones remain open;
> the full codebase index was refreshed at 02:55:15 UTC (63,160 nodes;
> 416,107 edges). Performance regression work remains deferred.

> **Next30 feature gate — 2026-10-01.**
> Ordinary SystemVerilog comb/FF process-template replay now binds checked
> physical-array element roles. Three instances with six processes prove two
> replays, shared operations, exact leaf reads and per-instance proxy sensitivity.
> The runtime fixture proves right-only X/Z changes and clocked NBA outputs. Native
> cache schema 200 and 36/36 freshly linked Release CTests pass. Broader A5
> state sharing, hidden A2 cones and replacement-proven retirement remain open.

> **Next29 feature gate — 2026-10-01.**
> Runtime artifact schema 70 preserves deterministic shared process-layout
> and operation tables; native cache schema is 199. Selective A4 observation
> demotion revokes the affected family while retaining unrelated authority.
> The freshly linked Release gate passes 36/36 CTests, including artifact
> phases, shared-body codec, physical VPI and interpreter/O0/O2 scheduling.
> Ordinary JM-1 required-direct-read, JN-5, wide/Logic9 A4 authority, hidden
> A2 cones and replacement-proven retirement remain open. No performance claim.

> **Next28 feature gate — 2026-10-01.**
> Narrow Logic4 A4 packed-slot authority and explicit observation/global
> invalidation demotion pass 35/35 freshly linked Release CTests with native
> cache schema 198. A passive VHDL projected-route witness checks exact
> transaction metadata for startup, changed and equal deposits, while native
> completion remains positive on changed measured pairs. Wide/Logic9 authority,
> ordinary JM-1 required-direct-read and hidden A2 cones remain open. No
> performance or replacement-retirement claim is made.

> **Next27 feature gate — 2026-10-01.**
> Copy-on-write process-layout metadata and wide LLVM bitwise lowering pass
> 35/35 freshly linked Release CTests with native cache schema 197. The new
> coverage CTest exercises reference escape, sharing and failure safety; the
> wide-bitwise IR test proves post-pause live-out values without debug-frame
> persistence. This does not close ordinary JM-1 required-direct-read or broad
> region-owned state. Performance regression work remains deferred.

> **Next26 feature gate — 2026-10-01.**
> Generic VHDL projected region runtime dispatch passes 33/33 freshly linked
> Release CTests with native cache schema 196. The application O0/O2 fixture
> requires native projected completions and backend runs, exact final values,
> and a separate observed fallback. Zero-delay publication retains ordinary
> source order; failed staging rolls back uncommitted pending updates. Broader
> VHDL scheduling and region-owned state remain open, with no performance claim.

> **Next25 feature gate — 2026-10-01.**
> JP-3 context-bound LLVM TargetMachine reuse passes 33/33 freshly linked
> Release CTests with native cache schema 195. A per-worker ThreadSafeContext
> serializes lowering and None/Less code generation; foreign module contexts
> retain ConcurrentIRCompiler. The JIT destroys modules before the pooled
> contexts and compilers, and the object cache last. Tests cover actual reuse,
> concurrent lowering and cache-byte identity. No throughput or paired Wall
> claim is made.

> **Next24 feature gate — 2026-09-30.**
> Complete multidimensional physical-leaf families now support direct-read
> planes and graph region closure, while static two- and three-dimensional
> element reads select the exact leaf or checked aggregate slice. An HIR
> branch-order repair prevents complete element bindings from falling into
> temporary-container reads. Native cache schema 194 passes 33/33 freshly
> linked Release CTests. Rank-one fused normalization, generic VHDL runtime
> dispatch and dynamic unpacked-index X behavior remain separate work. This
> gate makes no throughput or paired Wall claim.

> **Next23 feature gate — 2026-09-30.**
> JV-5 dynamic single-bit lowering and generalized A4 wide/Logic9 internal
> input planes pass 33/33 freshly linked Release CTests with native cache
> schema 193. LLVM O0/O2 fixtures cover wide bit-zero extraction and
> sequential updates using a legal one-bit source, plus typed strict-runtime
> failure metadata and unchanged destination sentinels. The generalized A4
> entry retains owner, generation, image and checked-fallback guards. The
> wide-source compiler resize is defensive under current one-bit SimIR
> admission. Generic VHDL runtime dispatch and broader region ownership
> remain open; no performance qualification was made.

> **Next22 feature gate — 2026-09-30.**
> A4 borrowed Logic4 input planes and VHDL projected generic activation pass
> 33/33 freshly linked Release CTests with native cache schema 192. The
> application route proves eight borrowed-plane completions in each LLVM
> mode. Both SystemVerilog backend entry paths now share the same owner,
> generation and committed-image preflight, with checked retry and caller-held
> backend lifetime. The VHDL graph/compiler tests verify projected inputs and
> the committed next-delta cut; generic VHDL runtime dispatch is still open.
> This gate makes no throughput or paired Wall claim.

> **Next21 feature gate — 2026-09-30.**
> A1 multidimensional fixed packed net arrays and JV-3 dynamic select/insert
> pass 33/33 freshly linked Release CTests with native cache schema 191.
> Static continuous packed element slices now retain the original net driver
> through a checked leaf/proxy `WriteUpdateSlice`; the 3D elaboration fixture
> verifies aliases, resolution, and wide partial publications. The full LLVM
> gate covers valid interval directions and explicit admission rejection for
> inconsistent DynamicPartIndex direction metadata. Physical VPI tests guard
> their initial dynamic-index read until indices are known; unknown-index
> conversion remains open. These are feature correctness gates, not a
> throughput or paired Wall qualification.

> **Next20 feature gate — 2026-09-30.**
> The JV-3 narrow dynamic write, JM-4/JM-5/JM-7 compiler slices, generic
> Update frontier, constant startup driver bank, A4 authoritative signal
> planes/readiness, and wide/Logic9 internal activation pass 33/33 freshly
> linked Release CTests. Native cache schema is 190. The generic frontier
> witness checks stable callback order and exact deltas 1/1/1/2; a separate
> projected-cohort pair proves native execution without observation and exact
> current/stored/driver parity with callback-observed demotion. JV-3 keeps
> signed-32 metadata rejection for unsupported extreme bounds. Evidence is
> `candidate-feature-next20-20260930/`. Remaining feature slices precede
> throughput regression analysis and paired Wall qualification.

> **Next19 feature gate — 2026-09-30.**
> The ordinary/FF A5 replay, JV-2 rounded register loads, JM-6 slot-load
> reuse, JV-6 Logic9 lowering, Logic9 native frame export, and VHDL projected
> effect inventory batch passes 31/31 freshly linked Release CTests. Native
> cache schema is 189. The LLVM gate includes all-state Logic9 and
> 65/129/256/1024-bit region cases; VHDL projected outputs retain interpreter
> and O0/O2 transcript parity. The VHDL graph change is an access inventory
> prerequisite, not native VHDL admission. Continue the remaining feature
> slices before measuring regressions or making Wall claims.

> **A3/A5/JP-2 feature gate — 2026-09-30.**
> A canonical Release build passes 28/28 focused CTests after rebuilding the
> affected executables. The fixed-topology O0/O2 application witness proves
> eight additional native completions and zero allocations per warmed public
> deposit-plus-advancing-run window. Reusable future-slot nodes and queue
> capacity close the measured scheduler allocation path; malformed activation
> prefixes and by-value copy failure have rollback witnesses. Generated A5
> replay retains lane-local write ownership and exact public outputs. JP-2's
> optimized verifier switch passes both Release settings while raw verification
> remains unconditional; the LLVM module shapes match. These are bounded
> feature contracts, not a general zero-allocation or throughput claim.
> Continue the remaining A5/Logic9/A4 work before regression analysis.

> **Next17R2 feature checkpoint — 2026-09-30.**
> The combined trusted-entry, wide Logic4 boundary and retained-reference
> coherence slice is integrated with native cache schema 188. Its freshly
> linked semantic gate passes 24/24 tests, including wide LLVM O0/O2,
> allocation and malformed-backing controls, runtime alias/driver failures,
> physical VPI, codecs/artifacts and three-engine scheduling. Synthetic
> width metadata is validated before JIT compilation; initial driver
> resolution refreshes exposed retained container references. This proves
> those feature contracts, not wide internal-cone or Logic9 native admission.
> Continue A3, Logic9 and application binding; defer performance regression
> decisions and paired throughput qualification until feature closure.

> **Native53 matching-throughput gate — 2026-09-30 19:50 UTC.**
> The persistent backend's focused semantic gate passes 19/19. Its positive
> `a2_native_chain` witness runs the backend at O0/O2 and matches Vivado; the
> observer-rich fork witness remains an explicit fallback case. Full
> region-opt-in, unprofiled, fresh-cache canonical parity passes on CPU5 for
> both matching production cases: `original_throughput` and
> `mixed_throughput`, each six summaries/seven correctness lines and canonical
> SHA-256 `d50a786447827198d6b31780f9a6199dcb347ec7639454bf67c0fdb3b367bd59`.
> One diagnostic end-to-end observation is 261.901 s Verilog and 23.521 s
> mixed, without paired Wall qualification. The bounded production route
> census finds 25,071 Verilog region attempts but zero backend executions;
> matching mixed has zero attempts. CPU5 cycles/instructions and DWARF
> call-path profiles are retained separately from timing. The historical
> `mixed_codec` case is outside this matching-throughput benchmark pair.
> Late thirds of the saved two-microsecond PMU recordings differ: Verilog
> emphasizes update dispatch, driver commit and container publication, while
> matching mixed remains interpreter arithmetic/container work. These bounded
> samples do not establish full-run hotspot shares.
> The earlier frozen-control mixed profile labelled "throughput" has the
> `mixed_codec` canonical SHA-256
> `461116a87e4eff4f85725fdc5590fe0aaf8fa642564834d2e86fb949c8c58926`;
> it is not a matched
> `mixed_throughput` control timing observation. Collect a matching six-by-twelve
> control before any paired qualification.
> Evidence: `candidate-native53-20260930/gate-receipt.json`.

> **Persistent native region integration — 2026-09-30 18:27 UTC; validation pending.**
> Integrated the reviewed 53-path composition: persistent narrow Logic4 LLVM
> region execution, runtime/application backend ownership, quiet-point exact
> backend reuse, durable public-reference pins, proxy/leaf differential tests,
> checked ABI direct-update bounds, and actual-cohort callback-word regression.
> Native cache identity is 187; runtime artifact69 and native metadata5 remain.
> Patch SHA-256:
> `9649aa993f780c6801a1eb48cb73bee11cca4d1148e66fb35809f23aa64f0882`.
> Receipt: `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-region-native-ready-20260930T1824Z/integration-receipt.json`.
> Exact base, candidate and unchanged repair hashes match; patch checks pass.
>
> Executable validation is next. The compiled scheduling witness must prove
> actual native backend runs and original member completion at O0/O2, alongside
> canonical output. Production region enablement remains opt-in. The allocation
> fixture covers native execute() only, not scheduler/publication/completion.
> Retained ContainerValue backing coherence, trusted entries, preallocated runtime
> staging, wide/Logic9 native execution and hidden internal cones remain open.
> Full correctness must include the unprofiled timing route: profiling masked
> the callback-word defect repaired in the preceding gate. Instrumented train
> parity alone cannot qualify that route. No Wall or route-retirement claim.
>
> **Region state safety gate complete — 2026-09-30 18:26 UTC.** Before the
> native integration above, all 17 focused CTests passed after freshly relinking
> fourteen targets with twelve build workers and requiring actual physical
> aliases in VPI. The 291-path source inventory and fifteen binary/archive hashes
> were reverified with zero drift. Receipt:
> `candidate-region-safety-v186-20260930/gate-receipt.json`, SHA-256
> `bc3f42ae36f5d6294258db3b3102e00a9117aaff1e351b7f67229d5b1f47d5e4`.
> Testing repaired two production defects: cohort slot batches could skip queued
> wide callback words, and region admission rejected an explicitly sized whole
> leaf write despite its proven full ownership. Fixtures now separate native
> VHDL execution from immediate-observation demotion, verify metadata5 with
> actual persistent/transient cache layouts, and reject genuine partial/proxy
> writers across components while preserving unaffected eligible work.
> Earlier failures are retained. This gate is focused correctness evidence;
> it contains no new full-throughput or paired Wall qualification.

> **Region state safety integrated — 2026-09-30 16:58 UTC; validation pending.**
> The reviewed 23-path composition adds v186 compiler persistence metadata,
> preflighted original-frame register completion, checked aggregate/leaf graph
> closure and allocation-free family observation invalidation. Exact live-base
> and final hashes match; patch checks pass. Composition receipt:
> `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-region-safety-composition-20260930T1655Z/integration-receipt.json`;
> patch SHA-256:
> `dcd48e4ed390c9cb5608ad75b7880d5491c7fbb850d0f39ef9ad3b1b8fcb4ea7`.
> Native cache identity is 186. A three-path follow-up at 17:07 UTC allows
> installed unused coverage services for an already certified coverage-free
> prefix. Explicit coverage operations retain unknown signal access and cannot
> enter a region; callback signatures do not prove signal-free effects. Patch:
> `605f87643ce997bc1680b567be8a5650f7edf49a49bbd8176b567da20b5570a7`.
> Application LLVM region equivalence remains false. Quiet-point/durable-reference
> integration, proxy/leaf runtime
> differential witnesses and the combined executable gate are next.
> The 16:51 passing receipt below covers the preceding source snapshot.

> **J-C prototype and full canonical correctness pass — 2026-09-30 16:51 UTC.**
> The consolidated gate passes 14/14 tests, including runtime/LLVM, actual
> physical-alias VPI, expressions, seventeen scheduling witnesses across
> interpreter/O0/O2, allocation-failure controls and publication-pool checks.
> The full Verilog throughput case and historical mixed-codec correctness case
> matched their respective canonical transcripts and fingerprints with unchanged
> external RTL and fresh native caches on CPU5. Verilog canonical SHA-256:
> `d50a786447827198d6b31780f9a6199dcb347ec7639454bf67c0fdb3b367bd59`;
> mixed-codec canonical SHA-256:
> `461116a87e4eff4f85725fdc5590fe0aaf8fa642564834d2e86fb949c8c58926`.
> Receipt: `candidate-jc-runtime-20260930/gate-receipt.json`, SHA-256
> `dacbb1b715448e62d28d779c48e0e4da79ec7bf9e79347eeec0a9a5c985c7e47`.
> The 290-path intended-source inventory and thirteen binary/archive hashes
> have zero postrun drift; the runnable CLI and dirty source are archived.
> These instrumented train runs are correctness evidence, not Wall qualification.
>
> Validation repaired coverage admission: configured counters and installed
> coverage services conservatively reject region replacement while ordinary
> ordered execution remains eligible. Unconfigured default-enabled counters
> no longer reject every wave. The scheduling runner accepts appended census
> fields while retaining exact legacy-route totals and zero region counters.
> Earlier failed diagnostics are retained with their repair explanations.
>
> Application LLVM replacement remains disabled. The tested C++/mock prototype
> preserves committed inputs and original publications; persistent native state,
> register/frame completion, quiet-point recertification, exact alias closure and
> full observation/coverage admission remain the next integration dependencies.
> The reviewed v186 register-completion package is still scratch-only. Retained
> ContainerValue backing coherence needs a separate fix. No route is retired,
> and neither the <=15-second median Wall target nor the language ratio is qualified.
>
> **Hourly audit, 16:33 UTC; evidence refreshed at 16:51 UTC.** Physical A1
> admission, production rewrite incidence and the region prototype now have
> passing semantic evidence and full canonical parity. The shortest dependency
> is to integrate reviewed completion/recertification/alias safety, gate the
> persistent evaluator, then connect its authenticated application boundary.
> Earlier checkpoints below are historical and retain their original limits.

> **Physical A1 semantic gate complete — 2026-09-30 15:43 UTC.**
> The final consolidated gate passes 21/21 tests plus the separate actual VPI
> gate with `FSIM_REQUIRE_A1_PHYSICAL_ALIASES=1`: 22 distinct tests. Receipt:
> `candidate-a1-physical-20260930/semantic-gate-receipt.json`, SHA-256
> `5a7e6525cf04cad140d391dd72756a67c6da26fbc41e4fed598867a9fc3b0309`.
> The 288-path source manifest and eighteen executable hashes show no drift.
> Physical leaf read/write assertions, mixed projected-writer fallback,
> artifact/schema round trips, runtime/LLVM, expressions, seventeen scheduling
> witnesses across interpreter/O0/O2 and failure controls pass.
>
> Testing corrected two production defects: initial leaves now use authoritative
> proxy slices instead of requiring equality with the unmaterialized container
> cache; physical VPI words retain the logical word-handle mutation policy.
> The specify fixture now proves unrelated a/z timing paths do not suppress
> eligible array splitting. Unsupported array-condition syntax is not claimed
> as a passing timing-terminal witness. All earlier failure logs remain.
>
> The separate two-path census package passes its instrumented/uninstrumented
> fixture gate. Fresh-cache elaboration and bounded 2 us runs on CPU5 match both
> prior partial stdout hashes. Verilog has 585 physical families / 5,259 leaves,
> with 19,986 leaf reads, 4,866 sensitivity rewrites, 489 element writes and 612
> slice writes. All physical families have a leaf writer; 4,158 slice writes
> conservatively retain their width fallback. Mixed has no eligible families.
> These are elaboration incidences, not runtime frequencies or speedup evidence.
> Census gate receipt SHA-256:
> `ffbfa94f9e300c8e928a7d5dcc879f956b55a95f00135b21f177baf44850f3bc`.
> Bounded receipt SHA-256:
> `88258d1b82abfde10cd14f112baa528a20d13b747902721278405692f5426346`.
> The post-A1 Verilog graph has 1,052 components, 25 structural candidates and
> 50 potential internal signals; every component epoch remains stale. Full
> canonical parity and paired Wall qualification remain pending.
>
> **Compiled-region prototype integrated — 2026-09-30 15:51 UTC.** The reviewed
> seventeen-path composition
> is `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-jc-runtime-composed-20260930T1542Z/source.patch`, SHA-256
> `ad33124bb1dd212909aba659b5103ba76fe20c63851ee34e8fafb32265cf5e8a`.
> Exact base/final hashes and patch checks pass. Executable validation is next.
> The prototype captures committed inputs and retains original owner publications,
> with atomic scheduler batch preflight and allocation-failure witnesses.
> Application LLVM equivalence stays false until original executor register/frame
> state is preserved. It is not hidden-state collapse or allocation-free native
> region execution. Persistent LLVM state, copyback, quiet-point recertification
> and checked graph alias closure are the active successor work.
>
> **Hourly audit, 15:33 UTC.** Physical admission and executor provenance are
> concrete progress. Shortest dependency: finish physical semantic validation,
> then measure actual rewrites before production region admission. Full throughput
> qualification remains unfinished; no old route is retired on this evidence.

**Physical admission integrated, 2026-09-30 15:08 UTC.** The nine-path
post-elaboration element-net package is applied with exact hash verification
(runtime schema 69, native cache 185). Eligible one-dimensional Logic4 wire/tri
families retain logical names/proxies while static accesses and contained
sensitivities use leaves. VHDL projected-writer families remain aggregate;
unmodeled register effects decline narrowing. The missing stochastic-queue
output-width case is corrected. Patch SHA-256:
`27aa05ed0776deb470f57441042b9906e0768020e6c09c944d3e96350305bde1`.
Executable validation and production rewrite census are pending. This is an
automatic eligibility transform, not an environment opt-in. No timing claim.


**Earlier executor-provenance gate, 2026-09-30 15:06 UTC.** Executor body/revision/remap
provenance and deferred-factory observation preparation pass the freshly rebuilt
15/15 focused gate. Four read-only operation accesses were corrected to avoid
invalidating exact revisions. Native-positive mocks declare exact bindings;
separate static and masked fixture pairs preserve both route assertions.
Receipt: `candidate-executor-provenance-20260930/gate-receipt.json`, SHA-256
`edbe74c2da9ce6ff733ccf8ea760d0046b27e8533424feeece08dedd5e6a4e09`.

Fresh elaboration and bounded 2 us production runs match previous partial
stdout hashes in both cases; this does not establish full fingerprint parity
or throughput. Access inventory is now complete with zero opaque incidences.
Verilog has sixteen structural candidate components and 41 potential internal
signals, but all 491 component epochs are stale. Mixed has no structural
internal candidates. Quiet-point recertification and actual region replacement
remain unfinished. Physical element-net composition is under final proof
review; it will apply automatically to eligible families. The physical-alias
environment variable is a test requirement, not an implementation opt-in.


**Earlier gate and integration, 2026-09-30 14:17 UTC.** Certified leaf direct
reads pass both targeted selectors and the 13/13 focused gate, including real
65/129-bit A/B-plane mutation and O0/O2 mixed direct/checked read witnesses.
Production retained its reviewed hashes; only fixture repairs were required.
The strengthened descending wire-array VPI fixture is now integrated for an
unsplit baseline before physical admission. Physical R2 remains unapproved:
mixed boundaries can share the same proxy with VHDL projected writers, requiring
per-family fallback until cycle-exact alias behavior is proven. The source-only
successor adds that guard and a focused mixed-language witness. Fork/filter
materialization and bounded dynamic ownership retain their passing gate.
Physical splitting and hidden cones remain disabled; see the resume checkpoint
for receipts and remaining dependencies.

**Active implementation resumed after the PMU repair.** The rebuilt control
is frozen; the corrected interpreter and ABI v2 pass the integrated semantic
gate and both full canonical transcripts. Full diagnostic profiles regress
(Verilog 208.42/54.28 s, mixed 36.04/25.44 s instrumented simulation versus
control); these are not Wall qualification. Ordered SV dispatch now passes
both full canonical cases. Its refreshed profile attributes 94.12% of Verilog
samples to the host executable and 2.05% to generated code. The next priority
is reducing signal/driver/publication traffic through exact element nets and
certified regions. See [performance-resume.md](performance-resume.md).


This document integrates the 2026-09-29 proposal with the owner's subsequent
implementation decisions. Those decisions govern this program and supersede
older campaign instructions to pause, preserve fsim-specific net-hop deltas,
or decide retention from a single Wall observation. Source anchors and profile
numbers below describe `ede7c24e`; estimates rank investigations, not promised
savings. This document preserves the original proposal supplied from
`/tmp/simulation-performance-architecture.md`; that temporary source is no longer present.

- IEEE 1800-2023-aligned Verilog scheduling becomes the default, with no legacy
  mode. Continuous assignments and implicit port assignments use Active net
  updates; Active/Inactive work drains before NBA processing; `$strobe` and
  `$monitor` publish at the end of the time slot. Establish an external-oracle
  witness and corrected interpreter reference before optimizing this behavior.
- VHDL keeps projected transactions and exact simulation cycles. SystemC
  updates and mixed-language boundaries remain explicit. The proposed
  delayed-publication cone algorithm is excluded: balanced path lengths do
  not establish its correctness for the required event and observation model.
- Observation is immediate and exact, including current/last values, original
  drivers, pending updates, and event/transaction metadata. Observation or
  mutation materializes affected state before returning control. Only affected
  regions demote; promotion requires a quiet point and renewed certification.
  Full detailed coverage survives through instrumentation or selective demotion.
- A public JIT runtime ABI v2 separates shared callbacks and instance frames,
  defines trusted entries/direct-plane capabilities, migrates in-tree users and
  Linux/Windows layout checks, and explicitly rejects incompatible objects.
- Cache identities include ABI, scheduling semantics, backend tier, target
  features and LLVM arguments. Each code-generation batch changes the cache
  schema; a correctness-only compiler concurrency repair need not change it.
- Keep backend `None` for ordinary/oversized kernels. Initially use `Less` for
  fused/masked kernels and templates with at least 64 instances, capped at
  16,384 IR instructions. Read deduplication is limited to tiered kernels;
  changing the cap requires paired evidence.
- Native regions require preallocated staging and a non-unwinding entry
  boundary. Effects that cannot satisfy that contract use checked execution.
  Validate all valid Logic9 encodings before exploiting unused codes.
- Admission depends only on semantic and graph properties: no benchmark names,
  module paths, polynomial constants, or special workload widths.
- Final qualification requires both full Verilog and mixed throughput cases
  at **≤15 seconds median Wall**, and Verilog at **≤1.05 × mixed Wall**.
  Slowing mixed to narrow the ratio does not meet the objective.

Implementation starts from restored `ede7c24e348949b8eebcd37704931a0db8027afa`.
The initial tracked worktree was clean. Preserve `.codebase-memory/`,
`phase.fst`, `scripts/__pycache__/`, and
`src/app/application_executors.cpp.orig`. No commit, push, or hosted CI is
authorized by this program. A GPT-6-Sol/high execution owner alone runs
builds/tests/profiles/timings, using at least 12 build workers; up to five
GPT-6-Luna/max workers implement or audit disjoint source scopes.

### Delivery ledger

| Milestone | Scope | Status / gate |
|---|---|---|
| M1 | Architecture, frozen control, timestamps, allocation/JIT census, task/commit tracing | Control rebuilt/frozen; phase, allocation/JIT census and task/publication tracing implemented with focused evidence. Production measurements and final paired qualification remain incomplete. |
| M2 | IEEE witness/reference, staging and compilation safety, S0, memory quick wins, J-A | Nineteen scheduling witnesses pass interpreter/O0/O2, with external-oracle evidence recorded. Captured origins, staging failures, non-unwinding boundaries and fixed-topology publication allocation gates pass. No-cache race and JV-1/2/3/4/5/6/7/8/9/10/11 plus JM-4/5/6/7 are implemented as bounded slices with focused gates. Next25 JP-3 adds context-bound LLVM TargetMachine reuse with concurrent lowering and cache-identity tests; Next27 adds wide-bitwise IR and live-out gates. Next32 adds the JN-5 checked known-Logic4 body with raw IR proof and exact X/Z fallback. Next33 JP-5 prepares eligible LLVM intrinsics and substantial switches before codegen, with explicit signed-min poison proof. Next35 adds JP-8 tiered read deduplication and retires the unused S0 test wrapper. Next37 pins JP-3 LLVM 22 pipeline tuning defaults explicitly and tests JN-3 dynamic-versus-constant native variants. Broader codegen qualification remains. |
| M3 | A1 element nets/ranges and J-B ABI/direct reads/tiering | Schema69/cache185 physical post-elaboration admission passes the 22-test semantic gate, including actual leaf rewrites, VPI handles/policy and mixed projected-writer fallback. Next21 adds fixed multidimensional packed net arrays, static continuous packed leaf-slice lowering and 3D/VPI witnesses. Next24 adds complete multidimensional physical-leaf direct-read planes, graph family closure and static two-/three-dimensional leaf/slice reads; its 33-test gate passes. Range sensitivity, ABI v2, certified direct reads and 63/64-instance tiering have focused passing gates. Next31 requires ordinary host JIT reads to use certified direct planes or checked callbacks, with LLVM O0/O2 gates. Next33 adds checked multidimensional dynamic reads and fixes generic object-read retention for 64-bit and Logic9 indices. Vivado xsim completed the new case but diverged on invalid X/Z and wide indices; full oracle parity for that case is unresolved. Proxy/leaf observation, driver identity, force/release and allocation-failure witnesses cover narrow and 65/129-bit storage. Production rewrite incidence is measured, and both full canonical throughput cases pass at the J-C checkpoint. Next35 proves ordinary required-direct-read leases and bounded JP-8 deduplication; that dated identity-only restriction has since been lifted. Cache230 validates mapped strict-read leases for nonidentity instances, and `fsim.application.connected_remap_direct` passes O0/O2 with Logic9 all-state coverage. Static-leaf physical admission remains conservative; the new runtime physical-net witness passes checked aggregate dynamic reads in interpreter/O0/O2. Invalid X/Z and wide-index XSim oracle lines still differ (12/14 normalized lines match after an outside-sandbox run), and complete fallback coverage and paired Wall qualification remain. |
| M4 | Shared RegionGraph, immediate materialization/demotion, A2/J-C Verilog cones | Graph/provenance, register completion, alias closure and coverage admission pass focused gates. The persistent native backend, quiet refresh and durable pins pass the native53 19-test gate and full unprofiled matching-case canonical parity. Retained fixed-shape ContainerValue coherence passes next17R2. The generic Update frontier passes exact callback-order/delta and ticket-lifetime tests in next20. Next32 adds bounded composite SV tickets with exact stable-order, frozen-round, stop/failure and late-observation controls. Next22 adds VHDL projected generic activation graph/compiler coverage; Next26 adds runtime dispatch with positive O0/O2 native completion and observed fallback. Next33 enables certified SystemVerilog regions by default, retaining an explicit-off comparison route and controlled/default scheduling checks. Next35 retained narrow internal state privately across native local waves. Current cache252 additionally admits certified SystemVerilog Active Logic4 forests, joins, unequal-depth cuts and wide internal roles to bounded private forwarding; unsupported effects keep original-key checked execution. The focused VHDL projected foreign-cut witness passes, while broader VHDL and production-performance qualification remain separate. |
| M5 | A5 shared lowering/programs, template-instance artifacts, constant drivers | Generated and ordinary/FF process-template replay, occurrence lowering reuse, and schema67 shared-body/instance-override encoding pass codec/artifact gates; consumed process-vector capacity is released. Next27 process-layout metadata shares immutable backing until a public reference escapes, while exposed copies remain independent. Next29 runtime artifact schema70 roundtrips deterministic shared process-layout and operation tables with exact-value tests. Next30 extends ordinary comb/FF replay to used physical-array element roles with checked per-instance remapping and a three-instance, six-process differential fixture. The generated replay witness retains lane-local write ownership and exact public outputs. The constant startup driver bank passes its next20 runtime gate; current cache252 additionally proves single-register Logic4/Logic9 startup forms and known-Logic4 SystemVerilog CopyRegister forms, with unsupported shapes on the checked path. Next32 moves VITAL per-instance maps to lazy allocation with SDF/VITAL reannotation gates. Next33 adds a stable-address process slab with overflow storage and lazy suspended callable snapshots. Next38 memoizes integral identity parsing with collision, lifetime and failure coverage. Consumed design process vectors, shared artifact bodies/layouts, row-backed process programs, and eligible template replay are implemented; the replaced route's sparse normalized-program payload storage was retired. Dense lookup remains, and effect/remap/coverage exclusions limit replay. The end-to-end savings from this A5 subset are unmeasured. |
| M6 | A3/A4/J-D compiled regions, replacement coverage, J-E retirement | Persistent narrow Logic4 LLVM execution and its provider boundary pass native53; trusted narrow entries and the guarded wide Logic4 boundary wrapper pass next17R2. The A3 fixed-topology public route proves eight O0/O2 native completions with zero allocations per warmed deposit-plus-run window, and activation failure rollback passes. Next19 adds Logic9 native frame export; next20 adds A4 authoritative planes/readiness and exact wide/Logic9 internal activation fixtures. Next22 proves eight borrowed-plane completions per LLVM mode and shares strict preflight across both SystemVerilog backend entries; next23 generalizes wide/Logic9 internal plane validation. Next28 adds narrow Logic4 authoritative packed slots with explicit public-observation and global-invalidation demotion, plus exact passive VHDL transaction metadata. Next29 selectively demotes only the observed A4 family while unrelated components remain authoritative. Next31 adds narrow Logic9 authoritative slots and allocation-free late unbinding with exact nine-state current/LAST/stored/raw values. Next32 proves bounded composite ticket compaction and recovery; broader queue storage compaction remains. Next35 validates narrow A2 private-state reuse, accepted native singletons and warmed O0/O2 zero-allocation windows under SystemVerilog boundary origins, plus versioned wide ingress guards. Next37 gives native activations readiness masks and repairs residual-ticket ordering across unrelated frozen work; checked singleton fallback remains ordinary. Next38 adds conservative masked error-branch admission and exact experimental wide single-owner A4 state for SV Active boundaries, with concurrent retained-snapshot, failure/retry and warmed 129-bit O0/O2 zero-allocation witnesses. That dated Next38 evidence did not establish general allocation freedom or full region-owned state. Current cache233 adds resolved sv_wire/std_logic projected authority, dedicated projected native callbacks, compact A3 private-stage tickets, and no-resolution bit_vector and std_ulogic_vector three-role sole-owner aliases. Two whole no-resolution outputs from one pure process qualify; ambiguous or delayed writers take the checked route. A2 privately stages selected singleton internal parents in longer certified forwarding chains without constructing their public update slots/tokens, retaining original scheduler keys and checked fallback. Generic projected Logic9 cones now run exact four-plane native kernels at widths 1/65/129 under O0/O2, with per-frame interpreter parity. A certified multi-parent SystemVerilog forwarding diamond now admits two independent roots and a join with full internal-read provenance. The actual unequal-depth callback declines the intermediate cut at its original key and preserves the later settled cut and output glitch; default forwarding and activation-only native application routes retain separate positive gates. Cache233 extends typed A2 forwarding to wide Logic4 internal signals with full-width output staging, original-key and revision guards, and default 65/129-bit parsed O0/O2 native execution. Graph tests cover 65/129/256/1024 bits; retained-output allocation failure proves unchanged outputs, checked fallback and native retry. The wide partial-cut stop/observation witness retains complete current/LAST/stored/raw roles; explicit wide-owner opt-out still declines. General wide zero-allocation and broader A2 cone closure remain unfinished, as does paired throughput qualification. The JN-6 pure-wave kernels and bindings are already absent from live production, public headers, and tests; generic fused routes remain until replacement coverage proves retirement safe. |
| Current cache252 | Bounded delivery and current qualification | Certified SystemVerilog Active Logic4 A2 forests, joins, unequal-depth cuts, wide roles, and blocking immediate/private reentry have focused interpreter/O0/O2 and original-key receipts; unsupported or observed cases retain checked execution. Generic A3 tickets authenticate retained logical members beyond the offered 64-task prefix and keep strictly future Active receipts outside the current frame. The admitted VHDL projected cycle path passes its foreign-cut/future-receipt and metadata-parity test. The relinked 90-name bounded selector passed 90/90 before the compact validator codegen trial, and the strict warmed width 1/65/129 O0/O2 allocation gate passed separately. On the compact validator source before the scheduler recycler change, the all-target build, five affected focused gates, unprofiled 65/129-member Generic fixture, and exact 90-name coherent selector pass. The targeted recycler, completed-observation cache, alias-family slab, redundant value-only recertification clear, geometric WorkQueue growth, and scoped external ranged-boundary frontier admission build cleanly. The corrected native invalidation witness, exact 18-name scheduler selector, 91-name coherent selector and separately corrected native boundary app witness provide passing receipts for all 92 distinct selected names on unchanged production. A profiled diagnostic shows both O2 frontier modules finish optimization, and the frozen ThinLTO control passes both full canonical cases. The prior combined ThinLTO candidate predates the recertification clear and stopped without a full original-case verdict at its 600-second diagnostic cutoff. The current geometric/ranged-boundary ThinLTO candidate is frozen with complete source/library provenance; its clean original-case preflight likewise stopped at the bounded cutoff with five completed summaries matching control, while instance 0 and mixed remain incomplete. The previous bounded sample found queue reservation and variant movement; after geometric growth the current quiet ThinLTO sample instead finds ProcessTable view, static next-delta queueing, packed-value initialization and native resume among the largest self costs. Neither partial profile establishes end-to-end savings or paired throughput. The earlier 86/90 and strict-route allocation failures are historical; the dated Next38 limitation is superseded for the tested fixed-shape routes, while general allocation freedom remains unproven. General A2/VHDL admission remains selective; paired throughput and hosted CI are unqualified. The current J-E audit supports no additional deletion. |

### Foundation and current diagnostic evidence

The Tier183 foundation passes the configured-target build and all 28
consolidated focused tests. Evidence is under
`candidate-tier183-foundations-20260930/`, including named-event oracle parity
and A5 census on/off parity. Its bounded 2 us production probe reports
1,463,465 Verilog ordered-wave attempts, of which 1,462,388 return zero solely
because the 16-wrapper budget is exhausted; only 41,880 of 89,875,072 offered
members execute through this route. Mixed has no SV-wave attempts in the same
interval. The subsequent direct ordered-dispatch bridge retains the wrapper
limit and accepts all 25,065 offered batches / 1,506,714 members in the same
bounded probe, with zero declines or failures. It passes 28 focused tests and
both full canonical transcripts. This establishes route execution and
correctness, not a Wall speedup or final region qualification. The frozen
bridge, source manifests and separate full profiles are recorded under
`candidate-ordered-dispatch-20260930/`.

Native cache184's checked non-unwinding module contract, the thirteenth
permanent scheduling witness and scheduler queue-storage reuse pass the full
configured-target build and all 29 focused tests. The twelve fixed-topology
publication fixtures record zero steady allocations; wide staging fault
injection still verifies exact rollback and retry. These are focused correctness
and allocation results; full throughput parity and paired timing for that
batch remain unverified. Physical element nets and hidden state/cones remain
scratch only. Earlier receipts below retain their original source boundaries.

The A1 scratch rebase preserves captured publication origins and the current
callback phase. Its aggregate-alias wire fields use the existing generic
aggregate codec with a new schema, subject to round-trip validation. Physical
admission remains disabled until late observation and mutation can switch an
alias family to exact aggregate authority without rejecting legal writes.
Independent leaf last values must survive sibling changes; pending updates
must retain their identities and scheduling order across that transition.

The integrated A2 structural certificate identifies pure SV Active subsets,
internal-signal candidates and public boundaries. It records capability epochs
and exclusions. Its combined full build, 29 focused tests and permanent
sixteen-case scheduling corpus pass, including the retained alias cache repair.
The runtime still grants no hidden-state execution capability. A bounded census
of both full designs finds zero internal candidates because the graph-wide
access inventory is incomplete: 491 Verilog components (44,719 members) and one
mixed component (eleven members). The current visitor conflates operations that
are ineligible for pure execution with operations whose signal accesses are
unknown. The next correction must separate these contracts, keep opaque effects
conservative, and report actual remaining exclusions before region execution. A DAG supplies only one structural condition; it does not
establish settled inputs or permission to execute. The first private-state
implementation must preserve each original process
activation and publication position in the scheduler. Boundary updates retain
their original drivers and captured origins. Eager topological draining or
end-of-cone publication is not justified by the current graph. Later removal
of scheduler entries requires its own equivalent-order proof and witnesses.

The combined v2 build and current focused semantic gate pass.
Evidence under `candidate-v2-integrated-20260929/` records eleven passing
interpreter scheduling witnesses and four passing range fixtures in each of
interpreter/O0/O2. Native `continuous_active` initially exposed a lost SV origin
in blocking-word publication. After repairing whole-word and slice paths, all
eleven witnesses pass O0/O2 with positive native admission for every case.
The corrected fork ownership proof passes runtime/staging. A5 overlay reuse
now produces ten lowered/eight replayed occurrences, and the full core and
expression application suites pass after native-origin and sampled-history
repairs. The full elaboration suite now passes after a real signed static-index
repair and conservative RegionGraph handling of valid zero-width VHDL arrays.
The corrected gated-history regression and the complete runtime suite pass.
The LLVM suite now passes after unused-register cache replay was repaired
with source-width/offset validation and corrupt-used-width rejection.
The 129-bit Logic9 regression verifies checked-callback normalization; wide
Logic9 direct-read admission is still unsupported and remains next-batch work. New sampled-history probes
remain diagnostic until their semantic scope is established. The live status and hourly
progress audits are in [performance-resume.md](performance-resume.md).
No throughput qualification or complete ABI migration claim follows from these
focused passes. The newer coherent foundation gate passes 23 focused CTests, including
both codecs, artifacts, domain-local cycle analysis, selective observation
preparation, failure-atomic demotion, and scheduler-contiguous SV batching.
Receipt: `candidate-region-foundation-20260930/foundation-gate-receipt.json`,
SHA-256 `35fc5e8ef84cd2baf4a7fc8963e1fb72df2e050137bf7cd80c844091f85dcf09`.
The sampled-clock fixture passes interpreter/O0/O2 with native clock-driver
admission; its program-history disagreement with Vivado remains explicitly
separate from the IEEE-derived expectation. The subsequent ordered-wave
candidate passes its coherent build and 23 focused CTests. In both O0 and O2,
the wide/X/Z witness has all 26 processes native and executes three ordered
waves of 20 members. Per-process scheduling and callbacks remain; this is not
final compiled-region state or cone collapse. LLVM argument/tiering work,
trusted activation and element-net bridges remain scratch candidates. Evidence
is under `candidate-ordered-wave-20260930/`; no new full-workload timing
measurement covers either foundation batch.

The following checkpoint history records what was verified at each earlier
batch. Historical pending items are superseded by the current status above;
the old receipts remain evidence only for their exact source revisions.

The foundation receipt is
`build/performance-campaign/simulation-architecture/candidate-m1-validation-20260929/foundation-source-gate-receipt.json`.
It binds nine passing focused CTests and the unchanged production/test inputs
for that batch; the allocation-failure fixture remains unresolved and is not
included in those passes. Later scheduling edits require new verification.
The separate SV scheduler core passes nine isolated Clang22 Release scenarios
with `-Wall -Wextra -Werror`. The integrated default-off runtime/LLVM/schema
CTest checkpoint passed before later Logic9 regressions were added. After
correcting `WaitFor(0)` and `WaitRegion` dispatch, the current interpreter
matches all ten verified Vivado witnesses using fresh workspaces:
`candidate-m2-validation-20260929/fsim-reference-witness-waitfor/summary.json`.
This covers the eight SV witnesses plus mixed VHDL cycles and inertial delays;
it is not a complete standard-conformance or optimized-native claim. A corrected
allocation-failure fixture proved a real retry bug: the fused cohort catch
path cleared its pending snapshot, losing failed owners on resume. Owned
static/masked recovery now preserves pending work and only marks commit
scheduling after enqueue succeeds. The focused regression passes 1/1:
`candidate-m2-validation-20260929/staging-masked-fixture-ctest.log`.
The static sweep covers four actual cuts and accepted-prefix staging one;
the masked sweep covers six. Counts and hashes are bound by M2
`staging-pass-receipt.json`. Fault injection ends at successful staging/enqueue,
before publication callbacks. Projected and separate normal-output routes retain
their prior non-replay contract; broader preallocation/non-unwinding work remains
in M6. The coherent M2 runtime, staging, LLVM, schema, and application-base gate now
passes, including generic/SV direct/external zero waits and Logic9 ingress.
All ten interpreter witnesses still match. The expressions gate matches values
and timing but fails its required native-process count because the temporary
v1 guard rejects SV provenance; preserve this requirement for v2 verification. ABI v1 explicitly rejects new
scheduling-tagged kernels; callable ABI v2 migration must precede native
qualification. The allocator smoke passes Clang Release in its separate build;
its full CLI integration remains pending.

An eleventh external-oracle witness now covers delayed NBA from an SV program.
Vivado produces Reactive/Re-Inactive/final-strobe values `0,0,1`; the frozen
coherent M2 interpreter produces `1,1,1`. This proves a remaining captured-origin
bug: the update must mature in Re-NBA. The permanent scheduling runner now
includes this expected transcript; the phase-origin repair is pending. Evidence:
M2 `vivado-reactive-re-nba-v3/` and `fsim-reactive-baseline/`. ABI v2 C/C++ native
layout tests pass, including Windows x64 target C layout assertions; this is not
a hosted Windows execution claim.

The A1 range checkpoint is retained at `/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a1-range-candidate` with
`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-a1-range.patch` and integration notes beside it. Runtime helper and
elaboration helper syntax pass Clang22 O2 with project warnings as errors;
isolated value/range-normalization tests pass (including all valid Logic9
states and 65/129/256/1024-bit values). Evidence is under the M2
`a1-scratch-helper/` directory. This does not validate interpreter integration,
source-level sensitivity, artifacts, or native range handling. The current
interpreter baseline for the Vivado-verified range fixture matches two of four
marker groups: unrelated element/bit changes incorrectly wake `always_comb`
and `@*`; selected-bit and dynamic-index groups match. Evidence is M2
`fsim-sensitivity-reference/comparison.json`, with identical source SHA to the
oracle. Initialization events are excluded by the declared marker comparison.
The candidate filters fanout before queueing and conservatively excludes legacy native routes
that have no range certificate. It is now integrated with scheduling/staging fixes preserved and runtime/native
schemas 66/181. The full changed expression/process lowering TUs pass strict
Clang22 syntax checks with unchanged source hashes. Vivado also verifies the
new ascending/wide/dynamic fixtures (`vivado-sensitivity-extended/summary.json`).
Permanent range tests are registered for interpreter and LLVM O0/O2. Integrated
execution remains pending; syntax and isolated helpers do not qualify it.

Fresh frozen-control profiles of both full throughput cases passed saved
fixture/source hashes and canonical summaries/correctness transcripts on idle
nonzero CPU12. Evidence is
`build/performance-campaign/simulation-architecture/candidate-m2-validation-20260929/control-throughput-profile-comparison.json`.
The binary is the frozen `067d7fa5…734f1` control, with fresh fsim caches.
These are instrumented `cycles:u`/99 Hz/DWARF8192 attribution runs, **not Wall
qualification or paired timing evidence**. No PMU samples were reported lost.

| Diagnostic | Verilog throughput | Mixed throughput |
|---|---:|---:|
| Self samples | 5,323 | 2,402 |
| Unresolved samples | 103 (1.9%) | 517 (21.5%) |
| Reported internal setup | 590.782 ms | 495.597 ms |
| Reported internal run | 51,839.2 ms | 23,925.6 ms |
| Reported LLVM modules | 78 | 313 |
| Sum of module SimIR operations | 35,226 | 211,284 |
| Sum of raw LLVM instructions | 568,621 | 3,649,984 |
| Sum of optimized LLVM instructions | 391,550 | 1,393,424 |

The historical disjoint profiler classifier assigns Verilog samples to masked
frontier (15.2%), Logic4/driver values (14.4%), generated JIT (10.5%), fused host
callbacks (9.0%), other interpreter/lookup (8.3%), publication/fanout (7.6%),
and scheduler/update (7.2%). Mixed has 417 named LLVM samples and 510 unresolved
samples attributed to LLVM, plus 313 generated-JIT and 254 allocator samples.
The large unresolved mixed share limits detailed attribution. Reported setup
excludes background native work performed during run; these phase values must
not be interpreted as total elaboration/native-compilation costs. Module sums
are not unique machine instruction counts. Use this evidence to prioritize
A1/A2/A3/A4 and shared compilation; estimates remain hypotheses.

### Proposal disposition

“Adopt” means authorized implementation, not implemented or qualified.

| IDs | Disposition |
|---|---|
| A1 | Adopt static element-level nets with preserved logical array names, dimensions, hierarchy and external handles; conservative ranges for dynamic/unknown access |
| A2 | Adopt certified pure acyclic Verilog cones under the corrected default; exclude delta-offset carriers and the opt-in legacy/zero-delay split; cycle-exact VHDL regions |
| A3, S1–S3 | Adopt one RegionGraph, route consolidation and region-owned state; prove scheduling domains and observation capabilities first |
| S0 | Adopt cleanup, sparse fork invalidation, counters gated off by default, removal of hot getenv, allocation-safe staging and ordered insertion; retire APIs only after coverage review |
| S4–S5 | Adopt immediate exact materialization and certified Verilog collapse; replace delayed observation and late quiet-point-only demotion |
| A4, P0–P3 | Adopt value-plane, driver-class, fanout and ready-mask program; preserve fallback for resolution, force and unsupported effects |
| P4, JN-5 | Adopt guarded known-value fast paths with exact Logic4/Logic9 fallback; broader two-state-shadow research remains deferred |
| A5 | Adopt safe memory quick wins, shared programs/state, one lowering per specialization/statement/generate-relative occurrence, template-instance artifacts and exact initialization events |
| A6 | Adopt parity gates, phases, allocation counters, census and task/commit traces; use shared host and idle nonzero core without global CPU/desktop changes |
| A7, J-A | Adopt JP-1/JP-10 first, then lowering/attributes/pipeline improvements under differential validation |
| J-B | Adopt ABI v2, direct reads, persistent state, trusted entries, dynamic selects and capped selective tiering |
| J-C, JN-2–JN-3 | Adopt cone code and constant drivers after semantic/observation certification |
| J-D, JN-4 | Adopt native region frontier, cohort kernels, wide planes and guarded fast paths |
| J-E, JN-6 | Adopt retirement after replacement coverage and fallback tests; no premature deletion |
| JV-1, JV-4 | Adopt word-level reductions and exact minimized Logic4 formulas |
| JV-2 | Adopt storage-width loads before truncation; validate widths and allocation bounds |
| JV-3, JV-5 | Adopt bounded dynamic select/insert and word-addressed wide single-bit access |
| JV-6 | Adopt only after validating Logic9 code invariants across all producers and artifacts |
| JV-7, JV-8, JV-9, JV-11 | Adopt directional offsets, overflow intrinsics, runtime power and rotate simplification |
| JV-10, JP-4 | Adopt wide vector planes, sized by census after A1; scalar/wide fallback remains |
| JM-1, JP-7 | Adopt validated direct-read contracts for plane-capable contexts; retain SystemC callbacks |
| JM-2 | Adopt scratch-buffer/non-escaping wide callback operands with verified lifetime rules |
| JM-3 | Adopt nounwind only at a proven non-unwinding boundary; unsupported effects checked |
| JM-4, JM-7, JP-6 | Adopt cold descriptor loads, block placement and shared error exits |
| JM-5 | Adopt invariance only for immutable tables; never annotate host-rewritten fields |
| JM-6 | Adopt load reuse and unsigned map indices |
| JM-8 | Deferred/rejected alias-scope/TBAA/noalias work; never mark frame noalias |
| JP-1 | Adopt LLVM argument passthrough/cache identity and per-module census; serialize process-global option parsing if used |
| JP-2, JP-3 | Adopt verification policy, thread-safe TargetMachine reuse and explicit tuning; retain diagnostic verification |
| JP-5 | Adopt evidence-backed FastISel lowering, storage loads and Win64 entry handling |
| JP-8 | Adopt certified non-failing cohort kernels; fallback on failed guards |
| JP-8b | Defer cross-member SIMD |
| JP-9 | Adopt None/Less policy and 16,384 instruction cap stated above |
| JP-10 | Adopt unconditional concurrent compiler installation, including null-cache configuration |
| JN-1 | Replace v1 reuse sketch with public ABI v2 callbacks/instance-frame separation |
| Other explicitly deferred research | Exclude carry-less-multiply/GFNI recognition, instance-parallel SIMD, global backend escalation, extra speculative passes, absolute helper symbols, runtime PGO and calendar/timing-wheel work |

Historical R1–R9 are hypotheses/cost attribution; I1–I8 describe the old
implementation, not new invariants. Historical retain/reject claims must be
revisited when they determine a new retention or retirement decision.

---

### Current PMU access (2026-09-29)

The live environment identifies the CPU as AMD Ryzen AI Max+ 395, family 26,
model 112, in a Microsoft VM on kernel `7.0.0-1014-azure`. After the user's PMU
repair, `/sys/bus/event_source/devices/cpu` is present. `perf` 7.0.14 successfully
counts cycles, instructions, branches, branch misses, generic cache events,
L1 data-cache and dTLB events. Explicit LLC-load/load-miss events remain
unsupported. A bounded `perf record -e cycles:u -F 499 --call-graph dwarf`
probe captured 162 samples with stacks; evidence is in
`/home/colin/projects/fsim/.local-artifacts/simulation-performance/fsim-pmu-recheck.Zk7QxM/perf.data`. Prefer hardware counters and sampling;
reserve bounded Callgrind for costs unresolved by those diagnostics. No global
host settings were changed by the agent. The earlier no-PMU observation was
superseded by this successful access test.

A subsequent `-F 99` DWARF probe captured 87 samples with no lost samples;
the counter logs, raw `perf.data`, decoded stacks and report are retained in
`build/performance-campaign/simulation-architecture/pmu-recheck-20260929/`.
This establishes PMU usability; native fsim and JIT symbol attribution must
also be checked on fsim itself.

The fsim check subsequently passed on nonzero CPU5, selected by a one-second
`/proc/stat` probe reporting it idle. A bounded `cycles:u`/99 Hz/DWARF run
captured 335 samples with zero lost and two unresolved samples. Seven samples
resolved to named JIT functions, with a five-symbol perf map retained; native
fsim symbols also resolved. Profiled stdout matched an independent preflight
byte-for-byte. Evidence is
`build/performance-campaign/simulation-architecture/candidate-m1-validation-20260929/jit-pmu-smoke/fsim-cycles-core5-result.json`.
This confirms attribution works, not that seven JIT samples support a cost or
speedup inference. JIT unwind information remains limited. An earlier CPU0
smoke is diagnostic only and does not meet the nonzero-core campaign protocol.

### Diagnostic tracing contract

`FSIM_TRACE_SCHEDULER=1` enables `FSIM-SCHEDULER` records on stderr. It is read
once when a scheduler is created. Ordinary callbacks and compact descriptors
emit begin/end or failure records with simulation time, delta, phase, stable
order and sequence. Batches emit offered and consumed counts, followed by
task-end records for exactly the consumed prefix; untouched suffixes are traced
only when they execute. A declined batch reports zero consumption and then the
ordinary fallback. Canceled tasks have no execution records.

Signal transaction records include unchanged publications; signal-change
records describe effective changes. They carry signal IDs, not driver or value
copies, and do not constitute a complete value/driver history. The borrowed
noexcept `Scheduler::set_trace_hook` sink permits bounded in-memory tests.
It does not register an HDL observer or demote native execution. Hooks must
not mutate or reenter the scheduler. Disable tracing for all Wall measurements.
Trace tests pass within `fsim.runtime` and cover cancellation, stop/resume,
task failure, partial and declined batches, and unchanged publication. A CLI
smoke emitted 27 stderr records while preserving stdout exactly. Its eight
task-end records versus seven task-begin records correctly include one consumed
batch member. Evidence is in the candidate validation directory under
`wide-profile-smoke/scheduler-trace/`.

### Logic9 admission audit

The nine defined ordinal codes are 0–8. Public packed-plane constructors and
enum setters currently permit raw codes 9–15, although value reads normalize
them to X. Those raw planes can reach native direct reads. Consequently JV-6
must not treat the seven unused encodings as unreachable. The bounded audit
also found that native Logic9 unary mapping defaults unmatched codes to U and
native case equality compares raw planes, whereas the interpreter reads X.
Canonical ingress and checked native-boundary handling need differential
tests before any unused-code optimization. Valid Logic9 states must remain
unchanged; this audit is not evidence that the repair has landed.

## 1. Executive summary

| | Total Wall | Compile | Elaborate | Native setup + simulate | Peak RSS |
|---|---:|---:|---:|---:|---:|
| fsim V23, Verilog DUT | **57.08 s** | 0.62 | 6.49 | 49.97 | 1,011 MB |
| fsim V23, same TB with the VHDL DUT | 16.38 s | 1.24 | 3.08 | 12.06 | 314 MB |
| Vivado (2026-09-28 matrix, `-debug off`) | ≈16.9 s | 0.82 | 8.15 | 7.95 | 798 MB |
| Target | ≤15 s | | | | |
| Speed of light (§2.3) | — | | | ≈0.02–0.2 s | |

**Root cause.** The Verilog-vs-VHDL gap, and most of the gap to Vivado, comes
from the event model: **every Verilog continuous assignment is its own
scheduled process.** Its result is committed through the same update phase as
a nonblocking assignment, and its readers wake one delta later.

`gf_mult` is written as a netlist of about 151 continuous assigns per instance,
and there are 540 instances. So:

- 95% of the design's 85,397 processes are gf_mult internals.
- One multiply takes **11 deltas**, ≈65 activations, ≈10 masked native calls and ≈8 private commits.
- fsim spends **≈33 µs per useful multiply** (1.52M useful multiplies in the whole run; ideal is 2–20 ns).

The VHDL version computes the same function in local variables of one process
and runs 3.5× faster on the same testbench.

**The unpacked wire array multiplies the waste.** `wire [14:0] red [0:7]` is
elaborated as *one* 120-bit resolved signal. Static sensitivity has no bit
range [verified: `Sensitivity{signal, edge}` at `include/fsim/runtime/simir.hpp:1303-1308`],
so every write to one element wakes all 7 reduction stages. That accounts for
83M of the 96.6M masked activations. The same pattern appears in the
Chien/conv XOR cascades.

**Why V16–V23 plateaued.** The fusion work batches processes that are ready
*in the same delta* (horizontal fusion). It never collapses work *across*
deltas (vertical fusion), and by the campaign's own invariant ("preserve every
activation and logical delta") it cannot. The counters show this: V20→V23 kept
15.07M masked calls, 96.6M represented activations and 11.76M private commits.
Along the way the fusion added ≈11.8k production lines in 8 overlapping
execution routes, some of them hard-coded to this benchmark's shapes.

**The JIT's IR has not been optimized yet.** The campaign added kernel
wrappers: V19/V20 concatenate member bodies, and the V10–V16 pure-wave kernels
are hand-built. Everything else was left as it was: value lowering,
signal-read lowering, function attributes, the IR pass pipeline and the backend
level. So every JIT proposal in §6 is new work, organized into five batches
(J-A to J-E). Two findings stand out:

- The fused kernels spend 28% of their instructions on register spills. Straight-line reads remove all of them.
- On their own, the JIT batches are worth ≈3–6 s on the Verilog case today (est.; the items overlap). They are also most of what the VHDL DUT needs to get under 15 s.

**"Regression."** There are two distinct issues:

1. **The structural gap** (57 s vs 16 s vs Vivado) is architectural.
2. **The apparent step regressions** (V8, V12, V17, V18, V21) cannot be separated from measurement noise. The same V20 binary measured 55.6 s once and 71.2 s in a later controlled pair; V21's +14 s "regression" vanished in a paired run. Retain/reject decisions have been made on single cold samples whose noise is larger than the effects. **Fixing measurement is a prerequisite** (§5.6).

**What reaching 15 s requires** (est.; ranges overlap and are not additive):

| Step | Engine change | JIT batch (§6) | Semantics | Est. total Wall after |
|---|---|---|---|---|
| 0 | Measurement protocol; value P0; memory quick wins; fusion cleanup | **J-A** lowering and intrinsics; **J-B** direct reads, trusted entry, tier-up | none | ≈45–52 s |
| 1 | **Per-element nets for unpacked net arrays** + element-precise sensitivity (A1) | — | none (IEEE-exact) | ≈35–45 s |
| 2 | **Combinational cones**: vertical fusion of pure continuous-assign networks (A2) | **J-C** cone code generation, constant nets | corrected IEEE Active scheduling; cycle-exact VHDL | ≈19–24 s (native ≈12–16 s, elaboration unchanged) |
| 3 | **Template-level elaboration** + constant-assign folding (A5) | — | none | ≈14–19 s |
| 4 | **Compiled regions**, value/driver pipeline (A3/A4) | **J-D** native region frontier, cohort kernel, wide vector planes, dual 2/4-state kernels | none | ≈10–15 s |
| 5 | Retire subsumed fusion routes | **J-E** remove the standalone compiler static fuser after masked all-active semantic/cache replacement; pure-wave helpers are already absent | none | — |

The VHDL-DUT case (16.4 s) is already close to 15 s. Batches J-A and J-B plus
steps 3 and 4 should take it below.

**Adopted decisions** are recorded above and in §7. The old fsim-specific
Verilog net-hop delta contract is replaced by the corrected scheduling
reference, with immediate observation and full coverage detail.

---

## 2. Where the time goes

### 2.1 Profile categories (V23, exclusive sampled CPU; Verilog vs VHDL DUT)

| Category | Verilog | VHDL | Mainly caused by |
|---|---:|---:|---|
| Generated native JIT | 5.64 s | 2.48 s | per-assign bodies, spills, callback diamonds (§6) |
| Logic values / drivers | 5.58 | 0.61 | 9 value copies, P35 composites, wide copy-on-write (§5.4) |
| Masked region / frontier | 5.50 | 0.01 | per-activation keys/sort/heap for ~96.6M activations (§5.3) |
| Signal publication / fanout | 4.74 | 0.40 | 11.76M private commits plus boundary commits; per-process fanout |
| Fused cohort host control | 3.85 | 0.23 | per-call 888-byte runtime struct rebuild, checked resume (§6.2) |
| System libraries / allocator | 3.53 | 0.72 | wide copy-on-write (3 malloc + 3 free per change), `std::function` entries |
| Other interpreter / lookup | 3.42 | 0.91 | interpreted testbench loops, binary-search remaps, container unpacks |
| Scheduler / update staging | 3.28 | 0.73 | pair sort, per-slot staging, 96-byte entries |
| App executor / reads + native entry | 2.85 | 1.11 | read bounds checks, ABI checks per call (§6.2) |
| Other (mapped host, ordinary execution, LLVM, container, queue, bridge, prepared wave) | 7.2 | 3.6 | |
| **Profiled user CPU** | **46.14 s** | **10.77 s** | |

Categories that exist only because intermediate nets are scheduled signals
total **≈26.0 s vs 2.6 s**.

### 2.2 Counters (V1–V23)

| Quantity | Value | Meaning |
|---|---:|---|
| Useful gf_mult evaluations | **1,516,205** | input changes. Measured: 97,037,120 fused AND members ÷ 64. Analytic: ≈1.62M |
| V1 native process resumes | 236.55M | ≈137 gf_mult-internal activations per useful evaluation |
| V23 masked calls / represented activations | 15.07M / 96.6M | ≈10 calls and ≈64 activations per evaluation |
| of which `red`-stage activations | 83.15M | whole-array wakes; P25: 87% ran with unchanged inputs |
| V23 private local commits | 11.76M | ≈7.8 per evaluation (`terms` nets) |
| V23 terminal (`product`) activations | 11.88M | ≈7.8 per evaluation (glitches through ~10 deltas) |
| Update slots (P31/P32) | 218M, 76–79% unchanged | |

### 2.3 Speed of light (workload reviewer; arithmetic in the scratch `count.py`)

**Workload shape.**
- About 45,000 clock cycles (10 ns period; the run ends ≈450 µs).
- 6 decoders, 93,589 active DUT-cycles in total.
- 540 gf_mult instances: syndrome 192 (constant b), Chien/Forney 216, KES 132; 396 of the 540 have constant b.
- Average multiplier activity is 6.2% of multiplier-cycles, i.e. about 34 useful evaluations per clock.

**Minimum work.** ≈60–200M instructions:
- gf_mult: 1.52M evaluations × 5–60 instructions;
- rest of the design: ≈28–56M;
- testbench: ≈14–41M.

That is **0.02–0.2 s**. fsim spends 1.11 ms per cycle (Verilog) and 268 µs (VHDL
DUT); Vivado spends 176 µs. Vivado's short cases imply roughly 4 s of fixed
simulator start-up, so its marginal cost here is about 3.9 s.

**Where V1's 236.55M activations went.**

| Source | Share |
|---|---|
| AND terms | 41% |
| `red` stages | 35% |
| `unreduced` | 5.7% |
| `product` | 5.0% |
| Chien/conv wire-array XOR cascades | ≈7% |
| Everything else | ≈5% |

Two floors for comparison:
- **Keep the per-assign model but make sensitivity element-precise:** about 137M activations would still be needed (58%).
- **Treat each gf_mult as one function:** about 13.5M would be needed (5.7%).

**About 94% of activations and ≈99% of update commits come from modeling
intermediate nets as scheduled signals.**

---

## 3. Semantics: what fsim promises and what IEEE requires

**fsim's documented model** [verified: `docs/cross-language-semantics.md:159-177`]:

- Active processes run in stable ID order.
- `#0` events run in the inactive phase.
- One update phase commits VHDL transactions, SystemC updates *and* SV NBA writes.
- Changed nets enqueue their readers for the **next delta**.
- The doc says explicitly that this is "fsim's observable behavior, not a claim about an IEEE…" standard.

`docs/design-specific-jit-scheduling.md:152-153,168-170`:

- "A dependency edge alone does not permit same-delta publication or removal of a scheduling boundary."
- Observation restrictions need explicit contracts.

Tests compare interpreter and LLVM runs keyed by (time, delta) (for example `tests/app/application_test_fused_static.cpp:138-142`). The campaign also imposed "preserve every activation and logical delta" on itself (`docs/performance-resume.md:431-432`).

**Lowering evidence.**

- A continuous assignment is lowered with `signal_assignment=true`, which selects `WriteUpdate`/`WriteUpdateSlice`, the same op as an NBA (`src/elaboration/lowerer_hir_statement.cpp:5685,9362-9365`).
- Each concurrent statement becomes one process with wildcard any-edge sensitivity (`lowerer_hir_process.cpp:1474-1515,2441-2507`).
- Expression port actuals such as `.a(S[gi])` become extra adapter processes, each adding one delta (`hierarchy_sv_ports.cpp:956-970`).

**IEEE.**

- **IEEE 1800-2023:**
  - A continuous assignment produces an *active* update event (§4.9.1); ports are implicit continuous assignments (§4.9.6).
  - Active events may be processed in any order (§4.7).
  - NBAs move to Active only after Active and Inactive are empty (§4.4–4.5).
  - `$strobe`/`$monitor` run once per time slot (§4.4.2.9, §21.2.3).
  - There is **no normative delta per net hop.** A topological evaluation of a continuous-assign chain within one active pass is a legal schedule.
- **IEEE 1076 §14.7.5 (VHDL)** *does* mandate a new simulation cycle per zero-delay signal update. VHDL signal chains must not be collapsed to zero delay by default.

**Existing divergences from IEEE** (the event-model reviewer found these by
reading source; each needs a witness test before it is relied on):

1. Net updates commit in the NBA update phase. A clock derived through an `assign` can therefore sample post-NBA data where IEEE simulators sample pre-NBA data.
2. After `a=1; #0 $display(b)` with `assign b=a`, fsim prints the old `b`; IEEE prints the new one.
3. `$strobe`/`$monitor` run in the postponed phase of *every delta* (`simir_state.cpp:1701-1735`), not once per time slot.

These mean fsim's delta model is already a policy, not IEEE fidelity. Any
change to it must be an explicit, documented, oracle-backed decision.

---

## 4. Root causes (ranked by attributable cost)

| # | Root cause | Evidence | Est. attributable (native phase) |
|---|---|---|---|
| R1 | **Per-assign processes with NBA-phase commits.** Every combinational hop costs a delta, a commit, a publication and a wake. | §3 lowering; 11-delta gf_mult | ≈25–38 s (central ≈30 s) |
| R2 | **Unpacked net arrays flattened into one resolved signal**, plus whole-signal sensitivity (O(n²) wakes). Also forces wide composites, copy-on-write and container unpacking. | `hierarchy_sv_declarations.cpp:533-580`; `simir.hpp:1303`; 83.15M of 96.6M masked activations | part of R1 (≈5–12 s) |
| R3 | **Horizontal-only fusion with per-member host bookkeeping.** `ProcessState` stays authoritative, fallback stays possible at every activation, and order keys, per-call revalidation and exception-prefix rules all apply. | invariants I1–I8 in §5.3 | ≈9–12 s |
| R4 | **Value and driver representation:** up to 9 copies of each value, ≈25 per-signal arrays touched per change (25–35 cache lines), runtime P35 composites, wide copy-on-write allocation. | §5.4 | ≈6–10 s |
| R5 | **Unoptimized JIT code and host↔JIT boundary:** per-bit reduction loops, 10-op 4-state forms, a callback diamond compiled into every signal read (even where executors trap the callback), no function attributes, and `CodeGenOptLevel::None` everywhere, so 28% of fused-kernel instructions are spills. Each fused call also rebuilds an 888-byte runtime struct. | §6 | ≈3–6 s |
| R6 | **Memory:** two full copies of all 85k `Process` objects, 2.7 KB executors, and hot records scattered over ≈47.5k pages. | §5.5 | TLB/cache pressure (unquantified); RSS −300–500 MB possible |
| R7 | **Elaboration:** one lowering per process *instance* (83,929 calls), with sharing re-derived 3–5 times across elaborate, artifact and setup. | §5.5 | 3–4.5 s of the 6.49 s elaboration |
| R8 | **Measurement noise** (bimodal ±20–25%) driving decisions. | §5.6 | not a runtime cost; it misdirects the campaign |
| R9 | **Observability cliff:** any waveform, VPI, debugger or API observer makes every fusion builder return early, globally and permanently. | [verified] `simir_fused_static_graph.cpp:103-106`; `simir_fused_masked_graph.cpp:227`; `simir_private_bridge.cpp:44` | traced/debug runs lose all V19–V23 gains |

---

## 5. Target architecture

Track A1 comes first: it is IEEE-exact and needs no semantic decision. A2 is
the large lever. A3–A5 make the remaining generic engine lean. A6 is the
measurement and validation infrastructure all of them depend on. A7, the JIT
plan in §6, runs alongside all of them: batches J-A and J-B need nothing else,
J-C is A2's code generator, and J-D is A3/A4's.

### 5.1 A1 — Nets as IEEE defines them: per-element nets and element-precise sensitivity

- **Change:** each element of an unpacked array of nets is a separate net (IEEE 1800 §7.4.x arrays of nets). When every access to such an array uses a constant index, elaborate `wire [14:0] red [0:7]` as 8 single-driver 15-bit nets instead of one 120-bit resolved bridge signal with a container alias and 7 drivers (`src/elaboration/hierarchy_sv_declarations.cpp:533-580`). For arrays with variable-index reads, keep one signal but record element or bit ranges in `Sensitivity`, so publication only wakes readers whose range changed.
- **Removes, for the benchmark:**
  - ≈72.7M redundant `red` stage wakes;
  - 540 wide owned composites and their copy-on-write allocations;
  - container re-unpacking on every stage write (`simir_containers.cpp:10-27`);
  - the O(n²) Chien/conv cascade wakes (≈17M).
- **JIT side effect:** the 120-bit `red` signal goes away, so its `trunc i120` and other wide-integer paths (FastISel misses, JP-5) disappear from these kernels.
- **Relation to P26.** P26 was different:
  - It was an opt-in *runtime filter* that still woke each reader, then skipped it.
  - It covered 21 of 60 `gen_reduce` processes in the reduced case.
  - It saved 1.6 s of 26.7 s (6%) and was deferred for missing a 2 s threshold on one sample, within the noise documented in §5.6.
  - Per-element nets avoid the wake entirely, and they need no opt-in.
- **Est. gain:** native −5 to −12 s (from V23 masked/publication/values categories and P26's partial result). **Risk:** low–medium (hierarchical references, VPI iteration over arrays, dumping of array elements). **Effort:** 1–2 weeks.

### 5.2 A2 — Combinational cones: vertical fusion

**Definition.** A cone is a maximal acyclic set of pure processes:

- continuous assigns, gate primitives, port adapters;
- `always_comb`/`always @*` blocks that write every output bit on every path and never read their own outputs.

Its *internal* nets must have every writer and reader inside the cone, and must
carry none of the following:

- dynamic waits;
- `$monitor` watches;
- assertion sampling;
- container aliases;
- hierarchical or SDF references;
- force/deposit;
- VPI handles, trace selection or coverage points.

**Compilation.** A SimIR→SimIR rewrite, so the interpreter and LLVM stay
equivalent by construction:

- member bodies are concatenated in topological order;
- internal `ReadSignal`/`WriteUpdate` become register copies;
- the result is one process sensitive to the boundary inputs, publishing only boundary outputs;
- the original processes are kept as a shadow fallback;
- constant-driven inputs (for example `.b(ALPHA_I)`) are folded.

The native code for a cone comes from JIT batch J-C (§6.4): a 2-state fast
path with a word-level 4-state fallback, and constant nets as immediates.

**Adopted execution contract.** There is one IEEE-aligned Verilog default.
A cone is legal only when its graph and scheduling-domain certificate proves
that topological evaluation preserves observable events and a legal Active
order. Unsettled values, edge-sensitive consumers, delays, feedback, partial
writes and unsupported effects retain ordinary execution unless separately
proved safe. Internal traffic becomes SimIR registers; boundary writes retain
the original driver identity and scheduling provenance. VHDL uses cycle-exact
compiled regions, never delayed-publication cone carriers.

**Observation.** Before hidden state is enabled, implement immediate exact
materialization and selective demotion. Pull reads cannot simply recompute
from boundary inputs while transitions are in flight. Return the current and
last values, pending updates, original drivers and event/transaction metadata
for the exact suspended state. Observation or mutation must complete this
materialization before returning to its caller. Detailed coverage is preserved
by instrumentation or demotion. Re-promotion occurs only at a quiet point
after re-certification; that restriction does not defer observation.

**Est. gain.**

- Anchor: the VHDL-DUT run has the same testbench and function-local arithmetic.
- The cone-attributable share of native time is ≈25–38 s.
- The original estimate was Verilog native ≈12–16 s; its carrier-based variant is excluded and the estimate needs fresh evidence.
- Total ≈19–24 s *with today's elaboration*.

**Effort and risk.** 4–8 weeks across extractor, compiler, scheduler API and
observability. Semantic risk is medium to high: a missed reader, demotion while
carriers are in flight, compile-time growth from per-instance specialization.

**Cheapest ceiling test (no fsim change).** In a scratch copy, rewrite
`gf_mult.v` as `always @*` plus a function mirroring `gf_mult.vhd`, run the
Verilog case, and measure a diagnostic ceiling only. Production RTL stays unchanged, and
this experiment cannot qualify the engine.

### 5.3 A3 — Compiled regions: one region abstraction instead of eight routes

**Current execution routes** (fusion reviewer):

```
batch task → checked scheduler execution
  ├ runtime static cohort → FusedMaskedProcess all-active entry → checked fallback
  ├ native region/frontier candidate → certified V2 entry → checked fallback
  └ singleton execute
```

The V22 runtime masked-region planner and dispatch, global reserved-key
frontier/barrier, and pending-touch marker are retired. The source-compatible
runtime masked-region facade returns no candidates and zero counters. Native V2
still uses the generic `Scheduler::current_batch_frontier`; that scheduler
frontier is separate from the retired V22 queue.

**Invariants that force per-member, per-delta work:**

| # | Invariant |
|---|---|
| I1 | Each assign commits at delta end |
| I2 | Whole-signal sensitivity |
| I3 | Original `ProcessState` is authoritative, and fallback is possible at every activation |
| I4 | Full-key task order, because regions don't prove their inputs are free of blocking writes (≈57 ns per activation) |
| I5 | Every aggregate output is published at the boundary (`simir_fused_masked_graph.cpp:374-377`) |
| I6 | Original-owner driver identity |
| I7 | Exact exception-prefix retirement |
| I8 | Every call is revalidated |

**End state: one compiled region (CR) abstraction.**

- **Formation.** A connected component of pure static processes, formed by one `RegionGraph` census. It replaces 3 census passes, about 11 duplicated eligibility predicates and a 10-condition signal predicate copied more than 6 times.
- **State.** Kernels are shared by canonical shape (`canonicalize_masked_process`). Each instance has one contiguous, generated-code-owned block: internal current/pending planes, member-ready bits, changed-slice bits and boundary output slots.
- **Execution.** `cr_step(block, globals, out_slots) → {boundary_changed_mask, rearm}`. Generated code runs the ready members, commits internal nets and computes the next ready set from compiled element-precise masks (JIT item JN-4, §6.4). The host sees one scheduler entry per active region per delta.
- **Invariants handled:**
  - Delta-exact mode maps internal deltas 1:1 to scheduler deltas.
  - An input-immutability certificate makes a region's position within a delta unobservable, which deletes V22's reserved keys, heap and barrier.
  - Pure bodies cannot fail, and staging capacity is reserved at bind time, so staging is `noexcept`.
  - Demotion is per region and reversible; re-certification replaces permanent global invalidation.
- **Code and retirement.** The V7/V9 AND bindings and V10–V16 pure-wave shape kernels are already absent from live source, public headers, and tests; the old `llvm_jit_execution.cpp` line citation was generic ABI validation, not a shape matcher. The test-only `FusedStaticProcess`/`fuse_static_processes` compiler API was removed and its generated O0/O2 semantic and cache witnesses moved to the `FusedMaskedProcess` all-active entry. That compiler path remains live for runtime static cohorts, with checked fallback. The V22 runtime masked-region planner/dispatch, global queue/barrier, and pending-touch marker are retired; its source-compatible facade returns no candidates and zero counters. The combined V22 replacement/fallback controls passed 18/18. The unused `normalize_fused_container_reads` helper is deleted; affected container behavior passed nine focused gates and source/package contracts passed 2/2.
- **Est. (delta-exact, without cones):** ≈25–28 s of profiled CPU removed, total ≈27 s. With cones, CR keeps the value it adds for clocked cohorts, observed or feedback cones, and multi-writer aggregates.

**Staged plan:**

- **S0 — cleanup.**
  - Remove test-only APIs.
  - Put counters behind a build flag.
  - Remove `getenv` from fork and hot paths.
  - Pre-reserve staging (fixes the allocation-safety bug in §8).
  - Make fork invalidation sparse.
  - Insert into ready lists instead of sorting.
- **S1 — one `RegionGraph`** with re-certification and per-region demotion.
- **S2 — merge execution routes.** Static cohorts compile through the `FusedMaskedProcess` all-active entry; the standalone compiler fuser is removed. V7/V9 and V10–V16 helper bindings are absent. The V22 runtime masked-region route has also been retired after its replacement and fallback gates passed; native V2 retains the generic scheduler frontier.
- **S3 — region-owned state and native frontier.** The private bridge and V22 global frontier are retired; native V2 continues to use the generic scheduler frontier. Region-owned state coverage continues through A4 replacement gates.
- **S4 — observability** via immediate exact materialization, selective demotion and detailed instrumentation.
- **S5 — collapsed mode** with A2.

### 5.4 A4 — Signal state, drivers, publication and the scheduler queue

**Today** (value/driver reviewer):

- One changed value can exist in up to 9 places: the P35 composite's committed and phase copies, the commit pair, `driven_values`, `SignalHot.initial_value`, `signal_last_values`, the ≤64-bit mirror, the wide mirror, and stale `DriverRecord`s.
- A commit compares the value three times.
- A changed net touches about 25 per-signal arrays, roughly 10 MB in total (`simir_internal.hpp:807-1001`).
- An 8-bit `terms` commit costs about 700–1,000 instructions and 25–35 cache lines.
- A 120-bit `red` commit adds 3 malloc + 3 free and 8–10 locked reference-count operations through wide copy-on-write [verified: `packed_value.cpp:372-385`].
- Owned-driver composites are sized for every signal [verified: `simir_owned_driver.cpp:19`].
- A wake visits 64 `ProcessState` records to queue one cohort, which then rescans all 64 members.
- Queue entries are 96 bytes and carry `std::function` fallbacks.

**Proposals:**

- **P0 — no semantic change, ≤1 week, est. 2–4 s:**
  - pending audit items T4–T8, L2, L4–L6;
  - `vector<bool>` → bytes;
  - cached per-signal route and publish flags with an epoch, replacing the 20–30-predicate chains;
  - a writer-touch mask instead of composite markers;
  - `std::function`-free batch fallback and frontier;
  - O(1) cohort readiness (P36: every ready span was full);
  - bucket pooling;
  - ≤128-bit Logic4 stored inline in `PackedLogic4`'s 32-byte payload (no copy-on-write).
- **P1 — planes become authoritative, 1–2 weeks, est. +2–4 s:**
  - `initial_value`, `driven_values`, `signal_last_values` and driver values materialize lazily, generalizing `direct_signal_materialization_pending`;
  - one mirror, one compare;
  - a dirty-bitset commit that iterates in SignalId order, so no pair sort (unlike rejected P21, no persistent index).
- **P2 — driver classes decided at elaboration, 2–3 weeks, est. +2–3 s:**
  - **S** (6,646 single-writer nets): fixed direct route.
  - **B** (9,258 bit-owned disjoint nets): a static own-mask per writer and native-word publish; no composite, phase, markers or sort.
  - **R** (resolved, forced or strength-sensitive nets): keep the `DriverTable`.

  This removes P35's runtime composite; P33 by contrast *added* a cache.
- **P3 — grouped fanout and region ready masks, ~2 weeks, est. +1–2 s:**
  - fanout records per (signal, consumer group) with a member mask;
  - an infallible `noexcept` wake, which removes the V16 exception-prefix objection;
  - ready masks *inside* certified regions, ordered by stored member keys. This is distinct from the rejected global process-ID bitmap.
- **P4 — research:** a 2-state shadow with a per-signal `has_unknown` bit. Its JIT half is JN-5 in §6.4: dual kernels with a 4-state fallback.
- **Not recommended:** a calendar queue or timing wheel. It was excluded, and the physical queue is only 0.76 s of this workload.

These overlap with A1/A2: after cones, the lean pipeline is worth ≈1–2 s on
this workload, but it still matters for observed or non-collapsible nets and
for other designs.

### 5.5 A5 — Memory, elaboration and setup

**Memory (sim phase ≈0.7–0.95 GB est.).** Quick wins:

- Hand the elaborated design to the interpreter instead of copying it. Setup calls `built.design.create_interpreter(...)` on an lvalue, which copies all 85k `Process` objects [verified: `application_simulation_impl_setup.cpp:97`; `elaborated_design.cpp:386-392`]. Saves −90–110 MB.
- Make `fused_masked_normalized_programs` sparse; it is sized to every process (`simir_fused_masked_normalization.cpp:13`). Saves −50 MB.
- Drop the up-front `process_names` copy (`application_run_commands.cpp:243-247`). Saves −10–15 MB.

Deeper changes:

- **Executor diet:** today a 2.7 KB `LlvmProcessExecutor` per recurring process embeds an 888-byte callback table. Share the table per JIT; this is the same change as JN-1's persistent runtime struct (§6.4). Saves −120–150 MB; needs an ABI/schema bump.
- **Split `ProcessColdState`:** it embeds a full ≈580-byte `Process`. Keep programs per template and make dynamic state lazy. Saves −100–130 MB.
- **Contiguous hot-record slab plus arenas with `MADV_HUGEPAGE`.** Today 85k 72-byte records are spread over ≈12k deque blocks, the source of V15's 47,545 pages; a 5.4 MB slab would fit in three 2 MiB huge pages.

**Allocations on the simulation path:**

- wide copy-on-write;
- `FusedMaskedPendingTouch` vectors;
- `std::function` entries, including a 32-byte lambda above the small-buffer limit;
- an 85 KB vector per demotion;
- 3 `std::set`s per callable snapshot.

Add **zero-allocation gates only for defined fixed-topology steady-state
fixtures**, not arbitrary HDL programs. Allocation instrumentation is separate
from Wall runs. Do not change global allocator/host settings for qualification.

**Residual interpreter work.** These keep processes interpreted:

- the `always #5 clk=~clk` generator and each testbench `@(posedge clk); #1` loop (`WaitFor`/`WaitOn` are not "recurring"), which run interpreted every cycle;
- single-instance glue processes (templates with ops × instances under 32);
- RAM writes (`WriteContainerObjectElement` is shareable only in the large non-recurring list);
- the adaptive gate.

All of these are in `application_simulation_impl_setup_execution.cpp:1626-1810`.
Verify with `FSIM_PROFILE_JIT=1` (`retained_processes`). Fixes:

- classify `forever @(edge)` loops as recurring;
- admit statically sensitive processes into packed modules;
- bind per-instance operands densely.

**Elaboration (6.49 s; the budget needs compile + elaborate + setup ≲ 4 s).**

The P18 Callgrind profile shows:

| Item | Share / calls |
|---|---|
| `lower_hir_process_body` | 43.5% inclusive, 83,929 calls, i.e. **one lowering per process instance** |
| `parse_integral_identity` | 5.3%, 7.36M calls |
| `find_declaration` | 3.9% |
| `resolve_systemverilog` | 3.6% |
| path comparison | 3.4% |

Sharing is derived 3–5 times:

1. elaboration's `canonicalize_process_operations`;
2. artifact encoding of expanded operation lists, with constants as MSB text;
3. decode followed by re-sharing;
4. setup's `ProcessSharingKey` map and `signal_remap` expansion with codec byte comparison;
5. fused-masked normalization.

Proposals:

- **Lower once per (specialization, statement, generate-relative occurrence)** and instantiate by binding vector. For gf_mult that is ≈151 lowerings instead of ≈81,540.
- **Template+instance artifact** decoded straight into shared form; setup reuses `shares_body_with`/`signal_remap`.
- **Fold the ≈34k constant continuous assigns** into a time-0 constant-driver bank, with the same time-0 update. That is −40% of process records.
- **Memoize literal identities.**
- **Longer term: lazy materialization.** Never build 85k `Process` objects.
- **Setup phase timestamps.** Setup is not separable in receipts today: `setup_ms` covers only `Simulation` construction.

### 5.6 A6 — Measurement and oracle infrastructure (prerequisite)

**Noise.** Single cold samples on a mobile APU (Ryzen AI Max+ 395) pinned to CPU 0 are bimodal:

- V20 measured 55.6 s once and 71.2 s later on the same binary; V21/V22 were ≈71–72 s; V23 was 57.1 s.
- P88 measured 2.16 s same-binary drift.

Plausible causes:

- shared CPU/iGPU power limits with long time constants;
- CPU 0 housekeeping and IRQs, with an SMT sibling possibly running ThinLinc;
- THP and fragmentation state;
- ASLR and JIT code placement;
- JIT workers sharing the pinned CPU, which changes how many activations run interpreted.

**Decisions affected** (memory/measurement reviewer):

| Decisions | Evidence | Verdict |
|---|---|---|
| V5, V8, V12, V17, V18 rejected or flagged | 1 sample each, +2–13% | not distinguishable from noise |
| V20 retained | its 55.6 s was the outlier | retention unsupported |
| V21 "regression" | disproven by a pair | — |
| V23 | 57.1 s may be the "fast mode" | not established |
| P33, P38 | pairs with opposite signs | inconclusive (correctly treated) |
| P84/P86/P87 | 1 sample each | within P88's drift |
| V2, V9, V14, V16, V19 | 1 sample each | credible only where counters show the mechanism |

The 170→57 s trajectory is real in aggregate; individual attributions are not established.

**Adopted protocol:**

1. Rebuild/freeze an uncontaminated control from `ede7c24e`; never reuse the
   reverted experiment's working build. Archive source, binary, dependencies,
   configuration, command, host state and cache identities.
2. Use the existing shared host, one idle nonzero core and exclusive executable
   work. Record load, affinity/SMT conditions, frequency and THP observations.
   Do not change global CPU, IRQ, desktop or memory settings.
3. Require canonical output, stimulus, final-fingerprint and correctness parity
   before accepting a timing comparison. External production RTL is unchanged.
   IEEE corrections require oracle-backed expected-output updates first.
4. Use fresh fsim caches and a warm OS page cache. Keep instrumented profiling
   separate from uninstrumented Wall. Use deterministic runtime counters and
   bounded Callgrind where PMU counters are unavailable; counts alone are not
   time savings. Report unresolved attribution.
5. Wall decisions require at least five randomized control/candidate pairs.
   Final qualification requires seven paired observations per full throughput
   case. Report medians, IQRs, paired bootstrap confidence intervals, phase
   times and RSS. Inconclusive evidence remains inconclusive.
6. Benchmark only Verilog and mixed throughput; smaller fixtures establish
   correctness and diagnostic screening. The old mixed_codec/all-ten campaign
   instructions below are historical, not additional benchmark scope.
7. Add precise setup/native/materialization/simulation timestamps, allocation
   counters, a JIT census and task/commit traces. Compare scheduling traces
   against the corrected interpreter, retaining language-domain provenance.
8. Preserve structural work when a first screen regresses. Revisit historical
   claims when they affect retirement/retention; do not infer causal effects
   from the old single observations.

**Correctness oracle:** interpreter, LLVM O0 and LLVM O2 must agree on the
corrected scheduling contract, including initialization, overlapping events,
NBA/derived clocks, VHDL cycles, SystemC/mixed boundaries, feedback/delta limits,
stop/resume, force/deposit, late observers, coverage and allocation failure.
Use focused external oracles for scheduling corrections. Cover scalar and
65/129/256/1024-bit values, Logic4 unknowns, every valid Logic9 state, array
directions, dynamic indices, partial ownership, resolution and random acyclic
combinational graphs. Test ABI rejection, deterministic sharing/artifact round
trips, cache invalidation and Linux/Windows layouts. No end-of-step-only oracle
can excuse an intermediate observable-event difference.

---

## 6. A7 — JIT optimization plan

This section is the full JIT work list:

- **all IR proposals from `docs/simplification-audit.md` §12** (JV value lowering, JM memory/boundary, JP pipeline/backend);
- **new items from this review** (JN), which come from reading the fused kernels and the cone design.

None of the IR-level items has been attempted. They are sequenced into five
batches (§6.5) tied to the program in §9, and each batch carries one
native-cache schema bump (v178→v179…).

Measurement basis (LLVM 22.1.8):

- **"fsim backend"** = `opt -passes='function(sroa,early-cse,simplifycfg,instcombine<no-verify-fixpoint>,simplifycfg)'` + `llc -O0 -fast-isel`. This is the production path at every level [verified: `llvm_jit.cpp:572-592`; `llvm_jit_module.cpp:115-121`].
- **"tier-up"** = `llc -O2` on the same IR.
- Counts are machine instructions of hand-written models, not simulator timings.
- For formula rewrites, "exhaustively equal" means compared over every 4-state input of the modelled width.
- The cone models were compared against an independent per-bit 4-state reference of `gf_mult.v` on all 65,536 2-state input pairs and 4M random 4-state vectors, with 0 mismatches.

**Anchors.** Line numbers are at `ede7c24e`. These files are unchanged since
`6efa4c35`:

- `llvm_jit_value_lowering.cpp`
- `llvm_jit_operations_logic.cpp`
- `llvm_jit_operations_value.cpp`
- `llvm_jit_dynamic_part.cpp`
- `llvm_jit_operations_container.cpp`
- `llvm_jit_lowering_operations_prefix.cpp`
- `llvm_jit_operations_control.cpp`
- `llvm_jit_module.cpp`

### 6.1 Constraints that shape the plan

- **Backend.**
  - Every level, O2 included, uses `CodeGenOptLevel::None` + FastISel + RegAllocFast. The source records why O2 stays at `None`: "the optimized machine scheduler has near-quadratic dependency-graph behavior on large generated HDL processes" (`llvm_jit.cpp:583-589`).
  - The consequences:
    - no block placement and no machine cleanup;
    - every value live across blocks is spilled and reloaded;
    - `!prof`, `llvm.expect` and `cold` have no effect;
    - IR block order is the code layout.
  - Any tier-up therefore has to be **size-capped**.
- **IR pipeline.**
  - It is only `sroa, early-cse, simplifycfg, instcombine, simplifycfg`.
  - `early-cse` runs without MemorySSA, so any store or call invalidates every earlier load. Therefore `noalias`, TBAA and alias scopes do nothing; only `!invariant.load` and same-address forwarding survive.
  - `PassBuilder` gets no `TargetMachine`, so there is no target cost model and vectorizers do nothing.
- **FastISel coverage.**
  - It misses `ctpop`, `fshl`/`fshr`, `abs`, `min`/`max` and `iN>64`; each miss costs a one-instruction SelectionDAG block.
  - Nested i1 `select`s (InstCombine's logical-and form) send the rest of the block to SelectionDAG.
  - **On Win64**, X86 FastISel rejects argument lowering, so every generated function's entry block goes to SelectionDAG.
  - `trunc i120` (the flattened `red`) also misses.
- **Where the payoff is.**
  - Generated code is ≈6% of the `original_throughput` Callgrind interval and 5.64 s of V23 sampled CPU (Verilog).
  - LLVM compilation is 43% of mixed_codec native samples (P62).
  - So the gains are **fewer spills and less per-call host work** on the long runs, and **smaller IR and fewer blocks** (cheaper compile) on short, mixed-language runs.
- **Plain vectorization finds nothing.** Clang `-O2` with a full AVX-512 target model produced zero vector instructions for a single 10-op body and for a fused 64-member cohort.
- **Shared templates.** The 64 gf_mult AND members are 64 distinct templates, because the sharing remap compares extract offsets exactly. Each template is shared across instances, and all 64 read the same two signals.
- **Schema.** The codegen level is not in the native-cache key today. Tiering needs a tier field.

### 6.2 What today's generated code costs

**Four kernel families at `ede7c24e`:**

1. **V19 fused static:** 64 AND member bodies, concatenated in SimIR and lowered by the ordinary lowering (`llvm_jit_fused_static_process.cpp`).
2. **V20 masked:** the same, plus a per-member activation-word load, bit test and branch (`llvm_jit_masked_lowering.cpp:12-97`). The word is reloaded for every member, because EarlyCSE without MemorySSA cannot reuse it across stores.
3. **Pure-wave kernels:** four fixed shapes plus a dispatcher, hard-coded to gf_mult M=8 widths [verified: `llvm_jit_execution.cpp:236,293,443-510`].
   - Each member does a 3-level pointer chase (`:640-651`).
   - Shifts use variable amounts because offsets are loaded as data.
   - They never get the O1 loop pipeline (`process_count=0`).
   - They are never persisted, because their identifiers are not 64-hex cache keys (`object_cache.cpp:873-883`).
4. **Generic/logic4 cohort wrappers:** unrolled per member.

**Where the instructions go.**

- The masked kernel lowers every `^terms` reduction through the per-bit loop (`llvm_jit_operations_logic.cpp:167-240`).
- Every signal read carries the null/count/bounds chain and a callback diamond (`llvm_jit_operations_signal.cpp:1079-1330`). This includes the fused executors, which install callbacks that *throw* if called (`application_fused_static_executor.cpp:285-305`).
- No function in `src/compiler` sets an attribute.

**Per-member cost** (fsim backend vs tier-up):

| Model | fsim backend | llc -O2 |
|---|---:|---:|
| Pure-wave AND (unchanged / changed slot) | 105 / 129 | ≈51 / 62 |
| Pure-wave reducer31 (no spill / spill) | 166 / 227 | 128 |
| **Fused static AND group, HEAD (64 members)** | **7,745 hot (121/member)**; 8,762 static, **2,449 spill/reload (28%)**; IR 5,429 instrs, 513 blocks | 4,198 |
| + straight-line reads (JM-1: drop the dead callback diamonds) | **4,498 (70/member), 0 spills, 1 block**, IR 4,070 | 3,013 |
| + read dedup (SimIR hoist or `!invariant.load`) | 4,866 (**+1,439 spill/reload at O0**) | 2,607 |
| + JV-4 + extract CSE | 4,606 (72/member) | 2,301 (36/member) |

**Findings that set the order of the plan:**

- **(a) RegAllocFast spills dominate, and JM-1 removes them all:** −42% hot instructions.
- **(b) Read dedup hurts at O0 but helps at tier-up.** EarlyCSE *does* reuse `!invariant.load` loads (837→459 loads), but the longer live ranges add spills under RegAllocFast. Rule: rematerialize reads per member at `None`; deduplicate only in tiered-up kernels.
- **(c) Tier-up to `CodeGenOptLevel::Less` is the cheapest large code win for hot fused templates:** −46% instructions, spills 2,449→457. It costs about +70–100 ms of backend time per kernel, and the Verilog run has only 78 native objects.
- **(d) Per-call fused host boundary.** Both fused-static and fused-masked calls
  enter `LlvmFusedStaticExecutor::resume_impl`, which constructs the descriptor
  (`application_fused_static_executor.cpp:277`) and invokes checked `resume`
  (`:511`; checks in `llvm_jit_execution.cpp:4306` onward). Each fused call does:
  - rebuilds an 888-byte runtime struct;
  - re-bounds-checks every read;
  - zeroes slots;
  - runs the checked `resume`.

  This is where fused host control (3.85 s) and native entry (0.59 s) live: up to ≈230 ns per call over 16.6M calls.

  The ordinary `LlvmProcessExecutor` already keeps `runtime_` per instance and
  uses `resume_prevalidated`. It refreshes mutable descriptor fields but does
  not reconstruct the callback table or use all the checked-entry validations
  on each resume. ABI v2 can still share its per-instance callback storage;
  do not apply the fused reconstruction/validation estimate to ordinary calls.

### 6.3 Whole-cone code generation (the J-C core, with A2)

**Today's cost per useful gf_mult evaluation:** 1 static call (64 members) + ≈9.9 masked calls (≈64 members) + ≈7.8 product evaluations. That is ≈22–28G generated instructions over the run, consistent with the 5.64 s sampled generated-JIT CPU.

**Target function** (one template-shared function; all intermediate nets are SSA values):

```
cone_gf_mult(inst*):
  a,b = load planes(inst->a), planes(inst->b)          ; invariant port pointers
  if (((a.b | b.b) & 0xff) == 0) {                    ; exact 2-state fast path
     u = XOR_i ((b>>i)&1 ? a<<i : 0)                   ; terms + unreduced
     7x: u ^= ((u>>hi)&1) ? P<<sh : 0                  ; red cascade
     prod = {u & 0xff, 0}
  } else prod = word-level 4-state forms (JV-1/JV-4)
  if (prod != inst->prod_cur) { stage prod; set fanout bit }   ; boundary only
```

| Cone model | clang -O2 (dynamic) | fsim backend | tier-up |
|---|---:|---:|---:|
| Per-assign formulas as SSA | ≈975 | 1,888 (573 spill/reload) | 1,476 |
| Word-level 4-state (JV-1/JV-4) | ≈280 | ≈540 dynamic | 375 |
| **2-state fast path** | **52** | **127** | 100 |
| 2-state + `pclmulqdq` | 39 | 75 | 59 |

**Effect.**

- 1.52M × (127 + ~20 I/O) ≈ 0.22G instructions, against ≈25G today: ≈99% of gf_mult generated code, ≈5 s.
- More important, it deletes the activations, calls and commits that drive the host categories (≈23 s gap).
- The 4-state fallback is built from JV-1 and JV-4, so batch J-A is a prerequisite.
- Fast paths must fall back on X-generating ops even with known inputs: divide or modulo by zero, and out-of-range dynamic indexes.

### 6.4 The complete JIT work list

Each item gives its change and the measured effect on a model. The "After
cones" column says whether the item still pays off once A2 has collapsed
gf_mult:

- **core:** needed by cone code;
- **general:** helps all remaining processes and other designs;
- **reduced:** less value on this workload.

#### Value lowering and intrinsics (JV)

| ID | Change | Anchors | Measured (model) | After cones |
|---|---|---|---|---|
| JV-1 | Reductions (and/or/xor, one-hot) and `$countones`/`$countbits`: replace the per-bit extract/combine chain with word ops plus `llvm.ctpop`. For example xor: `u=(b&m)!=0; aval=(ctpop(a&m)&1) or u; bval=u`. Expand `ctpop` for FastISel (JP-5). | `llvm_jit_operations_logic.cpp:167-340` | xor-reduce 8-bit 83→17 instrs, 32-bit 325→18, 256-bit 3,106→22; CountOnes 32-bit 382→9. Exhaustively equal. Hits every `^terms` (15 per gf_mult) and the syndromes. | core (4-state fallback) |
| JV-4 | Minimal 4-state bitwise forms: and `t=(la or lb)&(ra or rb); aval=t; bval=t&(lb or rb)`; xor `u=lb or rb; aval=(la^ra) or u; bval=u`; or in 7 ops. Build conditions with `and i1`, not nested selects. | `llvm_jit_value_lowering.cpp:515-552` | 10/14/7 → 5/7/3 ops after InstCombine. Exhaustively equal. Affects every Binary and the gf_mult AND/XOR trees. | core |
| JV-2 | Load wide registers as their storage type (`i128` for i100), then `trunc`. Today an exact-width load of rounded-up storage blocks SROA and is formally undefined. | `llvm_jit_value_lowering.cpp:100,110`; `llvm_jit_lowering.cpp:1040-1045,382-393` | A 104-bit register stays an `alloca` today; the micro-case went 39→9. Prerequisite for JP-4. | general |
| JV-3 | Closed-form dynamic part-select/insert/write: one interval computation, a shifted valid mask, and `llvm.fshr.i64` for two-word wide sources. Keep the per-bit loop only for non-HDL directions. | `llvm_jit_operations_value.cpp:325-539,730-850`; `llvm_jit_dynamic_part.cpp:65-110` | select W=8 294→≈70; insert W=8 into i2048 11,942→1,489 (llc 479→153 ms). Risk: medium (direction, fill planes); needs a differential matrix. | general |
| JV-5 | Wide single-bit DynamicExtract/Insert through word GEP + load (as P43 does), instead of variable `iN` shifts. | `llvm_jit_operations_value.cpp:93,680-705` | i2040 extract 187→12. | general |
| JV-6 | Logic9 truth tables minimized, with states 9–15 as don't-cares (coerce to Logic4, `not`). | `llvm_jit_value_lowering.cpp:142-260` | 56→22 and 86→25. The required exclusion of codes 9–15 is currently false; see the Logic9 admission audit above. | general (VHDL) |
| JV-7/8/11 | Direction-specific index offset (removes the `llvm.abs` DAG fallback); `with.overflow.i32` for 32-bit integer arithmetic; rotate without a second `urem` (FastISel emits a hardware `div`). | `llvm_jit_operations_value.cpp:79-83,1071-1108`; `llvm_jit_operations_logic.cpp:392,411` | 41→38, 17→11. | general |
| JV-9 | Power as a runtime square-and-multiply loop (today W multiplies are unrolled). | `llvm_jit_value_lowering.cpp:614-630` | Compile time and code size. | general |

#### Memory, call boundary and attributes (JM)

| ID | Change | Anchors | Measured (model) | After cones |
|---|---|---|---|---|
| JM-1 / JP-7 | **Direct-reads-required contract**, mirroring the existing `require_direct_update_slots`. The host validates once; each read becomes straight-line `load map[slot]` (`!invariant.load`) + two plane loads. This deletes the per-read null/count/bounds chain and the callback diamond. Rematerialize per member at `None` (§6.2 finding b). Only for contexts that supply direct planes (not SystemC). | `llvm_jit_operations_signal.cpp:1079-1330`; `llvm_jit_lowering_operations_prefix.cpp:440-600`; host check `application_executors.cpp:757-770` | 2-read body: hot path 138→81 instrs, spills 39→8, blocks 12→4. Fused 64-member group: 7,745→4,498 hot, 0 spills, 513→1 blocks, IR −25%. | core (cone input reads) |
| JM-2 | Wide (>64-bit) read/write callbacks take a scratch buffer, or `readonly captures(none)` pointers, instead of pointers into register slots. The escaping address stops SROA, and in array mode it pins the whole register file. | `…_prefix.cpp:556-660`; `llvm_jit_operations_container.cpp:268-395` | An i128 register becomes SSA. | general |
| JM-3 | `nounwind` on generated functions and callback calls. The ABI already forbids unwinding (`jit_runtime.h`) and no function attributes are set [verified]. | function creation `llvm_jit_lowering.cpp:121-124`; `llvm_jit_execution.cpp:210-321` | Removes `.eh_frame`/`.pdata` and per-module EH registration (objects −13–15%). | general |
| JM-4 / JM-7 / JP-6 | Load cold-only descriptor fields inside their callback blocks (not all in the entry block); emit error, callback and `invalid.pc` blocks last; route the per-site "6 stores + ret" error exits through one shared exit. | entry loads `llvm_jit_lowering.cpp:131-144`, `invalid.pc` `:1827-1840`; `…_prefix.cpp:313-322`; `llvm_jit_operations_control.cpp:285-305` | −4 instrs per resume; taken jumps on the hot path 6→3; return-dense model −49% O0 codegen. | general |
| JM-5 | `!invariant.load` on executor-constant tables (read maps, wide offsets, update-slot fields 7–9, callback pointers reloaded per operation). Do **not** tag host-rewritten fields (context, flags, slot pointers, trigger mask). | `llvm_jit_operations_signal.cpp:376-381,1134-1139,1270-1276`; `…_container.cpp:60-76` | Small at `None`; enables read dedup at tier-up. | general |
| JM-6 | Reuse the loaded `slot.bval` in `begin_direct_update`; `zext` instead of `sext` for the map index. | `llvm_jit_operations_signal.cpp:189-218` | −4 instrs per read/write. | general |

#### Pipeline, backend and compile time (JP)

| ID | Change | Effect | After cones |
|---|---|---|---|
| JP-1 | `FSIM_LLVM_ARGS` passthrough (folded into the cache key) and a per-module census: FastISel fallbacks by reason, `iN>64` instructions by opcode, return and switch blocks. **First item of J-A.** | Sizes JP-4/JP-5 and JV-3/JV-5 before committing. | general |
| JP-2 / JP-3 | Post-optimization `verifyModule` only in Debug or behind an environment switch (`llvm_jit_process_modules.cpp:495`; ≈9–10% of stage CPU). One thread-local `TargetMachine` per opt level (today one per module). Explicit `PipelineTuningOptions` for the P52B loop pipeline. (`setDiscardValueNames` was screened separately as §11 F2 and gave no measurable benefit; it is not part of this plan.) | Compile CPU ≈0.3–0.8 s on mixed_codec; no schema bump. | general |
| JP-5 | Keep FastISel on its fast path: expand `ctpop`/`fshl`/`fshr`/`min`/`max`/`abs` after InstCombine; split large blocks before `switch` terminators. Also handle the two new misses: on Win64, lower arguments in a trivial entry block (or pass the frame pointers through a single struct pointer) so the rest of the function stays in FastISel; replace `trunc iN>64` with storage-type word loads (JV-2). | ISel on affected blocks 5–8× cheaper (+7% code); 60-op switch blocks 0.56→0.20 s codegen. | general |
| JP-4 / JV-10 | Represent wide bitwise planes as `<K x i64>` vectors end to end (and/or/xor/not, masks, `!=0`); keep `iN` for arithmetic, shifts and concat. Needs JV-2 first; gate on the JP-1 census. | 256-bit 4-state AND 168→22 instrs; 1024-bit 518→39 (AVX-512) / 120 (AVX2); InstCombine 47→16 ms. The one real vectorization win. | reduced here (A1 removes the 120-bit `red`); general for wide designs |
| JP-8 | **Cohort kernel.** Build on the production masked all-active/activation-gated entries: constant per-member offsets; no per-member prologue; no host prepare/consume loop; bind-time read dedup, but only in tiered-up kernels (§6.2 b). Admission covers non-failing ops only; a guard failure falls back to the existing checked wrapper. | Per member ≈121 → ≈70 (O0, with JM-1) or ≈36 (`Less`, with dedup and JV-4) instrs. | reduced (clocked cohorts and non-collapsible logic) |
| JP-8b | True cross-member SIMD: the member compute loop does vectorize (AVX-512 VF=8), but the update-slot read-modify-write loop doesn't; it needs cohort-owned struct-of-arrays slots. | Deferred until JP-8 and A3 region-owned state land. | reduced |
| JP-9 | **Tier-up policy.** Build fused/masked templates, and any template bound to ≥64 instances, at `CodeGenOptLevel::Less` (greedy RA), with `!prof` weights. Cap by instruction count because of the machine-scheduler behavior recorded at `llvm_jit.cpp:583-589`. Keep `None` as the default: a global `Less`/`Default` costs 6.7–7.5× backend CPU (+10–20 s cold). Add a tier field to the cache key. | Fused group −46% instrs (spills 2,449→457), +70–100 ms per kernel. Est. −1 to −2 s Verilog, −0.5 to −1 s VHDL. | core (cone kernels are small and hot) |
| JP-10 | **Race fix.** `setNumCompileThreads(0)` plus a `ConcurrentIRCompiler` that is installed only when a cache directory exists (`llvm_jit.cpp:1298-1314`) means that a build with no cache path shares one `TargetMachine` across the app's up-to-8 materialization workers. Always install a `ConcurrentIRCompiler` (with a null object cache). | Correctness. Only C++ API and test configurations are exposed today. | general |

#### New kernel-architecture items (JN)

| ID | Change | Effect | Depends on |
|---|---|---|---|
| JN-1 | **Trusted fused entry ABI.** Keep one persistent, validated-once ABI v2 instance frame per executor (shared callback table per JIT, as in the A5 executor diet) and call kernels through a direct `(frame, activation_words)` entry. This replaces the per-call 888-byte rebuild, read re-validation and checked `resume`. | Part of fused host control 3.85 s + native entry 0.59 s (≈230 ns per call × 16.6M calls); executor memory −120–150 MB. | host validate-once contract (with JM-1) |
| JN-2 | **Cone code generation** (§6.3): one template-shared function per cone. Exact 2-state fast path; word-level 4-state fallback from JV-1/JV-4; internal nets as SSA; boundary-only publish. Build cone programs from the A2/RegionGraph dependency and observability proof; the standalone static concatenation helper is not a cone extractor. | ≈5 s of generated code; enables most of the ≈23 s host gap. 2–3 weeks of JIT work. | A2 extractor and observability contract |
| JN-3 | **Constant nets.** A net with a single constant driver and no force or VPI-write capability becomes an immediate. For example `prim_shifted`, which is a signal read today; the same applies to the ≈34k constant assigns A5 folds. | Removes loads and enables constant folding of cone arithmetic. | A2/A5 capability analysis |
| JN-4 | **Direct memory and a native region frontier** (with A3).<br>*Reads:* `inst->port_ptr` (invariant) plus 2 plane loads, instead of `map[slot]` + bounds check + planes + diamond (≈22 instrs saved per read).<br>*Writes:* compare, store-next and fanout bit (≈6), instead of the ≈30-instruction update-slot read-modify-write; private nets cost 0.<br>*Multi-cone regions:* levelized code with dirty-bit gating, `if (dirty & Ck) { v = cone_k(); if (v != cur) { cur = v; dirty \|= succ(k) } }`, at 5–8 instrs per cone. | Replaces the host masked frontier (≈365 ns per masked call) and the per-member host bookkeeping. | A3 region-owned state; signals addressable as instance base + constant or via port pointer |
| JN-5 | **Dual 2-state/4-state kernels** for the A4 P4 2-state shadow: branch on the per-signal `has_unknown` bits at entry; the 4-state body is the fallback. | For gf_mult: 127 vs ≈540 instrs per evaluation; general for X-free steady state. | A4 P4 |
| JN-6 | The historical plan called for pure-wave shape-kernel retirement after replacement coverage. Those kernels and bindings are already absent from live production, public headers, and tests; only the ignored `.orig` backup retains them. Generic static/masked routes remain subject to replacement coverage before retirement. | Avoid duplicating completed deletion work. | Live-source audit, 2026-10-02 |

#### Not recommended

| Item | Why not |
|---|---|
| Carry-less-multiply idiom recognition | LLVM 22's `llvm.clmul` does not lower to `pclmulqdq` on x86 (i16 becomes 63 instructions, i64 becomes 319), and the gain after collapse is ≈5 ms. |
| GFNI | `gf2p8mulb` hard-wires polynomial 0x11B, and `gf2p8affineqb` fits only constant-b multiplies, which LLVM already folds. |
| Instance-parallel SIMD across instances of a template | 32×i16 lanes cost 2.5 instructions per instance vs 52 scalar, but after cones total compute is ≈0.05–0.2 s. It also conflicts with per-instance layout and VPI. Revisit only for oblivious, clock-synchronous arrays. |
| More passes in the O2 string (GVN/NewGVN, correlated-propagation, aggressive-instcombine, DSE, instsimplify, SLP) | aggressive-instcombine creates intrinsics FastISel rejects; SLP finds nothing to vectorize. |
| A global backend level increase | Covered by JP-9 instead. |
| Alias scopes, TBAA or `noalias` (JM-8) | They do nothing without MemorySSA, and P98 rejected them on compile cost. Never mark `%frame` `noalias`. |
| Absolute helper symbols | They break the public runtime ABI. |
| Runtime PGO | Marginal on a cold benchmark. |

### 6.5 JIT batches and sequencing

Each batch is validated with:

- exhaustive formula checks at the modelled widths;
- the interpreter vs O0/O2 random differential;
- the `fsim.llvm` suite;
- a native-cache schema-bump test;
- per-module CPU from `FSIM_PROFILE_LLVM_MODULES`;
- `instructions:u` on mixed_codec → reduced → full `original_throughput` (§5.6).

| Batch | Program phase (§9) | Items | Effort | Est. gain |
|---|---|---|---|---|
| **J-A** Lowering and intrinsics | 0 | JP-1 census and JP-10 race fix first (no schema bump); then JV-1, JV-4, JV-2, JV-6, JV-7/8/11, JV-9, JM-3, JM-4/JM-7/JP-6, JM-5, JM-6, JP-2/JP-3 | 1–2 wk | 0.2–0.5 s Verilog; ≈0.5–1 s VHDL (unverified); compile CPU 0.3–0.8 s on mixed_codec |
| **J-B** Direct reads, entry, tiering | 0–1 | JM-1/JP-7, JN-1, JP-9 tier-up (tier field in cache key), JP-5 (including the Win64 entry block), JV-3, JV-5, JM-2 | 2–3 wk | JM-1 0.5–1.5 s; JN-1 part of 4.4 s; JP-9 1–2 s Verilog, 0.5–1 s VHDL (overlapping) |
| **J-C** Cone code | 2 (with A2) | JN-2, JN-3; cones tiered by JP-9 | 2–3 wk | ≈5 s generated code, plus the host categories A2 removes |
| **J-D** Region code | 4 (with A3/A4) | JN-4, JP-8 for cohorts that are not collapsed, JP-4 (if the census shows wide traffic after A1), JN-5 (with A4 P4); JP-8b deferred | 3–5 wk | part of A3's ≈25–28 s; JP-4/JN-5 design-dependent |
| **J-E** Retirement | 5 | The standalone `FusedStaticProcess`/`fuse_static_processes` helper is removed; `FusedMaskedProcess` remains live for all-active static cohorts. V22 runtime masked-region planning/dispatch, its global queue/barrier, the dead pending-touch marker, the unused `normalize_fused_container_reads` helper, and the static owned-Logic4 adapter are retired. The source-compatible runtime facade remains empty | V22 replacement/fallback controls pass 18/18; affected container gates pass 9/9 and source/package contracts 2/2. The current Release all-target relink and coherent 92-name gate pass; retired static route #450 and checked-prefix/preflight #471 pass in that gate | — |

**Bottom line for the JIT.**

- J-A and J-B are independent of every semantic decision and should start
  immediately. They are the JIT half of step 0 and most of what the VHDL DUT
  needs.
- Verilog cannot reach 15 s with JIT work alone: even at VHDL-level native
  speed, 0.62 + 6.49 + 12.06 = 19.2 s. J-C is where the JIT meets the event
  model, and it is mandatory for the Verilog target.

---

## 7. Resolved owner decisions

All A1–A7 and J-A–J-E tracks are adopted subject to the disposition table.
The default Verilog semantics are corrected with no legacy mode; delayed
publication is excluded; VHDL cycles remain exact. Observation is immediate,
coverage retains full detail, and ABI v2 is public and versioned. The initial
backend cap is 16,384 instructions. The shared-host measurement protocol
replaces the proposal's dedicated-host settings. Final gates apply to both
throughput cases and their ≤5% language gap. Explicitly deferred research
remains outside this implementation.

---

## 8. Correctness and risk findings (incidental)

1. **Allocation safety in fused staging is worse than the documented "granularity" caveat.** `stage_fused_masked_owned_slot` mutates `owned.phase` (`simir_owned_driver.cpp:560`) *before* several allocating steps (`:572-584`); `stage_fused_owned_slot` does the same (`:466` before `:472-486`). On `bad_alloc`, untracked bits can later be published under another owner or delta. Fix: reserve at install time, or stage into scratch.
2. **Observability cliff (R9).** Any waveform, VPI, API or debugger observer, deposit or force permanently disables all fusion. Nothing re-certifies.
3. **Fork cost.** Every fork allocates and zeroes three `signals.size()` vectors, scans all plans and calls `getenv`, even with no certified plans (`simir_fused_static_graph.cpp:497-498,604`; `simir_fused_masked_graph.cpp:632`). This is a risk for fork-heavy UVM testbenches.
4. **Quadratic frontier work.** Sort on every insert (`simir_fused_masked_execution.cpp:177`), front-erase retire (`:311`), and a per-member `find` for the terminal (`:391`).
5. **Latent JIT race.** With no cache directory, one TargetMachine is shared by up to 8 materialization workers (JP-10 in §6.4).
6. **Coverage gaps.** Kernel budgets (16 kernels / 16,384 ops) and the benchmark-shaped recognizers make fusion coverage design-dependent.

---

## 9. Delivery and verification sequence

1. Establish the architecture and measurement baseline (M1). Freeze the rebuilt
   control, add phase timestamps, allocation counters, JIT census and task/commit
   tracing; audit relevant historical attribution before retirement decisions.
2. Add scheduling witnesses, establish the IEEE interpreter reference, repair
   staging and no-cache compilation safety, then apply S0, memory quick wins
   and J-A. Validate Logic9 encodings before optimization assumptions (M2).
3. Implement A1 static element nets and retained-storage sensitivity ranges;
   filter irrelevant changes before queueing readers. Deliver J-B and public
   ABI v2 with consumer migration and explicit compatibility rejection (M3).
4. Build shared RegionGraph analysis for ownership, read ranges, domains,
   observation and invalidation dependencies. Deliver immediate materialization
   and selective demotion before enabling hidden state. Apply A2/J-C only to
   certified Verilog networks; VHDL gets cycle-exact regions (M4).
5. Share specialization/statement/generate-relative lowering, programs and
   per-instance bindings; persist template-instance artifacts directly. Fold
   constants with exact initialization/observation behavior (M5).
6. Complete A3/A4/J-D contiguous state, authoritative planes, driver classes,
   grouped fanout, native readiness and one scheduler entry per active region
   per required cycle. Include narrow/wide Logic4/Logic9 and guarded known-value
   paths. Retire the remaining superseded V7–V23 and J-E region/frontier
   machinery only after replacement and fallback coverage passes. The standalone
   compiler-fuser removal above is one bounded retirement slice. Then qualify
   both full throughput cases (M6).

Record implementation, validation evidence and remaining limitations after
every milestone in the campaign/resume documents. This ledger never equates
an estimate, a successful build, or a single screen with target qualification.
