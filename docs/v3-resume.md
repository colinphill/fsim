<!-- SPDX-License-Identifier: Apache-2.0 -->

> **2026-09-29 CI repair and commit preparation.** The performance campaign
> remains paused for review; no new benchmark or profiling run was made.
> [Dated language comparison](performance-throughput-language-comparison-2026-09-29.md)
> preserves the V23 report and its single-observation qualification limits.
> [Hosted run 36480701008](https://github.com/colinphill/fsim/actions/runs/36480701008)
> failed both Windows lanes in workspace application, Tcl debugger commands,
> and the performance parser. Artifact staging extended otherwise usable paths
> beyond MAX_PATH; design publication and metadata reads now use native extended
> paths on Windows. Proof reuse now compares resolved, platform-normalized path
> identities while retaining exact coverage, hash, and transcript checks.
> The C ABI test and Windows contract inventory now include the previously
> appended activation-mask fields (offsets 872/880/884, total size 888).
>
> Validation: full Release build passed; the first full CTest run passed
> 448/454. The stale ABI inventory and its dependent gates subsequently passed
> a 23/23 selected rerun. The three remaining package-dependent gates passed
> using an exact intended-source export at `/tmp/fsim-ci-prep-source-20260929`,
> leaving protected pre-existing `.aws` untouched. All 16 other offline-closure
> prerequisites had passed in the full run before the aggregate was checked.
> The clean manifest contains 2,135 ordered files. Focused Release passed 5/5
> and focused Debug passed 5/5. The Tcl-disabled build and all four selected
> tests passed.
> This is combined local evidence, not a fresh 454/454 run or hosted Windows
> confirmation. Logs are `/tmp/fsim-ci-prep-{release,debug,tcl-off}-*.log` and
> `/tmp/fsim-ci-prep-clean-{uvm,offline-closure}.log`.
>
> Preparation scope is 93 files: retained performance work, CI repairs, and
> this report. The `.orig` backup, generated graph/cache files, and `phase.fst`
> remain excluded. The commit message is prepared in
> `/tmp/fsim-prepared-commit-message.txt`; no commit or push has been made.
> Retain the documented aggregate-stage allocation-exception caveat. The
> next action is user review and commit/push authorization, with no further
> performance iteration before review.

> **2026-09-29 18:52 UTC throughput checkpoint — pause for review.** V23
> terminal closure passed 7/7 focused checks, exact full-case parity, and bound
> all 540 terminal readers. It removed 11,089,911 masked-frontier callbacks
> and 11,089,934 queue entries versus V22 while retaining original deltas and
> private commits. One uninstrumented cold screen measured **57.079736638 s**
> for Verilog `original_throughput` (42.080 s above the 15 s target) and
> **16.380882400 s** for matching mixed/VHDL throughput. Both have identical
> raw/canonical outputs. [Common CPU comparison and chart](../build/performance-campaign/verilog-throughput-fast-loop/v23-language-comparison/report.md)
> attribute the remaining gap using separate instrumented profiles. Retain
> V23; no further optimization or measurement before user review. An inherited
> aggregate-stage allocation-exception granularity caveat remains. The
> source-package-manifest CTest is blocked by protected pre-existing `.aws`;
> direct manifest/CMake registrations passed.

> **2026-09-29 17:56 UTC throughput checkpoint.** Retained measured control is
> V22 queue fix at **72.239842616 s** (57.240 s above target); no new timing
> claim. The active generic gate combines disjoint owned aggregate writers
> with a pure single-writer terminal reader in one masked native region while
> retaining the committed pre-update snapshot and ordinary boundary commits.
> A private post-elaboration copy normalizes only certified fixed container
> reads; the original process remains fallback. Wide 65/129-bit mixed-sink and
> VHDL projected-terminal tests, exact-source builds, graph review, and actual
> terminal admission/activation counters are pending before any full run.

> **2026-09-29 17:28 UTC throughput checkpoint.** V22 global-frontier queue fix
> passed six focused checks and full 6×12 transcript/78-object parity. Its
> counter-only run retained 15,069,883 masked calls and 96,599,156 represented
> members while reducing physical frontier callbacks by 18.7%; a hybrid
> ordered-bulk/min-heap WorkQueue removed the new sorting spike (30.0% to 1.4%
> exclusive CPU samples). One frozen uninstrumented cold screen took
> **72.239842616 s**, **57.240 s above** the 15 s target and 0.949 s slower
> than a separate V21 observation; no Wall gain or repeatability claim.
> [Three-way CPU histogram](../build/performance-campaign/verilog-throughput-fast-loop/v22-global-frontier-queuefix-profile/where-time-goes.png)
> and [profile report](../build/performance-campaign/verilog-throughput-fast-loop/v22-global-frontier-queuefix-profile/report.md)
> distinguish sampled CPU, counter diagnostics, and cold Wall. Retain V22;
> next source gate is generic packed producer/consumer plus pure terminal-reader
> region fusion that removes native/member/update work, preserving original
> delta and VHDL projected-owner semantics. No new source edit or measurement
> for a container callback micro-optimization alone.

> **2026-09-29 16:40 UTC throughput checkpoint.** The latest controlled
> frozen V21 full Verilog run took **71.291367206 s**, **56.291 s** above the
> 15 s goal; paired V20 timing was near equal. V22 global masked-frontier
> integration preserves exact full-key virtual ordering and passes a new
> width 3/65/129 runtime differential suite. Application/LLVM checks and
> independent projected exception review are pending. No V22 full-case
> mechanism or cold timing exists. Next: finish focused qualification, then
> require material callback and queue-entry reduction plus exact parity in
> a separate counter-only full-case diagnostic before a cold screen.

> **2026-09-29 16:10 UTC throughput correction.** A reverse-order frozen V21→V20
> full 6×12 cold pair measured **71.291367206→71.182836692 s**, only
> +0.109 s paired, with exact output and object parity. The first V20
> 55.575 s cold observation was much faster than its paired/profiled runs;
> the initial +14 s V21 cross-window difference is not causal evidence.
> Retain the generic V21 bridge, which bypasses 11.7567 million private
> fanout entries but leaves masked activations unchanged. The 15 s target
> remains unmet; no repeat cold run. [Evidence](../build/performance-campaign/verilog-throughput-fast-loop/v21-private-bridge/freeze/controlled-pair-receipt.json).

> **2026-09-29 16:00 UTC throughput checkpoint.** Retained V20 control is
> **55.575112179 s** cold Wall, 40.575 s above the 15 s target. V21 private
> graph bridge passed six focused tests and exact full-case output/object
> parity, and bypassed 11.7567 million generic private fanout entries in a
> counter-only diagnostic. Its one uninstrumented full-case cold result was
> **69.583531710 s** (+14.008 s versus V20). Keep V21 implementation per
> user direction while attributing the added path; no repeat cold run or
> automatic rollback. [Receipts](../build/performance-campaign/verilog-throughput-fast-loop/v21-private-bridge/freeze/).

> **2026-09-29 15:40 UTC performance checkpoint.** Measured V20 remains
> **55.575112179 s** cold Wall, 40.575 s above target. V21 joins certified
> private V19 producer outputs to V20 masked consumers without changing
> original delta boundaries or native kernels. Runtime differential and
> direct-commit-counter checks pass; exact-source VHDL application parity
> and final source review are pending. No V21 timing claim exists.

> **2026-09-29 15:13 UTC performance profile.** Frozen V20 CPU sampling on
> the full Verilog case produced 6,129 exclusive samples (zero lost, 77
> unresolved); fanout/queue and masked-region/scheduler host work remain
> prominent. The [histogram report](../build/performance-campaign/verilog-throughput-fast-loop/v20-masked-frozen-profile/report.md)
> distinguishes this profiled diagnostic from the retained 55.575 s cold
> observation. Next step is a bounded generic graph-source gate.

> **2026-09-29 15:02 UTC performance checkpoint.** Retain generic V20
> activation-masked graph fusion. Its one full Verilog cold Wall result is
> **55.575112179 s** with exact V16 transcript parity, leaving 40.575 s to
> the 15 s goal. A separate counter diagnostic records 15.070 million masked
> calls representing 96.599 million singleton activations, with zero
> fallbacks; normal boundary publication remains. Wide 9/65/129-bit SV/VHDL
> admission and parity passed focused tests. Next is a frozen-binary CPU
> profile and time-category report before another production change. See
> `build/performance-campaign/verilog-throughput-fast-loop/v20-masked-first/freeze/`.

> **2026-09-29 14:40 UTC performance checkpoint.** Retained V19 full
> Verilog cold Wall is 59.599133333 s, 44.599 s over the goal. V20 masked
> compiler/runtime initial focused checks pass, while the latest pending
> observer runtime case awaits rerun. Application binding and genuine sparse
> Logic9 VHDL fusion at widths 9, 65 and 129 are the next gate. No V20 full
> measurement has run.

> **2026-09-29 13:54 UTC performance checkpoint.** Retained V19 graph
> fusion includes wide SV/VHDL and scoped fork certificates. Its one full
> Verilog `original_throughput` cold screen is **59.599133333 s**, with exact
> V16 output parity and a 44.599 s gap to the 15 s target. The separate
> diagnostic confirms 540/540 plans survive the testbench fork and records
> 1,516,205 fused invocations bypassing 97,037,120 owner-stage calls. Next
> source gate: generic compiled private
> singleton/reduction chains, preserving V16 prepared batches and original
> delta/owner semantics. No repeat cold run.

> **2026-09-29 13:42 UTC performance checkpoint.** V19 generic graph
> fusion, including wide SV/VHDL vectors, is retained at one 81.071374422 s
> full Verilog cold observation, 66.071 s above the 15 s target. Its tiny
> initial route may be cut off by global invalidation at a testbench fork.
> Scoped fork certification and focused fallback/survival tests are next;
> no repeat cold screen or new chain candidate before route coverage proof.

> **2026-09-29 13:32 UTC: V19 graph-kernel pilot retained, target unmet.**
> One uninstrumented full Verilog `original_throughput` 6×12 cold screen took
> **81.071374422 s**, versus frozen V16 **83.468136206 s**; the 2.397 s
> one-observation difference is not a causal or repeatability estimate.
> Raw/canonical transcripts are byte-identical to V16; all 16 inputs, six
> summaries and seven correctness lines pass. A separate counter-only full
> diagnostic found 540 bound cohorts, five shared kernels and 888 fused
> invocations representing 56,832 members, only 0.0294% of V16 prepared
> member executions. V16 already batched those producer cohorts, so this is
> not 56,832 native calls saved. Six focused checks pass, including actual
> VHDL `bit_vector` and `std_logic_vector` fusion at widths 9/65/129 with
> exact nine-state and delta parity. Next extend the elaborated graph plan
> and fused kernel across repeated singleton/private XOR/reduction chains;
> preserve V16 batching, original owner/update and boundary delta behavior.
> The 15 s target remains unmet. Keep V19 for graph-driven iteration; do not
> repeat this cold screen. See `build/performance-campaign/verilog-throughput-fast-loop/v19-graph-kernel/final-vhdl-freeze/`.

> **2026-09-29 11:33 UTC: V19 generic graph-kernel prototype authorized.**
> Precompute closed internal-chain membership and disjoint owner masks from
> the elaborated graph; compile eligible strata as fused native work without
> per-member update staging. Preserve delta and scheduler boundaries. The
> compiler fixture and mechanism counters gate a later single full cold
> screen. V16 remains retained at 83.468136206 s; V18 is restored/rejected.
> User direction retains the V19 graph-kernel work through a first regression
> for diagnosis and iteration, with V16 frozen as control.

> **2026-09-29: V18 region fold rejected; retained throughput is V16.**
> Frozen V16 took **83.468136206 s** on full Verilog `original_throughput`
> 6×12; V18 took **94.447004235 s** despite eliminating 10,267,758 physical
> consumer tasks in its full diagnostic. Exact output/native-object parity
> passed, then V18 production was restored to V16. Generic semantic tests and
> archived V18 receipts remain. The 15-second target is 68.468 s away. Next
> architecture gate is generic fused region/kernel or stronger whole-chain
> elimination; P26 publication-time range filtering already failed and must
> not be repeated. Batch 189 remains unstarted; no commit, push or CI.
# fsim v3 clean resume

**Current performance checkpoint, 2026-09-29:** V16's app-owned stable-cell
pool is provisionally retained after four affected Release tests, structure
check, selftest, and a 129-instance block-growth parity witness. Its frozen
preferred ThinLTO binary took **83.468 seconds total cold Wall** on the full
six-instance/twelve-codeword Verilog `original_throughput` case. Raw and
canonical transcripts and all 57 native objects match V15. This is one
observation, not a causal or repeatable saving; the 15-second target remains
**68.468 seconds away**. A separate full-case diagnostic confirmed all
193.440 million prepared members on the fast route with no demotions; its
7,543 CPU samples rank runtime admission, static notification, compiler
preflight, and cohort queueing. The diagnostic source and launcher were
restored byte for byte. The rebuilt preferred executable differs from the
frozen screened V16 binary despite exact source/configuration hashes, so
keep the frozen binary as the comparison artifact. See
[performance-resume.md](performance-resume.md) and the V16 receipts. The
09:40 UTC checkpoint held a grouped fanout/queue fusion because it changed
the exception-visible prefix of trigger-mask stores. The next V17 gate is a
source/lifetime proof for a possible 72-to-64-byte hot process record, with
the frame-owner `shared_ptr` kept in cold storage; no implementation is
authorized at the 09:40 gate. The bounded V17 frame-owner split is now built:
the cold sidecar owns the frame, the 64-byte aligned hot record borrows a raw
pointer, and the executor is destroyed first. A post-fork shared-frame
deferred-promotion witness and all six affected Release/structure checks pass.
Optimized deque access uses eight-entry blocks and shifts. Its one full cold
screen took **88.595 seconds**, versus V16's separate 83.468-second
observation, with exact raw/canonical output and 57 native object bytes.
The change is rejected for no supported end-to-end gain; four production
files were restored exactly to V16 and the new fork regression test remains.
The frozen V16 binary remains the retained comparison artifact, 68.468
seconds above the goal. Next, one full-case CPU-clock profile of that
uninstrumented binary will show phase Wall and a disjoint sampled-self
histogram before another candidate. Batch 189, CI, publication, and all-ten
qualification remain outside this loop.

**Active performance campaign, 2026-09-28:** after `eb8a5042` was committed
and pushed, the user resumed optimization on full Verilog
`original_throughput`, targeting 15 seconds total cold Wall or less. V1 owns
a current preferred-ThinLTO baseline and separate detailed profile before
candidate selection. Follow [performance-resume.md](performance-resume.md);
evidence is under `build/performance-campaign/verilog-throughput-fast-loop/`.
No replacement-CI monitoring or further publication is authorized. Preserve
the existing unrelated untracked files; Batch 189 remains unstarted.
The exact frozen V1 ThinLTO binary takes 170.438 seconds cold Wall on the full
throughput fixture, with native setup/simulation 162.722 seconds and all six
fingerprints/seven correctness lines matching. V1's separate profile and
route-count receipt is under the active evidence root. The 15-second target is
unmet. V2's bounded validated static-wait completion trial passes focused
Release 5/5 and one cold full-throughput observation at 146.515 seconds;
the canonical transcript and all 57 native object bytes match V1. This is one
observation, not a repeatable speedup claim. The separate V2 profile confirms
external-boundary self sampling fell to 0.18%, while executor-resume self rose
to 15.55%; profile duration contradicts the single cold-Wall direction.
V3 first measures actual-resume-weighted alias eligibility before a bounded
native completion metadata trial. The full-case diagnostic replay found 86.45%
of resumes in processes with no container registers, with exact V2 stdout.
The bounded V3 change passes six focused Release tests and one cold full-case
observation at 137.220 seconds, with exact V2 stdout and native object bytes.
It is provisionally retained; the 9.295-second observed difference is not a
causal or repeatable gain claim. The separate V3 profile reports 12,238
samples, 79 unresolved and no losses; static-cohort execution, next-delta
wakeup and cohort resume lead host self costs. An app-level cache-hit replay
does not prove full registered-cohort readiness, so no prepared cache is
selected. The 15-second target remains unmet.
V4 now trials the simpler next-delta dynamic-wait guard using existing hot
state; six focused Release tests pass. Its one full cold observation is
134.270 seconds total Wall (126.955 native), with exact V3 output and 57
native object bytes. The observed 2.950-second difference is one sample, not
a causal or repeatable gain claim. V4 is provisionally retained. Its separate
full-case profile has 12,930 samples, 88 unresolved and none lost; generated
JIT self accounts for 19.52%, alongside dominant host executor and scheduler
costs. The target remains unmet.
V5 was a bounded move of the interpreter-operation tracking flag from cold
state into existing hot-state padding; Clang 22 confirms the 72-byte process
record was unchanged. A mid-resume live-enable regression and five related
tests passed (6/6). One cold full-case observation was 137.390 seconds,
3.120 seconds slower than V4, with identical output and 57 native objects.
The V5 source and test were reverted; its evidence remains archived.
V6's first fused-cohort candidate used an instruction order absent from the
actual full workload. Its cold observation was 170.949 seconds; profiling
mapped no fused helper. A construction-only dump found `DDREREBWSJ`, then the
diagnostic source was restored. V7 implements that exact order and passes
focused 3/3. Its one cold full throughput observation is 129.308 seconds
total Wall (122.093 native). The transcript and all 57 native object bytes
match V4; a separate profile maps and samples the fused helper. V7 is
provisionally retained, with no repeatability or causal speedup claim. The
15-second target is still 114.308 seconds away. Current cohort wrapper and
fused preflight costs need source-backed ranking before the next candidate.
V8's shared-context trial is not qualified as a standalone optimization: its
single cold screen took 155.867 seconds, although a separate profile confirmed
fusion and showed a different timing direction. A restored construction-only
diagnostic then proved 540 exact pure cohorts with 34,560 fully transient
members and one common actual input pair per cohort. V9 uses that proof for a
compact native helper. Focused Release tests pass 4/4; its single cold full
throughput observation is **111.349 seconds total Wall** (104.235 native),
with exact V7 transcript and all 57 native object bytes. A separate profile
maps and samples the compact helper, while the old full fused helper is absent.
V9 is provisionally retained, without a repeatability or causal speedup claim;
the 15-second target remains **96.349 seconds away**. Re-rank current host and
native costs before another candidate. Full receipts are under
`build/performance-campaign/verilog-throughput-fast-loop/v9-compact-cohort/`.

**CI repair and publication, 2026-09-28:** the user authorized inspection of
the existing CI failures, fixes, commit and push, with no monitoring of the
replacement run. Run `36437357263` at `6efa4c35` passed both Ubuntu lanes and
failed both Windows lanes. Repairs configure the fetched Tcl library for all
application tests (including direct `Tcl_Init`), keep stream-channel newlines
consistent with transcript commands, resolve both sides of campaign fixture
path identities, and clean the debugger fixture from a parent process after
SystemC's intentionally retained DLLs unload. Archive upload now skips an
archive-producing step that never ran. No substantive test assertion was
removed; the blocked closure-matrix case retains its dependency.

The cumulative all-target Release build and full suite pass 454/454 (140.94
seconds); fresh affected Debug passes 7/7 (22.60 seconds). The performance
parser passes 40 tests, including a new alias regression that fails against
the prior implementation. Final evidence is under `build/ci-repair-20260928/`.
The canonical full index was refreshed at `2026-09-28T20:33:45Z`, with 57,651
nodes and 372,201 edges; the existing 325 partial files and one unusable include
remain disclosed. This publication includes the completed simplification work
and preserved handoff documents. Hosted Windows confirmation is unverified;
do not monitor the new CI run. Batch 189 remains unstarted.

**Simplification implementation complete, 2026-09-28:** the user-authorized
plan has an explicit disposition for every selected requirement. S1, S2 and S3 are completed intermediate
packets. S2 connects HDL coverage discovery, per-instance hits and persistence;
Release 454/454, fresh affected Debug 14/14, Tcl-off 14/14 and final docs/package
24/24 pass. Final-binary O0/O2 replays prove native execution for both covered
leaf instances. Root independently verified the cumulative diff, six new-file
hashes and unchanged staged patch against the S2 closure receipt. The canonical
persistent graph was refreshed at `2026-09-28T17:49:31Z`.

S3 SDF request integration is locally closed. Release has 454 distinct passing
cases (451 in the full run plus three corrected diagnostic-count audit reruns),
fresh Debug 8/8, Tcl-off 7/7 and final docs/package 24/24. Actual O0/O2 native
producer resumes and source-free saved-snapshot replay are verified; no on-disk
native object-cache hit is claimed for the tiny fixture. CLI min/typ/max,
selectors, multi-file precedence, indexed provenance and real C/Tcl safe-point
application/rollback have permanent regression coverage. Root independently
verified `build/simplification-packet-3/closure-receipt.json`, cumulative/staged
patches, 68 file identities and the final executable. The canonical persistent
index generation is `2026-09-28T18:55:01Z`.

S4 cache reproducibility is locally closed:
final Release 454/454 (134.06 seconds), fresh Debug 16/16 (7.17 seconds) and
Tcl-off 16/16 (7.07 seconds). Fixed-input SV/VHDL replay agrees across reversed
catalog revision IDs; the native regression records two cold misses and two
warm hits after reversal. SDF relative-path and body-limit boundary tests found
by the full-plan audit also pass in all three configurations. The initial
453/454 Release run exposed source/object specialization-key version mismatch;
the consistent-version repair retains the five-path equality assertion. The
secondary runner's initial empty regex match is archived as harness triage,
not qualification.

Final-binary SV/VHDL replay is recorded in
`build/simplification-packet-4/reversed-id-final-comparison.json`.
Final docs/package gates pass 24/24. Root verified the closure receipt, all
22 referenced evidence hashes, 77 source/document identities, cumulative patch,
Release executable and unchanged original staged patch. The canonical graph
generation is `2026-09-28T19:40:10Z`; recorded partial-parse limits remain in
the receipt. S5 used the same Sol/high execution and Luna/max review
configuration. Its four candidate dispositions and requirement-by-requirement
audit are in [the disposition](simplification-disposition.md). The baseline is
frozen at `build/simplification-packet-5/baseline/manifest.json`; the current
ThinLTO binary passes the full workload's 19 input, 75 fingerprint and 87
correctness checks. Its separate single cold baseline is 33.186 seconds.
Fresh attribution rejects the DCO-7 index for its small observed working set
and T9's simple guard for changed observer-lifetime semantics. F2 passed dump,
native-object and cache compatibility, but is rejected: its 33.134897-second
cold screen has no useful measured change in the affected native stage. The
trial is restored byte-identically. T3 is retained as a small existing-field
constructor-lifetime simplification. Actual O0/O2 suppression-route and
sequential-executor witnesses pass, and all 308 native objects remain identical.
Its single cold screen is 34.137321 seconds, 0.950973 seconds above baseline;
no speedup or performance-neutrality conclusion is supported. Final cumulative
Release passes 454/454 (146.72 seconds), fresh Debug and Tcl-off pass 21/21
each. Final docs/package gates, graph coverage, CTest surface comparison and
exact source/evidence identities are recorded in
`build/simplification-packet-5/closure-receipt.json`. The six-phase audit found
no additional selected requirement. F2/T3 were not reindexed immediately before
their screens; the final integrated refresh and this procedural deviation are
recorded explicitly. No further implementation is scheduled by this plan.
The final canonical graph generation is `2026-09-28T20:21:27Z`, with 57,647
nodes, 372,200 edges, 325 partial files and one unusable include. All three
T3 changed paths have matching metadata and no recorded parse issue.
Batch 189 and the
original staged handoff were preserved through S5 closure. The subsequent
publication authorization is recorded above.

**Current checkpoint, 2026-09-28, simplification packet S1:** The user-authorized
first 20-change packet is implemented on top of `6efa4c35` in the uncommitted
`codex/v3` worktree. Nine dead lowerer methods and the unused runtime
assertion formatter were removed; retained container helpers, bounded
effective-subtype cache accounting, and selected test helpers were
consolidated. The two-validator CMake inventory parser prototype was
rejected because it added 16 source lines without simplifying the obligations.
No feature stack was retired. The [packet disposition](simplification-disposition.md)
records source ownership and the separate coverage/SDF repair sequence;
[the packet plan](simplification-implementation-plan.md) remains outside
Batch 189 numbering.

The full Release build and 454/454 CTests passed. Affected Debug and Tcl-off
builds passed with 12 workers, followed by focused 9/9 and 8/8 tests.
The separate SDF and VITAL pilot executables were then explicitly rebuilt
in both secondary configurations; fresh pilot and semantic checks passed
3/3 in each tree.
Generated CTest names, commands, fixtures, labels, and execution metadata
match the frozen 454/454/445 registrations. Fixed-library SV and VHDL
artifact replays from byte-identical inputs have equal compiled payloads,
cache objects, logical library rows, and behavior before and after the packet.
Raw fresh-compile artifact names vary because workspace revision IDs are
random and catalog order feeds the cache key; the controlled comparison and
raw failures are both retained. The tiny `--engine compiled -O O0/O2` witness
outputs match, but those runs did not materialize new native-cache objects,
so they are behavior smoke rather than native-codegen proof. Exact commands,
binary identities, hashes, case inventories, and logs are in
[`closure-receipt.json`](../build/simplification-packet-1/closure-receipt.json).
The final documentation, packaging, installed-public-contract, and related
source/SCV/SystemC gate passed 24/24 after the disposition's final edit.
Preserve the pre-existing staged performance handoff and other
dirty worktree entries. Do not commit, push, monitor hosted CI, or restart
the paused mixed-codec optimization loop under this packet. After local
closure, the next bounded product work is the separately planned S2 HDL
coverage integration; SDF repair follows its own gate.

**Prior performance checkpoint, 2026-09-28:** The performance change set and ten-case
matrix were committed and pushed as `6efa4c35` to `origin/codex/v3`.
Release 454/454, focused Debug 7/7, Tcl-off 6/6, and final packaging/docs
24/24 passed. All ten matrix cases passed stimulus and correctness parity;
the user accepted the small short-case timing regressions. CI was not
monitored, as requested. The under-15-second full mixed-codec loop remains
paused and its target unmet (fresh matrix Wall 32.933 seconds; earlier P96
32.579 seconds). Read the self-contained
[performance resume instructions](performance-resume.md) for worker setup,
exact commands, evidence, accepted/rejected work, and the next bounded action.
Saving the handoff does not restart the loop or authorize another push.

Verify the live branch, HEAD, tracking ref, and worktree first. Batch 188K is
complete on `codex/v3`; its initial commit is `b54d7066`, followed by the
completion repair commit. [The implementation plan](implementation_plan_v3.md)
is the authority for the accepted Tcl contract and Batch 189 scope. Batch 189
has not started. The user authorized the
[cross-simulator performance campaign](performance-campaign.md) on
2026-09-25, using `d11004e4` as its fresh baseline. The deterministic reduced
corpus, cross-simulator stimulus parity, and fresh baseline/profile report are
complete. User rule added September 26: retain performance-neutral changes that
simplify implementation, with correctness gates preserved. Assess separable
cleanup independently; do not equate new cache state with simplification.
The former all-ten lower-median and seven-pair qualification program is
deferred under the user's new full mixed-codec iteration direction.

**Historical full mixed direction, 2026-09-28.** Optimize only the full original
`mixed_codec` case, the longest mixed-mode elaboration workload. The target is
under 15 seconds for compile, elaborate, native setup, and simulation together.
P37 is the historical starting baseline; P96 is the current retained source,
and P84 ThinLTO is the preferred host build configuration. The lowest
observed full wall sample is P96 at 32.579 seconds.
Profile all phases in detail,
attack the largest measured host path, then repeat. Each candidate gets one
cold, uninstrumented wall run; no Vivado run, paired repetition, or reduced
transfer gate is part of this loop. Keep the same external RTL, VHDL
compatibility option, one CPU, O2 JIT, cold caches, and disabled waves/debug;
require the saved correctness lines and all final fingerprints. Instrumented
profile time is diagnostic only. The older all-ten goal remains open at 0/10
qualified cases and is deferred, not satisfied. Evidence for this loop is
under `build/performance-campaign/mixed-long-fast-loop/`.

**First full-case iteration.** The P37 cold wall baseline took 86.894 seconds:
1.293 compile, 40.594 elaborate, and 45.005 native setup/simulation. The
one-file evaluator-local VHDL bitwise-target memo passed the affected Release
build and semantic/elaboration tests, then took 72.498 seconds in one cold
full-case run: 1.343 compile, 28.105 elaborate, and 43.048 native
setup/simulation. All 75 final fingerprints and 87 correctness lines matched
the saved fixture. This is a single-run observation, not a dispersion claim.
The fresh [all-phase profile](../build/performance-campaign/mixed-long-fast-loop/p39-bitwise-memo/profile/profile_result.json)
then showed substantial background LLVM materialization. P40 awaited only
already-selected nonadaptive background modules before a long run, preserving
short bounded-run and adaptive policies. Its affected Release build and five
focused tests passed. The single cold full-case run took 64.182 seconds:
1.344 compile, 28.260 elaborate, and 34.576 native setup/simulation; all
saved fingerprints and correctness lines matched. Peak RSS was 1,041,388 KiB.
The [current P40 profile](../build/performance-campaign/mixed-long-fast-loop/p40-background-barrier/profile/profile_result.json)
still shows substantial declaration/package lookup in elaboration and native
JIT worker work plus cohort/update work; select the next source path from
this current attribution, without adding sampled percentages together.

**Current fast-loop checkpoint.** P41 cached immutable VHDL callable
resolution and return subtype metadata within the evaluator, removed a repeat
effective-declaration lookup, and gated LLVM IR-shape scans to profiling. Its
affected Release build and five focused tests passed. One cold full-case run
took 62.682 seconds (1.346 compile, 26.304 elaborate, 35.031 native), with
the saved 75 fingerprints and 87 correctness lines matching. Relative to the
P37 single run this is 24.212 seconds (27.9%) lower; the under-15-second
target remains unmet. The [fresh P41 profile](../build/performance-campaign/mixed-long-fast-loop/p41-callable-memo/profile/profile_result.json)
still exposes Lowerer binding, package lookup, LLVM workers, and generated
RAM reads. P42 now caches immutable raw expression resolution only within
the exact binding-frame scope, memoizes the bitwise callable fallback, and
reuses one package-member index lookup per resolver call. Its affected
Release build and eight focused tests passed; an additive same-process
generic-frame witness preserves the original result 6 and verifies result
59 for different actuals. Its one cold full-case run took 59.767 seconds:
1.394 compile, 21.988 elaborate, and 36.383 native. This is 2.915 seconds
below the P41 single sample, with no native speedup claim. The
[P42 profile](../build/performance-campaign/mixed-long-fast-loop/p42-resolution-bundle/profile/profile_result.json)
counted 10,459,163 raw-resolution hits, 149,159 misses, and zero cap drops
across 9,145 scopes. P43 targets the measured generated 8192-bit RAM read;
its word-select lowering and Logic9 invalid-fill repair passed the affected
Release build and four focused tests. Its one cold full-case run took 53.900
seconds (1.344 compile, 21.438 elaborate, 31.115 native), with all saved
fingerprints and correctness lines matching. This is 5.868 seconds below the
P42 single sample. The [fresh P43 profile](../build/performance-campaign/mixed-long-fast-loop/p43-word-selection/profile/profile_result.json)
places remaining native self work in generated process 226, static cohorts,
and LLVM; the selected process 662 left the leading rows. An isolated
[IR check](../build/performance-campaign/mixed-long-fast-loop/p43-selected-ir/result.json)
found its variable 8192-bit shifts fell from 32 to zero. The next change
requires a current-profile-supported dominant path. P44's transient literal
shortcut reached 24,807 eligible literals and 25,510 compiler loads, but
selected optimized LLVM module shapes were unchanged and its one cold wall
sample was effectively level at 53.804 seconds. The added compiler state and
test were archived under `mixed-long-fast-loop/p44-transient-literal/` and
removed; native cache schema remains v171. P45's scoped static VHDL
subtype-name cache passed the affected Release build and seven focused tests.
Two cold full-case samples took 56.150 and 52.644 seconds; elaboration took
20.032 and 19.732 seconds, respectively, versus P43's 21.438 seconds.
Native time varied from 34.773 to 31.566 seconds between the P45 samples,
so the first total reversed despite the repeatable targeted elaboration gain.
All samples matched the 75 final summaries and 87 correctness lines. The
[P45 profile](../build/performance-campaign/mixed-long-fast-loop/p45-static-subtype-name/profile/profile_result.json)
counted 1,126,511 cache hits, 13,474 misses, and no cap drops across scoped
readouts. Its run-prefix time includes the background barrier; those numbers
are not additive phases. P46 then cached only the pure no-binding integral
evaluation attempt within the existing Lowerer scope. Its affected Release
build and seven focused tests passed. One cold full-case run took 51.489
seconds (1.344 compile, 18.829 elaborate, 31.314 native), with the saved 75
summaries and 87 correctness lines matching. Against P45's quiet sample,
elaboration fell 0.902 seconds and total time fell 1.155 seconds; these are
single-sample observations. The [P46 profile](../build/performance-campaign/mixed-long-fast-loop/p46-pure-integral-attempt/profile/profile_result.json)
counted 939,713 scoped cache hits, 86,875 misses and zero cap drops. Package
resolution remains a prominent overlapping elaboration call path. The
current full-case observation is 51.489 seconds, still above the 15-second
target. P47 replaced only the package resolver's active-context hash set with
a stack ancestor check; its cycle-and-diamond test preserves repeated branch
visits and cycle termination. The affected Release build and semantic and
elaboration tests passed. One cold full-case run took 50.937 seconds (1.343
compile, 17.977 elaborate, 31.615 native), with the saved 75 summaries and
87 correctness lines matching. Against P46, elaboration fell 0.852 seconds
and total time fell 0.552 seconds in one sample; native time rose 0.302
seconds. The [P47 profile](../build/performance-campaign/mixed-long-fast-loop/p47-context-ancestor/profile/profile_result.json)
shows package-member resolution at 12.35% inclusive elaboration CPU versus
P46's 15.44%; inclusive paths overlap. The current observation is 50.937
seconds, still above target. Next measure LLVM worker CPU stages before
choosing another native compiler change. A rejected two-package-instance
fixture remains scratch evidence: it returned 55 even with the new raw cache
disabled, so it is not evidence of a P42 cache regression.

P48 added profile-gated thread CPU clocks without changing generated code or
the native cache schema. The [full-case diagnostic profile](../build/performance-campaign/mixed-long-fast-loop/p48-thread-cpu-stages/profile/profile_result.json)
matched all 75 summaries and 87 correctness lines. Across 312 materialized
modules, LLVM workers spent 15.327 seconds total CPU: 9.491 in
`add_process_module` and 5.832 in lookup/materialization. These are nested
stages, not additive with the total. Within `add_process_module`, lowering
used 1.086 seconds, IR optimization 6.913, verification 1.065, and profile
IR-shape scans 0.322. The lookup bucket includes backend, ORC, cache, and
link work; it is not a pure backend measure. Eight unmaterialized adaptive
jobs report CPU unavailable. This instrumented run makes no speed claim;
the retained uninstrumented P47 observation remains 50.937 seconds.

P49 measured LLVM optimization passes without changing the pipeline. Its
[native-only diagnostic](../build/performance-campaign/mixed-long-fast-loop/p49-pass-cpu/native-profile/pass-cpu-analysis.json)
reused the saved P48 elaborated snapshot with a fresh native cache and the
current executable; all 75 summaries and 87 correctness lines matched.
Across process modules, exclusive pass CPU was 7.007 seconds: InstCombine
3.641, EarlyCSE 1.272, SROA 1.043, and SimplifyCFG 1.024. Generated cohort
modules added only 0.048 seconds. All 447 pass summaries had zero invalid
nesting, remaining frames, and unavailable calls. Inclusive pass totals
overlap; pass timing is diagnostic, not a wall-time gain. The larger 65,528-byte
DWARF capture still cannot span the 86-KiB static-cohort frame, and perf lost
245 samples, so call ancestry is incomplete. The current cold wall remains
50.937 seconds; select the next implementation from these costs and current
source proof before changing the optimizer.

P50 isolated the current O2 pass sequence on fresh IR dumps for codec_check
runtime process 3221 (8,633 operations) and hot multiplier 226 (689
operations). [The offline comparison](../build/performance-campaign/mixed-long-fast-loop/p50-instsimplify-isolation/comparison-v3/result.json)
found that replacing InstCombine with InstSimplify lowered optimizer CPU on
the codec body but expanded its object 6% and the multiplier's optimized IR
and object 38.5% and 26.1%. Adding InstSimplify before InstCombine preserved
the final IR but did not lower optimizer CPU. No pipeline change or cold wall
trial followed. The first dump mistakenly used specialization ID 483 as a
runtime process ID; that diagnostic is preserved separately, and the current
comparison uses the verified runtime ID 3221.

P51 used a temporary scheduler Callgrind hook over ticks 0–15,122,750 of
the saved mixed-codec artifact, then restored the source and configured CLI
to their exact prior hashes. The [partial-window summary](../build/performance-campaign/mixed-long-fast-loop/p51-scheduler-callgrind/summary.json)
records 26.999 billion main-thread instructions. `Scheduler::run` owns the
window but contributes 0.49% self; `execute_static_cohort` contributes 1.33%
self, while its 42.40% inclusive path includes generated work. Same-run JIT
maps resolve 26.25% self, with four 689-operation `gf_mult` bodies at 20.20%
combined. `PackedLogic4::get` is 8.31% self, chiefly through integer operand,
binary value, and dynamic index helpers. This early 5% window also contains
LLVM lookup (4.65% inclusive). Valgrind masks a host CPU feature used in the
native cache key, so the run added 312 new objects while preserving all 312
original objects; it was not warm. These instruction counts are diagnostic,
not a wall-time result or a complete scheduler cost estimate. The retained
cold wall at that point was 50.937 seconds.

P52 isolated LLVM's default O1 pass pipeline on current `gf_mult` IR after
the existing O2 scalar cleanup. It greatly reduced the object, while smaller
LICM/induction and SCCP variants did not reproduce the reduction. The first
bounded trial admitted 42 generated packed/cohort modules as well as 12
process modules; its one cold wall was 50.785 seconds and native time rose
against P47. Profile clocks attributed about 1.259 seconds of added optimizer
CPU to those 42 modules. P52B now runs the extra pipeline only for a single
prepared process with at most 2,048 IR instructions, 256 blocks, and a real
natural loop. It leaves O0/O1 and cohort wrappers unchanged, and advances
the native object schema to v174. Its affected Release build and profiled
`fsim.llvm` test passed, including nested-loop success, range, and overflow
checks; the O2 fixture selected the new gate while cohort rows did not. One
cold full-case run took **48.782 seconds** (1.344 compile, 17.425 elaborate,
30.011 native), matching all 75 summaries and 87 correctness lines. That is
2.155 seconds below P47, including 1.605 seconds in native work; the 0.552
second elaboration difference is incidental single-run variation, not caused
by the LLVM change. Peak RSS rose 1,440 KiB. The target remains unmet. See
the [P52B wall result](../build/performance-campaign/mixed-long-fast-loop/p52b-process-only-loop-o1/wall/fast-loop-result.json)
and [focused gate](../build/performance-campaign/mixed-long-fast-loop/p52b-process-only-loop-o1/focused-gate.json).
Next quantify which processes execute interpreted integer and packed-value
operations using existing route-neutral counters; do not enable process
profiling flags that change native cohort routing.

P53 added route-neutral interpreted-owner and Logic9 batch counters. Its
native-only profile counted 34.35 million `std_logic`/direct-owner rejected
slot visits, which can include retries and are not unique writes. P54 tested
ordered compact Logic9 pending snapshots with FIFO replay and focused
reference parity; five initial focused tests passed, and a corrected runtime
fixture passed after replacing an invalid add-process-after-start action with
a late driver force. Its one cold full-case run matched all 75 summaries and
87 correctness lines but took 50.037 seconds (17.978 elaborate, 30.664
native), versus retained P52B's 48.782 seconds (30.011 native). A separate
[Logic9 reach profile](../build/performance-campaign/mixed-long-fast-loop/p54-logic9-ordered-pending/native-profile/fast-loop-result.json)
counted 6.535 million consumed compact snapshots and 13.009 million mask
runs. P54 is **rejected for performance** despite material reach. Its exact
source and test changes were reverted in the P55 batch. P55 defers the VHDL
lvalue subtype query until an aggregate RHS needs it and moves an owned
optional value; six focused tests and all 75 summaries/87 correctness lines
passed. Its one cold run took 50.636 seconds (17.374 elaborate, 31.867
native), so P55 is retained as a neutral simplification, with no performance
gain claim. P52B's 48.782-second sample remains the best observed control.
Next test P56's immutable shared array subtype metadata; the under-15-second
target remains unmet. Candidate bytes and focused/wall evidence are archived under
`build/performance-campaign/mixed-long-fast-loop/p54-logic9-ordered-pending/`.
The elaboration-only Callgrind reached its 900-second cap before completion;
its [partial 188.245-billion-instruction checkpoint](../build/performance-campaign/mixed-long-fast-loop/p54-logic9-ordered-pending/elab-callgrind/timeout-result.json)
is attribution only. It completed no snapshot or simulation and created no
native objects. The leading partial self paths include scoped HIR lookup,
VHDL subtype copying, package resolution, and allocation; inclusive
evaluator paths overlap. Use current native elaboration samples to confirm a
source-level opportunity; the instrumented count is not a timing result.

P56 shares immutable VHDL array-element subtype metadata across constant-value
copies and charges its one allocation to the existing work budget. Its affected
Release build (two C++ compilations and six links; 778 dependency scans) and
six focused gates passed. The [single cold full-case result](../build/performance-campaign/mixed-long-fast-loop/p56-shared-subtype/wall/fast-loop-result.json)
matched all 75 summaries and 87 correctness lines: 48.429 seconds total,
including 16.019 elaboration and 31.113 native. Elaboration fell 1.355
seconds from P55's one sample and 1.406 seconds from P52B's. Native variation
is not attributed to P56. P56 is retained and is the lowest observed full
sample, still above the 15-second target. Next use a separately gated
elaboration diagnostic to locate repeated initializer and HIR lookup work.

P57 skips the generated-callable expression traversal when no generated
callable exists; duplicate and generic-instance validation still run. Its
affected Release build and four focused gates passed. One cold full-case run
matched all 75 summaries and 87 correctness lines at
[47.181 seconds](../build/performance-campaign/mixed-long-fast-loop/p57-generated-callable-empty-guard/wall/fast-loop-result.json)
(1.345 compile, 14.917 elaborate, 30.917 native), 1.102 seconds less
elaboration and 1.248 seconds less total than P56's single sample. P57 is
retained as the lowest observed full run. A separate elaboration-only profile
found fewer allocator samples while initializer CPU and replacement-lookup
counts stayed roughly flat; it did no simulation. Remaining resolver and
allocation paths need source proof before the next change. The full
under-15-second target remains open.

P58 kept three VHDL name parts inline with overflow for longer names and
removed a copy of direct unit lookup results. The affected Release build and
six focused tests passed. Its [one cold full-case run](../build/performance-campaign/mixed-long-fast-loop/p58-inline-vhdl-name-parts/wall/fast-loop-result.json)
matched 75 summaries and 87 correctness lines at 46.275 seconds (1.344
compile, 14.065 elaborate, 30.864 native). The 0.852-second elaboration
reduction against P57 is one observation; native was essentially flat. P58 is
retained as the lowest observed full run, still above the 15-second target.
A separate native-only diagnostic copied its verified elaborated snapshot,
cleared the native cache, and interpreted eleven large nonshareable process
bodies. They executed 17,835,229 interpreted operations with 75/87 correctness;
its instrumented duration is not a wall-time comparison. The
[result](../build/performance-campaign/mixed-long-fast-loop/p58-large-unshared-interpreted/native-profile/fast-loop-result.json)
and raw stderr retain the process IDs and counts for the next architecture
choice.

P59 indexes immutable VHDL declarations by scope and spelling for the common
resolver lookup, retaining stale-index and specialization-overlay fallbacks.
The affected Release build and seven focused checks passed; the new overlay
test was repaired to use a supported VHDL entity specialization. Its
[single cold full run](../build/performance-campaign/mixed-long-fast-loop/p59-vhdl-scope-name-index/wall/fast-loop-result.json)
matched 75 summaries and 87 correctness lines at 45.274 seconds (1.345
compile, 13.362 elaborate, 30.565 native). Elaborate fell 0.703 seconds
against P58's single run. P59 is retained; the full goal remains 30.274
seconds away. Historical P53 unresolved native samples were all in stripped
libLLVM.so.22.1, not an unknown JIT/simulation bucket.

The P59 native diagnostic found eight adaptive jobs whose nine processes
executed 11.918 million interpreted operations while the average-work gate
kept them interpreted. A forced, instrumented counterfactual compiled those
already-selected jobs, but its timing is not wall evidence. P60 added a
250,000-operation cumulative escape; it promoted the eight jobs and passed
75/87 correctness, yet its one cold full sample took 54.530 seconds, with
compile and elaboration also unexpectedly slower. That all-phase drift has no
proven host cause. P60 was rejected and its threshold restored. P61 waited
for all selected JIT jobs before a long run; its correct cold sample took
46.524 seconds versus P59's 45.274. P61 was rejected and its source restored.
P59 remains the lowest observed full sample. P62 returns the register base
pointer directly for zero-offset loads and stores, preserving nonzero GEPs and
using native cache schema v175. Its affected Release build and four focused
checks passed. The [one correct cold full run](../build/performance-campaign/mixed-long-fast-loop/p62-zero-register-gep/wall/fast-loop-result.json)
took 45.824 seconds (1.344 compile, 13.714 elaborate, 30.764 native), 0.550
seconds above P59. P62 is retained as a small emitter simplification, with no
speedup claim. A separate [external-only native profile](../build/performance-campaign/mixed-long-fast-loop/p62-external-native-profile/native-profile/external-analysis.json)
matched 75 summaries and 87 correctness lines: LLVM held 43.39% of 2,938 CPU
samples, fsim 36.26%, mapped JIT 11.74%, and libc 7.95%; no samples were lost.
Its 756 raw LLVM samples (25.73% of all samples) remain unresolved because the
matching local debug file is absent. Sampled call paths across the 86-KiB
static-cohort frame have incomplete ancestry, so their inclusive percentages
are not full scheduler ceilings. The
fresh [external-only elaboration profile](../build/performance-campaign/mixed-long-fast-loop/p62-external-elab-profile/elab-profile-external/external-analysis.json)
used the same 19 inputs and current ELF in a new workspace, stopping before
simulation with zero native objects. Its 1,307 samples had no loss or unresolved
symbols; top self paths were expression lookup (4.13%), malloc (3.90%),
package-member resolution (3.60%), free (3.06%), and memcmp (2.98%).
Inclusive elaboration paths overlap and cannot be added into a wall estimate.
The
[P60 decision](../build/performance-campaign/mixed-long-fast-loop/p60-adaptive-cumulative-work/decision.json)
and [P61 decision](../build/performance-campaign/mixed-long-fast-loop/p61-long-run-jit-barrier/decision.json)
retain their exact samples and source restoration. The next dependency is a
larger source-supported compiler, elaboration, or runtime change selected from
the current external profiles; no worker-count policy change is selected. Profile-only
register diagnostics counted 1,735 lowered
process rows, including 1,598 split-transient rows; concurrent stderr writes
had embedded nine rows mid-line in the initial parser.

P63 removed the static-cohort interpreter's 512 inline contexts and entries,
using its existing reusable vector path and nested-call fallback. Its affected
Release build and three focused tests passed. The ELF's cohort local stack
frame fell from 86,424 to 392 bytes. Its
[one correct cold full run](../build/performance-campaign/mixed-long-fast-loop/p63-vector-only-cohort/wall/fast-loop-result.json)
took 44.716 seconds (1.343 compile, 13.511 elaborate, 29.860 native), a new
lowest single observation, 0.558 seconds below P59. This is a simplification
with a favorable single sample, not a dispersion claim. The separate
[external-only native profile](../build/performance-campaign/mixed-long-fast-loop/p63-external-native-profile/native-profile/external-analysis.json)
matched 75/87 and restored the known scheduler ancestry: Scheduler::run
inclusive rose from 16.54% to 28.01% of sampled CPU, with the cohort now
nested beneath it. Inclusive paths still overlap and are not a speed estimate.
The full target remains 29.716 seconds away. A
[profile-gated HIR lowering census](../build/performance-campaign/mixed-long-fast-loop/p63-lowering-reuse-census/analysis.json)
then recorded 7,355 complete top-level VHDL lowering rows and 6.574 seconds
of summed entry thread CPU. Each strict observed specialization and hierarchy
key was unique. Omitting hierarchy path alone produced 3,145 later rows with
4.246 seconds of CPU, an unsafe upper bound: paths and binding maps can change
the lowered result. No lowering cache is approved from these counts. The next
change requires a source proof of a larger safe work reduction.

The [P65 bounded scheduler Callgrind window](../build/performance-campaign/mixed-long-fast-loop/p65-scheduler-callgrind/logs/analysis.json)
collected 22.611 billion instructions over the first 5% of simulated ticks on
the frozen P63 runtime with the P64 diagnostic disabled. The prewarm and
collected runs both exited at the requested tick; all 312 native cache objects
retained identical paths, sizes, and content hashes. Their timestamps advanced
because successful cache reads refresh the LRU time. The launcher treated that
timestamp change as a cache failure; its raw result is preserved and no run
was repeated. The window includes 135 lazy cohort wrapper compilations inside
`Scheduler::run`, so its instruction shares are not pure steady scheduler
cost. `PackedLogic4` methods account for 25.43% of this early window, but the
full P63 native CPU profile places `PackedLogic4::get` at only 0.85% self.
These diagnostics establish no wall-time saving. The broad P66 word-path
patch remains scratch-only. P67 restored P64's diagnostic-only HIR source,
removed the redundant checked-integer unknown guard, and added bounded LLVM
constant-plane forwarding for proven single-definition split transient slots.
The affected Release fsim build and focused LLVM/runtime tests pass, and the
manual source/test index is refreshed. A diagnostic native-only run matched
75 fingerprints and 87 correctness lines while forwarding 91,588 plane loads
and suppressing 21,450 false guards. The one correct cold full run took 44.222
seconds (1.345 compile, 13.613 elaborate, 29.261 native), 0.494 below P63's
best observation. Retain provisionally; the single run does not establish
dispersion, and the P59-to-P67 raw LLVM reduction includes intervening
changes. The target remains 29.222 seconds away. P62's external elaboration
profile remains applicable because the HIR source is restored byte-exactly.
P68 then compared eleven giant process bodies by serialized operation,
validated register layout, and mapped signal width/kind/resolution. It found
two compatible groups with four eliminable native bodies. P69 binds only
known narrow Logic4 literal differences through the existing candidate-local
callback for large nonrecurring O2 processes. It rejects nonblocking object
writes, keeps cache identities separate by binding mask, and prevents bound
literals from entering constant-based LLVM fusions. Focused LLVM and app
tests pass; the app fixture confirms two large container instances share one
module and retain distinct values. The diagnostic native run matched 75/87,
produced 308 rather than 312 objects, and counted 428 bound callbacks. The
one cold full run took 42.209 seconds (1.343 compile, 13.663 elaborate,
27.202 native), 2.012 below P67. Retain P69 with single-sample limits. The
under-15 goal remains open by 27.209 seconds. Use the P69 native profile and
still-applicable P62 external elaboration profile to select the next largest
absolute host-path opportunity.

P70 exits VHDL package-member resolution on an engaged empty current index,
after the existing lookup-unit validation. A fresh diagnostic elaboration
counted 8,163,191 such exits in 8,406,959 valid resolver calls. Focused
semantic, elaboration, and application checks pass. The application fixture
also proves that shared container callback errors name the actual failing
instance; its first `WaitFor` version could not share under the existing
whitelist, so the final test uses initialized signals. One correct cold full
run took 41.810 seconds (1.344 compile, 12.709 elaborate, 27.755 native),
0.399 below P69. The targeted elaboration phase fell 0.954 seconds; the
native segment rose 0.554 seconds in this single sample. Retain P70 and the
independent error-identity repair. The under-15 goal remains open by 26.810
seconds. Continue from the largest current host path with source and profile
evidence; P70's diagnostic counters are applicability evidence, not timing.

P74's diagnostic found 34,348,337 Logic9 slots rejected despite a proven
single `std_logic` owner. P75 admits that narrow direct route, retains written
masks across equal-value assignments and intervening blocking writes, and
flushes staged masks into the ordered generic queue when the route changes.
Its affected Release build and runtime/application checks pass. A fresh
native-only diagnostic consumed 20,632,218 of 21,896,425 active slots and
matched all 75 fingerprints and 87 correctness lines. One cold full run took
40.405 seconds (1.344 compile, 12.709 elaborate, 26.350 native), 1.406 below
P70, with the measured difference in native setup/simulation. Retain P75;
the under-15 goal remains open by 25.405 seconds. Use the current native
profile and still-applicable elaboration evidence to select the next largest
host path. The diagnostic run is not timing evidence.

P76's guarded VHDL concurrent process replay admits only equal scalar
generic identities and compatible bijective whole-signal formal bindings.
It preserves callable invocation numbering across later bodies. The focused
Release elaboration witness checks same/different generics, aliased inputs,
dynamic indexed insertion, and later ordinary callable debug identity. A
diagnostic full run matched 75 fingerprints and 87 correctness lines while
reusing 1,143 of 1,173 guarded target-family bodies. The single cold full
run took 37.495 seconds (1.343 compile, 8.946 elaborate, 27.204 native),
2.910 below P75. Targeted elaboration fell 3.763 seconds; the native segment
rose 0.854 seconds in one sample and is not attributed to this change.
Retain P76; the under-15 goal remains 22.495 seconds away. Evidence is in
the [P76 receipt](../build/performance-campaign/mixed-long-fast-loop/p76-concurrent-process-reuse/retention-receipt.json).

P77 is an artifact-only attribution of unchanged P75 native/compiler code:
per-TID `perf script` recovered 1,150 worker call chains absent from the
whole-process report, though its leaf symbols were resolved. The remaining
324 empty main-thread stacks match JIT leaf samples. The
[P77 receipt](../build/performance-campaign/mixed-long-fast-loop/p77-static-llvm-profile/profile-receipt.json)
records sampled LLVM pass costs and the reusable workflow limitation. No
new workload run or source change belongs to P77. Select the next candidate
from the current P76 elaboration profile and P77 native attribution.

P79's diagnostic constant-call census found cross-evaluator repeats, while
P80's read-only audit could not establish a safe shared-cache key or a
unit-owned hit count. Neither changed production code. P81 simplified the
disjoint Logic4/Logic9 bitwise result classes; the focused LLVM suite and
full diagnostic 75/87 parity passed. The single cold full run took 36.344
seconds (1.344 compile, 8.997 elaborate, 26.002 native), 1.150 below P76.
The raw native IR count fell 2,469 instructions across 308 modules; the
larger native wall difference is a single observation, not a claimed direct
effect of those instructions. Retain P81. The under-15 goal remains 21.344
seconds away. The [P81 receipt](../build/performance-campaign/mixed-long-fast-loop/p81-logic-aval-simplification/retention-receipt.json)
has the source, tests, profile comparison, and one cold wall result.

P82 caps LLVM materialization workers by Linux calling-thread CPU affinity,
retaining hardware-concurrency fallback on other hosts or query failure.
Focused app/LLVM checks and full diagnostic 75/87 parity pass. On CPU0, all
316 profiled JIT module rows used one worker; summed worker CPU fell 2.487
seconds and instrumented peak RSS fell 206,112 KiB. The single cold full
run took 35.292 seconds (1.344 compile, 8.949 elaborate, 24.997 native),
1.053 below P81, with cold peak RSS down 193,844 KiB. Retain P82; this is
one timing observation with separate profile support. The under-15 goal is
20.292 seconds away. The [P82 receipt](../build/performance-campaign/mixed-long-fast-loop/p82-affinity-jit-workers/retention-receipt.json)
holds the source and measurement details.

P84 keeps P82 source frozen and builds an isolated Release Clang 22 ThinLTO
host executable. Five focused semantic, elaboration, runtime, LLVM, and
application tests passed; the full diagnostic case matched 75 fingerprints
and 87 correctness lines. Its single cold full run took 34.385 seconds
(1.344 compile, 8.646 elaborate, 24.394 native), 0.906 below P82, with
308 cold native objects. Retain the build configuration; the one-sample
phase differences are not independently attributable to ThinLTO. The
under-15 goal remains 19.385 seconds away. Keep the canonical P82 binary
as the non-IPO control and use the P84 configuration for later source
candidates. The [P84 receipt](../build/performance-campaign/mixed-long-fast-loop/p84-thinlto-host-config/retention-receipt.json)
records exact build and validation evidence. The latest
[convergence audit](../build/performance-campaign/convergence-audit-20260928T0919Z.json)
records progress and the next shortest dependency: the P85 count-only
native-sharing census outside benchmark intervals. P85 is now complete:
the [artifact census](../build/performance-campaign/mixed-long-fast-loop/p85-native-sharing-audit/README.md)
compared all 1,731 compiled functions and found 80 compatible cross-owner
pairs. Only five complete singleton modules could be eliminated, with at
most 176 ms of instrumented compiler CPU. No production sharing change is
supported by this bound; rank the next measured host path.
The same count found 650 static Logic9 XOR sites across 182 compiled
functions and 83 modules; those modules account for 2.022 seconds of
instrumented compiler CPU in total. This supports a bounded stateless
lowering trial, not a claim that XOR itself costs that time.
After measurement, a build-local GNUgold ThinLTO cache was enabled to speed
future incremental host links. Its relinked `fsim` SHA-256 exactly matches
the measured P84 binary; no workload was rerun for this build-only change.

P86's Logic9 XOR lowering trial passed the exhaustive O0/O2 raw-state JIT
fixture and reduced optimized LLVM IR by 7,096 instructions. Its one cold
full run took 37.149 seconds, 2.764 above P84, while unaffected phases
also slowed. The architect rejected P86 without repeating timing; exact
P84 source/schema/tests and the measured binary SHA-256
`a4de1308400278c47bc7b4402a24784da9c8cbdfe34e93ba4b0f74754185431d`
are restored. The [rejection receipt](../build/performance-campaign/mixed-long-fast-loop/p86-logic9-xor-lowering/decision-receipt.json)
holds the proof and measurement. P84 remains the retained 34.385-second
checkpoint. P87's isolated host IR-PGO training matched the full 19-input,
75-fingerprint, 87-line case. The profile-use build passed focused semantic
and elaboration tests and the full diagnostic. Its one cold full run took
36.351 seconds: compile 1.447, elaborate 7.945, and native setup/simulation
26.958. Against P84, the total was 1.966 seconds slower; elaboration was
0.701 faster but native 2.564 slower in this single observation. Reject
P87 as a build configuration and keep P84's measured binary. Because this
was a configuration-only screen with unchanged source, runtime, LLVM, and
application test links were deferred until after timing and skipped when
P87 lost. The [P87 receipt](../build/performance-campaign/mixed-long-fast-loop/p87-host-pgo/decision-receipt.json)
preserves exact flags, training/profile identity, validation, and the
single-sample limit. Next is P88's existing synchronize-recurring flag
screen on the retained P84 binary, then the bounded scope-lookup candidate.
An incidental untreated P88 run on that same binary later took 36.546
seconds, 2.160 above the historical P84 sample. This exposes run-to-run
variation; P86 and P87 phase deltas should not be read as causal effects.

P88's first profile and wall were invalid as a forced-compilation test:
the scratch runner passed `FSIM_JIT_SYNCHRONIZE_RECURRING=1` only in its
native-profile branch. That untreated wall is preserved as incidental
control-like evidence. After fixing the runner's common environment, the
valid diagnostic compiled eight additional native modules (308 to 316),
removed 13.481 million adaptive interpreted operations, and matched all
19 inputs, 75 fingerprints, and 87 correctness lines. The one valid cold
full run took 38.352 seconds (1.396 compile, 9.349 elaborate, 27.605
native), 1.806 above the incidental untreated run and 3.967 above the
historical P84 sample. Reject forced synchronization for this workload;
keep P84's default execution policy and binary. The [P88 receipt](../build/performance-campaign/mixed-long-fast-loop/p88-sync-effective/decision-receipt.json)
records the treatment proof, invalid attempt, and single-sample limits.
The bounded checked scope-index source candidate is next.

P89 replaces a linear semantic scope search with checked zero-based indexing
under `Model::valid`'s existing scope-ID invariant. It adds no cache, state,
or ABI. An initial local vector-copy error failed elaboration testing; the
saved P84 source passed the same test, and binding `scopes` by reference
restored focused semantic/elaboration 2/2 PASS before profiling or timing.
The full diagnostic matched 19/75/87. Its one cold wall took 36.440
seconds, 0.105 below the recent incidental untreated run but 2.055 above
historical P84; no speedup is attributable amid observed drift. Retain P89
solely as a bounded performance-neutral source simplification under the
user's rule. The P84 ThinLTO configuration remains preferred, and its
34.385-second wall remains the lowest observed sample. The [P89 receipt](../build/performance-campaign/mixed-long-fast-loop/p89-scope-index-scout/retention-receipt.json)
preserves the baseline, repair, tests, profile, and cold wall. The latest
[hourly audit](../build/performance-campaign/convergence-audit-20260928T1024Z.json)
records the active gap; the next source candidate awaits current host-path
ranking and a bounded initializer-key audit. The [P89 diagnostic ranking](../build/performance-campaign/mixed-long-fast-loop/p89-scope-index-scout/profile-ranking.json)
places the remaining cold wall mostly in native setup/simulation (26.399
seconds), with 308 LLVM modules taking 5.904 seconds summed optimization
CPU in the instrumented run. Native sampling splits 2,322 disjoint self
samples among LLVM 897, fsim host 908, JIT 324, and libc 181; 515 leaf
samples remain unresolved, mainly LLVM. These counts are diagnostic and
do not establish removable wall cost.

P91 used an artifact-only Callgrind harness on the unchanged P89 design and
binary: native instrumentation stayed off through tick 15,122,750, covered
the second five percent through tick 30,245,500, then stayed off to normal
completion at tick 302,455,000. All 75 summaries and 87 correctness lines
matched exactly. The 15.082 billion instrumented instructions split into
8.955 billion fsim host, 4.913 billion JIT process, and 1.150 billion libc
self instructions. Four GF multiplier processes account for 3.251 billion
second-window JIT instructions, but only 63 of 2,322 full-run native CPU
samples; all six equal-shape GF modules have 65 samples (about 0.66 sampled
CPU seconds) and 231 ms summed optimization CPU. This does not support a
complex GF-only specialization. The [P91 report](../build/performance-campaign/mixed-long-fast-loop/p91-second-window-callgrind/report.json)
and [arcs](../build/performance-campaign/mixed-long-fast-loop/p91-second-window-callgrind/arc-analysis.json)
are diagnostic instruction evidence, not wall-time savings.

P94 then measured exact cross-evaluator initializer reuse without changing
production behavior. Its [analysis](../build/performance-campaign/mixed-long-fast-loop/p94-initializer-exact-census/analysis.json)
contains 353 successful top-level calls in four hot families, 177 exact
overlay/selected-generate classes, and 176 repeat calls with 1.193 seconds
of instrumented inclusive CPU. All repeated classes had equal typed results
and work deltas, but 32 classes varied the incoming work counter. A cache
must preserve the existing work budget and failure behavior. The final run
reused compiled libraries after two incomplete unit-ID-gated attempts;
numeric caller unit IDs changed across fresh workspaces. The temporary
semantic source and retained P89 ThinLTO executable were restored to exact
pre-diagnostic SHA-256 values; see the [receipt](../build/performance-campaign/mixed-long-fast-loop/p94-initializer-exact-census/receipt.json).
No cold wall was taken for P94. P96 subsequently implemented the bounded
memo under per-validation ownership. Its profile observed 205 hits and 215
admitted entries; a single cold full wall passed 19/75/87 parity in 32.579
seconds (compile 1.343, elaborate 7.341, native setup and run 23.892).
Elaboration fell 1.355 seconds against P89. Do not attribute the native
delta between single samples to the semantic memo. Focused semantic and
elaboration checks passed 2/2. The 4096-entry cap does not strictly cap
retained array bytes, though this case showed no material RSS increase.
Broad Release, Debug, and Tcl-off qualification remains pending; the
under-15-second objective is still open. The [P96 receipt](../build/performance-campaign/mixed-long-fast-loop/p96-exact-initializer-memo/retention-receipt.json)
records exact source, binary, and run identities. The latest
[convergence audit](../build/performance-campaign/convergence-audit-20260928T1124Z.json)
precedes this result; next rank remaining native compilation and runtime
costs from P96's fresh profile.
The [P96 ranking](../build/performance-campaign/mixed-long-fast-loop/p96-exact-initializer-memo/profile-ranking.json)
shows native setup/simulation 23.892 seconds, including 4.879 seconds of
summed LLVM module optimization CPU over 308 modules (InstCombine 2.918).
Scheduler/update self samples are diffuse; select the next change only
after an aggregate cost and applicability proof.

P97's [64 KiB native-only diagnostic](../build/performance-campaign/mixed-long-fast-loop/p97-simulation-attribution/report.json)
passed exact P96 19/75/87 parity without a source edit or cold wall run.
The 13.479-second instrumented interpreter interval overlaps adaptive LLVM
compilation on CPU 0. Recovered Scheduler callers split 603 samples into
static-cohort 262, update-commit 144, single-execute 140, and other/self
57; 294 JIT leaves have no caller frames. The compiler worker active during
simulation spent about two sampled CPU seconds, but eligible adaptive
modules later served 697,739 native resumes. P88/P92/P95 rule out the
simple force-all, packed insert, and cohort cleanup paths. Next examine
aggregate LLVM lowering and code generation; no runtime microtrial is
authorized by this attribution alone.

P98's [IR diagnostic](../build/performance-campaign/mixed-long-fast-loop/p98-giant-call-ir/report.json)
captured current giant process 4075 with 19/75/87 parity. Offline alias
scopes between app register vectors cut optimized IR by 8,017 instructions,
yet the exact IR pass sequence became slower and O0/FastISel emission CPU
was unchanged at 0.19 seconds. No generic JIT frame alias guarantee was
proved. Reject this candidate; no production edit or cold wall followed.
Per-register lifetime promotion remains a separate unproved boundary.

P99's [count-only register census](../build/performance-campaign/mixed-long-fast-loop/p99-register-lifetime-census/report.json)
found zero candidates with a whole-process Fork exclusion and only 641
single-use narrow registers under a more optimistic Fork-interval rule.
Potentially failing operations make that count an upper bound, and public
JIT Pause semantics still require visible frame registers. No production
change or wall run followed. P100 then tested the payoff of skipping seven
giant native modules by interpreting every member of their 11 original
process bodies. The [receipt](../build/performance-campaign/mixed-long-fast-loop/p100-giant-admission-screen/rejection-receipt.json)
records exact 19/75/87 parity, 308 to 301 cold native objects, 17.835
million extra interpreted operations, and one cold wall of 39.153 seconds
versus retained P96's 32.579. Reject the 6.574-second observed regression.
The P96 app source and measured ThinLTO executable are restored byte-exactly;
the one build object and archive compiled from diagnostic source must be
rebuilt before future qualification. The under-15-second goal remains open.
P101's [count-only opcode diagnostic](../build/performance-campaign/mixed-long-fast-loop/p101-vm-opcode-census/diagnostic-receipt.json)
passed 19/75/87 parity and 308 cold native objects. All 31.531 million
counted leaf opcodes reconcile with the existing interpreted-process rows,
including eight separately tracked owners. DebugPoint 5.358 million and Jump
2.883 million are frequent, but the saved Callgrind `handle_boundary` self
cost is only 0.88% of its steady window across all boundaries. Frequency does
not prove a one-second fast path. The diagnostic runtime source and measured
P96 binary were restored byte-exactly; rebuild the diagnostic objects before
future qualification. No P101 wall or production optimization occurred.
The [12:24 convergence audit](../build/performance-campaign/convergence-audit-20260928T1224Z.json)
records P99 as the shortest pending diagnostic before this rejected screen;
the next audit should record P100's measured regression and restored state.

**Current performance checkpoint, 2026-09-27 21:15 UTC.** P37 is retained;
its frozen CLI is
`fa610df375a14affec578cd53aa4c5f7d5eedff1f671053cfc2a9258d3e81bd1`.
The [current full-throughput profile](../build/performance-campaign/p37-hot-execution-metadata/current-profile-architect-verification.json)
passed correctness and identity checks: native simulation had 16,254 CPU
samples, 106 unresolved (all in libLLVM), and no lost records. The
[instruction attribution](../build/performance-campaign/p37-hot-execution-metadata/boundary-attribution-shift-architect-evidence.json)
places a sampled ceiling of about 6.76% of native self time at the cold
operation-extent read in the external-boundary check; this is not a predicted
wall-time saving. P38 tested an exact immutable `size_t` extent in the hot
process record. Its [four-file patch](../build/performance-campaign/p38-operation-extent/candidate.patch),
affected Release build, and four focused tests passed. Two opposite-order
reduced pairs tentatively saved 3.661141 and 1.952271 seconds end to end,
but their round times drifted. The [full audit](../build/performance-campaign/p38-operation-extent/full-architect-verification.json)
passed all canonical, final-summary, correctness, identity and cold-cache
gates for the original throughput workload. Full fsim totals saved only
0.737246 seconds in the first order and regressed 6.632113 seconds in the
reverse; the candidate median was 2.947433 seconds (1.62%) slower, with
higher RSS in both pairs. The [rejection decision](../build/performance-campaign/p38-operation-extent/rejection-decision.json)
does not treat the added extent state as a performance-neutral simplification.
Only P38's four source files were restored to their recorded P37 bytes; the
affected Release rebuild returned the configured CLI to the frozen P37 SHA
above, with the same 22 physical dependencies and a passing runtime sanity
test. All P38 raw evidence is preserved. The
[20:58 convergence audit](../build/performance-campaign/convergence-audit-20260927T2058Z.json)
records the bounded iteration. No reference case meets the former all-ten
seven-pair qualification goal. The user's subsequent full mixed-codec
direction above supersedes the P38 pause.

**Earlier P35–P37 checkpoint.** P35's authoritative
disjoint-driver composite is retained on the pushed `220e0c05` LLVM-capability
baseline. The frozen candidate CLI is
`64e37033496fabc060f56843fce7d3668b95f00d69ad8cd3a2e37581d6c3ee8c`;
the frozen control is
`a77caa06446039a6f441a1cbe5ac58b90bc830a94b92eeda3d48611ce6e2ed2e`.
The [mechanism audit](../build/performance-campaign/p35-disjoint-driver-composite/mechanism-architect-verification.json)
confirmed 24,412,354 owned-composite updates, 95.89% of accepted reduced
slots, with exact cross-simulator parity. Two opposite-order cold reduced
pairs saved 3.310587 and 2.355714 seconds end to end. The
[full audit](../build/performance-campaign/p35-disjoint-driver-composite/full-architect-verification.json)
confirmed the original six-instance, two-mode throughput workload: 128,878
preflight event records and six summaries match fsim/Vivado, and all timed
final summaries match preflight. Full fsim control/candidate times were
183.255470/158.545673 and 212.012600/178.862778 seconds in opposite
orders, saving 24.709797 and 33.149822 seconds; the median reduction is
28.929810 seconds (14.638%). Native phases show the same direction. Full
Vivado moved 0.001761 and 1.256913 seconds against the candidate; fsim RSS
was 985,148/989,808 and 983,696/986,948 KiB. This is retained engineering
evidence, not the required seven-pair or all-ten qualification; fsim remains
slower than Vivado on this case. The first full control launcher's inherited
reduced correctness count rejected its otherwise valid sample; the original
false receipt and separate [analysis-only recovery](../build/performance-campaign/p35-disjoint-driver-composite/full-control-v1-analysis-recovery.json)
are both preserved. The [retention decision](../build/performance-campaign/p35-disjoint-driver-composite/retention-decision.json)
keeps P35 while the next opportunity is ranked. The [current P35 profiles](../build/performance-campaign/p35-disjoint-driver-composite/current-elf-profile-analysis.json)
passed correctness and frozen-identity checks. Original throughput simulation
had 15,565 CPU samples, 108 unresolved (0.69%) and no lost records;
exclusive self costs remain high in native resume (11.15%), static cohort
execution (8.06%), cohort resume (6.39%) and scheduler queueing (5.27%).
Inclusive call paths overlap and cannot be added. Mixed-codec elaboration
had 3,962 samples and two unresolved; subtype evaluation (36.88%) and
package-member resolution (29.48%) are overlapping inclusive paths. Its
native profile had 746/4,121 unresolved samples in libLLVM, limiting
symbol-level conclusions there. These are instrumented attribution, not
timing evidence.

P36 then counted actual cohort readiness without changing dispatch. The
[count audit](../build/performance-campaign/p36-cohort-readiness/count-architect-verification.json)
verified six final summaries and physical identities. All 3,104,730 ready
spans contained their full registered membership (124,664,605 member
entries), with no partial or mixed cohorts. Adapters consumed 121,949,776
members in 3,451,363 full-prefix calls, all reaching static wait; the
remaining 2,714,829 members used existing singleton routes. Registered
cohorts of 64–127 members contributed 97,233,024 ready members (78.0%).
Three source-verified 64-member probes had identical ten-operation CFGs
and started at PC 9, but together represent only 4.46% of adapter members;
a reusable structural-family benefit still needs proof. All 192 selected
processes have null `program_owner`; the scratch CFG reader's owner walk
must not be generalized to other processes. The temporary
source was restored byte-for-byte; the configured CLI again equals the
frozen P35 SHA above, and the source was manually reindexed. P36 counts and
elapsed time are instrumented evidence only. The source-family review chose a
narrower P37 metadata candidate without specializing any benchmark key. A
[static join](../build/performance-campaign/p36-cohort-readiness/current-64-cohort-static-shape.json)
found 540 of 541 current 64-member cohorts share the probes' declared
ten-operation shape; complete CFG eligibility and guarded savings remain
unproved. P37 is a bounded hot-execution-metadata candidate against frozen
P35, with a [reviewable patch](../build/performance-campaign/p37-hot-execution-metadata/candidate.patch)
and passing affected Release build and seven focused CTests. It moves the
authoritative timeout-origin optional into the hot process record, keeps a
source-certified callable-frame admission fact, reads the JIT's validated
operation bound once at executor construction, and avoids disabled-profile
argument loads. The hot record measures 64 to 72 bytes while its cold
sidecar shrinks 1584 to 1576 bytes. The frozen P37 CLI is
`fa610df375a14affec578cd53aa4c5f7d5eedff1f671053cfc2a9258d3e81bd1`.
The [reduced audit](../build/performance-campaign/p37-hot-execution-metadata/reduced-architect-verification.json)
and [full audit](../build/performance-campaign/p37-hot-execution-metadata/full-architect-verification.json)
verified opposite-order cold control/candidate pairs with equal complete
canonical transcripts, correctness, inputs, executable/dependency identities
and empty starting native caches. Full original-throughput fsim totals were
170.535054/168.019559 and 171.552432/167.972414 seconds, saving
2.515495 and 3.580018 seconds; the median saving was 3.047757 seconds
(1.782%). Native setup/simulation saved 2.516139 and 3.429941 seconds.
P37 is [retained](../build/performance-campaign/p37-hot-execution-metadata/retention-decision.json)
on this evidence, while fsim remains slower than Vivado and no reference
case meets seven-pair qualification. Fsim peak RSS moved from
985,628 to 986,824 KiB in the first pair and from 988,464 to 987,436 KiB
in the reverse pair; the direction is mixed. The next evidence gate at that
checkpoint was a current-ELF CPU profile before another source change. The
[19:58 convergence audit](../build/performance-campaign/convergence-audit-20260927T1958Z.json)
records the retained result and then-current profiling dependency. P35 and
P37 remain dirty and
unpushed. Hosted Ubuntu Release and
Debug passed; Windows Debug and Release reported only Tcl-console
`argc`/`argv` warnings. A narrow `[[maybe_unused]]` repair passes its local
Release build and focused CTest; hosted confirmation remains pending. No
reference case is fully qualified.

**Earlier LLVM closure checkpoint.** The user asked to close
valid LLVM implementation holes that currently trigger explicit rejection.
The [bounded scope](../build/performance-campaign/llvm-capability-closure/architect-scope.json)
covers `ScopeRandomize`, supported wide-value shapes, and finite control-flow
cycles without a suspension point. Malformed SimIR and finite ABI capacity
checks remain errors; LLVM cost-selection policy is separate. The startup
compilation tier is already awaited before simulation; optional background
compilation remains distinct from an unsupported process. Both reviewed
[implementation batches](../build/performance-campaign/llvm-capability-closure/implementation-sequence.json)
are integrated, including wide waveforms, attributes, containers,
FormatDisplay, file binary reads/scans, ScopeRandomize, and finite native
control cycles. The full Release build passed; its 454-case suite initially
passed 449, then repaired catalog-count and newly native container-test
expectations passed with their dependencies (30/30). Affected Debug and
Tcl-off LLVM builds and 14 focused tests in each lane pass. Release CLI
SHA-256 is
`a77caa06446039a6f441a1cbe5ac58b90bc830a94b92eeda3d48611ce6e2ed2e`.
One deterministic reduced `mixed_codec` O2 cross-simulator preflight outside
the sandbox also passed: fsim and Vivado produced the same 48 canonical
streams, 836 event records, and seven summaries. The
[receipt](../build/performance-campaign/llvm-capability-closure/final-parity-result.json)
records matching transcript and frozen binary/dependency hashes, with zero
timed results. The qualified closure was committed and pushed as `220e0c05`
on `codex/v3`. This is functional closure, with no speedup or hosted Windows
claim. P35 was then started against that committed capability baseline; its initial
[scaffold](../build/performance-campaign/p35-disjoint-driver-composite/p35-header-scaffold.patch)
is historical. The authoritative disjoint-driver composite prototype passed
an affected Release `-j12` build and five focused runtime/LLVM/application
checks before timing. Baseline witnesses
pin early native callbacks, queued-over-native overlap in both API orders,
and later raw-new/stored-old publication. P30 remains the frozen historical
pre-closure performance control. The current convergence receipt is
[`20260927T1800Z`](../build/performance-campaign/convergence-audit-20260927T1800Z.json).
P35 must follow the [corrected update-order boundary](../build/performance-campaign/p35-disjoint-driver-composite/update-order-architecture-refinement.json)
and its [audit addendum](../build/performance-campaign/p35-disjoint-driver-composite/update-order-audit-correction-1.json):
early native direct-word callbacks precede shared raw-driver commits, and
queued writes replay after native shared staging, winning overlapping bits
regardless of API submission order. The old P35 source snapshot is historical.

**Earlier P30 performance checkpoint, 2026-09-27.** P30's guarded VHDL primary-unit
relation index is retained for repeated full original `mixed_codec`
elaboration savings of 2.910066 and 3.958970 seconds in two opposite-order
pairs. Frozen P30 SHA-256 is `4d980c0df526e3527e3cd7ebf1b7fa1038773e454f9620491bb41e410895bc45`;
P24 remains a frozen fallback. Neither a statistical total-time gain nor any
of the ten required seven-pair qualifications is established. Separate P31
profiles of full original `mixed_codec` and `original_throughput` now pass
strict preflight parity and final-summary checks. Mixed elaboration samples
cluster in subtype evaluation and package/declaration lookup; mixed native
sampling includes substantial concurrent LLVM work, while long throughput
has large resume, cohort, update and scheduler-dispatch self costs. Native
update counters record 218,409,742 long-throughput slots, of which
165,773,756 take an unchanged route. P32 loaded the actual saved design and
joined one full-shape, instrumented count to its driver metadata. All
210,885,738 resolved or unchanged-resolved slots (96.56%) belong to 9,258
`sv_wire` signals whose separate process writers own disjoint, fully covered
bit ranges; no such slot belongs to a genuinely overlapping or unknown
writer set. Of those slots, 108,866,836 are on signals at most 64 bits and
102,018,902 are on wider signals. This confirms the user's per-bit
single-driver premise for the observed route, but neither instrumented counts
nor the current route label establish removable work or timed savings. P32's
temporary source was restored exactly, the configured CLI rebuilt to frozen
P30 bytes, and canonical source coverage refreshed. P33's bounded raw
owned-span cache reached all 19,135,652 active unchanged-resolved slots in a
separate reduced diagnostic, with exact fsim/Vivado transcript parity.
Two opposite-order uninstrumented reduced pairs then disagreed: P33 was
1.559105 seconds slower in control-first order and 0.455488 seconds faster
in candidate-first order. Vivado moved about 0.201 seconds in P33's favor
in each pair. No repeatable total gain supports its maintained cache, so
P33 is deferred without a full transfer. Its five production files were
restored byte-for-byte to P30, while the control-compatible 80-bit
cross-word/raw-versus-visible native-update test remains. The affected
`-j12` build and four focused CTests pass; the configured CLI again matches
frozen P30 SHA-256. Manual restoration reindex completed at 13:31:17 UTC.
P34 completed that bounded attribution without a production edit. Its
saved-artifact reader covers 85,397 throughput and 9,172 mixed-codec static
processes. One separate full-throughput simulate-only count exactly matched
P31's six final summaries and reconciled 236,550,074 native member resumes:
114,600,298 single and 121,949,776 cohort, across 3,451,363 cohort calls.
The aggregate mean is 35.33 consumed members per call; per-call ready-set
distribution is unknown. Of cohort member resumes, 97,135,072 (79.7%) are
in 541 keys with 64–127 registered members. All 49,700 active saved-process
rows match name/sensitivity/operation counts; 12 fork-created IDs remain
separate. Declared opcode histograms are not executed-opcode counts, and the
200-second instrumented run is not a timing comparison. See the
[P34 join](../build/performance-campaign/p34-cohort-shape-attribution/cohort-shape-join.json)
and [audit](../build/performance-campaign/p34-cohort-shape-attribution/final-audit.json).
The [design-specific scheduling assessment](design-specific-jit-scheduling.md)
adds a staged opportunity to the ranking: source-proven hot-cohort dispatch
and completion, then generated updates/fanout coordinated with P35, with
fusion considered only after cold gains. P34 shows high cohort frequency,
but readiness distribution and eligibility remain unproven; no new
implementation is selected from this assessment.
P35 was subsequently implemented and retained after the user-requested
LLVM capability closure. The
[P33 architect decision](../build/performance-campaign/p33-owned-driver-projection/architect-rejection.json)
preserves both opposite-order pairs without a statistical regression claim.
The user has already authorized continued iteration; this is an internal
evidence gate. For every campaign run, execute both simulators outside the
sandbox with the same environment and record
`sandbox_permissions=require_escalated`; sandboxed Vivado xsim can fail at
Tcl startup before HDL executes. Runner exit 2 with
`performance_gate_failed` is an objective failure, not by itself a parity
or execution failure. Reuse the [tested P33 reduced-leg launcher](../build/performance-campaign/p33-owned-driver-projection/run_reduced_leg.py)
and inspect
the complete report. The user's mixed-codec elaboration priority preceded
this update and scheduler work. See the
[P31 profile](performance-campaign.md#p31-current-phase-and-update-attribution),
[P32 ownership readout](performance-campaign.md#p32-owned-bit-driver-topology-and-route-counts),
and [P33 trial](performance-campaign.md#p33-raw-owned-span-projection-trial).

**Earlier performance checkpoint, 2026-09-26.** The active goal remains
closure of the performance gap for all ten cases. P18 is retained after its
candidate-first reverse full-throughput pair (P18 220.079873 seconds versus
P16 228.926758 seconds, while Vivado moved in the opposite direction) and
exact full original-codec parity. Both full pairs passed transcript,
correctness, executable/dependency identity and cold-cache audits. See the
[P18 retention and combined trial](performance-campaign.md#p18-retention-and-combined-p20p21-trial).
The combined P20/P21 trial passed its unchanged-P18 test-addition check,
twelve-worker affected Release build, 11 focused CTests and manual index.
Two opposite-order reduced cold pairs were slower than retained P18 by
1.119354 and 1.803863 seconds; Vivado drift does not reverse the direction.
The architect rejected only P21's production index sort and added vector
state; its useful ordering test remains. P19's reviewed array-path deletion
and P20 process-reference reuse then passed a twelve-worker build, 11 focused
checks and two opposite-order reduced pairs. The reverse reduced saving was
only 0.209239 seconds. One full original-throughput pair found P19/P20
3.151034 seconds slower than retained P18 while Vivado moved in the opposite
direction. Full transcript, correctness, identity and cold-cache checks pass;
one pair does not prove a statistical regression, but neutral non-regression
is unestablished. The architect deferred P19/P20 without per-change
attribution or further codec/profile work. Only its production hunks are
restored to recorded P18 bytes; both added tests remain. The affected Release
rebuild reproduces frozen P18 exactly and the focused runtime CTest passes.
The restoration was manually reindexed at 20:52:01 UTC. One cold full original
mixed-codec phase refresh then passed parity and identity checks: fsim took
95.085779 seconds versus Vivado's 28.754060 seconds. Fsim's excess was
25.545905 seconds in elaboration and 41.038745 seconds in native setup and
simulation. This is one diagnostic sample, not a qualification or speedup
claim. The subsequently user-authorized separate P18 CPU profile completed
with strict full preflight reuse and no timed pair. Elaboration had 4,413
exclusive samples, led by VHDL primary-unit/package lookup and specialized
declaration lookup. Native setup/simulation had 4,734 samples; 1,595 were
in LLVM, with 889 unresolved overall. Runtime `PackedLogic4::get`/`set`
took 543 disjoint samples, including interpreted division call paths.
Instrumentation and overlapping worker execution prevent a wall-time saving
claim. The user paused after that report and subsequently resumed the active
all-ten optimization goal. Automatic approval review
initially rejected this separate launch after the earlier pause request;
the user's later explicit approval resolved that block. See the
[current evidence](performance-campaign.md#p18-full-mixed-codec-phase-refresh-and-cpu-profile).
The historical [P18 pause checkpoint](performance-campaign.md#p18-measurement-round-and-pause)
records its earlier checks and then-pending decisions. P19 was reviewed
as scratch at that checkpoint and is now deferred after a bounded trial. None of
the ten reference cases has seven-pair qualification.

P23A's one-pass known-division change is provisionally retained as a
cache-free source simplification; frozen P18 remains the fallback. The
reduced mixed-codec pair favored P23A by 0.208666 seconds, but two
opposite-order full original mixed-codec pairs conflicted. All full
transcript, correctness, identity and cold-cache gates passed; neither
speedup nor non-regression is established. P24 then moved private design
identity into existing cold process state, producing an observed 72-to-64-byte
hot stride and eight rather than seven deque records per block. Its affected
Release build, `fsim.runtime` and `fsim.llvm` checks pass. A reduced
original-throughput pair favored P24 by 2.757221 seconds, but two full
opposite-order pairs conflicted: candidate-first P24 was 14.681473 seconds
slower and control-first P24 was 9.027463 seconds faster. Both full pairs
passed 16-input, 156-stream, 128,878-event transcript, correctness, identity
and cold-cache gates. Two-pair fsim medians are P23A 210.310637 and P24
213.137641 seconds; these do not establish a gain or non-regression. The
architect provisionally retained P24 as a bounded field-relocation
simplification, preserving frozen P23A and P18 fallbacks. No third full pair
or profile is authorized. No reference case has seven-pair qualification.

P25's temporary exact-input diagnostic is complete. It restored P17's
successful-resume snapshots on P24, admitted bounded forward branches and
conservatively merged static Extract ranges without changing scheduling or
publication. The old publisher-notification plan is superseded. The affected
Release build, focused runtime/LLVM checks and manual index passed. Reduced
and separately approved full original-throughput `--preflight-only` runs both
passed complete fsim/Vivado transcript parity. The reduced run recorded
9,877,000 generated `gen_reduce` native completed waits and 8,637,495
repeated exact selected inputs; the full run recorded 83,154,246 and
72,681,776 respectively. Full-classified totals were 200,928,315 successful
waits and 120,631,090 repeated inputs across 49,057 plans; all 85,409
registered processes were examined with no cap overflow. These are completed
entry snapshots, not transition/fanout counts, suppression proof or timing
gain. No timed pair was run. P25's six source/test files were restored to exact
pre-diagnostic P24 bytes, and the affected rebuild reproduced the frozen P24
ELF; both focused CTests passed. See the
[P25 evidence](performance-campaign.md#p25-completed-resume-read-range-diagnostic).
The latest hourly convergence audit is
`convergence-audit-20260927T1307Z.json`; no all-ten qualification is
demonstrated. P26's default-off array sensitivity trial passed focused checks
after source-certified parameterized-container and normal-CLI coverage-service
repairs. A real saved GF artifact carried ranges on all 21 M8 generated
reduction assignments; interpreter/O0/O2 outputs matched the archived
reference, and M8 native completions fell from 567 to 108 in both compiled
configurations. One cold reduced original-throughput pair passed complete
fsim/Vivado transcript, correctness, identity and cache checks, but P26's raw
fsim saving was 1.600907 seconds, below the preapproved two-second transfer
gate. Vivado improved by 0.852551 seconds in the same direction; the
fsim/Vivado speedup ratio moved from 0.430631 to 0.424127. The architect
[deferred P26](../build/performance-campaign/p26-static-array-sensitivity/architect-reduced-audit.json)
without a full pair or retention. The 14 P26 source/test files have been
restored to recorded P24 hashes; the affected Release rebuild reproduces
frozen P24 SHA-256 `66f691a707b9b0ce84cf0dbaab3675713ea5892c1d47f507dffa87f4a49d5965`
and all five focused CTests pass. Frozen P26 evidence remains under
`build/performance-campaign/p26-static-array-sensitivity/`. The manual
canonical restoration index is complete at 01:31:26 UTC; all 17 restored or
updated paths matched its recorded metadata. P27's exact local package-member
span hoist passed root and independent review, an affected twelve-worker
Release build, eight focused CTests, a five-path manual index and frozen
dependency checks. Its one reduced mixed-codec collection passed canonical
parity and cold-cache gates: P24 took 4.329837 seconds, P27 4.332550 seconds,
while Vivado took 7.944026 and 7.941027 seconds. P27's candidate identity
incorrectly used `baseline_commit` for its parent revision, so the runner
labeled it `frozen_baseline` and the architect did not accept a performance
comparison. Only the resolver is restored to exact P24 bytes; the semantic
witness remains. The affected rebuild reproduces frozen P24 and all eight
focused CTests pass. No P27 gain, neutrality, full transfer or retention is
claimed. P26's raw candidate receipt has the same known provenance-label
limitation; its failed two-second gate and deferral remain unchanged. P26 is
not retained as a neutral simplification because its new metadata, runtime
behavior and artifact schema add complexity. The loop
lesson is to inspect real saved metadata and normal CLI services before
timing a source-sensitive mode: a pure unit fixture missed both boundaries.
For candidate freezes, validate both dependency layouts with the runner's
reader, keep the parent revision out of `baseline_commit`, and assert the
runner labels the candidate correctly before simulator launch. The architect
independently closed P27 restoration in
`p27-vhdl-package-span-hoist/architect-restoration-audit.json`. P28's one
cold reduced mixed-codec off/on pair on the unchanged frozen P24 ELF passed
parity and identity checks. The off and on fsim totals were 4.431417 and
4.432714 seconds; enabling recurring-JIT synchronization also increased the
native phase by 0.050404 seconds. It missed the preapproved 0.2-second
saving gate, so P28 is deferred without a full run or source change. P29 now
tests one guarded machine-word path in known packed division, with the
existing wider and Logic9 fallbacks preserved. The exact two-file patch and
one local test-helper type repair passed the affected Release build and four
focused CTests. The manual canonical index matches all five changed paths,
and the candidate ELF and 22 dependencies are frozen and verified. Automatic
approval review rejected the attempted outside-sandbox reduced benchmark
launch before any simulator process or output directory was created. It said
the benchmark was unrelated to the earlier authorized Callgrind profile and
that the user had instructed a pause afterward. The user has now explicitly
resolved that conflict: “Do not pause. Continue iteration on your own.”
The frozen source, ELF, 22 dependencies and launcher were reverified. The
reduced P24/P29 mixed-codec pair then passed parity and identity checks;
fsim took 4.430014 and 4.330649 seconds respectively, while native time
was effectively unchanged. Its predeclared gross non-regression gate opened
one full original mixed-codec pair. P29 took 89.905813 seconds and P24
100.551358 seconds, but contemporaneous Vivado also moved from 34.102038
to 28.118480 seconds in the candidate-first leg. Both pairs passed full
transcript, correctness, frozen identity and cold-cache checks; the full
pair does not isolate a P29 gain. The architect deferred the P29 production
fast path without a reverse pair or profile. Only the 13-line production
hunk was restored to exact P24 bytes; its runtime regression remains.
The combined P30 affected Release rebuild has now removed that fast path;
its ten focused CTests pass, including runtime and LLVM checks.
The user now prioritizes **full mixed-codec elaboration first**, followed
by update and scheduler paths. In the fresh full pair, fsim elaboration was
45.459693 seconds against Vivado's 19.529261 seconds in the candidate leg.
P30's source-reviewed VHDL primary-unit relation index is retained. Its four
live source/test files match the reviewed hashes; the combined eight-target
Release build, ten focused CTests, manual index and frozen identities pass.
One reduced and two opposite-order full original `mixed_codec` pairs pass
complete parity, correctness, 19-input and 22-dependency identity, and cold
cache checks. Full elaboration saves 2.910066 and 3.958970 seconds; raw total
saves 1.700095 and 4.310114 seconds. Native setup/simulation changes by
+1.210618 and -0.350672 seconds, while Vivado is about 0.7 seconds faster in
each P30 leg. The two-pair median fsim elaboration is P24 44.134219 versus
P30 40.699701 seconds. The architect retains P30 for repeated elaboration
improvement, with frozen P24 available as fallback. This does not establish
a statistical end-to-end gain, native neutrality or any of the ten required
reference qualifications. See
`p30-vhdl-primary-unit-index/architect-retention-decision.json` and its two
independent full-pair audits under the campaign evidence root.

The user's standing course is to select future production optimizations from
current phase and call-path profiles plus real path frequency, cost and route
counts, ranked by credible absolute end-to-end saving. Use bounded Callgrind
when sampling cannot resolve a material cost, and keep instrumented evidence
separate from timed comparisons. The current P31 collections are complete;
no additional trial or source change has been selected from them yet.

The later user-requested [Callgrind diagnostic](performance-campaign.md#callgrind-diagnostic-of-the-long-throughput-case)
was completed while the optimization goal was paused. The unchanged long
Verilog throughput case reached the exact 5% time cutoff; full compilation
and elaboration were profiled, with simulation split into pre-scheduler setup
and a 5–10 microsecond scheduler interval. The latter records 66.906 billion
instructions: process lookup 8.68% self, cohort execution 5.56%, native resume
adapter 5.22%, and `Scheduler::run` itself 2.22%. Callback descendants are
not all scheduler mechanics; a conservative named scheduler subtotal is
11.55%. Repeated deque lookup, cohort/resume handling
and update staging define the next investigation order.
Callgrind masks hardware SHA support on this host, so hashing shares do not
represent native execution; instruction shares are not wall-time savings.
Evidence is `build/performance-campaign/callgrind-long-throughput-5pct-20260926/`.
The temporary scheduler hook is removed, its original bytes restored, and a
twelve-worker affected rebuild reproduces the frozen P18 executable exactly.
Restoration and final documentation are manually reindexed. P18 retention
and full codec parity subsequently closed; all-ten qualification remains
open. P19 later received a bounded trial and was deferred.

The preceding hourly convergence audit was `convergence-audit-20260926T2110Z.json`.
P12/C3 and P8A retention are closed; the fresh all-ten n1 matrix is complete.
Six Codex cases lead, four external RS cases lose, and none is qualified.
Raw P14 is rejected after losing native multiplier sharing. P14B is retained
after its semantic, reduced/full transfer, codec-parity and profile gates pass;
it removes the redundant cap only from statically proven bounded loops.
P15 is retained: its Release build, seven focused tests,
real-gf_mult and blocking array-write interpreter/O0/O2/Vivado replays pass.
All 30 multiplier stages and both array writers execute natively. Manual
reindexing of canonical `fsim` is verified at 14:02:21 UTC, with 57,176 nodes
and 365,955 edges; changed source metadata matches and existing parse gaps
remain recorded. Corrected static attribution excludes unmodeled control
flow without changing the 10,570 multiplier-read sites. The reduced cold pair
passes its two-second gate: P14B 34.160403 seconds, P15 23.122525 seconds;
complete parity, correctness, identity and cold-cache audits pass. Root took
over after worker service usage limits. The full Verilog throughput pair in
`p15-full-original-throughput-{control,candidate}-v2-20260926/` now passes its
transfer audit at 336.691670 versus 234.590019 seconds (30.3% saving), including
128,878 canonical events, original workload counts and cold-cache checks.
The prior full attempt's sandboxed Vivado launch failed; preserved simulation
passes outside the restriction, and both fresh engines use that same launch
environment. Full codec parity matches the preserved P8A evidence across
117,638 events, 514 streams and 75 summaries. The separate reduced profile
and independent identity audit pass: 2,417 native-phase samples, 60 unresolved
LLVM samples and zero lost; 32 modules, 203,697 optimized LLVM instructions.
P15 retention is closed in `p15-static-container-read/architect-retention-decision.json`.
These are single diagnostic pairs and fsim remains slower than Vivado.
P13 remains scratch-only. Executable checks must wait for the measurement slot.

The mixed-codec profile and mechanical audit are complete. Later P23A and
P24 trials passed source, semantic and full transcript checks and were
provisionally retained as bounded simplifications only. Their opposite-order
full pairs do not establish a gain or non-regression. P25's reduced and full
completed-resume attribution is collected and its temporary source restored.
P26's experimental mode was deferred after one correct reduced pair missed
its two-second full-transfer gate. It can omit unobserved same-value output
transactions when skipping an activation; executed activations retain their
original writes. Registered observers restore whole-signal wake eligibility,
not cross-key batch order, and late hooks cannot recreate hidden history.
Its source/tests are restored to P24 and manually indexed. P27 completed one
correct reduced collection but was deferred after a provenance-label error;
only its production resolver was restored, while its semantic test remains.
P28's independently audited off/on pair is complete and deferred after its
reduced transfer gate failed. P29's source/check/index/freeze and reduced/full
parity gates passed, but large contemporaneous Vivado drift makes its full
source effect inconclusive. Its production hunk is restored to P24 source;
the combined P30 elaboration candidate build and ten focused checks now
pass. Manual index and freeze precede the reduced pair. P13 remains
separate and lower priority.

The user requested a shorter iteration loop and documentation. Follow
[Shorter iteration loop](performance-campaign.md#shorter-iteration-loop): use
incremental affected-target Release builds with twelve workers, parallel
source audits and fixture preparation, focused semantic/native checks, then
reduced timing before full transfer/profiling. Use explicit conditional gates
and a single evidence packet to avoid redundant handoffs. Manually reindex
after every integrated or reverted source batch and record canonical project
freshness/coverage. One execution owner (currently Sol) coordinates all runs;
builds/tests/indexing never overlap
timing or sampling. Versioned preflight transcript reuse is integrated in
the runner as `--reuse-preflight PRIOR_CAMPAIGN`, after the P15 timing slot.
All 38 runner tests, five profiler tests and two configured CTest entries pass.
Manual canonical reindexing is verified at 14:51:00 UTC (recorded 14:51:01),
57,195 nodes and 366,044 edges; changed script metadata matches. Evidence is
`build/performance-campaign/iteration-loop-shortening/`. The live fresh/reused
Codex check passes: 46.735315 versus 27.603072 seconds, two validated cached
preflights, four matching complete transcripts, 62 source-hash checks and
fresh timed caches. The runner change is retained; the avoided 19.445712
seconds of preflight work is not a simulator speedup claim. P15's reduced
profile and full codec parity checks are complete. P16 is retained: attempt
the existing single-process direct slot batch by default
only when no callback update words are queued. All existing domain, profile,
staging and fallback guards remain. Five focused CTests, actual O0/O2 queued
callback fallback, wide update-order witnesses, independent source review,
manual reindexing, two opposite-order reduced pairs and full transfer pass.
Original full throughput improves from 216.505894 to 207.754424 seconds
(4.04%); Vivado is 17.068613/17.165916 seconds. Full codec parity and the
separate profile also pass. The profile records 2,169 samples, 71 unresolved
(70 LLVM, one libstdc++), zero lost, 32 modules and 203,697 LLVM instructions.
No reference case is qualified. The complete evidence and retention decision
are in `p16-single-update-slot-batch/execution-evidence-packet.json` and
`architect-retention-decision.json` in that directory.
Projected/callback ordering concerns must be distinguished from new behavior;
no broader ordering repair or observability change is authorized by P16.
The integrated P16 source/test batch was manually reindexed at 15:36:48 UTC
(57,200 nodes, 366,090 edges). Post-retention documentation freshness is
recorded separately in `p16-single-update-slot-batch/final-documentation-index.json`.
The no-op affected-target build costs 4.96 seconds for 822 scan/dyndep steps
and no compilation/linking. Defer the CMake scanning-default change to the next
required broad build; preserve explicit user settings. Sol owns execution.
P17 attribution is complete. The corrected diagnostic counts 5,542,882
unchanged-input resumes out of 28,398,208 actual attempts in reduced throughput
(19.52%), and 41,734,302 out of 240,336,855 in the full workload (17.36%).
Both complete transcripts, correctness results and source/executable/dependency
identities pass independent review. Full coverage is partial: 19,873 registered
processes exceed the cap and 14,837,748 attempts fall outside it; branched bodies
remain excluded. Mixed groups reset 29 prior histories. Mixed-group invalidation
is source-reviewed; the unsuccessful mixed fixture was removed. These are
activation counts, not time savings or permission to suppress execution.
The source-marker correction, failed probes and supplemental direct-ELF
identities are preserved under `p17-repeated-input-attribution/`. Its
`architect-final-audit.json` checks 42 distinct frozen/dependency files and both
canonical transcripts. All six temporary source/test files were restored
byte-for-byte with fresh mtimes at 17:20:45 UTC; `restoration.json` and
`manual-restoration-index.json` record the checks and manual refresh
(57,198 nodes, 379,866 edges, changed-path metadata matches). The configured
Release executable was subsequently rebuilt without the diagnostic by P18;
the frozen P16 executable remains the retained production control.
P18 implements the approved bounded trial: inline the exact empty-state guards
for dynamic-wait removal, timeout clearing and callable context restoration,
plus the null-safe operation-count accessor. The twelve-worker affected
Release build, 11 focused CTests, independent source review and manual index
pass (57,201 nodes, 366,120 edges; 17:42:48 UTC generation, matching metadata).
No snapshot, field-layout, state-cache or CMake-policy change is included.
Two opposite-order reduced pairs pass the diagnostic gate; the conservative
pair saves 0.804054 seconds (2.83%). Full parity, correctness and identity
checks pass, but the full timing pair tracks Vivado drift. The architect has
not retained or rejected P18. After user resume, resolve that uncertainty
before codec parity or separate profiling; evaluate the accessor simplification
separately under the performance-neutral retention rule. Preserve the live
candidate and exact baseline backups while paused. See
`p18-inline-bookkeeping/execution-evidence-packet.json`, `full-v1-audit.json`
and `architect-pause-audit.json`; the latter independently verifies 50 files
and closes the reduced audit's deferred executable/dependency rehash.
Final manual documentation refresh is recorded in
`p18-inline-bookkeeping/final-pause-index.json`. P19's reusable-buffer
simplification is reviewed scratch only in `p19-reusable-cohort-buffers/`;
it has no build, semantic or timing evidence. Repeated inputs alone do not
prove that an activation can be skipped: queue/wait transitions, per-driver
state and transaction observers still matter. Production activation filtering,
a general control-flow classifier and observability changes remain unapproved.
Inspect actual lowered target processes before expanding future diagnostics.
Reuse falls back to fresh preflight on any missing
or mismatched identity, including the Linux launch context. Timed caches, final stimulus
checks, full-transfer retention and full closure gates remain unchanged.
The user selected an explicit fsim compatibility option for
the original mixed VHDL mode 0 RAM at 18:58 UTC: preserve its bytes and use a
separately labeled repaired baseline. Strict VHDL remains the default; no RAM
overlay will be used. See the campaign document for diagnostics.

Batch 188K supplies structured Tcl workspace/object/debugger commands, a
terminal-independent completion and hint service, and a rich Isocline console.
`fsim tcl` and `fsim debug` use the Tcl console; Tcl `cd` switches workspaces.
Compile/elaborate expose verbosity progress in result `messages`. Interactive
diagnostics print once in severity color, and `fsim::transcript` can record
commands and output to a plain append-only log. Tcl-disabled builds are
command-line only.

## Qualification and limits

- Rescanned LLVM 22 Release build and full suite: **447/447**. A real Unix PTY
  check confirmed the rich prompt, live red diagnostic, and ordered plain log.
- Rescanned Debug affected-target build and focused suite: **5/5**. Rescanned
  Tcl-disabled affected-target build and focused suite: **6/6**. Earlier full
  Debug **446/446** and Tcl-disabled **438/438** suites predate the repair;
  the user requested focused follow-up verification on those builds.
- The Windows CI managed-directory comparison now normalizes both Tcl paths.
  Post-fix Windows CI and Windows console/ConPTY behavior remain unverified.
  Performance and new hosted monitoring were deferred, not passed.
- Stale generated CMake trees were removed, reducing `build/` from about
  **551 GB to 101 GB**. Small historical test reports are preserved in
  `build/qualification/stale-logs-2026-09-25/`. Current builds, the recent
  188E baseline, release packages, and external sources were preserved.

## Next action and guardrails

Campaign checkpoint, 2026-09-26 10:58 UTC: the four-case baseline has
complete parity, diagnostic timing, sampling, and post-run identity evidence.
All initial fsim pairs are slower than Vivado. P1 local producer-path key
membership is integrated with unchanged equivalence fallback and ordering.
The rescanned twelve-worker Release build and eleven focused checks pass;
eighteen artifact-file payloads match the frozen baseline byte for byte.
The graph is reindexed and the P1 executable/dependencies are frozen in
`build/performance-campaign/p1-candidate-d11004e4-uncommitted/`.
Its four-case O2 campaign and separate O0 diagnostics are complete, with
identical cross-simulator stimulus and correctness. Compile time falls
94-98% in these single diagnostic pairs, transferring to the full-size and
Codex sentinels; P1 is retained. Throughput remains slower than Vivado, and
no case has seven-pair qualification.

The explicit VHDL compatibility repair passes its Release build and four
focused tests, including overlapping/disjoint shared-array writes, scalar
`std_logic` storage, interpreter/LLVM O0/O2, wrong-unit rejection, and
source-hidden artifacts. The compatibility-only baseline is separately
built with twelve workers and frozen in
`build/performance-campaign/compat-only-baseline-d11004e4/`; an independent
source audit confirms the eighteen compatibility files and unchanged
baseline bytes everywhere else, including the absence of P1 and P2.
All five reduced fixture pairs now pass complete cross-simulator stimulus
comparison. The separately repaired fifth baseline also passes its complete
measurement/profile campaign and final identity checks. The first deliverable
is complete; see `build/performance-campaign/reduced-first-deliverable-report.json`.
Full fixtures now generate for all ten cases and pass original-parameter
identity checks; the first
full Verilog throughput preflight passes with six instances, twelve codewords
each, and 128,878 matching events. All six full Codex preflights also pass
exact canonical stimulus and correctness. Full codec's completed fsim run
passes offline validation after fixing a reserved seed-function argument and
two metadata/runner assumptions. Missing or duplicate scenario summaries are
rejected. Reduced fixture bytes remain unchanged; failed/interrupted runs are
preserved. Full mixed-throughput now also passes its complete two-tool preflight on P7,
bringing original-case parity to eight of ten at that checkpoint. Its first uninstrumented pair
passes correctness and identities but takes 135.650 seconds versus Vivado's
13.018 (compile 1.293, elaboration 40.647, native/simulation 93.708). Both full codec preflights now also pass, bringing original-case transcript
parity to ten of ten. No original case is qualified.
P2's ordered expression-candidate index passes its source audit,
twelve-worker Release build, and nine focused checks. All 907 serialized
payload files across 31 real Codex objects match P1 byte for byte. Its frozen
candidate preserves separate component identities. The single Codex O2
pair passes stimulus and correctness, taking 8.996 seconds versus P1's
11.905 seconds and Vivado's 9.847 seconds. The 2.909-second reduction meets
P2's diagnostic acceptance threshold; retain P2 without claiming reference
qualification.

The native-operation diagnostic identified 302 repeated multiplier modules
accounting for 91.6% of lowered operations. P3 admits their register-only
`DynamicPartInsert` to existing sharing with exact operand equality and all
existing guards. Its build and LLVM/existing sharing checks pass, and its
separately frozen candidate's cold mixed preflight matches all 24,354
canonical events and correctness results. A separate saved-workspace setup
diagnostic verifies a 92.85% reduction in lowered operations (1,702,220 to
121,649). The new Logic9 sharing fixture passes interpreter/O0/O2 equivalence
and verifies actual template reuse with a frozen-P2 negative control; all
three focused CTests pass. A cold single pair passes stimulus/correctness and
identity checks but takes 125.069 seconds versus Vivado's 9.751 seconds.
The compatibility-only baseline's measured total is 357.926 seconds, including
239.897 seconds native setup/simulation. The combined P1/P2/P3 candidate saves
232.857 seconds overall; its 163.088-second native reduction supports P3's
static sharing proof. P3 is retained. The baseline's separate profile and final
identity checks pass. The separate fresh-cache candidate
elaboration/native-simulation profile is
complete: elaboration samples identify repeated subtype/name/bound resolution;
native samples and LLVM shapes identify eight large slice templates retaining
over 100,000 LLVM instructions apiece. P4's indexed-word loading for one-bit
`DynamicPartSelect` from a wide register is retained. Its build and focused
checks pass; the cold pair saves 13.444 seconds in native setup/simulation
and 12.297 seconds overall versus P3, with complete stimulus/correctness and
identity parity. Fsim still takes 112.772 seconds versus Vivado's 9.500.
Separate IR proof removes all 1,056 wide shifts from the selected process;
instrumented elapsed time is not used as speedup evidence. P5's
definite-definition bit vectors are retained with the algorithm, supported
register-count domain, and first diagnostics preserved. Its twelve-worker
Release build, LLVM and affected application checks, and twenty Python tests
pass, including executed 65/133-register outputs and first-error tests.
The matched P4/P5 cold pair uses shared frozen runtime dependencies and passes
complete canonical parity, normalized correctness, final fingerprints, and
identities. Native setup/simulation falls from 63.317 to 55.295 seconds;
total falls from 113.282 to 104.310 seconds. Vivado takes 9.604 seconds.
P5 exceeds its five-second initial threshold; this single pair does not
qualify a reference case. Its independent audit is in
`build/performance-campaign/p5-localdeps-mixed-throughput-o2-pair-20260925/`.
Affine fusion and dynamic insertion remain outside these boundaries.
The preceding mixed profile has 36.9% unresolved samples, so attribution
remains incomplete. No build, test, or indexing may overlap timing.

The latest convergence audit is 10:58 UTC on September 26; evidence is
`build/performance-campaign/convergence-audit-20260926T105837Z.json`.
P7 retention, its 42.585-second full elaboration phase gain, and the eighth
original parity gate are new measured progress. P9's initial twelve-worker Release build passes. The new nested labeled
control fixture exposed a retained map iterator invalidated by nested scope
restoration; the loop now captures its register ID by value and eleven
focused CTests pass. The CLI fixture checks every copied bit and all nine
Logic9 states through disjoint 9-bit and 56-bit slices, and passes interpreter,
O0 and O2, as does the suspended-loop fixture. Its original whole-65-bit
comparison checker exposes the same invalid LLVM constants on frozen P7 and
P9; the separate C1 repair now passes its twelve-worker Release build,
focused LLVM/elaboration/application tests, and original whole-width checker
in all three execution modes. Frozen P9-only cold pairs
pass full parity, fingerprints, correctness and identities. Reduced mixed
falls from 77.820 to 51.229 seconds and original full mixed from 135.650 to
111.131 seconds, saving 26.591 and 24.519 seconds respectively. Vivado takes
9.753 and 13.777 seconds. These single pairs support transfer, not reference
qualification. Root independently audited both campaigns and all six CLI
case/mode outputs. The malformed-body P7/P9 control also passes exact first
and unique diagnostic signatures, including the source span. Root verifies
C1's eighteen phase exit codes and raw outputs separately. P9 plus C1 is
reindexed. The matched eight-function shape comparison and 131-visit source
breakpoint replay pass. P9 is retained after the matched P7/P9 replay also
records 131 body visits on identical VHDL source bytes. The attempted CLI
coverage gate is explicitly unavailable before and after P9: neither build
attaches an inventory or emits coverage hits through this path. This is a
pre-existing product limitation, not a coverage pass or observer restriction;
see `p9-preparation/architect-retention-decision.json`.
P4/P5 retention, complete reduced baseline evidence, and seven full parity
gates are substantive progress. P6 passed its build, focused correctness,
selected-process IR, complete fresh parity, and identity checks. However,
the cold pair saved only 1.153 seconds native and 1.359 seconds total, below
the fifteen-second threshold. It is rejected; only its production/direct-test
hunks were removed, preserving P4's tests and every experiment artifact.
The 395,268 KiB RSS reduction and 112,194 to 6,570 instruction-like lines
remain diagnostic evidence. Frozen P5's recurring counters now record
27.8 million native resumes and 25.3 million container word reads. Coarse
whole-array sensitivity in multiplier reduction processes is under read-only
audit. An isolated settled-update witness confirms fsim reevaluates a static
`red[0]` read after unrelated `red[1]` changes while Vivado does not; startup
differences are recorded separately. No dependency narrowing is approved. P7's isolated subtype probe
records 16.7 million lookups with a 99.563% repeated-key lower bound.
P7 production caching is retained within one outermost synchronous call,
with exact owned keys, bounded key/result storage, successful results only,
entry/exit/setter clearing, and normal resolver fallback. P7's production source audit,
twelve-worker Release build, and eight existing focused checks pass. All
seven serialized mixed-design payloads match frozen P5 byte for byte in an
independent audit. Its new real-Lowerer 65-width generic and package-scope
regression passes in the interpreter. The generic-binding/capacity fixture also
passes fresh CLI interpreter/O0/O2 replay with identical output. The full fixture
including qualified package calls hits the same two workspace visibility errors
on frozen P5 and P7; package scopes are covered by the in-memory test only.
The independently audited cold pair now passes parity, identities and the
empty-cache guard, saving 26.490 seconds overall and 25.084 seconds in
elaboration. Fsim still takes 77.820 seconds versus Vivado's 9.453. P7 is
retained; the full original mixed case passes correctness and parity, and a
fresh P5 elaboration-only control verifies 83.700 versus 41.115 seconds.
Five of seven independent fresh-compile full payloads differ in source/HIR
identifiers. The follow-up using the same 727 archived compiled input files
and absolute workspace produces seven byte-identical P5/P7 snapshot payloads,
including the 397,136,099-byte runtime. Root independently compares all
407,342,892 snapshot bytes and their hashes. This closes full artifact parity
for identical compiled inputs without establishing the earlier difference's
root cause. A separate
fresh profile finds LLVM owns 80.10% of native-phase samples, including all
44.02% with unresolved names. The next native-code audit covers every large
slice template and remaining one-bit insertion; P6 remains rejected. The
fresh full profile confirms LLVM still owns 64.07% of native-phase samples,
with a larger recurring-runtime share. The requested inherited module timer
flag was stripped; no module timers were recorded. The explicit profile-only
runner option now passes its isolation tests. P9 counted lowering for large
static VHDL loops is implemented within the approved campaign boundary,
with no semantic/observer restrictions. P8 permits only a local
operation-count cache, retaining bounds checks and current instruction lookup,
and is deferred behind the larger supported P9 candidate.

The cache audit found that workspace simulation ignores the supplied
external `--cache` in favor of `.fsim/cache/llvm-native`. Copied-workspace
P4/P6 IR runs, P3 setup-shape, and P5 counters cannot support cold-cache
claims; their shapes/counts/final results remain useful. Original reports
stay immutable, with a separate correction audit. Actual timing campaigns
use fresh whole workspaces and first simulation, so their cold measurements
remain valid. Effective-cache reporting and the pre-JIT empty-object guard
now pass 26 Python checks and an independently audited codec preflight
(zero objects before elaboration/simulation, 86 afterward). Receipt-only
rejection is tested and the cold flag uses the observed inventory. The Sol
orchestrator and existing workers resumed after the API interruption; do not
restart completed diagnostics. The P9 profile and all ten full parity gates are complete. Continue ranking
the remaining cost and qualifying all ten cases.
The graph was reindexed after runner dependency hardening at 02:23:21 UTC
(57,125 nodes, 365,211 edges). The 322 partial files and one unusable diagnostic include
remain explicit. The Release build tree contains P11 plus C1; P6's rejected
source is absent. The P9-only executable and both transfer campaigns are
frozen. The common-object control is complete and the slot released for the
codec preflights; no build, test or index may overlap timing or profiling.
The corrected frozen P9 module profile is complete and independently audited:
all eight slice functions total 127,952 native bytes; LLVM accounts for
1,952/2,587 native-phase samples, and the type-binding scan for 261/2,084
elaboration samples. Module wall timers overlap across native workers and
cannot be summed as CPU cost. An earlier profile used a different library
environment and is explicitly non-comparative; all cold timing dependencies
were separately verified identical. The matched P7 profile is now complete:
all eight target functions total 11,218,252 native bytes versus P9's 127,952,
a 98.86% reduction. Root verifies four complete preflight transcripts and all
eight module/map records independently. Module grouping differs, so the
879,836 to 93,441 optimized-instruction totals include additional P9 processes;
packed frame-slot counts are not resident stack-byte measurements. At 02:16, host process
inspection confirms no campaign/build process, while the agent registry shows
Sol pending initialization after a runtime restart. An explicit resume task
was sent and Sol resumed the matched P7 run. That run is complete. The combined full-codec campaign completed original
Verilog parity, then failed on mixed VHDL because its explicit compatibility
mapping was omitted. The separate mixed-only compatibility retry passed.
Root independently compares both engines' complete canonical bytes for each
case: 117,638 events, 514 streams and 75 summaries, with 1/11/75 correctness
markers and all phase exits zero. Shared identities, 18/19 source input hashes
and the successful retry's post-run identities close the partial case evidence
without relabeling the failed campaign. See
`p9-c1-full-mixed-codec-compat-preflight-20260926/architect-full-codec-parity-audit.json`.
At 08:53, host process inspection after another runtime restart confirms no
benchmark/build process. Sol explicitly resumed; completed runs are preserved.

The runner now validates both existing manifest forms, top-level
`linked_dependencies` and legacy `executable.dependencies`, requires complete
nonempty entries and observed identities, and records the verified field/count.
All 30 runner plus five profile-helper tests pass; live checks verify all 22
P7/P9/P9+C1 dependencies and reject the earlier wrong-library profile. Frozen
manifests are unchanged. Evidence is `dependency-identity-guard-20260926/`.
P10 tested the existing
ordered type-name index only for the base declaration scan, with C/POSIX
locale and complete candidate-ID validation guards; preserve the specialized
scan, nearest-scope/ambiguity rules and raw-scan fallbacks. It is now rejected
for failing its cold total saving gate; its isolated edits are reverted and
its complete evidence is preserved. Sol owns builds and measurements.
Its evidence, bounds and 1.5-second reduced cold acceptance
threshold are in `p10-type-candidate-index/architect-decision.json`.

The P11 change set is reindexed at 09:04:35 UTC: 57,126 nodes and 365,234
edges, with the same 322 partial files and one unusable diagnostic include.

P11 now has priority: lower only the minimum eligible static VHDL counted-loop
threshold from 64 to 16, keeping every P9 guard and observer behavior. The
remaining syndrome/chien modules contain 623,865 optimized instructions; their
31/32-iteration loops become eligible, while multiplier loops of 15/8/7 remain
unrolled. LLVM owns 75.45% of the reduced native profile, providing more
upside than P10's approximately 2.6-second scan ceiling. Sol owns implementation and timing. The twelve-worker Release build and six focused
checks pass, as do interpreter/O0/O2 output equivalence and 16/16 source visits.
Root verifies reversing the single threshold line reproduces frozen P9 source
bytes. The fresh P9+C1 control takes 56.999 seconds; P11 takes 47.874, saving
9.125 seconds overall and 9.226 seconds native with identical 25.447-second
elaboration. RSS falls 242,304 KiB. Four complete canonical preflights, all
eight correctness results, timed summaries, 19 inputs and 22 dependencies
match in the independent audit. This single pair passes the five-second
diagnostic gate, not reference qualification. Full original mixed transfer is
complete: 118.758972 to 83.844658 seconds, saving 34.914314 overall and
34.713691 native, with 410,204 KiB lower RSS. All four 128,878-event
canonical preflights, correctness, summaries, inputs and dependency/cache
identities match. The unsuffixed initial control omitted `--prepare-full`
and failed before launch; the `v2` control is authoritative. Full mixed codec
again passes 117,638-event parity; reduced codec changes by +0.049692 seconds
at n1 with exact stimulus and correctness. The separate profile has 40.5%
fewer optimized LLVM instructions, with 1,223/1,641 native samples still in
LLVM; all 594 unresolved names are in that DSO, with zero lost samples.
P11 is retained; see `p11-threshold16/architect-retention-decision.json` and
its linked independent audits. All ten original cases have parity, zero have
seven-pair qualification. P10 was the next experiment and is now rejected. Sol also
owns a separately labeled untimed retained P11 snapshot diagnostic to identify
euclid operation widths and template-sharing rejection. Fifteen unchanged
euclid modules retain 349,425 optimized instructions. The retained diagnostic
identifies their only unsupported sharing kind as `WriteProjectedDynamicSlice`.
P12 is now approved for exact-field native sharing with all current validation,
remapping and callback behavior preserved; see its architect decision. Its
worker prepares scratch code/tests only until the P10 binary is frozen. P10
has its first twelve-worker Release build and focused passes; Sol now owns
base-only and fallback tests, artifact replay, freezing and timing. P8 scratch
work adds compiled live VITAL reannotation coverage. Fresh Verilog sampling
confirms recurring runtime dominates (2,275/2,847 samples in fsim); 65 names
are unresolved in LLVM and none lost. All profile timings remain diagnostic.
No build/test/index may overlap timing.

At 10:15 UTC, P10 is frozen as executable `78deb2a9`, with source
`0b0709ec` and real-Lowerer test `2a83e0d6`. Twelve-worker Release and eight
focused checks pass. Tests cover base-only nearest-scope selection, stale
indexes, a malformed nearer candidate whose omission changes the result,
locale fallback and extended spelling. The 10:13:06 full index has 57,127
nodes and 365,295 edges, with unchanged coverage limitations. The same-object
replay uses 727 compiled files and reports all seven raw snapshot payloads
byte-identical; its phase durations are diagnostic only. Cold reduced and
full mixed measurements remain the retention gate. P12 scratch production
is ready; its actual-sharing and event-timeline fixture is under independent
review. P8's scratch VITAL test now keeps metadata spans and the JIT owner
alive for every installed executor. No additional improvement is retained
until its isolated correctness and cold timing gates close.

At 10:26 UTC, two fresh P10 reduced comparisons save -3.574132 and
+0.597790 seconds overall. Elaboration improves by 0.746423 and 3.208514
seconds, but native-phase variation overwhelms the total benefit; the
predeclared credible 1.5-second total gate is unmet. Independent audits
compare all eight complete canonical transcripts, final summaries,
correctness, input hashes and dependency/configuration identities. P10 is
rejected without full-size timing. Its lowerer source is restored exactly to
pre-P10 SHA `b653548e`; the test and candidate remain archived. See
`p10-type-candidate-index/architect-rejection-decision.json`. P12's bounded
production and actual-sharing fixture are now integrated for a twelve-worker
Release build. Sol owns fixture debugging, focused gates, freeze and timing;
root owns independent review. A separate scratch reproducer investigates a
possible existing connected-signal buffer remap issue; no speculative repair
is approved. P8 remains the next queued runtime experiment.

P12 is now frozen as executable `549de0d3`, with source `2e845493` and
test `1bfd3c86`, against retained P11. The twelve-worker Release build and
five focused gates pass. The final fixture observes 140 real dynamic
projected writers in four native modules: three timing forms with 45
instances each, plus five ascending instances. Interpreter/O0/O2 values and
full event timelines match. Removing P12's whitelist entry makes the original
135-writer O0 fixture produce 135 modules; restoring it produces three.
Early fixture failures were setup/assertion issues, including the existing
128-process eligibility threshold; production policy was unchanged. Root's
source/sharing audit is `p12-dynamic-projected-sharing/architect-source-and-sharing-audit.json`.
The refreshed graph has 57,145 nodes and 365,386 edges, with unchanged
coverage limitations. Fresh P11/P12 reduced and original-full pairs now
pass independent canonical, correctness, fingerprint, source/dependency and
cold-cache audits. Reduced total falls from 40.545008 to 35.929943 seconds;
full total falls from 80.380202 to 71.201076 seconds, saving 9.179126 seconds
and 124,260 KiB RSS. The selected full workload identity is verified for six
instances and twelve codewords each; the runner's all-ten-workload flag
correctly remains false for this subset. These n1 diagnostics meet the
initial gain/transfer gates, not reference qualification. The attempted HDL
connected witness reached the intended signal-ID overlap and 128 compiled
writers but compiled two modules, so it did not prove reuse. The direct C3
witness and explicit width applicability decision below supersede that setup.
No build, test or indexing may overlap measurements.

At 11:09 UTC, the direct connected-remap witness reproduces a real buffered
Logic9 defect in O0 and O2: a delayed whole-vector value incorrectly
overwrites the bit canceled by an immediate projected slice. Disabling
buffering makes both pass. C3 is approved to remove only the collector's
second remap of already candidate-local IDs. Keep generated callback
remapping and every eligibility guard unchanged. C3 now passes its final
twelve-worker Release build and seven focused checks, including LLVM and
elaboration. Root and a worker independently accept the source/lifetime
proof and the regression's exact A/B/C state and C timeline. Frozen P12+C3
executable `712e6356` retains 22 identified dependencies. The graph is refreshed
at 11:17:34, with 57,156 nodes, 365,609 edges and unchanged coverage gaps.
The isolated source-width HDL fixture failed before execution on alias and
composite-type setup; both attempts are preserved and the proven 140-writer
test is restored byte for byte. The architect accepts exact producer-width
and comparator proof plus existing O0/O2 width tests, with this limitation
explicit in `p12-dynamic-projected-sharing/architect-width-coverage-decision.json`.
P12 retention is now complete. The full mixed-codec canonical transcript
matches exactly (`5ef3e290`, 117,638 events, 75 passing scenarios). The separate
reduced profile verifies fifteen Euclid bodies becoming template 401, total
LLVM modules 87 to 60, and optimized instructions 758,897 to 431,250. It records
1,303 native samples, 428 unresolved (426 LLVM, two libc), and zero lost.
Instrumented times make no speedup claim. The root audit and decision are
`p12-c3-reduced-mixed-throughput-profile-20260926/architect-profile-audit.json`
and `p12-dynamic-projected-sharing/architect-retention-decision.json`.
P8 integration is authorized against frozen P12+C3 `712e6356`, with its
sanitized operation-count, VITAL and external-boundary fixtures, twelve-worker
Release build and focused checks, then fresh reduced/full Verilog pairs.
P8's build passed and seven focused checks passed. The added all-compiled
VITAL delay reannotation case fails identically with P8 reversed; native delay
constants remain stale, whereas live timing-check slots are read through
callbacks. Preserve this separate limitation and failed evidence. The approved
P8 fixture refinement compiles the timing-check process only at O0/O2, checks
actual limit replacement and all three policies, and keeps interpreted delay
oracles. The final twelve-worker Release build and eight focused gates pass. Frozen
P8 executable is `32ed0ac4`; the graph now has 57,170 nodes and 365,812 edges
with unchanged coverage gaps. Fresh reduced Verilog P12+C3/P8 pairs are next.
See
`p8-operation-count/architect-vital-applicability-decision.json`.
P8's count cache is now rejected: two reduced pairs show savings of -0.506
and -2.107 seconds, with correct canonical/identity/cache results. The seven
application files are restored to P12+C3. A worker artifact-self-check timing
audit remains explicit; no P8 speedup or neutrality is claimed. P8A retains
only the external-boundary lookup motion as a simplification candidate under
the new user rule, with separate build/focused checks and fresh paired timing
before the neutrality decision. Preserve all failed and rejected evidence.
See `p8-operation-count/architect-rejection-decision.json`.
P8A now passes the twelve-worker build and eight focused checks. Its frozen
executable is `ba2d29bf` with 22 verified dependencies. Root source audit is
`p8a-external-boundary-cleanup/architect-source-and-semantic-audit.json`;
index refresh records 57,168 nodes and 365,802 edges with unchanged coverage
gaps. The isolated P12+C3/P8A pair passes independent complete canonical,
correctness, final-summary, 32 input-hash, 22 dependency and cold-cache audits.
Totals are 32.905733 and 33.354906 seconds, a 0.449173-second increase within
recent control variation. P8A is retained under the user's simplification
rule with provisional engineering neutrality; this single pair proves neither
statistical equivalence nor speedup. The added cache remains reverted.
See `p8a-external-boundary-cleanup/architect-retention-decision.json`.
Rejected P8 diagnostics retain an explicit possible artifact-check overlap
limit; the new isolated P8A pair has no such overlap. The fresh n1 diagnostic
matrix for all ten original workloads is complete on frozen P8A, CPU 0,
with 22 frozen dependencies and explicit compatibility for both mixed cases.
The independent audit passes all 20 complete canonical transcripts, 40
engine results and 258 input-hash entries (80 unique paths), plus original
workload, correctness, empty-cache and post-identity checks. All six Codex
cases lead at n1; all four reference Reed-Solomon cases remain slower. None
is qualified. Verilog codec/throughput totals are 251.630/264.928 seconds
against Vivado 31.630/17.288; mixed codec/throughput are 144.650/70.082
against 26.691/13.801. Evidence and the phase ranking are in
`p8a-all-ten-original-n1-20260926/architect-original-matrix-audit.json` and
`architect-phase-ranking.md` in the same directory.

P14 live integration is now approved: change only the existing eligible
static VHDL counted-loop threshold from 16 to 8, preserving all P9 guards.
Mixed codec spends 92.526 seconds in elaboration and mixed throughput 37.797;
the measured expanded operation volume supports this bounded experiment.
Sol owns the twelve-worker Release build, focused checks, and prepared CLI
replay proving actual native O0/O2 execution alongside the interpreter. Then
freeze, reindex and measure a fresh reduced mixed pair against P8A; require
three seconds credible cold-total saving before full transfer, full mixed
codec parity and actual-shape profiling. See
`p14-threshold8/architect-integration-decision.json`.
Those build and focused gates now pass: seven CTests, nine CLI phases, and
byte-identical simulation output in interpreter/O0/O2. Both compiled legs
record the loop worker executing natively once (517 operations), with two
lowered processes and none retained. Frozen candidate is `75647d47`, source
`02a41227` and test `94dad474`, with 22 verified dependencies. The refreshed
index has 57,168 nodes and 365,800 edges with unchanged coverage gaps. Root
evidence is `p14-threshold8/architect-source-audit.json`. The fresh reduced
mixed P8A/P14 pair now fails the retention gate: 35.432421 to 57.694261
seconds overall, despite elaboration improving by 14.145808 seconds. Native
setup/simulation increases by 36.456182 seconds. The independent audit passes
all four complete canonical transcripts, correctness, final summaries, 38
input hashes, 22 dependencies and cold-cache/identity checks. Raw P14 is
unretained and will not receive full-transfer timing. One separate frozen
P14 profile is now complete: 364 native modules replace 60, with 301 separate
multiplier modules each retaining 16,575 optimized LLVM instructions. LLVM
accounts for 75.37% of sampled native-phase CPU; 42.79% of total samples lack
resolved symbols, with none lost. Source identifies the redundant static-loop
cap's Halt as a likely sharing blocker; the whitelist excludes it. These are
diagnostic counts, not performance measurements. P14B is approved to omit
only the cap block when static endpoints already prove at most one million
iterations. Dynamic caps, loop control and sharing rules remain unchanged.
Focused semantic/native checks, restored sharing, a fresh reduced cold saving
of at least three seconds, then full transfer still gate retention. See
`p14-threshold8/architect-static-cap-followup-decision.json` and
`p14-threshold8/profile-summary.json`.
P14B now passes the twelve-worker Release build, seven focused checks and
nine CLI phases with byte-identical interpreter/O0/O2 output. The native loop
worker executes once at both optimization levels with 447 operations; the
checker executes twice. The exact one-million static boundary elaborates
compactly, and the existing over-limit and dynamic-cap checks pass. Root
reconstruction proves every other production byte matches raw P14. Source
`b2a5627b`, test `eeaee2d1` and executable `154254ac` are frozen with 22
verified dependencies; index refresh records 57,168 nodes and 365,808 edges.
The source/semantic audit is
`p14-threshold8/p14b-architect-source-and-semantic-audit.json`. A separate
native dynamic over-limit CLI is not yet executed; dynamic emission is
preserved exactly. The fresh reduced pair is 36.239280 to 18.180317 seconds,
saving 18.058964 seconds; original full mixed throughput is 74.970936 to
39.955371 seconds, saving 35.015564. Independent parity/correctness, input,
dependency, configuration and cold-cache audits pass in both scopes. Native
object counts fall 60 to 40 and 165 to 79. Full mixed-codec parity now passes
all 117,638 events and 75 scenarios, including equality with retained P8A.
The independent profile audit confirms 40 modules, 26,104 lowered operations
and 300,801 optimized LLVM instructions. Multiplier representatives 102/304
each contain 689 SimIR operations and 1,360 optimized LLVM instructions;
sharing is restored. LLVM accounts for 623 of 941 native-phase samples;
289 samples lack symbols, all in LLVM, with none lost. P14B is retained;
see `p14-threshold8/p14b-architect-retention-decision.json`. No reference is
qualified. The next approved diagnostic is
`p15-static-container-read/architect-attribution-decision.json`: collect real
Verilog static-read shapes using instance-remapped container object IDs,
then restore the temporary instrumentation. Production P15 remains gated on
that evidence, native equivalence and cold full-transfer results.
See `p14-threshold8/architect-reduced-gate-decision.json` and
`p14-reduced-mixed-throughput-profile-20260926`.
P15 attribution is now complete: 11,472 direct Logic4 alias shapes include
10,570 sites in 2,114 multiplier reduction processes, with 264 unknown-index
sites excluded. Root verifies 24,354 complete events, four summaries,
16 source hashes, 22 dependencies and exact restoration of P14B source and
executable. These are static shapes, not proven lowerer conversions or read
frequency. See `static-container-read-attribution-diagnostic-20260926/
architect-attribution-audit.json`. P15 bounded integration is approved by
`p15-static-container-read/architect-integration-decision.json`: unchanged
whole-signal sensitivity, proven bare active-genvar index and exact Logic4
alias guards, existing ReadSignal/Extract and fallback. Sol owns the Release
build, focused tests and real-gf_mult interpreter/O0/O2/Vivado replay. Actual
native execution, remapping and optimized-code inspection gate timing;
require two seconds credible reduced cold saving before full Verilog
throughput transfer and full codec parity. P13 remains scratch-only and lower
priority after the P14B profile.
The existing native-static-region flag remains a deferred diagnostic; its
modeled candidate resumes establish no measured gain.

User rule added September 26: retain performance-neutral changes that
simplify implementation, with correctness gates preserved. Assess separable
cleanup independently; do not equate new cache state with simplification.
The all-ten lower-median and seven-pair closure requirements still apply.

The latest hourly convergence audit is `convergence-audit-20260926T135625Z.json`.
P12/C3 and P8A acceptance and the all-ten n1 matrix are closed. Raw P14 is
rejected; P14B is retained after all its bounded gates pass. P15 attribution is complete and its bounded production experiment is
approved; P13 remains scratch-only. Broader reuse and sensitivity changes remain unapproved.
Worker executable checks must wait for an available execution slot.
P15 real-design/native equivalence and optimized-code inspection now pass.
The shortest remaining dependency is the active fresh reduced cold pair;
only a credible two-second saving permits full transfer. The versioned
preflight-reuse runner proposal can be integrated as a separate batch after
this timing slot, without changing compiler-candidate identity.

Continue the bounded optimization loop and all-ten-case expansion in the
campaign document. Start Batch 189 only when the campaign scope
permits and after reviewing its plan. Preserve user-owned untracked
`phase.fst` and `scripts/__pycache__/`, frozen baselines, external design
sources, table-local IDs, artifact identities, the SystemC plugin C ABI, and
public string lifetimes. Rescan CMake dependencies before closure builds;
clean builds are unnecessary. Use at least twelve local build workers, avoid
`shared_ptr::unique()` for Windows, and use dependencies compatible with a
future closed-source fsim. Reindex the codebase manually after change sets.
When workers are needed, the primary `gpt-6-sol` orchestrator at high effort
may assign up to five `gpt-6-luna` workers at max effort for this campaign.
