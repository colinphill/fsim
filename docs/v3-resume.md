<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 clean resume

## Current state

Verify the live branch, HEAD, tracking ref, and worktree before continuing.
Read [implementation_plan_v3.md](implementation_plan_v3.md) for the Batch 188K
closure record and subsequent Batch 189 scope. Batch 188K is complete on
`codex/v3`; Batch 189 has not started. No user question is pending. If a new
decision is needed, ask and wait for the user's answer.

Batch 188K provides native structured Tcl workspace, object, and debugger
commands; a terminal-independent completion/hint service; and a rich Tcl
console using MIT-licensed Isocline. `fsim tcl` opens the general console,
and `fsim debug` opens the same console with a snapshot. Tcl `cd` changes the
workspace, its history, and completion context. Tcl-disabled builds are
command-line only. The non-Tcl interactive loop is removed.

## Qualification and limits

- Dependency-rescanned warnings-as-errors LLVM 22 Release and Debug builds
  each passed 446/446 CTests. After the final Tcl-disabled capability edits,
  five affected tests passed again in each Tcl-enabled build.
- A dependency-rescanned Tcl-disabled build passed 438/438 applicable CTests.
  Its target graph excludes the editor and Tcl console sources.
- The prior Windows CI failure in `fsim.application.tcl` compared different
  lexical path spellings of the same managed directory. Both Tcl operands now
  use `file normalize`. The implementation plan records the cause and fix.
  Post-fix Windows CI and Windows console/ConPTY execution remain unverified;
  the user waived new hosted monitoring for this handoff.
- Performance, sanitizer, and new hosted qualification were deferred. They
  are not recorded as passing Batch 188K evidence. The plan lists follow-up
  Tcl mutation, GUI, and historical-gate work.

## Guardrails for the next batch

Preserve user-owned untracked `phase.fst` and `scripts/__pycache__/`. Do not
clean, reset, stage, or package them. Rescan CMake dependencies before closure
builds; a clean build is unnecessary. Use at least twelve local build workers
for this v3 configuration. Avoid `shared_ptr::unique()` for Windows
portability. Keep the frozen corrected baseline and external design sources
read-only. Preserve table-local IDs, sorted-content artifact identity, the
SystemC plugin C ABI, and public string lifetimes. Referenced dependencies
must permit a future closed-source fsim distribution.

Manually reindex the codebase after change sets. The primary orchestrator
uses `gpt-6-sol` at high effort and may assign up to six `gpt-6-luna` workers
at max effort when the user calls for workers. Keep file ownership clear and
centralize build and test gates.
