<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Authority and live state

Read [implementation_plan_v3.md](implementation_plan_v3.md), especially
Batches 188G-188I and Standard Change 20 closure, before acting. Verify the
branch, HEAD, tracking ref, worktree, active agents, and any running build or
test handle; this checkpoint is a locator, not a substitute for live evidence.

The parent of the Batch 188G closure commit is Batch 188F head
`6bdf4d52af2cb998188280e329192a3a87b11500`. Verify the new local and
tracking HEAD live. The user-owned untracked `phase.fst` and
`scripts/__pycache__/` must be preserved. Do not clean, reset, commit, or
package them.

## Fixed baseline and qualification contract

The corrected pre-simplification baseline remains frozen in ignored
`.local-artifacts/188b-corrected-baseline/`, with provenance in
`PROVENANCE.md` and exact source patch/inputs in that directory. Its Release
Clang/LLVM executable SHA-256 is
`3a2bc711e19b939cf4c880abbaa1013ae2769c8dc4ac12dbd53e59e4698ca258`;
the benchmark-helper SHA-256 is
`5df4cea2dccd811f35f91e10d78dfe06e5f1cc4fdcf2007d064bbb45db8683c6`.
The original HEAD/tracking revision for that frozen patch was
`ed5ca693704edd277ec3f055ed7d9ed0e3048f2d`. Baseline one-shot/oracle
and native-profile evidence for all required case/configuration pairs is
under `.local-artifacts/188b-one-shot/evidence/`. These diagnostics do not
qualify a simplification candidate.
For Batch 188I's full matrix, use
`scripts/qualify-simplification-performance.py` with
`scripts/simplification_benchmarks.json` and override the mixed-language root
to `.local-artifacts/rs-vhdl-overlay/source-root`; the manifest's default
project checkout is not the corrected frozen source.

The user's seven-sample cumulative performance waiver applies to Batches
188C-188H; record it at each close and do not claim a performance pass.
The full fixed-baseline matrix resumes at Batch 188I. The user also waived
post-push CI monitoring for Batches 188C-188H, while requiring inspection of
the latest applicable hosted run before each commit/push. Batch 188I requires
four exact-head LLVM-only hosted Linux/Windows Debug/Release lanes and bounded
repair of failures, plus local LLVM-enabled ASan/UBSan, the full applicable
suite, and lifetime stress.

Every Batch 188B-188I has exactly twenty numbered changes. Changes 1-19 use
focused validation; Change 20 owns dependency-rescanned warnings-as-errors
Release first, then Debug and Release full suites, evidence, one cohesive
commit and push, and its applicable performance/hosted gates. Rerun CMake
configuration before each closure build; cleaning is optional. Keep
qualification commands at 120-minute timeouts, local builds at twelve or
more workers, hosted jobs at two workers, and central builds/tests
non-overlapping. There is no release
tag for Batches 188B-188I. Preserve the AST-free compiled-HIR pipeline,
scheduler order, observer mutation semantics, format/ABI compatibility
boundaries, and the fixed baseline. Keep private standards read-only and out
of repository artifacts; use LLDB on Linux and Windows.

## Batch 188G closure and next action

The commit containing this checkpoint closes Batch 188G; all twenty changes
are marked complete in the plan. It includes typed scheduler tasks, compact
hot/cold process and signal records, flat fanout, stable dynamic waits,
incremental switch components, inline-first driver records, narrow resolution,
word force/release, lazy packed mirrors, and reusable cohort scratch. Scheduler
discard/reset/move hooks were repaired after review. The two observer snapshot
`shared_ptr::unique()` calls were changed to `use_count() != 1` for Windows.
No release tag is made.

After CMake dependency rescans, the warnings-as-errors Clang/LLVM 22 Release
build passed 2,345/2,345 steps and Debug passed 1,895/1,895 remaining steps.
The first full Release CTest run had 418 passes, two failures, and one
dependent not-run gate because three new private headers were absent from the
source-package manifest. The manifest was fixed; a targeted rerun passed
19/19 including all three gates and their dependencies. The user authorized
proceeding to Debug without a full Release rerun. The unfiltered Debug suite
passed 421/421, and `git diff --check` passed. Logs are under
`/tmp/fsim-g20-{release,debug}-{build,ctest}.log` and
`/tmp/fsim-g20-release-repair.log`.

The latest applicable hosted run inspected before the G push was CI
36013514908 at the 188F head: Linux Debug/Release passed, Windows
Debug/Release failed on `shared_ptr::unique()`. The new G head is unverified
under the user's post-push monitoring waiver. The seven-sample cumulative
performance matrix was also waived and is not a claimed pass.

Next: begin Batch 188H Change 1. Worker read-only mapping located the
operation alternatives in `simir_operation_storage.hpp`, duplicated cache-key
encoders in `llvm_jit_cache_key_operations_{primary,secondary}.cpp`, and
contextual signal-width dependencies that the canonical encoder must retain.
Use explicit stable operation tags, typed semantic leaves, compile-time
exhaustiveness, and the native-cache schema bump without changing unrelated
metadata format. Avoid `shared_ptr::unique()` on Windows. Continue through
Batch 188I under the plan.
