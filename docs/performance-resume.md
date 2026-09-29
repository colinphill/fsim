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

> **2026-09-29 16:40 UTC convergence checkpoint.** The latest controlled
> frozen V21 observation is **71.291367206 s** for the full 6×12 Verilog
> workload, **56.291 s above** the 15 s goal; its near-equal paired V20
> observation does not establish repeatability. V22 now replaces per-region
> physical masked frontiers with one full-key-ordered global frontier per
> delta bucket. The new wide, interleaved runtime differential test passes;
> application/LLVM checks and independent exception review are in progress.
> No V22 full-case mechanism or Wall result exists. Next: finish exact-source
> focused checks, then measure callback and queue-entry removal in a separate
> counter-only diagnostic before deciding whether a cold screen is justified.

> **2026-09-29 16:10 UTC controlled-pair correction.** A justified reverse-order
> full cold pair on exact frozen binaries took **71.291367206 s for V21**
> followed by **71.182836692 s for V20** (+0.109 s paired); native-phase
> and user CPU times were nearly equal. Both raw/canonical transcripts and
> all 78 native objects matched byte-for-byte. The earlier cross-window
> 69.584 versus 55.575 s observations therefore do **not** support a
> 14 s V21 effect; the first V20 fast run remains unexplained. One pair
> cannot establish equivalence or repeatability. Frozen V21 remains retained
> under user direction. Its 8,100-signal bridge demonstrably bypasses
> 11.7567 million private fanout entries but does not reduce masked calls.
> [Pair receipt](../build/performance-campaign/verilog-throughput-fast-loop/v21-private-bridge/freeze/controlled-pair-receipt.json)
> and [CPU-profile comparison](../build/performance-campaign/verilog-throughput-fast-loop/v21-private-bridge-profile/exclusive-comparison.json)
> are diagnostic. The 15 s goal remains unmet; no more repeat runs without
> new evidence. Next: choose a generic graph change that removes actual
> activations or update work, using the retained V21 source and profile.

> **2026-09-29 16:00 UTC V21 result and next gate.** The one frozen, uninstrumented
> full Verilog `original_throughput` 6×12 screen took **69.583531710 s**,
> versus retained V20 **55.575112179 s** (+14.008 s observed). The 15 s
> goal remains 40.575 s from the retained control and 54.584 s from V21.
> Raw/canonical transcripts and all 78 native objects are byte-identical
> to V20; six exact-source focused tests passed. A separate counter-only
> diagnostic certified 8,100 private signals and recorded 11,756,696 local
> commits/fanout entries bypassed, including 11,748,596 direct owned
> commits; masked activation counts did not fall. Per the user's explicit
> retention instruction, keep V21 graph-bridge code while diagnosing the
> regression; one cold observation proves neither cause nor repeatability.
> [Freeze and receipts](../build/performance-campaign/verilog-throughput-fast-loop/v21-private-bridge/freeze/).
> Next: bounded source/call-path attribution before another production edit
> or full measurement. No repeat V21 cold screen.

> **2026-09-29 15:40 UTC convergence checkpoint.** Retained V20 remains
> **55.575112179 s** cold Wall, 40.575 s above the 15 s goal. V21 now
> composes a graph-certified closed private signal between a V19 producer
> cohort and V20 activation-masked consumers, retaining both native kernels
> and the original update-to-next-delta boundary. The focused runtime
> differential suite passes, including a counter proving the owned direct
> commit route. A VHDL application fixture first failed on an unsupported
> report expression; its repaired source and conservative late-fork reader
> census are rebuilding. Next: exact-source VHDL/runtime parity and source
> review, then a separate mechanism gate. No V21 full timing result exists.

> **2026-09-29 15:13 UTC profile checkpoint.** The separate frozen-V20
> full-case CPU profile is complete: 6,129 exclusive simulation samples,
> zero lost, 77 unresolved. Largest disjoint categories are signal
> fanout/queue 12.33%, generated JIT 11.76%, logic values/drivers 10.52%,
> masked dispatch/staging 9.63%, and scheduler/update commit 9.50%.
> Profiled phase Wall is sampler-affected and separate from the accepted
> 55.575 s cold result. Exact transcript and 78-object parity passed.
> [Report and histogram](../build/performance-campaign/verilog-throughput-fast-loop/v20-masked-frozen-profile/report.md).
> Next select a generic graph change from the remaining boundary and
> scheduler paths; no further run is authorized by these samples alone.

> **2026-09-29 15:02 UTC: V20 generic masked graph kernel retained.** One
> uninstrumented full Verilog `original_throughput` 6×12 cold screen took
> **55.575112179 s** (compile 0.671, elaborate 6.087, native setup and
> simulation 48.816 s), versus retained V19 59.599133333 s. The 15 s goal
> remains **40.575 s away**. Raw/canonical transcripts are byte-identical to
> frozen V16; 16 inputs, six summaries and seven correctness lines pass.
> A separate counter-only diagnostic records 15,069,883 activation-masked
> kernel calls representing 96,599,156 original singleton activations and
> bypassing as many owner-stage calls, with zero fallbacks/demotions. It
> preserves boundary publication; it does not elide private fanout yet.
> Archived topology supports 540 packed-reducer output groups, with at least
> 538 bound conditional on the source-classification proof; runtime counters
> do not split those calls by family. Wide SV and VHDL masked admission at
> 9/65/129 bits passed focused parity tests. The 4.024 s observed difference
> is one sample, not a causal or repeatability estimate. Next: profile the
> frozen V20 call path and time categories before another edit. Receipts:
> `build/performance-campaign/verilog-throughput-fast-loop/v20-masked-first/freeze/`.

> **2026-09-29 14:40 UTC convergence checkpoint.** Retained V19 remains
> 59.599133333 s for the full Verilog cold screen, 44.599 s above the 15 s
> goal; no V20 Wall result exists. The V20 generic activation-masked compiler
> and runtime passed initial focused LLVM and runtime checks, including a
> same-timestamp blocking-write interleave. A newly added pending-output
> observer case still needs its exact-source rerun. The shortest dependency
> is binding the masked kernel in the application and proving sparse projected
> VHDL admission at widths 9, 65 and 129 before any full diagnostic or cold
> screen. Preserve original owner/update and boundary delta behavior.

> **2026-09-29 13:54 UTC: V19 scoped-fork graph kernel retained.** The one
> uninstrumented full Verilog `original_throughput` 6×12 cold screen took
> **59.599133333 s**, versus the previous V19 81.071374422 s and frozen V16
> 83.468136206 s. Raw/canonical output is byte-identical to V16; all 16
> inputs, six summaries and seven correctness lines pass. The observed Wall
> difference is one sample, not a repeatability or causal estimate. A
> separate counter-only full diagnostic shows all 540 graph certificates
> survive 12 unrelated testbench fork children and 1,516,205 fused kernel
> calls represent 97,037,120 original members and bypass as many owner-stage
> calls; V16 already batched the
> producer cohorts, so that member count is not saved native calls. The
> 15 s goal remains **44.599 s away**. Next, prove a generic compiled graph
> region across the hot private singleton/reduction chains while preserving
> V16 prepared execution, original owner/update semantics and boundary
> delta timing. No repeat cold screen. Evidence:
> `build/performance-campaign/verilog-throughput-fast-loop/v19-fork-scope/freeze/`.

