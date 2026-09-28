<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim simplification implementation plan

Prepared September 28, 2026, from `/tmp/simplification-audit.md` and its
independent review. Source baseline: `6efa4c357f96bf7d82d0047963e2fefe2ca658f7`
on `codex/v3`, matching `origin/codex/v3` when this proposal was written.

Objective: reduce duplicate implementations and maintenance work while
preserving supported behavior, resource bounds, public surfaces, and useful
diagnostics. Line reductions are reported outcomes, not acceptance targets.

**Local implementation complete, September 28, 2026:** the user authorized
continuous execution of this plan. S1-S4 cleanup, coverage, SDF and cache
reproducibility packets are locally closed. S5 dispositions are complete:
retain T3 as a small implementation simplification, reject DCO-7 and T9, and
reject/revert F2. Final cumulative Release passes 454/454; fresh affected Debug
and Tcl-off pass 21/21 each. The closure receipt and final requirement audit
are recorded in [the disposition](simplification-disposition.md). The unrelated full mixed-codec
under-15-second target is not substituted for this plan's completion criteria.
Publication remains separate from local implementation and validation.

| Deliverable | Final disposition | Completion evidence |
| --- | --- | --- |
| S2 HDL coverage integration | Locally closed: Release 454/454, Debug 14/14, Tcl-off 14/14, docs/package 24/24; receipt and index archived | Actual discovery, exact hits and zero-hit inventory, save/report, interpreter/LLVM agreement, disabled behavior, distinct instances and artifact replay |
| SDF integration | Locally closed: 454 distinct Release cases qualified (451 full-run passes plus 3 corrected audit reruns), Debug 8/8, Tcl-off 7/7, docs/package 24/24; receipt/index archived | Valid annotation changes timing; invalid files fail before publication; CLI/Tcl/C API applied-path reporting and persisted timing agree |
| Cache reproducibility | S4 locally closed: Release 454/454, Debug 16/16, Tcl-off 16/16, docs/package 24/24 and final-binary replay; receipt independently verified | Equivalent managed libraries with reversed revision IDs produce stable identity/payloads under an explicit compatibility policy |
| Four selected performance candidates | DCO-7 and T9 rejected; F2 rejected and restored; T3 retained as a small implementation simplification, with no speedup claim | Fresh attribution, focused O0/O2 lifetime/route coverage, full-workload screens and artifact/cache comparisons; final cumulative Release 454/454, Debug 21/21 and Tcl-off 21/21 |
| SDF boundary evidence from closure audit | Permanent regressions pass final Release and fresh Debug/Tcl-off qualification | Pending relative paths survive cwd/project-load changes; body limits reject before publication |
| Full-plan closure | All selected requirements have an implementation or evidence-backed disposition | Six-phase audit below in the disposition; final qualification, graph coverage, original index preservation and source/evidence identities in the S5 closure receipt |

**Adopted September 28, 2026:** the user authorized the first 20-change
execution packet below, using the performance campaign's root architect,
GPT-6-Sol/high execution owner, and up to five GPT-6-Luna/max workers.
S1 implementation, validation and rejected/deferred items are recorded in
[the disposition](simplification-disposition.md). The retained cleanup passed
Release 454/454, focused Debug 9/9, Tcl-off 8/8 and fixed-input artifact
comparison. Missing coverage/SDF integration and cache reproducibility remain
separate repairs; no feature retirement or performance experiment was folded
into this packet.
Batch 189 retains its parallel-elaboration scope. Existing
20-change batch discipline applies when this proposal is scheduled: Changes
1–19 contain focused work and Change 20 owns full qualification and any
authorized publication. This packet does not resume the paused performance
campaign or authorize feature retirement, commits, or pushes.

**Implementation order**

