<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Current checkpoint

Read [implementation_plan_v3.md](implementation_plan_v3.md), especially
Batch 188I and Standard Change 20. Verify branch, HEAD, tracking ref,
worktree, active agents, and running commands before acting.

Batch 188I Changes 1–19 are complete; Change 20 is open. The final runtime
decoder has a 2 GiB aggregate allocation cap and a 1 GiB wire-size limit.
The DesignIR loader rejects sections above 256 MiB before reading them.
Focused rejection tests and the exact mixed-codec candidate replay passed
(`ALL_CODEC_DONE`, empty stderr). The plan records the Windows-only
cancellation failure, why the fix works, and the prior performance failures.

On the final source, dependency-rescanned warnings-as-errors Release and
Debug builds and their immediately following full suites passed 428/428 each.
The LLVM-enabled ASan/UBSan build and full suite passed 428/428, with
LeakSanitizer disabled for the sandbox ptrace constraint. Logs:
`/tmp/fsim-i20-release-{reconfigure7,rebuild7,ctest7}.log`,
`/tmp/fsim-i20-debug-{reconfigure5,rebuild5,ctest5}.log`, and
`/tmp/fsim-i20-sanitizers-{reconfigure4,rebuild4,ctest4}.log`.

On 2026-09-25 the user deferred Batch 188I's full performance comparison
to future work after local correctness passed. The final-source attempt in
`.local-artifacts/188i-performance-final3/` stopped at sample 3 with exit
130; no performance pass is claimed. No I commit, push, or exact-head hosted
result exists yet.

## Next action

Review the final diff and staged file list, preserving user-owned untracked
files. Make one cohesive Batch 188I commit and push, then verify all four
exact-head LLVM Linux/Windows Debug/Release hosted lanes. If a required
lane fails, make bounded repairs and requalify the changed source. Windows
confirmation of the cancellation fix is still required. When all required
lanes pass, mark Change 20 complete and hand off to Batch 189 without a
release tag. The deferred matrix contract and frozen baseline are retained
in the plan for future performance work.

## Guardrails

Use at least 12 local workers and 120-minute build/test command timeouts.
Rescan CMake dependencies before any closure rebuild; cleaning is optional.
Keep the baseline and external design sources read-only. Preserve user-owned
untracked `phase.fst` and `scripts/__pycache__/`; do not clean, reset, stage,
or package them. Do not use `shared_ptr::unique()` on Windows. Keep
table-local IDs, sorted-content artifact identity, the SystemC plugin C ABI,
and public string lifetimes.