> **2026-09-29 13:42 UTC convergence checkpoint.** V19 remains retained:
> wide SV/VHDL fusion is qualified, and its one full cold screen took
> 81.071374422 s versus frozen V16's 83.468136206 s, leaving 66.071 s to
> the 15 s target. The separate diagnostic's 888 fused invocations may be
> limited by a global certificate invalidation when the full-case testbench
> forks after startup. A scoped fork repair is in focused validation, with
> unrelated-child survival and affected-output fallback witnesses. Confirm
> live certificate survival and coverage before expanding the graph kernel;
> no repeat cold screen or new chain architecture is justified yet.

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
> V16 remains the frozen 83.468136206 s full-throughput baseline; V18 was
> rejected and restored. Build an elaboration-time complete read/ownership
> graph plan, then a generic fused native kernel that collapses eligible
> member execution and private owner writes while retaining original delta,
> scheduler, and boundary semantics. The first gate is an unrelated compiler
> fixture proving admission and fewer native/update calls, followed by runtime
> integration and mechanism counts. No V19 cold result exists. See
> `build/performance-campaign/verilog-throughput-fast-loop/v19-graph-kernel-source-gate.md`.
> User direction now retains this graph-kernel line through an initial
> regression for diagnosis and iteration; the old automatic one-screen
> rollback rule does not apply to V19. Keep one cold observation per candidate
> unless a further diagnostic need is justified, and preserve frozen V16.

> **2026-09-29: V18 region fold rejected; generic architecture work remains active.**
> Retained frozen V16 is **83.468136206 s** cold Wall on the full Verilog
> `original_throughput` 6×12 case; the 15 s target remains 68.468 s away.
> V18 admitted 8,644 private signals and avoided 10,267,758 physical
> consumer queue entries/callbacks in a separate full-case diagnostic, while
> retaining one per-signal driver queue entry. It bypassed per-owner staging
> bookkeeping on 26,531,818 writes. The single uninstrumented V18 cold screen
> took **94.447004235 s**, 10.978868029 s slower than V16, so V18 production
> was restored byte-exact to V16. Raw/canonical transcripts and all 57 native
> objects matched V16. The generic semantic fixture remains; V18 source,
> binary, tests, mechanism and cold receipts are archived under
> `build/performance-campaign/verilog-throughput-fast-loop/v18-region-architecture-gate/`.
> Next gate: a generic fused region/kernel or stronger whole-chain elimination
> that removes complete activations/updates while preserving scheduler order,
> phase, delta, and observation semantics. P26 already tried publication-time
> range filtering and was reverted; do not repeat it. No new candidate is
> authorized at that checkpoint. Work is uncommitted; no CI, push, Vivado run or extra cold
> measurement.
# Performance campaign: complete resume instructions

Active checkpoint: September 28, 2026, at committed source
`eb8a50423cdf608ebbe140dd1b642662425402fe`, matching `origin/codex/v3`.
The user has resumed the campaign and switched its target to the full Verilog
`original_throughput` case: **15 seconds total Wall or less**. The completed
simplification and Windows CI repairs are included in this source. No new CI
monitoring or publication is authorized by the resume request.

Opening slice V1 establishes a fresh preferred-ThinLTO cold baseline and a
separate detailed profile before selecting a production change. Evidence goes
under `build/performance-campaign/verilog-throughput-fast-loop/`. The published
168.080-second Verilog observation is historical, not a current baseline.
The full fixture retains six instances and twelve codewords each. The later
mixed-case commands and measurements below are historical context; use the
case-specific V1 launcher and receipt for this resumed loop.

V1 is measured at `eb8a5042`: the exact frozen ThinLTO binary takes
**170.438 seconds cold total Wall** (0.673 compile, 7.041 elaborate, 162.722
native setup/simulation), with all six fingerprints and seven correctness lines
matching. The 15-second target is unmet. The separate profile and route-count
replay are recorded in
`build/performance-campaign/verilog-throughput-fast-loop/v1-attribution.json`.
There were 236.55 million native process resumes, 48.45% single and 51.55%
cohort; 99.82% returned static waits. The hottest shared host costs are
executor resume, external-boundary completion and next-delta scheduling.
The previously rejected operation-extent cache is not being repeated.
V2 is a bounded typed native static-wait completion trial, preserving generic
boundary validation and timeout/hook behavior. Focused Release tests pass 5/5;
its one cold full-throughput observation is **146.515 seconds** (0.673 compile,
6.691 elaborate, 139.148 native setup/simulation), with the same correctness
and all 57 generated native object paths and bytes as V1. The observed
23.923-second reduction is a single observation, larger than the narrow
profiled cold-load ceiling; do not attribute that whole delta to this change
or call it a repeatable gain. The 15-second target is still unmet. V2 is
provisionally retained. Its separate full-throughput profile reports 16,118
simulate samples, 86 unresolved (0.53%), none lost. External-boundary self
samples fell from 7.75% to 0.18%, while executor-resume self rose from 12.83%
to 15.55%; profiled run time increased despite the lower single cold Wall
observation, so the measured Wall delta remains unattributed. V3 is limited
to native completion metadata shared by single and cohort routes: immutable
absence of container aliases and a duplicate single static-wait opcode check.
The case launcher requires explicit `--binary` and `--label`.