| Phase | Deliverable | Dependency / exit gate |
| --- | --- | --- |
| 1. Correct the audit and establish ownership | Evidence-backed disposition and public-surface inventory | Every selected item has a current source anchor, classification, and validation owner |
| 2. Establish missing-feature behavior | Minimal coverage and SDF reproductions; plugin entry-path findings | Each finding is reproduced, disproved, or explicitly unresolved; retained-feature repair scope is concrete |
| 3. Delete verified dead code | Small deletion-only changes, starting with L-01 and RT-01 | Focused checks, full closure, and applicable design/artifact comparisons pass |
| 4. Consolidate retained code and checks | Shared test helpers, inventory parsing, and bounded cache accounting | Existing assertions and substantive obligations retain owners; resource behavior is preserved |
| 5. Evaluate performance changes separately | Fresh attribution, isolated candidates, and retention receipts | Correctness plus the active campaign's measurement policy; no unsupported speedup claims |
| 6. Close each accepted packet | Validation receipt and precise resume checkpoint | Exact retained diff, limitations, outstanding decisions, and next bounded action recorded |

**Phase 1: disposition and ownership**

Create a compact disposition document when repository implementation starts.
Use the audit IDs rather than creating another independent numbering system.
For each selected recommendation record: verified revision and source paths,
classification, public/installed exposure, substantive obligations, dependency,
expected maintenance benefit, validation, and final disposition.

Use five classifications: verified dead-code deletion; behavior-preserving
consolidation; correctness repair; measured performance experiment; product or
policy decision. Do not place cache-bound changes or test-framework rewrites
in the mechanical-cleanup category.

Correct these audit premises explicitly:

- SystemC kernel-backend and SCV headers are installed by the directory rule
  in `CMakeLists.txt:1438`; only `scv.hpp` is excluded. Inventory declarations,
  dependencies, inline implementations, link surfaces, and install witnesses
  before proposing any removal. Lack of a CLI call is insufficient.
- L-08 must retain the effective-subtype cache's current count, per-entry byte,
  total accounted-byte, and traversal-depth limits. Shared accounting is a
  candidate; removing accounting is rejected from the mechanical scope.
- DCO-7's `(UnitId, StatementId)` is a candidate bucket index, not a complete
  compatibility key. Preserve all generic, language/profile, invocation,
  layout, aliasing, and read-only checks. Bound storage independently.
- Native cache-key equality is supporting evidence for native-code inputs,
  not proof of complete design or runtime equivalence. Use whole-design
  comparisons, applicable artifact bytes, and behavioral witnesses as well.
- Replace mixed-revision performance estimates with revision-labeled costs.
  P96's approximately 6.1 CPU seconds describes combined lowering,
  optimization, and verification, not removable value-name overhead.
  Cache-miss attribution remains unproven by the cited sampling evidence.
- Update stale schema references: the current native-object schema is v178.
- Preserve campaign-used diagnostics. A missing automated parser is not
  evidence that an instrument has no consumer.

For each of the five proposed retirement stacks, default to retaining its
current surface while establishing the facts. Prepare a separate decision
record identifying retained behavior, disconnected implementation, installed
contracts, documentation promises, and the estimated cost to wire or retire
it. Only an explicit retirement decision unlocks removal of a whole stack.
Unrelated cleanup continues while a stack decision is pending; avoid
refactoring substantial internals of a stack awaiting that decision.

**Phase 2: correctness before retirement**

Coverage: use the current managed-workspace CLI with a tiny HDL fixture that
has a predictable executed statement and both taken and untaken branches.
Enable coverage through compile/elaborate, simulate, and save the database.
Check nonempty point inventory, expected hit counts, and disabled-coverage
behavior. Exercise interpreter and LLVM O0/O2. Distinguish HDL code coverage
from functional covergroups and database/report functionality.

SDF: use a simple timed design and a minimal annotation whose delay visibly
changes the observed transition time. Check valid, missing, and malformed
inputs, applied-path reporting, and the CLI/Tcl/C API routes that advertise
the feature. Acceptance must examine timing and annotation effect, not only
option parsing or a successful control request.

Foreign interfaces: trace the shipped loading and callback installation
routes, including installed C bridges and dynamic registration. Record which
plugin mechanisms are reachable. Keep legacy ACC/TF functionality and
installed link surfaces intact.

