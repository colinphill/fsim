<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 restart handoff

Read [implementation_plan_v2.md](implementation_plan_v2.md) first; it is the
authoritative v2 batch/status record. Preserve the completed v1 history in
`v1-resume.md`.

- Branch: `codex/v2`, tracking `origin/codex/v2`.
- Baseline: `1462f18`; annotated `v1.0.0` points to `6450599`.
- Current unit: Batch 133, all 20 changes complete; Batch 134 is next.
- Completed work: Batch 133 implements parent-library inference for HDL-to-HDL,
  HDL-to-SystemC, and SystemC-proxy-to-HDL boundaries, including resolver-only
  bindings, deterministic ambiguity, multiple logical-library SystemC
  plug-ins, and stringized `SC_FSIM_HDL_MODULE` implementation names. The
  binding-free vertical and three-language examples pass the application
  regression. Exact-LLVM Debug and Release both pass 106/106 tests, in 129.35
  and 99.88 seconds respectively, and the source, diagnostic-catalog,
  inventory, and release gates pass. The batch is committed and pushed as one
  accumulated unit. No sanitizer or GitHub CI monitoring was run because
  Batch 133 is not a scheduled monitoring boundary.
- Build every target with at least eight workers. Batch 134 must begin with
  exactly 20 numbered changes in `implementation_plan_v2.md`; sanitizers remain
  reserved for the next CI-monitoring batch, Batch 140.