The V3 eligibility replay used the unchanged full throughput snapshot with a
temporary diagnostic-only count field. It found 49,712 resuming processes and
236,550,074 native resumes; processes with zero container registers account
for 204,507,106 resumes (86.45%), including 88.65% of single resumes and
84.39% of cohort members. The diagnostic transcript exactly matches V2.
Only a process with container registers can install a container-object alias;
V3 trials a constructor proof to avoid cold alias-list reads for the
ineligible majority, plus a redundant native single-wait opcode read. The
sampled, eligibility-weighted ceiling was a screening bound, not an expected
or measured saving.
The hourly convergence check finds concrete progress from a fresh V1 baseline,
one V2 trial, and a bounded V3 eligibility proof, with no retread of the
rejected operation-extent cache. V3's six focused Release tests pass. Its one
cold full throughput screen is **137.220 seconds total Wall** (0.673 compile,
6.789 elaborate, 129.757 native setup/simulation), with exact V2 stdout,
canonical transcript, and all 57 native object paths and bytes. The 9.295-second
observed difference from V2 is one sample, not a causal or repeatable gain
claim. V3 is provisionally retained. The target is still 122.220 seconds away.
The next dependency is ranking the remaining shared scheduler, cohort, and
executor costs using a separate full-case profile.
The separate V3 full-case profile has 12,238 simulation samples, 79 unresolved
(0.65%) and none lost. Sampled simulation took 125.056 seconds and is not
cold-Wall evidence. Executor-resume self fell to 9.66%; current self costs are
static-cohort execution 7.03%, next-delta wake task 6.78%, and cohort resume
5.19%. Inclusive cohort stacks overlap the generated body and updates, so
their 30.09%/19.29% shares are not removable budgets. A diagnostic-only replay
observed 3,451,363 cohort resume attempts, 3,450,723 cache hits, and full
execution of all 121,949,776 submitted members with static-wait statuses.
This does not measure full registered-cohort readiness. Dynamic process
membership and deferred executor installation require invalidation machinery
for a prepared-cohort cache, while its isolated grouping/entry ceiling is
similar to the observed next-delta guard. The next source trial waits on the
existing hot dynamic-wait flag's lifecycle proof; no persistent cohort cache
has been added.
V4 is the next bounded trial, chosen from the current 6.78% next-delta task
self samples. The dynamic-wait guard's cold branch accounts for about 4.92%
of all V3 samples, an upper bound rather than a promised Wall saving. All
dynamic-registration writers set the existing hot `waiting_on_signal` flag
before recording cold state; fresh fork children start with neither, and the
only early clear in process completion has been removed. Cleanup now clears
the flag after removing signal and container registrations, so the common
guard needs no cold reads. A killed `WaitOn`/`WaitPla` child regression and
the six focused Release tests pass. The preferred ThinLTO binary's one cold
full-case screen takes **134.270 seconds total Wall** (0.673 compile, 6.639
elaborate, 126.955 native setup/simulation). Its canonical transcript, raw
stdout, and all 57 native object paths and bytes match V3. The observed
2.950-second difference from V3 is one sample, not a causal or repeatable gain
claim. V4 is provisionally retained; the 15-second target is still 119.270
seconds away. The separate V4 full-case profile has 12,930 simulation samples,
88 unresolved (0.68%) and none lost. Generated JIT self accounts for 19.52%
of samples; the seven hottest related process symbols account for 4.61%.
Host self remains led by executor resume (11.09%), singleton execute (7.83%),
static-cohort execute (6.46%), static value notification (4.89%), and cohort
resume (4.88%). These are sampled diagnostic shares, not cold-Wall timings or
removable budgets. The next dependency is identifying a materially larger
supported contraction using both generated and host costs.
The bounded V5 trial moved the live interpreter-operation tracking flag
from cold process state into an existing padding byte of hot process state.
Clang 22 confirms the record remains 72 bytes. A mid-resume public API
tracking witness and five related focused tests pass (6/6). Its one cold
full-case screen took **137.390 seconds total Wall** (130.077 native), versus
V4's 134.270 seconds; the transcript and all 57 native objects match exactly.
The single observation does not support retention, so V5 source and test were
reverted while its evidence and frozen binary remain archived. The sampled
branch near the old cold read likely included call latency and supplied no
reliable speedup ceiling. At the hourly convergence check, V1 through V4 moved
the single observed cold total from 170.438 to 134.270 seconds, with each
trial separately screened and no repeatability claim. The 15-second target
remains unmet; the next design investigation is a compile-proven pure
deferred-assignment cohort fusion with current ordered binding and fallback,
without a new persistent runtime cohort cache.

V6's broad exact-SimIR classifier found 97,037,120 members in wholly admitted
offered spans (79.57% of cohort member traffic), but its first strict order
matched a synthetic fixture rather than the actual throughput processes. Its
one cold screen took 170.949 seconds; a separate profile materialized no fused
helper. V6 is retained as a failed admission diagnostic, not a speedup claim.
A construction-only dump established the actual authored process order
`DDREREBWSJ`; the diagnostic source was restored. V7 specializes only that
exact order, retaining the generic fallback and per-member frame, result,
driver and scheduler semantics. Focused compiler/application tests pass 3/3.
Its one full cold observation is **129.308 seconds total Wall** (0.673 compile,
6.540 elaborate, 122.093 native setup/simulation), 4.963 seconds below V4's
single observation. Raw stdout, canonical transcript, and all 57 native object
paths and bytes match V4. A separate full profile has 12,157 simulation
samples, 87 unresolved (0.72%), none lost, and one materialized fused JIT
helper with 311 self samples (2.56%). The fused route therefore ran, but the
single Wall delta is not a repeatable or causal speedup claim. V7 is
provisionally retained; the target remains **114.308 seconds away**. Current
host self costs include cohort resume 11.38%, cohort execution 6.79%, and
the fused preflight 4.52%. Rank truly removable work before another trial;
inclusive call paths overlap generated execution and update commits. Evidence
is in `build/performance-campaign/verilog-throughput-fast-loop/v7-pure-cohort/`.

V8 hoisted cohort context and immutable native binding checks. Its four
focused tests pass, but its single cold full-case screen took **155.867
seconds** (148.626 native setup/simulation), 26.560 seconds above V7. A
separate diagnostic profile took 121.796 seconds and confirmed the fused
helper ran; it does not explain the cold/profile discrepancy or establish a
repeatable V8 gain. V8 remains **unqualified as a standalone optimization**.
The frozen V7 binary remains the retained comparison baseline. V8's context
certificate is being tested only as a prerequisite for a compact pure-cohort
path; no V8 speedup is claimed.

A construction-only replay of the unchanged full saved throughput snapshot
found 540 exact pure registered cohorts, containing 34,560 members. Every
member in those cohorts has no debug locals, and every cohort shares the same
actual two input signal IDs across its members. This establishes static
eligibility, not activation frequency. The temporary diagnostic source was
restored byte for byte. V9 binds this exact fully transient, common-input
family to a compact native helper while preserving the ordered deferred update
commit and generic fallbacks. Four focused compiler/application/runtime tests
pass. Its one cold full-case screen took **111.349 seconds total Wall** (0.672
compile, 6.439 elaborate, 104.235 native setup/simulation), 17.959 seconds
below the retained V7 observation. Raw and canonical transcripts, simulation
stdout, and all 57 native object paths and bytes match V7. A separate full
profile materialized and sampled one compact helper, with no old full fused
helper: 357 of 10,229 simulation samples (3.49%), 76 unresolved (0.74%) and
none lost. Instrumented setup was 0.586 seconds and run 102.455 seconds; this
is diagnostic, not another cold Wall observation. V9 is provisionally retained
without a causal or repeatable speedup claim. The 15-second target remains
**96.349 seconds away**. Evidence is under
`build/performance-campaign/verilog-throughput-fast-loop/v9-compact-cohort/`.
Current self costs are singleton runtime execute 8.17%, cohort execute 6.94%,
singleton app resume 5.55%, static notification 4.97%, and cohort app resume
4.42%. Inclusive stacks contain nested native work and update commits, so
they are not additive removable budgets. Re-rank the largest supported
opportunity using the current profile before another source trial.