Record failing reproductions as diagnostic evidence before fixing them;
do not turn an observed failure into a passing empty-output expectation or
leave a required suite knowingly failing. For confirmed missing coverage or
SDF wiring, prepare a separate feature-repair packet using the existing
pipeline. Identify the minimal supported subset, failure diagnostics, and
end-to-end acceptance tests before coding. Wiring or withdrawing advertised
features is not silently folded into simplification. Unresolved integration
work does not justify deleting those implementations.

**Phase 3: verified deletion**

Start with L-01's unreachable capability-walker roots and their exclusively
reachable helpers, then RT-01's unused VHDL assertion-formatting copy. Retain
shared helpers that still have callers. Expand to the other proposed dead
APIs only after checking source references, function pointers, callbacks,
virtual dispatch, macros, generated code, exported symbols, and installed
headers. Test-only use is a classification, not automatic deletion authority.

Use the current `home-colin-projects-fsim` codebase-memory graph, check cited
path/scope coverage, and inspect source for parser gaps. Each deletion gets
a small evidence record and focused build/tests. Do not rewrite adjacent live
code merely to enlarge the reduction.

**Phase 4: consolidation**

Start with dependency-free test helpers: assertions with useful location and
message reporting, diagnostic lookup, file IO, and uniquely named temporary
directories with reliable cleanup. Pilot a small representative set before
wide migration. Then share elaboration and application differential harnesses,
keeping fixture intent and interpreter/cold/warm assertions visible.

Preserve individual case execution and failure isolation. Compare generated
CTest commands, fixtures, labels, timeouts, and required names before and
after CMake changes. Self-registration, sharding, and executable merging are
separate optional changes, justified by measured link/test costs and checked
for process-global state leakage. They are not prerequisites for helpers.

Pilot one shared inventory parser with two structurally different validators.
Keep domain-specific checks explicit. Map each old obligation to its retained
validator, compile-time assertion, or behavioral test before removing tokens.
Use representative invalid inputs to demonstrate rejection of missing IDs,
duplicate IDs, broken references, missing tests, and relevant ABI violations.
Preserve generated-command uniqueness and fixture behavior. Extend only after
the pilot demonstrates clearer code and net maintenance reduction.

Consolidate cache byte accounting without changing admission, lifetime,
invalidation, fallback, or bad-allocation behavior. Add focused boundary and
oversized-value witnesses where needed. Do not introduce a universal memo
framework solely because three caches share some syntax.

Cross-cutting helpers follow only after checking semantic differences:
case folding, path boundaries, wildcard escaping, encoding validity, and
overflow policy. Keep deliberate variants explicit.

**Phase 5: performance work, when resumed**

Begin with a fresh profile of the retained full mixed-codec workload and
rank opportunities by credible absolute end-to-end savings. Use existing
profilers before adding instrumentation. Keep instrumented attribution apart
from uninstrumented timing. Record executable, dependencies, source revision,
input identities, cache state, phase times, RSS, and correctness evidence.

The current campaign permits one cold uninstrumented CPU-0/O2 observation per
candidate, with unchanged external RTL, compatibility mode, waves/debug off,
and all saved correctness lines/fingerprints. A single observation supports
screening, not a statistically established speedup or proof of no regression.
Do not restart the deferred seven-pair/Vivado program or the accepted short-case
regression investigation through this plan.

Evaluate the following independently, ordered by fresh applicability and cost:

- T9: guard class and static-property snapshots before allocation when their
  corresponding observer is absent. Preserve notification order and behavior
  with observers present; use a representative class workload for this path.
- T3: hoist the stable-direct-update environment switch to the executor's existing
  configuration boundary. Verify the intended environment lifetime and check
  both enabled and disabled routes, including sequential executor instances.
