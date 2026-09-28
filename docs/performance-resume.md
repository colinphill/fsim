<!-- SPDX-License-Identifier: Apache-2.0 -->
# Performance campaign: complete resume instructions

Checkpoint: September 28, 2026, after commit and push of
`6efa4c357f96bf7d82d0047963e2fefe2ca658f7` to `origin/codex/v3`.
This handoff records the completed work and how to restart. Saving this
handoff does **not** restart the paused optimization loop.

## Scope and user decisions

- The last optimization direction is the **full original `mixed_codec`**
  workload, targeting **less than 15 seconds total Wall** for compile,
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
  prerequisite to resuming the mixed-codec campaign.
- Preserve external RTL bytes. Use explicit fsim VHDL compatibility
  `legacy-unprotected-shared-variable` for mixed cases, with the repaired
  historical baseline labeled separately from pristine `d11004e4`.
- Both simulators use one CPU, O2 JIT, cold artifact/native caches, and no
  waves or interactive debugging. More CPUs cannot satisfy the target.
- **Do not monitor CI**, per the user's latest instruction. Hosted CI for
  `6efa4c35` was deliberately not checked. The commit/push request is
  fulfilled; it is not blanket permission to publish future change sets.
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
   ref were both `6efa4c35`. Preserve any newer user changes. The handoff
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
   detailed profile of the current committed full mixed case, followed by
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