The next architecture gate joined the saved V9 design with the earlier
per-ProcessId full-throughput count run; their serialized runtime payloads
are byte-identical. An existing read-only topology reader and a bounded
ordered-operation reader found five recurring singleton signatures covering
110,927,832 of 114,600,298 single resumes. One 31-operation branching
reducer accounts for 83,154,246 of them. Its 3,780 processes form seven
15-bit stages on each of 540 disjoint-driver, 120-bit `red` signals; each
stage reads the adjacent higher slice and queues its own slice update. The
existing compact AND family adds about 97 million cohort resumes to the
potential V10 scope. These are activity counts, not removable-time claims.
The [architecture-gate receipt](../build/performance-campaign/verilog-throughput-fast-loop/v9-architecture-gate/architecture-gate-receipt.json)
preserves reader commands, source/output hashes, exact count/design joins,
the ordered CFG, and the unsupported-container boundary. Prior P25/P26
already studied repeated inputs and optional static-array sensitivity; V10
must preserve every activation and logical delta rather than repeat that
deferred sensitivity filter. V10 implemented whole-task ordered pure-wave
execution with a generic pre-mutation fallback. Its four focused checks and
exact output/native-object parity passed, but its one cold observation was
257.033 seconds. A separate diagnostic profile showed zero pure-wave
executions: the compiler had rejected a private result buffer that remains
uninitialized after ordinary singleton resumes. V11 removed that irrelevant
bind check. A saved-snapshot witness reached the route and a separate full
profile counted 193.440 million completed pure-wave members, yet its cold
observation was 200.432 seconds. Neither V10 nor V11 is retained as a timing
improvement.

V12 reuses bound member leases, dispatches an ordered pointer span without a
per-wave heap binding, and constructs per-process execution contexts only if
the existing ordered update staging declines. Its four focused checks pass.
A saved-full-snapshot diagnostic confirmed one committed pure-wave task before
the diagnostic was removed byte for byte. The frozen uninstrumented binary's
one full cold observation is **144.645 seconds total Wall** (0.622 compile,
6.691 elaborate, 137.312 native setup/simulation). All 16 inputs, six
summaries, and seven correctness lines match; its canonical transcript and all
57 native-object paths and bytes equal V9. V12 is **unqualified** because that
single observation is 33.296 seconds slower than retained V9. Its separate
full-case diagnostic profile confirmed 193.440 million completed pure-wave
members, 97.919 million completed tasks, 12.462 million direct dispatches,
and no direct declines or lazy flushes. The instrumented simulate phase took
121.599 seconds; its 11,874 self samples had 91 unresolved (0.77%) and none
lost. Runtime pure-wave collection accounts for 17.73% self, app admission
10.85%, app buffered-update checking 4.35%, and all five mapped pure-wave
helpers together 5.12%. These sampled shares are diagnostic and are not
additive with inclusive call paths. Saved instruction annotation places about
77% of runtime collection's local samples around cold
`state.program().operations.size()` loads, with sampling skid and dependent
loads limiting precise attribution. At the 05:40 UTC convergence check, the
shortest next dependency is proving immutable operation-count and app storage
certificate lifetimes before selecting one bounded source trial. V9 remains
the best retained observation at 111.349 seconds, **96.349 seconds above the
target**. The 193.45-million member activity count is an opportunity measure,
not a timing projection. V12 evidence is in
`build/performance-campaign/verilog-throughput-fast-loop/v12-stable-pure-wave/`.

V13 is the bounded follow-up to that profile. It stores the immutable
6/7/10/31-operation eligibility bit in the hot runtime process record and
reuses a bound member's private warm-state proof until a non-pure mutation
invalidates it. The external storage, hook, and writer certificate remains
live on every offered wave. A redirect/reentry timeline regression and the
four affected Release tests pass; source-only review found no missing
invalidation entrance. The preferred ThinLTO binary has been rebuilt, and
its one full cold `original_throughput` observation is **109.959 seconds total
Wall** (0.673 compile, 6.541 elaborate, 102.743 native setup/simulation).
All 16 inputs, six summaries, seven correctness lines, canonical and raw
transcripts, and all 57 native-object paths and bytes match V9. V13 is a
provisional best observation, 1.390 seconds below V9 and still 94.959 seconds
above the target. This single small difference does not establish a repeatable
or causal gain. Its separate full-case profile completed 193.440 million
pure-wave members: 193.393 million reused a warm binding, and 46,980 ran the
full warm scan. The sampled runtime pure wrapper is 12.39% self, down from
V12's 17.73%; app admission is still 10.86% self, close to V12's 10.85%.
Scheduler execution is 52.67% inclusive, including 40.52% pure wave, 7.40%
generic cohort, and 4.68% ordinary singleton; update commit is another
19.52% inclusive. These overlapping sampled paths and one cold observation
cannot justify a causal speedup. The pure wrapper's entire inclusive share
is below the remaining target gap, so another small certificate change is
not a target-scale path. V9 remains the preserved fallback while the next
architecture decision uses this attribution.

At the 06:40 UTC convergence check, V13's 109.959-second result remains one
provisional observation against the preserved V9 111.349-second fallback;
the 15-second goal is still 94.959 seconds away. V10 through V13 mostly
recovered the overhead of the broader pure-wave route, rather than delivering
a target-scale gain. The next bounded direction is one prepared member-record
stream from runtime admission through ordered update staging, with explicit
owner and executor invalidation. It keeps the 72-byte hot process record and
the existing logical deltas and commit rules. This is an architecture gate,
not a predicted route to 15 seconds. It does not repeat the rejected P33
unchanged-value projection or P38 operation-extent cache.

V14 replaces repeated pure-wave entry, lease-pointer, and update-batch
construction with one prepared member-record stream. The four affected Release
tests and both translation-unit structure checks pass. Its one cold full
`original_throughput` observation is **95.107 seconds total Wall** (0.673
compile, 6.490 elaborate, 87.943 native setup/simulation), 14.852 seconds
below V13's single observation. All 16 inputs, six summaries, seven
correctness lines, raw and canonical transcripts, and 57 native-object paths
and bytes match V13. V14 is provisionally retained as the best single
observation; the target remains **80.107 seconds away**. The difference is
not yet a repeatable or causal gain. V9 and V13 binaries and receipts remain
preserved. A separate full-case diagnostic profile is the next gate before
another optimization decision. That profile passed the same full fixture and
counted 193.440 million completed prepared members across 97.919 million
tasks, with 193.393 million cached-record reuses, 46,980 successful first
bindings, zero dispatch declines, and zero lazy flushes. Of 8,328 simulation
samples, 72 are unresolved (0.865%) and none lost. The runtime prepared-wave
collector is 12.79% self / 32.83% inclusive; prepared staging is 5.14% self /
14.89% inclusive, compiler preflight 4.36% self, and mapped pure JIT helpers
7.78% self. The app dispatch wrapper is only 0.07% self. Commit and generic
execution remain substantial overlapping paths. These sampled shares are
diagnostic, not cold-Wall savings. The diagnostic source and launcher were
restored byte for byte; its evidence is under the V14 directory.