- DCO-7: simplify the concurrent-template lookup only if fresh costs justify it.
  A bucket index must retain full matching, deterministic selection, and
  allocation-independent fallback. Select explicit entry/retained-byte budgets
  from the observed working set before implementation. Test generic mismatch,
  layout/alias mismatch, invocation mismatch, and cap-exhaustion fallback.
- F2: trial LLVM value-name discarding if current setup attribution supports it.
  Preserve usable IR dumps and check generated code and cache compatibility;
  do not assume a schema change is unnecessary without evidence.

Retain a neutral change when it demonstrably simplifies implementation and
preserves correctness. Added caches, state, or branches need supported benefit.
Revert rejected candidates without disturbing other changes. IPO defaults,
post-optimization verifier removal, lock-file durability, scheduler ownership
changes, and new test frameworks remain separate decisions/experiments.

**Simplification packet S1: 20 changes**

This packet establishes evidence and delivers bounded cleanup. It is an
independent maintenance packet before Batch 189 and does not renumber it.
Items that fail their evidence gate finish as a documented deferral rather
than an unsafe edit or an unrelated substitute.

1. Freeze the working baseline, obligations, generated CTest inventory, and
   applicable design/artifact comparison fixtures.
2. Write the audit corrections and selected-item disposition.
3. Inventory installed SystemC/SCV surfaces and their compile/link witnesses.
4. Complete ownership/reachability decisions for the remaining proposed stacks.
5. Reproduce or disprove the HDL coverage finding and specify its repair.
6. Reproduce or disprove SDF annotation behavior and specify its repair.
7. Verify foreign-plugin loading and installed-bridge reachability.
8. Delete the unreachable capability-walker roots and exclusive helpers.
9. Delete the unused assertion formatter copy.
10. Pilot shared container helpers from RT-02 after verifying semantic identity.
11. Pilot assertion and diagnostic test helpers.
12. Pilot file and temporary-directory test helpers.
13. Pilot the elaboration harness migration.
14. Pilot the application differential harness migration.
15. Implement the small shared inventory parser and migrate one validator.
16. Migrate a second distinct validator; verify rejection behavior and net benefit.
17. Remove only pilot token checks whose substantive obligations have retained owners.
18. Consolidate subtype-cache accounting while preserving all current bounds.
19. Review the complete diff, design/artifact identity evidence, test-case inventory,
    and disposition of confirmed correctness gaps; prepare the exact next packet.
20. Run local batch closure, update packaging/docs as required, and write the
    handoff. Commit/push only under the publication authorization active then.

**Validation and closure rules**

- Use at least twelve local build workers under the campaign execution-owner
  configuration; preserve CI's two-worker setting.
  Run affected targets and meaningful focused checks during each change.
- At closure, run the full Release suite, appropriate Debug coverage, and
  Tcl-disabled checks when affected, plus install/public-contract,
  source-package, and documentation gates relevant to the final diff.
- For pure refactors compare applicable deterministic artifacts, complete
  elaborated-design state, diagnostics, and interpreter/LLVM behavior. Use
  native keys and IR comparison as additional evidence. Correctness repairs
  document intentional output changes separately.
- Account for old and new individual test cases, not just CTest-name counts.
  Removed redundant executions need an identified surviving owner.
- Do not claim hosted Windows, sanitizer, or performance qualification from
  local correctness results. Keep the current no-CI-monitoring instruction
  until superseded; preserve designated roadmap validation boundaries.
- Refresh the canonical code index after integrated or reverted implementation
  packets. Record exact source/build identities, tests, retained changes,
  unresolved issues, decisions, and the next bounded action.
- Preserve the existing staged handoff files, `.codebase-memory/`, `phase.fst`,
  `scripts/__pycache__/`, and ignored benchmark evidence. Use explicit paths
  for staging; never blanket-reset or clean the worktree.

Completion of this workstream means the selected duplicate implementations
and redundant checks have been removed, every retained obligation has an
owner, resource/public contracts remain intact, and all selected repairs or
experiments have an explicit disposition. It does not require reaching a
line-count quota or retiring any of the five feature stacks.
