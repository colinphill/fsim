<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Current handoff

Read [implementation_plan_v3.md](implementation_plan_v3.md), then verify
branch, HEAD, tracking ref, worktree, and active agents before acting.
Batch 188J's implementation and local qualification are complete on
`codex/v3`; publication is pending from base `c1ccaa0a`. The parent owns
commit/push and the closing handoff. Plan Changes 1-19 are complete;
Change 20 awaits publication. Batch 189 remains unstarted.

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
The plan also records the requested future Tcl object-model work and its
acceptance criteria. These deferrals do not expand this closure.

## Guardrails

Preserve user-owned untracked `phase.fst` and `scripts/__pycache__/`; do not
clean, reset, stage, or package them. Keep the frozen corrected baseline and
external design sources read-only. Use up to six `gpt-6-luna` workers with
`max` reasoning, explicitly selected on every launch, including nested
workers. Existing worker roles are `workspace_*_luna`; earlier inherited-model
workers were stopped. Use at least twelve local build workers and 120-minute
qualification timeouts. Rescan CMake dependencies before closure builds;
cleaning is optional. Avoid `shared_ptr::unique()`. Preserve table-local IDs,
sorted-content artifact identity, the SystemC plugin C ABI, and public string
lifetimes. The next batch is 189: parallel execution foundation and elaboration.