At the 07:40 UTC convergence check, V14 is the provisional best single cold
observation at 95.107 seconds, down from V13's 109.959 seconds, while the
15-second goal remains 80.107 seconds away. The full saved-snapshot staging
diagnostic reproduces 97.919 million completed tasks and 193.440 million
prepared members with exact transcript and native-object parity. Every
prepared member activates exactly one slot; all use the owned-driver route,
with 155.120 million unchanged and 38.320 million changed updates. There are
no prepared-stage fallbacks. This is workload eligibility, not a measured
saving. The next bounded dependency is a source-reviewed selected owned-slot
staging contract that preserves live ownership, force, transaction, phase,
driver, notification, and commit semantics; no V15 production edit or timing
claim had yet been made at that gate. The replay receipt is in the V14
evidence directory.

V15 now implements the bounded selected owned-slot staging trial. Each
executor cold-certifies the exact one-slot batch, stable owned span, and at
most two word masks; hot staging still checks live ownership and current
phase/value, consumes each slot in scheduler order, and continues through the
ordinary per-slot route if ownership demotes. The shared update commit is
scheduled once. The four focused Release tests pass, including a width-120
offset-60/105 owned-span parity and live out-of-owned-mask demotion witness;
both translation-unit structure checks pass. Source/style checks and graph
index are current. The preferred ThinLTO build and one full cold throughput
screen were the remaining V15 qualification steps. One full cold screen took
**93.051 seconds total Wall** (0.672 compile, 6.641 elaborate, 85.736 native
setup/simulation), 2.056 seconds below V14's single observation. All 16
inputs, six summaries, seven correctness lines, raw and canonical transcripts,
and all 57 native-object paths and bytes match V14. This is provisional
screening progress, not a repeatability or causal claim; the 15-second goal
remains **78.051 seconds away** and the frozen V14 binary remains available.
The separate full-case diagnostic profile confirmed the selected owned-slot
route for all 193.440 million prepared members: 155.120 million unchanged,
38.320 million changed, and no live demotion or whole-wave fallback. All
46,980 cold owned-slot certificates succeeded. The profile has 8,481
simulation samples, 80 unresolved (0.943%) and none lost. Runtime collection
is 12.58% self, prepared staging 4.88%, owned-slot staging 3.87%, compiler
preflight 4.26%, and mapped pure-wave JIT 7.53%; inclusive paths overlap.
Diagnostic source and launcher edits were restored byte for byte. Exact V15
receipts and both frozen binaries are under
`build/performance-campaign/verilog-throughput-fast-loop/v15-prepared-owned-slot/`.

At the 08:40 UTC convergence checkpoint on September 29, V15 remained the
best single cold observation at 93.051 seconds, still 78.051 seconds above
the target. The shortest dependency was a bounded address-layout probe before
another architecture choice. That saved-full-snapshot probe has since recorded
46,980 distinct first bindings, exactly the V15 certificate count, then
intentionally exited 77 before simulation output. Its design snapshot and
index match frozen V15 byte for byte. The app hot fields occupy 47,545
distinct 4 KiB virtual pages when known object spans and minimum nonempty
vector spans are included; the compiler's released flag, kernel member and
prepared view occupy 6,987. These are one-time address footprints, not
activation-weighted working sets or measured cache/TLB misses. The two
temporary diagnostic source edits were restored byte for byte to the frozen
V15 manifest; the separate `.orig` file was preserved. The reproducible
[layout receipt](../build/performance-campaign/verilog-throughput-fast-loop/v15-layout-diagnostic/layout-receipt.json)
and analysis retain exact hashes and distributions. The next architecture
gate selected a bounded app-owned stable-cell pool for eligible original
single-slot Logic4 executors, with legacy fork and ineligible paths.

V16 implements that pool with fixed-address 128-cell blocks, one direct
update slot/view/bitmap and up to two wide words in each cell. Fork clones
use separate fallback storage. A 129-instance compiled/interpreter parity
witness crosses a block boundary; the four affected Release tests, structure
check and selftest pass. Its frozen preferred ThinLTO binary
`v16-hot-cell/fsim-v16` (SHA-256 `2148d8c69f5f78c8`) took **83.468 seconds
total cold Wall** (0.622 compile, 6.491 elaborate, 76.354 native), versus
V15's separate 93.051-second observation. All 16 inputs, six summaries,
seven correctness lines, raw and canonical transcripts, and all 57 native
object paths and bytes match V15. V16 is provisionally retained, without a
causal or repeatability claim. The 15-second target remains **68.468 seconds
away**. See the [cold-screen receipt](../build/performance-campaign/verilog-throughput-fast-loop/v16-hot-cell/cold-screen-receipt.json).

The separate full-case V16 diagnostic profile counted 193,439,732 prepared
members and 12,462,215 direct dispatches, with 38,320,161 owned-slot changes,
155,119,571 unchanged, and no demotion, decline or fallback. Its 7,543
simulation CPU samples include 84 unresolved (1.11%) and none lost. Runtime
collection is 11.75% self / 26.90% inclusive, static notification 5.89%
self, compiler preflight 4.56% self / 4.91% inclusive, cohort next-delta
queueing 4.20% self, owned staging 3.21% self / 7.37% inclusive, and
prepared-stage scanning 1.99% self / 9.60% inclusive. Inclusive paths overlap;
none is a removable-Wall estimate. Saved instruction annotation puts 738 of
886 collector self samples before the indirect app dispatch setup; 225 fall
on a process-state branch, 137 on a prepared-slot flag branch, and 91 on a
current-PC versus prepared-resume-instruction compare. Compiler preflight
samples concentrate at the released-flag check and view/kernel pointer
loads. Notification and cohort queue annotations show concentrated per-edge
and ready-scan branches. Sampling skid and missing source-line debug mapping
limit field attribution; these are not measured cache misses. The profile's
raw/canonical transcripts and all 57 native objects match the cold screen.
Its temporary source and launcher counters were restored byte for byte.
The preferred build after restoration passed, but its ELF differs from the
frozen screened binary despite exact frozen source and CMakeCache hashes;
that rebuilt binary is unmeasured. The frozen screened V16 binary remains the
comparison artifact. The [profile receipt](../build/performance-campaign/verilog-throughput-fast-loop/v16-hot-cell/diagnostic-profile-receipt.json)
preserves counters, annotation and hashes. At the 09:40 UTC convergence
checkpoint, V16 remains the best single observation at 83.468 seconds, still
68.468 seconds above the target. A proposed grouped fanout/ready-scan fusion
is held: it would move all trigger-mask stores before the first queue, changing
the prefix visible if snapshot acquisition or scheduling throws. No V17
production edit has been made. The shortest next dependency is a source and
lifetime proof for moving the frame-owner `shared_ptr` out of the 72-byte
hot `ProcessState` while retaining its raw frame pointer, to test whether a
64-byte record is safely attainable. That source gate has now passed for two
frame-assignment writers, the fork owner copy, deferred promotion and teardown
order. V17 moves the strong frame owner into cold state, retains a raw hot
frame pointer, and deletes move assignment to protect destruction order.
Compile-time checks prove a 64-byte, 64-aligned hot record. Optimized
`get_process` uses an eight-entry deque block and power-of-two shifts in place
of frozen V16's seven-entry block and reciprocal division. The unshared
deferred handoff and a new post-fork shared-frame promotion witness pass;
all six affected Release/structure checks pass. Yet the frozen preferred
ThinLTO V17 binary took **88.595 seconds total cold Wall** (0.673 compile,
6.893 elaborate, 81.027 native), 5.127 seconds above V16's separate single
observation. Raw/canonical transcripts, simulation stdout, and all 57 native
objects match V16 byte for byte. This one screen does not prove a causal or
repeatable regression, but it supports no end-to-end gain, so V17 is
**rejected**. Its four production files were restored byte for byte to frozen
V16; the new shared-frame regression test remains and the focused runtime
test passes after a 12-worker rebuild. Frozen V17 binary, source patch,
mechanism assembly and [cold receipt](../build/performance-campaign/verilog-throughput-fast-loop/v17-frame-owner/cold-screen-receipt.json)
remain archived. V16 is the retained best observation at 83.468 seconds,
still 68.468 seconds above the goal. The next bounded measurement is one
full-case CPU-clock profile of the frozen uninstrumented V16 binary, showing
all phase Walls and a disjoint sampled-self histogram before another source
choice. Profiled time and CPU samples must not be presented as another cold
Wall observation or as removable-time budgets.

