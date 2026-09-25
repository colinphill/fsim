<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

Verify the live branch, HEAD, tracking ref, and worktree first. Batch 188K is
complete on `codex/v3`; its initial commit is `b54d7066`, followed by the
completion repair commit. [The implementation plan](implementation_plan_v3.md)
is the authority for the accepted Tcl contract and Batch 189 scope. Batch 189
has not started. No user question or approval is pending; ask and wait if a new
decision is required.

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

Start Batch 189 only after reviewing its plan. Preserve user-owned untracked
`phase.fst` and `scripts/__pycache__/`, frozen baselines, external design
sources, table-local IDs, artifact identities, the SystemC plugin C ABI, and
public string lifetimes. Rescan CMake dependencies before closure builds;
clean builds are unnecessary. Use at least twelve local build workers, avoid
`shared_ptr::unique()` for Windows, and use dependencies compatible with a
future closed-source fsim. Reindex the codebase manually after change sets.
When workers are needed, the primary `gpt-6-sol` orchestrator at high effort
may assign up to six `gpt-6-luna` workers at max effort.
