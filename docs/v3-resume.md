<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Current handoff

Read [implementation_plan_v3.md](implementation_plan_v3.md), then verify
branch, HEAD, tracking ref, worktree, and active agents before acting.
Batch 188J is complete on `codex/v3`. Implementation commit `13b1f174`
is published to `origin/codex/v3`, and all twenty plan changes are complete.
The next work is the user's requested Tcl closure plan, including a rich
Tcl console and removal of the non-Tcl interactive mode. Wait for the user's
answers and explicit implementation approval. Batch 189 remains unstarted.

The workspace is cwd. `.fsim/libraries/<name>` contains managed libraries
(default `work`); `.fsim/libraries.toml` maps external libraries. SQLite
catalogs own named definitions, generated artifacts, source replacement,
dependencies, and library deletion. HDL and SystemC use managed libraries
and default/named snapshots. Simulate/debug/Tcl select snapshots. See
[workspace-mode.md](workspace-mode.md) for the public command contract.

## Qualification and deferrals

On 2026-09-25, dependency-rescanned warnings-as-errors LLVM/Clang 22.1.8
builds and their immediately following full suites passed in this order:

- Release: 438/438 tests, 138.29 seconds. Logs:
  `/tmp/fsim-j-release-configure2.log`, `/tmp/fsim-j-release-build2.log`,
  `/tmp/fsim-j-release-tests2.log`.
- Debug: 438/438 tests, 142.02 seconds. Logs:
  `/tmp/fsim-j-debug-configure-final.log`, `/tmp/fsim-j-debug-build-final.log`,
  `/tmp/fsim-j-debug-tests-final.log`.

The plan records the Windows artifact-relocation failure in CI run
`36120150859`, its scoped-stream fix, and the platform explanation. The
repaired test passes locally; post-fix Windows execution remains unverified.
Hosted monitoring and sanitizer reruns are not required again before
Batch 190. Performance measurement and harness migration remain deferred.
The plan's Tcl object-model gap list is the basis of the requested follow-on
plan. Its implementation has not been authorized to start.

## Guardrails

Preserve user-owned untracked `phase.fst` and `scripts/__pycache__/`; do not
clean, reset, stage, or package them. Keep the frozen corrected baseline and
external design sources read-only. Tcl implementation is to use a
`gpt-6-sol` orchestrator at `high` effort and up to six `gpt-6-luna` workers
at `max` effort, explicitly selected on every launch, including nested
workers. Earlier inherited-model workers were stopped. Use at least twelve local build workers and 120-minute
qualification timeouts. Rescan CMake dependencies before closure builds;
cleaning is optional. Avoid `shared_ptr::unique()`. Preserve table-local IDs,
sorted-content artifact identity, the SystemC plugin C ABI, and public string
lifetimes. Preserve the existing numeric batch sequence when inserting the
Tcl closure work before Batch 189.