## Scope and user decisions

- The active optimization direction is the **full Verilog `original_throughput`**
  workload, targeting **15 seconds or less total Wall** for compile,
  elaboration, native setup, and simulation. The target remains unmet.
- On an explicit resume, profile in detail, attack the largest supported
  host cost, and repeat. One cold uninstrumented fsim observation per
  candidate is sufficient for this loop. Do not automatically restart
  Vivado comparisons, repeated pairs, reduced-transfer runs, or all-ten
  qualification on each iteration.
- Earlier priority was the longest mixed elaboration case, then update and
  scheduler paths. Re-rank using current absolute phase/call-path costs:
  native setup/simulation is now larger than elaboration. Avoid another
  series of speculative tiny changes.
- Retain performance-neutral changes that simplify the implementation.
  Added caches, state, and branches require supported benefit; neutrality
  alone does not justify added complexity.
- The user accepted the small Codex native-phase regressions in the final
  matrix and stopped further investigation. Do not reopen that work as a
  prerequisite to resuming the Verilog-throughput campaign.
- Preserve external RTL bytes. Use explicit fsim VHDL compatibility
  `legacy-unprotected-shared-variable` for mixed cases, with the repaired
  historical baseline labeled separately from pristine `d11004e4`.
- Both simulators use one CPU, O2 JIT, cold artifact/native caches, and no
  waves or interactive debugging. More CPUs cannot satisfy the target.
- **Do not monitor CI**, per the user's instruction. The existing failing
  `6efa4c35` run was inspected and repaired in `eb8a5042`; its replacement
  was deliberately not monitored. That commit/push request is fulfilled
  and is not blanket permission to publish future change sets.
- The older all-ten lower-median/seven-interleaved-pair objective and the
  historical frozen-baseline program remain deferred, not completed.

## First actions in a fresh session

1. Work in `/home/colin/projects/fsim`. Read this file, the top of
   [v3-resume.md](v3-resume.md), the
   [published matrix](performance-matrix-2026-09-28.md), and the current
   [implementation plan](implementation_plan_v3.md). Live files and the
   latest user instruction override old checkpoints.
2. Check `git status --short --branch`, `git rev-parse HEAD`, and
   `git rev-parse origin/codex/v3`. Last verified source commit and tracking
   ref were both `eb8a5042`. Preserve any newer user changes. The handoff
   documentation itself may still be uncommitted; inspect before acting.
3. Preserve `.codebase-memory/`, `phase.fst`, `scripts/__pycache__/`, and
   ignored `build/` evidence. They are not campaign commit content. Never
   use blanket reset, clean, or add commands to tidy them.
4. For structural exploration read the installed codebase-memory skill
   (`/home/colin/.claude/skills/codebase-memory/SKILL.md` in this workspace).
   Use `list_projects`/`index_status`, then graph queries, exact snippets,
   and cited-path coverage checks. The project is
   **`home-colin-projects-fsim`**, not the stale `fsim` project. Read source
   directly for parser gaps. Benchmark artifacts under `build/` are
   deliberately excluded and should be read from disk.
5. Inspect live agents and active executable jobs. Agent names in an old
   transcript are historical, not proof that those workers are available.
   Create the team below if needed. Do not launch measurements until Sol
   owns a quiet execution slot.
6. Verify the recorded binary, dependencies, external inputs, and fixture
   hashes before reusing evidence. If local ignored evidence is absent,
   reconstruct from tracked runners/manifest with new identities; do not
   claim an old measurement was reproduced.
7. If the user has resumed optimization, the first bounded action is a
   detailed profile of the current committed full Verilog throughput case, followed by
   an architect decision. Do not start by implementing a speculative fix.
   If the user only requested a handoff, stop after saving it.

## Worker setup and use

The agreed campaign team is a root architect, **GPT-6-Sol / high** for
execution and integration, and **up to five GPT-6-Luna / max** workers.
That is at most six delegated agents total. Nested workers count against
the same cap; do not multiply the team by nesting. Use fewer workers when
there is no useful independent task. Check live tool capacity first.

Root owns profile interpretation, ranking by credible absolute Wall savings,
bounded architecture decisions, acceptance review, and user reporting.
Sol owns source integration, builds, tests, profiles, measurements, receipts,
and checkpoint updates. Luna workers implement narrowly owned changes or
independently audit semantics, runtime behavior, and evidence. Sol takes
over a worker's change when the worker enters a debug loop. Root can cover
temporary service limits, then gives executable/implementation work back
to Sol when service returns, as explicitly requested by the user.

With the collaboration tools used in this campaign, launch examples are:

```json
{
  "task_name": "performance_execution",
  "model": "gpt-6-sol",
  "reasoning_effort": "high",
  "fork_turns": "none",
  "message": "Read docs/performance-resume.md and verify live state. Own implementation, builds, tests, profiles and timing. The loop is paused until explicitly resumed. When resumed, profile the full mixed_codec case first and report a bounded candidate before source edits. Coordinate one exclusive execution slot; delegate only disjoint work within the shared five-Luna limit. Do not monitor CI."
}
```

