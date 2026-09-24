<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Authority and live state

Read [implementation_plan_v3.md](implementation_plan_v3.md), especially
Batches 188G-188I and Standard Change 20 closure, before acting. Verify the
branch, HEAD, tracking ref, worktree, active agents, and any running build or
test handle; this checkpoint is a locator, not a substitute for live evidence.

The commit containing this checkpoint closes Batch 188F on `codex/v3` and
hands off to Batch 188G Change 1. Its parent was Batch 188E head
`2dca7f742f310509bf7e67c3aff8036efd7bd243`; verify the new local and
tracking HEAD live. The user-owned untracked `phase.fst` and
`scripts/__pycache__/` must be preserved. Do not clean, reset, commit, or
package them.

The latest applicable hosted run inspected before the Batch 188F push was
`35997982063` at Batch 188E head `2dca7f74`. All four LLVM-only
Linux/Windows Debug/Release lanes passed. The newly pushed Batch 188F run
is unverified under the user's post-push monitoring waiver.

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

## Batch 188F closure and next action

All twenty Batch 188F changes are marked complete in the plan. They cover
scheduler cancellation lifetime, streaming trace retention and FST writing,
trace metadata, observer hooks, VPI indexing, and PSL bindings. Focused
Release tests passed 14/14, including a 1,000,001-event FST round trip.
The final FST workspace uses private creation on POSIX and an owner-only
inherited Windows ACL. The corrected VCD governance anchors passed 3/3.

The final-source Clang/LLVM 22 Release build passed 3,079/3,079 steps and
its unfiltered suite passed 421/421. After a CMake dependency rescan, the
Debug build passed 1,927/1,927 pending steps without cleaning and its
unfiltered suite passed 421/421. Both builds used twelve workers;
`git diff --check` passed. The user waived the seven-sample performance
matrix and post-push hosted monitoring for Batch 188F; neither is claimed
as a pass. There is no release tag.

Next: begin Batch 188G Change 1. Worker read-only maps identified scheduler
Changes 1-3/5 as one file-ownership lane, process/fanout changes 6-11 as
staged SIMIR work, and driver/force changes 12-18 as a later shared-state
wave. Keep `simir_internal.hpp` and SIMIR dispatch owners exclusive; freeze
each shared API before client migrations. Mark each plan item complete when
focused validation closes it. Continue through Batch 188I.
