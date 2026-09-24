<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Authority and live state

Read [implementation_plan_v3.md](implementation_plan_v3.md), especially
Batch 188I and Standard Change 20 closure, before acting. Verify the
branch, HEAD, tracking ref, worktree, active agents, and any running build or
test handle; this checkpoint is a locator, not a substitute for live evidence.

Batch 188H is closed by the current `codex/v3` head and pushed to its
tracking ref; verify the exact SHA live. Its full warnings-as-errors Release
and Debug builds passed after CMake dependency rescans, and both full suites
passed 423/423 tests. Logs are `/tmp/fsim-h20-release-{build-final,ctest-green}.log`
and `/tmp/fsim-h20-debug-{build-final,ctest-final}.log`. The previous G-head
hosted run 36046188745 had both Linux LLVM lanes green and both Windows LLVM
lanes failing `fsim.runtime` cancellation payload lifetime. H includes the
swap-based repair, but its new hosted head is unverified under the H monitoring
waiver. Preserve user-owned untracked `phase.fst` and `scripts/__pycache__/`;
do not clean, reset, commit, or package them.

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

## Active Batch 188I checkpoint

Batch 188H Changes 1-20 are marked complete in the plan. Its seven-sample
cumulative performance matrix was explicitly waived, so no performance pass
is claimed. The next bounded action is Batch 188I Change 1: introduce a
project-owned hierarchy path table with stable handles and explicit lifetime
ownership. `ElaboratedDesign` and independently copied or loaded `DesignIr`
must retain their table owner. IDs are local to a table; canonical artifact IDs
must be assigned by sorted path content. Follow with Change 2's heterogeneous
`SourceName` lookup before constructing a string. Preserve the SystemC plugin
C ABI and public string lifetimes through the later migrations. Do not use
`shared_ptr::unique()` on Windows.

Batch 188I requires the full fixed-baseline seven-sample matrix, local
LLVM-enabled ASan/UBSan, full applicable suite and lifetime stress, and four
exact-head hosted LLVM-only Linux/Windows Debug/Release lanes. Mark each plan
item complete as its evidence closes. Do not tag a release.