```json
{
  "task_name": "performance_semantic_audit",
  "model": "gpt-6-luna",
  "reasoning_effort": "max",
  "fork_turns": "none",
  "message": "Read docs/performance-resume.md. Independently audit only the exact candidate scope supplied by the architect. Report guards, semantic risks, tests and evidence gaps. Do not edit files or run builds/tests/profiles/benchmarks unless explicitly assigned by the execution owner. Do not monitor CI or spawn more workers without a slot assignment."
}
```

Use a non-full-history fork (`none` or a small positive turn count) when
setting explicit model/reasoning overrides. Full-history forks inherit the
parent configuration in this tool environment. Set every Luna's model and
reasoning explicitly, including nested workers. Do not silently substitute
the root model. Adapt tool names to the live environment without changing
the agreed roles; a capacity error calls for reusing available agents or
running fewer, not changing configuration or stalling the task.

Every actual task message must also supply: current revision, candidate ID,
owned files, exact boundary and invariants, acceptance checks, evidence
directory, graph project/coverage findings, and prohibited overlapping work.
All agents share a worktree: use disjoint file ownership and review patches
before integration. Reuse idle agents with follow-up tasks; do not create a
new team every iteration. Root approves architecture boundaries, not each
routine command. Give Sol conditional gates so a passing build/test/index
can lead directly to the already-authorized measurement.

Only one owner launches executable work. Finish builds, tests, indexing,
hash scans, and other heavy jobs before timing/profiling. Lightweight
read-only reviews may continue. Workers must explicitly announce acquisition
and release of the execution slot. Capture logs while running, rather than
rerunning a successful check to recover missing evidence.

## Current source, binaries, and qualification

`6efa4c35` contains the retained compiler/elaboration/runtime work through
P96, expanded tests, performance runners, matrix, and packaging repairs.
P100/P101 temporary probes are removed. The older P58 diagnostic admission
switch was also removed. Native cache schema is **v178** and its assertion
matches. Two new production files are `src/runtime/simir_owned_driver.cpp`
and `src/diagnostic/thread_cpu_clock.hpp`. ThinLTO is a preferred local host
build configuration, not a newly imposed repository-wide CMake default.

Preferred measured CLI:

```text
build/performance-campaign/mixed-long-fast-loop/p84-thinlto-host-config/build/fsim
SHA256 706a4c984ee6f76b5d266b56861daede47da69856c0de29643422605388de8d3
```

Do not confuse it with the earlier P96 binary
`a9dfe1e86fee0e372349c6a5201dce3e8e8457b03b4f57ee1ac99e78ff4c92d1`
or P8A control
`ba2d29bfa09edd0a9d637a32b0e6d9afe28674a537bebc640346cfa5df53a741`.
Archive an executable before rebuilding over it, and label every new binary.

Configured build trees:

| Purpose | Directory under `build/` |
| --- | --- |
| Canonical Release | `batch188-release-clang22-final` |
| Debug | `batch188-debug-clang22-final` |
| Tcl-off, LLVM enabled | `batch188k-tcl-off-clang22` |
| Benchmark Release ThinLTO | `performance-campaign/mixed-long-fast-loop/p84-thinlto-host-config/build` |

The benchmark tree uses Clang 22, Release, IPO ON, GNU ld/LLVMgold, and
`-Wl,-plugin-opt,jobs=12` plus its existing `thinlto-cache` directory.
Preserve configured dependency/Tcl settings. Use **at least 12 build workers**.
Host build caches may be reused; simulator artifact/native caches must be
fresh. Read each CMakeCache before configuring instead of guessing options.

Qualification at the committed boundary: Release **454/454**, focused Debug
**7/7**, Tcl-off **6/6**, final packaging/documentation **24/24**. Focused
sets cover semantic, elaboration, runtime, LLVM, structural signal remap,
VHDL Logic9, and (when enabled) Tcl console. Logs are in
`build/performance-campaign/mixed-long-fast-loop/closure-qualification/`.
The schema and packaging failures encountered during closure were repaired.
No unresolved correctness failure is carried forward. Do not rerun these
checks merely because a fresh session started; rerun affected checks after
changes, and the broad suite at the next actual closure.

## Measurement and profile commands

All paths below are relative to the repository unless absolute. Use a unique
label/output directory; never overwrite previous evidence. Commands are
instructions for a resumed loop, not authorization to execute during handoff.

The local fsim-only launcher is
`build/performance-campaign/mixed-long-fast-loop/run_case.py`. Its default
binary is historical P37: **always pass `--binary`**. It checks all 19 mixed
input hashes and the saved full fixture, then requires 75 exact final
fingerprints and 87 correctness lines. The underlying fixture is in
`build/performance-campaign/p35-disjoint-driver-composite/profile-full-mixed_codec-v1/`.
The full workload is not the old shortened 5% diagnostic window.

```sh
python3 build/performance-campaign/mixed-long-fast-loop/run_case.py profile \
  --binary build/performance-campaign/mixed-long-fast-loop/p84-thinlto-host-config/build/fsim \
  --label resume-current-profile

python3 build/performance-campaign/mixed-long-fast-loop/run_case.py wall \
  --binary build/performance-campaign/mixed-long-fast-loop/p84-thinlto-host-config/build/fsim \
  --label next-approved-candidate
```

Run these sequentially in the same permitted outside-sandbox execution
context used for the matrix (`sandbox_permissions=require_escalated` in
the shell tool when required). The launcher strips `FSIM_*` environment
flags, pins CPU 0, sets C locale/UTC and frozen dependencies, and creates
cold workspaces. Its profile and wall outputs are separate. Do not use its
stale experimental switches such as `--skip-giant11`,
`--large-unshared-interpreted`, or `--synchronize-recurring` in normal runs.
Some switches refer to removed probes and others alter execution policy.

For a native-only profile use `native-profile --reuse-workspace <verified
elaborated-workspace>` with an explicit binary/label; this copies the saved
snapshot and clears native caches. Confirm source/artifact/configuration
compatibility first. Such a profile omits compile/elaborate and cannot supply
total Wall. `--external-only` avoids internal profile counters for this mode.
Per-thread sampled call paths and JIT symbol maps matter: LLVM worker frames
can be absent from aggregate reports, and some JIT leaves lack callers.
Report lost/unresolved samples and instrumentation effects. Use bounded
Callgrind when sampling cannot resolve a material cost; do not use
instrumented elapsed time to claim improvement.

The full matrix launcher is the local
`build/performance-campaign/matrix-closure-20260928/run_matrix.py`:

```sh
python3 build/performance-campaign/matrix-closure-20260928/run_matrix.py \
  --fsim build/performance-campaign/mixed-long-fast-loop/p84-thinlto-host-config/build/fsim \
  --output build/performance-campaign/new-matrix-unique-label \
  --provenance 'actual revision, dirty patch identity, compiler and host configuration'
```

