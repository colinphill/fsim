<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v1 reboot checkpoint

Recorded: 2026-07-30.

This file is the durable checkpoint for a reboot during the v1 implementation
goal. Read [the authoritative v1 handoff](v1-resume.md) first; this note only
records the precise unfinished-work boundary.

## Live repository boundary

- Active goal: continue implementation until the v1 release is
  feature-complete.
- Branch: `codex/resumable-jit`.
- Local and remote baseline:
  `2fc988b8afd7248d689a5a1bbf8c8d1bd86ce898`
  (`feat: add named reduction iterators`).
- The worktree was clean and `HEAD` matched
  `origin/codex/resumable-jit` when this checkpoint was created.
- Feature Batch85 is complete, fully tested, committed, and pushed.
- Feature Batch86 is the active batch. Its scope is fully specified under
  "Next ten-feature batch" in `docs/v1-resume.md`.
- No Batch86 implementation edit had started at this checkpoint. The next
  action is the existing-assignment-pattern audit below.

## Resume invariants

- Treat the checked-out repository and remote branch as authoritative; do not
  reconstruct state from stale chat context.
- Use at least eight workers for every local build, including interim and
  fetched-dependency builds: `cmake --build <tree> --parallel 8`.
- GitHub Actions alone remains limited to build parallelism two because hosted
  Ubuntu runners experienced memory pressure.
- Inspect and repair non-documentation GitHub Actions at every tenth numbered
  batch. The next mandatory boundary is Batch90. Do not monitor
  documentation-only CI runs.
- Pending and future implementation batches are authorized to commit and push.
- Preserve the 2,000-line authored-source hard limit and the Windows MSVC Debug
  stack-footprint constraints documented in `docs/v1-resume.md`.

## Exact next work

Audit the current assignment-pattern representation and implementation before
changing it:

```sh
git status --short --branch
git rev-parse HEAD
git rev-parse origin/codex/resumable-jit
rg -n "ExpressionKind::Aggregate|assignment pattern|apostrophe" \
  src/frontend include tests
rg -n "ExpressionKind::Aggregate|SVPATTERN|assignment pattern" \
  src/elaboration include tests
rg -n "ContainerWrite|ContainerCopy|AssignContainer|BuildContainer|Aggregate" \
  include/fsim/runtime/simir.hpp src/compiler/llvm_jit_cache_key.cpp src/runtime
rg -n "SV-33[1-9]|SV-340|SVPATTERN" docs tests src include
```

Then implement Batch86's bounded static-array assignment-pattern default and
signed index-key semantics. Use focused Debug gates while implementation is in
progress, always rebuilding with `--parallel 8`; run the complete exact-LLVM
22.1.8 Debug and Release gates only after all ten Batch86 items are complete.
Update the authoritative handoff, implementation plan, feature matrix,
language support, diagnostics, and cache schema evidence as the implementation
requires. Commit and push the completed non-boundary batch without inspecting
Actions.

The existing validated Batch85 baseline is recorded in
`docs/v1-resume.md`, including all 59 Debug and Release tests, scoped-locals
timings, container differential timings, diagnostic count, source-size gate,
and the repaired Windows MSVC Debug behavior.
