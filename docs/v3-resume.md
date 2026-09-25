<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Current handoff

Read [implementation_plan_v3.md](implementation_plan_v3.md), then verify
branch, HEAD, tracking ref, worktree, and active agents before acting.
Batch 188I Changes 1–20 are complete on `codex/v3`; the next planned work
is Batch 189 Change 1, registering parallel correctness, determinism,
resource, and performance obligations. Do not start from an older checkpoint.

Batch 188I's cohesive implementation commit `c531fd6a` was pushed. Its
final-source dependency-rescanned warnings-as-errors Release and Debug
builds and immediately following full suites each passed 428/428. The
LLVM-enabled ASan/UBSan build and full suite also passed 428/428; only
LeakSanitizer was disabled for the sandbox ptrace constraint. Logs are
`/tmp/fsim-i20-release-{reconfigure7,rebuild7,ctest7}.log`,
`/tmp/fsim-i20-debug-{reconfigure5,rebuild5,ctest5}.log`, and
`/tmp/fsim-i20-sanitizers-{reconfigure4,rebuild4,ctest4}.log`.

The user deferred Batch 188I's seven-sample fixed-baseline performance
matrix to future work; its stopped partial run is diagnostic only, and no
performance pass is claimed. The deferred contract is in the plan. Exact-head
hosted run `36119757637` was started for the implementation commit. The user
waived post-push monitoring and directed the handoff, so all four LLVM
Linux/Windows Debug/Release results remain unverified. The plan records why
the prior Windows cancellation failure occurs and how the fix works; hosted
confirmation of that fix is not claimed. No release tag was created.

## Guardrails

Preserve user-owned untracked `phase.fst` and `scripts/__pycache__/`; do not
clean, reset, stage, or package them. Keep the frozen corrected baseline and
external design sources read-only. Use at least 12 local build workers and
120-minute qualification command timeouts. Rescan CMake dependencies before
closure builds; cleaning is optional. Do not use `shared_ptr::unique()` on
Windows. Keep table-local IDs, sorted-content artifact identity, the SystemC
plugin C ABI, and public string lifetimes.