This is **not** part of each fast-loop iteration. It invokes tracked
`scripts/perf_campaign.py` with `scripts/simplification_benchmarks.json`,
`--prepare-full`, O2, seed `0x6d2b79f5`, one sample/tool/case, and the mixed
compatibility option. Both tools must have matching outside-sandbox launch
context. Vivado Tcl startup failed inside the sandbox before HDL execution;
the abandoned matrix `round-1` has no valid timing comparison.

Shared matrix environment is C locale/UTC and
`LD_LIBRARY_PATH=/home/colin/projects/fsim/build/performance-campaign/frozen-runtime-deps-d11004e4-p5-comparison:/opt/thinlinc/lib64:/opt/thinlinc/lib`.
The fsim-only launcher uses the same frozen dependency directory without the
ThinLinc suffix; retain and record the environment appropriate to the run.
Do not mix those contexts silently. Record source, generated-fixture,
binary, dependency, configuration and cache identities before/after runs.

For cross-simulator comparisons, require complete canonical transcript
preflight parity before timing, then exact final counts/fingerprints and
normalized correctness. Original/mixed throughput correctness line ordering
can differ across tools; canonical contents must agree. A runner exit 2 can
mean valid evidence with `performance_gate_failed`, not a simulator failure.
For a per-case campaign, check `workload_identity[case].verified`; the global
all-ten `original_workloads_verified` field is false for a one-case report.
Wall includes all phase and launch costs. RSS is the maximum phase GNU Time
`%M`, not the sum or a live process-tree aggregate. One sample has no useful
dispersion estimate. O0 remains a diagnostic/correctness configuration; AOT
experiments must be labeled separately and include their compilation costs.

## Current evidence and next opportunity selection

The [matrix report](performance-matrix-2026-09-28.md) is the durable table.
Full local evidence is `build/performance-campaign/matrix-closure-20260928/`:

- `round-2/`: all ten passed full transcript, timed fingerprint and
  correctness parity; `wall-rss-rows.{json,csv}`, `matrix-receipt.json`,
  per-case manifests/reports, and `independent-audit.{md,json}`.
- `qualified-source.{json,patch}` and `commit-preparation.json`: source and
  binary identities, staged path hashes, validations and indexing.
- `commit-push-receipt.json`: completed push of `6efa4c35`; CI not monitored.
- `runtime-review.md`, `semantic-review.md`, `compiler-cleanup-review.md`:
  independent source audits, no confirmed correctness blocker.
- `matched-short-control-round1/` and `matched-native-profile-round1/`:
  accepted minor Codex regressions. Do not spend the next loop on them.

The fresh mixed-codec matrix observation is **32.933 s / 776.5 MiB**:
compile 1.345 s, elaborate 7.292 s, native/setup/simulate 24.294 s.
The earlier P96 observation was **32.579 s** (1.343 / 7.341 / 23.892).
These are distinct single observations, not medians or a causal comparison.

Under `build/performance-campaign/mixed-long-fast-loop/`, read:

| Evidence | Meaning |
| --- | --- |
| `p96-exact-initializer-memo/profile-ranking.json` | Last broad all-phase ranking: optimization 4.879 CPU s across 308 modules, including InstCombine 2.918 and EarlyCSE 0.892; lowering 0.700, verify 0.546. Costs are not removable-gain estimates. |
| `p97-simulation-attribution/report.json` | Main Scheduler callers: 262 static-cohort, 144 update commit, 140 single-execute, 57 other/self; 294 JIT leaves lack callers. Interpreter interval includes concurrent adaptive LLVM work. |
| `p98-giant-call-ir/report.json` | Offline alias annotation reduced IR size but did not improve production-like compilation; rejected. |
| `p99-register-lifetime-census/report.json` | Public frame/Fork semantics sharply limit register elision; no default-ABI change justified. |
| `p100-giant-admission-screen/rejection-receipt.json` | Skipping seven giant modules regressed full Wall to 39.153 s; source restored. |
| `p101-vm-opcode-census/diagnostic-receipt.json` | Opcode counts are frequencies, not time savings; temporary instrumentation removed. |
| `p65-scheduler-callgrind/` and `p91-second-window-callgrind/` | Bounded scheduler evidence; inspect each receipt for collection scope and thread limitations. |

After refreshing attribution on the committed binary, rank aggregate LLVM
lowering/optimization/codegen and scheduler/update opportunities by applicable
workload frequency and credible absolute saving. Do not add inclusive call
paths together. A new diagnostic classifier needs proof on an actual lowered
benchmark process before extensive synthetic tests.

Keep [design-specific-jit-scheduling.md](design-specific-jit-scheduling.md)
on the opportunity list: specialize a proven hot cohort/update family while
retaining generic scheduling and required fallbacks. It is an opportunity,
not an approved implementation. Single-driver designs should use proven
ownership/disjoint projection fast paths; preserve resolved behavior for
multi-driver, force, external access, hooks, and Logic9 cases. Do not weaken
debug/coverage/observability implicitly. Any restriction needs an explicit
mode and compatibility decision. Do not assume every process is native:
check actual admission and native coverage, interpreter/O0/O2 equivalence,
and callback/frame semantics for the proposed boundary.

## Per-candidate and closure discipline

1. Architect writes one boundary: evidence, expected absolute gain ceiling,
   invariants, owned files, semantic checks, and acceptance/rejection rule.
2. Sol delegates disjoint implementation/review, integrates, and builds only
   affected Release targets with `--parallel 12`. Derive test targets from
   CTest metadata, not guessed executable names. Fix routine failures within
   scope without another approval round.
3. Run meaningful affected tests, including relevant mixed-language,
   scheduler/value-state/artifact and interpreter/O0/O2 behavior. Finish all
   executable checks. Manually call `index_repository` with
   `repo_path=/home/colin/projects/fsim`, `name=home-colin-projects-fsim`,
   `mode=full`, `persistence=true` after each integrated source/doc batch.
4. Freeze identities, acquire the quiet slot, take one cold full wall sample,
   and require exact saved correctness/fingerprints. Profile separately.
   Root decides retain/reject; restore only candidate-owned hunks on rejection
   and rebuild stale candidate objects before reuse. Preserve all evidence.
5. Update campaign, implementation plan, and resume state together. Record
   exact retained binary, candidate decision, next bounded action and risks.
   Audit net progress hourly; if work churns, shorten the remaining dependency.
6. At closure rescan CMake, build all Release targets with at least 12 workers,
   run the full suite and focused Debug/Tcl-off checks. Include new intended
   source/docs in the Git-based package manifest; exclude generated artifacts.
   Run affected packaging checks, reindex, and `git diff --check`.
7. Commit/push only within the user-authorized boundary. Do not monitor CI
   unless the user changes that instruction. Report the under-15 target and
   deferred all-ten qualification honestly; do not mark either achieved based
   on this matrix or a partial profile.

The original deterministic reduced corpus, xorshift known-answer checks,
separate random streams, handshake-edge rules, seven-pair qualification, and
historical campaign decisions remain documented in
[performance-campaign.md](performance-campaign.md). Their older reproduction
examples are historical; this handoff takes precedence for current defaults.
