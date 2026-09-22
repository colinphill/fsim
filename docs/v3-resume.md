<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 restart handoff

Read [implementation_plan_v3.md](implementation_plan_v3.md) first. It is the
authoritative v3 batch and status record. Preserve the completed v1 and v2
history in their existing plan and resume documents.

Before triggering any hosted CI run authorized by the governing batch cadence,
inspect the most recent applicable run, its conclusion, and its failing job
logs. Identify and resolve known actionable errors before starting the new run.
Batch 188A's replacement hosted matrix contains exactly four LLVM-enabled
lanes: Linux Debug/Release and Windows Debug/Release. Non-LLVM and fuzz presets
remain available for local development but are not hosted jobs.

## Batch 178 planned restart checkpoint - before Change 1

1. Resume in the fsim repository on branch codex/v3. The branch starts from
   clean v2 checkpoint 22d5e2ba43b5e7711db6fe0b27d7e9e0d5345095.
   Before implementation, require a clean worktree and verify that local
   codex/v3 is synchronized with origin/codex/v3 at this documentation-only
   checkpoint.
2. This checkpoint creates only implementation_plan_v3.md and v3-resume.md.
   It changes no implementation, test, build, packaging, or workflow file and
   consumes no Batch 178 numbered change. Push it before Change 1 begins.
3. The authoritative plan allocates exactly twenty batches, Batches 178-197,
   and exactly twenty numbered changes per batch. Do not split, combine,
   renumber, insert, or silently broaden a change.
4. Changes 1-19 accumulate in one recoverable worktree. Use focused
   warnings-as-errors Debug builds and tests as dependencies require. Change 20
   alone owns clean full Debug and Release qualification, documentation, one
   implementation commit, and one push.
5. At every Change 20, compile the clean Release configuration before running
   the Debug suite. Fix Release compiler errors first so Debug qualification is
   not invalidated by a compiler-driven source correction.
6. Release-closing Change 20s additionally own release artifacts, the exact
   release record, annotated tag, and publication after their required local
   lanes are green. They do not add sanitizer or hosted-CI runs unless the
   closing batch is also Batch 180 or Batch 190.
7. Batch 180 and Batch 190 are the scheduled tenth-batch sanitizer and
   non-documentation hosted-CI boundaries. Batch 178 is neither boundary: do
   not configure sanitizers or inspect hosted CI during this batch.
8. Use at least eight local build workers and retain 120-minute qualification
   timeouts. Avoid formatting-only header changes; make only necessary semantic
   header edits and keep formatter churn confined to changed implementation
   sources where practical.
9. Create v3 manifest, native ABI, object, design, checkpoint, cache, and
   plugin schemas directly. Reject all versioned v2 inputs deterministically.
   Do not implement compatibility readers, migrations, fallbacks, or dual
   writes. Preserve every existing HDL language profile.
10. The two privately supplied language standards are read-only references.
   Never copy, commit, package, quote, or log their contents, locations, or
   file hashes. Repository inventories may record standard identifiers, clause
   numbers, independently written summaries, and test owners only. Author all
   tests and examples independently.
11. Batch 178 establishes the language-neutral code-coverage foundation only.
    Condition, expression, toggle, and FSM coverage remain Batch 179 work;
    unified database/API/report work remains Batch 180 work.
12. Changes 1-4 register the obligation matrix, define coverage point/metric/
    run/result types, canonicalize relocation-independent source identity, and
    derive stable point IDs from language, construct, span, and source identity.
13. Changes 5-8 discover executable Verilog/SystemVerilog and VHDL statement
    points, model decisions and individually addressable branch arms, and
    derive covered, partial, and uncovered line states.
14. Changes 9-13 attach inventories to elaborated instances, add a validated
    SimIR coverage-hit operation, implement saturating interpreter counters,
    lower equivalent LLVM O0-O3 counters, and preserve identity/hits in the
    Debug engine.
15. Changes 14-18 exclude non-executable and statically removed constructs,
    assign stable instance identities, retain both instance and source-union
    results, add opt-in manifest/CLI controls with no default overhead, and
    include coverage identity in v3 object/design/native-cache keys.
16. Change 19 proves Verilog/SystemVerilog/VHDL engine and aggregation
    equivalence. Change 20 runs the standard batch closeout and freezes the
    foundation inventory.
17. Preserve the public v3 coverage contract: [coverage],
    --code-coverage, --coverage-metrics, and --coverage-db are opt-in; one
    versioned .fsimcov database eventually carries distinct code,
    SystemVerilog functional, and PSL namespaces. Do not prematurely implement
    Batch 179 or 180 surfaces.
18. Begin with Change 1 only. Register one clause-neutral coverage obligation
    and ownership matrix whose rows have stable independent identifiers,
    independently worded obligations, exact Batch 178 change ownership,
    implementation/test/diagnostic owners, profile and engine scope, closure
    evidence, and explicit active status. Add a bounded validator and
    deterministic normalized identity following existing inventory patterns.
19. Change 1 validation must be focused and warning-clean in the existing
    exact-LLVM Debug tree. Exercise the new inventory owner plus affected
    diagnostic/source-budget/inventory gates. Record exact commands, results,
    elapsed time, resource evidence when available, and the normalized matrix
    identity in both authoritative v3 documents.
20. After Change 1, preserve its intentionally dirty worktree and proceed one
    numbered change at a time. Do not reset, commit, push, run Release
    qualification, run sanitizers, or inspect hosted CI before Batch 178
    Change 20.
21. The immediate transition is to validate these two documentation files,
    make and push the documentation-only codex/v3 checkpoint, verify the branch
    is clean and synchronized, reread this section and Batch 178, and then
    perform only Change 1.

## Batch 178 active checkpoint - after Change 1

1. Resume in the fsim repository on codex/v3. The documentation-only checkpoint
   is committed and pushed at
   ac7927db6763fa74e037a8be12a7fa3ca4edaa50. Change 1 is the only active
   implementation-batch work and is intentionally uncommitted.
2. The worktree has seven Batch 178 paths: the authoritative plan and resume,
   packaging/source-package-manifest.txt, tests/CMakeLists.txt,
   tests/feature_matrix/README.md, the new
   cmake/CheckCodeCoverageInventory.cmake validator, and the new
   tests/feature_matrix/code_coverage_inventory.tsv ledger. Preserve all seven
   paths without reset, commit, or push.
3. The clause-neutral ledger has seventeen unique active rows assigned
   one-to-one to Changes 2-18. It binds IEEE1076, IEEE1364, and IEEE1800 plus
   VHDL87/93/2000/2002/2008, Verilog 1995/2001/2001-noconfig/2005, and
   SystemVerilog 2005/2009/2012/2017 to exact domain, implementation,
   positive, negative, engine, aggregation, artifact, diagnostic, and resource
   owners.
4. The normalized ledger SHA-256 is
   4099bbe2a9a8ba45021a6661411a4db1b735ac04803d2d2538510adfcc634cce.
   The validator enforces the exact v3 batch range, twenty-change Batch 178
   allocation, unique row identities/domains/changes, active-to-preserved
   transitions, bounded independently worded obligations, safe relative
   owners, complete retained-profile identity, and private-reference exclusion.
5. Registering fsim.code-coverage-inventory caused the exact-LLVM 22.1.8 Debug
   tree to regenerate and build 811/811 targets with eight workers. The live
   output contains no warning, error, or failed-build marker. No clean-first or
   Release build was run because both remain Change 20 work.
6. The initial five-owner focused run passed the new inventory, diagnostic
   catalog, source-line budget, and CTest command-uniqueness gates, then
   correctly failed source-package ownership because the new validator was not
   yet in the manifest. Preserve
   build/llvm22-ninja-debug/batch178-change01-focused.log at SHA-256
   5d795260ecf6b8d373c81d730b2e6198fac6a37fea5f8be66ec0ab258e275a61.
7. The deterministic source manifest now contains 1,503 ordered paths at
   SHA-256
   46ed4416fe9ce9c9185ac2e7297433e9bfe4c2962b92d4d3f22d1f100c1b4049.
   Its only additions are the two v3 documents, coverage ledger, and coverage
   validator.
8. The repaired focused slice passes 5/5 in 4.78 seconds at 21,376 KiB peak
   RSS with zero swaps. Its warning/error/crash-marker-clean retained log
   SHA-256 is
   e577a18f0879184cf908679e14a628a484726bbbd178ad1892f79931760fdb67.
   A verbose inventory-only rerun passes 1/1 in 0.02 CTest seconds and 0.03
   wall seconds at 20,056 KiB peak RSS with zero swaps; its retained log
   SHA-256 is
   c008471197e383db9e8785a219e65a80677cf083bcbeda5d24c90b0bd15cde18.
   After the authoritative status update, the final five-owner slice passes
   5/5 in 8.40 seconds at 21,168 KiB peak RSS with zero swaps. Its retained log
   is marker-clean at SHA-256
   0312441ab7caa11e53b1c5a13ed84a4e8009ab2d8ec646d6539ba141c04bbbdf.
9. No product source, public header, manifest schema, ABI, object, design,
   checkpoint, cache, or plugin implementation changed. No Release
   qualification, sanitizer, hosted-CI inspection, commit, or push ran after
   the documentation-only checkpoint.
10. Proceed only to Batch 178 Change 2: define the language-independent
    coverage point, metric, run, and result types. Preserve the registered
    Change 1 ledger and validator, keep all rows active except Change 2 when its
    complete owners exist, use focused warning-as-errors Debug validation, and
    do not begin Change 3 in the same bounded slice.

## Batch 178 active checkpoint - after Change 2

1. Resume in the fsim repository on codex/v3 at the intentionally dirty Batch
   178 worktree. The documentation-only checkpoint remains the last commit and
   pushed branch state at ac7927db6763fa74e037a8be12a7fa3ca4edaa50.
   Changes 1 and 2 are complete and uncommitted; preserve their accumulated
   paths without reset, commit, or push.
2. Change 2 adds include/fsim/runtime/code_coverage.hpp and
   tests/runtime/code_coverage_model_test.cpp, registers the focused runtime
   test in tests/runtime/CMakeLists.txt, catalogs FSIM-COV-001 in
   docs/diagnostics.md, and extends the resource-portability contract and
   source-package manifest. No public installed header, manifest schema, ABI,
   object, design, checkpoint, cache, CLI, database, or engine surface changed.
3. The language-neutral runtime model uses distinct strong run, point, and
   counter IDs. Its stable metrics are statement, branch, and derived line;
   its statuses are empty, uncovered, partial, covered, and excluded. Runs own
   dense point-counter inventories, point results retain hits and status, and
   metric results retain exact component totals without a combined score.
4. validate_code_coverage is allocation-free and enforces configured maximums
   of 1,048,576 points and three metric summaries. It rejects invalid or
   mismatched run/point identities, line or unknown metrics owning counters,
   non-dense counter ownership, result drift, incoherent hit/status pairs,
   invalid aggregate counts/status, duplicate or missing metric results, and
   point/metric disagreement. Source identity construction is deliberately
   absent because it belongs to Change 3; point-ID generation belongs to
   Change 4.
5. The first warnings-as-errors Debug target build retained
   build/llvm22-ninja-debug/batch178-change02-build.log at SHA-256
   8e30267f3547996219db602852ac5e29fcb3321dd74263533be5e44797018c3c.
   It rejected two incomplete designated initializers in the new empty-model
   test. Both were made explicit; no production-source diagnostic was emitted.
6. The repaired eight-worker exact-LLVM Debug target build is warning/error
   clean and completes in 0.76 seconds at 143,036 KiB peak RSS with zero swaps.
   Its retained build/llvm22-ninja-debug/batch178-change02-build-final.log has
   SHA-256
   813a7ba9d887d5fbc2426eba736da61c5f6796916728b35cf98eb16cdc7284b1.
   The focused model test passes 1/1 in 0.01 CTest seconds and 0.02 wall seconds
   at 20,140 KiB peak RSS with zero swaps; its retained log SHA-256 is
   e99e84a84510b1b8ddee1f03e85bfd0369b700700b499b0226e2df13723b81e5.
7. The first combined pre-documentation slice passed the model, source-budget,
   package, resource, and CTest-owner gates but exposed that the Change 1
   inventory validator required Change 19's future engine, aggregation, and
   artifact files before an individual row could close. The repaired rule
   requires implementation plus positive/negative evidence at row closure,
   continues validating all planned owner paths, and requires every cross-row
   evidence file when Change 19 closes; no placeholder or future-change test
   was added.
8. The final post-documentation focused runtime model, coverage inventory,
   diagnostic catalog, resource portability, source-line budget,
   source-package manifest, and CTest command-uniqueness slice passes 7/7 in
   6.78 CTest and wall seconds at 21,948 KiB peak RSS with zero swaps. Its
   warning, error, failure, crash, and sanitizer-marker-clean retained log is
   build/llvm22-ninja-debug/batch178-change02-focused-final.log at SHA-256
   173018316cd2a4a958775db1a2a0e8ace285f4fdadb0d9a46674267721061499.
9. COVBASE-C02 is preserved and sixteen rows remain active. The normalized
   ledger SHA-256 is
   e5d223030d3e4f3f863118632cd385e19a4d8ba5f6004bdbac887b2e02e50639.
   The deterministic source manifest contains exactly 1,505 paths at SHA-256
   2f2ceafc9090288c5c82eeb77b91e6a62889818bbba907d1acf65154419942ac.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 3:
    canonicalize source paths and content identities independently of checkout
    location. Preserve all Change 1-2 work and do not begin point-ID generation
    from Change 4 in the same bounded slice.

## Batch 178 active checkpoint - after Change 3

1. Resume in the fsim repository on codex/v3 at the intentionally dirty Batch
   178 worktree. The documentation-only checkpoint remains the last commit and
   pushed branch state at ac7927db6763fa74e037a8be12a7fa3ca4edaa50.
   Changes 1-3 are complete and uncommitted; preserve their accumulated paths
   without reset, commit, or push.
2. Change 3 adds include/fsim/frontend/coverage_source_identity.hpp,
   src/frontend/coverage_source_identity.cpp, and
   tests/frontend/coverage_source_identity_test.cpp. CMake adds the source to
   fsim_frontend, gives that target its existing fsim_support SHA-256
   dependency, and registers fsim.frontend.coverage_source_identity. The
   diagnostic catalog, resource contract, and source manifest include the new
   surface. It remains an internal header and adds no installed public API.
3. make_code_coverage_source_identity accepts a checkout root, an absolute or
   root-relative native source path, raw content bytes, and explicit limits.
   It emits a generic UTF-8 checkout-relative logical path, the exact content
   SHA-256, and a length-framed composite SHA-256 under frozen schema
   fsim-code-coverage-source-v1. Checkout-root spelling never enters either
   digest, so relocation preserves identity while path or content changes do
   not alias in the governed corpus.
4. Validation rejects missing roots and sources, a source equal to or outside
   its checkout, incompatible native roots, lexical parent escape, embedded
   NULs, malformed UTF-8, logical paths over 1 MiB, and contents over 1 GiB.
   FSIM-COV-002 owns these failures. The implementation performs no filesystem
   I/O, canonical/symlink probing, or point-ID generation; Change 4 alone owns
   the latter.
5. The final warnings-as-errors exact-LLVM Debug target build uses eight
   workers and completes in 1.12 seconds at 234,416 KiB peak RSS with zero
   swaps. Its warning/error-marker-clean retained log is
   build/llvm22-ninja-debug/batch178-change03-build-final.log at SHA-256
   2b71ebe9a0fd92b6be6d01983b931821c0615fd957499d91a0cda22a08306a00.
6. The final focused source-identity test passes 1/1 in 0.01 CTest seconds and
   0.02 wall seconds at 19,888 KiB peak RSS with zero swaps. Its retained
   build/llvm22-ninja-debug/batch178-change03-test-final.log is marker-clean at
   SHA-256
   a9e753b8173f3ff83a731c8f4999e3ae28cc9d96bfaffa481e6b5dc02dc45342.
7. The final post-documentation accumulated runtime model, source identity,
   coverage inventory, diagnostic catalog, resource portability, source-line
   budget, source-package manifest, and CTest command-uniqueness slice passes
   8/8 in 6.69 CTest and wall seconds at 21,920 KiB peak RSS with zero swaps.
   Its warning, error, failure, crash, and sanitizer-marker-clean retained log
   is build/llvm22-ninja-debug/batch178-change03-focused-final.log at SHA-256
   383f87fb50f99a3d5343e5b38106fe9c8ececdb46cf63beb1d469c26b1ad25bf.
8. COVBASE-C02 and COVBASE-C03 are preserved and fifteen rows remain active.
   The normalized ledger SHA-256 is
   6826c48a6a543760828083bc216b0a5e5487edc79b0feb89153c12c08226f282.
   The deterministic source manifest contains exactly 1,508 paths at SHA-256
   a45b640702c1c91e5a66f532b844dfc8c8c879f03661e0dc78e98c661d737331.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 178 Change 4:
   generate stable point IDs from language, construct kind, exact source span,
   and the Change 3 source identity without allocation-order input. Preserve
   all Change 1-3 work and do not begin statement discovery from Change 5 in
   the same bounded slice.

## Batch 178 active checkpoint - after Change 4

1. Resume in the fsim repository on codex/v3 at the intentionally dirty Batch
   178 worktree. The documentation-only checkpoint remains the last commit and
   pushed branch state at ac7927db6763fa74e037a8be12a7fa3ca4edaa50.
   Changes 1-4 are complete and uncommitted; preserve their accumulated paths
   without reset, commit, or push.
2. Change 4 adds include/fsim/frontend/coverage_point_identity.hpp,
   src/frontend/coverage_point_identity.cpp, and
   tests/frontend/coverage_point_identity_test.cpp. CMake includes the source
   in fsim_frontend and registers fsim.frontend.coverage_point_identity. The
   diagnostic catalog, resource contract, source manifest, inventory, and
   README own the new surface. It remains internal and adds no installed API.
3. CodeCoverageLanguage freezes Verilog, SystemVerilog, and VHDL tags.
   CodeCoverageConstructKind freezes statement plus true, false, case, default,
   and implicit branch-arm tags. CodeCoverageSourceSpan is a 64-bit nonempty
   `[begin,end)` byte range; byte offsets are exact because the source identity
   authenticates the complete content bytes and their extent.
4. make_code_coverage_point_identity validates every typed input, then hashes
   the frozen schema, full Change 3 source digest, language, construct kind,
   begin offset, and end offset using length framing and big-endian integer
   encoding. The first 128 SHA-256 bits populate CodeCoveragePointId. No global
   registry, allocator, traversal order, checkout path, or filesystem state is
   consulted. FSIM-COV-003 owns stable failures.
5. Change 4 also strengthens Change 3 by retaining and authenticating the exact
   source content byte extent in its composite digest and by exposing internal
   source-identity validation. This lets point spans reject bytes beyond their
   source without trusting mutable metadata. Existing relocation and content
   tests remain green.
6. The first warnings-as-errors build retained
   build/llvm22-ninja-debug/batch178-change04-build.log at SHA-256
   aab2aa025cb234ff350d7084d52f2ee84ffa964c1350f86413457589f4907549.
   It found one local helper name collision after adding authenticated content
   extent; renaming the digest-byte buffer resolved all four resulting compiler
   diagnostics without changing the algorithm.
7. The repaired eight-worker exact-LLVM Debug source/point build completes in
   1.00 second at 234,648 KiB peak RSS with zero swaps. Its retained
   build/llvm22-ninja-debug/batch178-change04-build-final.log is warning/error
   clean at SHA-256
   de87068b3e075c8674e65d1f5d3e2a26b7edf916f0e926dec37c6370f85a01d4.
   After freezing the canonical vector, the 2/2 identity tests pass in 0.01
   CTest seconds and 0.02 wall seconds at 19,612 KiB peak RSS with zero swaps;
   build/llvm22-ninja-debug/batch178-change04-tests-final.log has SHA-256
   3392d5a4f70a4fe3e9025678e8b371e3cfebb5c3e5b205ec835fee10aa6274ea.
8. The accumulated runtime model, source identity, point identity, coverage
   inventory, diagnostic catalog, resource portability, source-line budget,
   source-package manifest, and CTest command-uniqueness slice passes 9/9 in
   6.46 CTest seconds and 6.46 wall seconds at 21,924 KiB peak RSS with zero
   swaps. Its warning, error, failure, crash, and sanitizer-marker-clean log is
   build/llvm22-ninja-debug/batch178-change04-focused-final.log at SHA-256
   65919f458b9457c3901fc8ec4c3b0eb0f32a837b8816f48ddedb9e5e06aa1e1c.
9. COVBASE-C02 through COVBASE-C04 are preserved and fourteen rows remain
   active. The normalized ledger SHA-256 is
   39cf78cfd86524613722ad1b4b053afb35aba5bfe702644595c8c20027c890f2.
   The deterministic source manifest contains exactly 1,511 paths at SHA-256
   9ca48d38b31c35820c07d860b65de7569e4e752b326f7502249908536c9f1588.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 5:
    discover executable Verilog and SystemVerilog statement points. Preserve
    all Change 1-4 work and do not begin VHDL statement discovery from Change 6
    in the same bounded slice.

## Batch 178 active checkpoint - after Change 5

1. Resume in the fsim repository on codex/v3 at the intentionally dirty Batch
   178 worktree. The documentation-only checkpoint remains the last commit and
   pushed branch state at ac7927db6763fa74e037a8be12a7fa3ca4edaa50.
   Changes 1-5 are complete and uncommitted; preserve their accumulated paths
   without reset, commit, or push.
2. Change 5 adds include/fsim/elaboration/verilog_coverage_points.hpp,
   src/elaboration/verilog_coverage_points.cpp, and
   tests/elaboration/verilog_coverage_points_test.cpp. CMake includes the
   implementation in fsim_elaboration and registers the focused
   fsim.elaboration.verilog_coverage_points test. The diagnostic catalog,
   resource contract, source manifest, coverage ledger, and feature-matrix
   README own the surface. It remains internal and adds no installed API.
3. discover_verilog_statement_points accepts one retained Statement forest, an
   owning Verilog-2005 or SystemVerilog-2017 language family, and exact physical
   source names mapped to authenticated Change 3 identities. Every emitted
   point retains its Change 4 ID, typed language, original StatementKind,
   exact `[begin,end)` byte span, and source-map index. No counter allocation,
   runtime instrumentation, hierarchy attachment, or execution occurs yet.
4. The iterative lexical depth-first walk visits ordinary bodies, false bodies,
   and all case alternatives. Every executable statement kind is governed;
   Block is only a lexical container, Null is explicitly non-executable, and
   declarations are structurally absent from the accepted forest. Normalized
   loop_updates are source-expression fragments rather than standalone grammar
   statements and are not walked. Change 14 remains responsible for excluding
   constructs removed by later static elaboration.
5. FSIM-COV-004 owns unsupported languages, empty or duplicate physical source
   keys, invalid authenticated identities, unknown statement sources, invalid
   spans, duplicate IDs, and source, statement, or nesting ceiling failures.
   The operation clears partial output on every rejection and converts internal
   allocation failures into the bounded resource error.
6. The independently authored test parses nested SystemVerilog declarations,
   assignments, if/else blocks, case alternatives, a null statement, and a for
   loop, then proves the exact eight executable points. A separate Verilog
   corpus proves relocation and root-order independence. Negative cases cover
   VHDL routing, unauthenticated and unnamed sources, duplicate and unknown
   source mappings, out-of-source spans, duplicate points, and all resource
   ceilings. It also proves direct equivalence with Change 4 identity creation.
7. The first warnings-as-errors implementation/test build retained
   build/llvm22-ninja-debug/batch178-change05-build.log at SHA-256
   a2e13c52bc1d7655a5beac4a2a8a27a3c7bb6fcbb939548cbde9e291faa0f569.
   The first test run retained
   build/llvm22-ninja-debug/batch178-change05-test.log at SHA-256
   d10af63590114375dd9c64b8bcba23fb8a432211c1a1511db5321df221e691ca.
   It exposed a test-only invalid indexing assumption: the parser retains the
   two statements in that simple initial block as two process roots rather than
   as children of a Block node. Reordering the actual roots fixed the test;
   discovery implementation semantics did not change for that failure.
8. The final eight-worker warnings-as-errors exact-LLVM Debug target builds in
   2.48 seconds at 1,018,916 KiB peak RSS with zero swaps. Its retained
   build/llvm22-ninja-debug/batch178-change05-build-final.log is warning/error
   clean at SHA-256
   1687f738a5acea31a8c0d098eb04595699d5bf9f0f54450e3675a64c643c0a55.
   The focused discovery test passes 1/1 in 0.01 CTest and wall seconds at
   19,896 KiB peak RSS with zero swaps; retained
   build/llvm22-ninja-debug/batch178-change05-test-final.log has SHA-256
   8ff5b775878ed321529d83e997b2c790da074c76a7c8e45a20feba5bfde24164.
9. The accumulated runtime model, source identity, point identity, Verilog/
   SystemVerilog statement discovery, coverage inventory, diagnostic catalog,
   resource portability, source-line budget, source-package manifest, and
   CTest command-uniqueness slice passes 10/10 in 6.28 CTest seconds and 6.29
   wall seconds at 21,944 KiB peak RSS with zero swaps. Its warning, error,
   failure, crash, and sanitizer-marker-clean retained log is
   build/llvm22-ninja-debug/batch178-change05-focused-final.log at SHA-256
   b8414e72c242d59cc1cdf5d505eba3eb3330a14706ca94ca4ceaf5439da4c22b.
10. COVBASE-C02 through COVBASE-C05 are preserved and thirteen rows remain
    active. The normalized ledger SHA-256 is
    85ace6c335e8730f1b24281b16681048953c761a77e892809a4cef67e21a07eb.
    The deterministic source manifest contains exactly 1,514 paths at SHA-256
    da251e7c07d35af4e0fd9fc48fb6b935dac928d1e4cf1435d057a36503999c55.
11. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 6:
    discover executable VHDL statement points. Preserve all Change 1-5 work
    and do not begin decision-branch modeling from Change 7 in the same bounded
    slice.

## Batch 178 active checkpoint - after Change 6

1. Resume in the fsim repository on codex/v3 at the intentionally dirty Batch
   178 worktree. The documentation-only checkpoint remains the last commit and
   pushed branch state at ac7927db6763fa74e037a8be12a7fa3ca4edaa50.
   Changes 1-6 are complete and uncommitted; preserve their accumulated paths
   without reset, commit, or push.
2. Change 6 adds include/fsim/elaboration/vhdl_coverage_points.hpp,
   src/elaboration/vhdl_coverage_points.cpp, and
   tests/elaboration/vhdl_coverage_points_test.cpp. CMake includes the
   implementation in fsim_elaboration and registers
   fsim.elaboration.vhdl_coverage_points. The diagnostic catalog, resource
   contract, source manifest, coverage ledger, and feature-matrix README own
   the surface. It remains internal and adds no installed API.
3. discover_vhdl_statement_points accepts a retained sequential or concurrent
   Statement forest, requires Language::Vhdl2008 as the current frontend family
   marker, validates Vhdl1987, Vhdl1993, Vhdl2000, Vhdl2002, or Vhdl2008, and
   maps exact physical source names to authenticated Change 3 identities. Every
   point retains the Change 4 VHDL ID, original StatementKind, exact byte span,
   and source-map index. The source revision is deliberately not identity input.
4. The iterative lexical depth-first walk visits ordinary bodies, false bodies,
   and all case alternatives. VHDL assignments, force/release, if/case/loop,
   exit/next, returns, procedure calls, assertions, waits, and reports are
   executable points. Block, Null, declarations, and Verilog/SystemVerilog-only
   statement kinds produce none. Static-elaboration exclusions remain Change
   14 ownership.
5. FSIM-COV-005 owns non-VHDL families, unknown revisions, empty or duplicate
   physical source keys, invalid authenticated identities, unknown statement
   sources, invalid spans, duplicate IDs, and source, statement, or nesting
   ceiling failures. Rejection clears partial points, and internal allocation
   failures map to the bounded resource result.
6. The independently authored common VHDL-87 corpus parses under all five
   retained profiles. It proves nine sequential and two concurrent executable
   points across assignments, if/else, case, loop, wait, and assertion forms;
   declarations and nulls are excluded. The corpus also proves identical IDs
   across source revisions, checkout relocation, direct Change 4 equivalence,
   language/revision routing, source-map failures, invalid spans, duplicate
   points, and every resource ceiling.
7. The first focused accumulated run found only a source-manifest ordering
   error: vhdl_coverage_points.cpp preceded the alphabetically earlier
   vhdl_array_boundary.hpp. Moving the new path after vhdl_component_profiles.hpp
   repaired the manifest without changing implementation or test semantics.
8. The final eight-worker warnings-as-errors exact-LLVM Debug target builds in
   5.64 seconds at 1,019,792 KiB peak RSS with zero swaps. Its retained
   build/llvm22-ninja-debug/batch178-change06-build.log is warning/error clean at
   SHA-256
   19dd10bc04bd8c20a823a686c679a6371625fa1978364cd8a81bc554cd86099b.
   The VHDL discovery test passes 1/1 in 0.01 CTest and wall seconds at 19,936
   KiB peak RSS with zero swaps; retained
   build/llvm22-ninja-debug/batch178-change06-test.log has SHA-256
   f644833ddd828967a5575f4f4d14f12911a9f5be5b5fdfc13e5f252a622f03bd.
9. The accumulated runtime model, source and point identities, both language
   statement discoverers, coverage inventory, diagnostic catalog, resource
   portability, source-line budget, source-package manifest, and CTest command
   uniqueness slice passes 11/11 in 6.53 CTest and wall seconds at 21,956 KiB
   peak RSS with zero swaps. Its warning, error, failure, crash, and
   sanitizer-marker-clean retained log is
   build/llvm22-ninja-debug/batch178-change06-focused-final.log at SHA-256
   8c0789e2ca6f28a71effd5dcd0828d8995af5778f4e188d5ef14102552f03eaa.
10. COVBASE-C02 through COVBASE-C06 are preserved and twelve rows remain active.
    The normalized ledger SHA-256 is
    d59b49067d8762ca5cb30308ac396b8b1ccb50dd57975b85c52434ed48ad0fd1.
    The deterministic source manifest contains exactly 1,517 paths at SHA-256
    d1624162d805841a421d7b231f86dc9c7a8bcd1ea00bb1a1181bc72e609c1f29.
11. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 7: model
    decision branches and individually addressable branch arms. Preserve all
    Change 1-6 work and do not begin line-state derivation from Change 8 in the
    same bounded slice.

## Batch 178 active checkpoint - after Change 7

1. Resume in the fsim repository on codex/v3 at the intentionally dirty Batch
   178 worktree. A user-directed performance detour fast-forwarded and pushed
   codex/v3 through 0ad6b45444fff7914e0090d9b11354f4e44e43f5 while preserving
   every uncommitted coverage path. Changes 1-7 are complete and accumulated;
   do not reset, commit, or push them before Change 20.
2. Change 7 adds include/fsim/elaboration/coverage_branches.hpp,
   src/elaboration/coverage_branches.cpp, and
   tests/elaboration/coverage_branches_test.cpp. CMake includes the
   implementation in fsim_elaboration and registers
   fsim.elaboration.coverage_branches. Diagnostics, resource policy, source
   package ownership, the coverage ledger, and the feature-matrix README own
   the new internal surface; no installed API or runtime counter exists yet.
3. discover_coverage_branch_points walks retained Verilog, SystemVerilog, and
   VHDL statement forests in lexical depth-first order. If decisions emit true
   and explicit-false or implicit-false arms. Case decisions emit every
   alternative and an implicit no-match arm when no default exists.
   Conditional, repeat, and bounded loops emit body and completion arms;
   unconditional forever and VHDL loop forms emit none.
4. Every branch point retains its canonical Change 4 identity, language,
   decision family, arm construct, exact authenticated source span, source
   index, and lexical arm index. Verilog and SystemVerilog remain distinct
   identity domains. Checkout location, traversal allocation, and counter state
   do not enter point identity.
5. FSIM-COV-006 owns invalid language, malformed explicit arm, empty,
   duplicate, unknown, or unauthenticated source mappings, invalid spans,
   duplicate point IDs, and source, statement, arm, nesting, or allocation
   limits. Every rejection clears partial output. Limits default to 65,536
   sources, 1,048,576 statements and arms, and 4,096 nesting levels.
6. The independently authored SystemVerilog corpus proves explicit and
   implicit if arms, case/default/no-match arms, conditional-loop arms,
   unconditional-loop exclusion, lexical order, language-separated identity,
   and direct Change 4 equivalence. The VHDL corpus proves equivalent arm
   semantics and relocation. Negative tests cover every diagnostic and resource
   family without using private-reference text.
7. The first post-performance-merge build retained
   build/llvm22-ninja-debug/batch178-change07-build.log at SHA-256
   089c7a24d8fd2b3adf6546e93bdbd1cbbd37505b9f93bd9e5d6e150be6f91819.
   It exposed that compact RareVector case-alternative storage intentionally
   has no reverse iterators while the accumulated Change 5-7 walkers still used
   rbegin/rend. All three walkers now schedule alternatives by descending index,
   retaining lexical output without whole-container snapshots.
8. The broad recovery build completed with eight workers in 2 minutes 28.27
   seconds at 2,466,844 KiB peak RSS and zero swaps. The final warning-clean
   three-discovery-target build completes in 5.06 seconds at 351,164 KiB peak
   RSS with zero swaps; its retained
   build/llvm22-ninja-debug/batch178-change07-build-clean.log SHA-256 is
   144167f889ce98db0146b2373c25be6735ff1561f7816af92554bfc389a31eab.
9. The Verilog/SystemVerilog statement, VHDL statement, and branch-arm tests
   pass 3/3 in 0.02 CTest and wall seconds at 20,132 KiB peak RSS with zero
   swaps. The retained marker-clean
   build/llvm22-ninja-debug/batch178-change07-discovery-tests.log SHA-256 is
   4645a8e2ad82f94b16f71ca7a9e6977a36a48e4627015aaed007c833d3d252fe.
10. The accumulated model, source and point identity, both statement
    discoverers, branch arms, coverage inventory, diagnostic catalog, resource
    portability, source-line budget, source-package manifest, and CTest command
    uniqueness slice passes 12/12 after documentation in 6.65 CTest and wall
    seconds at 21,724 KiB peak RSS with zero swaps. Its warning, error, failure,
    crash, and sanitizer marker-clean retained log is
    build/llvm22-ninja-debug/batch178-change07-focused-final.log at SHA-256
    63bafcf367a4c73df4ca01fe48f2f2780acb9968d4f1ed271a338b1544ccbc47.
11. COVBASE-C02 through COVBASE-C07 are preserved and eleven rows remain active.
    The normalized ledger SHA-256 is
    ac76be94a2b5858f26264ddbc73ae66b0f8d3e6a4b0065b32d6e70e238ced658.
    After the merged translation-unit split inventory, the deterministic source
    manifest contains exactly 1,546 paths at SHA-256
    ff9fe4d8da06ad14d461df4b9bc89466dce7962cde17d2a063cc18d4dbefc5b1.
12. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, coverage implementation commit, or coverage push ran. Proceed
    only to Batch 178 Change 8: derive covered, partial, and uncovered line
    states from statement points. Preserve all Change 1-7 work and do not begin
    elaborated-instance attachment from Change 9 in the same bounded slice.

## Batch 178 active checkpoint - after Change 8

1. Resume in the fsim repository on codex/v3 at the intentionally dirty Batch
   178 worktree. The pushed branch remains at
   0ad6b45444fff7914e0090d9b11354f4e44e43f5. Changes 1-8 are complete and
   accumulated; preserve them without reset, commit, or push before Change 20.
2. Change 8 adds include/fsim/runtime/coverage_line_state.hpp,
   src/runtime/coverage_line_state.cpp, and
   tests/runtime/coverage_line_state_test.cpp. CMake includes the implementation
   in fsim_runtime and registers fsim.runtime.coverage_line_state. Diagnostics,
   resource policy, source-package ownership, the coverage ledger, and the
   feature-matrix README own the new internal surface; no installed API,
   database, elaborated inventory, or execution counter is added.
3. VerilogStatementCoveragePoint and VhdlStatementCoveragePoint now retain the
   one-based source start line from the parsed statement span. The line is
   metadata, not Change 4 identity input. Start-line ownership ensures a
   multiline control statement does not manufacture executable continuation
   lines or contaminate nested body-line scoring.
4. derive_code_coverage_line_states accepts aligned statement sites and
   CodeCoveragePointResult entries. Each site retains the statement point and
   counter owner, source index, and physical line. Branch metrics, mismatched
   point/counter owners, invalid identities, duplicate points, partial/empty or
   incoherent hit states, and zero line numbers fail transactionally through
   FSIM-COV-007.
5. Points are sorted in place first by point identity to reject duplicates and
   then by source/line/identity to produce deterministic output independent of
   discovery order. No associative container, line point ID, line counter, or
   whole-container snapshot is created. The output owns only per-line component
   counts/status plus one exact CodeCoverageMetric::Line summary.
6. A scored line is covered when all nonexcluded points are covered, uncovered
   when none is covered, and partial otherwise. A line is excluded only when
   every owned point is excluded. Consequently covered-plus-excluded remains
   covered, uncovered-plus-excluded remains uncovered, and excluded points
   never dilute the scored state.
7. Explicit defaults bound 65,536 sources, 1,048,576 statement points,
   1,048,576 derived lines, and physical line numbers through 2^31. Size,
   source-index, line-number, unique-line, and allocation failures return a
   cleared ResourceLimit result. The resource contract independently freezes
   the implementation and positive/negative ceiling witnesses.
8. The first warnings-as-errors exact-LLVM Debug build of the line-state and
   both statement-discovery targets completes with eight workers in 7.88
   seconds at 925,812 KiB peak RSS with zero swaps. Its retained
   build/llvm22-ninja-debug/batch178-change08-build.log is marker-clean at
   SHA-256
   0d1a66fac5f36b134de3c729f0cbf748130bb2330fd0ec75b98fee9bf28186df.
9. The first three-test run correctly found a test-only state leak: after an
   excluded point was deliberately given an invalid hit, the same mutated
   corpus reached the later unique-line resource assertion. Resetting the
   corpus before resource tests changed no production semantics. The final
   statement-line and both discovery tests pass 3/3 in 0.02 CTest and wall
   seconds at 20,148 KiB peak RSS with zero swaps. The retained marker-clean
   build/llvm22-ninja-debug/batch178-change08-tests-final.log SHA-256 is
   1ee1d23c4bc6d5fc57adae127cb12f38323b17a363479f43a70997535eeff0a4.
10. The first accumulated run passed every owner except the inventory's
    deliberately frozen per-change state/count expectations. Updating its
    completed change from 7 to 8 and its exact seven-preserved/ten-active totals
    repaired the independent freeze after the normalized digest had already
    detected the row transition.
11. The accumulated model, source and point identity, both statement
    discoverers, branch arms, line states, coverage inventory, diagnostic
    catalog, resource portability, source-line budget, source-package manifest,
    and CTest command uniqueness slice passes 13/13 after documentation in 6.65
    wall seconds at 22,000 KiB peak RSS with zero swaps. Its marker-clean
    build/llvm22-ninja-debug/batch178-change08-focused-postdoc.log SHA-256 is
    f253ece171aec4764becbac09dee3fb4daa24e9a276a6fdf76722cf9987f1be4.
12. COVBASE-C02 through COVBASE-C08 are preserved and ten rows remain active.
    The normalized ledger SHA-256 is
    e3e371762cb273ce3805cfe0e8a11ee18bd94f879dd2691a8ac95dfeb254fc79.
    The deterministic source manifest contains exactly 1,549 paths at SHA-256
    56938d51923500842e81a6720175a29654df8099ce399d8340371aed8d64e9f9.
13. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 9: attach
    immutable coverage inventories to elaborated design instances. Preserve
    all Change 1-8 work and do not begin the SimIR coverage-hit operation from
    Change 10 in the same bounded slice.

## Batch 178 active checkpoint - after Change 9

1. Branch `codex/v3` remains based on local and remote `0ad6b45`. Batch 178
   Changes 1-9 are accumulated in the intentionally dirty worktree. Preserve
   every existing change without reset, commit, push, full Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 9 adds include/fsim/elaboration/coverage_inventory.hpp,
   src/elaboration/coverage_inventory.cpp, and
   tests/elaboration/coverage_inventory_test.cpp. fsim_elaboration owns the
   implementation and fsim.elaboration.coverage_inventory owns focused
   positive, negative, resource, state, instance, and language-family evidence.
   Diagnostics, resource policy, the source-package manifest, coverage ledger,
   and feature-matrix README freeze the new internal surface.
3. CoverageInventoryOwner is projected only from each dense
   SpecializationInfo: specialization ID, canonical instance path, language,
   primary source, and source dependencies. Coverage attachment never recovers
   hierarchy or ownership from process-name strings. An attachment must supply
   exactly one draft for every specialization, including a deliberately empty
   draft for an instance with no executable points.
4. Each draft point owns one stable Change 4 point ID, statement or branch
   metric, source index, exact nonempty byte span, and one-based physical line.
   Its source must be the specialization's primary source or an explicit
   dependency. Duplicate point IDs are rejected within an instance but remain
   valid across repeated instances of the same source construct.
5. Arbitrary draft order is normalized through one bounded vector of draft
   pointers indexed by specialization. No draft or point container snapshot is
   created. Only the final retained instance point vectors are materialized,
   sorted by stable point identity, and assigned dense design-global uint32
   counters in specialization/point order. Repeated source point IDs therefore
   retain distinct instance-local counter ownership deterministically.
6. ElaboratedDesign stores an optional CodeCoverageInventory. It is absent for
   ordinary elaboration, exposed only through a const accessor, and replaced
   only after the complete candidate validates. A failed second attachment
   leaves the prior inventory unchanged. Change 17 still owns manifest/CLI
   enablement and the proof that disabled execution has no hot-path overhead.
7. ElaboratedDesignState copies or moves the optional inventory. from_state
   reconstructs the authoritative owner projection and rejects incomplete,
   noncanonical, source-mismatched, counter-mismatched, or total-mismatched
   state before runtime validation. Change 18 still owns v3 object/design
   serialization and cache identity; Change 9 adds no v2 reader or migration.
8. FSIM-COV-008 transactionally owns empty/duplicate/invalid sources; invalid
   owners; missing, duplicate, or unknown instances; source-ownership, point,
   metric, span, line, duplicate-ID, counter, order, and total failures.
   Explicit defaults bound 65,536 sources, 1,048,576 instances, 1,048,576 total
   points, physical lines through 2^31, and the uint32 counter space. Allocation
   and length failures report ResourceLimit without publishing partial state.
9. The focused test proves arbitrary input-order equivalence, canonical dense
   counters, empty owners, repeated-instance IDs, VHDL/Verilog/SystemVerilog
   language-family retention, every negative/resource family, actual
   SystemVerilog top/two-child attachment, transactional replacement, immutable
   access, and valid/corrupt design-state round trips. Its final standalone run
   passes 1/1 in 0.01 CTest seconds at 20,208 KiB peak RSS with zero swaps.
10. The first compile exposed that the pre-existing CodeCoveragePoint model has
    no aggregate equality operator; explicit inventory equality over ID,
    metric, counter, and source metadata repaired the local contract. The next
    compile exposed only an overloaded ranges-comparator deduction ambiguity;
    typed lambdas repaired it. The first runtime assertion assumed the raw unit
    token `leaf`, while specialization units use canonical spelling; selecting
    authoritative child instance paths repaired the test without production
    changes.
11. The public ElaboratedDesign ownership-header change triggered a 102-step
    warnings-as-errors exact-LLVM Debug rebuild with eight workers. It passed in
    2:02.02 at 2,471,232 KiB peak RSS with zero swaps. Review then removed the
    builder's deep draft/point snapshot in favor of a pointer index and final
    owned-vector sort. The current incremental target rebuild passes in 6.75
    seconds at 1,001,372 KiB peak RSS with zero swaps; its marker-clean retained
    build/llvm22-ninja-debug/batch178-change09-build-final3.log SHA-256 is
    7ec5b28487a6b74559dcb4cd76147da4e3a74cbe7e03aa4e872f46b4f644702a.
12. The final post-documentation accumulated source/point identity, both statement
    discoverers, branch, line-state, instance-inventory, language-neutral model,
    ledger, diagnostic, resource, source-line, source-package, and CTest command
    uniqueness slice passes 14/14 in 6.56 wall seconds at 21,920 KiB peak RSS
    with zero swaps. Its marker-clean retained
    build/llvm22-ninja-debug/batch178-change09-focused-postdoc.log SHA-256 is
    90e1fe422e274065afa5889b6dbbf44e1d4b05e2021780980a30412b4bda54f1.
13. COVBASE-C02 through COVBASE-C09 are preserved and nine rows remain active.
    The normalized ledger SHA-256 is
    15d03a6bf5cfe44cb14c2460e7f87c6f47753a89dbf5f0b715626471c289a8ee.
    The deterministic source manifest contains exactly 1,552 paths at SHA-256
    fbe9422409f3709c51ef39062deda6ecd4b5eda48bdfb4cec62b9e54c11b7a19.
14. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 10: add a
    validated SimIR coverage-hit operation. Preserve all Change 1-9 work and do
    not begin interpreter counter execution from Change 11 in the same bounded
    slice.

## Batch 178 active checkpoint - after Change 10

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-10. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 10 adds include/fsim/runtime/simir_coverage.hpp,
   src/runtime/simir_coverage.cpp, and tests/runtime/simir_coverage_test.cpp.
   fsim_runtime owns the implementation and fsim.runtime.simir_coverage owns
   focused positive, negative, resource, ownership, and sharing evidence.
3. CodeCoverageHit is appended after the ten existing output-operation
   alternatives. It owns only a stable CodeCoveragePointId, a statement or
   branch CodeCoverageMetric, and a dense CodeCoverageCounterId. It has no
   register, signal, object, container, time, or scheduler operand and thus
   cannot name unrelated mutable simulation state.
4. validate_code_coverage_hit checks one hit against its exact point, metric,
   and counter owner. validate_code_coverage_hits checks the complete const
   process operation stream against one instance's canonical point slice:
   nonzero IDs, executable metrics, sorted unique point identities, contiguous
   counters, design-global bounds, exact ownership, and one instrumentation hit
   per point.
5. Validation publishes no state and maps bounded point/operation/instruction
   overflow plus allocation or length failure to ResourceLimit under
   FSIM-COV-009. Defaults permit at most 1,048,576 instance points and
   16,777,216 operations, with InstructionIndex remaining uint32.
6. Repeated hierarchy instances retain one immutable SimIR body. OperationList
   stores only an instruction/counter pair when equivalent point operations
   differ by dense instance counter. Validation and expanded artifact/debug
   views restore the effective counter. Re-sharing, full replacement, and
   copy-on-write materialization retain the correct instance value without a
   boxed output-operation copy.
7. LLVM structural validation rejects empty point IDs and non-executable
   metrics. Until Changes 11-12 install execution services, both direct and
   compiled paths terminate explicitly at an unavailable code-counter service;
   the compiled form is an explicit host boundary. The native cache key retains
   the operation's point, metric, and counter, and the design-artifact enum
   validator recognizes the language-neutral metric domain.
8. The focused test proves typed operation/group identity, exact statement and
   branch ownership, invalid identity/metric/counter/point families, malformed
   owner order and density, duplicate instrumentation, both resource ceilings,
   unrelated-state immutability, counter-only structural sharing, repeat
   sharing, full replacement, and copy-on-write materialization. It passes 1/1
   in 0.01 CTest seconds at 20,252 KiB peak RSS with zero swaps.
9. The public SimIR variant change required a warnings-as-errors exact-LLVM
   22.1.8 Debug fsim build spanning all 480 affected elaboration, LLVM,
   artifact, application, and CLI steps. Eight workers completed it in 4:57.79
   at 3,913,628 KiB peak RSS with zero swaps. Its marker-clean retained
   build/llvm22-ninja-debug/batch178-change10-fsim-build-initial.log SHA-256 is
   82a599af69d21f7abdf43d863d08432a8a6c5c3e1b9c22e1f8161646e0dd741f.
10. Review found and repaired two subtle sharing cases: an already-shared
    candidate must contribute its effective override during re-sharing, while
    a full operation replacement must supersede the old compact override. The
    final nine-step incremental fsim plus focused-target rebuild passes in
    17.77 seconds at 1,506,332 KiB peak RSS with zero swaps; its retained log
    SHA-256 is
    6931b13af8d195f04af93ddb4e7070f8e05c98e75a57a48abb70b66655a8dbc5.
11. The first accumulated 15-owner run exposed only source-manifest ordering
    for the two new alphabetic paths; moving them to their canonical positions
    repaired the generated inventory without production changes. The final
    post-documentation accumulated source/point identity, both statement
    discoverers, branch, line-state, instance-inventory, SimIR-hit,
    language-neutral model, ledger, diagnostic, resource, source-line,
    source-package, and CTest command-uniqueness slice passes 15/15 in
    6.79 wall seconds at 22,304 KiB peak RSS with zero
    swaps. Its marker-clean retained
    build/llvm22-ninja-debug/batch178-change10-focused-postdoc.log SHA-256 is
    d34714af23ed9a0b29f283a688e45619822c466dedb2f59ba53d6349b3bfb38f.
12. COVBASE-C02 through COVBASE-C10 are preserved and eight rows remain active.
    The normalized ledger SHA-256 is
    6ceff8253c7e3d41e6d5fb6dd3812d1163e82eaf3b3676e1dc0a9f33f1c22855.
    The deterministic source manifest contains exactly 1,555 paths at SHA-256
    af4e87a0951ad69343cb5d73f49df98d52f216c4e28487006e363748f5c1cab4.
13. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 11:
    implement saturating interpreter counters with overflow reporting.
    Preserve all Change 1-10 work and do not begin LLVM counter lowering from
    Change 12 in the same bounded slice.

## Batch 178 active checkpoint - after Change 11

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-11. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 11 extends include/fsim/runtime/simir_coverage.hpp,
   src/runtime/simir_coverage.cpp, the Interpreter surface/implementation, and
   tests/runtime/simir_coverage_test.cpp. No new source-manifest path is added.
3. CodeCoverageCounters owns one dense vector of uint64 values and one
   same-sized byte overflow table. reset transactionally validates the
   1,048,576-point default ceiling, allocates replacement flags before
   publication, clears prior overflow state, and deliberately distinguishes an
   unconfigured service from a configured empty table.
4. record performs no allocation. Values below UINT64_MAX increment exactly
   once. The first attempt beyond UINT64_MAX leaves the value saturated, marks
   the counter, increments the bounded overflow count, and returns
   FirstOverflow. Later attempts return Saturated without wrapping or
   re-reporting. Missing and out-of-range tables have distinct results.
5. Interpreter counters are installed or restored only before start and remain
   observable as a const span plus per-counter/total overflow queries. The
   optional overflow hook receives the exact dense counter identity only on
   FirstOverflow. FSIM-COV-010 owns missing, out-of-range, excessive-table, and
   saturation diagnostics.
6. Direct CodeCoverageHit execution resolves OperationList's effective compact
   instance counter, records once, invokes first-overflow reporting if needed,
   and advances one PC. A configured ordinary statement/branch pair therefore
   changes only its two exact counters. Missing and out-of-range services fail
   before advancing. Compiled execution remains explicitly unavailable at the
   host boundary so Change 12, rather than Change 11, owns native LLVM lowering.
7. The focused test now also proves unconfigured/out-of-range store results,
   final representable increment, first/repeated overflow, sticky no-wrap state,
   bounded transactional reset, ordinary two-counter interpreter execution,
   exact first-overflow callback, direct failure diagnostics, and execution of
   the effective compact hierarchy-instance override. It passes 1/1 in 0.01
   CTest seconds.
8. The public Interpreter change required a warnings-as-errors exact-LLVM
   22.1.8 Debug fsim build spanning all 460 affected elaboration, LLVM,
   artifact, application, and CLI steps. Eight workers completed it in 4:52.21
   at 2,789,560 KiB peak RSS with zero swaps. Its marker-clean retained
   build/llvm22-ninja-debug/batch178-change11-fsim-build.log SHA-256 is
   8c4b84f1baf7434d8b08634b23646d55ffbccf604f3f4d9d3238b4cb7f14a1be.
9. After adding the stable FSIM-COV-010 identity and transactional-replacement
   assertion, the final 53-step runtime, focused-test, and fsim relink passes in
   25.38 seconds at 1,506,624 KiB peak RSS with zero swaps. Its retained
   build/llvm22-ninja-debug/batch178-change11-build-final.log SHA-256 is
   9ce3f3affc56db56eb34451620ed3143fdf1160605cb9dd149ada7d3eec745f7.
10. The first accumulated run then exposed that the new public bridge methods
    pushed the pre-existing general interpreter source to 2,529 lines, above
    the 2,500-line hard ceiling. Moving those definitions into the dedicated
    coverage implementation restored ownership cohesion and the line budget.
    The current six-step exact-source rebuild and relink passes in 6.89 seconds
    at 1,506,592 KiB peak RSS with zero swaps; its retained
    build/llvm22-ninja-debug/batch178-change11-build-final2.log SHA-256 is
    a414098c42629d7bd47b6086275036d746e8f22ab698c7401cf5ab8b5f814800.
11. The final post-documentation accumulated 15-owner slice passes 15/15 in
    7.11 wall seconds at 22,276 KiB peak RSS with zero swaps. Its
    marker-clean retained
    build/llvm22-ninja-debug/batch178-change11-focused-postdoc.log SHA-256 is
    f1391a693b4e8b87117640d96f70c1d086df024c6418c48b0a9e4d9a604f2a37.
12. COVBASE-C02 through COVBASE-C11 are preserved and seven rows remain active.
    The normalized ledger SHA-256 is
    f46e9700c20df9868cf345d231085bc2b43516d46a6b906bcc51ee192d6a80a0.
    The deterministic source manifest remains exactly 1,555 paths at SHA-256
    af4e87a0951ad69343cb5d73f49df98d52f216c4e28487006e363748f5c1cab4.
13. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 12: lower
    equivalent counters through LLVM O0-O3. Preserve all Change 1-11 work and
    do not begin Debug-engine coverage ownership from Change 13 in the same
    bounded slice.

## Batch 178 active checkpoint - after Change 12

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-12. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 12 adds src/compiler/llvm_jit_coverage.cpp/.hpp,
   tests/compiler/llvm_jit_coverage_test.cpp, and
   tests/app/code_coverage_application_test.cpp. Those four alphabetic paths
   are now owned by the deterministic source manifest; existing LLVM, runtime,
   application, C ABI, CMake, diagnostic, and portability-contract surfaces
   carry the integration.
3. Covered validated processes publish a compact per-instruction map from a
   shared native body to each hierarchy instance's effective dense counter.
   The v3 native runtime appends that map, its length, the simulation-owned
   dense uint64 counter table and length, and a checked coverage callback. The
   C ABI owns offsets 816, 824, 832, 836, and 840 and size 848.
4. Generated O0-O3 code loads the effective counter, bounds-checks both runtime
   tables, and directly increments the dense uint64 value. The ordinary path
   invokes no host callback. UINT64_MAX never wraps; saturation enters the
   checked service so the simulation-owned flag and exact first-overflow hook
   retain Change 11 semantics. Unavailable or out-of-range storage becomes a
   typed generated-runtime failure rather than unchecked native access.
5. CodeCoverageHit is no longer a compiled host execution boundary. A generic
   boundary failure remains as a safety trap, but validated covered processes
   lower the operation natively. Uncovered processes create no hit map.
   Removing the instance-specific dense counter from native cache hashing lets
   equivalent hierarchy instances share one compiled body while their compact
   maps retain exact results.
6. Compiler tests run the generated body at O0, O1, and O2 and prove a `[5,2]`
   compact override, ordinary direct increments with zero checked callbacks,
   sticky saturation, and unavailable-storage failure. Application tests prove
   the same contract through the real LlvmProcessExecutor/Interpreter bridge at
   O0, O1, and O2 and explicitly assert that the existing application O3
   selection uses the O2 optimization pipeline.
7. The public runtime/application changes and all new tests build cleanly under
   the exact LLVM 22.1.8 warnings-as-errors Debug configuration with eight
   workers. The final incremental target build completed nine steps in 14.3
   seconds.
8. The accumulated Change 2-12 source/point identity, both statement
   discoverers, branch, line-state, instance inventory, SimIR hit,
   language-neutral counter model, C runtime ABI, LLVM, application, ledger,
   diagnostic, resource, source-line, and source-package slice passes 16/16 in
   22.21 wall seconds at 188,788 KiB peak RSS with zero swaps.
9. COVBASE-C02 through COVBASE-C12 are preserved and six rows remain active.
   The normalized ledger SHA-256 is
   88c765c97812ce58334f9020683b968ea83367ed2b7f4a1c37d55e7f0031f0c8.
   The deterministic source manifest contains exactly 1,559 paths at SHA-256
   c672ba9c52ff209c63a9797c00e3007da79d0a6e054eb0ca19b79eaeb0ec0506.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 13:
    preserve point identity and hits in the Debug engine. Preserve all Change
    1-12 work and do not begin Change 14 exclusion ownership in the same
    bounded slice.

## Batch 178 active checkpoint - after Change 13

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-13. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 13 adds src/app/application_debug_coverage.cpp and extends the
   application-internal declarations, application target, existing code
   coverage application test, test labels, resource contract, ledger, and
   source manifest. The Debug coverage source is the sole new manifest entry.
3. debug_code_coverage_snapshot is a bounded read-only projection over one
   process's effective CodeCoverageHit operations. Each observation retains
   the exact instruction, 128-bit point ID, metric, compactly resolved dense
   counter ID, and current hit count. Snapshot capture cannot execute an
   operation, advance a PC, clear a stop, or mutate a counter.
4. Snapshot construction rejects invalid point/metric identity, duplicate
   points, unavailable or out-of-range dense counter storage, instruction
   overflow, allocation/length failure, and the explicit default 1,048,576
   point ceiling. Every failure clears partial observations before return.
5. The focused differential executes the same DebugPoint/CodeCoverageHit body
   in the direct interpreter and in LLVM O0 with debug instrumentation enabled.
   Both stop at coverage_debug.sv:17:5 in lexical scope top.covered before the
   covered statement, where the exact point has zero hits. After clear/resume,
   both complete through the same point and counter with exactly one hit.
6. Additional evidence proves the snapshot resolves a compact shared-body
   instance override from counter zero to counter five without changing the
   point ID and rejects missing storage, duplicate identities, and a zero-point
   ceiling. The existing compiled O0-O3 increment/saturation evidence remains
   unchanged.
7. The exact LLVM 22.1.8 warnings-as-errors Debug fsim and application targets
   rebuild cleanly with eight workers. No Debug instrumentation is added to the
   ordinary compiled engine, and no coverage snapshot is taken unless called
   by Debug observation.
8. The accumulated Change 2-13 source/point identity, statement discovery,
   branch, line-state, instance inventory, SimIR, counter model, C ABI, LLVM,
   application Debug, ledger, diagnostic, resource, source-line, and source
   package slice passes 16/16 in 21.90 wall seconds at 186,340 KiB peak RSS
   with zero swaps.
9. COVBASE-C02 through COVBASE-C13 are preserved and five rows remain active.
   The normalized ledger SHA-256 is
   c33e826346da6c621f54b9e87259e7041c5e9a6fdcf0a9ea4322df169a5aae63.
   The deterministic source manifest contains exactly 1,560 paths at SHA-256
   784eeae5e98184db6eeefe8b3de11346d50813a39bc2d2d260fd707a4d4f185e.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 14:
    exclude non-executable declarations and statically removed constructs.
    Preserve all Change 1-13 work and do not begin Change 15 hierarchical
    instance identity in the same bounded slice.

## Batch 178 active checkpoint - after Change 14

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-14. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 14 adds include/fsim/elaboration/coverage_points.hpp,
   src/elaboration/coverage_points.cpp, and
   tests/elaboration/coverage_points_test.cpp. CMake, the two existing language
   statement-discovery tests, the resource contract, ledger, and source
   manifest carry the remaining integration. Those three new alphabetic paths
   increase the deterministic manifest to 1,563 entries.
3. CoveragePointExclusion identifies either a non-executable declaration or a
   statically removed elaboration construct by canonical source index and exact
   source interval. The exclusion pass validates input point identities,
   metrics, lines, sources, spans, uniqueness, and explicit 1,048,576 point and
   exclusion ceilings before publishing a result.
4. Valid exclusion intervals are sorted and merged per source. A statement or
   branch point is removed only when its complete span is contained by the
   exclusion union. A containing executable decision therefore remains, as do
   all neighboring IDs, metrics, spans, lines, and lexical input ordering. The
   pass never regenerates a point ID or assigns a counter.
5. Duplicate exclusions, invalid kinds, empty/out-of-source spans, unknown or
   invalid sources, malformed or duplicate points, allocation/length failure,
   and point/exclusion budget overflow clear all partial output and return a
   typed failure. Empty and declaration-only exclusions preserve every point.
6. The existing independently authored Verilog/SystemVerilog corpus now
   asserts that its procedural integer declaration has no point in both
   language modes. The common VHDL-87 corpus asserts the same for its process
   variable under VHDL-87, -93, -2000, -2002, and -2008.
7. Focused language-neutral evidence retains an enclosing executable point and
   its live neighbor exactly while removing one statically absent statement and
   its branch point. It also covers declaration-only no-op behavior, empty
   filters, every typed validation family, and both resource ceilings.
8. The exact LLVM 22.1.8 warnings-as-errors Debug fsim, exclusion, and language
   discovery targets build cleanly with eight workers. The accumulated Change
   2-14 slice passes 17/17 in 21.85 wall seconds at 190,808 KiB peak RSS with
   zero swaps.
9. COVBASE-C02 through COVBASE-C14 are preserved and four rows remain active.
   The normalized ledger SHA-256 is
   f5249bede2e64f2b4224b394d455cea1c7c80e848c972d2d6a00b65fabbb5a0a.
   The deterministic source manifest contains exactly 1,563 paths at SHA-256
   7c5057a6bb0d81d656388c7f0c8153447163ccf174ff2b7b9dbee44facb3da5c.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 15:
    assign stable hierarchical instance identities. Preserve all Change 1-14
    work and do not begin Change 16 source aggregation in the same bounded
    slice.

## Batch 178 active checkpoint - after Change 15

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-15. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 15 adds
   include/fsim/elaboration/coverage_instance_identity.hpp,
   src/elaboration/coverage_instance_identity.cpp, and
   tests/elaboration/coverage_instance_identity_test.cpp. CMake, the coverage
   inventory/state seam, diagnostics, resource contract, ledger, and source
   manifest carry the remaining integration. Those three new alphabetic paths
   increase the deterministic manifest to 1,566 entries.
3. `fsim-code-coverage-instance-v3` hashes the canonical hierarchy path,
   retained HDL profile, logical library, elaborated unit, and semantic
   parameter identities into a 128-bit ID. All strings are length-delimited
   and parameter inputs are sorted before hashing; specialization ordinals,
   allocation order, source locations, and checkout paths are excluded.
4. The constructor rejects empty or embedded-NUL hierarchy, an unknown
   language, missing library/unit, malformed or duplicate parameter identity,
   zero identity, allocation/length failure, and explicit hierarchy, library,
   unit, parameter-count, and parameter-byte ceilings transactionally.
5. Every retained CoverageInstanceInventory owns the stable ID. Construction
   rejects duplicate IDs, validation recomputes the expected ID from immutable
   owner data, and ElaboratedDesign state restoration therefore rejects a
   corrupted or semantically mismatched stored identity.
6. Focused synthetic evidence proves parameter-order invariance and distinct
   IDs for different roots, generate indices, profiles, libraries, units, and
   parameter values. A real parameterized SystemVerilog generate is elaborated
   twice and produces distinct root/child IDs with identical results across
   both elaborations.
7. The exact LLVM 22.1.8 warnings-as-errors Debug identity, inventory, fsim,
   LLVM, and application targets build cleanly with eight workers. The focused
   diagnostic, resource, ledger, source-line, and source-package contracts are
   green.
8. The accumulated Change 2-15 source/point/instance identity, statement
   discovery, branch, line-state, instance inventory, exclusion, SimIR,
   counter, LLVM, application Debug, ledger, diagnostic, resource,
   source-line, and source-package slice passes 18/18 in 29.41 wall seconds at
   185,752 KiB peak RSS with zero swaps.
9. COVBASE-C02 through COVBASE-C15 are preserved and three rows remain active.
   The normalized ledger SHA-256 is
   3a365eb191c738797c33b9bc02607a0afd58ffba9b3f02d45ec2ae2b88483e32.
   The deterministic source manifest contains exactly 1,566 paths at SHA-256
   83f3bab8d3c21d169c595db72467c532d30188987a59d48f824804f9adfdd36d.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 16:
    compute source-aggregate unions without losing instance results. Preserve
    all Change 1-15 work and do not begin Change 17 opt-in control in the same
    bounded slice.

## Batch 178 active checkpoint - after Change 16

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-16. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 16 adds include/fsim/runtime/coverage_aggregation.hpp,
   src/runtime/coverage_aggregation.cpp, and
   tests/runtime/coverage_aggregation_test.cpp. Runtime/test CMake,
   diagnostics, the resource contract, ledger, and source manifest carry the
   remaining integration. Those three paths increase the deterministic
   manifest to 1,569 entries.
3. The aggregation pass validates the complete CodeCoverageRun and
   CodeCoverageResult before requiring one dense-counter-ordered owner for
   every point. It rejects incomplete/noncanonical ownership and unknown
   source or instance indices before publishing any output.
4. Every exact point result is retained in its owning instance, including the
   original global counter, hits, and covered/uncovered/excluded status.
   Instance metric counts are independently derived from those retained point
   results, so an instance-level disagreement cannot be erased by aggregation.
5. Equal stable point IDs union only within their canonical source. Any covered
   occurrence covers the source point, all-excluded occurrences exclude it,
   and remaining unions are uncovered. Covered, uncovered, excluded, and
   total occurrence counts remain explicit; hit sums saturate at uint64 max
   with a retained saturation flag.
6. Source points sort deterministically by ID and source/instance metric
   summaries remain separate. There is no grand aggregate and no score across
   statement and branch metrics.
7. Focused evidence proves two instances disagree on one branch while the
   source union covers it and both instance statuses/counters remain exact. It
   also covers distinct sources, exclusion, empty owners, hit saturation,
   point-source/metric conflicts, duplicate instance points, invalid results,
   malformed ownership, and all three resource ceilings.
8. The exact LLVM 22.1.8 warnings-as-errors Debug aggregation, fsim, LLVM, and
   application targets build cleanly with eight workers. The accumulated
   Change 2-16 focused slice passes 19/19 in 22.30 wall seconds at 189,200 KiB
   peak RSS with zero swaps.
9. COVBASE-C02 through COVBASE-C16 are preserved and two rows remain active.
   The normalized ledger SHA-256 is
   19eed8adb905adb0b4bbfb435bae5a50107e2713079b2fdf6bce525cd9025bb1.
   The deterministic source manifest contains exactly 1,569 paths at SHA-256
   8b15040e411ba76571cf2c989e48e79febae73e44f0088a53f7555dce7a11020.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 17: add
    explicit manifest/CLI coverage enablement with no disabled-path overhead.
    Preserve all Change 1-16 work and do not begin Change 18 artifact/cache
    identity in the same bounded slice.

## Batch 178 active checkpoint - after Change 17

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-17. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 17 adds `src/app/application_coverage_control.cpp` and
   `tests/app/code_coverage_control_test.cpp`, updates the project/CLI/
   application models and parsers, and registers the focused application
   owner. Project schema, current examples/tutorials/tests, the manifest
   freeze, resource contract, coverage ledger, and source manifest carry the
   remaining integration.
3. The project manifest is now schema 3. Its optional single `[coverage]`
   table accepts only `enabled = true|false`; omission defaults to false.
   Schema 2 is a deterministic stale input, not a compatibility surface.
   Missing, zero, schema 1, schema 2, future schema 4, and out-of-range inputs
   name schema 3 as the required regeneration identity.
4. `--code-coverage` sets the same immutable configuration bit for project or
   direct HDL compile/elaborate/simulate phases. Check and SystemC-only phases
   reject the option through the existing CLI argument diagnostic. No
   Change 18 artifact serialization or identity work was pulled forward.
5. `code_coverage_enabled(config)` is an allocation-free Boolean projection.
   The built project retains it once at the build boundary. A disabled real
   elaboration proves there is no attached coverage inventory; no counter
   storage, callback, or per-operation disabled-path branch was added.
6. Independently authored focused evidence covers default false, explicit
   true and false, wrong type, unknown key, duplicate table, schema-2
   rejection, manifest-to-CLI override, invalid CLI phase, built-project
   enablement, and disabled elaboration/runtime state.
7. The project manifest freeze authenticates sixteen schema-3 contract rows,
   including the optional Boolean coverage surface, at SHA-256
   `4d858da38980443e2af0c7d8f25a2ff45b299fc155e1908193c8c95beeed064f`.
   It also forbids schema-1/2 comparison paths, migration entry points, and
   legacy writes.
8. The warnings-as-errors exact-LLVM Debug application/project/CLI/fsim
   targets build incrementally with eight workers. The first build rejected
   one test-only range-loop copy warning; changing the loop variable to a
   reference repaired it. No production warning or error remained.
9. The accumulated Change 2-17 slice passes 22/22 in 7.53 wall seconds at
   76,620 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch178-change17-focused-predoc.log` SHA-256 is
   `c68de97b63eb09a23f3a88abfed83fc17553c96a58dca6fda9b55efac3b81a54`.
   The final post-documentation rerun passes the same 22/22 in 7.02 wall
   seconds at 76,388 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch178-change17-focused-postdoc.log` SHA-256 is
   `a1ed4c3508b7c1a996ebada808bec7c1acba9859159fee9b929af25d1d88c869`.
10. COVBASE-C02 through COVBASE-C17 are preserved and only COVBASE-C18
    remains active. The normalized ledger SHA-256 is
    `f757d4d59a1d7e71ef7b66fd816ffd38544bb951f1cc939b52f932f84347bf96`.
    The regenerated source manifest contains 1,568 paths at SHA-256
    `e1a240e411c0cc41bcf324180f86b951b502bfcb8e0713b10d4e84b3b7d4870f`.
11. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 18:
    include coverage configuration/model identity in v3 objects, designs, and
    native cache keys while rejecting versioned v2 inputs directly. Preserve
    all Change 1-17 work and do not begin Change 19 equivalence in the same
    bounded slice.

## Batch 178 active checkpoint - after Change 18

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-18. Do not commit, push, run Release,
   clean-first, sanitizer, or hosted-CI work before Change 20.
2. Change 18 adds `include/fsim/artifact/coverage_identity.hpp`,
   `src/artifact/coverage_identity.cpp`, and
   `tests/artifact/coverage_identity_test.cpp`. Object/design metadata,
   application build/artifact/cache plumbing, LLVM JIT options and cache keys,
   artifact tests, freeze checks, ABI/schema documentation, diagnostics,
   resource ownership, the coverage ledger, and source manifest carry the
   remaining integration.
3. The canonical schema-3 identity binds the immutable enablement bit to model
   `none` when disabled or `fsim-code-coverage-foundation-v3` when enabled.
   A domain-separated SHA-256 digest authenticates schema, bit, and model.
   Schema 2, mismatched model, and altered digest are typed failures.
4. `.fsimobj` is format 7. Its metadata and compilation digest retain the
   coverage identity. `.fsimdesign` is format 12. Its global provenance and
   every ordered object input retain matching identities. Format-6 objects and
   format-11 designs reject directly before payload parsing; no compatibility
   reader, migration, fallback, or dual write exists.
5. Object elaboration compares every loaded identity to the current schema-3
   request before lowering. Design publication derives the identity from the
   built project; design restoration validates it and restores the immutable
   enablement bit. Design-cache keys and `FSIM-DESIGN-CACHE-V3` records retain
   the digest.
6. LLVM native-object key schema `fsim-llvm-native-object-v168` includes the
   coverage identity for process-local and immutable-design modules. Focused
   cache evidence proves disabled hits disabled, while enabled produces a
   separate miss/store and native object.
7. The exact LLVM 22.1.8 warnings-as-errors Debug artifact, application, LLVM,
   and fsim targets build incrementally with eight workers. The compiler found
   no production warning or error. Three first-pass failures were stale
   object/design/schema freeze expectations; updating them to the deliberate
   v3 identities repaired all three.
8. The accumulated Change 2-18 focused slice passes 30/30 in 27.12 wall
   seconds at 189,432 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch178-change18-focused-predoc.log` SHA-256 is
   `2f75e419da52b698c664bacc490d296a0ef6ead1ec5629a9cde811c707be1a8e`.
   The final post-documentation rerun passes the same 30/30 in 32.79 wall
   seconds at 190,060 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch178-change18-focused-postdoc.log` SHA-256 is
   `a395b80e1683fc9a93b2dc0c4dadd430d96330f6e54e41be1b69382cf7a2ee9a`.
9. COVBASE-C02 through COVBASE-C18 are preserved. The normalized ledger
   SHA-256 is
   `e3921c7c2e4a7a18bfdfc2d6984f8dad1105943e27ab73042ba7dce150be7814`.
   The regenerated source manifest contains 1,571 paths at SHA-256
   `e45bb9ac875ae03ce24bc31ac876c80217ce89505be9ea523593a0f7fc17d92f`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 178 Change 19:
    prove Verilog/SystemVerilog/VHDL interpreter, LLVM O0-O3, Debug, instance,
    and source-aggregation equivalence. Preserve all Change 1-18 work and do
    not begin Change 20 closure in the same bounded slice.

## Batch 178 active checkpoint - after Change 19

1. Remain on `codex/v3` with the intentionally dirty cumulative Batch 178
   worktree. Preserve Changes 1-19. Do not commit or push until Change 20 has
   completed every required qualification and documentation owner.
2. Change 19 adds `tests/app/code_coverage_equivalence_test.cpp` and its
   focused application target. Test registration, resource ownership, the
   completed coverage ledger, and source manifest carry the remaining
   integration. No production behavior changed in this slice.
3. The corpus independently authors and parses one Verilog-2005, one
   SystemVerilog-2017, and one VHDL-2008 source. Language-specific discovery
   produces one canonical executable statement point for each retained
   source, and the generic inventory assigns dense counters to two distinct
   hierarchical instances per language.
4. Exactly one instance in each language executes. Its peer remains
   uncovered. The source union therefore retains two occurrences, one covered
   and one uncovered, while scoring the point covered. Per-instance point ID,
   counter, hit count, and status remain independently observable.
5. The serial interpreter snapshot is the reference. LLVM O0, O1, O2, the
   application O3-to-O2 profile, and compiled Debug reproduce the exact counter
   vector, point results, instance results, and source aggregates. Existing
   Change 13 evidence remains the owner for exact Debug pause observations.
6. The exact LLVM 22.1.8 warnings-as-errors Debug equivalence target builds
   incrementally with eight workers. The first execution exposed that
   execution-point hooks can observe both adapter and native Debug boundaries;
   the equivalence assertion was correctly scoped to coverage state rather
   than asserting an unrelated hook count. No production warning or failure
   remained.
7. The accumulated Change 2-19 focused slice passes 31/31 in 27.22 wall
   seconds at 189,868 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch178-change19-focused-predoc.log` SHA-256 is
   `459fd71887b0d5af131774c512deac6663442ea3e89f3cf645ca1aaae63e39cf`.
   After the documentation freeze, the same slice passes 31/31 in 27.03 wall
   seconds at 190,368 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch178-change19-focused-postdoc.log` SHA-256 is
   `4d1f569e0d68ba5424b8a022004dc9cb86ddff88786dbb9565a961861aa6c524`.
8. All seventeen COVBASE-C02 through COVBASE-C18 rows are preserved and every
   declared evidence owner exists. The normalized ledger SHA-256 remains
   `e3921c7c2e4a7a18bfdfc2d6984f8dad1105943e27ab73042ba7dce150be7814`.
   The regenerated source manifest contains 1,572 paths at SHA-256
   `d3ce8dc4beef96671f495f6585920e50880d311ece86ecf20deba6e271a0ab96`.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 178 Change 20: run
   clean full Debug/Release qualification, freeze the foundation inventory and
   documentation, then create the one Batch 178 implementation commit and push
   only after every required local lane is green. Batch 178 is not a sanitizer
   or hosted-CI boundary.

## Batch 178 closed checkpoint - after Change 20

1. Batch 178 Changes 1-20 are complete on `codex/v3`. The foundation covers
   the language-neutral model, relocation-independent source and point
   identities, Verilog/SystemVerilog/VHDL statement discovery, branch and line
   semantics, instance inventories and identities, SimIR/interpreter/LLVM/
   Debug execution, exclusions, aggregation, opt-in controls, v3 artifact and
   cache identities, and mixed-language engine equivalence.
2. Clean full-tree qualification exposed one stale SDF aggregate initializer;
   it now supplies the canonical disabled coverage identity to both design and
   object metadata. The coverage-specific native-cache matrix moved beside
   the LLVM coverage lowering test, leaving the general cache source at 2,486
   lines under the 2,500-line hard limit. Portable stale-schema, SCV design,
   Windows JIT ABI, diagnostic, source, SPDX, and test/control freezes now own
   the deliberate v3 identities and counts.
3. The final exact inventories are 2,560 production diagnostics, 1,218 bounded
   authored sources, 1,507 SPDX-owned files, 454 conformance test/control
   files, and 656 FST test/control files. All seventeen COVBASE-C02 through
   COVBASE-C18 rows are preserved at normalized SHA-256
   `e3921c7c2e4a7a18bfdfc2d6984f8dad1105943e27ab73042ba7dce150be7814`.
   The source manifest contains 1,572 paths at SHA-256
   `d3ce8dc4beef96671f495f6585920e50880d311ece86ecf20deba6e271a0ab96`.
4. The clean warnings-as-errors exact-LLVM Debug build completes 2,497/2,497
   steps with eight workers in 16:00.79 at 4,322,560 KiB peak RSS with zero
   swaps. `build/qualification/batch178-change20-local-debug-build.log` has
   SHA-256
   `30e187c8ae1edcc2e45a48a8f364be6137888c51b8de802aea88c23d3330dbd1`.
5. The sequential Debug suite passes 330/330 in 6:30.85 at 1,099,524 KiB peak
   RSS with zero swaps. Its retained
   `build/qualification/batch178-change20-local-debug-ctest.log` SHA-256 is
   `a343f671a00de5fa705c6788c20a0ce91bbb0da4bc55ba8c177df495652f22fb`.
6. The clean warnings-as-errors LLVM Release build completes 1,302/1,302 steps
   with eight workers in 13:40.12 at 1,891,240 KiB peak RSS with zero swaps.
   `build/qualification/batch178-change20-local-release-build.log` has SHA-256
   `0b56552cab5114f4a85571acace795805dae21f66b2450ed94fa9629ef527b2b`.
7. The sequential Release suite passes 330/330 in 8:51.62 at 1,099,280 KiB
   peak RSS with zero swaps. Its retained
   `build/qualification/batch178-change20-local-release-ctest.log` SHA-256 is
   `602b510a6a259a7e70cdf8a532fba080dc2ae4a3f94f4d7600a64e43d3fae76c`.
8. Both build logs are warning-, error-, failure-, crash-, and sanitizer-marker
   clean. Both complete test logs report 330/330 and zero failures. Batch 178
   is neither a sanitizer nor a non-documentation hosted-CI boundary; do not
   run or claim those lanes here. They remain owned by Batches 180/190 and
   release-closing Change 20s under the governing contract.
9. The next bounded implementation work is Batch 179 Change 1 only: register
   the exact condition, expression, toggle, and FSM obligations. Preserve the
   exactly twenty-change batch structure and do not begin Batch 179 Change 2
   in the same bounded slice.

## Batch 179 active checkpoint - after Change 1

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Change 1 worktree. Preserve this work without reset, commit, or
   push until Batch 179 Change 20.
2. Change 1 adds the exact 17-row
   `tests/feature_matrix/code_coverage_metrics_inventory.tsv` matrix and
   `cmake/CheckCodeCoverageMetricsInventory.cmake` validator, registers
   `fsim.code-coverage-metrics-inventory`, updates source-package and resource
   ownership, and corrects the Batch 178 feature-matrix summary to its closed
   all-preserved state. No metric production behavior changes in this slice.
3. COVMET-C02 through COVMET-C18 assign Changes 2-18 one-to-one across
   Verilog/SystemVerilog and VHDL condition decomposition, short-circuit
   recording, binary and auxiliary unknown outcomes, bounded expression
   combinations, directed binary toggle bins, language object inventories,
   default and explicit memory/array selection, unknown transitions, inferred
   current/next/legal FSM state, SystemVerilog pragmas, VHDL/manifest hints,
   separate state/transition results, and transactional description
   diagnostics.
4. Every row binds IEEE1076, IEEE1364, and IEEE1800, all thirteen retained HDL
   profiles, independently written obligations, safe relative implementation
   and positive/negative/engine/aggregation/artifact owners, the diagnostics
   catalog, and the resource contract. All seventeen rows are active. MC/DC is
   explicitly excluded from the matrix and remains outside Batch 179.
5. The validator enforces exactly twenty v3 batch headings, exactly twenty
   Batch 179 changes, unique row/change/domain identities, exact active-to-
   preserved transition ownership, bounded obligation wording, profile
   completeness, safe paths, private-reference exclusion, feature README and
   CTest registration, and deterministic normalized identity. The matrix
   SHA-256 is
   `ccbe30e82d1fa7954165a516c4930319eca69bb91382a3be291d9cb8805857c2`.
6. The source manifest contains 1,574 ordered paths at SHA-256
   `9b5d9e14c26b2b3f75eef8df4d39c02e634bcb06cc7a59fead02c5e6fe62df98`.
   Source-package, resource-portability, and the unchanged 1,218-source line
   budget gates pass directly.
7. The exact-LLVM 22.1.8 warnings-as-errors Debug tree regenerates with eight
   workers and reports `ninja: no work to do`; this governance-only slice
   changes no compiled source. The registered focused set covers the new and
   foundation inventories, diagnostic catalog, source budget, source package,
   resource contract, and CTest command uniqueness.
8. The focused pre-documentation set passes 7/7 in 6.59 wall seconds at 22,752
   KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch179-change01-focused-predoc.log` SHA-256 is
   `93bedafd9ed7347acffe9c79fa0b011a5668c9fac43cecc962814b7f723b7342`.
   The post-documentation rerun passes the same 7/7 in 7.05 wall seconds at
   22,596 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch179-change01-focused-postdoc.log` SHA-256 is
   `431438923687df2bfd453d4e8e77d1d14e0f41db7c2475c012984ae34d5af01a`.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 179 Change 2:
   decompose Verilog/SystemVerilog decisions into stable atomic conditions.
   Preserve all Change 1 work and do not begin Change 3 in the same bounded
   slice.

## Batch 179 active checkpoint - after Change 2

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-2 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 2 adds
   `include/fsim/elaboration/verilog_coverage_conditions.hpp`,
   `src/elaboration/verilog_coverage_conditions.cpp`, and
   `tests/elaboration/verilog_coverage_conditions_test.cpp`. The public model
   owns typed if/loop/immediate-assertion decisions, ordered logical path
   steps, independently addressable atomic points, exact source spans, lexical
   decision/condition ordinals, and explicit bounded error results.
3. Decomposition is a read-only walk over the retained frontend AST. Logical
   `&&`, `||`, and `!` remain path nodes; every other expression is a
   source-exact atom. Consequently `!(a && b)` yields the two inner atoms while
   preserving the outer negation and inner short-circuit connective, whereas
   comparisons remain whole atoms. Change 4 still owns runtime
   short-circuit-aware recording; no runtime outcome behavior was pulled
   forward.
4. `CodeCoverageConstructKind::AtomicCondition` extends the canonical point
   identity domain without changing existing statement or branch identities.
   The new `FSIM-COV-015` catalog row owns invalid languages, authenticated
   source maps, missing/malformed expressions, source spans, duplicates, and
   resource exhaustion. All publication is transactional.
5. The focused test parses the same independently authored decision corpus in
   Verilog-1995, Verilog-2001, Verilog-2001-no-config, Verilog-2005,
   SystemVerilog-2005, SystemVerilog-2009, SystemVerilog-2012, and
   SystemVerilog-2017. It proves lexical atoms/path shape, AST immutability,
   checkout relocation, language identity separation, immediate assertions,
   composite negation, malformed arity, source authentication, exact spans,
   duplicate containment, and every configured ceiling.
6. COVMET-C02 is preserved and COVMET-C03 through COVMET-C18 remain active.
   The inventory validator requires completed implementation and focused
   positive/negative owners while retaining safe future engine, aggregation,
   and artifact paths for their later integration changes; it does not permit
   placeholder evidence. The normalized matrix SHA-256 is
   `8bd4c99af60d486455f9fa6db513613ac0b28a9c7829fb90d6e96e8a450c36f2`.
7. The source manifest now contains 1,577 ordered paths at SHA-256
   `dbc2341d0eee406ab5ad13636e44a2c731bd6b6ca07fdaa467d5fcd66a091fc3`.
   Source-package, diagnostics, source-line-budget, resource-portability,
   foundation-coverage-inventory, metrics-inventory, and CTest uniqueness
   ownership are green.
8. The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 9/9 in 7.05 wall seconds at 22,560
   KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch179-change02-focused-predoc.log` SHA-256 is
   `c0ae27cd21d1e96d5177ea2c6d52b5852ea029e720a4425eb8a2145ef160ef96`.
   The post-documentation rerun passes 9/9 in 7.65 wall seconds at 22,680 KiB
   peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch179-change02-focused-postdoc.log` SHA-256 is
   `8aebeae90bf597b861aa52ee8eaa00e339b2c63edd95dbface66c78ccf1e307b`.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 179 Change 3:
   decompose VHDL Boolean decisions under the equivalent stable atomic model.
   Preserve all Changes 1-2 work and do not begin Change 4 in the same bounded
   slice.

## Batch 179 active checkpoint - after Change 3

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-3 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 3 adds the language-neutral
   `include/fsim/elaboration/coverage_conditions.hpp` and
   `src/elaboration/coverage_conditions.cpp` engine, plus
   `include/fsim/elaboration/vhdl_coverage_conditions.hpp`,
   `src/elaboration/vhdl_coverage_conditions.cpp`, and
   `tests/elaboration/vhdl_coverage_conditions_test.cpp`. The prior Verilog
   wrapper now delegates to the same engine; its Change 2 behavior remains
   green.
3. The common engine owns one immutable, transactional, bounded traversal for
   authenticated sources, statement/expression trees, atomic identities,
   lexical ordinals, and operator/operand paths. Verilog/SystemVerilog select
   `&&`, `||`, and `!`; VHDL selects binary `and`, `or`, `nand`, `nor`, `xor`,
   `xnor`, and unary `not`. The path retains the exact language operator and
   operand side without flattening either AST.
4. VHDL decisions include if/elsif and source-normalized conditional control,
   conditional while loops, assertions, and explicit wait-until statements.
   The parser's synthetic true conditions for unconditional loops and bare
   waits are omitted. The VHDL-2008 `??` conversion and unary reduction
   operators remain indivisible source atoms, as do relational expressions.
5. The focused test covers VHDL-87, VHDL-93, VHDL-2000, VHDL-2002, and
   VHDL-2008 with checkout relocation. It also proves every VHDL logical path,
   decision family, tree immutability, canonical point identity, `??`, invalid
   language/revision, malformed unary/binary arity, source authentication,
   span bounds, duplicate rollback, shared resource limits, and unconditional
   omission. `FSIM-COV-016` owns the stable VHDL diagnostic family.
6. COVMET-C02-C03 are preserved and COVMET-C04 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `12e6f0a9bfe99533b40331b625bc99f077958c83f603ae79a1457d7597f5a0aa`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
7. The source manifest now contains 1,582 ordered paths at SHA-256
   `432937b2bab3dd7418917e49e4d1db572bd05baecfb67646130392b562222118`.
   Source-package, diagnostics, source-line-budget, resource-portability,
   both coverage inventories, and CTest uniqueness ownership are green.
8. The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 10/10 in 8.20 wall seconds at
   22,864 KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch179-change03-focused-predoc.log` SHA-256 is
   `eb7a9c2e27744485e89ba3f50da5bc49e6b91c5ccd5e12055994278933a78965`.
   The post-documentation rerun passes 10/10 in 6.93 wall seconds at 22,644
   KiB peak RSS with zero swaps. Its retained
   `build/llvm22-ninja-debug/batch179-change03-focused-postdoc.log` SHA-256 is
   `3a0f29c1cb9f31dbc59a203bbb3040cb4defe3ef52d6edd098aa405e9ecb5983`.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 179 Change 4:
   preserve governed short-circuit evaluation while recording condition
   outcomes. Preserve all Changes 1-3 work and do not begin Change 5 in the
   same bounded slice.

## Batch 179 active checkpoint - after Change 4

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-4 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 4 adds
   `include/fsim/runtime/coverage_condition_evaluation.hpp`,
   `src/runtime/coverage_condition_evaluation.cpp`, and
   `tests/runtime/coverage_condition_evaluation_test.cpp`. The shared
   logical-operator, operand, and path-step types moved into the runtime model;
   the elaboration header aliases them, so Changes 2-3 retain one ABI-neutral
   path representation without making runtime depend on elaboration.
3. `evaluate_coverage_condition` transactionally reconstructs one complete
   decision tree from its stable atom paths. It validates nonzero unique point
   identities, dense unique condition ordinals, consistent operators and
   operand sides, complete unary/binary nodes, and bounded atoms, nodes, total
   path steps, and nesting before it calls user evaluation code.
4. Atom callbacks run lazily in expression order, independent of the storage
   order of the input atoms. Definite false `and`/`nand` and definite true
   `or`/`nor` skip the complete right subtree. Unknown SystemVerilog truth is
   not a controlling value and therefore evaluates the right side for the
   correct four-state result. `xor`/`xnor` evaluate both operands and `not`
   retains unknown truth.
5. The result separates actual observations from lexically ordered skipped
   atoms. Callback rejection, callback exceptions, invalid truth, malformed
   trees, duplicates, and resource exhaustion clear all partial observations
   and skips. `FSIM-COV-017` owns this stable failure family. Change 5 still
   owns persistent true/false/unknown counters and scoring; none were added.
6. The focused corpus proves nested SystemVerilog evaluation order, subtree
   skips, every determining and nondetermining four-state boundary, VHDL
   `nand`/`nor`, eager `xor`/`xnor`, shuffled atom storage, callback rollback,
   invalid identities/ordinals/paths, incomplete trees, and all configured
   ceilings. A graph-backed audit confirms the existing SystemVerilog and VHDL
   lowerers use the same branch semantics.
7. COVMET-C02-C04 are preserved and COVMET-C05 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `f4c5bc29d245027bf0d47df29e98c98541079d56d6d58033fc8adb63e176157b`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
8. The source manifest now contains 1,585 ordered paths at SHA-256
   `92db9db0aab112cd7ffaeb3c06f83c06f565bc85bcfb67d70049a5acf3f1d640`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 11/11 in 7.24 wall seconds at
   22,816 KiB peak RSS with zero swaps. The post-documentation rerun passes
   11/11 in 7.32 wall seconds at 22,640 KiB peak RSS with zero swaps.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 179 Change 5: record
   true, false, and auxiliary unknown four-state outcomes. Preserve all
   Changes 1-4 work and do not begin Change 6 in the same bounded slice.

## Batch 179 active checkpoint - after Change 5

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-5 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 5 adds `include/fsim/runtime/coverage_condition_outcomes.hpp`,
   `src/runtime/coverage_condition_outcomes.cpp`, and
   `tests/runtime/coverage_condition_outcomes_test.cpp`. One dense table entry
   owns the stable point plus separate true, false, and auxiliary unknown
   uint64 counters and explicit saturation flags.
3. `record_coverage_condition_outcomes` validates the complete table and
   incoming observation batch before mutation: table identities must be
   nonzero and unique; overflow flags must agree with saturated counters; each
   truth encoding must be valid; and every dense condition ordinal must name
   its exact point owner only once per evaluation.
4. After validation, the mutation pass is allocation-free and non-throwing.
   True and false increment only their respective scored bins. Unknown
   increments only `unknown_observations`. Every counter saturates at uint64
   maximum, records its own overflow flag, and contributes to the update's
   explicit saturation count without wrapping.
5. `coverage_condition_outcome_status` deliberately ignores unknown activity:
   zero binary bins is uncovered, either true or false is partial, and both
   are covered. Consequently even a saturated unknown counter cannot create a
   binary coverage result. A Change 4 skipped atom produces no observation and
   leaves its table entry unchanged.
6. The focused corpus proves true/false/unknown separation, binary status
   progression, skipped-atom stability, saturation and overflow reporting,
   transactional rejection of invalid/duplicate ownership, invalid truth,
   inconsistent saturation, and pre-mutation table/batch ceilings.
   `FSIM-COV-018` owns the stable failure family. Change 6 expression
   combinations remain untouched.
7. COVMET-C02-C05 are preserved and COVMET-C06 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `6ce16dde407eeb50a7802363fb6652c7ff48a62f94c139cb56b083978bf2db6e`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
8. The source manifest now contains 1,588 ordered paths at SHA-256
   `2fb5ebfd35d1c50b0819d1124367e12d8e447e4013f31259410bd8182582c056`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 12/12 in 7.15 wall seconds at
   22,592 KiB peak RSS with zero swaps. The post-documentation rerun passes
   12/12 in 7.27 wall seconds at 22,776 KiB peak RSS with zero swaps.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 179 Change 6: bound
   expression-combination expansion and report every omitted combination.
   Preserve all Changes 1-5 work and do not begin Change 7 in the same bounded
   slice.

## Batch 179 active checkpoint - after Change 6

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-6 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 6 adds `include/fsim/runtime/coverage_expression.hpp`,
   `src/runtime/coverage_expression.cpp`, and
   `tests/runtime/coverage_expression_test.cpp`. An expression inventory owns
   the ordered atomic point identities, retained packed binary combinations,
   and an explicit omission summary.
3. `build_coverage_expression_inventory` validates nonempty, nonzero, unique
   atom ownership before publication. It enumerates a canonical binary prefix
   in stable ordinal order, with the last lexical atom as the least significant
   bit. Repeated construction from the same atom order is byte-structurally
   deterministic.
4. Atom count, retained combination count, and total packed uint64 words have
   independent ceilings. A word ceiling reduces the number of complete bins;
   it never retains a partial combination. A zero combination/storage budget
   is a valid bounded inventory with every combination reported omitted.
5. For expression widths below 64, `omitted_combinations` is the exact
   `2^N - retained` value. Wider spaces set `omitted_count_exact` false and
   render the exact symbolic formula such as `2^70-4`; the saturated numeric
   field is never presented as an exact total. Thus truncation cannot become a
   synthetic coverage-completion claim.
6. The focused corpus proves complete three-atom enumeration, canonical bit
   order, combination and storage truncation, exact/symbolic omissions, wide
   packed words, zero budget, invalid/duplicate rollback, atom ceilings, and
   repeat determinism. `FSIM-COV-019` owns the stable failure family. Change 7
   toggle bins remain untouched.
7. COVMET-C02-C06 are preserved and COVMET-C07 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `d97a03c13050be51b8c792b5963b4634a5f2fee11ab7df78755c32478eaec66b`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
8. The source manifest now contains 1,591 ordered paths at SHA-256
   `5de6b0277c75ce52468cc84923229d96b0702b82a83d5a41a2378634e4808674`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 13/13 in 7.34 wall seconds at
   22,736 KiB peak RSS with zero swaps. The post-documentation rerun passes
   13/13 in 7.37 wall seconds at 22,944 KiB peak RSS with zero swaps.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 179 Change 7: define
   separate zero-to-one and one-to-zero toggle bins. Preserve all Changes 1-6
   work and do not begin Change 8 in the same bounded slice.

## Batch 179 active checkpoint - after Change 7

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-7 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 7 adds `include/fsim/runtime/coverage_toggle.hpp`,
   `src/runtime/coverage_toggle.cpp`, and
   `tests/runtime/coverage_toggle_test.cpp`. Each selected binary point/bit
   owner has two distinct `CoverageToggleBinId` values qualified by
   `ZeroToOne` or `OneToZero`, plus separate counters and overflow flags.
3. `record_coverage_toggle_transitions` validates nonzero unique point/bit
   ownership, consistent saturation state, exact dense transition ownership,
   and bounded table/batch size before mutation. The mutation pass allocates
   nothing and cannot fail, so invalid input never requires a table snapshot.
4. False-to-true and true-to-false events increment only the corresponding
   direction counter. Same-value samples are no-ops. An ordered batch may
   contain multiple sequential events for one bit, and the two direction
   counters independently saturate at uint64 maximum with explicit overflow
   flags and update reporting.
5. Toggle status is uncovered when neither direction has occurred, partial
   when exactly one has occurred, and covered only when both have occurred.
   Direction is part of bin identity, so reverse transitions complete the bit
   rather than aliasing the first bin.
6. The focused corpus proves direction identity, exact increment routing,
   same-value no-ops, status completion, repeated transitions, nonwrapping
   saturation, transactional identity/saturation/ownership rejection, and
   both resource ceilings. `FSIM-COV-020` owns the stable failure family. X/Z
   transitions are not accepted by this binary API and remain Change 12 work.
7. COVMET-C02-C07 are preserved and COVMET-C08 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `65df83d604f7c074c0c1d426e6fed0762c9ca2aed0301a92cc8acc7c32899c15`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
8. The source manifest now contains 1,594 ordered paths at SHA-256
   `5a80f0114cb0a7914df029aa921cab0b95e0e660a3254ca9040dd60e5a66a9d6`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 14/14 in 7.85 wall seconds at
   22,644 KiB peak RSS with zero swaps. The post-documentation rerun passes
   14/14 in 7.44 wall seconds at 22,732 KiB peak RSS with zero swaps.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 179 Change 8:
   instrument Verilog/SystemVerilog ports, nets, signals, and retained
   variables. Preserve all Changes 1-7 work and do not begin Change 9 in the
   same bounded slice.

## Batch 179 active checkpoint - after Change 8

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-8 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 8 adds
   `include/fsim/elaboration/verilog_toggle_inventory.hpp`,
   `src/elaboration/verilog_toggle_inventory.cpp`, and
   `tests/elaboration/verilog_toggle_inventory_test.cpp`. The builder consumes
   exactly one post-specialization semantic design unit and its existing
   immutable `CoverageInventoryOwner`; it does not parse text or recover
   hierarchy from runtime/process display names.
3. The owner boundary accepts Verilog modules and SystemVerilog modules,
   interfaces, and programs. It applies the same empty-library-to-`work` and
   canonical unit-identity rules as hierarchy elaboration, then authenticates
   the instance through the existing v3 coverage-instance identity. Ports,
   recognized nets/user nettypes, residual signals, and packed retained
   module/generate-scope variables retain explicit semantic kinds and full
   instance-qualified paths.
4. Every selected declaration owns a checkout-independent source
   `ToggleObject` point and a separate concrete-instance point hashed from the
   source point, instance identity, semantic object kind, and hierarchy path.
   Each packed bit maps contiguously to an initially empty Change 7 outcome.
   Canonical lexical path order makes results independent of declaration
   container order, while different hierarchy instances share source points
   but never concrete point/counter ownership.
5. The selection boundary accepts only binary/four-state packed integral
   types. Real, string, class/handle, interface, and whole-container objects do
   not create binary bins. Automatic callable/process locals never enter the
   design-unit surface. Change 10 still owns explicit inspectable default
   exclusions, and Change 11 still owns selected memory/array elements; neither
   policy was pulled forward.
6. Independent ceilings cover authenticated sources, semantic input objects,
   selected objects, aggregate bits, per-object width, object-name bytes,
   hierarchy bytes, line numbers, and instance identity. Invalid language or
   unit ownership, source identity/mapping/ownership, span/line, unspecialized
   width, duplicate path/point, and resource excess clear all output under
   stable diagnostic `FSIM-COV-021`.
7. The focused corpus includes a real parsed SystemVerilog surface and proves
   correct `wire` versus `reg`/`logic`/integral retained-variable
   classification, all retained Verilog/SystemVerilog unit families, generated
   hierarchy names, per-instance separation, relocation stability, canonical
   order, contiguous bit ownership, nonbinary/container omission,
   transactional failures, and every configured ceiling.
8. COVMET-C02-C08 are preserved and COVMET-C09 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `acee3ca8c5d69e2caeb9e7bf1768fbeeb82e061bd8fc69b8a3ebfe5df1a486c5`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
9. The source manifest contains 1,597 ordered paths at SHA-256
   `0d533fb73adc05a7a779e7d7efd1e91393e75555a1894013bf4febd53d58383e`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 15/15 in 7.54 wall seconds at
   22,844 KiB peak RSS with zero swaps. After the full affected incremental
   Debug rebuild, the post-documentation rerun passes 15/15 in 7.69 wall
   seconds at 22,900 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 9:
    instrument equivalent VHDL ports, signals, and retained variables.
    Preserve all Changes 1-8 work and do not begin Change 10 in the same
    bounded slice.

## Batch 179 active checkpoint - after Change 9

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-9 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 9 adds
   `include/fsim/elaboration/vhdl_toggle_inventory.hpp`,
   `src/elaboration/vhdl_toggle_inventory.cpp`, and
   `tests/elaboration/vhdl_toggle_inventory_test.cpp`. The builder consumes
   one already-specialized VHDL architecture and the resolved specialized
   entity-port view for one immutable `CoverageInventoryOwner`.
3. VHDL entity ports and architecture declarations intentionally remain
   separate inputs. The architecture supplies the canonical
   `vhdl:library.entity(architecture)` specialization identity and primary
   source; entity-port spans must resolve either to that source or to an exact
   recorded source dependency. Empty semantic libraries normalize to `work`.
4. All VHDL-87, VHDL-93, VHDL-2000, VHDL-2002, and VHDL-2008 profiles share
   the same source-point identities. Ports, architecture signals, and retained
   shared variables own explicit kinds. Each object retains a source
   `ToggleObject` point, an instance/path-qualified point, exact
   specialization/instance metadata, and a contiguous range of empty Change 7
   bit outcomes.
5. Direct scalar bit, std_logic, Boolean, and integer objects are selected.
   One-dimensional concrete vectors are selected only for one-bit bit or
   std_logic elements. Ordinary and process-local variables, file/access/
   protected/physical/string objects, and composite or multidimensional arrays
   create no default bins. Changes 10-11 still own inspectable default
   exclusions and explicit memory/array element selection.
6. Canonical hierarchy-path order is independent of declaration-container
   order. Relocated checkouts and retained standard revisions preserve source
   points; sibling instances share those source points but have distinct
   concrete points and counter ownership.
7. Independent ceilings cover authenticated sources, semantic input objects,
   selected objects, aggregate bits, per-object width, object-name bytes,
   hierarchy bytes, line numbers, and instance identity. Invalid revision,
   language, architecture/owner identity, entity dependency, source/span,
   specialized width, duplicate path/point, and resource excess clear all
   output under stable diagnostic `FSIM-COV-022`.
8. COVMET-C02-C09 are preserved and COVMET-C10 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `0946cbf0fa2084011b98cf6ae6ff2fac2b6a1955109a52a8b631c7e96d12fe4b`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
9. The source manifest contains 1,600 ordered paths at SHA-256
   `743c690e13dd0ce177bb60f3471e4e4267211338bfcd8589f466d76f1eae08de`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 16/16 in 7.36 wall seconds at
   22,960 KiB peak RSS with zero swaps. The post-documentation rerun passes
   16/16 in 7.55 wall seconds at 22,956 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 10:
    exclude automatic locals and memories by default. Preserve all Changes
    1-9 work and do not begin Change 11 in the same bounded slice.

## Batch 179 active checkpoint - after Change 10

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-10 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 10 adds
   `include/fsim/elaboration/coverage_toggle_selection.hpp`,
   `src/elaboration/coverage_toggle_selection.cpp`, and
   `tests/elaboration/coverage_toggle_selection_test.cpp`. The builder consumes
   one already-specialized Verilog/SystemVerilog unit or VHDL architecture,
   the resolved entity-port view where applicable, and the immutable
   `CoverageInventoryOwner` for exactly one hierarchy occurrence.
3. Default-excluded objects remain explicit records. Automatic function/task
   and VHDL callable locals carry `automatic-local`; process and nested-block
   locals carry `procedural-local`; static memories and retained VHDL array
   memories carry `memory`; SystemVerilog containers and non-directly-packed
   VHDL arrays carry `array`. Ordered reason sets retain every applicable
   reason without enabling any scored toggle bit.
4. Ordinary retained scalars create no exclusion and remain selected by the
   Change 8/9 inventories. Change 10 has no opt-in surface: explicit bounded
   memory/array element, bit, and range selection remains solely Change 11.
5. Each exclusion authenticates its semantic source and span, owns the same
   checkout-independent source `ToggleObject` domain as selected objects, and
   receives a distinct instance, hierarchy-path, and reason-qualified identity.
   Canonical path order is stable across declaration-container order and
   relocated checkouts; sibling instances share source points but not concrete
   exclusion identities.
6. Recursive process, nested-statement, function, task, and VHDL procedure
   discovery uses source-offset-qualified lexical scope components. VHDL entity
   ports continue to authenticate through the architecture owner's exact
   source dependencies. Invalid embedded scope text is rejected before any
   result is published.
7. Independent ceilings cover authenticated sources, traversed declarations,
   exclusions, total reasons, object names, hierarchy paths, lexical depth,
   line numbers, and instance identity. Invalid language/unit/owner/source/
   span/line/text, duplicate paths or identities, and resource excess clear all
   output under stable diagnostic `FSIM-COV-023`.
8. COVMET-C02-C10 are preserved and COVMET-C11 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `9bc16f6d399e3ec7807281fc41fcf6569bda6a668078bb076a1c3b66e4ac4503`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
9. The source manifest contains 1,603 ordered paths at SHA-256
   `f2a52a4bfbcb9ae5b93ba895345872f1d9665e6b5075559693a71605d557ee30`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 17/17 in 7.14 wall seconds at
   22,876 KiB peak RSS with zero swaps. The post-documentation rerun passes
   17/17 in 7.54 wall seconds at 23,052 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 11: add
    explicit memory and array toggle-selection rules. Preserve all Changes
    1-10 work and do not begin Change 12 in the same bounded slice.

## Batch 179 active checkpoint - after Change 11

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-11 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 11 adds
   `include/fsim/elaboration/coverage_memory_toggle.hpp`,
   `src/elaboration/coverage_memory_toggle.cpp`, and
   `tests/elaboration/coverage_memory_toggle_test.cpp`. It consumes the
   authenticated Change 10 default-exclusion inventory and exact
   hierarchy-path selection rules; it never enables an object merely because
   the object is a memory or array.
3. Numeric static, dynamic, queue, integer-associative, and VHDL selectors name
   one explicit inclusive range per dimension. String-keyed associative arrays
   name exact nonempty keys. Every selector names a nonempty packed-element bit
   slice. No wildcard, missing-coordinate, missing-bit, or implicit
   whole-container form exists.
4. Change 10 exclusion records now retain container kind, concrete semantic
   dimensions, binary element width, and string-index classification. A real
   parsed two-dimensional SystemVerilog memory proves the elaborated semantic
   surface supplies exact `[3:2][0:1]` bounds and eight-bit element width.
   Static/VHDL selection rejects unresolved bounds rather than inferring them.
5. Static and VHDL selector endpoints must lie within every declared bound.
   Dynamic and queue indices must be nonnegative; integer and string
   associative keys retain their exact value. Unsupported nonbinary element
   types create no bins.
6. Each selected element keeps the exclusion's checkout-independent source
   point and receives an exclusion/coordinate-or-key-qualified point. Its exact
   bit slice maps densely into Change 7 outcomes. Canonical path/bit order is
   independent of rule and key declaration order, and overlapping selectors
   are rejected before any scored bit can alias.
7. Independent ceilings cover exclusion input, rules, selectors, dimensions,
   string keys and bytes, individual range span, expanded elements, expanded
   bits, and paths. Invalid selection/exclusion ownership, duplicate rule or
   exclusion identity/path, missing/unknown rules, unsupported/unresolved
   shapes, malformed/out-of-range selectors, invalid keys/bits, overlap or
   identity collision, and resource excess clear all output under stable
   diagnostic `FSIM-COV-024`.
8. COVMET-C02-C11 are preserved and COVMET-C12 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `5c33ffb1cb6200a0b33ba3e71c5690a42bfa8b7753750afaae36399319bb891f`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
9. The source manifest contains 1,606 ordered paths at SHA-256
   `283b40814ca847f1f34783819ee1d3644c21033f14e749808f7283811b9ee420`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 18/18 in 7.42 wall seconds at
   22,980 KiB peak RSS with zero swaps. The post-documentation rerun passes
   18/18 in 7.55 wall seconds at 23,020 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 12:
    track X/Z transitions diagnostically without scoring them as binary
    toggles. Preserve all Changes 1-11 work and do not begin Change 13 in the
    same bounded slice.

## Batch 179 active checkpoint - after Change 12

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-12 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 12 extends the existing
   `include/fsim/runtime/coverage_toggle.hpp`,
   `src/runtime/coverage_toggle.cpp`, and
   `tests/runtime/coverage_toggle_test.cpp` surface. Each Change 7 outcome now
   retains independent unknown-participation and high-impedance-participation
   diagnostic counters and overflow flags beside its two unchanged scored
   direction counters.
3. The four-state transition API accepts only exact zero, one, unknown, and
   high-impedance values. Changed transitions involving X increment the X
   diagnostic; those involving Z increment the Z diagnostic; X-to-Z increments
   each once. Equal X-to-X and Z-to-Z samples are no-ops.
4. A binary direction increments only when both endpoints are exact zero/one.
   X-to-one, zero-to-Z, and every other transition with a nonbinary endpoint
   leave both scored counters and derived uncovered/partial/covered status
   unchanged. Exact binary transitions through the four-state API still update
   their original Change 7 bins.
5. Scored and diagnostic counters saturate independently at uint64 maximum and
   retain explicit overflow flags. Per-update results separately report X and Z
   observations plus saturated writes. The original bool transition API remains
   available and validates the complete outcome saturation state.
6. Both entry points validate bounded outcome/transition tables, unique valid
   point/bit ownership, consistent saturation, exact dense transition ownership,
   and canonical four-state encodings before mutation. The mutation passes
   allocate nothing and cannot fail, so invalid state never requires a
   whole-table snapshot. `FSIM-COV-020` remains the stable failure family.
7. The focused corpus proves independent X/Z observations, X-to-Z dual
   accounting, same-nonbinary no-ops, zero score from diagnostic activity,
   later exact-binary completion, independent diagnostic saturation, invalid
   encodings and saturation, ownership mismatch, and resource transactionality.
8. COVMET-C02-C12 are preserved and COVMET-C13 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `544b1abfc20f62eb35af062e5be5051d377103b0b4d69d5ac3c62119e12d32ae`.
   Future engine, aggregation, and artifact paths remain registered to their
   later integration owners without placeholder evidence.
9. The source manifest remains 1,606 ordered paths at SHA-256
   `283b40814ca847f1f34783819ee1d3644c21033f14e749808f7283811b9ee420`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The focused pre-documentation set passes 18/18 in 7.60 wall seconds at
   22,980 KiB peak RSS with zero swaps. The post-documentation rerun passes
   18/18 in 7.51 wall seconds at 22,884 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 13:
    infer enum- and case-based current-state objects. Preserve all Changes
    1-12 work and do not begin Change 14 in the same bounded slice.

## Batch 179 active checkpoint - after Change 13

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-13 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 13 adds
   `include/fsim/elaboration/coverage_fsm_inference.hpp`,
   `src/elaboration/coverage_fsm_inference.cpp`, and
   `tests/elaboration/coverage_fsm_inference_test.cpp`, registers the focused
   target, and adds `FsmCurrentStateObject` as the stable source construct kind.
   The published model contains current-state objects and their ordered stable
   states only. Change 14 still exclusively owns optional next-state objects
   and legal-state sets.
3. Candidates are retained unit signals and variables plus output/buffer ports.
   Enumeration evidence comes from VHDL enumeration literals, parsed
   SystemVerilog enum typedefs, or retained enumeration values. Exact-case
   evidence requires a direct identifier selector and at least two distinct
   simple identifier, integer, Boolean, or logic choices. Enum declaration
   order is retained; a case-only set is sorted canonically.
4. Wildcard/range/pattern cases are ignored. Process and nested-block locals
   shadow unit objects. Duplicate retained names cannot establish ownership,
   and inconsistent case-only descriptions mark that candidate ambiguous and
   suppress it. Enum evidence may still qualify a typedef-backed object whose
   scalar domain is not yet copied onto the declaration; case-only inference
   remains restricted to accepted scalar domains.
5. Source-point identity hashes the authenticated source identity, language,
   `fsm-current-state-object` kind, and declaration span. Instance-object
   identity adds the stable instance identity and canonical hierarchy path;
   each state identity adds its ordinal and name. Relocated checkouts and
   declaration-container ordering therefore compare equal, while sibling
   instances remain distinct.
6. VHDL-87, VHDL-93, VHDL-2000, VHDL-2002, and VHDL-2008 use equivalent
   inference rules. The parsed SystemVerilog corpus proves enum-plus-case and
   case-only inference. Negative coverage proves conflicting cases, lexical
   shadowing, duplicate declarations, unknown source ownership, and bounded
   object/case/choice/state/statement-depth input.
7. Construction validates language/unit/instance ownership, authenticated
   source inventories and spans, unique paths and identities, names, lines,
   and every declared resource ceiling before publishing a result. Failures
   discard the result under stable diagnostic `FSIM-COV-025`.
8. COVMET-C02-C13 are preserved and COVMET-C14 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `9953619a01ad6b350a6f2b93ab31a8681441fefc6a48b6ef15b88c19dc71b5f7`.
   Future next-state, pragma, hint, visit/transition, engine, aggregation, and
   artifact owners remain registered without placeholder evidence.
9. The source manifest contains 1,609 ordered paths at SHA-256
   `2590fd8392ba51d778cd9a2cad952aaf10db850d42176523c2af55850ecaef23`.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers. The
   focused pre-documentation set passes 19/19 in 7.29 wall seconds at 23,068
   KiB peak RSS with zero swaps. The post-documentation rerun passes 19/19 in
   7.67 wall seconds at 22,948 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 14:
    infer optional next-state objects and legal-state sets. Preserve all
    Changes 1-13 work and do not begin Change 15 in the same bounded slice.

## Batch 179 active checkpoint - after Change 14

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-14 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 14 extends the Change 13 `coverage_fsm_inference` header,
   implementation, and focused test. `CoverageFsmInference` now carries
   optional next-state objects and one explicit legal-state set per inferred
   current object. `FsmNextStateObject` is the new stable source construct kind;
   its name is frozen as `fsm-next-state-object`.
3. A next-state relation requires a direct ordinary assignment whose target
   and value are distinct retained identifiers. Enum sets must match exactly;
   otherwise nominal/named type identity or concrete scalar domain, width, and
   signedness must match. Self-assignment, expression-valued updates,
   incompatible types, duplicate declarations, and lexically shadowed names do
   not contribute evidence.
4. One current object may have only one compatible next candidate, and one next
   declaration may belong to only one current object. Multiple candidates or
   shared ownership suppress the optional relation without discarding the
   current object or its legal set. An enum object used only as a next source
   and never as a case selector or current target is suppressed from the
   current-object inventory.
5. A legal-state set hashes the current-object identity and its complete ordered
   state IDs. It references those existing IDs rather than copying or
   manufacturing states. Enum declaration order remains authoritative. Exact
   case evidence supports the legal set only when its canonical choices equal
   the inferred set; no transition is created in this slice.
6. The parsed SystemVerilog corpus proves enum-typed and scalar case-only next
   objects, stable relocation, current/next linkage, and legal ID reuse. Manual
   VHDL objects prove equivalent behavior in VHDL-87, VHDL-93, VHDL-2000,
   VHDL-2002, and VHDL-2008. Negative cases cover competing candidates,
   lexical shadows, and assignment/next-object/legal-set ceilings.
7. Assignment traversal and next/legal ownership extend the existing bounded,
   transactional `FSIM-COV-025` family. Source attributes, standard
   SystemVerilog FSM pragmas, VHDL source hints, and manifest hints remain
   absent; Changes 15 and 16 retain those responsibilities.
8. COVMET-C02-C14 are preserved and COVMET-C15 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `1254b56412974f2f33771d9d68059497eb72ab94d3deb203673ae9697dfff833`.
   Future pragma, hint, visit/transition, description validation, engine,
   aggregation, and artifact owners remain registered without placeholder
   evidence.
9. The source manifest remains 1,609 ordered paths at SHA-256
   `2590fd8392ba51d778cd9a2cad952aaf10db850d42176523c2af55850ecaef23`.
   The public construct enum passes a complete 871-step exact-LLVM 22.1.8
   warnings-as-errors Debug impact rebuild with eight workers. The focused
   pre-documentation set passes 19/19 in 7.73 wall seconds at 23,196 KiB peak
   RSS with zero swaps. The post-documentation rerun passes 19/19 in
   7.62 wall seconds at 23,292 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 15:
    implement standard SystemVerilog FSM description pragmas. Preserve all
    Changes 1-14 work and do not begin Change 16 in the same bounded slice.

## Batch 179 active checkpoint - after Change 15

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-15 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 15 extends the shared frontend design model, SystemVerilog parser,
   and `coverage_fsm_inference` implementation/test. Attribute instances
   immediately preceding a SystemVerilog module, interface, or program retain
   the exact `fsm_current_state`, `fsm_next_state`, and `fsm_legal_states`
   keys by original attribute-instance group. Unknown vendor attributes are
   ignored rather than aliased, and Verilog profiles retain no FSM pragma
   meaning.
3. Every retained group requires one decoded-string current object. The
   optional next object must be different, unique, retained, and type
   compatible. The optional comma-separated legal list must contain at least
   two unique bounded names. A pragma can augment enum evidence, select an
   ordered subset of existing enum states, or supply the state universe for a
   retained scalar object without enum/case evidence; it never manufactures
   an enum state.
4. Explicit pragma next-state evidence takes precedence over assignment
   inference. Multiple compatible explicit next candidates suppress only the
   optional relation. Conflicting repeated legal descriptions suppress their
   pragma evidence and fall back to independently valid enum/case inference;
   Change 18 retains stable ambiguity/incompleteness/conflict diagnostics.
   Current, next, and legal records expose separate pragma and assignment
   provenance.
5. SystemVerilog-2005, 2009, 2012, and 2017 share identical behavior and
   stable identities. The independently authored corpus proves grouped
   parsing, unknown vendor-key isolation, Verilog profile isolation,
   relocation, enum legal subsets, pragma-only scalar FSMs, explicit
   current/next linkage, malformed/missing/unknown/incompatible values,
   ambiguity containment, and group/specification/value resource ceilings.
6. Malformed exact pragmas, wrong-profile retained metadata, incompatible
   objects, and unknown enum legal states fail transactionally under the
   extended `FSIM-COV-025` family. The new bounds are 65,536 groups, 262,144
   specifications, and 1 MiB per decoded value. VHDL source hints and
   language-neutral manifest hints remain absent and solely owned by Change
   16.
7. COVMET-C02-C15 are preserved and COVMET-C16 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `070d2ee13b6ab1e75d3209a84bb77a4ba368bb1b22ac97fd9f6d5de8717d34b0`.
   The source manifest remains 1,609 ordered paths at SHA-256
   `2590fd8392ba51d778cd9a2cad952aaf10db850d42176523c2af55850ecaef23`.
8. The shared `DesignUnit` extension passes an 898-step, eight-worker,
   exact-LLVM 22.1.8 warnings-as-errors Debug impact build in 10:35.31 at
   3,949,796 KiB peak RSS with zero swaps. Its retained build log SHA-256 is
   `e6ee17819d4aabec488173b932566df508bc38a4cd8e84a26c43d610cba66516`.
9. The focused pre-documentation set passes 19/19 in 7.39 wall seconds at
   23,280 KiB peak RSS with zero swaps; its retained log SHA-256 is
   `4510e4f45d305c52db697eb4c3ae5c51444e83e7eb64cedc11b1301abd71020b`.
   The first post-documentation rerun passes 19/19 in 7.68 wall seconds at
   23,316 KiB peak RSS with zero swaps; its retained log SHA-256 is
   `1d172cede3c6658b1160100cde3d7c8765a915628b18078f8ecda5c1f9e7b54b`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 16: add
    VHDL source hints and language-neutral manifest FSM hints. Preserve all
    Changes 1-15 work and do not begin Change 17 in the same bounded slice.

## Batch 179 active checkpoint - after Change 16

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-16 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 16 adds
   `include/fsim/elaboration/coverage_fsm_hints.hpp`,
   `src/elaboration/coverage_fsm_hints.cpp`, and
   `tests/elaboration/coverage_fsm_hints_test.cpp`. It also extends the
   schema-3 project model/parser and the Change 13-15 FSM inference model.
   One language-neutral hint record retains its VHDL-source or manifest
   origin, exact instance selection, current marker, optional next object, and
   optional legal states.
3. VHDL source hints use ordinary string-typed user attributes named exactly
   `fsm_current_state`, `fsm_next_state`, and `fsm_legal_states` on exact
   signal or variable names. The current value is the string `true` or
   `false`; next names one retained object; legal states are comma separated.
   All five retained VHDL profiles normalize names identically. Unknown vendor
   attributes remain ordinary ignored VHDL metadata and no execution semantics
   change.
4. The project manifest accepts repeatable `[[coverage.fsm]]` tables with
   required nonempty `instance` and `current_state`, optional `next_state`, and
   optional string-array `legal_states`. Entries are exact rather than globbed
   and are language neutral. The frozen 16-row schema-3 project-manifest
   contract now hashes to
   `be2e1aee6cdc3dafd30c8f7e20c933545394a2ebdd39595e6988a0738e0b8430`;
   no v2 reader or migration was added.
5. Inference revalidates hint origin, instance, names, legal states, retained
   object ownership, and type compatibility. Valid entries for other instances
   are ignored. Matching VHDL, manifest, and SystemVerilog pragma descriptions
   compose and retain separate provenance on current, next, and legal records.
   Conflicting explicit legal descriptions suppress explicit evidence while
   retaining independently valid enum/case inference; detailed stable
   ambiguity/incompleteness/conflict diagnostics remain Change 18.
6. `FSIM-COV-026` owns transactional source/manifest hint construction;
   `FSIM-COV-025` now also owns inference-side trust-boundary validation. The
   new producer bounds attributes, attribute entity names, manifest entries,
   combined hints, legal states, names, values, and instances. Inference
   independently bounds received hints, legal-state totals, names, and
   instances.
7. The independently authored corpus proves all five VHDL profiles, VHDL and
   SystemVerilog manifest use, relocation, matching VHDL/manifest and
   pragma/manifest composition, conflicting-description suppression,
   other-instance isolation, unknown vendor-key rejection, malformed
   source/schema entries, unknown objects/states, and every new resource
   family. The existing FSM inference and project tests remain green.
8. COVMET-C02-C16 are preserved and COVMET-C17 through COVMET-C18 remain
   active. The normalized matrix SHA-256 is
   `8a459c5baf3bc56592083540e6a828ecccb460fb373d7dbe525ff6f9e9005aa9`.
   The source manifest contains 1,612 ordered paths at SHA-256
   `decea49910c11df02edc37a2fb71701f01a8404a3c2b13d7a474c97b59ad1c53`.
9. The shared project and inference model changes pass a 632-step,
   eight-worker, exact-LLVM 22.1.8 warnings-as-errors Debug impact build in
   7:00.57 at 2,792,604 KiB peak RSS with zero swaps. Its retained build log
   SHA-256 is
   `647d2f9ef6688777b7f1d8e040f6a3d82aaffb88942bceaea1c0d64106065bf6`.
   The focused pre-documentation set passes 22/22 in 7.57 wall seconds at
   23,520 KiB peak RSS with zero swaps; its retained log SHA-256 is
   `3c32eb01f6655de43a7ca90dbf43f62e71a60786bfa80e19c4f6d813c32c77f0`.
   The first post-documentation rerun passes 22/22 in 8.26 wall seconds at
   23,412 KiB peak RSS with zero swaps; its retained log SHA-256 is
   `e0c0f09a61f000c90c4c79cba9dabc7993211fa6b501ad1feb8779e053597480`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 17:
    record state visits and legal transitions separately. Preserve all Changes
    1-16 work and do not begin Change 18 in the same bounded slice.

## Batch 179 active checkpoint - after Change 17

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-17 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 17 adds `include/fsim/runtime/coverage_fsm.hpp`,
   `src/runtime/coverage_fsm.cpp`, and
   `tests/runtime/coverage_fsm_test.cpp`. The runtime-facing definition uses
   only language-neutral stable identities and therefore does not add a
   runtime-to-elaboration dependency. It retains instance identity,
   specialization, exact instance path, current-state object, states, and
   explicitly declared legal ordered transitions.
3. Runtime construction canonicalizes machines, states, state-visit bins, and
   legal-transition bins. Every bin identity includes the hierarchical
   instance identity, current-state object, state or ordered state pair, and a
   distinct `state-visit` or `legal-transition` role. Reordered equivalent
   definitions remain identical while sibling instance identities remain
   distinct.
4. Each observation records one state visit. A machine's first observation
   establishes history only; later observations score an exact declared
   ordered transition. An undeclared pair never creates or scores a coverage
   bin and instead increments that machine's separate diagnostic count.
   Previous-state histories remain independent while multiple machines are
   interleaved.
5. State-visit, legal-transition, and undeclared-transition diagnostic
   counters saturate without wrapping and record overflow once. The entire
   runtime model and observation batch validate before mutation. Invalid or
   duplicate identities/ownership, non-canonical or tampered bins,
   inconsistent saturation, invalid previous states, and foreign observations
   fail transactionally under `FSIM-COV-027`.
6. Bounds cover 1,048,576 machines, 4,194,304 states, 16,777,216 declared
   legal transitions, 16,777,216 observations per call, and 1 MiB instance
   paths. Allocation or length failure maps to the same bounded resource
   result. Summary output reports state visits and legal transitions as two
   independent metric families and has no combined or synthetic FSM score.
7. The independently authored corpus proves canonical order and identity,
   sibling-instance separation, visit and legal-transition scoring,
   diagnostic-only undeclared pairs, interleaved machine histories, all three
   saturation families, tampered-model rejection, complete-batch
   transactional failure, and every new resource ceiling.
8. COVMET-C02-C17 are preserved and only COVMET-C18 remains active. The
   normalized matrix SHA-256 is
   `b33e6b1d80bceeeee4586411b20b3d3eac8a8b1c7306747a8060532047f84a11`.
   The source manifest contains 1,615 ordered paths at SHA-256
   `645470d8324ce8378ce5db7808cac6cba79eb2e0e22a438b773fc7dcbd74f556`.
9. The runtime-library change passes a 135-step, eight-worker, exact-LLVM
   22.1.8 warnings-as-errors Debug impact build in 36.39 wall seconds at
   1,649,264 KiB peak RSS with zero swaps. The focused pre-documentation set
   passes 25/25 in 1.27 wall seconds at 23,520 KiB peak RSS with zero swaps.
   The post-documentation rerun passes 25/25 in 1.34 wall seconds at 23,468
   KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 18:
    diagnose ambiguous, incomplete, and conflicting FSM descriptions.
    Preserve all Changes 1-17 work and do not begin Change 19 in the same
    bounded slice.

## Batch 179 active checkpoint - after Change 18

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-18 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 18 adds
   `include/fsim/elaboration/coverage_fsm_validation.hpp`,
   `src/elaboration/coverage_fsm_validation.cpp`, and
   `tests/elaboration/coverage_fsm_validation_test.cpp`. It extends the shared
   FSM inference result with canonical language-neutral description
   diagnostics and a separately bounded validation contract.
3. Diagnostic candidates carry issue kind (ambiguous, incomplete, or
   conflicting), subject (current state, next state, or legal states), stable
   instance identity/path, normalized object, and exact evidence origins.
   Origins distinguish enum, case, assignment, SystemVerilog pragma, VHDL
   source, and manifest evidence. Equivalent candidates coalesce, their
   origins form a bounded sorted union, and the canonical diagnostic identity
   includes every retained field.
4. `FSIM-COV-028` warns about ambiguous duplicate current objects, competing
   inferred next-state objects, or one next object owned by multiple current
   machines. `FSIM-COV-029` warns about explicit scalar current objects that
   lack a legal state universe and exact cases that cover only part of an enum
   universe. `FSIM-COV-030` warns about differing repeated case/pragma
   descriptions, enum/case disagreement, explicit/assignment disagreement,
   or incompatible pragma, VHDL-source, and manifest descriptions.
5. A description issue suppresses only the affected evidence or optional
   relation. Independently valid enum/case inference remains available. The
   inference result publishes stable diagnostics; malformed diagnostic state
   or a diagnostic resource failure rejects the entire construction before
   publication under the extended `FSIM-COV-025` trust boundary.
6. The validator bounds 1,048,576 candidates, 1,048,576 diagnostics, six
   unique origins per diagnostic, 1 MiB instance paths, and 65,536-byte object
   names. Origin union is checked incrementally so repeated candidates cannot
   accumulate an unbounded temporary vector. Allocation and length failures
   map to the same transactional resource result.
7. The independently authored corpus proves deterministic order and
   coalescing, stable codes and identities, sibling instances, unioned
   origins, invalid kind/subject/identity/text/origin input, all resource
   ceilings, duplicate enum ownership, competing and shared next objects,
   scalar incompleteness, enum/case subset and conflict behavior, repeated
   pragma/case conflict, and VHDL/manifest plus pragma/manifest conflict with
   valid fallback preservation.
8. All seventeen COVMET-C02-C18 rows are preserved and zero remain active.
   The normalized matrix SHA-256 is
   `9776aa279d03422fb469bf869783a274fe8f6f79ceaa4df4a574b45880f86113`.
   The source manifest contains 1,618 ordered paths at SHA-256
   `30ca495468c5079caaff91c6964cbdc9605585be2c3640aa48c1eded6d3dbac6`.
9. The public inference-model change passes a 129-step, eight-worker,
   exact-LLVM 22.1.8 warnings-as-errors Debug impact build in 35.18 wall
   seconds at 1,649,292 KiB peak RSS with zero swaps. The focused pre-
   documentation set passes 26/26 in 1.34 wall seconds at 23,476 KiB peak RSS
   with zero swaps. The post-documentation rerun passes 26/26 in 1.31 wall
   seconds at 23,660 KiB peak RSS with zero swaps.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 179 Change 19:
    prove metric semantics across generate instances, engines, and mixed
    designs. Preserve all Changes 1-18 work and do not begin Change 20 batch
    closure in the same bounded slice.

## Batch 179 active checkpoint - after Change 19

1. Remain on `codex/v3` at the clean synchronized Batch 178 commit
   `f57661eb9970883136b82ceacdd81fd8043666a5` plus the intentionally dirty
   Batch 179 Changes 1-19 worktree. Preserve it without reset, commit, or push
   until Batch 179 Change 20.
2. Change 19 adds
   `tests/app/code_coverage_metrics_application_test.cpp`,
   `tests/app/code_coverage_metrics_equivalence_test.cpp`, and
   `tests/artifact/coverage_metrics_identity_test.cpp`. All three are built,
   registered, source-packaged, and frozen by the resource contract. Every
   COVMET row's pre-registered engine, aggregation, and artifact evidence owner
   is now required to exist.
3. The parsed application witness uses independently authored
   SystemVerilog-2017 and VHDL-2008 designs. Two generated sibling instances
   per language share canonical source points while retaining different
   hierarchy-qualified toggle objects, instance identities, and FSM bins.
   Per-instance condition and toggle results remain partial while source union
   is covered. Expression combinations remain bounded and exact; state visits
   and legal transitions remain separate results with no synthetic score.
4. The engine witness runs six instances together: two Verilog-2005, two
   SystemVerilog-2017, and two VHDL-2008. The interpreter, LLVM O0/O1/O2/O3,
   and compiled Debug produce an exact common event and metric snapshot. That
   snapshot includes short-circuit observations/skips, true/false/unknown
   outcomes, expression combinations, binary and diagnostic X/Z toggles, FSM
   visits/transitions, and stable instance-qualified identities.
5. Coverage-enabled artifact identity now uses
   `fsim-code-coverage-broad-metrics-v3`. The earlier
   `fsim-code-coverage-foundation-v3` model is retained only as a negative
   rejection witness. Disabled identity remains `none`, and the schema stays
   directly versioned at 3.
6. All seventeen COVMET-C02-C18 rows remain preserved and zero are active. The
   normalized ledger SHA-256 remains
   `9776aa279d03422fb469bf869783a274fe8f6f79ceaa4df4a574b45880f86113`.
   The source manifest contains 1,621 ordered paths at SHA-256
   `c0542c5b8baae097e53cccf2d282aab580efcf18b2853f76a23f2fb74a39b5b1`.
7. The artifact-identity propagation passes a 334-step, eight-worker,
   exact-LLVM 22.1.8 warnings-as-errors Debug impact build in 4:31.29 at
   1,649,512 KiB peak RSS with zero swaps.
8. The cumulative pre-documentation focused set passes 34/34 in 1.87 wall
   seconds at 78,356 KiB peak RSS with zero swaps. It covers all Batch 179
   frontend, elaboration, runtime, application, artifact, manifest, inventory,
   and resource-contract witnesses. The post-documentation rerun passes the
   same 34/34 in 1.58 wall seconds at 78,328 KiB peak RSS with zero swaps.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran.
10. Proceed only to Batch 179 Change 20: run the standard clean Debug/Release
    batch closure, freeze the broad metric set with MC/DC still excluded,
    update the authoritative documents, make the single Batch 179
    implementation commit, and push it. Batch 179 is not a sanitizer or
    hosted-CI boundary.

## Batch 179 closed checkpoint - after Change 20

1. Batch 179 Changes 1-20 are complete on `codex/v3`. The frozen broad-metric
   surface covers stable Verilog/SystemVerilog and VHDL condition
   decomposition, short-circuit accounting, binary and auxiliary unknown
   outcomes, bounded expression combinations, binary and diagnostic toggles,
   selected objects and containers, inferred and explicitly described FSMs,
   separate state/transition results, and mixed-language engine equivalence.
2. All seventeen COVMET-C02-C18 rows are preserved and zero remain active.
   The normalized ledger SHA-256 is
   `9776aa279d03422fb469bf869783a274fe8f6f79ceaa4df4a574b45880f86113`.
   MC/DC remains explicitly excluded from Batch 179. The source manifest
   contains 1,621 ordered paths at SHA-256
   `c0542c5b8baae097e53cccf2d282aab580efcf18b2853f76a23f2fb74a39b5b1`.
3. Change 19's application and engine witnesses retain distinct generated
   instance results while source aggregation forms the exact union. The
   interpreter, LLVM O0/O1/O2/O3, and compiled Debug reproduce one exact
   broad-metric snapshot across Verilog-2005, SystemVerilog-2017, and
   VHDL-2008. Coverage-enabled artifact identity is frozen at
   `fsim-code-coverage-broad-metrics-v3`; the foundation-only v3 identity and
   all incompatible inputs are rejected.
4. Full-tree qualification refreshed deliberate repository freezes to 2,576
   production diagnostics, 1,265 bounded authored sources, 1,556 SPDX-owned
   files, 471 conformance test/control files, and 675 FST test/control files.
   Release optimization also exposed one copied structured binding in the
   toggle-inventory corpus; binding that immutable profile pair by reference
   removes the warning without changing semantics.
5. The clean exact-LLVM warnings-as-errors Debug build completes 2,595/2,595
   steps with eight workers in 17:22.80 at 4,322,732 KiB peak RSS with zero
   swaps. The sequential Debug suite passes 348/348 in 6:26.96 at 1,100,028
   KiB peak RSS with zero swaps.
6. The final uninterrupted clean warnings-as-errors LLVM Release build
   completes 1,351/1,351 steps with eight workers in 14:46.51 at 1,893,948
   KiB peak RSS with zero swaps. The sequential Release suite passes 348/348
   in 8:55.50 at 1,099,516 KiB peak RSS with zero swaps.
7. Normalizing only the new Batch 179 translation units to the repository's
   WebKit format triggers warning-clean eight-worker incremental rebuilds of
   196 Debug and 160 Release steps. The exact final tree then passes the full
   Debug suite 348/348 in 6:44.40 and the full Release suite 348/348 in
   8:46.99.
8. Batch 179 is neither a sanitizer nor a non-documentation hosted-CI
   boundary, so neither lane ran or is claimed. Those lanes remain owned by
   Batches 180/190 and release-closing Change 20s under the governing
   contract.
9. Change 20 owns the single Batch 179 implementation commit and push. After
   that checkpoint is synchronized, the next bounded implementation work is
   Batch 180 Change 1 only: define the bounded, versioned `.fsimcov` container
   schema. Do not begin Change 2 in the same bounded slice.

## Batch 180 active checkpoint - after Change 1

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Change 1 worktree. Preserve it without reset, commit, or push until
   Batch 180 Change 20.
2. Change 1 adds `include/fsim/artifact/coverage_database.hpp`,
   `src/artifact/coverage_database.cpp`, and
   `tests/artifact/coverage_database_schema_test.cpp`. The artifact library and
   focused CTest own the new surface; the source manifest and resource contract
   freeze all three files.
3. The wire contract fixes `FSIMCOV` plus its terminating zero, container and
   namespace schema 3, the canonical big-endian marker, zero flags, a 64-byte
   header, and three 64-byte directory records. Payloads begin at byte 256.
   This slice defines layout and validation only; deterministic byte encoding
   remains Change 5.
4. The directory always owns code, SystemVerilog-functional, and PSL
   namespaces in that exact order. Each has direct v3 identity, raw encoding,
   an independent SHA-256 slot, and a gap-free packed payload extent. Empty
   namespaces remain explicit so later contents cannot alias or manufacture a
   synthetic combined namespace.
5. Default limits bound the complete container to 1 GiB, each namespace to
   512 MiB, the directory to 1 MiB, and namespace count to exactly three.
   Every offset and aggregate size uses checked uint64 arithmetic. Validation
   rejects v2/unknown schema, magic or byte-order drift, flags, alternate
   encoding, directory drift, namespace reorder/unknown identity, gaps,
   overlaps, size disagreement, overflow, and every resource excess under
   `FSIM-COV-031`.
6. The independently authored corpus proves deterministic empty and populated
   construction, exact offsets/sizes/digests, all three namespace spellings,
   every header/directory/namespace mutation, a direct v2 rejection, all four
   resource ceilings, and uint64 overflow. Model fingerprint, source inventory,
   run metadata, metrics, and exclusions remain exclusively Change 2.
7. The new public artifact surface rebuilds an eight-step exact-LLVM
   warnings-as-errors Debug target with eight workers in 0.38 wall seconds at
   117,024 KiB peak RSS with zero swaps. The focused schema, diagnostic,
   manifest, resource, and retained-audit set passes 13/13 in 7.48 wall seconds
   at 27,724 KiB peak RSS with zero swaps.
   Its post-documentation rerun passes the same 13/13 in 7.29 wall seconds at
   27,832 KiB peak RSS with zero swaps.
8. Repository freezes now own 2,577 production diagnostics, 1,268 bounded
   authored sources, 1,559 SPDX-owned files, 472 conformance test/control
   files, and 676 FST test/control files. The regenerated source manifest has
   1,624 ordered paths at SHA-256
   `eefee94662f873b1d00da92e6c2b46bae66aa1d1474499664c370654e1a11205`.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 180 Change 2: store
   model fingerprint, source inventory, run metadata, metrics, and exclusions
   inside the bounded container model. Preserve Change 1 and do not begin
   Change 3 in the same bounded slice.

## Batch 180 active checkpoint - after Change 2

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-2 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 2 adds `include/fsim/artifact/coverage_database_model.hpp`,
   `src/artifact/coverage_database_model.cpp`, and
   `tests/artifact/coverage_database_model_test.cpp`. The artifact library,
   source manifest, resource contract, diagnostics catalog, and focused CTest
   own the surface.
3. The owning content model requires schema 3,
   `fsim-unified-coverage-database-v3`, and a nonzero model digest. Source
   inventory records stable identity, safe checkout-independent logical path,
   content size, and content digest. Run metadata records stable identity,
   label, producer, seed, final tick/delta, and complete, stopped, or failed
   status.
4. Metric bins retain namespace, family, source or instance scope, bin,
   source, instance, and run identities, hit count, and saturation state.
   Code owns statement, branch, line, condition, expression, toggle, FSM-state,
   and FSM-transition families; SystemVerilog functional coverage owns
   coverpoint and cross families; PSL owns directive and property families.
   Cross-namespace family pairings are rejected.
5. Exclusions retain their namespace/family/scope, stable point and owner
   identities, and required reason. No combined namespace or synthetic grand
   score exists. Construction sorts sources, runs, metrics, and exclusions by
   their complete canonical identities before transactional validation.
6. `FSIM-COV-032` rejects direct v2 or model drift, zero identities or digests,
   unsafe paths, invalid status/namespace/family/scope, missing source/run
   ownership, unsaturated overflow state, empty or excessive text, duplicate
   identities, noncanonical externally supplied models, arithmetic overflow,
   allocation failure, and every count ceiling without publishing partial
   contents.
7. Default limits bound sources to 1,048,576, runs to 65,536, metrics and
   exclusions independently to 16,777,216, logical paths and reasons to 1 MiB,
   labels and producers to 64 KiB, and all retained text to 1 GiB. The
   independently authored corpus exercises canonical repeatability, all
   namespace/family groups and scopes, source/run ownership, exclusions,
   invalid and duplicate records, order drift, saturation, and every limit.
8. The exact-LLVM warnings-as-errors Debug target rebuilds eight steps with
   eight workers in 1.93 wall seconds at 202,836 KiB peak RSS with zero swaps.
   The focused schema/model, diagnostic, manifest, resource, and retained-audit
   set passes 13/13 in 7.57 wall seconds at 27,748 KiB peak RSS with zero
   swaps. The post-documentation rerun passes the same 13/13 in 7.32 wall
   seconds at 27,772 KiB peak RSS with zero swaps.
9. Repository freezes now own 2,578 production diagnostics, 1,271 bounded
   authored sources, 1,562 SPDX-owned files, 473 conformance test/control
   files, and 677 FST test/control files. The regenerated source manifest has
   1,627 ordered paths at SHA-256
   `31d2f517ffe6e6b0524abce624c79d90ea62d15dc860513f44b64090f87baf78`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI
    inspection, commit, or push ran. Proceed only to Batch 180 Change 3: move
    SystemVerilog functional coverage into its unified database namespace.
    Preserve Changes 1-2 and do not begin Change 4 in the same bounded slice.

## Batch 180 active checkpoint - after Change 3

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-3 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 3 adds
   `include/fsim/frontend/coverage_database_systemverilog.hpp`,
   `src/frontend/coverage_database_systemverilog.cpp`, and
   `tests/frontend/coverage_database_systemverilog_test.cpp`. It extends the
   generic metric record with independently saturated excluded-hit accounting.
   The frontend/library targets, source manifest, resource contract,
   diagnostics catalog, and focused CTest own the surface.
3. `project_systemverilog_coverage_namespace` consumes the existing owning
   `SystemVerilogCoverageState`; it does not introduce a competing functional
   coverage runtime model. It appends scored coverpoint and cross state to an
   already validated unified database model and refuses to replace a nonempty
   SystemVerilog-functional namespace.
4. Stable domain-separated hashes identify each covergroup instance,
   coverpoint bin, and cross bin. Every result retains its source binding, run,
   instance scope, hit count, and saturation. Crosses additionally retain
   excluded-hit count and independent saturation; sticky excluded crosses
   produce an explicit exclusion owning point, source, instance, and reason.
5. The database intentionally omits derived reports, callbacks, traces,
   aliases, transition progress, prior samples, and illegal sample history.
   Those remain live/runtime or design-model state rather than scored result
   state. Change 5 still owns deterministic namespace byte encoding and atomic
   `.fsimcov` file replacement.
6. `FSIM-COV-033` rejects an invalid/v2 database model, missing run, invalid or
   duplicate source binding, source drift, invalid or duplicate declaration or
   instance, missing declaration/bin ownership, non-regular scored bin,
   inconsistent threshold/covered state, nonempty destination namespace,
   allocation failure, and resource excess without publishing partial state.
7. Default limits independently bound 1,048,576 source bindings, declarations,
   and instances, 16,777,216 total scored bins, and 1 MiB source names and
   persistent identities. The independently authored corpus proves unordered
   deterministic input, header/source ownership, sibling-instance separation,
   coverpoint/cross families, saturated hit/excluded-hit values, sticky
   exclusions, every malformed ownership case, v2 rejection, and all limits.
8. The exact-LLVM warnings-as-errors Debug impact build completes 17 steps with
   eight workers in 4.02 wall seconds at 299,428 KiB peak RSS with zero swaps.
   The focused frontend, schema/model/namespace, diagnostic, manifest,
   resource, and retained-audit set passes 16/16 in 7.40 wall seconds at 75,076
   KiB peak RSS with zero swaps. The post-documentation rerun passes the same
   16/16 in 7.34 wall seconds at 74,872 KiB peak RSS with zero swaps.
9. Repository freezes now own 2,579 production diagnostics, 1,274 bounded
   authored sources, 1,565 SPDX-owned files, 474 conformance test/control
   files, and 678 FST test/control files. The regenerated source manifest has
   1,630 ordered paths at SHA-256
   `0e516c1178d321cd96eda8ee5dab7762fe9dd197b17c7bf5d75e2fdb819ec9e2`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 4: move PSL
    coverage into its unified database namespace. Preserve Changes 1-3 and do
    not begin Change 5 in the same bounded slice.

## Batch 180 active checkpoint - after Change 4

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-4 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 4 adds `include/fsim/app/coverage_database_psl.hpp`,
   `src/app/application_coverage_database_psl.cpp`, and
   `tests/app/coverage_database_psl_test.cpp`. The application/library targets,
   source manifest, resource contract, diagnostics catalog, and focused CTest
   own the surface.
3. `project_psl_coverage_namespace` consumes the existing
   `ConcurrentAssertionCoverage` vector rather than introducing a parallel PSL
   runtime model. Every assert, assume, cover, and restrict directive produces
   one directive-attempt metric plus separate pass, failure, vacuous, and
   aborted property-outcome metrics in the PSL namespace.
4. Stable length-delimited, domain-separated hashes incorporate directive
   name, process, kind, slot, semantic source, instance, and outcome role.
   Caller-supplied semantic-source bindings attach results to the unified
   source inventory; every metric is instance-scoped and run-owned. Maximum
   counters retain saturation. No synthetic combined PSL score is stored.
5. Completed outcome counts must add exactly to attempts using checked uint64
   arithmetic. `FSIM-COV-034` also rejects invalid/v2 database state, missing
   run, missing or duplicate source binding, unknown directive kind, empty or
   excessive identity, duplicate directive identity, unknown source, nonempty
   PSL destination, allocation failure, and resource excess without partial
   publication.
6. Default limits independently bound 1,048,576 source bindings and directives
   plus 1 MiB for each identity component; the shared model's 16,777,216 metric
   ceiling remains the final aggregate bound. The corpus proves all four
   directive kinds and five metric roles, multiple sources and instances,
   deterministic unordered input, maximum counters, sum mismatch and overflow,
   duplicate/missing ownership, v2/model rejection, namespace replacement,
   and every limit.
7. Actual `.fsimcov` encoding remains Change 5. The live VHDL/PSL application
   test remains in the focused shield so projection changes cannot silently
   alter the source coverage producer.
8. The exact-LLVM warnings-as-errors Debug impact build completes nine steps
   with eight workers in 12.12 wall seconds at 1,577,984 KiB peak RSS with zero
   swaps. The focused frontend, both namespace projections, live VHDL/PSL
   application, schema/model, diagnostic, manifest, resource, and retained-
   audit set passes 18/18 in 10.29 wall seconds at 249,352 KiB peak RSS with
   zero swaps. The post-documentation rerun passes the same 18/18 in 10.30 wall
   seconds at 249,560 KiB peak RSS with zero swaps.
9. Repository freezes now own 2,580 production diagnostics, 1,277 bounded
   authored sources, 1,568 SPDX-owned files, 475 conformance test/control
   files, and 679 FST test/control files. The regenerated source manifest has
   1,633 ordered paths at SHA-256
   `b5aa7ef253369c58ca269463a9e0d5b4a695aa91f570bce34cd5f92d1e3691b5`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 5:
    deterministic serialization and atomic replacement. Preserve Changes 1-4
    and do not begin Change 6 in the same bounded slice.

## Batch 180 active checkpoint - after Change 5

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-5 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 5 adds `include/fsim/artifact/coverage_database_codec.hpp`,
   `src/artifact/coverage_database_codec.cpp`, and
   `tests/artifact/coverage_database_codec_test.cpp`. The artifact library,
   source manifest, resource contract, diagnostics catalog, and focused CTest
   own the surface.
3. Serialization emits the exact 64-byte big-endian outer header and three
   64-byte directory entries established by Change 1. Code,
   SystemVerilog-functional, and PSL payloads are packed without gaps in that
   order, carry direct v3 namespace headers, and have independent SHA-256
   digests in the directory.
4. The code payload owns the model fingerprint plus canonical source and run
   inventories. Each namespace owns only its metric and exclusion records.
   Text is length-prefixed; numeric values, counters, and 128-bit identities
   are big-endian; flags and reserved bytes are exact. Encoding canonicalizes
   the complete model first, so unordered equivalent input produces identical
   bytes.
5. Decoding verifies outer schema/layout/file size, namespace schema/kind and
   extent, digest, exact count ceilings, minimum possible encoded byte size
   before reserve, text limits, flags/reserved bytes, namespace ownership,
   trailing bytes, canonical record order, references, and saturation before
   publishing. Container and namespace v2 inputs are rejected directly.
6. `write_coverage_database_atomically` finishes encoding before filesystem
   mutation. It recovers a missing destination from `.fsim-old`, writes the
   complete `.fsim-tmp`, preserves the old destination before publication,
   restores it if publication fails, and removes governed siblings after
   success. `read_coverage_database` applies the container ceiling before file
   allocation. Failures are reported under `FSIM-COV-035` without partial
   decoded or newly written state.
7. The corpus proves deterministic all-namespace round trips, every record
   family and saturation flag, outer and namespace v2 rejection, digest
   corruption, truncation, encode/decode ceilings, initial publish,
   replacement, interrupted-write recovery, sibling cleanup, and I/O failure.
8. The exact-LLVM warnings-as-errors Debug target rebuilds eight steps with
   eight workers in 1.78 wall seconds at 224,316 KiB peak RSS with zero swaps.
   The focused frontend, schema/model/codec and namespace projections, live
   VHDL/PSL application, diagnostic, manifest, resource, and retained-audit
   set passes 19/19 in 10.44 wall seconds at 249,560 KiB peak RSS with zero
   swaps.
9. Repository freezes now own 2,581 production diagnostics, 1,280 bounded
   authored sources, 1,571 SPDX-owned files, 476 conformance test/control
   files, and 680 FST test/control files. The regenerated source manifest has
   1,636 ordered paths at SHA-256
   `1288709b1190b253db31eda0e98a55f8535d9ab8270754475a34e670b8eab935`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 6: strict
    same-design merging as the default. Preserve Changes 1-5 and do not begin
    Change 7 in the same bounded slice.

## Batch 180 active checkpoint - after Change 6

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-6 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 6 adds `include/fsim/artifact/coverage_database_merge.hpp`,
   `src/artifact/coverage_database_merge.cpp`, and
   `tests/artifact/coverage_database_merge_test.cpp`. The artifact library,
   source manifest, resource contract, diagnostics catalog, and focused CTest
   own the new default merge surface.
3. `merge_coverage_databases` is the deliberately strict default. It
   canonicalizes and validates each input as a direct v3 model and requires
   exact equality of the model fingerprint, canonical source inventory, and
   design-level exclusion inventory before accepting any additional runs.
4. Successful merge unions distinct run records and their per-run metrics
   across code, SystemVerilog-functional, and PSL namespaces. A repeated run
   identity is rejected instead of replayed or summed, preventing accidental
   double counting. Final canonicalization makes output independent of input
   record order and database operand order.
5. Empty input, malformed/v2 input, fingerprint, source, or exclusion mismatch,
   duplicate run, allocation failure, and resource exhaustion have stable
   result categories under `FSIM-COV-036`. The API returns an optional result
   with input and record attribution and never mutates or partially publishes
   caller inputs.
6. Input count is bounded at 65,536. Aggregate run and metric sizes are checked
   against the shared model ceilings before reserve. Inputs are canonicalized
   one candidate at a time rather than retained as a second full collection,
   and an ordered run-identity set replaces a quadratic duplicate scan. The
   final shared-model pass also enforces aggregate text and reference limits.
7. The independently authored corpus proves canonical and operand-order
   determinism, all three namespaces, preserved saturation, exact inventory
   ownership, every mismatch category, duplicate-run rejection, malformed
   references, v2 rejection, transactional input preservation, and input/run/
   metric/text ceilings.
8. The exact-LLVM warnings-as-errors Debug merge target rebuilds eight steps
   with eight workers in 0.83 wall seconds at 142,336 KiB peak RSS with zero
   swaps. The focused database, live coverage, artifact/cache, diagnostic,
   manifest, resource, and retained-audit set passes 21/21 in 7.21 wall seconds
   at 92,504 KiB peak RSS with zero swaps.
   The post-documentation rerun passes the same 21/21 in 7.30 wall seconds at
   92,196 KiB peak RSS with zero swaps.
9. Repository freezes now own 2,582 production diagnostics, 1,283 bounded
   authored sources, 1,574 SPDX-owned files, 477 conformance test/control
   files, and 681 FST test/control files. The regenerated source manifest has
   1,639 ordered paths at SHA-256
   `1097a8b881d26374bf2c126a83ad27d46632dcd75beaa55877765a30f096b69c`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 7: explicit
    partial merging of unchanged point identities. Preserve Changes 1-6 and do
    not begin Change 8 in the same bounded slice.

## Batch 180 active checkpoint - after Change 7

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-7 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 7 adds `include/fsim/artifact/coverage_database_partial_merge.hpp`,
   `src/artifact/coverage_database_partial_merge.cpp`, and
   `tests/artifact/coverage_database_partial_merge_test.cpp`. The artifact
   library, source manifest, resource contract, diagnostics catalog, and
   focused CTest own the explicit compatibility surface.
3. `merge_coverage_databases_partially` takes a distinguished current target
   plus historical databases. It preserves the target's direct-v3 model
   fingerprint, canonical source inventory, point inventory, and exclusion
   policy; the strict merge from Change 6 remains the default API.
4. A historical metric survives only when its complete point key---namespace,
   family, scope, source identity, instance identity, and bin identity---exists
   in the target and its complete source record is unchanged. Changed or
   removed sources, point kinds, bins, and hierarchy instances are omitted and
   counted. Historical exclusion records never replace or extend the target's
   current exclusion policy and are counted as omitted.
5. Only historical runs referenced by at least one retained metric are copied.
   A retained run identity that already exists in the target or earlier
   history is rejected rather than replayed; a duplicate identity belonging
   only to wholly omitted data has no effect. Final canonicalization makes the
   result independent of historical operand and record order.
6. Target-point membership is binary-searched directly in the canonical target
   metric prefix, so partial merge does not allocate a second whole point
   inventory. Unchanged source identities use a compact sorted vector, and
   candidates are canonicalized one at a time. Total examined history is
   independently bounded at 4,194,304 sources, 1,048,576 runs, 67,108,864
   metrics, and 16,777,216 exclusions, in addition to the 65,536-database and
   shared output limits.
7. `FSIM-COV-037` owns invalid target/history, direct-v2 rejection, retained-run
   conflict, allocation failure, and examined/output resource failures. The
   optional result is absent on every failure and caller inputs are unchanged.
   Success statistics expose unchanged-source matches and retained/omitted
   runs, metrics, and historical exclusions.
8. The independently authored corpus proves target authority, all three
   namespaces, saturation preservation, changed-source/point/instance
   omission, exclusion isolation, unused-run omission, duplicate retained-run
   refusal, history-order determinism, malformed/v2 rejection, transactional
   input preservation, and every examined/output ceiling.
9. The exact-LLVM warnings-as-errors Debug target rebuilds eight steps with
   eight workers in 1.14 wall seconds at 175,092 KiB peak RSS with zero swaps.
   The focused database, live coverage, artifact/cache, diagnostic, manifest,
   resource, and retained-audit set passes 22/22 in 7.35 wall seconds at 92,480
   KiB peak RSS with zero swaps. The post-documentation rerun passes the same
   22/22 in 7.57 wall seconds at 92,976 KiB peak RSS with zero swaps.
   Repository freezes now own 2,583 production
   diagnostics, 1,286 bounded authored sources, 1,577 SPDX-owned files, 478
   conformance test/control files, and 682 FST test/control files. The
   regenerated source manifest has 1,642 ordered paths at SHA-256
   `43a97caa9606b186109e3b3d35c3f1741dd42964d081f258f63a10b981beff02`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 8: the
    standardized SystemVerilog coverage constants and `$coverage_control`.
    Preserve Changes 1-7 and do not begin Change 9 in the same bounded slice.

## Batch 180 active checkpoint - after Change 8

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-8 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 8 adds no new path. Existing frontend, elaboration, SimIR, runtime,
   LLVM, application, artifact-codec, cache-test, diagnostics, resource-
   contract, and CTest owners now carry the standardized SystemVerilog
   constants and `$coverage_control` surface.
3. SystemVerilog 2005, 2009, 2012, and 2017 expose exact command values 0-3,
   scope values 10-11, coverage-type values 20-23, and signed result values
   -2 through 2. Verilog profiles reject the macros. The bounded call surface
   currently accepts exactly command and coverage type; module, hierarchy,
   instance, and further selection arguments remain Change 11.
4. The parser records a signed 32-bit result and `FSIM-COV-038` owns invalid
   arity or lowered call shape. Elaboration preserves signed 32-bit operands
   and emits one `CoverageControl` operation. Scope and coverage-type enums are
   separate so selector values cannot masquerade as metric families.
5. Statement START, STOP, RESET, and CHECK directly control the interpreter's
   simulation-owned saturating code-counter table. A stopped table suppresses
   hits and exposes no direct LLVM counter span; reset clears counters and
   overflow state without replacement allocation; check returns NOCOV, OK, or
   OVERFLOW as applicable. Invalid or unknown operands return ERROR. A bounded
   hook delegates non-statement families without embedding their state in the
   code-counter table.
6. Interpreter and compiled O0-O3 execution share the same control function.
   The operation is validated, encoded by the design-artifact codec, included
   in structural process sharing and native cache identity, and lowered as a
   checked resume boundary. Coverage-enabled application setup sizes counters
   from an attached inventory or scans effective shared-counter identities;
   default-disabled builds retain no counter table or hit operation.
7. The independently authored application corpus proves every macro value,
   profile and arity rejection, interpreter/O0/O2 parity, start/stop/reset/
   check, unavailable/unsupported/invalid/unknown results, counter clearing,
   sharing, and design-artifact round trip. The LLVM corpus proves that command
   and coverage-type register changes each invalidate the persistent object
   cache. Moving that case into the existing control-cache source returned the
   main cache test to 2,486 lines and restored the source-budget gate.
8. The 542-step exact-LLVM warnings-as-errors Debug impact rebuild uses eight
   workers and passes in 8:20.29 at 2,791,856 KiB peak RSS with zero swaps. The
   six-step source-budget correction rebuild passes in 13.93 seconds at
   660,204 KiB peak RSS. The focused database, live coverage, interpreter/
   LLVM/cache, diagnostic, source, resource, and retained-audit set passes
   31/31 in 25.14 seconds at 190,040 KiB peak RSS with zero swaps. Its post-
   documentation rerun passes the same 31/31 in 24.02 seconds at 189,708 KiB
   peak RSS with zero swaps.
9. Repository freezes now own 2,584 production diagnostics, 1,286 bounded
   authored sources, 1,577 SPDX-owned files, 478 conformance test/control
   files, and 682 FST test/control files. No new path was added; the source
   manifest remains 1,642 ordered paths at SHA-256
   `43a97caa9606b186109e3b3d35c3f1741dd42964d081f258f63a10b981beff02`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 9:
    `$coverage_get`, `$coverage_get_max`, `$coverage_merge`, and
    `$coverage_save`. Preserve Changes 1-8 and do not begin Change 10 in the
    same bounded slice.

## Batch 180 active checkpoint - after Change 9

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-9 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 9 adds one path,
   `src/app/application_simulation_coverage.tpp`. Existing frontend,
   elaboration, SimIR, interpreter, LLVM, application, artifact-codec,
   cache-test, diagnostics, resource-contract, and CTest owners now carry the
   standardized aggregate query/merge/save surface.
3. SystemVerilog 2005, 2009, 2012, and 2017 recognize `$coverage_get(type)`,
   `$coverage_get_max(type)`, `$coverage_merge(type, filename)`, and
   `$coverage_save(type, filename)`. The result and type remain signed 32-bit;
   file operands remain exact strings. Named arguments, invalid arity,
   non-string filenames, and wrong profiles fail through `FSIM-COV-039`.
   Selector-bearing module/hierarchy/instance forms remain Change 11.
4. `CoverageAccess` owns distinct get, get-max, merge, and save identities and
   permits a filename only for the latter two. Validation, design-artifact
   round trip, structural sharing, persistent native-cache identity, direct
   interpretation, and compiled O0-O3 resume boundaries all retain those
   fields. Unknown types/values return ERROR or NOCOV instead of throwing
   across HDL execution.
5. Direct statement queries count configured bins and bins with nonzero hits,
   with signed-result overflow containment. The application service projects
   attached source/instance/point inventories and live saturating counters into
   the v3 code namespace, unions identical bins across runs, and keeps a
   distinct current-run identity. Designs with no executable point table and
   assertion/FSM/toggle families without a connected standard service return
   NOCOV rather than fabricated results.
6. Standard merge reads only the bounded direct-v3 `.fsimcov` codec and uses
   strict same-design merge transactionally across existing history, the
   loaded database, and the current run. Standard save atomically replaces the
   sandboxed destination with the accumulated history plus current snapshot.
   Invalid paths, corrupt/v2 files, model/source/exclusion conflicts, duplicate
   runs, and resource failures return ERROR without changing history or
   publishing a partial destination. The older functional-coverage-only
   `$set_coverage_db_name`/`$load_coverage_db` surface remains separate.
7. The independently authored application test exercises all four calls in
   interpreter, compiled O0, and compiled O2 modes, including artifact round
   trip, unavailable-family/no-point results, exact direct-counter get/max
   values, filename event delivery, malformed filename rejection, and
   filename/access-kind native-cache invalidation. The exact LLVM cache corpus
   and all retained `.fsimcov` model/codec/merge tests remain green.
8. The warnings-as-errors Debug impact build uses eight workers and completes
   cleanly. The focused database, live coverage, interpreter/LLVM/cache,
   diagnostic, source, resource, and retained-audit set passes 53/53 in 24.39
   wall seconds at 189,724 KiB peak RSS with zero swaps. The final post-
   documentation and endian-stability rerun passes the same 53/53 in 24.66
   wall seconds.
9. Repository freezes now own 2,585 production diagnostics, 1,287 bounded
   authored sources, 1,578 SPDX-owned files, 478 conformance test/control
   files, and 682 FST test/control files. The source manifest has 1,646
   ordered paths at SHA-256
   `c043563bc4e5d30109d342bd040dfcb73534f7e20fd5ebea7929fd7432510141`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 10: the
    corresponding VPI coverage controls, properties, and traversal. Preserve
    Changes 1-9 and do not begin Change 11 in the same bounded slice.

## Batch 180 active checkpoint - after Change 10

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-10 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 10 adds three paths: `include/fsim/runtime/vpi_coverage.hpp`,
   `src/runtime/vpi_coverage.cpp`, and
   `tests/runtime/vpi_coverage_test.cpp`. The public application API, VPI C
   ABI constants, ordinary VPI object registry, simulation setup/coverage
   fragments, runtime C ABI corpus, diagnostics, source manifest, resource
   contract, and retained release audits carry the corresponding integration.
3. `SystemVerilogVpiCoverageService` owns the typed standard control, property,
   FSM/state object, exact state-value, relation, iterator, and failure
   surfaces. The numeric C ABI identities span controls 750-755, FSM relations
   758-759 and 775-776, metric types 760-763, general properties 765-767, and
   assertion counters 770-774 and 777; guarded legacy spellings are exact
   aliases rather than separate values.
4. Coverage object handles reserve bit 62, iterator handles additionally use
   bit 63, and both retain bounded owner, epoch, and slot identities. Ordinary
   VPI objects now use bits 40-61 for registry ownership and explicitly reject
   the coverage tag, preventing cross-domain aliasing. Foreign-owner, stale,
   released, malformed, exhausted-generation, and resource failures publish
   no object or iterator.
5. FSM publication validates the instance, state-expression object and type,
   nonempty bounded identity, distinct legal-state names and exact encodings,
   cumulative machine/state ceilings, and slot arithmetic before one
   transaction publishes the FSM and all state records. Bidirectional
   expression/FSM lookup, instance-to-FSM traversal, FSM-to-state traversal,
   iterator release, and exact stored-state reads are deterministic.
6. Properties distinguish assertion, FSM-state, statement, and toggle
   availability and expose all-covered, maximum-bin, total-hit, attempts,
   successes, failures, vacuous successes, disables, and kills with explicit
   signed-integer overflow. Provider and control exceptions are contained;
   callbacks run outside service locks and object/service locking has one
   order.
7. Application setup exposes the service next to the existing VPI services.
   Attached statement counters and concurrent-assertion results are keyed by
   their exact published VPI handles; statement controls use the existing live
   counter table and merge/save use the direct-v3 Change 9 persistence path.
   A coverage-enabled design with no executable counters advertises no phantom
   target. Change 11 still owns module/hierarchy/instance and coverage-type
   selector interpretation beyond an already published exact handle.
8. The independent runtime/application corpus proves exact ABI values,
   control and filename delivery, metric/property behavior, assertion counter
   separation, FSM relations/state values/traversal, handle-domain separation,
   iterator lifecycle, cross-simulation and duplicate rejection, overflow,
   exception containment, empty-target suppression, and cumulative
   transactional ceilings. Warnings-as-errors Debug impact targets build
   cleanly with eight workers.
9. The post-documentation 48-test coverage lane and 12-test ABI/governance
   lane pass 60/60 in 19.13 aggregate wall seconds at 91,332 KiB peak RSS with
   zero swaps.
   Repository freezes own 2,586 diagnostics, 1,290 bounded authored sources,
   1,581 SPDX-owned files, 479 conformance test/control files, and 683 FST
   test/control files. The source manifest has 1,649 ordered paths at SHA-256
   `0465c883805df9c6dc75a6dc5487e6204f6e1c2e6e7ee5abc9078f98b01e6a24`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 11: module,
    hierarchy, instance, and coverage-type selection. Preserve Changes 1-10
    and do not begin Change 12 in the same bounded slice.

## Batch 180 active checkpoint - after Change 11

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-11 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 11 adds no new path. Existing frontend, elaboration, SimIR, runtime,
   LLVM, application, design-artifact, native-cache, application-test,
   diagnostics, and resource-contract owners now carry standardized module,
   hierarchy, instance, and coverage-type selection.
3. `$coverage_control` requires exactly command, type, scope, and selector;
   `$coverage_get` and `$coverage_get_max` require exactly type, scope, and
   selector. `$coverage_merge` and `$coverage_save` retain exactly type and
   filename. SystemVerilog 2005, 2009, 2012, and 2017 own these forms, while
   malformed arity/profile/operand shapes remain `FSIM-COV-038` or
   `FSIM-COV-039` failures.
4. String-valued selectors name module definitions and fan out over every
   matching design occurrence. Hierarchical expressions name instances;
   `$root`, `$root.`-prefixed names, absolute occurrence paths, and
   call-site-relative paths are normalized explicitly. Module scope selects
   seed occurrences only, while hierarchy scope includes descendants only
   across an exact `.` path boundary.
5. `CoverageControl` and `CoverageAccess` preserve scope register, selector
   string register, originating hierarchy, and the module-versus-instance
   selector identity. Validation checks register widths, string ownership,
   access-kind field ownership, and nonempty instance context. Portable design
   serialization, structural operation sharing, interpreter/compiled boundary
   events, and native cache keys retain every field.
6. Application selection maps DesignIR occurrences to attached elaboration
   inventory instances and their statement counters. A bounded scan of
   effective per-process `CodeCoverageHit` counters remains available for a
   design carrying hits without an attached inventory. Valid assertion, FSM,
   and toggle requests return NOCOV until their standard live services are
   connected; invalid type, scope, selector, or counter ownership returns
   ERROR; a partially instrumented selected hierarchy returns PARTIAL.
7. Counter collection now owns a per-counter enable mask. Selected start/stop
   changes only those counters, selected reset clears only those counters, and
   selected check examines only their overflow state. A partial mask disables
   the compiled direct-counter pointer so LLVM uses the checked runtime hit
   callback and cannot bypass selective collection; restoring the full mask
   restores the direct fast path.
8. The independent application corpus attaches a valid three-instance
   statement inventory at the established elaboration seam and proves root
   hierarchy, two-instance module-definition fanout, exact single-instance
   selection, exact 3/2/1 get-max results, missing selector and invalid
   scope/type containment, and interpreter/compiled equivalence. Direct SimIR
   tests prove selected stop/start/reset/check semantics, and LLVM cache tests
   vary scope, selector, instance context, and selector kind independently.
9. The warnings-as-errors Debug impact build uses eight workers and completes
   cleanly. The post-documentation 48-test coverage lane and 12-test focused
   ABI, artifact, LLVM, runtime, diagnostic, source, and governance lane pass
   60/60 in 42.98 aggregate wall seconds at 189,588 KiB peak RSS with zero
   swaps.
10. Repository freezes remain at 2,586 diagnostics, 1,290 bounded authored
    sources, 1,581 SPDX-owned files, 479 conformance test/control files, and
    683 FST test/control files. No new path was added, so the source manifest
    remains 1,649 ordered paths at SHA-256
    `0465c883805df9c6dc75a6dc5487e6204f6e1c2e6e7ee5abc9078f98b01e6a24`.
    No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 12: source
    `fsim coverage off/on` controls with metric and reason. Preserve Changes
    1-11 and do not begin Change 13 in the same bounded slice.

## Batch 180 active checkpoint - after Change 12

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-12 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 12 adds three paths:
   `include/fsim/frontend/coverage_source_control.hpp`,
   `src/frontend/coverage_source_control.cpp`, and
   `tests/elaboration/coverage_source_control_test.cpp`. Existing Verilog and
   VHDL statement-point discovery, build/test registration, diagnostics,
   source manifest, resource contract, and retained release audits carry the
   integration.
3. Real Verilog/SystemVerilog `//` and VHDL `--` comments accept exact
   `fsim coverage off metric=<metric> reason="..."` and matching
   `fsim coverage on metric=<metric>` controls. Strings, Verilog block
   comments, and look-alike prose are ignored. Off reasons are nonempty;
   escaped quote and backslash are decoded; on reasons are forbidden.
4. Metrics comprise `all`, `statement`, `branch`, `line`, `condition`,
   `expression`, `toggle`, `fsm_state`, `fsm_transition`, `coverpoint`,
   `cross`, `psl_directive`, and `psl_property`. Distinct metrics may overlap;
   duplicate opens, unmatched closes, and wildcard/specific overlap reject the
   complete source-control plan transactionally under `FSIM-COV-041`.
5. Source, line, directive, individual-reason, and cumulative-reason inputs
   have explicit ceilings. An off region may extend to EOF. Exclusions retain
   exact byte bounds, metric, reason, and opening/closing lines in memory and
   are sorted deterministically. Change 14, not this slice, owns retaining
   every excluded point and reason in database/report output.
6. Verilog-family and VHDL statement discovery parse controls only from raw
   source text whose byte count and SHA-256 match its stable source identity.
   A statement is omitted only when its starting byte belongs to a statement
   or wildcard exclusion; a branch-only exclusion demonstrably leaves the
   statement inventory unchanged. The shared lookup supports all metric
   families for their respective inventory owners.
7. The independent corpus proves deterministic parsing, all metric spellings,
   concurrent distinct-metric regions, escaped reasons, EOF closure, SV/VHDL
   string decoys, block-comment and prefix decoys, authenticated-source
   rejection, malformed/conflicting directives, each ceiling, and exact
   Verilog/SystemVerilog/VHDL statement behavior. The three focused
   warnings-as-errors Debug targets build cleanly with eight workers.
8. The pre-documentation coverage lane passes 49/49 in 10.92 wall seconds at
   90,528 KiB peak RSS with zero swaps. After advancing exact repository
   counts, the post-documentation 49-test coverage lane and 14-test focused
   source-control, statement-discovery, diagnostic, source, manifest,
   resource, and retained-audit lane pass 63/63 in 12.54 aggregate wall
   seconds at 90,608 KiB peak RSS with zero swaps.
9. Repository freezes now own 2,587 diagnostics, 1,293 bounded authored
   sources, 1,584 SPDX-owned files, 480 conformance test/control files, and 684
   FST test/control files. The source manifest has 1,652 ordered paths at
   SHA-256
   `e76c60e5de9decc10af09cae88d69f2212a1320269463b54c58b7bf47a57177a`.
10. No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 13: external
    source, hierarchy, object, and metric exclusion rules. Preserve Changes
    1-12 and do not begin Change 14 in the same bounded slice.

## Batch 180 active checkpoint - after Change 13

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-13 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 13 adds three paths:
   `include/fsim/elaboration/coverage_external_exclusions.hpp`,
   `src/elaboration/coverage_external_exclusions.cpp`, and
   `tests/elaboration/coverage_external_exclusions_test.cpp`. Existing project
   configuration, application build validation, code-coverage application
   corpus, build/test registration, diagnostics, source manifest, resource
   contract, and retained release audits carry the integration.
3. Repeatable `[[coverage.exclude]]` manifest tables accept optional `source`,
   `hierarchy`, and `object` glob selectors plus required nonempty `metric` and
   `reason` strings. Omitted selectors match all values in that dimension;
   supplied selectors combine with logical AND. A metric-only rule is valid.
4. Metrics share Change 12's exact language-neutral vocabulary, including
   `all`. Source selectors are checkout-independent forward-slash logical
   paths; absolute, parent-relative, dot/empty-component, drive-qualified, and
   backslash forms fail. Hierarchy and object selectors reject their invalid
   separators and empty forms. Only `*` and `?` are glob operators, repeated
   stars canonicalize, and the matcher is non-recursive and work-bounded.
5. Rule construction is transactional and bounds rule count, per-pattern and
   per-reason bytes, and total retained bytes. Rules canonicalize independently
   of declaration order, retain their declaration indices, and carry stable
   per-rule and whole-plan SHA-256 identities. Equal selector/metric tuples
   reject as duplicates when reasons agree and conflicts when reasons differ.
6. Batched matching validates the immutable plan once, bounds target count,
   individual target bytes, and total matching operations, and returns one
   aligned result for every input target. Every matching canonical rule index
   is retained, including overlapping wildcard and metric-specific rules, so
   Change 14 can preserve all applicable reasons without first-match behavior.
7. `build_checked_project` constructs and validates the external plan before
   elaboration. Invalid metrics, selectors, conflicts, identities, or ceilings
   report `FSIM-COV-042` and cannot produce a partially elaborated project.
8. The independent corpus proves manifest retention, source/hierarchy/object/
   metric-only selection, wildcard and wildcard-all composition, overlapping
   reasons, empty results, declaration-order identity, corrupt-plan rejection,
   duplicate/conflicting rules, malformed patterns and targets, and every
   rule/byte/target/work ceiling. The application corpus proves pre-elaboration
   rejection. A 193-step warnings-as-errors Debug impact build completes
   cleanly with eight workers.
9. The pre-documentation coverage lane passes 50/50 in 10.91 wall seconds at
   90,916 KiB peak RSS with zero swaps. The retained seven-audit subset passes
   after advancing exact repository counts. The final post-documentation
   50-test coverage lane and 15-test focused exclusion, source-control,
   application, project, diagnostic, source, manifest, resource, and retained-
   audit governance lane pass 65/65 in 12.77 aggregate wall seconds at 93,460
   KiB peak RSS with zero swaps.
10. Repository freezes now own 2,588 diagnostics, 1,296 bounded authored
    sources, 1,587 SPDX-owned files, 481 conformance test/control files, and
    685 FST test/control files. The source manifest has 1,655 ordered paths at
    SHA-256
    `7c71bfec2a1fedcfc163d5ae53e4458dd36b24dd41ce976c6004a23ec163984e`.
    No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 14: preserve
    every excluded point and reason in the database and reports. Preserve
    Changes 1-13 and do not begin Change 15 in the same bounded slice.

## Batch 180 active checkpoint - after Change 14

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-14 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 14 adds six paths:
   `include/fsim/elaboration/coverage_exclusion_persistence.hpp`,
   `src/elaboration/coverage_exclusion_persistence.cpp`,
   `tests/elaboration/coverage_exclusion_persistence_test.cpp`,
   `include/fsim/artifact/coverage_exclusion_report.hpp`,
   `src/artifact/coverage_exclusion_report.cpp`, and
   `tests/artifact/coverage_exclusion_report_test.cpp`. Existing Verilog-family
   and VHDL statement discovery, database model/codec corpus, build/test
   registration, diagnostics, source manifest, resource contract, and retained
   release audits carry the integration.
3. Source-controlled executable statements remain absent from scored point
   vectors and receive no runtime counter, but discovery now retains a separate
   exclusion record containing the same stable point identity, language or
   construct, authenticated source span, source index, line, and exact reason.
   Rejection remains transactional and clears scored and excluded output.
4. `make_coverage_exclusion_records` consumes fully elaborated point, source,
   and instance identities plus source, hierarchy, and object target names.
   It maps every unified database family to the exact Change 12 selector
   vocabulary, retains source-control reasons at source scope, and retains
   every Change 13 external match at instance scope. Thus one source waiver is
   not redundantly serialized per instance, while a hierarchy-specific waiver
   cannot leak to sibling instances.
5. Candidate count, source-reason count, output count, individual and total
   reason bytes, identity, namespace/family pairing, target validity, external
   plan integrity, target bytes, and matching work are independently bounded.
   Invalid or duplicate candidates, invalid reasons, corrupt plans, allocation,
   and resource failure publish no partial records under `FSIM-COV-043`.
6. The canonical database exclusion key now includes the reason. Different
   reasons for the same namespace/family/scope/source/instance/point tuple are
   valid and serialize deterministically; an exact duplicate remains invalid.
   Equal reason text reached through overlapping rules canonicalizes to one
   distinct reason rather than inflating reports. Strict merge still compares
   the complete exclusion policy, and partial merge still omits historical
   exclusions by design.
7. `make_coverage_exclusion_report` first validates the complete canonical
   database and then groups each excluded point once with its full lexically
   ordered reason vector and exact aggregate reason count. Point, reason,
   individual-reason-byte, total-reason-byte, database-model, allocation, and
   order failures are bounded and transactional. Change 15 can add source,
   instance, and combined scored views over this lossless exclusion projection.
8. The independent persistence/report corpus proves source plus overlapping
   external reasons, code/SystemVerilog-functional/PSL namespaces, empty
   matches, tampered plans, invalid and duplicate candidates, invalid reasons,
   every ceiling, corrupt/noncanonical databases, distinct-reason grouping,
   exact-duplicate rejection, and deterministic codec round trips. Focused
   exact-LLVM 22.1.8 warnings-as-errors Debug targets build cleanly with eight
   workers.
9. The pre-documentation coverage lane passes 52/52 in 11.74 wall seconds at
   90,308 KiB peak RSS with zero swaps. The final post-documentation 52-test
   coverage lane and 19-test focused persistence, source/external exclusion,
   database/report, application, project, diagnostic, source, manifest,
   resource, and retained-audit governance lane pass 71/71 in 13.40 aggregate
   wall seconds at 93,612 KiB peak RSS with zero swaps.
10. Repository freezes now own 2,589 diagnostics, 1,302 bounded authored
    sources, 1,593 SPDX-owned files, 483 conformance test/control files, and
    687 FST test/control files. The source manifest has 1,661 ordered paths at
    SHA-256
    `3a753b9129e0141c37f11be3b16a0f67c7278fe34f9070680b0150ea84c9256d`.
    No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 15: source,
    instance, and combined report models without a synthetic grand score.
    Preserve Changes 1-14 and do not begin Change 16 in the same bounded slice.

## Batch 180 active checkpoint - after Change 15

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-15 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 15 adds three paths:
   `include/fsim/artifact/coverage_report_model.hpp`,
   `src/artifact/coverage_report_model.cpp`, and
   `tests/artifact/coverage_report_model_test.cpp`. Build/test registration,
   diagnostics, source manifest, resource contract, and retained release audits
   carry the integration.
3. `make_coverage_report_model` accepts only a complete canonical v3 database
   and reuses the Change 14 lossless exclusion projection. It groups metric
   records across runs by namespace, family, source, point, and optional
   instance with saturating hit/excluded-hit sums and sticky overflow state.
   Exclusion-only points therefore remain reportable even though they own no
   runtime counter.
4. Every database source receives one canonical source report, including
   sources with no points. Instance occurrences union by stable source/point
   identity: covered wins over uncovered, uncovered wins over excluded, and a
   point is excluded only when no scored occurrence remains. A source-scoped
   metric or exclusion is authoritative and replaces, rather than adds to,
   instance-occurrence hits and status.
5. Instance reports retain exact stable instance identities and source-qualified
   points. Source-scoped exclusions remain in the source and combined views;
   instance-scoped exclusions remain attributable to their exact instance.
   This avoids inventing instance membership for a source-only waiver.
6. The combined view summarizes source-union points only, so repeated hierarchy
   instances cannot inflate its totals. Source, instance, and combined reports
   each expose independent namespace/family totals for covered, uncovered, and
   excluded points plus hit/saturation evidence. No public overall percentage,
   cross-family rollup, or synthetic grand-score field exists.
7. Exact reason strings are not copied into every source and instance point.
   `CoverageReportModel::exclusions` owns the single complete Change 14
   point/scope/reason projection, while the three scored views own statuses and
   counts. Construction uses one exact-point map and populates output vectors
   directly without whole-source or whole-combined point snapshots.
8. Independent ceilings bound exact points, source-union points, instance
   points, instance count, the database, and the exclusion projection. The
   corpus proves empty sources, source-union precedence, source and instance
   exclusions, code/SystemVerilog-functional/PSL families, multi-run
   saturation, deterministic reconstruction, per-family count conservation,
   corrupt/noncanonical databases, propagated exclusion failures, and every
   report ceiling under `FSIM-COV-044`. The warnings-as-errors exact-LLVM
   22.1.8 Debug target builds cleanly with eight workers.
9. The pre-documentation coverage lane passes 53/53 in 11.74 wall seconds at
   90,516 KiB peak RSS with zero swaps. The final post-documentation 53-test
   coverage lane and 20-test focused report-model, exclusion, database,
   application, project, diagnostic, source, manifest, resource, and retained-
   audit governance lane pass 73/73 in 13.26 aggregate wall seconds at 93,316
   KiB peak RSS with zero swaps.
10. Repository freezes now own 2,590 diagnostics, 1,305 bounded authored
    sources, 1,596 SPDX-owned files, 484 conformance test/control files, and
    688 FST test/control files. The source manifest has 1,664 ordered paths at
    SHA-256
    `88dd72df66ce79e3e8f576937e9c96643289780d17d8b5da433c05f7f34d417c`.
    No Release qualification, clean-first build, sanitizer, hosted-CI,
    commit, or push action ran. Proceed only to Batch 180 Change 16:
    deterministic text, HTML, and full-fidelity JSON reports. Preserve Changes
    1-15 and do not begin Change 17 in the same bounded slice.

## Batch 180 active checkpoint - after Change 16

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-16 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 16 adds seven paths:
   `include/fsim/artifact/coverage_report_render.hpp`,
   `src/artifact/coverage_report_render.cpp`,
   `src/artifact/coverage_report_render_internal.hpp`,
   `src/artifact/coverage_report_render_text.cpp`,
   `src/artifact/coverage_report_render_html.cpp`,
   `src/artifact/coverage_report_render_json.cpp`, and
   `tests/artifact/coverage_report_render_test.cpp`. Build/test registration,
   diagnostics, source manifest, resource contract, and retained release audits
   carry the integration.
3. `render_coverage_report` rejects an unknown format before constructing
   report state, then accepts only a canonical v3 database through the bounded
   Change 15 model builder. Model, database, exclusion, and record index errors
   remain attributable in the result. A checked output writer publishes no
   partial string when model construction, allocation, or the configurable
   output-byte ceiling fails under `FSIM-COV-045`.
4. Text, standalone HTML, and JSON share exact lower-case namespace, family,
   scope, status, boolean, decimal-counter, and 32-digit hexadecimal identity
   spellings. They consume the canonical model order directly and perform no
   sorting or whole-report snapshot. Separate format translation units remain
   well below the source-line budget while one private writer/escaping contract
   prevents semantic drift at the boundary.
5. Text retains source, instance, combined, point, metric, saturation, and
   exclusion evidence while escaping quotes, backslashes, and control bytes so
   database text cannot inject false records. HTML exposes the same sections in
   a fixed UTF-8 document shell and entity-escapes markup-sensitive and control
   bytes. Neither format computes an overall percentage or grand score.
6. Full-fidelity JSON uses schema `fsim-coverage-report-v3` and emits every
   Change 15 field: empty sources, source and instance identities, logical
   paths, exact point identities, namespaces, families, hits, excluded hits,
   both sticky saturation flags, statuses, per-family summaries, combined
   summaries, exclusion scopes, every reason, and the exact reason count. It
   adds no lossy percentage or synthetic-score field.
7. The independent renderer corpus proves byte-for-byte repeated rendering,
   Code, SystemVerilog-functional, and PSL namespaces, covered/uncovered/
   excluded states, UINT64_MAX counters and saturation, empty-source retention,
   complete identity and exclusion evidence, hostile text in all three escaping
   grammars, standalone HTML framing, JSON field fidelity, score omission,
   invalid-format precedence, corrupt database propagation, nested model
   ceilings, and transactional output limits. Exact-LLVM 22.1.8 warnings-as-
   errors Debug targets build cleanly with eight workers.
8. The pre-documentation coverage lane passes 54/54 in 11.58 wall seconds at
   91,664 KiB peak RSS with zero swaps. The final post-documentation 54-test
   coverage lane and 21-test focused renderer, report-model, exclusion,
   database, application, project, diagnostic, source, manifest, resource, and
   retained-audit governance lane pass 75/75 in 13.45 aggregate wall seconds at
   93,428 KiB peak RSS with zero swaps.
9. Repository freezes now own 2,591 diagnostics, 1,312 bounded authored
   sources, 1,603 SPDX-owned files, 485 conformance test/control files, and 689
   FST test/control files. The source manifest has 1,671 ordered paths at
   SHA-256
   `e31d5089272e15f6929516ed4ae9b3f354a7481ab0b3efdcd6b1b14d9536dc4d`.
   No Release qualification, clean-first build, sanitizer, hosted-CI, commit,
   or push action ran. Proceed only to Batch 180 Change 17: LCOV and Cobertura
   projections for supported metric families. Preserve Changes 1-16 and do not
   begin Change 18 in the same bounded slice.

## Batch 180 active checkpoint - after Change 17

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-17 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 17 adds three paths:
   `include/fsim/artifact/coverage_report_projection.hpp`,
   `src/artifact/coverage_report_projection.cpp`, and
   `tests/artifact/coverage_report_projection_test.cpp`. Build/test
   registration, diagnostics, source manifest, resource contract, and retained
   release audits carry the integration.
3. The v3 coverage database metric and exclusion schemas now retain a bounded
   one-based physical source line. The deterministic codec round trips it,
   exact report and exclusion views preserve it, partial merge treats a
   relocated point as changed, exclusion persistence validates it, and the
   SystemVerilog functional-coverage producer carries declaration locations.
   Change 16 text, HTML, and JSON rendering now exposes the same field. A zero
   line remains available to nonprojectable records, while a supported,
   nonexcluded projection point without a line fails explicitly. Location
   disagreement across runs, exclusion reasons, or runtime/exclusion evidence
   is invalid rather than silently reconciled.
4. `project_coverage_report` accepts only a canonical v3 database through the
   bounded Change 15 model. It projects only Code statement, explicit line,
   and branch families. SystemVerilog functional, PSL, condition, expression,
   toggle, and FSM evidence remains available through the full-fidelity
   reports but is deliberately omitted from LCOV and Cobertura. Explicit line
   records supersede statement aggregation for the same physical line, and
   excluded points do not inflate line or branch totals.
5. LCOV uses deterministic `TN`, `SF`, `DA`, numeric `BRDA`, `LF`, `LH`,
   `BRF`, `BRH`, and `end_of_record` records, including sources without
   projected points. Cobertura emits deterministic UTF-8 XML with exact root,
   package, class, line, and branch counts, fixed six-decimal rates, stable
   conditions, escaped paths, and timestamp zero. Grammar-invalid path text is
   rejected before publication.
6. Both formats consume canonical source order and build only one bounded
   source-local ordered line map at a time. Cobertura deliberately repeats the
   bounded projection pass to obtain global totals without retaining a
   whole-project duplicate. Global line, branch, nested-model, and output-byte
   ceilings publish no partial result on failure under `FSIM-COV-046`.
7. The focused corpus proves exact LCOV bytes, Cobertura framing/rates/path
   escaping, stable branch ordinals, statement aggregation and explicit-line
   precedence, unsupported and excluded omission, empty sources, deterministic
   repetition, source-line codec round trips, producer/exclusion propagation,
   relocated-point partial-merge omission, line conflicts, missing locations,
   unsafe paths, invalid formats/models, and every resource ceiling.
   Exact-LLVM 22.1.8 warnings-as-errors Debug targets build cleanly with eight
   workers.
8. The pre-documentation coverage lane passes 55/55 in 11.77 wall seconds at
   88,796 KiB peak RSS with zero swaps. The final post-documentation 55-test
   coverage lane and 22-test focused projection, report, exclusion, database,
   producer, diagnostic, source, manifest, resource, and retained-audit
   governance lane pass 77/77 in 13.44 aggregate wall seconds at 91,156 KiB
   peak RSS with zero swaps.
9. Repository freezes now own 2,592 diagnostics, 1,315 bounded authored
   sources, 1,606 SPDX-owned files, 486 conformance test/control files, and 690
   FST test/control files. The source manifest has 1,674 ordered paths at
   SHA-256
   `778fb7e040bb2617dd1d63faff4ae83363224eb473a3e17767c9b34ab54d55c5`.
   No Release qualification, clean-first build, sanitizer, hosted-CI, commit,
   or push action ran. Proceed only to Batch 180 Change 18: coverage
   merge/report commands, per-metric thresholds, and CI exit status. Preserve
   Changes 1-17 and do not begin Change 19 in the same bounded slice.

## Batch 180 active checkpoint - after Change 18

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-18 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 18 adds `src/app/application_coverage_command.cpp` and
   `tests/app/coverage_command_test.cpp`. Existing public CLI/application
   headers, service routing, build/test registration, diagnostics, source
   manifest, resource contract, and retained release audits carry the
   integration.
3. `fsim coverage merge` requires input databases and `--output`. Strict
   same-design merge is the default; `--partial` explicitly treats the first
   database as the target and the remaining inputs as history. Both paths
   reuse the bounded Change 6-7 model implementations and atomically replace
   the destination, so a conflict, corrupt input, or I/O failure publishes no
   partial database.
4. `fsim coverage report` requires exactly one database and emits deterministic
   text by default. `--format text|html|json|lcov|cobertura` routes through the
   Change 16-17 bounded report implementation. Without `--output` the complete
   report is written to standard output; with it, the report uses an atomic
   temporary/backup replacement and leaves no partial destination.
5. Repeated `--threshold FAMILY=PERCENT` selections are case-normalized,
   unique, integer-only, and limited to zero through 100. Thresholds use only
   the combined per-family report summary with `total - excluded` as the
   scored denominator and overflow-safe ceiling arithmetic. They do not form
   an overall score. Missing or wholly excluded families pass only at zero
   percent.
6. Report output is completed before threshold enforcement. An unmet valid
   threshold returns dedicated CI exit status 4 with `FSIM-COV-047`; malformed
   command syntax returns 2, while corrupt inputs, unknown metric families,
   merge/model/render/projection failures, and output failures return 1. HDL,
   simulation, trace, and native-build switches are rejected on coverage
   utility commands.
7. The independent end-to-end CLI corpus proves strict and explicit partial
   merge, mismatch rollback, corrupt input, text and atomic LCOV output,
   passing and failing thresholds including empty families, report-before-exit,
   all three exit classes, duplicate/malformed controls, bad formats and
   arities, and unrelated-option rejection. The existing application and
   code-coverage control regression hosts also rebuild and pass against the
   extended CLI service aggregate with exact-LLVM 22.1.8 warnings as errors
   and eight workers.
8. The pre-documentation coverage lane passes 56/56 in 10.98 wall seconds at
   89,940 KiB peak RSS with zero swaps. The final post-documentation 56-test
   coverage lane passes in 11.31 wall seconds at 90,968 KiB peak RSS, and the
   28-test focused command, application, database/report, project, diagnostic,
   source, manifest, resource, conformance, and retained-audit governance lane
   passes in 2.00 wall seconds at 93,392 KiB peak RSS. Together they pass
   84/84 in 13.31 aggregate wall seconds with zero swaps.
9. Repository freezes now own 2,593 diagnostics, 1,317 bounded authored
   sources, 1,608 SPDX-owned files, 487 conformance test/control files, and 691
   FST test/control files. The source manifest has 1,676 ordered paths at
   SHA-256
   `338187727d8bbc4187cb62e9224d560d895a466fdf5c05067d81f528a6768dcd`.
   No Release qualification, clean-first build, sanitizer, hosted-CI, commit,
   or push action ran. Proceed only to Batch 180 Change 19: corruption, size
   ceilings, path safety, merge conflicts, and mixed-language regressions.
   Preserve Changes 1-18 and do not begin Change 20 in the same bounded slice.

## Batch 180 active checkpoint - after Change 19

1. Remain on `codex/v3` at clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346` plus the intentionally dirty
   Batch 180 Changes 1-19 worktree. Preserve it without reset, commit, or push
   until Batch 180 Change 20.
2. Change 19 adds
   `tests/artifact/coverage_database_robustness_test.cpp`. Existing database
   model implementation/tests, build/test registration, source manifest,
   resource contract, and retained release audits carry the integration.
3. Canonical v3 logical source paths now reject ASCII drive-letter prefixes in
   both absolute and drive-relative forms. Empty, POSIX-absolute, backslash or
   UNC, repeated-separator, trailing-slash, dot, parent, and embedded-NUL paths
   remain invalid. This closes the cross-platform path gap before database
   serialization, reporting, or projection.
4. The robustness corpus builds one canonical mixed-language database with
   SystemVerilog and VHDL Code points, SystemVerilog functional coverage, and
   PSL coverage. It proves order-independent strict merging, exact codec round
   trip, source/instance/combined reports, full-fidelity JSON namespace
   retention, and Code-only LCOV/Cobertura projection including empty
   nonprojected sources.
5. An explicit target/history partial merge changes VHDL content and point
   identity, retains only unchanged SystemVerilog and PSL historical metrics,
   and reports exact retained/omitted statistics. Strict fingerprint, source,
   exclusion, and duplicate-run conflicts return their stable error classes,
   publish no result, and do not mutate inputs.
6. Every strict serialized prefix and every single-byte bit flip is rejected,
   as are appended bytes, corrupt files, and hostile declared container and
   namespace sizes. Model, codec, strict/partial merge, report-model, renderer,
   projection, and filesystem ceilings publish no partial result. An invalid
   replacement leaves an existing valid `.fsimcov` file intact.
7. Exact-LLVM 22.1.8 warnings-as-errors Debug targets build cleanly with eight
   workers. The focused schema/model/codec/merge/report/producer and mixed
   engine-equivalence lane passes 14/14, and the resource portability contract
   passes independently.
8. The pre-documentation coverage lane passes 57/57 in 11.59 wall seconds at
   91,220 KiB peak RSS with zero swaps. The final post-documentation 57-test
   coverage lane passes in 11.27 wall seconds at 89,648 KiB peak RSS, and the
   14-test robustness, diagnostic, source, manifest, resource, conformance, and
   retained-audit governance lane passes in 1.54 wall seconds at 30,424 KiB
   peak RSS. Together they pass 71/71 in 12.81 aggregate wall seconds with zero
   swaps. Repository freezes remain at 2,593 diagnostics and advance to 1,318
   bounded authored sources, 1,609 SPDX-owned files, 488 conformance
   test/control files, and 692 FST test/control files. The source manifest has
   1,677 ordered paths at SHA-256
   `048b8b8ef71122a3cb54489126c5ef507482f0dcf8a657b839909140296fbdbc`.
9. No Release qualification, clean-first build, sanitizer, hosted-CI, commit,
   or push action ran. Proceed only to Batch 180 Change 20: clean Debug/Release,
   sanitizer, hosted monitoring, documentation freeze, one implementation
   commit, and one push. Preserve Changes 1-19 until that closure begins.

## Batch 180 paused development checkpoint - Change 20 incomplete

1. Resume on `codex/v3-batch180-development`, forked from `codex/v3` at the
   clean synchronized Batch 179 commit
   `7cdaaed0f21b425fa9bd9417217dc052bdf60346`. The checkpoint contains complete
   Batch 180 Changes 1-19 plus incomplete Change 20 qualification work. Do not
   mark Change 20 complete or begin Batch 181 from this checkpoint.
2. Linux release presets and both hosted Linux Debug and Release lanes now use
   Clang 22. Windows remains on the LLVM-MinGW Clang toolchain. The former
   `linux-gcc` hosted job is named `linux-clang`; retained portability and
   release contracts were updated to freeze the Clang lane names. The three
   coverage application/equivalence targets whose implementation requires
   LLVM are registered only when `fsim_llvm` exists, so the hosted Clang Debug
   no-LLVM configuration retains the portable coverage corpus without trying
   to compile unavailable JIT types.
3. Before the late sanitizer repairs, the clean exact-LLVM Clang 22 Debug build
   completed 2,701/2,701 steps with eight workers in 16:35.68 at 4,322,976 KiB
   peak RSS with zero swaps, and its suite passed 365/365 in 391.78 seconds at
   1,100,216 KiB peak RSS. The fresh exact-LLVM Clang 22 Release build completed
   its final 1,283-step phase with eight workers in 8:49.88 at 2,773,128 KiB
   peak RSS with zero swaps, and its suite passed 365/365 in 385.64 seconds at
   1,098,416 KiB peak RSS. The user directed that these lanes need not be
   rebuilt after the closure repairs only if the final sanitizer suite passes;
   that final sanitizer pass is not yet complete, so do not claim the current
   checkpoint fully qualified.
4. The fresh Clang 22 no-LLVM ASan/UBSan build exposed and repaired two hosted
   Debug warnings-as-errors: LLVM-only AOT constants now share their LLVM guard,
   and `Simulation::native_cache_statistics` consumes its synchronization
   parameter in the no-LLVM branch. Its build then exposed three LLVM-only
   coverage test targets incorrectly registered without LLVM; their target
   registration now follows the `fsim_llvm` capability. The complete 664-step
   resumed sanitizer build passed with eight workers in 7:23.47 at 1,895,052
   KiB peak RSS and zero swaps, with no retained warning, error, failed-build,
   ASan, or UBSan markers. The no-LLVM configuration owns 361 CTest tests.
5. Local LeakSanitizer cannot execute under the workspace ptrace supervisor.
   The attempted `detect_leaks=1` suite was stopped after the uniform supervisor
   failure. All actionable local sanitizer runs therefore use
   `ASAN_OPTIONS=detect_leaks=0:strict_string_checks=1` plus
   `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1`; the checked-in CI
   sanitizer preset continues to require `detect_leaks=1` outside the tracer.
6. ASan found a real VHDL HIR heap use-after-free in
   `VhdlHirBuilder::add_type_declaration`: `add_type_payload` can append a
   declaration and reallocate the declaration vector before the caller rereads
   `output.origin`. The implementation now snapshots the stable origin ID
   before that mutation. Both original reproducers,
   `fsim.application.sdf_vital_scheduling` and
   `fsim.application.artifact_phases`, pass under ASan/UBSan, as does the fully
   relinked `fsim.application.vhdl_ieee_integration` partition.
7. The sanitizer suite later exposed a SystemC consumer-link setup failure:
   an ASan-instrumented plug-in intentionally resolves sanitizer runtime symbols
   from its instrumented host, which conflicts with `--no-undefined`. The
   non-project CLI test now retains `--no-undefined` for ordinary builds and
   omits it only under ASan while forwarding the sanitizer link option.
   `fsim.application.core_non_project_cli` then passes under ASan/UBSan in 3.15
   seconds.
8. No uninterrupted final sanitizer suite has completed after both repairs.
   The last fully relinked attempt was intentionally stopped at the user's
   checkpoint request after 117/361 tests had passed and while
   `fsim.application.systemc_matrix` was running normally with no failure
   output. Earlier diagnostic attempts are not qualification evidence. Retained
   logs are under `build/batch180-change20-evidence/`; the next action is a full
   361-test ASan/UBSan rerun with local leak detection disabled, followed by a
   sanitizer-marker scan and the scheduled governance gates.
9. Hosted CI has not been inspected or qualified, the Batch 180 Change 20 plan
   row remains open, and the accumulated checkpoint is explicitly unverified
   development work. After local sanitizer/governance closure, update the
   Change 20 documentation, merge or fast-forward the reviewed checkpoint back
   to `codex/v3`, push that integration, and monitor the exact hosted Linux
   Clang Debug/Release and Windows SHA before declaring Batch 180 complete.

## Batch 180 Windows CI repair checkpoint - pending hosted verification

1. GitHub Actions run `32574652878` for development checkpoint
   `8217dce57143072bb8b3e8c4653d30865f5deaf9` completed with all four Linux
   Clang Debug/Release lanes and the frontend fuzz lane green. All four Windows
   LLVM-MinGW Debug/Release with LLVM ON/OFF jobs failed during the build and
   did not run tests.
2. Every retained Windows log reports the same first compiler error in
   `src/runtime/packed_value.cpp`: LLVM-MinGW libc++ correctly omits the removed
   C++20 `std::shared_ptr::unique()` member that Linux libstdc++ still exposes
   as an extension. This is one shared portability defect across the matrix,
   not four independent failures.
3. Replace all five production `shared_ptr::unique()` calls with their exact
   portable `use_count() == 1` or `use_count() != 1` equivalents. The affected
   copy-on-write paths are wide `PackedLogic4` storage, `OperationList` mutable
   and recyclable storage, expression-profile storage, and interpreter
   container-register storage. A graph-backed scan confirms no `.unique()`
   calls remain under `src/` or `include/`.
4. The exact-LLVM Clang 22 warnings-as-errors Debug dependency rebuild passes
   552/552 steps with eight workers. The focused runtime, SimIR coverage, and
   code-coverage metric engine-equivalence tests pass 3/3. The no-LLVM Clang 22
   sanitizer dependency rebuild also passes with eight workers, and its focused
   runtime and SimIR coverage tests pass 2/2 under ASan/UBSan.
5. The complete no-LLVM Clang 22 ASan/UBSan tree then rebuilds 1,074/1,074
   actions with eight workers in 18:07.54 at 2,977,180 KiB peak RSS and zero
   swaps. The uninterrupted suite passes 361/361 in 9:39.46 at 2,314,484 KiB
   peak RSS and zero swaps with local leak detection disabled for the documented
   ptrace-supervisor constraint. Its retained build and test transcripts contain
   no compiler-warning, failed-build, ASan, UBSan, runtime-error, segmentation,
   or failed-CTest markers. Per the user's closure direction, that final
   sanitizer pass also carries the already completed clean Debug and Release
   qualifications without rebuilding or retesting them.
6. The v1, Windows-LLVM, and resource-portability CMake contracts pass, as does
   `git diff --check`. The Windows matrix itself remains unverified until this
   repair is pushed and its exact SHA is green; do not integrate the development
   checkpoint into `codex/v3` or declare Change 20 complete before that result.
7. The first repair run, GitHub Actions `34086042061` at exact development SHA
   `9ebaaeb2c1f28ba319e10c1fdad5560f746f0263`, proves that all four Windows
   LLVM-MinGW configurations advance beyond the removed `shared_ptr::unique()`
   calls. They then expose one common second portability error in
   `src/library/portable_unit.cpp`: libc++ does not accept the interned
   `frontend::SourceName` wrapper directly as a native Windows
   `std::filesystem::path` source. Both logical and physical span names now pass
   their explicit string views through the existing UTF-8-to-native
   `fsim::support::path_from_utf8` boundary. Exact-LLVM warnings-as-errors Debug
   and no-LLVM ASan/UBSan builds of `fsim_library_tests` pass, and the portable
   library artifact test passes 1/1 in both configurations. A follow-up hosted
   run is required before Windows or Change 20 can be declared green.
8. The follow-up run, GitHub Actions `34087236763` at exact development SHA
   `1e23553d64767391b2d6ef261c3266f7805f6985`, advances every Windows
   LLVM-MinGW configuration beyond portable-unit compilation. All four then
   fail at the same direct construction of `std::filesystem::path` from a
   `runtime::simir::InternedString` in `src/api/api_session.cpp`; the reported
   `ranges::find_if` constraint failure is a cascade from that invalid lambda.
   The source comparison now converts both UTF-8 spellings through
   `fsim::support::path_from_utf8` before comparing filenames.
9. The same audit found a latent libc++ failure in the VHDL VITAL memory-file
   elaboration path, where `frontend::physical_source` returns a
   `std::string_view` that was passed directly to `std::filesystem::path`.
   Both the decoded load-file spelling and its physical source now use the
   explicit UTF-8-to-native path boundary. Exact-LLVM Debug and no-LLVM
   ASan/UBSan builds of the API and affected application targets pass with
   eight workers; `fsim.api`, `fsim.api.abi`, and
   `fsim.application.vhdl_vital_delays` pass 3/3 in both trees. The source-line,
   schema-producer, Windows-LLVM, and resource-portability contracts pass 4/4.
   Push this repair and require a new exact-SHA hosted run before declaring the
   Windows matrix or Batch 180 Change 20 green.
10. GitHub Actions `34088983562` at exact development SHA
    `0687fb270b34ba3a2a792589f3ef4e787705c982` confirms that all four
    Windows LLVM-MinGW configurations compile the complete production tree and
    reach application-test compilation. Each then reports the same sole error:
    `application_test_non_project_cli.cpp` constructs a filesystem path
    directly from a `frontend::SourceName`. That assertion now uses the
    explicit UTF-8 path helper. A graph-backed audit also converted the four
    remaining test boundaries that supplied SimIR `InternedString` or frontend
    `std::string_view` values directly to `std::filesystem::path`; no equivalent
    production or test pattern remains. Exact-LLVM Debug and no-LLVM ASan/UBSan
    rebuilds pass with eight workers, and the frontend, non-project CLI,
    call-safe-point, and assertion tests pass 4/4 in each tree. A new hosted run
    is still required before Windows can be declared green.
11. The complete no-LLVM Clang 22 ASan/UBSan suite on this final repair set
    passes 361/361 in 9:16.06 at 2,315,144 KiB peak RSS with zero swaps and
    local leak detection disabled only for the documented ptrace-supervisor
    constraint. Its retained transcript has no compiler-warning, failed-build,
    ASan, UBSan, runtime-error, segmentation, or nonzero-exit markers. Per the
    user's closure direction, this sanitizer result carries the earlier clean
    Debug and Release qualifications. The source-line, schema-producer,
    Windows-LLVM, and resource-portability contracts again pass 4/4, and
    `git diff --check` is clean.
12. GitHub Actions `34092602473` at exact development SHA
    `5b2734514c92ac5224d20d49482d6c64837f76bc` advances all four Windows
    LLVM-MinGW configurations through the production tree. The retained log
    from every Debug/Release and LLVM ON/OFF lane reports the same sole
    application-test compiler error: a third assertion in
    `assertion_application_test.cpp` constructs `std::filesystem::path`
    directly from a SimIR `InternedString`. This site was split across lines
    and escaped the earlier line-oriented audit. It now converts the explicit
    UTF-8 spelling through `fsim::support::path_from_utf8`, and a multiline
    production-and-test audit finds no remaining direct filesystem construction
    from source-name, physical-source, or SimIR source-path wrappers. The
    LLVM-enabled Clang 22 Release merged application-test target builds with
    eight workers and `fsim.application.assertions` passes; the corresponding
    no-LLVM ASan/UBSan target and focused test also pass. Push this repair and
    require a new exact-SHA hosted run before declaring Windows or Change 20
    green.
13. GitHub Actions `34095767906` at exact development SHA
    `b6ca6811266868636dde2767acaae405882bac1f` proves the source-portability
    repair across the complete hosted matrix. Both Windows Debug lanes are
    green; Windows Release passes 352/352 tests without LLVM and 356/356 with
    LLVM; all four Linux Clang Debug/Release lanes and frontend fuzz are green.
    Both Windows Release jobs fail only after their green regressions and
    deterministic archive creation because the install audit still supplies
    the historical v2 exact-test counts 302/303. The current v3 hosted
    expectations are now 352/356 in both the workflow matrix and the Windows
    package-definition policy freeze. The Windows package, Windows LLVM,
    source-line, and resource-portability contracts pass 4/4 locally. Push the
    count repair and require one new exact-SHA hosted run; no compiler or test
    failure from `34095767906` remains to diagnose.
14. GitHub Actions `34102798801` at exact development SHA
    `9e179ce5d9656580e3781c3d161fdbf88503c525` proves both corrected Release
    test inventories: LLVM OFF passes 352/352 and LLVM ON passes 356/356. Both
    deterministic archives contain exactly 1,247 entries, and both install
    audits advance beyond the test-count check before stopping only at the
    historical hard-coded 542-entry archive freeze. The workflow now supplies
    an explicit `expected_archive_entries: 1247` for each Release lane, and the
    install audit validates that required numeric input rather than embedding
    the old release count. The same run's Debug/LLVM-ON lane fails before
    configuration because the MSYS2 mirror transfers the pinned LLVM tools
    package too slowly for ten seconds; the identical Release/LLVM-ON package
    installation succeeds. The pinned idempotent `pacman -U --needed` operation
    now receives three bounded retries, followed by the unchanged exact
    LLVM-version and `lli` validation. Workflow YAML parsing and the v2 release
    record, Windows package, Windows LLVM, source-line, and resource-portability
    contracts pass 5/5 locally. Push this repair and require a new exact-SHA
    hosted run before closing Change 20.
15. GitHub Actions `34107242568` at exact development SHA
    `229526933d96e64fbb3a572085f10018969ff65c` passes both LLVM package
    installation steps and proves the bounded retry does not disturb the
    pinned toolchain. Its Windows Release/LLVM-OFF lane again passes 352/352,
    creates the 1,247-entry deterministic archive, and advances beyond both
    corrected numeric inventory checks. The install audit then reports an
    empty example inventory even though direct inspection of the published
    archive proves the exact six expected example roots are present. A local
    reproduction confirms the cause: with a relative Change 12 work directory,
    script-mode archive extraction and globbing resolve against different
    bases; the same archive passes the inventory check when the work directory
    is absolute. The audit now normalizes `FSIM_CHANGE12_WORK_DIR` once against
    `CMAKE_CURRENT_BINARY_DIR` after validating its inputs. Repeating the
    relative-path reproduction finds all examples and advances to Windows
    executable validation, which expectedly cannot execute on the Linux host.
    The five local release/package/Windows/resource gates, workflow YAML parse,
    and `git diff --check` pass. Push this repair and require the next exact-SHA
    Windows Release audits to complete their native version, alias, metadata,
    and uninstall checks.
16. GitHub Actions `34111302404` at exact development SHA
    `1faef3d2088cb4bc727af4369b68d4010589dc21` proves the relative-work-directory
    repair: Windows Release/LLVM-OFF passes 352/352 tests, creates its exact
    1,247-entry archive, and completes the native install audit and artifact
    uploads. Both Windows Debug lanes also pass in full, as do every Linux
    Clang lane and frontend fuzz. Windows Release/LLVM-ON alone exposes two
    related cache-lock failures: one of four concurrent `fsim.cache` publishers
    exhausts the former 505 ms `MoveFileExW` sharing-lock retry, and
    `fsim.application.vpi` later waits until its 1,500-second CTest timeout.
    The lock destructor previously ignored failure to delete `owner`; if a
    Windows scanner retained a non-delete-sharing handle, that record continued
    to identify the still-live fsim process and a later same-process cache user
    could wait forever. Active lock tokens are now registered before owner
    publication and unregistered only after the destructor has stopped touching
    the canonical path, allowing an abandoned same-process owner to be safely
    quarantined while preserving active-lock exclusion. The Windows atomic
    replacement retry remains bounded but is extended to five seconds. A
    Windows-only regression deliberately retains an owner handle without delete
    sharing across destruction and proves a later contender reclaims the lock.
    On Linux, 200 paired Release repetitions of `fsim.cache` and
    `fsim.application.vpi` pass, the focused ASan/UBSan pair passes with the
    documented local leak-detection exception, and the five source-line,
    release-record, Windows-package, Windows-LLVM, and resource-portability
    gates pass. Build and test the Windows regression in a new exact-SHA hosted
    run before declaring the matrix or Batch 180 Change 20 green.
17. GitHub Actions `34117679982` at exact development SHA
    `b8455692c69c7aeb71545e4e9684d2b5112e43e1` proves the cache-lock repair and
    completes the hosted Batch 180 closure. Windows Release/LLVM-ON passes
    356/356 tests, including `fsim.cache` in 0.61 seconds and
    `fsim.application.vpi` in 0.76 seconds, then creates the deterministic
    1,247-entry archive, completes the native install audit, and uploads both
    artifacts. Windows Release/LLVM-OFF passes 352/352; both Windows Debug
    lanes pass their complete 356/352 inventories. All four retained Windows
    logs contain no compiler warning, assertion, timeout, sanitizer finding,
    or unexpected error; the only failed-probe spelling is CMake's expected
    Windows pthread capability test. All four Linux Clang 22 Debug/Release
    lanes, with and without LLVM 22.1.8, also pass, and their retained logs are
    clean under the same audit. The frontend fuzz lane completes 20,000 runs.
    Together with the complete local 361/361 ASan/UBSan result recorded above,
    the user-directed sanitizer carry-forward for Debug/Release, and the clean
    five-gate policy audit, the exact nine-lane run closes Batch 180 Change 20.

## Batch 181 active checkpoint - after Change 1

1. Resume in the fsim repository on `codex/v3`. Batch 180 is integrated and
   pushed at `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`; its exact GitHub Actions
   run `34124043615` is green across all nine Linux, Windows, and fuzz lanes.
   Batch 181 Change 1 is complete and intentionally uncommitted. Preserve its
   accumulated worktree without reset, commit, or push.
2. Change 1 adds `tests/feature_matrix/legacy_tf_inventory.tsv` and
   `cmake/CheckLegacyTfInventory.cmake`, registers
   `fsim.legacy-tf-inventory`, documents the ledger in the feature-matrix
   README, extends the resource-portability contract, and advances the source
   package manifest. It changes no product implementation, public ABI/header,
   plugin loader, manifest schema, object, design, checkpoint, or cache format.
3. The independently authored ledger has eighteen unique active IEEE 1364-2005
   rows assigned one-to-one to Changes 2-19. It covers the v3 native-plugin ABI,
   standard header and platform link surfaces, registration and descriptor
   validation, task/function and `misctf` callbacks, argument/value/parameter/
   time/scope access, output and synchronization controls, HDL registration,
   scheduler coordination, failure containment, and Linux/Windows plugin proof.
4. Every row applies to `V1995`, `V2001`, `V2001NoConfig`, `V2005`, `SV2005`,
   `SV2009`, `SV2012`, and `SV2017`, and carries the exact
   `ieee-only-no-vendor-extensions` policy. The v3 surface directly rejects v2
   plugin ABI inputs and admits no compatibility reader, migration, vendor
   extension, or private-reference material. The normalized ledger SHA-256 is
   `2d730b840eb80307e6a025317c4447ad682335e9fc6145ffcd8a5842cdc4f39b`.
5. The validator freezes the twenty-batch roadmap and exact twenty-change Batch
   181 allocation; all row IDs, domains, assigned changes, standard/profile
   sets, safe relative owners, shared diagnostic/resource owners, active state,
   and extension policy; and forbidden private paths and named vendor/tool
   spellings. The source-package manifest now contains 1,676 ordered paths at
   SHA-256
   `4057a4fff165a11f363543342891c34f4f294ff02bd3e0076b1b6109f4399985`.
6. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug tree reconfigures and
   builds 1,328/1,328 actions with eight workers without a warning or error.
   The legacy-TF inventory, diagnostic catalog, source-line budget,
   source-package manifest, resource-portability contract, and CTest command
   uniqueness gates pass 6/6 in 15.93 seconds. No clean-first, Release,
   sanitizer, hosted-CI trigger, commit, or push action ran for this change.
7. At the user's request, the newest failed Windows log was inspected before
   any next commit. GitHub Actions `34111302404` failed only in Windows
   Release/LLVM-ON: `fsim.cache` aborted after a `MoveFileExW` sharing-lock
   retry and `fsim.application.vpi` then reached its 1,500-second timeout while
   reusing the same-process O2 cache. The current integrated tip contains the
   active-lock-token recovery, five-second bounded replacement retry, and a
   Windows retained-owner-handle regression. Exact subsequent runs
   `34117679982` and `34124043615` pass all four Windows configurations; the
   latest four retained logs contain no compiler, linker, runtime, assertion,
   timeout, or test error. The only failed spelling is CMake's expected Windows
   pthread capability probe. Twenty paired local Debug repetitions of
   `fsim.cache` and `fsim.application.vpi` also pass in 2.85 seconds.
8. Proceed only to Batch 181 Change 2: define the direct v3 native-plugin ABI
   and common loader metadata. Reuse the existing VPI, DPI, and VHPI loader
   seams where appropriate, but do not expose or accept a v2 ABI. Preserve the
   dirty Change 1 paths and focused warnings-as-errors Debug validation; do not
   begin Change 3, run Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 2

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1 and 2 are complete
   and uncommitted. The temporary `codex/v3-batch180-development` branch was
   deleted locally and from `origin` at the user's direction; do not recreate
   or use it.
2. Change 2 adds the direct C ABI in
   `include/fsim/runtime/native_plugin_abi.h`, the owned C++ metadata model in
   `include/fsim/runtime/native_plugin.hpp`, validation and identity logic in
   `src/runtime/native_plugin.cpp`, and independent C/C++ evidence in
   `tests/runtime/native_plugin_abi_c_test.c` and
   `tests/runtime/native_plugin_abi_test.cpp`. The focused test is registered
   as `fsim.runtime.native_plugin_abi` and the resource contract owns all five
   paths.
3. The only discovery symbol is
   `fsim_native_plugin_descriptor_v3_get`. The append-only descriptor requires
   ABI version 3, at least the exact v3 prefix size, native pointer width, zero
   reserved flags, and a nonempty known capability mask. Current known bits are
   IEEE TF and ACC so the shared metadata boundary can serve both legacy PLI
   batches without silently admitting an unrelated extension family.
4. Required name, version, and producer strings and optional build identity
   are byte-counted and bounded. Embedded NUL/control bytes, inconsistent
   null/size pairs, and oversize values are invalid. They are metadata, never
   filesystem paths. A successful copy owns all storage independently of the
   image and derives a stable SHA-256 identity from a format marker, the exact
   capability mask, explicit little-endian lengths, and field bytes.
5. Exact version 2 and unknown future versions are rejected before a result is
   published. Truncated descriptors, foreign pointer width, reserved flags,
   zero/unknown capabilities, malformed required text, and ambiguous optional
   build identity likewise return their exact error with no partial metadata.
   Registration-table discovery, descriptor callbacks, dynamic-library open,
   and lifecycle execution deliberately remain Changes 5-8 and were not
   pulled into this ABI-model slice.
6. The legacy-TF ledger now has seventeen active and one preserved row. Its
   normalized SHA-256 is
   `95f1b626bf0aa3b1cebc96b11fe54486dc0943e250bb14d0aef79db5c94e3ec4`.
   Five new paths advance the source-package manifest to 1,681 ordered entries
   at SHA-256
   `06a28905682e69c37bde451855a436097232329ba8862911b6f85aaa2a091217`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug target builds 9/9
   actions with eight workers. `fsim.runtime.native_plugin_abi` passes, and the
   ABI test plus legacy inventory, diagnostic catalog, source-line budget,
   source-package manifest, resource-portability, and CTest-uniqueness slice
   passes 7/7 in 6.58 seconds. `git diff --check` is clean. The prior requested
   Windows failure audit and its two later all-green exact-SHA runs remain
   recorded in the Change 1 checkpoint above.
8. Proceed only to Batch 181 Change 3: provide the standard-compatible
   `veriuser.h` declarations and constants. Preserve the v3-only common ABI and
   dirty Changes 1-2; do not implement platform import libraries, registration
   discovery, callbacks, Release/sanitizer/hosted qualification, commit, or
   push.

## Batch 181 active checkpoint - after Change 3

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-3 are complete and
   uncommitted. Preserve all current Batch 181 files and do not recreate the
   deleted `codex/v3-batch180-development` branch.
2. Change 3 adds the independently authored public TF header at
   `include/fsim/runtime/veriuser.h`. It exposes the standardized PLI scalar
   aliases; error, reason, argument, and node constants; vector, strength,
   expression, and node records; simulator service declarations; and the
   legacy version/end-of-compile globals. The declarations have C linkage in
   C++ and retain the source-level Windows import/export annotations that
   Change 4 will turn into actual shared/import-library surfaces.
3. The historical `bool`, `true`, and `false` aliases remain defined for C
   unless `PLI_EXTRAS` is predeclared, but are suppressed in C++ so the header
   is warning-clean under Clang 22 warnings-as-errors. Uppercase `TRUE` and
   `FALSE` plus `null` remain available in both languages. This is the only
   intentional modernization of the source compatibility layer.
4. Change 3 does not define a registration record or discovery global. No
   vendor task/function kinds, aliases, or registration tables were admitted;
   the exact governed discovery surface remains Change 5 after Change 4
   provides the platform link boundary.
5. `tests/runtime/veriuser_abi_c_test.c` and
   `tests/runtime/veriuser_abi_test.cpp` independently prove C11/C++ inclusion,
   scalar widths, representative layout offsets, constant aliases, C linkage,
   implicit-instance and explicit-instance routine signatures, and the C++
   keyword shield. The focused test is `fsim.runtime.veriuser_abi`.
6. The legacy-TF ledger now has sixteen active and two preserved rows. Its
   normalized SHA-256 is
   `2aedbbd128ecc5cd3d2dace00665a792db1d9f4a6eb95a79fc07dde6b9ee8e7b`.
   Three new paths advance the source-package manifest to 1,684 ordered entries
   at SHA-256
   `800e014211c8f59e08c3b262a8fc0e25f8c86472a3295c3bbb592e15f53e9af2`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug target builds 5/5
   actions with eight workers. The veriuser ABI, native plug-in ABI, legacy-TF
   inventory, diagnostic catalog, source-line budget, source-package manifest,
   resource-portability, and CTest-uniqueness slice passes 8/8 in 8.51 seconds.
   Direct inventory/resource checks and `git diff --check` must remain clean.
8. Proceed only to Batch 181 Change 4: provide Linux shared-library and Windows
   import-library link surfaces for the standardized TF declarations and direct
   v3 native plug-in metadata boundary. Do not begin registration discovery,
   callback execution, Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 4

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-4 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit, push, or
   recreate the deleted Batch 180 development branch before Change 20.
2. Change 4 adds `fsim_tf` with alias `fsim::tf`, configured by
   `cmake/FsimNativePlugin.cmake` and implemented by
   `src/runtime/tf_link.cpp`. It is a C-linkage ABI shim with ABI-major soname 3,
   explicit default Linux visibility, and Windows export annotations so the
   shared target creates the import library consumed by LLVM-MinGW plug-ins.
3. The link library exposes all 107 simulator-owned TF routine names. It does
   not expose `err_intercept`, `veriuser_version_str`, or
   `endofcompile_routines`, because those are plug-in-owned definitions, and it
   does not expose vendor names such as `vpi_printf`. Current entry points are
   neutral outside a registered call context; Changes 7-15 replace each service
   group with governed simulator behavior without changing symbol addresses.
4. `fsim_tf` is explicitly excluded from the SystemC process-runtime target
   inventory because it contains no simulator state and must not preload a
   SystemC runtime in a native plug-in process. Simulation executables and
   runtime libraries retain the existing one-runtime rule. This keeps the TF
   link/import surface dependency-minimal and independently relocatable.
5. `tests/runtime/tf_link_probe_plugin.c` is a real C shared plug-in linked to
   `fsim_tf`; it publishes only the direct v3 TF metadata descriptor and calls
   representative implicit/explicit-instance, expression/node, time, and
   synchronization symbols. `tests/runtime/tf_plugin_link_test.cpp` loads both
   images, verifies all 107 standard exports and the forbidden-symbol boundary,
   validates descriptor identity, and executes the plug-in probe. The target is
   `fsim.runtime.tf_plugin_link`; the build copies the host DLL beside the test
   plug-in on Windows so safe loader search does not depend on `PATH`.
6. Installation owns `fsim_tf`, its Windows import library, `veriuser.h`, and
   `native_plugin_abi.h`. `fsimConfig.cmake` publishes the relocatable imported
   target `fsim::tf`; the installed-public and binary-ownership contracts now
   require the new artifacts, compare both header digests, and build/run the
   offline C consumer in `tests/runtime/installed_tf_consumer`.
7. The legacy-TF ledger now has fifteen active and three preserved rows. Its
   normalized SHA-256 is
   `253c2adfa50844078a5fc6a7f6bba29724f37a4bece6ab856c73dfef6962196a`.
   Six new paths advance the source-package manifest to 1,690 ordered entries
   at SHA-256
   `08619c4452ddd5b642a0476b6f34001a564c55413a6e8f0e75ac98bd1e4ded95`.
8. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The three ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 12/12 in 15.39 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   must remain clean.
9. Proceed only to Batch 181 Change 5: discover and validate the standardized TF
   registration-table surface transactionally. Preserve the direct v3 metadata
   boundary and the dependency-minimal link shim; do not begin callback
   execution semantics, Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 5

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-5 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit, push, or
   recreate the deleted Batch 180 development branch before Change 20.
2. Change 5 extends `fsim_native_plugin_descriptor_v3` after its frozen base
   prefix with a bounded array of capability-interface records. The common
   Change 2 validator accepts the base prefix and never reads the extension;
   the TF-specific loader requires the full extension, one through eight
   aligned records, and exactly one version-3 TF interface. This is append-only
   evolution of the direct v3 ABI, not a v2 reader or migration.
3. `include/fsim/runtime/tf_plugin_abi.h` defines the portable C registration
   record and table header. The table must contain 1-4,096 records, use a stride
   between the record size and 4,096 bytes, keep all reserved fields zero, and
   carry exact ABI version 3. Entry contents intentionally remain unvalidated
   until Change 6. Obsolete registration structures, vendor startup arrays,
   and alternate bootstrap symbols are excluded.
4. `src/runtime/tf_plugin.cpp` opens the artifact through the existing safe
   platform abstraction, resolves only
   `fsim_native_plugin_descriptor_v3_get`, contains descriptor exceptions,
   validates the common descriptor plus the entire TF discovery graph, copies
   common metadata, and retains the library in `TfLoadedPlugin`. Every failure
   returns a typed error and no partially published object.
5. `tests/runtime/tf_link_probe_plugin.c` now publishes one real direct-v3 TF
   table. `tests/runtime/tf_plugin_test.cpp` covers successful discovery and
   ownership plus absent artifacts/symbols, old or malformed table headers,
   missing/truncated/oversized/null interface arrays, malformed interface
   records, absent or duplicate TF interfaces, incompatible interface versions,
   malformed registration tables, and absent declared capability. It does not
   execute callbacks.
6. The SDK install now owns and byte-compares `tf_plugin_abi.h`; the offline C
   consumer includes and instantiates its table type. The legacy-TF ledger has
   fourteen active and four preserved rows at normalized SHA-256
   `b77028d80b9a1a2c18c81a953ae410005699f330455bfe2067ece8ab71d92f0a`.
   Four new paths advance the source-package manifest to 1,694 entries at
   SHA-256
   `0dd0259569a866a76678bfb67f311936d9aa6dc47e96c7aa413e6af19c2958d0`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The four ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 13/13 in 15.34 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   must remain clean.
8. Proceed only to Batch 181 Change 6: validate every task/function registration
   descriptor transactionally before publishing any callable registration.
   Preserve the exact discovery boundary and loaded-library ownership; do not
   begin callback execution semantics, Release/sanitizer/hosted qualification,
   commit, or push.

## Batch 181 active checkpoint - after Change 6

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-6 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit, push, or
   recreate the deleted Batch 180 development branch before Change 20.
2. Change 6 adds `include/fsim/runtime/tf_registration.hpp` and
   `src/runtime/tf_registration.cpp`. `validate_and_copy_tf_registrations`
   validates the table header again, reserves bounded temporary storage, walks
   entries only by the validated stride, and returns either the full host-owned
   vector or an empty result with an exact error and failing index.
3. Each entry must fit inside its stride, select task, integral-function, or
   real-function kind, keep flags/reserved fields zero, and use a unique
   2-255-byte `$identifier` with an alphabetic/underscore first identifier
   character. `calltf` is mandatory; `sizetf` is mandatory only for integral
   functions and forbidden for tasks and real functions. `checktf` and `misctf`
   are optional. User data and callback addresses are copied but never invoked.
4. `TfLoadedPlugin` retains the loaded image and now exposes the immutable
   copied registration vector separately from its table summary. The loader
   constructs this vector before allocating or publishing the final loaded
   object, so a bad later descriptor, duplicate name, or allocation failure
   cannot leave a partial callable set.
5. `tests/runtime/tf_registration_test.cpp` proves all three profiles, stable
   source ordering, copied names/metadata, header rejection, low/high kinds,
   flags and reserved state, required/forbidden callbacks, null/short/long/
   malformed names, duplicate names, exact failing indexes, empty rollback,
   and zero callback execution. The real plug-in loader test also verifies its
   one copied registration.
6. The legacy-TF ledger has thirteen active and five preserved rows at
   normalized SHA-256
   `060dbc67968336a8a08b4dd2453ee067ec62fd70861c8856c106a4e718aaf731`.
   Three new paths advance the source-package manifest to 1,697 entries at
   SHA-256
   `dd1c2f01e6144977404831eb20973397fcf6bdf835ed67c1c2ce9ad2e16e8981`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The five ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 14/14 in 15.25 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   remain clean.
8. Proceed only to Batch 181 Change 7: implement `checktf`, `sizetf`, and
   `calltf` invocation plus task/function result ownership and failure
   containment. Preserve transactional registration and library lifetime; do
   not begin `misctf` lifecycle scheduling, Release/sanitizer/hosted
   qualification, commit, or push.

## Batch 181 active checkpoint - after Change 7

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-7 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit, push, or
   recreate the deleted Batch 180 development branch before Change 20.
2. Change 7 corrects the direct-v3 registration ABI to use typed two-argument
   `fsim_tf_routine_v3` callbacks for `checktf`, `sizetf`, and `calltf`, and the
   distinct three-argument `fsim_tf_misc_routine_v3` only for `misctf`. This
   removes ambiguous lifecycle parameters before callback execution is exposed.
3. `include/fsim/runtime/tf_call.hpp` and `src/runtime/tf_call.cpp` implement
   binding and invocation. Binding calls optional `checktf`, calls `sizetf`
   exactly once for integral functions, bounds results to 1-1,048,576 bits,
   copies the registration, and retains the owning loaded-plugin implementation.
   Tasks use width zero and real functions use width 64.
4. Every invocation creates fresh result storage before entering plug-in code.
   Integral results own `aval`/`bval` arrays sized to the declared width; real
   results own a double slot; tasks own no slot. The result-only bridge in
   `tf_call_bridge.h` and `fsim_tf` permits parameter zero writes through
   `tf_putp`, `tf_putlongp`, and `tf_putrealp`, clears the slot before use,
   masks unused high bits, rejects wrong kinds/indexes, and leaves all general
   argument/value access for Change 10.
5. Only `sizetf` has a semantic return value. `checktf` and `calltf` return
   values are retained diagnostically but are not treated as status codes.
   Invalid sizes, missing function results, callback exceptions, allocation
   failures, bad registration indexes, and a nested attempt to replace the
   active call context return typed failures. Every exception path leaves the
   bridge before returning. `misctf` is neither invoked nor scheduled here.
6. The C probe now publishes one task and one 17-bit integral function. The
   call test proves exact check/size/call reasons, task/integral/65-bit/real
   result ownership, fresh per-call storage, callback-return handling,
   exception containment, invalid or missing result writes, nested-call
   rejection, index bounds, and plug-in lifetime retention after the loader
   handle is released.
7. The legacy-TF ledger has twelve active and six preserved rows at normalized
   SHA-256
   `fd13fb893711202a53d4eecd9bebe56ce2eab7699bb9cd9b835bede1b49f90a1`.
   Four new paths advance the source-package manifest to 1,701 entries at
   SHA-256
   `7f714b53952211b9482fadf49fd262113451e743664a4347d2bcc3cc1f7b0bcf`.
8. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The six ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 15/15 in 15.19 seconds. The
   dependency audit confirms `fsim_tf` still has no SystemC runtime dependency;
   direct inventory/resource/source-manifest/source-line checks and
   `git diff --check` remain clean.
9. Proceed only to Batch 181 Change 8: implement `misctf` lifecycle and
   synchronization reasons with deterministic ordering. Preserve result-slot
   containment and do not begin general argument/value services,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 8

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-8 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit, push, or
   recreate the deleted Batch 180 development branch before Change 20.
2. Change 8 adds `include/fsim/runtime/tf_misc.hpp` and
   `src/runtime/tf_misc.cpp`. `TfMiscDispatcher` copies only registrations with
   a `misctf` callback, preserves source-table indexes/order, and shares its
   loaded-plugin owner so callback addresses remain live after the originating
   loader handle is released.
3. The typed `TfMiscReason` covers all eighteen lifecycle reasons: save,
   restart, disable, parameter value/driver changes, read-write/read-only
   synchronization, finish, reactivate, end-of-compile, scope, interactive,
   reset/end-of-reset, force/release, and start-of-save/start-of-restart.
   `checktf`, `sizetf`, `calltf`, gaps, and unknown integers are rejected.
4. The third callback parameter is positive only for parameter value/driver
   changes and zero for all other reasons. Callback returns have no control
   meaning and are ignored, though the last is retained diagnostically.
   Exceptions return the exact failing registration and completed prefix;
   recursive dispatch is rejected before entering a nested callback.
5. This is a lifecycle dispatcher, not yet a scheduler registration service.
   In particular it distinguishes `reason_synch` from `reason_rosynch`, but
   `tf_synchronize`/`tf_rosynchronize` do not enqueue them until Change 15.
6. `tests/runtime/tf_misc_test.cpp` exercises every reason, parameter class,
   exact user-data and source order, ignored nonzero returns, exception prefix,
   recursive rejection, invalid reasons/parameters, an empty callback set, and
   retained-image dispatch through both C plug-in registrations.
7. The legacy-TF ledger has eleven active and seven preserved rows at normalized
   SHA-256
   `966d73a71f0b9748cf4900c467c982e66f194fafdc5e0b73fe6c0aa96e48696a`.
   Three new paths advance the source-package manifest to 1,704 entries at
   SHA-256
   `b55af8a4da9d3db069f87615a7aa5c4b0bde530ab6bed748910758176cd5fb2d`.
8. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The seven ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 16/16 in 15.00 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   remain clean.
9. Proceed only to Batch 181 Change 9: implement argument count, type,
   direction, expression, and width inspection against an active call context.
   Preserve callback return semantics and do not begin general value mutation,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 9

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-9 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit, push, or
   recreate the deleted Batch 180 development branch before Change 20.
2. Change 9 adds `include/fsim/runtime/tf_argument.hpp` and
   `src/runtime/tf_argument.cpp`. The language-neutral argument model owns its
   expression text and records kind, direction, width, signedness, and select
   bounds. Validation is transactional and bounded to 4,096 arguments,
   1,048,576 bits, and 4,096 expression bytes; kind-specific inconsistencies,
   invalid selections, empty/control-bearing expressions, and oversized input
   return an exact argument index with no callbacks run.
3. `TfBoundCall` now copies the validated argument inventory at binding and
   presents the same immutable metadata during `checktf`, `sizetf`, and
   `calltf`. The internal call-context bridge was renamed from a result-only
   bridge and now carries both the existing simulator-owned function result and
   immutable argument records without exporting C++ ownership across the ABI.
4. `fsim_tf` implements current-context `tf_nump`, `tf_typep`, `tf_sizep`, and
   `tf_exprinfo`. All return neutral values outside a callback or for invalid
   indexes. `tf_exprinfo` exposes only structural metadata in this change;
   expression values remain Change 10, and explicit-instance variants remain
   deliberate stubs until Change 11.
5. `tests/runtime/tf_argument_test.cpp` proves every argument profile, exact
   count/type/width/expression/select/sign/direction metadata across all three
   callback phases, neutral out-of-context and explicit-instance behavior, all
   validation bounds and kind errors, rollback, and zero callback execution on
   invalid binding.
6. The legacy-TF ledger has ten active and eight preserved rows at normalized
   SHA-256
   `f89e27fd48e8e86d87c8831bf41ea6e6e70f41391355877f496a0566ae8c3ea8`.
   Three new paths advance the source-package manifest to 1,707 entries at
   SHA-256
   `1e7b471739bbbef4b8a318a8f2070d64b3e7a395f63550932b60d794b394df01`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The eight ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 17/17 in 15.25 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   remain clean.
8. Proceed only to Batch 181 Change 10: implement integer, real, string, vector,
   and expression value access with fresh per-invocation value ownership.
   Preserve immutable argument metadata and function-result containment; do not
   begin explicit-instance access, Release/sanitizer/hosted qualification,
   commit, or push.

## Batch 181 active checkpoint - after Change 10

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-10 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit, push, or
   recreate the deleted Batch 180 development branch before Change 20.
2. Change 10 adds `include/fsim/runtime/tf_value.hpp` and
   `src/runtime/tf_value.cpp`. `TfArgumentValue` is separate from immutable
   `TfArgument` metadata and owns null, exact-width interleaved `aval`/`bval`,
   real, or string storage. Validation requires exact argument count and kind,
   exact vector group count, clear unused high bits, bounded NUL-free strings,
   and no more than 16 MiB aggregate storage per call.
3. `TfBoundCall::invoke(values)` validates and copies input values before
   entering plug-in code. The legacy no-argument overload builds bounded zero
   values without a second whole-container snapshot. Callback execution uses a
   private working copy and returns only a source-ordered list of explicitly
   assigned read-write parameters; function results and parameter updates are
   discarded together on callback failure, missing function assignment, or
   allocation failure.
4. The internal C bridge now carries per-invocation value views. `fsim_tf`
   implements `tf_getp`, `tf_getlongp`, `tf_getrealp`, `tf_getcstringp`, and
   `tf_strgetp`; `tf_exprinfo` exposes full vector words, real values, or string
   contents during `calltf`. `tf_putp`, `tf_putlongp`, `tf_putrealp`, and
   `tf_propagatep` accept only read-write arguments, retain X/Z pairs, and mask
   unused high bits before an update is published. All value APIs are neutral
   outside an active call. Delayed writes and explicit-instance variants remain
   later changes.
5. `tests/runtime/tf_value_test.cpp` proves supplied and default values,
   32/64-bit scalar reads, 65-bit vector access, binary/hex projections,
   distinct unknown/high-impedance bits, string and real reads, expression
   propagation, integral and real write-back order, read-only rejection,
   count/kind/group/high-bit/string failures, callback non-entry, atomic result
   publication, and the aggregate resource ceiling.
6. The legacy-TF ledger has nine active and nine preserved rows at normalized
   SHA-256
   `de6827fe470bf619055174387bafcd357db4d550608a1b35abf64d92a8977e47`.
   Three new paths advance the source-package manifest to 1,710 entries at
   SHA-256
   `adf8ce8ac8e73a1ee11188ccb8a901fd0e611cbb1fade8dbadd680f2f686caa4`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The nine ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 18/18 in 15.16 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   remain clean.
8. Proceed only to Batch 181 Change 11: implement parameter and
   instance-specific access with generation-qualified instance identity.
   Preserve transactional value updates and do not begin time/delay services,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 11

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-11 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 11 adds `include/fsim/runtime/tf_instance.hpp` and
   `src/runtime/tf_instance.cpp`. `TfInstanceIdentity` requires nonzero design,
   hierarchy, and generation components. `TfBoundCall` owns that identity and
   a direct-v3 bridge token stable across check, size, and call callbacks;
   `TfLoadedPlugin::bind` accepts the same optional identity.
3. `tf_getinstance` exposes the token only while its call is active. Explicit
   `tf_i*` count, type, size, scalar/long/real/string value, expression,
   node-info, evaluation, propagation, and immediate-write routines first
   compare the supplied opaque pointer with the active token. Null, stale, and
   other-generation tokens are rejected without dereference. Time, delay,
   synchronization, scope-name, and work-area variants remain later changes.
4. Current and explicit `tf_nodeinfo` derive a bounded node profile from the
   validated argument kind, width, signedness, selection, and expression, and
   expose the same invocation-owned vector or real value storage. No hierarchy
   handle or scope lifetime is invented in this change.
5. `tests/runtime/tf_instance_test.cpp` binds two generations of one hierarchy,
   observes distinct stable tokens in `checktf` and `calltf`, proves exact-token
   explicit reads, expression/node access and write-back, rejects the other
   generation, verifies neutral out-of-context behavior, and proves invalid
   zero identity fields prevent all callbacks.
6. The legacy-TF ledger has eight active and ten preserved rows at normalized
   SHA-256
   `a324e8194e24d48fb4004699c0de43bd5a8cf56de25535d34cded212b69bc234`.
   Three new paths advance the source-package manifest to 1,713 entries at
   SHA-256
   `0b201c95438067e6acbdb16a586a91c7e0c67ae29aaf8ff99524d1986c2d82c9`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The ten ABI/runtime tests, six inventory/policy gates,
   and three install/consumer gates pass 19/19 in 15.17 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   remain clean.
8. Proceed only to Batch 181 Change 12: implement simulation time, delay, and
   timescale access with exact bounded conversions tied to the active instance.
   Preserve generation isolation and do not begin scope/work-area lifetimes,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 12

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-12 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 12 adds `include/fsim/runtime/tf_time.hpp` and
   `src/runtime/tf_time.cpp`. `TfTimeProfile` validates unit/precision
   exponents in the common `10^0` through `10^-15` range, precision no coarser
   than the instance unit, and a nonzero scheduler-tick multiplier. Integer
   conversion is overflow checked and half-up rounded; real delay conversion
   additionally rejects negative and non-finite inputs.
3. `TfBoundCall` owns the profile and accepts a per-invocation `TfTimeState`
   snapshot. The internal direct-v3 bridge exposes current time, optional next
   event time, and a fixed 256-entry delay-request buffer only during
   `calltf`. Successful calls return source-ordered `TfDelayRequest`s together
   with function and argument effects; exceptions or result failures publish
   none. The scheduler coordinator remains the sole owner of later queue
   insertion.
4. `fsim_tf` implements current and explicit-instance integer, split-word,
   real, unit, and precision queries; next-event and string queries; integer
   and real scale/unscale services; and integer, long, real, clear-all delay
   requests. Lifecycle calls are neutral, explicit calls require the exact
   active generation token, and conversion failure never appends a request.
5. `tests/runtime/tf_time_test.cpp` proves current/next queries, local-unit
   conversion, half-up rounding, scale/unscale round trips, exact-token
   rejection, absent-next-event behavior, negative/non-finite/overflow
   rejection, invalid-profile callback non-entry, the 256-request ceiling, and
   atomic rollback after a throwing callback.
6. The legacy-TF ledger has seven active and eleven preserved rows at
   normalized SHA-256
   `389d15d1736e57f24b023f4682f4e86b406fd3566614d45360a25afa845c61d5`.
   Three new paths advance the source-package manifest to 1,716 entries at
   SHA-256
   `8b7734c4e1c1e8bee0a093321eea50652a0e4e61be36591af9eb42216b00efa2`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The eleven ABI/runtime tests, six inventory/policy
   gates, and three install/consumer gates pass 20/20 in 8.27 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   remain clean.
8. Proceed only to Batch 181 Change 13: implement scope, instance, work-area,
   and user-data lifetimes. Preserve time/delay transaction ownership and do
   not begin output/control or synchronization services,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 13

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-13 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 13 adds `include/fsim/runtime/tf_context.hpp` and
   `src/runtime/tf_context.cpp`. `TfContextProfile` owns bounded canonical
   module-instance and active-scope names. Empty names, names above 4,096 bytes,
   control characters, and scopes outside the module instance are rejected
   before any callback. Binding copies both the context and registration, so
   later caller mutation cannot change names, routine identity, or user data.
3. The direct-v3 call bridge carries module, scope, routine, user-data, and
   opaque work-area state through `checktf`, `sizetf`, and `calltf`.
   `tf_mipname`, `tf_spname`, `tf_getroutine`, and their explicit-instance
   forms return thread-local copies, preventing writable legacy return types
   from aliasing simulator-owned strings. Explicit calls still require the
   exact active generation token.
4. Each `TfBoundCall` retains one opaque plug-in-owned work-area pointer.
   Current and explicit get/set APIs can carry it across lifecycle phases and
   repeated invocations; fsim never dereferences or frees it. A per-bound
   recursive mutex serializes concurrent instance access without deadlocking
   same-thread re-entry, which still fails at the existing context guard. A
   changed pointer commits only with all other callback effects, so exception,
   result, or allocation failure preserves the previous value.
5. `tests/runtime/tf_context_test.cpp` proves copied name and registration
   state, check/size/call continuity, current and explicit name/work-area APIs,
   mutation containment, null clearing, wrong-token neutrality, two-instance
   isolation, callback rollback, and all validation failures.
6. The legacy-TF ledger has six active and twelve preserved rows at normalized
   SHA-256
   `65c2bef49298399d0b9638dd8a1c6ce191c9e246d1d313856513e82d104eca48`.
   Three new paths advance the source-package manifest to 1,719 entries at
   SHA-256
   `1df9fb7d050d85e9180926a8e6a991ea69e679cbbf4daa36c8ee9395f3b26db1`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The twelve ABI/runtime tests, six inventory/policy
   gates, and three install/consumer gates pass 21/21 in 8.56 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff --check`
   remain clean.
8. Proceed only to Batch 181 Change 14: implement TF output, warning, error,
   finish, and stop controls. Preserve context and work-area transaction
   ownership and do not begin synchronization services,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 14

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-14 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 14 adds `include/fsim/runtime/tf_control.hpp`,
   `src/runtime/tf_control.cpp`, and `tests/runtime/tf_control_test.cpp`.
   `TfControlEffect` owns output/control kind, channel, severity, facility,
   message number, and text. Capture accepts at most 256 effects, 64 KiB per
   formatted record, 255 bytes per metadata field, and 1 MiB total text; the
   first validation, capacity, formatting, or allocation failure is sticky.
3. The direct-v3 bridge carries a simulator-owned capture callback.
   `io_printf`, `io_mcdprintf`, `tf_text`, `tf_warning`, `tf_error`,
   `tf_message`, `tf_dostop`, and `tf_dofinish` append ordered effects only
   while a valid callback context is active. They never write a host stream or
   mutate scheduler control directly.
4. Successful `checktf` and `sizetf` output is returned in
   `TfBindResult::control_effects`; successful `calltf` output is returned in
   `TfInvokeResult::control_effects`. Callback exceptions, unassigned function
   results, invalid message levels, formatter failures, resource-limit
   failures, and allocation failures publish no effects or other callback
   transaction state. Change 17 remains the sole owner of ordered output
   publication and stop/finish execution through the scheduler coordinator.
5. `tests/runtime/tf_control_test.cpp` proves lifecycle capture, all output and
   control APIs, channel/severity/structured-metadata preservation, distinct
   stop and finish effects, stable order, neutral out-of-context calls,
   malformed capture rejection, all resource ceilings, and complete rollback.
6. The legacy-TF ledger has five active and thirteen preserved rows at
   normalized SHA-256
   `82b33d1ee62580e8ad6df4f2681b22f8d34206c7e90a18140fabc1b3b8d2976b`.
   Three new paths advance the source-package manifest to 1,722 entries at
   SHA-256
   `bbbd28e3a2c56d819a58809724744e17992a536cc6f81c1d9d5a33175f49d517`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The thirteen ABI/runtime tests, six inventory/policy
   gates, and three install/consumer gates pass 22/22 in 8.32 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff
   --check` remain clean.
8. Proceed only to Batch 181 Change 15: implement read-only and read-write
   synchronization callbacks. Preserve transactional control capture and
   defer its publication to Change 17. Do not begin HDL registration,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 15

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-15 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 15 adds `include/fsim/runtime/tf_synchronization.hpp`,
   `src/runtime/tf_synchronization.cpp`, and
   `tests/runtime/tf_synchronization_test.cpp`. The model distinguishes
   read-write and read-only requests, maps them exactly to `reason_synch` and
   `reason_rosynch`, retains the requesting `TfInstanceIdentity`, and bounds
   every callback to 256 source-ordered requests.
3. The direct-v3 bridge adds read-write and read-only synchronization phases
   plus a fixed request buffer. `tf_synchronize`, `tf_rosynchronize`,
   `tf_isynchronize`, and `tf_irosynchronize` append requests during `calltf`
   or a read-write synchronization callback. They fail neutrally during
   lifecycle entry, outside a callback, for the wrong generation token, from
   a read-only callback, or after the fixed capacity is reached.
4. `TfBoundCall::synchronize` invokes the bound registration's `misctf` with
   the exact synchronization reason and a current argument, time, context,
   work-area, and effect transaction. Read-write callbacks may stage argument
   updates, delay requests, output/control records, and follow-up
   synchronization requests. Read-only callbacks see values and time but all
   bridge values are non-writable, and delay or synchronization requests are
   rejected. Change 17 remains the only owner of scheduler-region insertion
   and returned-effect publication.
5. Invalid synchronization kinds and missing `misctf` callbacks are rejected
   before callback entry. Exceptions discard argument, delay, control, work
   area, and synchronization effects. The existing recursive instance lock
   serializes concurrent use, while attempted callback re-entry receives
   `TfCallError::ContextBusy` and cannot replace the active bridge context.
6. The link-probe and installed C consumer now require synchronization calls
   outside an active callback to report neutral failure rather than the former
   placeholder success. `tests/runtime/tf_synchronization_test.cpp` proves
   model mapping, all four request APIs, request order and instance identity,
   both callback phases, read-only mutation rejection, capacity, rollback,
   missing-callback/invalid-kind failures, and re-entry containment.
7. The legacy-TF ledger has four active and fourteen preserved rows at
   normalized SHA-256
   `f3e64ce5ec13450770ec26d1bb70c89a23a2491a2de1061785eb6e6a0c6ae27a`.
   Three new paths advance the source-package manifest to 1,725 entries at
   SHA-256
   `c15889fef398c224eb72b00fbe5eb9182e2180068e5944d0124931520ffbbdcf`.
8. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The fourteen ABI/runtime tests, six inventory/policy
   gates, and three install/consumer gates pass 23/23 in 8.46 seconds. Direct
   inventory/resource/source-manifest/source-line checks and `git diff
   --check` remain clean.
9. Proceed only to Batch 181 Change 16: register TF system tasks/functions in
   every retained Verilog and SystemVerilog profile. Preserve the direct-v3
   callback/effect contract and do not begin scheduler coordinator wiring,
   Release/sanitizer/hosted qualification, commit, or push.

## Batch 181 active checkpoint - after Change 16

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-16 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 16 adds `include/fsim/app/application_tf.hpp`,
   `src/app/application_tf.cpp`, and
   `tests/app/tf_plugin_application_test.cpp`. `TfApplicationRegistry` owns
   every loaded `TfLoadedPlugin` so its direct-v3 image and callback pointers
   remain valid, and publishes copied name/kind/index metadata only after the
   complete load transaction succeeds.
3. The registry exposes every validated TF task and integer/real function to
   Verilog 1995, 2001, 2001-no-configurations, and 2005 plus SystemVerilog
   2005, 2009, 2012, and 2017. VHDL 1987 through 2008 are rejected explicitly.
   Resolution uses exact source spelling and optional callable-kind checking;
   binding delegates to the existing instance/time/context-aware plug-in bind
   transaction.
4. One application is bounded to 256 loaded plug-ins and 65,536 registrations.
   Missing artifacts, invalid plug-ins, cross-plug-in name collisions,
   unsupported profiles, missing names, wrong callable kinds, and allocation
   failures leave the published registry unchanged. A second load of the real
   probe plug-in proves collision rollback without changing either count.
5. `tests/app/tf_plugin_application_test.cpp` loads the independently authored
   C probe plug-in, resolves its task and 17-bit function across all eight
   retained profiles, rejects all five VHDL profiles, binds and invokes both
   callable kinds, verifies the function result, and proves exact-name,
   wrong-kind, duplicate, and missing-artifact behavior. Change 17 retains
   ownership of HDL-call lowering, scheduler serialization, and effect
   publication.
6. The legacy-TF ledger has three active and fifteen preserved rows at
   normalized SHA-256
   `b9cef58642320c9ced4e9a77d4ee0972a8934c88cb15e2b687d8200b1b3e023f`.
   Three new paths advance the source-package manifest to 1,728 entries at
   SHA-256
   `98f558dcdea64231714564ad7aaed86d24ee90bc8049a700e75a99885999ebf9`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The application test, fourteen ABI/runtime tests, six
   inventory/policy gates, and three install/consumer gates pass 24/24 in 8.42
   seconds. Direct inventory/resource/source-manifest/source-line checks and
   `git diff --check` remain clean.
8. Proceed only to Batch 181 Change 17: serialize TF calls through the
   scheduler coordinator, lower registered HDL task/function calls into that
   path, execute read-write/read-only synchronization requests in their exact
   regions, and publish transactional argument, delay, output/control, and
   follow-up effects in canonical order. Do not begin failure-containment or
   cross-platform closure, Release/sanitizer/hosted qualification, commit, or
   push.

## Batch 181 active checkpoint - after Change 17

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-17 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 17 adds `include/fsim/app/application_tf_scheduler.hpp`,
   `src/app/application_tf_scheduler.cpp`, and
   `tests/app/tf_scheduler_application_test.cpp`. Each registered or directly
   bound HDL call becomes an owner-qualified `TfSchedulerCallHandle`; the
   coordinator refuses direct execution outside a running scheduler phase and
   serializes initial calls, functions, synchronization callbacks, and delayed
   reactivation behind one guarded boundary.
3. One successful callback produces a single `TfSchedulerPublication` holding
   its ordered argument, control, delay, and synchronization effects. The
   coordinator stages value state and cancelable scheduler entries first,
   invokes the publication hook once, and commits only on acceptance. A
   rejected or throwing hook cancels every new entry and retains the prior
   value snapshot.
4. Read-write synchronization uses the scheduler reactive region, read-only
   synchronization uses postponed, and delay requests return through active
   with exact `reason_reactivate` dispatch. Stable orders are assigned
   monotonically from the coordinator's caller-selected base. Accepted
   read-write values are visible to later read-only callbacks; finish and stop
   effects request scheduler termination only after the publication succeeds.
5. The coordinator bounds live HDL call sites and pending callbacks to 65,536
   each, retains typed function results and per-call values, records callback
   and failure counts, rejects foreign/stale handles, and cancels retained
   pending entries on destruction. `TfBoundCall::reactivate` uses the same
   argument/time/context/work-area transaction as call and synchronization
   dispatch.
6. `tests/app/tf_scheduler_application_test.cpp` proves exact active/reactive/
   postponed placement, two delayed reactivations at exact times, follow-up
   read-only scheduling, state commit order, output/control batching, typed
   function return, finish behavior, inactive-scheduler rejection, publication
   rollback, and registry-to-coordinator execution through the independently
   authored C probe plug-in.
7. The legacy-TF ledger has two active and sixteen preserved rows at normalized
   SHA-256
   `448af37355d6f1743c98ef979f0dc747eb1e9dc7b48df45d7e24f92d2c775ec0`.
   Three new paths advance the source-package manifest to 1,731 entries at
   SHA-256
   `4ec76874ea6c6e7f23f9cb62e5dfc54fefa4d1d7c94e4c9430e0ed357cd22fba`.
8. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug target builds cleanly
   with eight workers. The two application tests, fourteen ABI/runtime tests,
   six inventory/policy gates, and three install/consumer gates pass 25/25 in
   8.47 seconds. Direct inventory/resource/source-manifest/source-line checks
   and `git diff --check` remain clean.
9. Proceed only to Batch 181 Change 18: contain plug-in exceptions, invalid
   pointers, unload, and re-entry across the complete loader, call, and
   coordinator path without publishing partial state. Do not begin
   cross-platform closure, Release/sanitizer/hosted qualification, commit, or
   push.

## Batch 181 active checkpoint - after Change 18

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-18 are complete and
   uncommitted. Preserve all current Batch 181 files; do not commit or push
   before Change 20.
2. Change 18 adds `include/fsim/runtime/tf_containment.hpp`,
   `src/runtime/tf_containment.cpp`, and
   `tests/runtime/tf_containment_test.cpp`. `validate_tf_native_pointer`
   validates complete nonempty ranges for read, write, or execute permission
   and reports null, empty, arithmetic overflow, unmapped, access, and
   inspection failures without dereferencing the candidate range.
3. Linux containment parses bounded `/proc/self/maps` entries and requires
   contiguous permission-compatible mappings. Windows containment walks
   `VirtualQuery` regions and requires committed, non-guarded pages with the
   requested protection. Callback validation uses the same execute-range
   contract.
4. Native metadata, descriptor entries and extents, interface tables, nested
   descriptors, TF registration arrays, names, and callback functions are
   validated before host inspection or invocation. Invalid ranges fail the
   existing load, validation, or bind transaction without publishing a
   plug-in or callable.
5. The TF bridge validates its context, instance, argument/value arrays,
   result storage, names, control callback, time/delay storage,
   synchronization storage, expressions, strings, and vector backing before
   entry. Public TF output-parameter and text APIs reject unmapped caller
   pointers rather than dereferencing them.
6. C++ exceptions from descriptor, `checktf`, `sizetf`, `calltf`, and `misctf`
   boundaries become stable failure results. Staged callback state is
   discarded, recursive entry returns `TfCallError::ContextBusy`, and a bound
   call retains its shared dynamic-library owner after the public loaded
   plug-in object is released.
7. `tests/runtime/tf_containment_test.cpp` proves valid permissions,
   null/overflow/unmapped rejection, malformed nested ABI pointers,
   non-executable callbacks, invalid public API pointers, lifecycle and call
   exceptions, recursive entry, transactional rollback, and retained-image C
   plug-in execution.
8. The legacy-TF ledger has one active and seventeen preserved rows at
   normalized SHA-256
   `a92791af4d45b840741bc6df20fa2c6ac581c102346cb8535a782a3ae399e83f`.
   Three new paths advance the source-package manifest to 1,734 entries at
   SHA-256
   `eb9ea05f62f95f6e83b282eeee413b3115e07c84805489e5558a62695d1e236b`.
9. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug target builds cleanly
   with eight workers. The two application tests, fifteen ABI/runtime tests,
   six inventory/policy gates, and three install/consumer gates pass 26/26 in
   8.65 seconds. Direct inventory/resource/source-manifest/source-line checks
   and `git diff --check` remain clean. Proceed only to Batch 181 Change 19:
   prove independently authored C and C++ TF plug-ins on governed Linux and
   Windows toolchains. Do not begin Release/sanitizer/hosted qualification,
   commit, or push before Change 20.

## Batch 181 active checkpoint - after Change 19

1. Resume on `codex/v3` at the intentionally dirty Batch 181 worktree. The
   integrated and pushed base remains
   `eace83d8a9dbe7bf6db85daf6889beb1f527c8e4`. Changes 1-19 are complete and
   uncommitted. Preserve all current Batch 181 files; Change 20 exclusively
   owns full qualification, documentation closure, commit, and push.
2. Change 19 adds `tests/runtime/tf_cpp_probe_plugin.cpp` and
   `tests/runtime/tf_cross_platform_plugins_test.cpp`. The C++20 probe is an
   independently authored direct-v3 consumer with a read-write task and a
   12-bit integer function. The existing C11 probe remains the independent C
   consumer with its task and 17-bit function.
3. `fsim_tf` now uses `LINKER_LANGUAGE CXX`, matching the implementation in
   `tf_link.cpp` and ensuring the Windows DLL receives the required C++ runtime
   linkage. Both C and C++ plug-ins link only through the same public
   `fsim_tf` shared/import target and use `FSIM_NATIVE_PLUGIN_CALL` plus the
   exact descriptor symbol.
4. `tf_plugin_artifact_loaded` exposes the platform layer's no-reference-count
   module query for qualification. The cross-platform test proves both images
   start unloaded, load with distinct copied metadata and two registrations,
   remain loaded after their public loader objects are released while bound
   calls retain them, execute exact task/function value semantics, and unload
   after the final bound calls are destroyed.
5. The targets use CMake target-file paths, carry C11/C++20 warnings-as-errors,
   and register one unexcluded `fsim.runtime.tf_cross_platform_plugins` test
   labeled for Linux and Windows. Local Linux Clang 22 execution is green; the
   existing Windows LLVM-MinGW full-test lane executes the same target at
   Change 20, because hosted monitoring is forbidden before that boundary.
6. The legacy-TF ledger has zero active and eighteen preserved rows at
   normalized SHA-256
   `a3bff1ef32fbf3fd412556c63c39e69a45f4efdd5cb599d07b3ac47136b531e0`.
   Two new paths advance the source-package manifest to 1,736 entries at
   SHA-256
   `e5f344dd3811d355d90252f583dc8dcc0f2f9a530155149e444b4934b718d58a`.
7. The exact LLVM 22.1.8 Clang 22 warnings-as-errors Debug targets build cleanly
   with eight workers. The two application tests, sixteen ABI/runtime tests,
   six inventory/policy gates, and three install/consumer gates pass 27/27 in
   8.69 seconds. Direct inventory/resource/source-manifest/source-line checks
   and `git diff --check` remain clean.
8. Proceed only to Batch 181 Change 20. Run the clean local Debug and Release
   closure, sanitizers, then hosted Linux and Windows qualification with the
   retained 120-minute timeouts. Fix every failure, finalize the Batch 181
   documentation, make exactly one implementation commit, push it, and only
   then begin Batch 182. Do not publish a release tag for this non-release
   batch.

## Batch 181 complete checkpoint - after Change 20

1. Resume on `codex/v3` from the pushed Batch 181 implementation commit that
   contains this checkpoint. Batch 181 Changes 1-20 are complete; the IEEE TF
   surface is frozen, and no release tag belongs to this non-release batch.
2. The direct-v3 native ABI, standard-compatible `veriuser.h`, Linux shared
   library and Windows import-library surfaces, transactional registration and
   callback lifecycle, argument/value/time/scope/control APIs, scheduler
   coordination, failure containment, and independently authored C11/C++20
   plug-in proofs are integrated. Versioned v2 plug-ins remain rejected
   without a compatibility reader or migration.
3. The legacy-TF ledger is fully preserved with zero active and eighteen
   preserved rows at normalized SHA-256
   `a3bff1ef32fbf3fd412556c63c39e69a45f4efdd5cb599d07b3ac47136b531e0`.
   The source-package manifest remains 1,736 paths at SHA-256
   `e5f344dd3811d355d90252f583dc8dcc0f2f9a530155149e444b4934b718d58a`.
4. Local Clang qualification is green. An unnecessary ASan/UBSan run also
   passed 380/380 and, per the user instruction, carries the local Debug and
   Release test qualification without redundant rebuilds or retests. Do not
   repeat or monitor sanitizer/hosted qualification before Batch 190 or an
   earlier release-closing Change 20; Batch 181 does not change that cadence.
5. Closure updates the exact bounded-source, SPDX-owned, and FST test/control
   inventories to 1,376, 1,671, and 719. The Windows audit expects 371 tests
   without LLVM and 375 with LLVM, plus exactly 1,252 archive entries in both
   Release package variants. The normal push may trigger hosted jobs, but they
   are deliberately not monitored or claimed as Batch 181 closure evidence.
6. Proceed only to Batch 182 Change 1: register the complete IEEE ACC routine
   and object inventory with independent wording and exact ownership. Do not
   implement `acc_user.h`, ACC runtime behavior, or any later Batch 182 change
   in that slice. Preserve all existing HDL profiles and the direct-v3,
   no-v2-compatibility contract; do not access or publish private LRM content.

## Batch 182 active checkpoint - after Change 1

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Change 1 is complete
   and uncommitted. Preserve the cadence clarification in the Batch 181
   closure documentation and every current Batch 182 file; Changes 1-19
   accumulate until Change 20 owns the single implementation commit and push.
2. `tests/feature_matrix/legacy_acc_inventory.tsv` has eighteen active rows
   assigning Changes 2-19 one-to-one. It inventories 102 unique standardized
   ACC routines and 115 unique canonical ACC object kinds across `V1995`,
   `V2001`, `V2001NoConfig`, `V2005`, `SV2005`, `SV2009`, `SV2012`, and
   `SV2017`. Every row has explicit implementation, positive, negative,
   coherence, scheduler, diagnostic, and resource ownership.
3. `cmake/CheckLegacyAccInventory.cmake` freezes the exact 20-batch and
   20-change plan shape, ledger schema and row identities, routine/object
   uniqueness and family coverage, safe relative ownership, all retained HDL
   profiles, independent bounded prose, and the
   `ieee-only-no-vendor-extensions` policy. Vendor names, private-reference
   traces, absolute paths, and v2 compatibility readers or migrations are
   rejected. The normalized ledger SHA-256 is
   `0e1991b462856cb75b85704af3b399420509221ebcb66f55abd98888c9cb147b`.
4. The inventory is registered as `fsim.legacy-acc-inventory`, documented in
   the feature-matrix guide, composed into the resource-portability contract,
   and included with its checker in the deterministic source-package
   manifest. The manifest now contains 1,738 ordered paths at SHA-256
   `09460f22807cb933cec82f4f4c95fc2fe067f112870048f4943b1c558846ffb3`.
5. The existing exact LLVM 22.1.8 Clang warnings-as-errors Debug target is
   current under an eight-worker build. The focused ACC/TF inventory,
   diagnostics, line-budget, source-manifest, resource-portability, and CTest
   uniqueness slice passes 7/7 in 9.45 seconds; direct inventory, resource,
   and source-manifest checks also pass, and `git diff --check` is clean.
6. Proceed only to Batch 182 Change 2: provide the standard-compatible public
   `acc_user.h` C ABI and independently authored C/C++ ABI tests. Do not begin
   ACC lifecycle behavior or later changes in that slice. Do not run Release,
   sanitizers, or hosted-CI monitoring, and do not commit or push before Batch
   182 Change 20.

## Batch 182 active checkpoint - after Change 2

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-2 are
   complete and uncommitted. Preserve the cadence clarification and all
   accumulated ACC files; Change 20 owns the single implementation commit and
   push.
2. `include/fsim/runtime/acc_user.h` is the independently authored public C
   surface. It provides all 115 inventoried object-kind identities, standard
   aliases and selectors, eight standard record families, all 102 routine
   declarations, `acc_error_flag`, and `acc_handle_calling_mod_m`. It shares
   the existing PLI fixed-width types and visibility macros, is C++ linkage
   safe, and includes no vendor bootstrap or v2 descriptor surface.
3. `tests/runtime/acc_user_abi_c_test.c` and `acc_user_abi_test.cpp` independently
   freeze the C11/C++20 record layouts, scalar widths, aliases, representative
   constants, declarations, and C++ keyword hygiene. The warnings-as-errors
   `fsim.runtime.acc_user_abi` target passes and the public header is included
   in installation, installed-copy digest, and binary-ownership contracts.
4. The ledger has 17 active/1 preserved rows at normalized SHA-256
   `e78b7fee44af319ff367743306e89f4413f31792faa1df09ec73b6e4482c8b70`.
   Three new paths advance the source-package manifest to 1,741 ordered paths
   at SHA-256
   `a74651fc76ca9aeaf52b2995df18debf5bea4f4e1cecaa36488ab8136f938d13`.
5. The exact LLVM 22.1.8 Clang warnings-as-errors Debug target builds cleanly
   with eight workers. The focused ACC/TF ABI and inventory, diagnostics,
   line-budget, source-manifest, resource-portability, and CTest-uniqueness
   checks are green; direct inventory/resource/manifest checks and
   `git diff --check` are clean.
6. Proceed only to Batch 182 Change 3: implement ACC initialization, shutdown,
   configuration, product identity, buffer reset, and error reporting behind
   the declared C ABI with transactional lifecycle state. Do not begin handle
   mapping or later work in that slice. Do not run Release, sanitizers, or
   hosted-CI monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 3

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-3 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `src/runtime/acc_lifecycle.cpp` is compiled directly into `fsim_tf` and
   exports `acc_initialize`, `acc_close`, `acc_configure`, `acc_product_type`,
   `acc_product_version`, `acc_reset_buffer`, `acc_version`, and
   `acc_error_flag`. Project version text is supplied from CMake so later
   release identity updates cannot leave the ACC product string stale.
3. The process-wide state is mutex serialized. Initialization and close are
   idempotent; generation counters cannot wrap; configuration is cleared
   without allocating. All ten standardized configuration selectors are
   accepted only inside an active lifecycle. Values are native-pointer checked
   in bounded windows, capped at 4 KiB, and rejected for null, missing
   termination, or control bytes; a copied candidate is swapped only after
   validation. Each public operation deterministically updates the standard
   error flag.
4. `tests/runtime/acc_lifecycle_test.cpp` proves both lifecycle directions,
   every selector, unassigned/null/oversize/control/inactive rejection,
   recovery without poisoned state, active-only buffer reset, stable product
   and interface identities, and four-thread serialization. The seven function
   symbols and error variable are visible from the built shared library.
5. The ledger has 16 active/2 preserved rows at normalized SHA-256
   `8fd3ba3e49f5948c6dad3cd44773b791eb74be82fcfa127250c779f320a13a69`.
   Two new paths advance the source-package manifest to 1,743 ordered paths at
   SHA-256
   `6a6e41fdb7dc9d81e1f54a48efc7fc0dcab053be955b608ea3f6483e6143b9cd`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug targets build with
   eight workers. The focused ACC lifecycle/header, adjacent TF link and
   plug-in, inventory, diagnostics, source, manifest, resource, and CTest
   uniqueness checks are green; direct contract checks and `git diff --check`
   are clean.
7. Proceed only to Batch 182 Change 4: map ACC handles onto
   generation-qualified hierarchy/VPI handles. Do not begin name lookup or
   later work in that slice. Do not run Release, sanitizers, or hosted-CI
   monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 4

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-4 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` defines a direct-v3,
   callback-driven context that maps existing 64-bit VPI object identities
   into opaque ACC wrappers without making `fsim_tf` depend back on
   `fsim_runtime`. The context freezes nonzero simulation and hierarchy
   generations, a maximum handle count capped at 1,048,576, and a
   native-pointer-checked resolver callback.
3. `src/runtime/acc_handle.cpp` keys each wrapper by simulation identity,
   hierarchy generation, and VPI handle. Publication has strong rollback for
   every allocating container operation. Stable live mappings are reused;
   released wrappers remain tombstones, underlying VPI objects are not
   released, and remapping creates a distinct wrapper. Every observation
   revalidates the underlying generation through the resolver. Null,
   malformed, stale, released, cross-simulation, cross-generation, callback,
   unterminated-type-list, and capacity failures set `acc_error_flag` without
   dereferencing unvalidated caller storage.
4. The shared link surface exports the four bridge entry points plus
   `acc_compare_handles`, `acc_object_of_type`, bounded
   `acc_object_in_typelist`, and `acc_release_object`. The test uses the real
   `SystemVerilogVpiObjectRegistry` to prove exact identity/type mapping,
   containment, stable reuse, release/remap behavior, and limits.
5. The ledger has 15 active/3 preserved rows at normalized SHA-256
   `59aa7f7b057bda2208df2a30e52562a71a4a44823d01be2662553f0cee56df90`.
   Three new paths advance the source-package manifest to 1,746 ordered paths
   at SHA-256
   `f29ace63a40eb17f6f2c2c621ee8ea82bf3768ea1d18fd3ca5a1505c890b01c5`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug target builds with
   eight workers. All eight handle symbols are visible in `libfsim_tf`; the
   focused ACC/TF, diagnostics, line-budget, source-manifest,
   resource-portability, and CTest-uniqueness slice passes 14/14 in 9.76
   seconds. Direct inventory/resource/manifest checks and `git diff --check`
   are clean.
7. Proceed only to Batch 182 Change 5: implement absolute and relative name
   lookup with exact hierarchy ownership. Do not begin traversal or later work
   in that slice. Do not run Release, sanitizers, or hosted-CI monitoring, and
   do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 5

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-5 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. The direct-v3 ACC context now carries calling, default-top, and interactive
   scope identities plus bounded lookup, relation, and name callbacks.
   `src/runtime/acc_internal.hpp` shares only active-context and lifecycle
   configuration queries between the ACC translation units; `fsim_tf` still
   has no reverse dependency on `fsim_runtime`.
3. `src/runtime/acc_lookup.cpp` implements `acc_handle_by_name`,
   `acc_handle_object`, `acc_handle_parent`, `acc_handle_scope`,
   `acc_handle_simulated_net`, `acc_handle_interactive_scope`,
   `acc_set_interactive_scope`, and `acc_set_scope`. Absolute lookup uses a
   null scope, relative lookup requires a validated scope, and object lookup
   uses generation-keyed PLI scope initialized from the calling scope.
   Null set-scope selects the default top unless `accEnableArgs` explicitly
   selects the optional `acc_set_scope` module-name argument. Scope updates
   publish only after object type and returned full name are valid.
4. Input names are copied from native storage in validated windows, have a
   4-KiB ceiling, and reject null, unreadable, empty, unterminated, or control
   data before callback dispatch. Parent absence is a successful null result;
   unsupported object kinds, nonscope bases, invalid interactive callback
   flags, and callback failures set `acc_error_flag`. An uncollapsed net maps
   to itself, while a nonnet is rejected.
5. `tests/runtime/acc_lookup_test.cpp` uses the actual VPI registry with two
   branches that share a leaf name. It proves absolute, relative, and current
   PLI-scope lookup, optional-argument configuration, atomic rejected scope
   updates, parent and containing scope, interactive scope, simulated-net
   identity, and malformed-pointer containment.
6. The ledger has 14 active/4 preserved rows at normalized SHA-256
   `41226756b7d37a18efba1297c6bb092ae209450c1e2347b741b8165bf3447511`.
   Three new paths advance the source-package manifest to 1,749 ordered paths
   at SHA-256
   `3fcc03a34e772ab803ebbed112c60f267b8a5caeef404ca1bb3dc2bdbb066853`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug target builds with
   eight workers. All eight lookup/scope symbols are visible in `libfsim_tf`;
   the focused ACC/TF, diagnostics, line-budget, source-manifest,
   resource-portability, and CTest-uniqueness slice passes 15/15 in 9.84
   seconds. Direct inventory/resource/manifest checks and `git diff --check`
   are clean.
8. Proceed only to Batch 182 Change 6: implement canonical top, scope, module,
   instance, and child traversal. Do not begin complete object-model work or
   later changes in that slice. Do not run Release, sanitizers, or hosted-CI
   monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 6

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-6 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` extends the direct-v3 context
   with begin/next/end traversal operations and nine canonical families. The
   callback remains owned by the simulator integration; `fsim_tf` does not
   acquire a reverse dependency on `fsim_runtime`.
3. `src/runtime/acc_traversal.cpp` implements the nine standardized
   `acc_next_*` entry points plus `acc_collect`, `acc_count`, and `acc_free`.
   Sessions bind callback cursors to simulation identity, hierarchy
   generation, family, and owner. A valid prior object can be repositioned
   canonically when no session survives; family/owner mismatch, invalid type,
   callback failure, exhaustion, and failed handle publication close cursors.
4. Collection dispatch accepts only exact standardized iterator entry points.
   Arrays are capped at 1,048,576 objects, null-terminated, published only
   after complete traversal, tracked by exact allocation address, and freed
   once. Invalid count storage and stale/foreign arrays fail without partial
   publication.
5. `tests/runtime/acc_traversal_test.cpp` uses the actual VPI registry to prove
   filtered creation order, independent top modules, nested scopes, every
   traversal family, prior-object recovery, cross-family cursor rejection,
   collector/count equivalence, malformed input containment, and complete
   native-cursor cleanup.
6. The ledger has 13 active/5 preserved rows at normalized SHA-256
   `a3982146cd3fc3ef3b619322e9ff435f60047e3a9206a1870bdaf88cdde8da4a`.
   Two new paths advance the source-package manifest to 1,751 ordered paths
   at SHA-256
   `43b9a99f43c036415de8269346b15eb98bc4d71b47edcd22ec85cca2057db07f`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug traversal target builds
   with eight workers, and all twelve traversal/collection symbols are visible
   in `libfsim_tf`. The focused ACC/TF, diagnostics, line-budget,
   source-manifest, resource-portability, and CTest-uniqueness slice passes
   16/16 in 9.77 seconds. Direct inventory/resource/manifest checks are also
   clean. No Release, sanitizer, hosted-CI inspection, commit, or push has run.
8. Proceed only to Batch 182 Change 7: implement the complete ACC object model
   for ports, nets, variables, parameters, primitives, paths, and timing
   objects. Do not begin value-read or later work in that slice. Do not run
   Release, sanitizers, or hosted-CI monitoring, and do not commit or push
   before Change 20.

## Batch 182 active checkpoint - after Change 7

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-7 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` adds a versioned, bounded object
   query record and callback with fifteen operations. It carries exact owner
   and endpoint VPI identities, index/type/edge selectors, and synchronously
   borrowed copied names. Context validation requires executable callback
   storage before publication.
3. `src/runtime/acc_object.cpp` implements all fifteen Change 7 handle
   routines. Unary relations, indexed ports/terminals, module and intermodule
   paths, and timing checks validate their source/result families before
   publishing an ACC wrapper. Module-path and timing-check variadic handles
   are consumed only when `accEnableArgs` enables that exact routine; names
   remain bounded and pointer-checked.
4. `src/runtime/acc_handle.cpp` now preserves generic type membership for full
   module/scope, net, register, port, terminal, primitive, parameter, and
   timing-check types in both `acc_object_of_type` and bounded type lists.
5. `tests/runtime/acc_object_test.cpp` uses the actual VPI registry and an
   independently authored query integration. It covers every object subtype
   assigned to Change 7, all fifteen operations, named and handle-selected
   paths/checks, generic membership, successful absence, pre-dispatch input
   rejection, missing callback rejection, and exception containment.
6. The ledger has 12 active/6 preserved rows at normalized SHA-256
   `aab280fd9d5aa9645179e4218f57f7b2116b1297cb2c24ab93894fff7e5b1438`.
   Two new paths advance the source-package manifest to 1,753 ordered paths
   at SHA-256
   `e200e19c57903a150c3c999e227189739bbde76bdc59ef912e0dba9976b5a077`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug object target builds
   with eight workers, and all fifteen new object/connectivity symbols are
   visible in `libfsim_tf`. The focused ACC/TF, diagnostics, line-budget,
   source-manifest, resource-portability, and CTest-uniqueness slice passes
   17/17 in 9.96 seconds. Direct inventory/resource/manifest checks are also
   clean. No Release, sanitizer, hosted-CI inspection, commit, or push has
   run.
8. Proceed only to Batch 182 Change 8: implement scalar, vector, real, string,
   strength, delay, property, location, range, and timescale reads. Do not
   begin value updates or later work in that slice. Do not run Release,
   sanitizers, or hosted-CI monitoring, and do not commit or push before
   Change 20.

## Batch 182 active checkpoint - after Change 8

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-8 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` adds a bounded version-3 read
   query/result callback. It carries exact object/property metadata,
   four-state words, real and parameter values, and synchronously borrowed
   bounded strings. Context entry now requires that callback.
3. `src/runtime/acc_read.cpp` implements all twenty Change 8 routines. It
   supports direct binary/octal/decimal/hexadecimal/strength strings and
   structured scalar, integer, real, string, and arbitrary-width vector
   values. Names and values use thread-local copied storage; destination and
   callback pointers, widths, lengths, ABI records, live full types, range,
   timescale, and precision are validated before output publication.
4. Attribute reads distinguish present, missing, and callback failure. Missing
   values return the caller default, while `accDefaultAttr0` returns the exact
   zero form without reading an omitted variadic argument. Callback and VPI
   resolver exceptions cannot cross the C boundary.
5. `tests/runtime/acc_read_test.cpp` uses the actual VPI registry and covers
   metadata, location/range/time, parameters, attributes/defaults, all value
   format families, wide and unknown four-state values, real/string values,
   checked destinations, malformed callback storage, missing callbacks, and
   exception containment. The inventory checker requires all 115 frozen ACC
   types in the type-string table.
6. The ledger has 11 active/7 preserved rows at normalized SHA-256
   `86b78b3383fa3b7f9142d65b89653d2e6184cae034c1a9c7bf28f4818aa9bff9`.
   Two new paths advance the source-package manifest to 1,755 ordered paths
   at SHA-256
   `4f12fd9f4e15e133b80840c6efb0c05a1412e1f304d51fbcbd302ac27ad87d19`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, all twenty new read/property symbols are visible in `libfsim_tf`,
   and the focused ACC/TF, diagnostics, line-budget, source-manifest, resource,
   install, and CTest-uniqueness slice passes 18/18 in 15.88 seconds. Direct
   inventory/resource/manifest checks are also clean. No Release, sanitizer,
   hosted-CI inspection, commit, or push has run.
8. Proceed only to Batch 182 Change 9: implement deposit, force, release,
   assign, deassign, and scheduled value updates. Do not begin indexed
   iterator or later work in that slice. Do not run Release, sanitizers, or
   hosted-CI monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 9

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-9 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` extends the direct-v3 context
   with a synchronous write callback. Bounded query/result records carry the
   exact VPI object/full type, target width, update model, normalized integer
   or real time, copied value data, and an atomic post-operation value.
   Elaboration metadata supplies separate deposit, force, release, assign, and
   deassign capability bits.
3. `src/runtime/acc_write.cpp` implements `acc_set_value`. It validates the
   live generation/type, object family, capability, model, delay, format,
   width, and every input/output pointer before dispatch. Scalar, integer,
   real, empty/nonempty string, all radix-string, and arbitrary-width vector
   inputs are copied before the callback; vector words retain both `aval` and
   `bval`.
4. No-delay, inertial, modified-transport, and pure-transport deposits remain
   distinct. Force/release and procedural assign/deassign use independent
   capabilities. Release and deassign validate their destination before the
   transaction and return the post-operation value through that same callback,
   avoiding a second simulator boundary crossing.
5. `tests/runtime/acc_write_test.cpp` uses the actual VPI registry and covers
   all models, time encodings, input forms, Change 9 object subtypes, atomic
   returned forms, incompatible targets, malformed pointers/results, callback
   rejection, missing callbacks, and exception containment.
6. The ledger has 10 active/8 preserved rows at normalized SHA-256
   `7f9bf91e584bda7bd61c9561c9198524613454868a67b6ba254b64d55496a44a`.
   Two new paths advance the source-package manifest to 1,757 ordered paths
   at SHA-256
   `e03310154a6fe4cdd9f927639e00f5a9b2a3d9c14c3dd0499428ba4f42fd6e79`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, `acc_set_value` is visible in `libfsim_tf`, and the focused ACC/TF,
   diagnostics, line-budget, source-manifest, resource, install, and CTest-
   uniqueness slice passes 19/19 in 15.83 seconds. Direct inventory/resource/
   manifest checks are also clean. No Release, sanitizer, hosted-CI inspection,
   commit, or push has run.
8. Proceed only to Batch 182 Change 10: implement generic and specialized
   indexed `acc_next_*` traversal. Do not begin path-delay/timing-check or later
   work in that slice. Do not run Release, sanitizers, or hosted-CI monitoring,
   and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 10

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-10 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` adds a bounded direct-v3
   relation-iterator query/result callback with thirteen begin/next/end
   families and at most 256 generic filter types. Context entry requires the
   callback before any handle can be published.
3. `src/runtime/acc_iterator.cpp` implements generic `acc_next` and all twelve
   specialized Change 10 routines. Sessions are keyed by simulation,
   hierarchy generation, prior handle, family, owner, and exact copied type
   list, so identical result handles in distinct relation families do not
   collide. Generic/full-type matching is shared with the existing handle
   membership implementation.
4. Generic filter lists are pointer-checked, bounded, nonempty, unique, and
   restricted to the frozen ACC type inventory. Owners and results are checked
   against their standardized families. A missing session recovers only by
   finding the exact valid prior object in canonical order; mutation or an
   unrelated prior fails. Exhaustion and every post-begin failure close the
   native cursor.
5. `tests/runtime/acc_iterator_test.cpp` uses the actual VPI registry and covers
   generic filtering, all specialized families, order, recovery, same-result
   cross-family sessions, malformed lists/owners/prior objects/results,
   callback exceptions, and cursor cleanup.
6. The ledger has 9 active/9 preserved rows at normalized SHA-256
   `2c0ab781f4da5a6a6cd0f33d821bfa03b79fad0afac2ee2a90e962bb5b9c8990`.
   Two new paths advance the source-package manifest to 1,759 ordered paths
   at SHA-256
   `4f7142fb1b9c393c47fd250775983f94e04bbc523459bc9d8794cd669c9dcff5`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, all thirteen indexed-iterator symbols are visible in `libfsim_tf`,
   and the focused ACC/TF, diagnostics, line-budget, source-manifest, resource,
   install, and CTest-uniqueness slice passes 20/20 in 16.08 seconds. Direct
   inventory/resource/manifest checks are also clean. No Release, sanitizer,
   hosted-CI inspection, commit, or push has run.
8. Proceed only to Batch 182 Change 11: implement path-delay and timing-check
   access. Do not begin value-change callback or later work in that slice. Do
   not run Release, sanitizers, or hosted-CI monitoring, and do not commit or
   push before Change 20.

## Batch 182 active checkpoint - after Change 11

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-11 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` appends a required bounded v3
   timing callback. Read metadata carries timing capabilities and a maximum or
   fixed delay count; timing queries carry exact object identity, configured
   arity, min/typ/max mode, high-impedance policy, and copied values.
3. `src/runtime/acc_timing.cpp` implements the nine path-delay and timing-check
   routines. Paths use initialized `accPathDelayCount` configuration; primitive,
   timing-check, and input-port operations use exact object metadata. Scalar
   arguments and min/typ/max arrays are validated before callback dispatch.
4. Pulse pairs must contain finite nonnegative ordered reject/error values;
   percentage pulse selection is also limited to one hundred. Every fetch
   validates all destinations and the complete simulator result before any
   caller-visible update. Exceptions and malformed results retain neutral
   returns and the ACC error flag without partial publication.
5. `tests/runtime/acc_timing_test.cpp` uses the actual VPI registry and covers
   all nine routines, paths, primitives, timing checks, input ports, rejected
   nets, default and overridden configuration, scalar and min/typ/max forms,
   pulse ordering, transactional output, malformed results, rejection, and
   callback exceptions.
6. The ledger has 8 active/10 preserved rows at normalized SHA-256
   `4bc92f4dfa61a154820f9e9441ec6bde7a7b7cabf078bc5b171216b1fd802bf7`.
   Two new paths advance the source-package manifest to 1,761 ordered paths at
   SHA-256
   `eae8992afe529f43540db075c3e483effc4b3bd7cb3abb962256b751476e99e8`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, all nine timing symbols are visible in `libfsim_tf`, and the
   focused accumulated ACC/TF, diagnostics, line-budget, source-manifest,
   resource, install, and CTest-uniqueness slice passes 21/21 in 20.54 seconds.
   Direct inventory/resource/manifest checks and `git diff --check` are clean.
   No Release, sanitizer, hosted-CI inspection, commit, or push has run.
8. Proceed only to Batch 182 Change 12: implement value-change-link callback
   registration. Do not begin cancellation/order or later work in that slice.
   Do not run Release, sanitizers, or hosted-CI monitoring, and do not commit or
   push before Change 20.

## Batch 182 active checkpoint - after Change 12

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-12 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `include/fsim/runtime/acc_handle_bridge.h` appends a required v3 VCL control
   callback plus a checked host-to-ACC event record and dispatch entry point.
   Registration conveys a stable link identity, exact VPI/ACC object identity,
   and the standardized logic-or-strength view.
3. `src/runtime/acc_vcl.cpp` implements `acc_vcl_add`. It records the exact
   context generation, object handle and width, consumer, user data, and flags
   before simulator registration, then rolls back the unpublished record if
   registration rejects or throws.
4. Dispatch revalidates the active context and current VPI type, then validates
   time and scalar, strength, real, or wide four-state event shape before
   invoking the consumer outside the link lock. Vector-like records retain the
   exact ACC handle. Simulator and consumer exceptions are contained.
5. `tests/runtime/acc_vcl_test.cpp` uses the actual VPI registry and covers
   logic, strength, vector, and real callbacks, high/low simulation time, exact
   user data, rejected objects and flags, malformed event payloads, registration
   rejection/exceptions, consumer exceptions, and inactive-context dispatch.
   Cancellation and re-entry ordering remain explicitly Change 13.
6. The ledger has 7 active/11 preserved rows at normalized SHA-256
   `386aa77a6007a7ecb91ad188898691d5a7354f1e5418bed3c076eb5441407e62`.
   Two new paths advance the source-package manifest to 1,763 ordered paths at
   SHA-256
   `650219911a89478842f9df79dc28f5647d77d3ba6e3fb35395d17102beec799b`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, the VCL registration and dispatch exports are visible in
   `libfsim_tf`, and the focused accumulated ACC/TF, diagnostics, line-budget,
   source-manifest, resource, install, and CTest-uniqueness slice passes 22/22
   in 16.60 seconds. Direct inventory/resource/manifest checks and `git diff
   --check` are clean. No Release, sanitizer, hosted-CI inspection, commit, or
   push has run.
8. Proceed only to Batch 182 Change 13: implement callback cancellation,
   ordering, and re-entry containment. Do not run Release, sanitizers, or
   hosted-CI monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 13

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-13 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. VCL event records now carry stable sequence identities. Each link permits
   only strictly increasing sequence values and one active consumer; duplicate,
   reversed, concurrent, and same-link re-entrant dispatch fails before entry.
3. `src/runtime/acc_callback.cpp` implements `acc_vcl_delete` over the shared
   VCL registry. Cancellation marks a link unavailable before synchronous host
   unregister. A rejected unregister restores it; successful unregister waits
   for an active consumer and then retires the record.
4. A thread-local dispatch identity makes self-cancellation nonblocking. The
   record remains canceling during the consumer and is erased immediately when
   that dispatch returns, preventing any later callback without deadlock.
5. `tests/runtime/acc_callback_test.cpp` uses the actual VPI registry and covers
   unique registrations, monotonic ordering, rejected-cancellation recovery,
   host re-entry while canceling, late dispatch, consumer recursion,
   self-cancellation, and absent exact tuple deletion.
6. The ledger has 6 active/12 preserved rows at normalized SHA-256
   `c74739a6a3d17a18bbb50636366aebffb82ad4736a3116586a4ee579e750c81e`.
   Two new paths advance the source-package manifest to 1,765 ordered paths at
   SHA-256
   `4120f9baf7a78b3673d0d715e3b79c4158d1c277352e0ae0c0233fb126727e54`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, registration/cancellation/dispatch exports are visible in
   `libfsim_tf`, and the focused accumulated ACC/TF, diagnostics, line-budget,
   source-manifest, resource, install, and CTest-uniqueness slice passes 23/23
   in 19.15 seconds. Direct inventory/resource/manifest checks and `git diff
   --check` are clean. No Release, sanitizer, hosted-CI inspection, commit, or
   push has run.
8. Proceed only to Batch 182 Change 14: preserve handles, iterators, callback
   links, and borrowed value storage across documented safe points. Do not run
   Release, sanitizers, or hosted-CI monitoring, and do not commit or push
   before Change 20.

## Batch 182 active checkpoint - after Change 14

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-14 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `fsim_acc_safe_point_advance_v3` accepts a bounded record with a nonzero,
   strictly increasing identity per simulation/hierarchy generation. Failed
   iterator invalidation rolls its publication back.
3. A successful advance closes retained native iterator cursors and tombstones
   exact continuation keys, so an old prior object cannot resume at a later
   safe point. Starting a fresh traversal at that point remains supported.
4. Read and returned-write thread-local string storage is invalidated at each
   safe point and by `acc_reset_buffer`. Generation-qualified handles and VCL
   links survive safe-point advancement, while a changed hierarchy generation
   invalidates both.
5. `tests/runtime/acc_handle_lifetime_test.cpp` uses the actual VPI registry and
   covers cursor retirement, forbidden continuation, fresh traversal,
   handle/link survival, borrowed-storage reacquisition and reset, monotonic and
   malformed point rejection, and changed-generation rejection.
6. The ledger has 5 active/13 preserved rows at normalized SHA-256
   `b5d376fcc6eb45354399c30fea89a148841b6b38b92ec55ce376f0073de554f5`.
   Two new paths advance the source-package manifest to 1,767 ordered paths at
   SHA-256
   `b3110c1e217a4203a7d4870fb1d2b8b811e6dfcaffd49b86962f3b1ef88cc8b0`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, the safe-point export is visible in `libfsim_tf`, and the focused
   accumulated ACC/TF, diagnostics, line-budget, source-manifest, resource,
   install, and CTest-uniqueness slice passes 24/24 in 18.50 seconds. Direct
   inventory/resource/manifest checks and `git diff --check` are clean. No
   Release, sanitizer, hosted-CI inspection, commit, or push has run.
8. Proceed only to Batch 182 Change 15: share values, scopes, and work areas
   coherently between TF and ACC. Do not run Release, sanitizers, or hosted-CI
   monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 15

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-15 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. The optional `fsim_acc_tf_context_v3` binding names the exact active
   `fsim_tf_call_context_v3`, one VPI identity per TF argument, and bounded
   process argv storage. ACC context entry validates active-context identity,
   simulation identity, hierarchy generation, counts, pointers, terminators,
   limits, and every nonzero argument object before publication.
3. `src/runtime/acc_tf_coherence.cpp` implements all eleven standardized
   task/function ACC routines. Numeric and string fetches read the live TF
   value bridge; explicit fetches accept only the mapped current instance;
   argument and instance handles use the existing generation-qualified
   VPI-to-ACC bridge. The opaque `acc_handle_itfarg` token must be the exact
   current TF instance token.
4. The shared work-area pointer remains solely in the TF call context. ACC
   operations observe that same call while TF mutations remain subject to the
   existing successful-call commit, callback-exception rollback, and
   per-instance isolation behavior. No duplicate ACC work-area state exists.
5. `tests/runtime/acc_tf_coherence_test.cpp` uses the actual VPI registry and
   covers argv, integer/real/string values, implicit and explicit argument
   access, exact instance and argument handles, wrong handle/token rejection,
   malformed context/argv binding, a TF write immediately observed through
   ACC, work-area persistence, and post-callback lifetime rejection.
6. The ledger has 4 active/14 preserved rows at normalized SHA-256
   `0175158b4a9c1cc8f7576bdd4b38f9a9721d3228f934edfa76ae6f41bba92930`.
   Two new paths advance the source-package manifest to 1,769 ordered paths at
   SHA-256
   `8817eaeff57d598f9c7bbcac25f0b5d3889d25d0bf16f10fb722924ad8c21e28`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, all eleven ACC routines plus the current-TF-context export are
   visible in `libfsim_tf`, and the focused accumulated ACC/TF, diagnostics,
   line-budget, source-manifest, resource, install, and CTest-uniqueness slice
   passes 25/25 in 17.03 seconds. Direct inventory/resource/manifest checks and
   `git diff --check` are clean. No Release, sanitizer, hosted-CI inspection,
   commit, or push has run.
8. Proceed only to Batch 182 Change 16: prove ACC and VPI views refer to the
   same simulation objects. Do not run Release, sanitizers, or hosted-CI
   monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 16

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-16 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. The SystemVerilog VPI object registry now has explicit `Constant`,
   `Concatenation`, `Operation`, and `MinTypMax` expression kinds, retaining
   one generation-qualified identity and the existing bounded value storage.
3. `fsim_acc_vpi_same_object_v3` compares an ACC handle with the exact VPI
   identity returned by the existing handle bridge. Null identities, distinct
   live objects, stale ACC handles, and released VPI generations fail through
   `acc_error_flag`; names or copied values are never used as an equivalence
   fallback.
4. `tests/runtime/acc_vpi_coherence_test.cpp` backs the ACC callbacks with one
   actual VPI registry and proves the same expression types, value storage,
   parent/full-name hierarchy, condition connectivity, path-delay record, and
   released-generation rejection through direct VPI and ACC views.
5. The ledger has 3 active/15 preserved rows at normalized SHA-256
   `6efd69113229bb69bf950288feb34e7d35a04198870e26b20f60fd1dc28558bf`.
   Two new paths advance the source-package manifest to 1,771 ordered paths at
   SHA-256
   `d88d360f92f87be8c400fee76f776db7816ba5d67d6cc2e8ce9963c68050e9b4`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, the equality bridge is visible in `libfsim_tf`, and the focused
   accumulated ACC/TF, diagnostics, line-budget, source-manifest, resource,
   install, and CTest-uniqueness slice passes 26/26 in 8.03 seconds. Direct
   inventory/resource/manifest checks and `git diff --check` are clean. No
   Release, sanitizer, hosted-CI inspection, commit, or push has run.
7. Proceed only to Batch 182 Change 17: define deterministic PLI behavior under
   future parallel execution. Do not run Release, sanitizers, or hosted-CI
   monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 17

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-17 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `AccSchedulerCoordinator` accepts bounded staged observations, mutations,
   traversals, and callbacks. Every request owns an immutable epoch, scheduler
   phase, stable order, and source-order identity; ordered storage makes
   execution independent of worker arrival order.
3. Only a running scheduler in the named phase can drain an epoch. Duplicate
   identities, completed epochs, later epochs that would strand earlier work,
   direct inactive drains, and callback re-entry are rejected. Foreign and
   publication exceptions are contained while every attempted request is
   retired exactly once.
4. `tests/app/acc_scheduler_application_test.cpp` concurrently stages all four
   operation families in reverse and shuffled orders and proves identical
   canonical traces. It also covers exact time/delta/phase publication,
   duplicate and stale identities, monotonic epoch drains, inactive access,
   exception containment, and re-entry rejection.
5. The ledger has 2 active/16 preserved rows at normalized SHA-256
   `622bdc76903f3a962b8c42985a952eed117c5b7d6b4451a076891a2fc8b9c7eb`.
   Three new paths advance the source-package manifest to 1,774 ordered paths
   at SHA-256
   `b53a780a2d33692dc5ea50335cb62bf90bd0c0209b2ec9dc496e78ac467b47c0`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers and the focused accumulated ACC/TF, diagnostics, line-budget,
   source-manifest, resource, install, and CTest-uniqueness slice passes 27/27
   in 8.19 seconds. Direct inventory/resource/manifest checks and `git diff
   --check` are clean. No Release, sanitizer, hosted-CI inspection, commit, or
   push has run.
7. Proceed only to Batch 182 Change 18: reject unsupported vendor names with
   stable diagnostics. Do not run Release, sanitizers, or hosted-CI monitoring,
   and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 18

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-18 are
   complete and uncommitted. Preserve all accumulated ACC and cadence files;
   Change 20 owns the batch's single implementation commit and push.
2. `fsim_acc_validate_and_dispatch_standard_v3` accepts only an exact v3
   query layout. It validates bounded terminated lowercase routine names,
   numeric object and behavior selectors, reserved fields, and executable
   dispatch addresses before entering the host callback.
3. The sorted runtime table contains all 102 standardized routine names. The
   exhaustive witness submits all 115 canonical object constants plus every
   behavior family. The checker cross-references every ledger routine against
   the table and every canonical object token against the witness.
4. Unsupported routine, object, and behavior requests receive distinct stable
   `FSIM-ACC-NAME-002`, `003`, and `004` diagnostics without fallback dispatch.
   Malformed or v2 queries use `001`, host rejection uses `005`, and a caught
   dispatch exception uses `006`; every code is in the diagnostic catalog.
5. The ledger has 1 active/17 preserved rows at normalized SHA-256
   `cf597d162ad3ad362f91bba92dfc765ae0a956487e44aa23ef02b002f007e27b`.
   Two new paths advance the source-package manifest to 1,776 ordered paths at
   SHA-256
   `6ff4f37af1ff96a346b82e8e1ed452f3eafd5a5b6e542e32ca498f929133df9c`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers, the validation/dispatch symbol is visible in `libfsim_tf`, and the
   focused accumulated ACC/TF, diagnostics, line-budget, source-manifest,
   resource, install, and CTest-uniqueness slice passes 28/28 in 9.05 seconds.
   Direct inventory/resource/manifest checks and `git diff --check` are clean.
   No Release, sanitizer, hosted-CI inspection, commit, or push has run.
7. Proceed only to Batch 182 Change 19: run the full TF/ACC engine, artifact,
   cache, and platform corpus. Do not run Release, sanitizers, or hosted-CI
   monitoring, and do not commit or push before Change 20.

## Batch 182 active checkpoint - after Change 19

1. Resume on `codex/v3` from pushed Batch 181 commit
   `3322ff5d557af8ae22a2378f62af91282ec5b1bc`. Batch 182 Changes 1-19 are
   complete and intentionally uncommitted. Preserve all accumulated ACC and
   cadence files; Change 20 owns the batch's single implementation commit and
   push.
2. Change 19 adds independent C11 and C++20 ACC shared-library consumers plus
   `fsim.runtime.acc_cross_platform_plugins`. Both consumers include the
   public `acc_user.h` surface and link to `fsim_tf`; the host uses the common
   platform loader and CMake target-file paths on Linux and Windows.
3. The host resolves all 102 standardized ACC routine exports and the standard
   error flag, rejects representative vendor names, proves both original
   images load/execute/unload, then copies them into cache-artifact paths and
   repeats the same execution and final-unload checks.
4. The ledger has zero active and 18 preserved rows at normalized SHA-256
   `1fa0db768c78515e6596639dc3da43b323c1e7f8c2b209d668c02f35071bb1a7`.
   Three new paths advance the source-package manifest to 1,779 ordered paths
   at SHA-256
   `be6a91b5d3310f26659ad6f805b29c37486cfe55a3feb28740d56d2b2c931bf6`.
5. The exact LLVM 22.1.8 Clang warnings-as-errors Debug target builds with
   eight workers. The complete TF/ACC slice passes 38/38 in 0.11 seconds; the
   LLVM engine and object/design/library/application artifact/cache slice
   passes 7/7 in 22.58 seconds; and the focused diagnostics, line-budget,
   source-manifest, install, Windows-toolchain, resource, and uniqueness slice
   passes 9/9 in 8.36 seconds. Direct legacy-ACC, resource, and source-manifest
   checks are clean.
6. No Release build, sanitizer, hosted-CI monitoring, commit, or push ran.
   Proceed only to Batch 182 Change 20: run the clean local Clang Debug and
   Release closure, finalize documentation, create the single implementation
   commit, and push. Batch 182 is not a sanitizer or hosted-CI boundary.

## Batch 182 closure checkpoint

1. Resume on `codex/v3` with Batch 182 complete. The batch implements the
   standardized IEEE ACC surface over the common v3 TF/ACC plugin library and
   closes the legacy IEEE PLI inventory at zero active/18 preserved rows. The
   normalized ledger SHA-256 is
   `1fa0db768c78515e6596639dc3da43b323c1e7f8c2b209d668c02f35071bb1a7`.
2. Change 20 completed clean eight-worker warnings-as-errors builds with the
   exact LLVM 22.1.8 Clang toolchain for both Debug and Release. The clean
   Debug suite passes 403/403 in 147.89 seconds; the clean Release suite passes
   403/403 in 266.48 seconds.
3. The first Release build exposed one pre-existing coverage-control decode
   parameter shadowing the enclosing SimIR process identity. The inner
   parameter now has an unambiguous name, and the subsequent clean Release
   build is warning-free.
4. The completed ACC sources and tests advance the frozen audit evidence to
   2,599 production diagnostics, 1,417 bounded authored sources, 1,714
   SPDX-owned files, 533 conformance test/control files, and 742 release-audit
   test/control files. All affected composed release gates pass in both full
   suites.
5. The source-package manifest remains at 1,779 ordered payload paths with
   SHA-256
   `be6a91b5d3310f26659ad6f805b29c37486cfe55a3feb28740d56d2b2c931bf6`.
   Direct legacy-ACC inventory, resource-portability, source-manifest, and
   whitespace checks are clean.
6. Batch 182 is not a tenth-batch boundary, so no sanitizer lane and no
   hosted-CI monitoring ran. Sanitizers and hosted-CI monitoring run only
   every ten batches; the next and final such boundary in this plan is Batch
   190. Release closures outside Batch 180 and Batch 190 do not add those
   lanes.
7. After the single Batch 182 implementation commit and push, proceed only to
   Batch 183 Change 1: build the independently worded VHDL 2008-to-2019 clause
   inventory. Keep the two private LRMs read-only and do not copy, quote, hash,
   log, package, or record their paths in repository artifacts.

## Batch 183 active checkpoint - after Change 1

1. Resume on `codex/v3` from pushed Batch 182 commit
   `6258a2a9e608cd80dda96261861cd56874308aae`. Batch 183 Change 1 is complete
   and intentionally uncommitted; preserve its inventory, checker, package,
   resource-policy, test-registration, plan, and resume changes through the
   batch's Change 20 commit boundary.
2. `tests/feature_matrix/vhdl_2019_inventory.tsv` is the independently worded
   37-row IEEE 1076-2019 delta ledger. It maps Batch 183 Changes 2-19 and Batch
   184 Changes 1-19 one-to-one, begins with 37 active/zero preserved rows, and
   records only standard identities, clause numbers, project-authored feature
   summaries, closure owners, and repository-owned implementation/evidence
   paths. Its normalized SHA-256 is
   `7878624b9dfbe77e91544bca04dfb618aa3436d3d0369231fe41c421bee55fae`.
3. `cmake/CheckVhdl2019Inventory.cmake` freezes the ledger shape, digest,
   identities, unique closure allocation, existing repository owners,
   reference-material exclusion, documentation, CTest registration, and
   source-package inclusion. `fsim.vhdl-2019-inventory` owns the focused CTest
   entry, and the resource-portability contract now owns the ledger.
4. Two new paths advance the source-package manifest to 1,781 ordered payload
   paths at SHA-256
   `38cdcc93959786f4eab63f0a9131e6a548f8374dd931ad9ef2d28df6952afd03`.
   The private reference remained read-only; no reference wording, location,
   or hash was copied into repository artifacts.
5. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree builds with eight
   workers. The focused VHDL inventories, diagnostics, source line-budget,
   source-package manifest, resource-portability, and CTest-uniqueness slice
   passes 8/8 in 8.01 seconds. Direct inventory, resource, manifest, and
   whitespace checks are clean. No Release, sanitizer, hosted-CI monitoring,
   commit, or push ran.
6. Proceed only to Batch 183 Change 2: add the VHDL-2019 enum, manifest, CLI,
   artifact, and cache identities. Keep the older VHDL profiles unchanged and
   reject v2 persisted inputs directly. Do not run Release, sanitizers, hosted
   CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 2

1. Batch 183 Changes 1-2 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 is a distinct project/frontend standard revision inside the
   existing VHDL language family. Manifests and direct CLI use canonical 2019
   identity through 19, 2019, vhdl-19, and vhdl-2019 spellings while the
   default remains 2008. The durable enum value was appended, not inserted, so
   retained object payload numeric identities do not move.
3. Equivalent 2008 and 2019 compilation/elaboration cases prove distinct
   object compilation digests, design digests, design cache keys, and
   specialization cache keys. The portable-unit enum validator explicitly
   accepts the appended 2019 value. The 2019 baseline also proves inheritance
   of the complete retained 2008 lexical surface.
4. V19-B183-C02 is preserved. The inventory now has 36 active/one preserved
   rows at normalized SHA-256
   `d9eb8c7bd35fdd867a03d73d777c2eebe878afa7cb78a7e6cc928dd536aa153d`.
   No paths were added, so the source-package manifest remains at 1,781 paths
   and SHA-256
   `38cdcc93959786f4eab63f0a9131e6a548f8374dd931ad9ef2d28df6952afd03`.
5. The exact LLVM 22.1.8 Clang warnings-as-errors Debug tree completed a
   1,491-step impact rebuild with eight workers. The focused identity,
   profile, artifact/cache, inventory, and governance implementation slice
   passes 16/16 in 8.33 seconds, and the post-documentation governance slice
   passes 6/6 in 8.73 seconds. No Release, sanitizer, hosted-CI, commit, or
   push action ran.
6. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   release closures at 188, 191, 193, 195, and 197 do not add those lanes.
7. Proceed only to Batch 183 Change 3: implement the revised VHDL-2019
   lexical, grammar, and conditional-analysis behavior while keeping every
   older profile unchanged. Do not run Release, sanitizers, hosted CI, commit,
   or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 3

1. Batch 183 Changes 1-3 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. Change 3 adds a pre-lexer VHDL conditional-analysis stage in
   `src/frontend/vhdl_conditional_analysis.cpp`. It selects nested `if,
   `elsif, `else, and `end if branches using deterministic VHDL/tool
   identifiers, Boolean composition, and case-sensitive string comparisons.
   Inactive bytes are replaced with spaces while CR/LF bytes are retained, so
   selected tokens and AST nodes preserve their original offsets, lines, and
   columns and inactive lexical garbage cannot reach the lexer.
3. Directive recognition is case-insensitive, respects VHDL block and line
   comments, and permits trailing line comments. Stable diagnostics
   `FSIM-VHDL-CA-001` through `FSIM-VHDL-CA-004` cover profile isolation,
   malformed expressions/directives, invalid group structure, and the
   128-frame nesting ceiling. Excess groups are consumed with a separate
   bounded synchronization depth so closing directives still match correctly.
4. Focused evidence covers selected and inactive branches, nested Boolean
   conditions, exact physical coordinates, value case sensitivity,
   comment-context isolation, pre-2019 rejection, malformed/unterminated input,
   and over-depth synchronization. V19-B183-C03 is preserved. The inventory
   has 35 active/two preserved rows at normalized SHA-256
   `dd08da36038185079d1f75d1a2aaa606def7a019e20ebe11045cd002ba933594`.
5. Two new implementation paths advance the source-package manifest to 1,783
   ordered payload paths and SHA-256
   `216487cc9516f15f0e86725fcd6cb7837444995284617a673b1ae39b586f2d26`.
   The exact LLVM 22.1.8 Clang warnings-as-errors Debug target builds with eight
   workers, the focused frontend executable passes, and the final focused
   frontend/diagnostics/line-budget/manifest/inventory/resource/CTest slice is
   7/7 green in 12.75 seconds.
6. No Release, sanitizer, hosted-CI, commit, or push action ran. Sanitizers and
   hosted-CI monitoring remain exclusive to Batch 190 because Batch 180 is
   already complete.
7. Proceed only to Batch 183 Change 4: implement the revised VHDL-2019
   protected-type declaration, body, and method rules with strict older-profile
   isolation. Do not run Release, sanitizers, hosted CI, commit, or push before
   Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 4

1. Batch 183 Changes 1-4 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 protected declarations now retain bounded generic interfaces,
   public variable members, explicitly private members, and aliases to methods.
   `private` becomes reserved only in the 2019 profile. Legacy protected-body
   variables remain implicitly private, and declaration/body merging keeps
   declaration-owned state and aliases while installing conforming bodies.
3. Protected method parameters may use file class or locally declared access,
   file, and protected subtypes under 2019. The same forms, protected generics,
   declaration variables, explicit privacy, and method aliases diagnose under
   VHDL-2008. Ordinary retained protected methods and bodies are unchanged.
4. The frontend and semantic models carry protected generics, variable
   visibility, and alias targets. Type identity and the existing generated,
   substitution, static-folding, and qualified-name traversals include the new
   generic interface without introducing a compatibility representation.
5. V19-B183-C04 is preserved. The inventory has 34 active/three preserved rows
   at normalized SHA-256
   `d755311bb5d0b3a624bf4b2a52082e7e58091b9e24f9bdfdec1642510126a075`.
   No paths were added, so the source-package manifest remains at 1,783 paths
   and SHA-256
   `216487cc9516f15f0e86725fcd6cb7837444995284617a673b1ae39b586f2d26`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug frontend target builds
   through 170 impact steps and passes directly. The broader fsim target builds
   through 454 impact steps with eight workers. The focused frontend,
   portable/design artifact, VHDL type-generic, and protected-runtime slice is
   5/5 green in 0.63 seconds. The post-documentation diagnostics, line-budget,
   source-manifest, VHDL inventory, resource-portability, and CTest-uniqueness
   governance slice is 6/6 green in 12.92 seconds.
7. No Release, sanitizer, hosted-CI, commit, or push action ran. Sanitizers and
   hosted-CI monitoring remain exclusive to Batch 190 because Batch 180 is
   already complete.
8. Proceed only to Batch 183 Change 5: implement VHDL-2019 unspecified scalar
   and composite type categories with unique-inference constraints and strict
   older-profile isolation. Do not run Release, sanitizers, hosted CI, commit,
   or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 5

1. Batch 183 Changes 1-5 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. The frontend models all nine VHDL-2019 incomplete type categories: private,
   scalar, discrete, integer, physical, floating, array, access, and file.
   Nested array index/element and access/file designated profiles are retained,
   and an optional implicit type name is accepted on an object interface.
   Stable source-derived identities bind every name from one interface
   declaration to one inferred type.
3. Explicit interface-type generic actuals are checked against their category
   and nested profile. VHDL function and procedure overload selection requires
   typed actuals, enforces shared inference identities, and specializes the
   selected callable frame to concrete argument types before allocation and
   lowering. The semantic HIR retains classified subtype profiles and records
   the uniquely selected callable plus its inferred type identities.
4. Child port associations infer concrete formal types before ordinary array-
   shape adaptation, connection validation, signal creation, and process
   lowering. Multiple ports sharing one implicit formal must infer the same
   concrete subtype. The inferred identity also participates in specialization
   identity, preventing cache aliasing across distinct actual port types.
5. Stable diagnostics are `FSIM-VHDL-PARSE-286` for malformed profiles,
   `FSIM-ELAB-VHUNSPEC-001` for ambiguous, conflicting, or indeterminate
   callable/port inference, and `FSIM-ELAB-VHUNSPEC-002` for an explicit
   interface-type actual outside its declared category. Older profiles reject
   all classified and inline forms through `FSIM-FE-VHSTD-003`.
6. Independently authored coverage includes every category, nested and named
   inline profiles, category acceptance, shared identity, malformed input,
   VHDL-2008 isolation, semantic HIR inference, function/procedure execution,
   inferred child-port execution, conflicting port actuals, and incompatible
   explicit generic actuals. V19-B183-C05 is preserved. The inventory has 33
   active/four preserved rows at normalized SHA-256
   `c80586a605aa0cb6518a9daff42c934ca5ae3c61566b79bfcc4b261b72e3c8d6`.
7. No paths were added, so the source-package manifest remains at 1,783 paths
   and SHA-256
   `216487cc9516f15f0e86725fcd6cb7837444995284617a673b1ae39b586f2d26`.
   The exact LLVM 22.1.8 Clang warnings-as-errors Debug fsim target completed a
   454-step impact rebuild plus a 17-step inferred-port follow-up with eight
   workers. The final focused frontend, portable/design artifact,
   type-generic, diagnostics, line-budget, source-manifest, VHDL inventory,
   resource, and CTest-uniqueness slice passed 10/10 in 13.29 seconds.
   The final post-documentation governance slice passed 6/6 in 13.66 seconds.
8. No Release, sanitizer, hosted-CI, commit, or push action ran. Sanitizers and
   hosted-CI monitoring remain exclusive to Batch 190 because Batch 180 is
   already complete.
9. Proceed only to Batch 183 Change 6: enforce the VHDL-2019 predefined
   `INTEGER` minimum signed 64-bit range while keeping older profiles and every
   existing executable 32-bit limit explicit. Do not run Release, sanitizers,
   hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 6

1. Batch 183 Changes 1-6 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 predefined `INTEGER`, `NATURAL`, and `POSITIVE` use a signed
   64-bit representation. `INTEGER` spans the complete signed 64-bit range;
   all earlier profiles retain the existing portable signed 32-bit range.
   The profile-selected width and range survive frontend/semantic types,
   portable objects, designs, caches, constants, defaults, hierarchy,
   callable frames, and SimIR without adding a compatibility reader.
3. Static evaluation accepts the minimum signed literal directly, protects
   endpoint successor/predecessor operations from host overflow, and computes
   representable lengths wider than 32 bits. Interpreter and exact LLVM
   lowering execute checked 32- and 64-bit unary/binary arithmetic and range
   checks with matching failure behavior.
4. Dynamic VHDL array selection now performs absolute index arithmetic in the
   profile-selected 32- or 64-bit domain, validates the declared bounds, and
   normalizes far absolute indices to bounded packed offsets before narrowing
   into the retained selection ABI. Independently authored execution covers a
   small array based above 2^32, including a dynamic element read, dynamic
   slice, and dynamic target write.
5. The application witness runs the 2019 design in the interpreter and the
   compiled engine at O0 and O2. It proves both signed endpoints, wide add and
   multiply, negative values, a 6,000,000,001-element subtype length, checked
   maximum-plus-one overflow, far-bound selection, and VHDL-2008 rejection.
   The focused frontend, runtime, LLVM, elaboration, portable/design artifact,
   artifact-phase, and application checks are green.
6. V19-B183-C06 is preserved. The inventory has 32 active/five preserved rows
   at normalized SHA-256
   `b637aec2c93a22a504edd7bf5be5c3542970f7a49946e93bde833b98952cf961`.
   The new application evidence advances the source-package manifest to 1,784
   ordered payload paths and SHA-256
   `aa4eaeda53d6df493b27ad5a9c5d38376f59f8c4a982783db67d25ec51532f11`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact build completes
   with eight workers. Direct inventory, source-manifest, resource-portability,
   and whitespace checks are clean. The final post-documentation diagnostics,
   line-budget, source-manifest, VHDL inventory, resource-portability, and
   CTest-uniqueness slice passes 6/6 in 13.20 seconds. No Release, sanitizer,
   hosted-CI monitoring, commit, or push ran.
8. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
9. Proceed only to Batch 183 Change 7: parse and model VHDL-2019 interface view
   declarations while preserving strict older-profile isolation. Do not run
   Release, sanitizers, hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 7

1. Batch 183 Changes 1-7 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 mode-view declarations parse in package, entity, architecture,
   generate, process, function, and procedure declarative regions. The
   frontend retains the unresolved record subtype, expands grouped element
   names, and distinguishes direct `in`, `out`, `inout`, and `buffer` modes
   from nested record-view and array-view indications.
3. The semantic HIR publishes a distinct mode-view declaration with owned
   subtype, element, direction, referenced-name, and source nodes. Referenced
   prior views resolve to their declaration identity, and the model remains
   valid after parsed-unit storage is released. Recursive record-view
   composition and cycle handling remain Change 8 work.
4. Mode views are declarations rather than types. Every unit, statement,
   local-region, and generate-body type-environment path skips them, preventing
   a view name from being selected as a subtype mark while retaining the view
   declaration for HIR construction.
5. Stable diagnostics cover malformed syntax (`FSIM-VHDL-PARSE-287`), duplicate
   declarations or elements and mismatched ending names (`FSIM-VHDL-SEM-107`),
   and illegal linkage mode (`FSIM-VHDL-SEM-108`). `view` is reserved only in
   VHDL-2019, and VHDL-2008 rejects the complete declaration through the
   standard-profile gate.
6. V19-B183-C07 is preserved. The inventory has 31 active/six preserved rows
   at normalized SHA-256
   `5267fe5d1394f5a5d18f94ebe8d71dd59c953bba3f4559537fb93753bc34a8fd`.
   No source paths were added, so the source-package manifest remains at 1,784
   ordered payload paths and SHA-256
   `aa4eaeda53d6df493b27ad5a9c5d38376f59f8c4a982783db67d25ec51532f11`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact build completes
   with eight workers. The frontend executable and focused VHDL type-generic
   HIR/application test pass. The pre-documentation diagnostics, line-budget,
   source-manifest, VHDL inventory, resource-portability, and CTest-uniqueness
   slice passes 6/6 in 13.20 seconds; the final post-documentation rerun passes
   6/6 in 13.16 seconds, and `git diff --check` is clean.
8. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
9. Proceed only to Batch 183 Change 8: compose record views recursively while
   preserving each element direction and subtype. Do not run Release,
   sanitizers, hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 8

1. Batch 183 Changes 1-8 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. `src/app/application_vhdl_mode_view.cpp` owns semantic mode-view
   composition. It resolves each root record subtype through semantic IDs,
   follows subtype/alias chains, annotates every element with its immediate
   subtype, and recursively materializes nested record and array-element view
   trees without re-parsing source names.
3. Each composed node retains its element name, source, form, referenced view
   declaration, immediate subtype, direction, and children. Direct leaves
   retain their modes; record nodes retain recursively composed record
   elements; array nodes retain the array subtype and the referenced record
   element tree.
4. Profiles publish explicit uncomposed, complete, invalid, and recursive
   composition states. A three-state traversal terminates self and indirect
   cycles, never copies an incomplete referenced tree, and lets Change 10 add
   exact legality diagnostics without reconstructing semantic structure.
5. VHDL subtype HIR definitions now retain the base type mark written after
   `is` rather than replacing it with the new subtype's declaration identity.
   The nested-view witness proves that a record element declared through such
   a subtype remains visibly typed by that subtype while composition reaches
   the terminal record structure.
6. The first inline implementation exceeded the 2,500-line hard limit for the
   primary HIR builder. The composition pass was extracted rather than
   weakening the gate; `application_vhdl_hir.cpp` is below the limit and the
   new focused module is 182 lines.
7. Independently authored application evidence covers two nested record
   levels, grouped nested elements, subtype-chain traversal, array-element
   view composition, leaf directions/subtypes, semantic name identity, owned
   storage after parsed-unit release, and recursive-state containment.
8. V19-B183-C08 is preserved. The inventory has 30 active/seven preserved rows
   at normalized SHA-256
   `b0c4e392e79b78762fb20e0861b45f0055a0d8b2a257e7c2006abf5524bd8992`.
   The new implementation path advances the source-package manifest to 1,785
   ordered payload paths and SHA-256
   `fbe264e2c835d6400e316ac9ab2cf22b229638213816bfc1687175e41ac0c0ea`.
9. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact build completes
   with eight workers, and the focused VHDL type-generic HIR/application
   witness passes. The final post-documentation diagnostics, line-budget,
   source-manifest, VHDL inventory, resource-portability, and CTest-uniqueness
   slice passes 6/6 in 13.11 seconds, and `git diff --check` is clean.
10. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
    is complete, so the next and final such boundary in this plan is Batch
    190; Batch 183 Change 20 receives only clean Clang Debug/Release
    qualification, documentation, one implementation commit, and one push.
11. Proceed only to Batch 183 Change 9: implement view-based port declarations
    and associations using the composed semantic tree. Do not run Release,
    sanitizers, hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 9

1. Batch 183 Changes 1-9 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 entity and component port lists accept record form `view name`
   and array form `view (name)`, each with an optional explicit `of` subtype.
   The frontend retains the indication form and source separately from the
   scalar port direction, which remains unknown because direction belongs to
   the composed leaves.
3. Semantic HIR interface declarations bind their view name through lexical,
   selected-package, or use-clause visibility. Each owns the canonical view
   declaration identity, optional inferred record subtype, composition state,
   and declaration-order leaf direction tree. Unknown or ambiguous references
   are rejected before elaboration.
4. Elaboration keeps mode views as declarations rather than named types while
   preserving them across package import and the isolated entity-interface
   and component-specialization lifecycles. Imported and selected spellings
   canonicalize to the resolved declaration name. Component/entity profile
   conformance compares view form, identity, leaf paths, and leaf directions
   in addition to the effective subtype.
5. A whole-record port association records one `VhdlModeViewBinding` on the
   actual signal. It contains the formal interface path and one formal/actual
   hierarchy-path pair per composed leaf in declaration order. The existing
   scalar boundary validator is deliberately bypassed for this composite
   metadata path. Batch 184 Changes 1-2 still own materializing directional
   runtime endpoints and enforcing writes at those endpoints.
6. Stable diagnostics are `FSIM-VHDL-PARSE-288` for a malformed array-view
   delimiter, `FSIM-VHDL-SEM-109` for an unknown or ambiguous semantic view,
   and `FSIM-ELAB-VHVIEW-001`/`002` for missing or incomplete elaboration
   profiles.
7. Independently authored evidence declares a package record and mixed
   direction view, uses inferred subtype syntax on the entity and explicit
   subtype syntax on its component, binds them through a named association,
   and proves the resulting request/response leaf paths and directions.
8. V19-B183-C09 is preserved. The inventory has 29 active/eight preserved rows
   at normalized SHA-256
   `bb1a7ceac9323d8f82faae9e4c459254c02d854cf0ab16fd914a9b330f878e39`.
   No source path was added, so the source-package manifest remains at 1,785
   ordered payload paths and SHA-256
   `fbe264e2c835d6400e316ac9ab2cf22b229638213816bfc1687175e41ac0c0ea`.
9. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact build completes
   with eight workers. The focused elaboration test passes, and the
   diagnostics, line-budget, source-manifest, VHDL inventory,
   resource-portability, and CTest-uniqueness governance slice is green.
10. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
    is complete, so the next and final such boundary in this plan is Batch
    190; Batch 183 Change 20 receives only clean Clang Debug/Release
    qualification, documentation, one implementation commit, and one push.
11. Proceed only to Batch 183 Change 10: enforce subtype compatibility and
    nested direction rules within complex view interfaces. Do not run Release,
    sanitizers, hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 10

1. Batch 183 Changes 1-10 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. Semantic mode-view composition now accepts only unresolved record roots and
   requires the declaration to cover every record element. A nested record
   view must have a root compatible with its member subtype; a nested array
   view must have a root compatible with the array element subtype. Base-type
   identity permits legal subtype chains without accepting unrelated records.
3. A record-form view interface requires its explicit subtype to be compatible
   with the view root; an array-form interface requires the explicit array's
   element subtype to be compatible. Record-form subtype omission continues to
   infer the view root. Invalid declaration or interface composition is
   retained as invalid HIR and now makes `check_project` fail transactionally.
4. Elaboration repeats the legality checks over resolved structural types
   before it publishes a leaf-direction map. Recursive composition uses
   `FSIM-ELAB-VHVIEW-002`, invalid roots, missing elements, and incompatible
   nested views use `FSIM-ELAB-VHVIEW-003`, and incompatible record or array
   interface subtypes use `FSIM-ELAB-VHVIEW-004`.
5. Semantic diagnostics are `FSIM-VHDL-SEM-110` for an invalid mode-view
   declaration composition and `FSIM-VHDL-SEM-111` for an invalid interface
   composition. These checks occur after semantic HIR construction and before
   otherwise-valid application state can be returned.
6. Independently authored application evidence covers valid record and array
   interfaces plus incomplete, nested-record-incompatible,
   nested-array-incompatible, and explicit-interface-incompatible negatives.
   Direct elaboration evidence separately proves the invalid-composition and
   incompatible-interface diagnostics.
7. V19-B183-C10 is preserved. The inventory has 28 active/nine preserved rows
   at normalized SHA-256
   `58c7a6979872bd0877ca7a693f5a00a1f16182b928c1eaadd774be4cfde6be67`.
   No source path was added, so the source-package manifest remains at 1,785
   ordered payload paths and SHA-256
   `fbe264e2c835d6400e316ac9ab2cf22b229638213816bfc1687175e41ac0c0ea`.
8. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact targets build
   cleanly with eight workers. The focused application and elaboration tests
   pass 2/2. The pre-documentation diagnostics, line-budget, source-manifest,
   VHDL-inventory, resource-portability, and CTest-uniqueness slice passes 6/6
   in 13.29 seconds. The final combined post-documentation rerun passes 8/8 in
   14.64 seconds.
9. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
10. Proceed only to Batch 183 Change 11: implement conditional expressions and
    their contextual typing. Do not run Release, sanitizers, hosted CI, commit,
    or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 11

1. Batch 183 Changes 1-11 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. A VHDL-2019 first-class conditional expression is retained as
   `frontend::ExpressionKind::Conditional` and
   `semantic::vhdl::ExpressionKind::conditional`. Legacy VHDL conditional
   assignments keep their existing synthetic call representation so older
   profiles and statement rewriting do not change.
3. The parser requires VHDL-2019 for first-class `when`/`else` expression
   syntax. Candidate results are retained as condition, selected result, and
   remaining conditional or final result, which preserves chained first-true
   evaluation order.
4. Elaboration propagates the surrounding target or callable result type to
   both arms. Return-type-overloaded calls therefore resolve from context;
   mismatched common base types or widths fail as `FSIM-ELAB-VHCOND-002`.
5. Runtime lowering emits a branch, independently lowered true and false
   blocks, and a join register. Only the selected arm executes. Boolean
   conditions are direct; VHDL-2019 scalar bit and standard-logic conditions
   receive the implicit truth conversion. Malformed/profile failures and
   invalid conditions use `FSIM-ELAB-VHCOND-001` and
   `FSIM-ELAB-VHCOND-003`.
6. Independently authored evidence covers the distinct AST/HIR kind,
   VHDL-2008 isolation, contextual return-type overloads, incompatible arms,
   invalid condition types, chained expressions, and a division-by-zero arm
   that remains unexecuted. The application path passes in interpreter and
   compiled LLVM engines at O0 and O2.
7. V19-B183-C11 is preserved. The inventory has 27 active/ten preserved rows
   at normalized SHA-256
   `3081b0005ba7a0976f5abfccbdf056b968d6590b77156b48753183ee38f2d907`.
   No source path was added, so the source-package manifest remains at 1,785
   ordered payload paths and SHA-256
   `fbe264e2c835d6400e316ac9ab2cf22b229638213816bfc1687175e41ac0c0ea`.
8. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact targets build
   cleanly with eight workers. The pre-documentation governance slice passes
   6/6 in 13.14 seconds; the final focused frontend, elaboration, application,
   and governance rerun passes 9/9 in 14.44 seconds. Release qualification
   remains deferred to Batch 183 Change 20.
9. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch
   190; Batch 183 Change 20 receives only clean Clang Debug/Release
   qualification, documentation, one implementation commit, and one push.
10. Proceed only to Batch 183 Change 12: expose result-array constraints inside
    functions. Do not run Release, sanitizers, hosted CI, commit, or push
    before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 12

1. Batch 183 Changes 1-12 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 function syntax accepts `return result_identifier of type_mark`.
   The frontend records the identifier and creates its implicit subtype as the
   first function declaration. Profile or local conflicts use
   `FSIM-VHDL-SEM-112`; VHDL-2008 and older profiles reject the syntax.
3. Semantic HIR retains the implicit subtype declaration and connects it to
   `CallableProfile::return_identifier`. Callable/type HIR builder methods now
   live in `application_vhdl_hir_callables.tpp` and
   `application_vhdl_hir_types.tpp`; this coherent split reduces
   `application_vhdl_hir.cpp` from 2,506 to 1,921 lines.
4. Elaboration obtains the result subtype from the immediate expected type.
   It requires a compatible, fully constrained, nonempty array and applies the
   concrete type to the specialized result, implicit subtype, and dependent
   local region. Missing or unconstrained context uses
   `FSIM-ELAB-VHRESULT-001`; lost implicit metadata uses
   `FSIM-ELAB-VHRESULT-002`.
5. A focused one-process regression exposed cross-specialization local-register
   aliasing: 4-bit and 8-bit result contexts shared storage keyed only by
   source offset/name. Callable-local keys now include the existing VHDL
   specialization identity, and colliding debug-local names receive the
   callable invocation identity. Repeated calls and recursion within one
   specialization retain the existing frame semantics.
6. Independently authored evidence covers result-subtype locals and
   `'length`, 4-bit/8-bit contexts lowered from one process, VHDL-2008
   isolation, unconstrained contexts, local/profile conflicts, and interpreter
   plus compiled LLVM execution at O0 and O2.
7. V19-B183-C12 is preserved. The inventory has 26 active/eleven preserved
   rows at normalized SHA-256
   `ed5cc47a5ad0d897e7209e38011d78eca66f4b6b196bc1360e9395455649e21d`.
   Two new HIR implementation fragments advance the source-package manifest
   to 1,787 ordered payload paths and SHA-256
   `7b23f1ec49b25dc521e86accd7dad9dd0edd32cb96223cc435a7e2a45edd677b`.
8. The exact LLVM 22.1.8 Clang warnings-as-errors Debug frontend,
   elaboration, and application targets build cleanly with eight workers. The
   focused frontend, elaboration, interpreter/LLVM application, diagnostics,
   line-budget, source-manifest, VHDL-inventory, resource-portability, and
   CTest-uniqueness slice is green. No Release qualification ran.
9. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
10. Proceed only to Batch 183 Change 13: implement revised dynamically
    allocated storage semantics. Preserve all Changes 1-12 work and do not run
    Release, sanitizers, hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 13

1. Batch 183 Changes 1-13 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL access types now carry `deallocate_releases_storage` and
   `reclaim_when_unreachable` through frontend metadata, semantic HIR, and
   access-type specialization identities. VHDL-2008 and older defaults remain
   destructive and simulation-lifetime based.
3. In the VHDL-2019 profile, `DEALLOCATE` nulls its inout access argument but
   does not emit `DeleteContainer`; other aliases can continue designating the
   allocated object. The type is marked for reclamation after the final
   designating value becomes unreachable. Batch 184 Change 3 retains ownership
   of executing that reclamation at deterministic runtime safe points.
4. Independently authored evidence allocates a scalar, copies its access
   value, deallocates the original twice, proves the original is null, and
   dereferences the surviving alias. It passes in interpreter and compiled
   LLVM engines at O0 and O2. Existing VHDL-2008 elaboration evidence now also
   asserts its destructive/no-reachability-reclamation policy.
5. V19-B183-C13 is preserved. The inventory has 25 active/twelve preserved
   rows at normalized SHA-256
   `f5bde7d8bd5ed850df5a5146553c53b48222b6cfa2b44c27339b7fdb038f674f`.
   No source path was added, so the source-package manifest remains at 1,787
   ordered payload paths and SHA-256
   `7b23f1ec49b25dc521e86accd7dad9dd0edd32cb96223cc435a7e2a45edd677b`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact build completes
   with eight workers. Focused VHDL-2019 interpreter/LLVM and retained-profile
   elaboration tests are green; the final frontend, elaboration, application,
   diagnostics, line-budget, source-manifest, inventory, resource, and
   CTest-uniqueness slice passes 9/9 in 13.87 seconds. No Release qualification
   ran.
7. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
8. Proceed only to Batch 183 Change 14: implement sequential block statements
   and nested declarative regions. Preserve all Changes 1-13 work and do not
   run Release, sanitizers, hosted CI, commit, or push before Batch 183 Change
   20.

## Batch 183 active checkpoint - after Change 14

1. Batch 183 Changes 1-14 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 accepts labeled and unlabeled sequential block statements,
   optional `is`, optional closing `block`, matching ending labels, and nested
   blocks. VHDL-2008 and older profiles issue the standard-profile diagnostic.
   Malformed declaration/body delimiters use `FSIM-VHDL-PARSE-289` through
   `FSIM-VHDL-PARSE-293`; unsupported declarative items use
   `FSIM-VHDL-UNSUPPORTED-056`.
3. Each frontend block retains constants, types, aliases, package instances,
   local functions/procedures, variables/files, attributes, groups, and its
   source-ordered statement part. These uncommon collections use
   pointer-dormant storage so ordinary statement nodes do not acquire a vector
   allocation for the new feature.
4. Semantic HIR construction predeclares a distinct scope for every block,
   places the complete declaration set in that scope, and builds nested
   statements beneath it. Local callable bodies and protected-type members use
   the same ownership chain. `application_vhdl_hir_statements.tpp` contains the
   recursive region builder and keeps `application_vhdl_hir.cpp` at 1,924
   lines.
5. The existing lexical `Lowerer::lower_block` seam continues to handle basic
   block variables and scope restoration. Batch 184 Change 4 remains the owner
   of complete execution across wait, return, and exception boundaries,
   including runtime handling of the full local declaration surface.
6. Independently authored frontend evidence covers the complete declaration
   set, both ending forms, nesting, and profile isolation. Application evidence
   confirms separate outer/inner HIR scopes and declaration sets.
7. V19-B183-C14 is preserved. The inventory has 24 active/thirteen preserved
   rows at normalized SHA-256
   `733d98a569ed65c206e8bae2c70447abbffaca4b8530bb1a341427be261f4b5a`.
   One new HIR implementation fragment advances the source-package manifest to
   1,788 ordered payload paths and SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
8. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact targets build
   with eight workers. The final frontend, HIR/application, diagnostics,
   line-budget, source-manifest, inventory, resource-portability, and
   CTest-uniqueness slice passes 8/8 in 13.61 seconds. No Release qualification
   ran.
9. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
10. Proceed only to Batch 183 Change 15: add all VHDL-2019 predefined
    attributes and legality rules. Preserve all Changes 1-14 work and do not
    run Release, sanitizers, hosted CI, commit, or push before Batch 183 Change
    20.

## Batch 183 active checkpoint - after Change 15

1. Batch 183 Changes 1-15 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. VHDL-2019 now retains `INDEX` and `DESIGNATED_SUBTYPE` as subtype-valued
   metadata, `REFLECT` as a reflection-valued predefined attribute, and
   `CONVERSE` as a named mode-view alias. These identities are appended to
   durable enums so existing v3 numeric values remain stable.
3. `INDEX` accepts zero or one locally static dimension, defaults to the first
   dimension, reconstructs predefined INTEGER/NATURAL/POSITIVE index subtypes,
   and preserves a selected constraint. `DESIGNATED_SUBTYPE` resolves the
   single access designated subtype or file element subtype. Invalid prefix,
   arity, staticness, and rank use `FSIM-VHDL-SEM-113` and
   `FSIM-ELAB-VHATTR-009` through `FSIM-ELAB-VHATTR-010`.
4. Converse aliases resolve visible local or imported package views, compose
   recursively, reverse every nested leaf direction, and bind as ordinary
   named views on interfaces. Missing and recursive sources use
   `FSIM-ELAB-VHVIEW-005`; duplicate aliases use `FSIM-VHDL-SEM-114`.
5. The semantic type inventory records scalar `LENGTH`, `RANGE`, and
   `REVERSE_RANGE`, representable-composite `IMAGE` and `VALUE`, and
   type/object `REFLECT` applicability for the 2019 profile. Scalar object
   shorthand result inference and execution cover `POS`, `SUCC`, `PRED`,
   `LEFTOF`, and `RIGHTOF`. Batch 184 Change 12 still owns construction and
   execution of the reflection mirror API.
6. Independently authored evidence covers all four new attribute families,
   HIR applicability inventories, valid and invalid subtype resolution,
   converse interface binding, scalar/object-shorthand execution, exact
   diagnostics, and VHDL-2008 isolation.
7. V19-B183-C15 is preserved. The inventory has 23 active/fourteen preserved
   rows at normalized SHA-256
   `b75297f113aba958de05372ac3ab9f96016cfdafc4018864b1390d0e51254765`.
   No source paths were added, so the source-package manifest remains at 1,788
   ordered payload paths and SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
8. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact targets build
   with eight workers. The focused frontend, HIR/application, diagnostics,
   line-budget, source-manifest, inventory, resource-portability, and
   CTest-uniqueness slice passes 8/8 in 13.60 seconds. No Release
   qualification ran.
9. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
10. Proceed only to Batch 183 Change 16: apply revised overload, visibility,
    and conformance rules. Preserve all Changes 1-15 work and do not run
    Release, sanitizers, hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 16

1. Batch 183 Changes 1-16 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. `vhdl_base_type_profiles_match` now owns result/base-type profile equality,
   while `vhdl_parameter_type_profiles_match` owns parameter-profile equality
   and the VHDL-2019 rule that either corresponding unspecified formal makes
   the parameter types match. Homograph checks use parameter types rather than
   modes, classes, defaults, or subtype constraints.
3. Function and procedure duplicate-profile detection now preserves distinct
   imported declarations and generic specializations, diagnoses direct
   unspecified/concrete homographs with `FSIM-ELAB-VHOVER-003` and
   `FSIM-ELAB-VHOVER-006`, and stable-sorts each candidate vector by retained
   owner, specialization, logical source, and source offsets.
4. Package declaration/body merging, protected-type declaration/body merging,
   generic subprogram declaration/body matching, and interface subprogram
   binding use the shared profile rules. Conforming bodies still require the
   applicable purity, mode, class, and file-object properties. Generic value
   formal declarations retain strict subtype-indication conformance, including
   the existing INTEGER-versus-NATURAL negative case.
5. Generic type specialization assigns deterministic identities to the
   pre-mapping homograph classes. Originally duplicate declarations share an
   identity and remain errors; originally distinct overloads that become
   homographs after type mapping retain different identities, stay in the
   overload set, and diagnose an ambiguous call rather than a duplicate.
6. Semantic HIR overload-set declaration identities are explicitly sorted.
   Application evidence verifies both sorted publication and identical
   overload-set order across repeated project checks.
7. Independently authored elaboration evidence covers direct unspecified
   homographs, unspecified package declarations with concrete conforming
   bodies, mapped generic-package homographs, ambiguity, and preserved
   duplicate diagnostics. Scalar `LENGTH` fixtures discovered by the broader
   elaboration test now select VHDL-2019 explicitly and retain the 64-bit
   predefined INTEGER result width.
8. V19-B183-C16 is preserved. The inventory has 22 active/fifteen preserved
   rows at normalized SHA-256
   `2e83563517dac3aab7c0e018c4b0afc628d4b0132950de6d081efe8bcbcf3524`.
   No source paths were added, so the source-package manifest remains at 1,788
   ordered payload paths and SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
9. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact targets build
   with eight workers. The focused frontend, elaboration, HIR/application,
   diagnostics, line-budget, source-manifest, inventory,
   resource-portability, and CTest-uniqueness slice passes 12/12 in 13.87
   seconds. No Release qualification ran.
10. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
    is complete, so the next and final such boundary in this plan is Batch 190;
    Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
    documentation, one implementation commit, and one push. Proceed only to
    Batch 183 Change 17: prevent every VHDL-2019 construct from leaking into
    older profiles. Preserve all Changes 1-16 work and do not run Release,
    sanitizers, hosted CI, commit, or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 17

1. Batch 183 Changes 1-17 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. `ParsedDesign::vhdl_profile_compatible` is transient parse state. Every
   failed VHDL revision gate clears it, and conditional analysis clears it when
   an older profile rejects a recognized directive. Project checking stops
   before standard-library injection or HIR construction when it is false;
   direct elaboration stops in O(1) with `FSIM-ELAB-VHPROFILE-001`.
3. No recovery AST containing unavailable VHDL-2019 syntax can be consumed by
   application HIR or direct elaboration under an older profile. Scalar
   object-prefix shorthand for `POS`, `SUCC`, `PRED`, `LEFTOF`, `RIGHTOF`, and
   `IMAGE` now participates in the same revision gate rather than relying only
   on a later shape diagnostic.
4. Independently authored frontend evidence runs the protected-generic,
   unspecified-type, mode-view, view-interface, conditional-expression,
   named-result-constraint, sequential-block, `INDEX`, `REFLECT`, `CONVERSE`,
   object-shorthand, and conditional-analysis rejection matrix under every
   VHDL-1987, 1993, 2000, 2002, and 2008 profile. It also preserves the older
   32-bit predefined `INTEGER` model and older access-storage policy.
5. Direct elaboration evidence rejects the VHDL-2019 scalar `LENGTH` addition
   under all five older profiles and proves that a recovered object-shorthand
   node is stopped by the preflight guard. The retained VHDL-2008 enumeration
   application now computes scalar counts with `POS(HIGH)-POS(LOW)+1` rather
   than depending on scalar `LENGTH`.
6. Rebuilding the complete VHDL application slice exposed an accumulated
   Change 6 compatibility regression in ordinary dynamic array indices. Small
   32-bit VHDL arrays again use the direct `DynamicIndex` runtime path, keeping
   the established range diagnostic and avoiding normalization instructions;
   only 64-bit or far-bound indices use the new normalized path. Interpreter
   and compiled O0/O2 failure evidence passes.
7. V19-B183-C17 is preserved. The inventory has 21 active/sixteen preserved
   rows at normalized SHA-256
   `5163544b89393fee45605ef0683c7f78b146f745aedad23069111c2f2b65f719`.
   No source paths were added, so the source-package manifest remains at 1,788
   ordered payload paths and SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
8. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact targets build
   with eight workers. The complete VHDL application slice passes 29/29 in
   4.05 seconds. The focused frontend, elaboration, diagnostics, line-budget,
   source-manifest, VHDL inventory, resource-portability, and CTest-uniqueness
   slice passes 8/8 in 7.49 seconds. `git diff --check` is clean. No Release
   qualification ran.
9. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
10. Proceed only to Batch 183 Change 18: round-trip every new semantic form
    through v3 objects and elaborated designs. Preserve all Changes 1-17 work
    and do not run Release, sanitizers, hosted CI, commit, or push before Batch
    183 Change 20.

## Batch 183 active checkpoint - after Change 18

1. Batch 183 Changes 1-18 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. The explicit portable and standalone-design archives now retain every
   VHDL-2019 frontend/HIR field added by Changes 2-17. In particular, frontend
   `Type` archives include predefined-subtype attribute identity and optional
   dimension, `SignalDeclaration` archives include view indications, and the
   design codec admits and validates mode-view declarations and composition.
3. Portable owning-unit/class schema 27, portable-library schema 11,
   runtime-state schema 51, and VHDL-HIR schema 3 own the changed layouts.
   Stale and future schemas reject directly; there is no compatibility reader,
   migration, fallback, or dual writer. The current ABI reference and the
   portable-object, design/library, nested-portable, and stale-schema contracts
   all carry the same identities and pinned digests.
4. Independently authored portable-unit evidence round-trips the 64-bit
   predefined integer model, subtype-valued `INDEX`, unspecified inference,
   revised access lifetime, protected generics/private state/aliases, nested
   and converse views, a view-based port, named function result, conditional
   expression, and sequential block region with byte-identical
   reserialization. Design-HIR and runtime-state evidence covers the same
   semantic forms plus elaborated view bindings and corrupt-enum rejection.
5. V19-B183-C18 is preserved. The inventory has 20 active/seventeen preserved
   rows at normalized SHA-256
   `c8d2afc6b1cc509a62cc82199dd0c2d90dad16f66f5be9b7e0863514fcf38d45`.
   No source path was added, so the source-package manifest remains at 1,788
   ordered payload paths and SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug impact targets build
   with eight workers. The focused artifact and governance slice passes 11/11,
   the complete schema label passes 15/15, and the complete VHDL application
   slice passes 29/29 in 4.30 seconds. `git diff --check` is clean. No Release
   qualification ran.
7. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
8. Proceed only to Batch 183 Change 19: add positive, negative, recovery, and
   profile-differential tests for every new VHDL-2019 frontend form. Preserve
   all Changes 1-18 work and do not run Release, sanitizers, hosted CI, commit,
   or push before Batch 183 Change 20.

## Batch 183 active checkpoint - after Change 19

1. Batch 183 Changes 1-19 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `6258a2a9e608cd80dda96261861cd56874308aae`.
2. The independently authored frontend suite has detailed positive and
   targeted-negative owners for every VHDL-2019 form introduced in Changes
   2-17. The existing profile matrix rejects twelve distinct 2019-only forms
   under each of VHDL-1987, 1993, 2000, 2002, and 2008, and separately proves
   the older 32-bit predefined integer and access-storage policies.
3. `test_vhdl_2019_frontend_recovery_corpus` adds eleven malformed parser
   families: protected members, unspecified types, mode views, view
   interfaces, conditional expressions, result subtype identifiers,
   sequential blocks, subtype-valued attributes, converse views, reflection,
   and conditional analysis. Each case requires its targeted diagnostic,
   retains `vhdl_profile_compatible` for the selected 2019 profile, and proves
   parsing resumes at a following independent entity.
4. The resource-portability contract pins the recovery entry point, every
   family name, the profile-compatible requirement, and the independent-unit
   synchronization assertion. V19-B183-C19 is preserved. The VHDL-2019
   inventory has 19 active/eighteen preserved rows at normalized SHA-256
   `2dfcaeeb274f240014cb12a9ccaf0f7c8889eaae6291dfcfb1c35b5289744a1f`.
   No source path was added, so the source-package manifest remains at 1,788
   ordered payload paths and SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
5. The exact LLVM 22.1.8 Clang warnings-as-errors Debug frontend target builds
   with eight workers in 28 steps, and the complete frontend executable and
   CTest owner pass. `git diff --check` is clean. No Release qualification ran.
6. Sanitizers and hosted-CI monitoring run only every ten batches. Batch 180
   is complete, so the next and final such boundary in this plan is Batch 190;
   Batch 183 Change 20 receives only clean Clang Debug/Release qualification,
   documentation, one implementation commit, and one push.
7. Proceed only to Batch 183 Change 20: run the standard clean Clang
   Debug/Release batch closure, freeze the frontend inventory, document the
   result, create one implementation commit, and push it. Do not run
   sanitizers or monitor hosted CI at this batch boundary.

## Batch 183 closure checkpoint

1. Resume on `codex/v3` with Batch 183 complete. The frontend implements the
   governed VHDL-2019 syntax, type, interface, expression, profile-isolation,
   artifact, and recovery surface. The inventory retains nineteen preserved
   Batch 183 rows and nineteen active Batch 184 runtime rows at normalized
   SHA-256
   `2dfcaeeb274f240014cb12a9ccaf0f7c8889eaae6291dfcfb1c35b5289744a1f`.
2. Change 20 completed clean eight-worker warnings-as-errors builds with the
   exact LLVM 22.1.8 Clang toolchain for both Debug and Release. The complete
   Debug suite passes 405/405 in 142.75 seconds; the complete Release suite
   passes 405/405 in 149.89 seconds.
3. Closure refreshed the frozen repository evidence to 2,638 production
   diagnostics, 1,424 bounded authored sources, 1,723 SPDX-owned files, 534
   conformance test/control files, and 745 release-audit test/control files.
   All composed audit owners pass in both full suites.
4. A retained expression application used VHDL-2019-only conditional and case
   expressions while declaring VHDL-2008. It now selects VHDL-2019 explicitly
   and expects the governed 64-bit predefined `INTEGER` values. A debugger
   comparison confirmed that interpreter and compiled execution produced the
   same value vector before the stale expectations were corrected.
5. The source-package manifest remains at 1,788 ordered payload paths with
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
   Direct VHDL-2019 inventory, resource-portability, source-manifest, and
   whitespace checks are clean.
6. Batch 183 is not a tenth-batch boundary, so no sanitizer lane and no
   hosted-CI monitoring ran. Sanitizers and hosted-CI monitoring run only at
   Batch 180 and Batch 190 in this plan.
7. After the single Batch 183 implementation commit and push, proceed only to
   Batch 184 Change 1: elaborate interface views and nested directional
   connections. Keep the two private LRMs read-only and do not copy, quote,
   hash, log, package, or record their paths in repository artifacts.

## Batch 184 active checkpoint - after Change 1

1. Batch 183 is committed and pushed at
   `3eca918e8093d686f2135f5d0bef07e83923542d`. Batch 184 Change 1 is complete
   and intentionally uncommitted on `codex/v3`; preserve the accumulated
   worktree through the batch's Change 20 commit boundary.
2. `VhdlModeViewElementBinding` now owns an explicit signal ID, packed LSB
   offset, and width in addition to its concrete formal/actual paths, leaf
   direction, and source span. `materialize_vhdl_mode_view_endpoints` expands
   composed record and constrained-array paths only after specialization, so
   no runtime consumer needs to reconstruct a wildcard `(<>)` path from type
   metadata.
3. Endpoint expansion supports nested records, arrays, and multidimensional
   concrete constraints in declared index order. Null arrays are empty;
   unconstrained/incomplete layouts, missing members, overflow, out-of-range
   storage, and more than 65,536 endpoints fail transactionally under
   `FSIM-ELAB-VHVIEW-006`. Ordinary boundary type checks now precede
   view-endpoint publication.
4. `ElaboratedDesign::from_state` rejects endpoint records with wrong signal
   ownership, empty paths, zero/out-of-range widths, duplicate offsets, or an
   excessive endpoint count. Runtime-state schema 52 replaces schema 51
   directly. The artifact test round-trips the new fields and rejects a
   malformed zero-width endpoint.
5. The elaboration corpus proves a nested bus view containing a nested pair
   view and a constrained array view. Its eight endpoints have exact concrete
   hierarchy paths, directions, signal IDs, widths, and packed offsets from 7
   through 0. The simple record-view case now proves the same explicit range
   contract.
6. V19-B184-C01 is preserved. The VHDL-2019 inventory has eighteen active/19
   preserved rows at normalized SHA-256
   `3a97b341d221b007f24bd9277896ef984ed63b82284e13b87aae38aabaadbb0f`.
   No source path was added, so the source-package manifest remains at 1,788
   ordered payload paths and SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug elaboration and
   application targets build with eight workers. The focused elaboration,
   artifact, schema, inventory, diagnostics, manifest, and portability gates
   pass; `git diff --check` is clean. No Release, clean-first, sanitizer,
   hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 2: execute view-based signal and variable
   updates with element-direction enforcement. Sanitizers and hosted-CI
   monitoring remain reserved for Batch 190.

## Batch 184 active checkpoint - after Change 2

1. Batch 184 Changes 1-2 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. `Lowerer::validate_vhdl_mode_view_write` reconstructs VHDL selected
   assignment targets through record-member, index, and slice expressions,
   scopes them to the active hierarchy, and compares them with the concrete
   endpoint map. A write intersecting an input leaf fails under
   `FSIM-ELAB-VHVIEW-007`; output, buffer, and inout leaves lower normally.
   Whole-view writes are rejected when any covered leaf is input.
3. Direction comparison removes array-index spelling while endpoint execution
   retains exact Change 1 signal IDs and packed ranges. Consequently static
   and dynamic selections obey one view rule without folding distinct runtime
   elements together. Disjoint view-leaf process drivers on an unresolved
   composite are accepted, while overlapping regions still fail the ordinary
   multiple-driver audit.
4. The independently authored application uses a local record variable to
   stage and update a selected value, reads `channel.response`, and drives
   `channel.request`. Interpreter, cold compiled, and warm compiled execution
   produce `11` at LLVM O0 and O2 with identical time/delta state and the
   expected two compiled modules. The elaboration corpus separately rejects an
   input-leaf write and admits legal nested record/array output-leaf writes.
5. The diagnostic catalog now owns 2,640 production codes. V19-B184-C02 is
   preserved, leaving seventeen active/20 preserved VHDL-2019 rows at
   normalized SHA-256
   `9961510657913d975ea546f7bee4e753864b965c8ea7c646745311ac1b8e6b9d`.
   The source-package manifest remains at 1,788 ordered payload paths and
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug elaboration and
   application targets build with eight workers. Focused interpreter/compiled
   execution, direction-negative, schema, inventory, diagnostics, manifest,
   portability, and whitespace checks pass. No Release, clean-first,
   sanitizer, hosted-CI, commit, or push action ran.
7. Proceed only to Batch 184 Change 3: execute the revised allocation and
   automatic-reclamation behavior at deterministic safe points. Sanitizers and
   hosted-CI monitoring remain reserved for Batch 190.
8. Cadence correction: sanitizer execution and hosted-CI monitoring occur only
   at Batch 180 and Batch 190. Release-closing Batches 188, 191, 193, 195, and
   197 run their required local qualification and artifact work without adding
   either lane.

## Batch 184 active checkpoint - after Change 3

1. Batch 184 Changes 1-3 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL-2019 access assignments snapshot the displaced handle, publish the new
   access value, and then conditionally erase the owning heap entry only when
   the non-null handle is absent from every visible same-nominal-type local
   root. Root registers are sorted before lowering, keeping SimIR and compiled
   cache identity independent of unordered-map iteration.
3. `Deallocate` now nulls its variable before running that same safe point.
   Automatic nested-block and callable locals are nulled and swept at common
   epilogues, while returned values, arguments, outer locals, and static state
   remain roots. The implementation reuses existing validated SimIR operations,
   so no artifact or runtime-state schema changed.
4. Independently authored one-object-limit tests prove actual reclamation and
   reuse after final-root loss, plus preservation and allocation failure while
   an alias survives. The retained pre-2019 exhaustion case remains unchanged.
   Application evidence exercises aliasing, repeated `Deallocate`, root loss,
   new allocation, and dereference under interpreter and compiled LLVM O0/O2.
5. V19-B184-C03 is preserved, leaving sixteen active/21 preserved VHDL-2019
   rows at normalized SHA-256
   `ee56709a3785082e5bb61e5725784a850fe7214685735301461b7d6f884bd8cc`.
   The source-package manifest remains 1,788 paths at SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug elaboration and
   application targets build with eight workers. The focused diagnostics,
   source-package, VHDL-2019 inventory, ABI reference, nested-schema,
   portability, elaboration, and application gates pass 8/8 in 5.88 seconds;
   `git diff --check` is clean. No Release, clean-first, sanitizer, hosted-CI,
   commit, or push action ran.
7. Proceed only to Batch 184 Change 4: execute sequential blocks across wait,
   return, and exception boundaries. Sanitizers and hosted-CI monitoring remain
   reserved exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 4

1. Batch 184 Changes 1-4 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. Sequential-block registers are initialized on block entry, retained while a
   wait suspends the process, and reused when execution resumes after that wait.
   A later loop re-entry runs the initializers again for the new activation.
3. `Lowerer::lower_block` records function and procedure return sites created
   in the block, rewrites them through a duplicated block cleanup path, and
   replaces them with one continuation return. Nested blocks therefore close
   files and release unreachable access storage from inner to outer scope before
   reaching the callable epilogue. Normal fallthrough executes cleanup once and
   jumps over the return-only cleanup path.
4. The independently authored VHDL-2019 runtime design includes an access-owning
   nested procedure block and observes wait, return, and two-entry values 5, 7,
   and 9 at time 2. Interpreter, cold compiled, and warm compiled LLVM O0/O2
   runs agree. A separate design fails after a wait inside a named block and
   proves unchanged `AssertionError` message/source propagation plus the
   `.failing` lexical debug scope in both engines.
5. V19-B184-C04 is preserved, leaving fifteen active/22 preserved VHDL-2019
   rows at normalized SHA-256
   `aa95be7785cd04c5efbb1693ebcb64e2ec01063e0dd613072604e3d6058ad40a`.
   No source or schema path changed; the source-package manifest remains 1,788
   paths at SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
6. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application and
   elaboration targets build with eight workers. The focused diagnostics,
   source-package, VHDL-2019 inventory, ABI reference, nested-schema,
   portability, elaboration, VHDL-2019 HIR, and projected/sequential-block
   application gates pass 9/9 in 5.62 seconds. The changed lowerer range is
   clang-format clean and `git diff --check` is clean. No Release, clean-first,
   sanitizer, hosted-CI, commit, or push action ran.
7. Proceed only to Batch 184 Change 5: implement the standard simulator API
   additions. Sanitizers and hosted-CI monitoring remain reserved exclusively
   for Batch 190.

## Batch 184 active checkpoint - after Change 5

1. Batch 184 Changes 1-5 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. Exact VHDL-2019 selected-name recognition owns `std.env.stop`,
   `std.env.finish`, and `std.env.resolution_limit`. Older VHDL profiles reject
   those names during parsing, semantic analysis enforces subprogram category,
   arity, and the optional named `status` association, and package import skips
   normal lookup only for these three intrinsic identities.
3. `resolution_limit` produces one scheduler tick in a 64-bit TIME-compatible
   register. `stop` emits the existing resumable `Pause` operation and `finish`
   emits the existing terminal `Stop` operation. Each operation now carries an
   optional signed INTEGER status register. Interpreter and compiled boundary
   handling capture the exact value, LLVM validation and cache identity include
   it, and a compiled boundary synchronizes it to the process frame without a
   new native ABI callback or operation tag.
4. `RunResult::simulator_status` exposes the captured value to the embedding
   application. CLI execution returns values in the portable range 0 through
   255 and diagnoses other values under `FSIM-RUN-VHENV-001`. Runtime-state
   schema 52 remains the only direct v3 schema while the unreleased `Pause` and
   `Stop` archive records acquire this optional field.
5. Independently authored integration evidence observes status 3 from a
   resumable stop at tick 1 and status 7 from a terminal finish at tick 2,
   proves no statement after finish executes, checks exact HIR and SimIR names,
   rejects malformed calls and VHDL-2008 use, and agrees across interpreter,
   Debug, cold/warm LLVM O0, and cold/warm LLVM O2 with native-cache miss/hit
   evidence. Accidental whole-file formatting churn in the function and
   procedure lowerers was removed while preserving the Change 3 reclamation
   epilogues and the Change 5 simulator-call lowering.
6. V19-B184-C05 is preserved, leaving fourteen active/23 preserved VHDL-2019
   rows at normalized SHA-256
   `4d01c0c7b01da691a1c5d84c44c7eee5647b6fab1c97e89e653b8e5300e338a1`.
   No source path was added; the source-package manifest remains 1,788 paths at
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application target
   builds with eight workers. The focused diagnostics, source-package,
   VHDL-2019 inventory, ABI reference, nested-schema, portability, and simulator
   API application gates pass 7/7 in 7.10 seconds; `git diff --check` is clean.
   No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 6: implement the standard data and time
   APIs. Sanitizers and hosted-CI monitoring remain reserved exclusively for
   Batch 190.

## Batch 184 active checkpoint - after Change 6

1. Batch 184 Changes 1-6 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL-2019 `STD.ENV` now provides `DAYOFWEEK`, the concrete 515-bit
   `TIME_RECORD`, no-argument and converting `LOCALTIME`, `GMTIME`, and `EPOCH`
   overloads, every standardized `TIME_RECORD`/`REAL` arithmetic direction,
   `TIME_TO_SECONDS`, `SECONDS_TO_TIME`, and both `TO_STRING` forms. Older
   profiles reject the names, and semantic/lowering checks enforce exact
   categories, named associations, overload types, and result contexts.
3. Project time normalization preserves `@builtin:time` on scaled physical
   literals, and unary numeric type inference preserves the operand type.
   Runtime calendar conversion is range checked and round-trip validated;
   microseconds, weekday, and day-of-year are retained. Decimal real literals
   use binary64 payloads throughout VHDL HIR and SimIR.
4. `VhdlEnvironmentTime` and `VhdlEnvironmentTimeToString` execute through the
   interpreter and the compiled host boundary. LLVM wide-register validation,
   lowering, frame synchronization, native-cache identity, and executor
   classification admit genuine O0/O2 compilation rather than silently
   retaining the interpreter. Runtime-state schema 53 directly replaces 52,
   and VHDL HIR schema 4 directly replaces 3; no compatibility reader exists.
5. Independently authored evidence covers UTC/local fields, calendar
   round trips, arithmetic, conversions, current-time calls, formatting,
   malformed overloads, bounded runtime failures, runtime/HIR serialization,
   Debug execution, and cold/warm compiled O0/O2 cache miss/hit behavior.
6. V19-B184-C06 is preserved, leaving thirteen active/24 preserved VHDL-2019
   rows at normalized SHA-256
   `5f4a0347be412dab2de4eb1e23d37eea14ff48bead985999b93761499b2fc4ac`.
   No source path was added; the source-package manifest remains 1,788 paths at
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application targets
   build with eight workers. The focused simulator API, artifact, diagnostics,
   source-package, VHDL-2019 inventory, ABI/schema, portability, and whitespace
   gates pass 11/11 in 8.47 seconds. No Release, clean-first, sanitizer,
   hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 7: implement the standard directory APIs.
   Sanitizers and hosted-CI monitoring remain reserved exclusively for Batch
   190.

## Batch 184 active checkpoint - after Change 7

1. Batch 184 Changes 1-7 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL-2019 `STD.ENV` now owns `DIRECTORY_ITEMS`, `DIRECTORY`, the four
   standardized status types, every directory procedure/function overload,
   item queries, and `DIR_SEPARATOR`. Older profiles reject the surface;
   semantic and lowering checks enforce call category, associations, writable
   directory/status variables, string-compatible paths, and matching result
   types.
3. Directory paths are converted from UTF-8, weakly canonicalized, and confined
   to the configured project root. The logical working directory never escapes
   that root. Enumeration is sorted, bounded to 4,096 entries and 1 MiB of
   retained text, and preserves the canonical directory name plus item names.
   Root and working-directory ancestors cannot be recursively removed.
4. `VhdlEnvironmentDirectory` carries packed status/Boolean results, strings,
   and directory containers through interpreter, Debug, and compiled host
   execution. Runtime-state schema 54 directly replaces 53; artifacts reject
   stale schemas and native-cache identities include the operation. The JIT
   resume inventory now includes the directory boundary successor, fixing the
   invalid-PC dispatch found by the initial compiled O0 run.
5. Independently authored evidence covers both procedure and function forms,
   deterministic item order, item queries, working-directory changes, create
   and delete behavior, every portable status family, project-root escape
   denial, VHDL-2008 rejection, malformed profiles, wrong-type diagnostics,
   runtime artifact round trips, and interpreter, Debug, cold/warm LLVM O0,
   and cold/warm LLVM O2 execution.
6. V19-B184-C07 is preserved, leaving twelve active/25 preserved VHDL-2019
   rows at normalized SHA-256
   `889605766da8f58d5fb5ab7a64bd2d1f4690cec2d8dca7840f762bec4d2adc9a`.
   No source path was added; the source-package manifest remains 1,788 paths at
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application targets
   build with eight workers. The focused directory, artifact, diagnostics,
   source-package, inventory, ABI/schema, nested-schema, and portability gates
   pass 10/10 in 8.16 seconds; the whitespace gate is clean. No Release,
   clean-first, sanitizer, hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 8: implement the standard environment APIs.
   Sanitizers and hosted-CI monitoring remain reserved exclusively for Batch
   190.

## Batch 184 active checkpoint - after Change 8

1. Batch 184 Changes 1-8 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL-2019 `STD.ENV` now provides both `GETENV` result profiles and the
   `VHDL_VERSION`, `TOOL_TYPE`, `TOOL_VENDOR`, `TOOL_NAME`, `TOOL_EDITION`, and
   `TOOL_VERSION` information functions. Older profiles reject the exact
   intrinsic names; semantic and lowering checks enforce function category,
   arity, the optional named `NAME` association, and string-compatible input.
3. The direct `STRING` result and the `LINE` result observed through `.ALL`
   use the same bounded string value. `GETENV` reads only the requested host
   variable, yields empty for an absent variable, retains neither name nor
   value in an artifact, and rejects embedded nulls or results beyond 4,096
   bytes. Tool and language identities are deterministic constants and expose
   no compiler path, operating-system identity, or other incidental host state.
4. `VhdlEnvironmentGetenv` executes as a scheduler-owned host boundary across
   interpreter, Debug, and compiled execution. LLVM validation, string-frame
   synchronization, resume registration, operation serialization, and native
   cache identity include it. Runtime-state schema 55 directly replaces 54;
   no compatibility reader exists.
5. Independently authored evidence covers a controlled present variable, a
   guaranteed absent variable, both result profiles, all identity functions,
   VHDL-2008 rejection, malformed and wrong-type calls, a 4,097-byte bounded
   failure with identical interpreter/compiled diagnostics, artifact round
   trips, Debug, and cold/warm LLVM O0/O2 execution.
6. V19-B184-C08 is preserved, leaving eleven active/26 preserved VHDL-2019
   rows at normalized SHA-256
   `0d9eff72c9618358e661666484e2ca7440cd2a3e4be24225b6211e2e28faa205`.
   No source path was added; the source-package manifest remains 1,788 paths at
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application targets
   build with eight workers. The focused environment integration, artifact,
   diagnostics, source-package, inventory, ABI/schema, nested-schema, and
   portability gates pass 10/10 in 8.48 seconds; `git diff --check` is clean.
   No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 9: implement current-file, line, and
   call-path APIs. Sanitizers and hosted-CI monitoring remain reserved
   exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 9

1. Batch 184 Changes 1-9 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. Exact VHDL-2019 `STD.ENV` names now provide `FILE_NAME`, `FILE_PATH`,
   `FILE_LINE`, `CALL_PATH_ELEMENT`, `CALL_PATH_VECTOR`,
   `CALL_PATH_VECTOR_PTR`, `GET_CALL_PATH`, and the standardized element,
   vector, and access-value `TO_STRING` profiles. Older profiles reject these
   names, and semantic validation enforces category, arity, named associations,
   result type, and the element overload's single-argument profile.
3. Source identity is derived from the exact call-site span. File names are
   leaf names, file paths retain the normalized source identity, and line
   results are positive 64-bit integers. `GET_CALL_PATH` retains at most 256
   language frames as an owning heterogeneous container, with scope name, file
   name, file path, and line stored separately. Direct record-field selection,
   `.ALL`, vector indexing, saved access values, and both `TO_STRING` overloads
   consume the same representation. Aggregate member-selection results own the
   resolved leaf type rather than borrowing storage from a temporary VHDL type,
   preventing intermittent dereference/index lowering failures.
4. `VhdlEnvironmentGetCallPath` materializes the value and
   `VhdlEnvironmentCallPath` formats either the current or a saved path at a
   scheduler-owned host boundary. Interpreter, Debug, and compiled execution
   share the boundary; LLVM admission keeps callable stack state observable,
   synchronizes container/index/string values, registers resume successors,
   validates the operation profiles, and includes every field in native-cache
   identity. Runtime-state schema 56 directly replaces 55 with no compatibility
   reader.
5. Independently authored evidence covers current source identity, a nested
   function call path, access-value materialization, all four element fields,
   element/vector/access formatting with default and explicit separators,
   VHDL-2008 rejection, malformed and wrong-type calls, artifact round trips,
   Debug, and cold/warm LLVM O0/O2 execution.
6. V19-B184-C09 is preserved, leaving ten active/27 preserved VHDL-2019 rows at
   normalized SHA-256
   `dab7be40f47a6b0664d0c514ed0ed0e24d17971155c927f5f4399bb61df1c527`.
   No source path was added; the source-package manifest remains 1,788 paths at
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application targets
   build with eight workers. The focused source-location integration,
   artifact, diagnostics, source-package, inventory, ABI/schema,
   schema-producer, schema-evidence, nested-schema, and portability gates pass
   11/11 in 9.00 seconds. The environment integration test also passes ten
   consecutive invocations after the leaf-type lifetime repair.
   `git diff --check` is clean. No Release, clean-first, sanitizer, hosted-CI,
   commit, or push action ran.
8. Proceed only to Batch 184 Change 10: implement the standardized PSL API.
   Sanitizers and hosted-CI monitoring remain reserved exclusively for Batch
   190.

## Batch 184 active checkpoint - after Change 10

1. Batch 184 Changes 1-10 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL-2019 `STD.ENV` now owns the exact `PSLASSERTFAILED`, `PSLISCOVERED`,
   `GETPSLCOVERASSERT`, `PSLISASSERTCOVERED`, `SETPSLCOVERASSERT`, and
   `CLEARPSLSTATE` intrinsic names. Older profiles reject them. Semantic and
   lowering checks enforce function/procedure category, the four no-argument
   Boolean query profiles, and the optional named Boolean `ENABLE` actual with
   its true default.
3. The existing scheduler-owned PSL engine is the sole state authority.
   Assertion failures latch until cleared; ordinary cover goals and assertions
   sampled while cover-assert mode is enabled retain separate completion state.
   `PSLISCOVERED` checks every currently enabled cover goal,
   `PSLISASSERTCOVERED` requires that assertion coverage was enabled and every
   assert reached its cover goal, and `CLEARPSLSTATE` restores the monitor
   engine, counters, goals, and controls to post-elaboration values.
4. `VhdlPslApi` is a validated and serialized SimIR host-boundary operation.
   Interpreter, Debug, and LLVM O0/O2 use the same application service. LLVM
   validation constrains every Boolean register, boundary handling synchronizes
   query results and controls, resume successors are admitted, and native-cache
   keys retain the operation kind and optional operands. Runtime-state schema
   57 directly replaces 56; there is no compatibility reader.
5. Independently authored evidence proves initial values, default and named
   control calls, successful cover/assert coverage, a later assertion failure,
   complete clearing, VHDL-2008 rejection, interpreter and Debug execution,
   compiled O0, compiled O2 cold/warm cache reuse, and design-artifact replay.
6. V19-B184-C10 is preserved, leaving nine active/28 preserved VHDL-2019 rows
   at normalized SHA-256
   `736aa35c89ceb1eddb5f528f1d66045ec42edab0648c33a7c457322d02f0cd1f`.
   No source path was added; the source-package manifest remains 1,788 paths at
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application targets
   build with eight workers. The focused PSL API, artifact, diagnostics,
   source-package, inventory, ABI/schema, nested-schema, portability, and
   whitespace gates pass 10/10 in 9.19 seconds. No Release, clean-first,
   sanitizer, hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 11: implement report/assert statement APIs.
   Sanitizers and hosted-CI monitoring remain reserved exclusively for Batch
   190.

## Batch 184 active checkpoint - after Change 11

1. Batch 184 Changes 1-11 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL-2019 `STD.ENV` now owns the standardized report/assert query and
   control surface: `ISVHDLASSERTFAILED`, `GETVHDLASSERTCOUNT`,
   `CLEARVHDLASSERT`, `SETVHDLASSERTENABLE`, `GETVHDLASSERTENABLE`,
   `SETVHDLASSERTFORMAT`, `GETVHDLASSERTFORMAT`, `SETVHDLREADSEVERITY`, and
   `GETVHDLREADSEVERITY`. Older VHDL profiles reject those intrinsic names.
   Semantic analysis and lowering enforce function/procedure category, exact
   named associations, defaults, return types, and writable `VALID` results.
3. Scheduler-owned state retains saturating counts for note, warning, error,
   and failure; aggregate queries deliberately exclude note. It also owns
   per-level enables and formats plus the current TextIO read severity.
   Disabled report/assert statements neither publish nor terminate, clearing
   resets counts without changing controls, and an invalid checked format
   preserves the previous value. TextIO reads without `GOOD` publish failed
   conversions through the same state machine.
4. `VhdlAssertApi` is a validated and serialized SimIR host-boundary
   operation. Interpreter, Debug, and LLVM O0/O2 share the scheduler service;
   compiled VHDL-2019 reports cross the boundary before termination is
   decided. LLVM admission constrains every packed/string operand, validates
   source metadata, synchronizes resume state, and hashes every field into the
   native-cache identity. Runtime-state schema 58 directly replaces 57 with
   no compatibility reader.
5. Independently authored evidence covers defaults, named and positional
   controls, enabled and suppressed reports, formatted severity/message/
   instance/time output, aggregate and per-level counts, clearing, invalid
   format retention, TextIO failure severity, VHDL-2008 rejection, artifact
   replay, Debug, and cold/warm LLVM O0/O2 execution.
6. V19-B184-C11 is preserved, leaving eight active/29 preserved VHDL-2019
   rows at normalized SHA-256
   `c5b1f3c9ba0e18c4b909c439b2c4c64cd97840ec9853a439439d63ee11534b1e`.
   No source path was added; the source-package manifest remains 1,788 paths
   at SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application targets
   build with eight workers. The focused report/assert integration, artifact,
   diagnostics, source-package, inventory, ABI/schema, nested-schema, and
   portability CTest lanes pass 9/9 in 9.03 seconds; `git diff --check` is
   clean. No Release, clean-first, sanitizer, hosted-CI, commit, or push action
   ran.
8. Proceed only to Batch 184 Change 12: implement the reflection API and
   reflected type/value model. Sanitizers and hosted-CI monitoring remain
   reserved exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 12

1. Batch 184 Changes 1-12 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL-2019 `STD.REFLECTION` is compiler-owned and profile gated. Exact
   subtype/value mirror handles cover enumeration, integer, floating,
   physical, record, array, access, file, and protected classes, while generic
   mirrors retain the represented class and subtype descriptor. Older profiles
   continue to reject the `reflect` attribute and reflection-only identities.
3. `VhdlReflectionApi` is a validated SimIR host-boundary operation shared by
   interpreter, Debug, and LLVM O0/O2. It owns mirror creation, generic/typed
   conversion, scalar bounds and images, physical units, record and
   multidimensional array access, designated subtypes/access values, and file
   logical-name/open-kind queries. Access and aggregate value mirrors are
   immutable snapshots, including a designated heap value captured before the
   source access value can change.
4. Recursive reflection descriptors, source metadata, scalar/string operands,
   access heaps, result widths, index arity, descriptor depth/cardinality,
   string bytes, physical scales, and offset arithmetic are bounded and
   validated. Serialization and LLVM native-cache hashing retain the complete
   operation. Runtime-state schema 59 directly replaces 58 with no
   compatibility reader.
5. Independently authored evidence exercises typed and generic conversions,
   enumeration literals/images/snapshots, integer bounds, record and array
   lookup, access snapshots/null state, physical-unit selection, floating,
   protected, and file classes, exact file logical names, artifact replay,
   and interpreter plus cold/warm LLVM O0/O2 execution. The retained frontend
   profile/attribute tests cover malformed and pre-2019 rejection.
6. V19-B184-C12 is preserved, leaving seven active/30 preserved VHDL-2019
   rows at normalized SHA-256
   `bbf48153f27fb96e24f26c8d0dacc771dfb4e7d42cc30d7d047cc9875c071f42`.
   No source path was added; the source-package manifest remains 1,788 paths
   at SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug frontend/application
   targets build with eight workers. The focused frontend, reflection,
   attribute/profile, artifact, diagnostics, source-package, inventory,
   ABI/schema, nested-schema, and portability lanes pass 10/10 in 5.76
   seconds; `git diff --check` is clean. No Release, clean-first, sanitizer,
   hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 13: update predefined packages and governed
   package compilation. Sanitizers and hosted-CI monitoring remain reserved
   exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 13

1. Batch 184 Changes 1-13 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. Every VHDL predefined environment now carries the v3 identity
   `ieee-1076-standard:<year>:fsim-v3`. VHDL-2019 analysis verifies and
   snapshots the compiler-owned `std.standard`, `std.textio`, `std.env`, and
   `std.reflection` sources and rejects project redeclarations. The 2019-only
   `reflect` and `converse` attributes remain isolated from older profiles.
3. Official IEEE projection now includes `numeric_bit_unsigned`,
   `numeric_std_unsigned`, and `math_complex` declarations and bodies with
   exact minimum-profile rules and transitive selection. Existing IEEE, VITAL,
   and Synopsys-compatibility projections remain available under their owning
   profiles.
4. Object, library, design, debugger, and VHPI provenance records every
   directly selected governed package's standard, predefined environment,
   governed revision, and complete declaration/body digest. Loading validates
   official IEEE, VITAL, Synopsys-compatibility, and 2019 `std` dependencies
   against the current compiler and rejects stale or unavailable identities;
   no v2 compatibility reader or migration was added.
5. Validation exposed and repaired one adjacent VHDL string-dispatch
   regression: a quoted literal in a logic-vector `=` or `/=` comparison no
   longer routes through string comparison merely because the literal itself
   is quoted. Actual VHDL-2019 reflected-string comparisons retain their
   dedicated lowering path. Mixed-language provenance tests now expect both
   `ieee.std_logic_1164` and the explicitly selected
   `ieee.std_logic_unsigned` dependency.
6. V19-B184-C13 is preserved, leaving six active/31 preserved VHDL-2019 rows
   at normalized SHA-256
   `0001605683161a29f90709de89a5697152ff769c78c0b838ec73e0fd9c01d1c6`.
   No source path was added; the source-package manifest remains 1,788 paths at
   SHA-256
   `419a5f4a1ea6786c9da50d53ba625941c3dd65f631b3b4e110f1f2ad4a0128f9`.
7. The exact LLVM 22.1.8 Clang warnings-as-errors Debug application target
   builds with eight workers. The focused package/application/artifact slice
   passes 11/11 in 3.83 seconds and the policy slice passes 8/8 in 4.72 seconds;
   `git diff --check` is clean. No Release, clean-first, sanitizer, hosted-CI,
   commit, or push action ran.
8. Proceed only to Batch 184 Change 14: implement revised tool,
   conditional-analysis, and protection directives. Sanitizers and hosted-CI
   monitoring remain reserved exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 14

1. Batch 184 Changes 1-14 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. The VHDL directive prepass now recognizes active-branch `warning` and
   `error` messages, doubled quotes within their string arguments, and both
   `end` and `end if`. Inactive conditional text remains coordinate-preserving
   masked input and cannot publish directive messages. VHDL-2019 owns the
   conditional diagnostic surface without leaking it into older profiles.
3. Protection envelopes are recognized from VHDL-2008 onward. Plaintext
   `begin`/`end` controls are masked while their payload remains compilable at
   original coordinates. Encrypted `begin_protected`/`end_protected` payload is
   masked before lexing and produces one stable unavailable-key-provider
   diagnostic; nested, mismatched, and unterminated controls have distinct
   bounded diagnostics. Protected payload bytes are never reproduced in
   diagnostics or repository evidence. VHDL-1993 and older profiles reject the
   surface deterministically.
4. Independently authored frontend coverage proves active/inactive warning and
   error handling, doubled-quote decoding, malformed messages, optional `if`
   on the closing conditional directive, plaintext coordinate preservation,
   encrypted invalid-source masking and recovery, older-profile rejection,
   and nested, unmatched, and unterminated protection controls.
5. Focused validation exposed accumulated source-line growth from Changes
   1-13. Without changing behavior, the LLVM validation visitor, hierarchy port
   connection implementation, interpreter profiling implementation, and
   VHDL-2019 integration fixture are partitioned into four included `.tpp`
   files at existing semantic boundaries. Their parent files and every authored
   source are now below the 2,000-line refactor target. Resource-policy checks
   explicitly follow the partitioned ownership rather than relaxing tokens.
6. V19-B184-C14 is preserved, leaving five active/32 preserved VHDL-2019 rows
   at normalized SHA-256
   `eb788b4ee7d7bbbc6a4f3a4cd627810364511b139faacebbd771e0a0544082fa`.
   Four partition paths advance the source-package manifest to 1,792 ordered
   payload paths at SHA-256
   `59153de17ad5622e4e021ec24a6787872a5676dccbf0bc005484a32b52a63d1e`.
7. Exact LLVM 22.1.8 Clang warnings-as-errors Debug frontend, application,
   LLVM, elaboration, and runtime targets build with at least eight workers.
   The focused frontend, VHDL integration, artifact, diagnostics, source-line,
   source-package, inventory, and portability lanes pass 8/8 in 9.07 seconds;
   `git diff --check` is clean. No Release, clean-first, sanitizer, hosted-CI,
   commit, or push action ran.
8. Proceed only to Batch 184 Change 15: update VHPI capabilities, information
   model, and property access. Sanitizers and hosted-CI monitoring remain
   reserved for the next tenth-batch boundary, Batch 190.

## Batch 184 active checkpoint - after Change 15

1. Batch 184 Changes 1-15 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHPI capability discovery now reports the VHDL-2019 model revision, exact
   supported object/relationship/property ranges, 32-dimensional index limit,
   256-entry package-provenance limit, and availability of selected names,
   source locations, interface views, and package provenance.
3. Existing numeric identities remain stable. Append-only object kinds cover
   contexts, interfaces, ports, generics, aliases, attributes, enumeration
   literals, physical units, record and array elements, interface views, and
   nested view elements. The append-only `Parent` relationship returns an
   immutable zero-or-one snapshot.
4. Checked property queries return an explicit owned result kind for object
   kind, handle, unsigned integer, or string. They expose parent, live-child
   count, ordinal, names, index count, source coordinates, language standard,
   predefined environment, compatibility profile, and package-dependency
   count. Missing optional values, unsupported properties, stale handles, and
   foreign handles remain distinct errors. Reused slots clear all source and
   provenance metadata before reuse.
5. The application registry now publishes the primary entity declarations for
   an elaborated architecture. View ports retain the `Port` kind and recursively
   publish their composed `ViewElement` hierarchy, with bounded element count
   and exact parent/source identities. Checkpoint and type validation accept
   the new append-only object range.
6. V19-B184-C15 is preserved, leaving four active/33 preserved VHDL-2019 rows
   at normalized SHA-256
   `d540ea4e2da3becb428dbc4421166ee0e79ac500e11cdc7c78af2f795423a7c3`.
   No source path was added; the source-package manifest remains at 1,792 paths
   and SHA-256
   `59153de17ad5622e4e021ec24a6787872a5676dccbf0bc005484a32b52a63d1e`.
7. Exact LLVM 22.1.8 Clang warnings-as-errors Debug runtime and application
   targets build with eight workers. The focused runtime, view-port
   application, inventory, resource, source-line, and source-package slice
   passes 6/6 in 4.33 seconds. No Release, clean-first, sanitizer, hosted-CI,
   commit, or push action ran.
8. Proceed only to Batch 184 Change 16: update VHPI callbacks, value access,
   tool execution, and headers. Sanitizers and hosted-CI monitoring remain
   reserved exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 16

1. Batch 184 Changes 1-16 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. The public VHPI host is append-only ABI v3. Its 80-byte table retains the
   complete 40-byte v1 and 56-byte v2 prefixes, then appends required
   capability-query, typed-value-access, and tool-execution callbacks. All
   three use the already validated service context.
3. Public fixed-width declarations now cover capability flags, the complete
   retained callback reasons plus tool execution, scalar/logic/binary-string
   value formats, read/write access, tool actions, a 48-byte value record, and
   a 32-byte tool request. Property, tool, and capability service families are
   appended as operations 14-16 without renumbering operations 1-13.
4. Host validation requires the complete v3 size and every appended callback.
   Incomplete hosts fail before the dynamic-library open boundary. The
   simulation-owned callback system publishes object-free tool events with a
   copied action identity and bounded request text while retaining stable order,
   re-entry behavior, exception containment, and teardown.
5. Independently compiled C and C++ plug-ins query VHDL-2019 capabilities,
   perform typed integer access, issue a bounded save request, and invoke all
   sixteen service families. Their exact service transcripts remain identical
   across interpreter, compiled O0/O2 labels, repeated loads, and relocated
   image paths. C11/C++20 layout/type probes freeze the new ABI.
6. V19-B184-C16 is preserved, leaving three active/34 preserved VHDL-2019 rows
   at normalized SHA-256
   `8af401517d2b7b3639244a74f783a52ff0ac21776f6d7fe061cdd398c6b0cf24`.
   No source path was added; the source-package manifest remains at 1,792 paths
   and SHA-256
   `59153de17ad5622e4e021ec24a6787872a5676dccbf0bc005484a32b52a63d1e`.
7. Exact LLVM 22.1.8 Clang warnings-as-errors Debug runtime and reference-image
   targets build with eight workers. Runtime, public API/ABI, installed-public,
   foreign-ABI, inventory, resource, source-line, and source-package lanes pass
   8/8 in 4.78 seconds. No Release, clean-first, sanitizer, hosted-CI, commit,
   or push action ran.
8. Proceed only to Batch 184 Change 17: integrate VHDL-2019 constructs with
   code and PSL coverage. Sanitizers and hosted-CI monitoring remain reserved
   exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 17

1. Batch 184 Changes 1-17 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. VHDL statement coverage now enters sequential-block declarative regions.
   Executable statements in block-local functions and procedures, recursively
   nested local callables, and the block body use one bounded traversal with
   lexical source ordering. Callable count, statement count, and nesting depth
   are all constrained before point publication.
3. The retained VHDL profiles share the canonical v3 point-identity path.
   Independently authored VHDL-2019 block fixtures prove the expected return,
   assignment, procedure-call, assignment, and wait points and retain their
   exact IDs after checkout relocation.
4. PSL database bin IDs now hash the authenticated v3 source identity obtained
   from the source binding, not the transient semantic span number. Duplicate
   detection uses the same durable key. Renumbered semantic spans therefore
   retain identical metric records, while a changed authenticated source
   identity changes the associated bins.
5. The VHDL-2019 standardized PSL application matrix compares the complete
   returned coverage records across interpreter, Debug, compiled O0, and
   compiled O2 cold and warm execution.
6. V19-B184-C17 is preserved, leaving two active/35 preserved VHDL-2019 rows
   at normalized SHA-256
   `94ef6a96d80bd2df64eddcfbc24521dea8be765418e6a9533f45c2bf0140c04d`.
   No source path was added; the source-package manifest remains at 1,792 paths
   and SHA-256
   `59153de17ad5622e4e021ec24a6787872a5676dccbf0bc005484a32b52a63d1e`.
7. Exact LLVM 22.1.8 Clang warnings-as-errors Debug application, elaboration,
   and PSL database targets build with eight workers. The focused functional
   slice passes 3/3 in 2.92 seconds. Inventory, resource, source-line, and
   source-package governance passes 4/4 in 4.56 seconds. No Release,
   clean-first, sanitizer, hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 18: prove interpreter, LLVM, Debug,
   artifact, cache, and mixed-language behavior. Sanitizers and hosted-CI
   monitoring remain reserved exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 18

1. Batch 184 Changes 1-18 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. The direct typed-boundary application now configures its VHDL source set as
   VHDL-2019. A SystemVerilog top, VHDL-2019 middle, and native SystemC leaf
   exchange scalar and 137-bit nine-state values under interpreter, Debug,
   LLVM O0/O2, cold/warm cache, VCD, multi-root, reordered-root, and retained
   older Verilog/SystemVerilog-profile paths.
3. That direct matrix compares run status, time/delta, PSL attempts, signal
   values and changes, debugger and waveform output, specialization keys,
   standard/profile provenance, and timing paths. The VHDL-2019 mixed marker
   makes the governed profile explicit.
4. The artifact-phase matrix now constructs its primary mixed `.fsimdesign`
   from the VHDL-2019 object, verifies 2019 object/unit/package provenance, and
   compares interpreter, compiled cold/warm, and Debug results plus portable
   checkpoints. Relocated interpreter and standalone compiled CLI consumers
   continue after the producer object is hidden.
5. The resource-portability contract freezes both matrix owners and their
   VHDL-2019, engine, cache, artifact, relocation, and mixed-language markers.
6. V19-B184-C18 is preserved, leaving one active/36 preserved VHDL-2019 rows
   at normalized SHA-256
   `de39d5aa90f39f2290c472f36a06ac35249ea931d67b2f672d2764291e7fa3d6`.
   No source path was added; the source-package manifest remains at 1,792 paths
   and SHA-256
   `59153de17ad5622e4e021ec24a6787872a5676dccbf0bc005484a32b52a63d1e`.
7. Exact LLVM 22.1.8 Clang warnings-as-errors Debug application targets build
   with eight workers. The typed-boundary matrix passes in 46.53 seconds, the
   artifact-phase matrix passes in 0.66 seconds, and the resource-portability
   contract passes in 4.25 seconds. No Release, clean-first, sanitizer,
   hosted-CI, commit, or push action ran.
8. Proceed only to Batch 184 Change 19: close the final active VHDL-2019 clause
   row and publish independently authored implementation guidance. Sanitizers
   and hosted-CI monitoring remain reserved exclusively for Batch 190.

## Batch 184 active checkpoint - after Change 19

1. Batch 184 Changes 1-19 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains
   `3eca918e8093d686f2135f5d0bef07e83923542d`.
2. `docs/vhdl-2019.md` is the independently authored public guide for profile
   selection/isolation, supported language additions, runtime and predefined
   APIs, code/PSL coverage, VHPI v3, standalone artifacts, mixed-language
   execution, and deliberate boundaries.
3. The inventory checker requires the guide and its complete section structure,
   rejects private-reference path/material tokens from both guide and ledger,
   and verifies that the deterministic source manifest contains the guide.
4. The public feature matrix no longer broadly defers post-2008 VHDL. It links
   the completed VHDL-2019 ledger and guide while continuing to defer VHDL-AMS,
   PSL beyond the governed digital surface, and vendor-only extensions. The
   feature-matrix authoring contract records all 37 rows as preserved.
5. V19-B184-C19 is preserved. The VHDL-2019 inventory has zero active/37
   preserved rows at normalized SHA-256
   `87e90ceea9effba9b2fbbb44a63152bb11f07d4afb674d71c43ae667afe30dbe`.
   Adding the guide advances the source-package manifest to 1,793 paths and
   SHA-256
   `ed17b79a627158eb101a7ea2f5a7275d52aebf7d004d3aa59f08dbd5a750bd9d`.
6. The focused inventory and source-package lanes pass 2/2 in 1.13 seconds.
   No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
7. Proceed only to Batch 184 Change 20: run clean Clang warnings-as-errors
   Debug and Release qualification, update the closure record, make the one
   implementation commit, and push it. Do not run sanitizers or monitor hosted
   CI; both remain reserved exclusively for Batch 190.

## Batch 184 closure checkpoint

1. Batch 184 Changes 1-20 are complete on `codex/v3`. The complete 37-row
   independently authored VHDL-2019 inventory is preserved at normalized
   SHA-256
   `87e90ceea9effba9b2fbbb44a63152bb11f07d4afb674d71c43ae667afe30dbe`.
2. Clean Clang 22/LLVM 22.1.8 warnings-as-errors Debug and Release builds use
   eight workers. Release completed all 2,935 steps in 847.96 seconds before
   the final Debug regression run, as explicitly required by the user.
3. Qualification repaired one exposed SystemVerilog compatibility regression:
   integral constant zero is a valid null `chandle` initializer, including in
   aggregate/container initialization; nonzero integral-to-`chandle`
   conversion remains rejected. Focused Release and Debug builds and the
   frontend/application regression pair pass.
4. The synchronized frozen release inventories contain 2,661 production
   diagnostics, 1,428 bounded sources, 1,728 SPDX-owned files, 746 release
   test/control files, and 1,296 execute rows with 5,184 evidence cells.
   Candidate evidence spans 627 exact paths split 278 test, 323 production,
   and 26 release paths, with 146 runtime owners. The matrix and evidence
   SHA-256 values are respectively
   `28aca45a0ca88ded646cbd1baff5d9cc29346d3bf8a0da0fa0187dd58b0179da`
   and
   `2915ba1d963be5a0215313df3a0c6a62ff3406a972f2b2bd3446c16187154049`.
5. The complete Debug suite passes 405/405 in 132.03 seconds. The complete
   Release suite passes 405/405 in 139.68 seconds. Sanitizers and hosted-CI
   monitoring were intentionally not run; both remain reserved exclusively
   for Batch 190 under the ten-batch cadence.
6. The single Batch 184 implementation commit is pushed as `1d579baa` on
   `codex/v3`. Proceed to Batch 185 Change 1 only.

## Batch 185 active checkpoint - after Change 1

1. Batch 184 is synchronized at pushed commit `1d579baa`. Batch 185 Change 1
   is complete and intentionally uncommitted on `codex/v3`; preserve the dirty
   worktree through Batch 185 Change 20.
2. `tests/feature_matrix/systemverilog_2023_inventory.tsv` is the independently
   worded 56-row IEEE 1800-2023 delta ledger. It assigns Changes 2-19 in Batch
   185 and Changes 1-19 in Batches 186-187 one-to-one across frontend/data,
   class/process, verification, hierarchy/timing, foreign API, integration,
   and closure obligations.
3. All 56 rows start active and zero are preserved. Each row contains only a
   standard identity, clause numbers, a project-authored obligation, and
   existing planned parser, semantic, elaboration, runtime, evidence, and
   resource owners. No reference contents, local reference location, or
   reference hash is recorded.
4. `cmake/CheckSystemVerilog2023Inventory.cmake` freezes the exact row shape,
   unique IDs/domains/closure assignments, complete Batch 185-187 ownership,
   repository paths, forbidden-reference policy, test registration, authoring
   contract, source manifest, and resource-governance integration. Its initial
   normalized SHA-256 is
   `99439b5a02b980aaabcb037757f09d6c2d490b5173193839cb97123733de5063`.
5. The two new source paths advance the deterministic source manifest to 1,795
   paths at SHA-256
   `817c592908e7bb0f0929d6f819bf14f13a08d19869f0abcbbd7ab15c17832174`.
6. Exact Clang warnings-as-errors Debug regeneration and relinking completes
   with eight workers. The inventory, resource, source-package, and source-line
   slice passes 4/4 in 6.16 seconds; `git diff --check` is clean.
7. Proceed only to Batch 185 Change 2: add the SystemVerilog-2023 enum,
   manifest, CLI, artifact, and cache identities. Do not run Release,
   clean-first, sanitizers, hosted-CI monitoring, commit, or push before Change
   20. Sanitizers and hosted CI remain reserved exclusively for Batch 190.

## Batch 185 active checkpoint - after Change 2

1. Batch 185 Changes 1-2 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains Batch 184 commit `1d579baa`.
2. The project and durable frontend enums append SystemVerilog-2023 without
   renumbering retained identities. Manifest aliases `23`, `2023`, `sv-23`,
   `sv-2023`, and `systemverilog-2023` canonicalize to `2023`; CLI help exposes
   `23/2023`, while the default remains SystemVerilog-2017.
3. The existing `Language::SystemVerilog2017` value remains the family
   discriminator. `StandardRevision::SystemVerilog2023` carries the exact
   profile through preprocessing, parsing, semantic units, portable object and
   design provenance, and foreign profile routing. This avoids treating an
   edition as a different HDL family.
4. SystemVerilog processes now copy exact standard and compatibility identities
   into SimIR. The LLVM native-object cache already hashes those fields, and
   the focused cache test proves 2017 and 2023 produce distinct cold entries
   while identical profiles warm-hit.
5. S23-B185-C02 is preserved. The inventory has 55 active and one preserved
   row at normalized SHA-256
   `04a14f353c734e6ea985a0f4b014f7bff9fb2d6a4759180990aaff512d3a0c6f`.
   The source manifest remains 1,795 paths at SHA-256
   `817c592908e7bb0f0929d6f819bf14f13a08d19869f0abcbbd7ab15c17832174`.
6. Exact Clang warnings-as-errors Debug project, frontend, library, LLVM,
   elaboration, CLI, application, TF, and VPI targets build with eight workers.
   The focused functional/governance slice passes 10/10 in 30.88 seconds; the
   direct CLI help check also passes.
7. Proceed only to Batch 185 Change 3: implement 2023 keyword, tokenization,
   preprocessing, and lexical changes. Do not run Release, clean-first,
   sanitizers, hosted-CI monitoring, commit, or push before Change 20.

## Batch 185 active checkpoint - after Change 3

1. Batch 185 Changes 1-3 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. `KeywordSet::SystemVerilog2023` is an explicit appended identity with a rank
   later than 2017. It retains the existing reserved-word membership because
   the 2023 edition adds no reserved word, but prevents profile provenance from
   collapsing to the 2017 keyword-set identity.
3. `begin_keywords "1800-2023"` is accepted under the 2023 source profile and
   rejected with `FSIM-SV-PP-052` at its version token under 2017. Nested older
   keyword regions retain the existing push/pop behavior. Preprocessing and
   parser construction preserve the exact 2023 `StandardRevision` while using
   the common SystemVerilog language family.
4. Parenthesized `ifdef and `elsif conditions under the 2023 profile evaluate
   nested macro identifiers with `!`, `&&`, `||`, `->`, and `<->`, including inside
   multiline macro replacement bodies. Older profiles reject the form,
   malformed expressions are diagnosed, and bare-identifier conditionals are
   unchanged. The standard-revision rank now explicitly places 2023 after
   2017, preventing 2023-only gates from falling through to the oldest rank.
5. S23-B185-C03 is preserved. The inventory has 54 active and two preserved
   rows at normalized SHA-256
   `3cc0237c3a729095aa5ef65eeaf7a54399cb26d8bc6adca8392d2acbf1f49511`.
   The source manifest remains 1,795 paths at SHA-256
   `817c592908e7bb0f0929d6f819bf14f13a08d19869f0abcbbd7ab15c17832174`.
6. Exact Clang warnings-as-errors Debug frontend targets build with eight
   workers. The frontend and focused governance slice passes 5/5 in 6.04
   seconds.
7. Proceed only to Batch 185 Change 4: implement revised design-unit and
   scheduling declarations. Do not run Release, clean-first, sanitizers,
   hosted-CI monitoring, commit, or push before Change 20.

## Batch 185 active checkpoint - after Change 4

1. Batch 185 Changes 1-4 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. `DesignUnit::systemverilog_scheduling_declaration` now records the parsed
   SystemVerilog unit's default process region, prototype status, and source
   coordinate. Modules and interfaces own the Active region, programs own the
   Reactive region, and Verilog units retain no SystemVerilog scheduling
   declaration.
3. Portable-unit validation rejects out-of-range region values and any
   language, unit-kind, region, or prototype inconsistency. Elaboration uses
   the retained region to assign stable program ownership while preserving a
   unit-kind fallback for handcrafted frontend objects. The runtime's five
   duplicate process-region selections now share
   `process_execution_phase`; scheduler ordering itself is unchanged.
4. The SystemVerilog-2023 application witness parses module, interface, and
   program declarations and elaborates module/program roots to prove Active
   and Reactive process ownership. S23-B185-C04 is preserved. The inventory
   has 53 active and three preserved rows at normalized SHA-256
   `de9138c7c5a0bf6be78cde75f516df87e466b8dd110ca71f729f0f635f3686e8`.
   The source manifest remains 1,795 paths at SHA-256
   `817c592908e7bb0f0929d6f819bf14f13a08d19869f0abcbbd7ab15c17832174`.
5. Change 3's lexical closure also includes the complete parenthesized 2023
   Boolean `ifdef/`elsif operator set at source scope and inside multiline macro
   replacement bodies, with exact older-profile and malformed-form rejection.
   `SystemVerilog2023` is explicitly the newest preprocessor standard rank.
6. The exact Clang warnings-as-errors Debug application target and its 792-step
   dependency rebuild complete with eight workers. The application and focused
   governance slice passes 5/5 in 6.23 seconds; `git diff --check` is clean.
7. Proceed only to Batch 185 Change 5: implement scalar, integral, literal,
   and type-system changes. Do not run Release, clean-first, sanitizers,
   hosted-CI monitoring, commit, or push before Change 20.

## Batch 185 active checkpoint - after Change 5

1. Batch 185 Changes 1-5 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. `SystemVerilogIntegralTypeDescriptor` is the single source of truth for
   `bit`, `logic`, `reg`, `byte`, `shortint`, `int`, `longint`, `integer`, and
   `time`. Both general declaration parsing and parameter/type parsing now use
   the same two-state/four-state domain, width, signedness, and scalar-kind
   defaults.
3. Resolved non-nominal simple integral types compare by domain, width, and
   signedness rather than source spelling or packed-range direction. This rule
   is shared by `type(...)` equality and callable-profile matching. Enums,
   packed aggregates, containers, unresolved aliases, classes, interfaces,
   real types, and handles remain on their existing nominal or exact-shape
   paths.
4. The 2023 frontend witness freezes all nine descriptors and parsed parameter
   types. The elaboration witness covers equivalent built-in/vector spellings
   and opposite range directions while retaining enum/container distinctions.
   The application witness runs interpreter and compiled O0/O2 and proves that
   assigning X/Z collapses those bits to zero only in two-state storage.
5. S23-B185-C05 is preserved. The inventory has 52 active and four preserved
   rows at normalized SHA-256
   `a54e6b3e74e80b717c78b402c0d169aa4c387ffb2ba0bcca894e01ced8d73499`.
   The parameter test moved to `frontend_parameter_tests.cpp`, reducing the
   original declaration test from 2,504 to 1,617 lines; the new file has 914
   lines. The deterministic source manifest now has 1,796 paths at SHA-256
   `9c1ce4b07aa47c56a0da800bad811171affcfb28f310812c380f15e7980369a9`.
6. Exact Clang warnings-as-errors Debug frontend, elaboration, and merged
   application targets build with eight workers. The frontend, elaboration,
   application, inventory, resource, source-package, and source-line slice is
   green; `git diff --check` is clean.
7. Proceed only to Batch 185 Change 6: implement revised packed and unpacked
   aggregate declarations, selections, assignments, and type equivalence. Do
   not run Release, clean-first, sanitizers, hosted-CI monitoring, commit, or
   push before Change 20.

## Batch 185 active checkpoint - after Change 6

1. Batch 185 Changes 1-6 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. `systemverilog_types_equivalent` is now the shared recursive type rule for
   expression type operators and callable-profile conformance. It preserves
   nominal typedef aggregate and enum identity while comparing anonymous
   packed/unpacked aggregate kind, named members, nested types, dimensions,
   container bounds, element profiles, and associative index profiles.
3. Source spans and initializers do not affect type identity. Mismatched member
   names and static bounds remain distinct. The prior elaboration-local
   recursive comparator and the shallow callable width/spelling check are
   removed in favor of the common frontend rule.
4. A direct 2023 frontend witness covers equivalent and mismatched anonymous
   packed structures plus matching and bound-mismatched arrays of anonymous
   unpacked structures. The application witness copies the matching packed and
   unpacked values, selects their members, and freezes structural type equality
   under interpreter and compiled O0; the retained aggregate corpus continues
   to cover compiled O2, nested containers, unions, selections, and assignment
   patterns.
5. S23-B185-C06 is preserved. The inventory has 51 active and five preserved
   rows at normalized SHA-256
   `f25bee00314bc72f88a9333100c730683d9c46664777f4803adf7522cbf1e08d`.
   The source manifest remains 1,796 paths at SHA-256
   `9c1ce4b07aa47c56a0da800bad811171affcfb28f310812c380f15e7980369a9`.
6. Exact Clang warnings-as-errors Debug frontend and merged application targets
   build with eight workers. The frontend, aggregate application, inventory,
   resource, source-package, and source-line slice passes 6/6 in 7.18 seconds.
7. Proceed only to Batch 185 Change 7: implement revised string, event, handle,
   and dynamic-object semantics with bounded ownership and lifetime. Do not run
   Release, clean-first, sanitizers, hosted-CI monitoring, commit, or push
   before Change 20. At Change 20, compile clean Release before the Debug suite.

## Batch 185 active checkpoint - after Change 7

1. Batch 185 Changes 1-7 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. SystemVerilog strings are now modeled as ordered sequences of eight-bit
   characters. Length, indexing, inclusive slicing, comparison, numeric
   conversion, indexed assignment, and `getc`/`putc` operate on unsigned bytes;
   mutation stores the low eight bits without changing length. Arbitrary byte
   sequences are valid as long as they meet the existing resource ceilings.
3. UTF-8 validation and transient decoded-code-point vectors are removed from
   runtime, LLVM validation, DPI marshalling, container elements, and
   associative string keys. The shared mutation operation and JIT callback are
   named `StringReplaceByte`; its cache-key spelling intentionally prevents
   reuse of native objects lowered under the old semantics.
4. The 2023 container application proves independent dynamic-array assignment,
   self-resizing initialization, deletion, and source lifetime. The retained
   2023 aggregate application covers chandle value copies. The named-event
   application now selects 2023 and retains alias, null, trigger, event-region,
   interpreter, compiled O0, and compiled O2 evidence.
5. S23-B185-C07 is preserved. The inventory has 50 active and six preserved
   rows at normalized SHA-256
   `101bc55af062dde949e089574c64d019b60726c901f3df9a5264debff845999e`.
   The source manifest remains 1,796 paths at SHA-256
   `9c1ce4b07aa47c56a0da800bad811171affcfb28f310812c380f15e7980369a9`.
6. Exact Clang warnings-as-errors Debug runtime, LLVM, and merged application
   targets build with eight workers. The runtime, LLVM, mutable-string,
   container, named-event, aggregate, inventory, resource, source-package, and
   source-line slice passes 10/10 in 24.67 seconds; `git diff --check` is clean.
7. Proceed only to Batch 185 Change 8: implement revised class declaration,
   inheritance, member visibility, and override legality. Do not run Release,
   clean-first, sanitizers, hosted-CI monitoring, commit, or push before Change
   20. At Change 20, compile clean Release before the Debug suite.

## Batch 185 active checkpoint - after Change 8

1. Batch 185 Changes 1-8 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. Interface classes now retain a primary and additional `extends` relations;
   ordinary classes retain one ordinary base and implement interface classes.
   Name resolution, cycle detection, method collection, type-actual
   resolution, and pure-method fulfillment traverse the complete graph without
   treating diamond paths as duplicate declarations.
3. Interface bodies accept parameter and type declarations plus public pure
   virtual method prototypes. Nested interface classes, state properties,
   method bodies, constraints, covergroups, incompatible class relationships,
   duplicate method names, invalid qualifier combinations, and mismatched
   virtual overrides reject with stable diagnostics.
4. Multiple inherited interface method, type, and parameter declarations with
   the same name require an explicit local resolving declaration. Argument
   names, directions, reference qualifiers, structural types, visibility, and
   static qualification participate in override conformance; class-handle
   result covariance remains supported.
5. The 2023 class application witness implements a three-interface graph and
   passes interpreter, compiled O0/O2, native-cache, artifact, and relocated
   library execution. Artifact cardinality now accounts for the three explicit
   interface declarations.
6. S23-B185-C08 is preserved. The inventory has 49 active and seven preserved
   rows at normalized SHA-256
   `8cfa14332f7120468b50636bcdb474aaea3a705785619dfdfaf6de8acabe20b6`.
   The source manifest remains 1,796 paths at SHA-256
   `9c1ce4b07aa47c56a0da800bad811171affcfb28f310812c380f15e7980369a9`.
7. Exact Clang warnings-as-errors Debug frontend and merged application targets
   build with eight workers. The frontend and class application tests pass.
   Proceed only to the focused governance slice, then Batch 185 Change 9. Do
   not run Release, clean-first, sanitizers, hosted-CI monitoring, commit, or
   push before Change 20. At Change 20, compile clean Release before the Debug
   suite.

## Batch 185 active checkpoint - after Change 9

1. Batch 185 Changes 1-9 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. Parameterized class identity now recursively includes nominal boundaries,
   canonical integral representation, aggregates, containers, nested
   class-handle actuals, and value-expression structure. Equivalent `logic`
   and `reg` actuals coalesce; two-state/four-state and structural differences
   remain distinct. Class-handle type equivalence includes every type/value
   actual.
3. Class base and interface relation actuals substitute the enclosing
   specialization's value/type environment. Dependent class bases can resolve
   through a concrete type actual, and a uniquely resolved declaration remains
   available after portable source/object reconstruction.
4. Each specialization retains direct interface-specialization identities.
   Recursive diamond traversal coalesces the same specialization and diagnoses
   distinct specializations of one inherited interface class with
   `FSIM-SV-CLASS-SPEC-012`.
5. Standalone class-state schema 12 persists the interface edges and validates
   that every base/interface target exists and that direct interface edges are
   unique. The class application proves schema round trips and relocated
   libraries; the 2023 type-parameter application proves stable identities
   through interpreter, compiled O0/O2, and native-cache rebuilds.
6. S23-B185-C09 is preserved. The inventory has 48 active and eight preserved
   rows at normalized SHA-256
   `58fe41bbd69cf872a1523fc44eaae51c4deec6c2bd65cad9b23a5f28ff51052c`.
   The source manifest remains 1,796 paths at SHA-256
   `9c1ce4b07aa47c56a0da800bad811171affcfb28f310812c380f15e7980369a9`.
7. Exact Clang warnings-as-errors Debug frontend, merged application, and
   application shard-one targets build with eight workers. The frontend, class,
   type-parameter, artifact-phase, and nested-schema tests pass. Proceed only
   to the focused governance slice, then Batch 185 Change 10. Do not run
   Release, clean-first, sanitizers, hosted-CI monitoring, commit, or push
   before Change 20. At Change 20, compile clean Release before the Debug suite.

## Batch 185 active checkpoint - after Change 10

1. Batch 185 Changes 1-10 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. A class now owns at most one constructor. Constructors reject explicit
   result types and static, virtual, pure, or final qualifiers. An explicit
   `super.new` call is legal only once, directly as the constructor's first
   executable statement; late, nested, repeated, and ordinary-method uses
   diagnose transactionally.
3. Every class callable specializes to an Automatic per-invocation lifetime,
   including static class-member methods. Source-level `function static` and
   `task static` class storage lifetimes reject, and the runtime no longer owns
   maps that retained class-method integral or string locals across calls.
4. Standalone class-state validation admits only Automatic method profiles.
   The class application preserves base-before-derived construction, virtual
   dispatch, recursion, fresh repeated-call locals, static-member invocation,
   interpreter, compiled, Debug, cache, artifact, and relocated-library
   behavior.
5. S23-B185-C10 is preserved. The inventory has 47 active and nine preserved
   rows at normalized SHA-256
   `c1bd1097a8b7ef842c1a8d9691692f045b4292c8918e27164e4773b4692fb652`.
   The source manifest remains 1,796 paths at SHA-256
   `9c1ce4b07aa47c56a0da800bad811171affcfb28f310812c380f15e7980369a9`.
6. Exact Clang warnings-as-errors Debug frontend and application targets build
   with eight workers. The frontend, class application, artifact-phase,
   nested-schema, inventory, resource, source-package, and source-line tests
   pass 8/8 in 4.40 seconds; `git diff --check` is clean.
7. Proceed only to Batch 185 Change 11: implement revised process,
   fork/join, and process-control behavior. Do not run Release, clean-first,
   sanitizers, hosted-CI monitoring, commit, or push before Change 20. At
   Change 20, compile clean Release before the Debug suite.

## Batch 185 active checkpoint - after Change 11

1. Batch 185 Changes 1-11 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. SystemVerilog-2023 functions may spawn `fork...join_none` background
   processes only from procedural code rooted in an `initial` block. Spawned
   branches may contain task-legal timing controls while the function returns
   in the same simulation time. Older profiles, non-`initial` origins, other
   function join kinds, and a `return` nested in any fork branch reject with
   stable diagnostics.
3. An escaping automatic-function activation owns one shared callable-register
   context. Sibling processes share it, overlapping and recursive activations
   remain distinct, and outer captured values are shielded while an inner
   callable is suspended. Completion and kill release a terminated process's
   context ownership.
4. Context switching occurs only at existing scheduler/SimIR boundaries and
   moves only the captured packed, string, and container registers. Ordinary
   module/process storage retains the existing shared frame, LLVM fork children
   remain compiled, and no whole-frame or whole-container snapshot was added.
5. The independently authored application witness launches two overlapping
   automatic activations with different arguments and delayed children. It
   passes interpreter and compiled LLVM O0/O2 cold/warm cache execution while
   the retained fork corpus continues to prove status, await, kill, suspend,
   resume, named/unnamed disable, wait-fork, Debug, trace, and artifact behavior.
6. S23-B185-C11 is preserved. The inventory has 46 active and ten preserved
   rows at normalized SHA-256
   `5af437644431606439ac2acce0f39e2062804328b9bbba322806822cd5a4ded6`.
   The source manifest now has 1,797 paths at SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
7. Exact Clang warnings-as-errors Debug frontend, elaboration, runtime, and
   application targets build with eight workers. The frontend, elaboration,
   fork application, runtime, inventory, resource, source-package, and
   source-line slice passes 8/8; `git diff --check` is clean.
8. Proceed only to Batch 185 Change 12: implement revised assignment and
   assignment-pattern behavior. Do not run Release, clean-first, sanitizers,
   hosted-CI monitoring, commit, or push before Change 20. At Change 20,
   compile clean Release before the Debug suite.

## Batch 185 active checkpoint - after Change 12

1. Batch 185 Changes 1-12 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. In the exact SystemVerilog-2023 profile, a static cast provides the
   assignment-like context for an untyped assignment pattern. Retained
   profiles reject that 2023 semantic with `FSIM-ELAB-SVCAST-005`.
3. SystemVerilog-2023 rejects nonblocking writes to elements of dynamically
   sized array variables with `FSIM-ELAB-SVASSIGN-001`. The retained-profile
   indexed-container extension captures the index and value during the active
   phase and publishes only that element update in the update phase. It does
   not snapshot the whole container.
4. Interpreter and compiled execution share the scheduler update operation.
   Existing generated procedural-continuous drivers still re-evaluate their
   right-hand side when dependencies change.
5. Independently authored runtime and application witnesses cover deferred NBA
   visibility and operand capture, the exact-profile positive and negative
   boundaries, procedural-continuous re-evaluation, and interpreter plus LLVM
   O0/O2 equivalence.
6. S23-B185-C12 is preserved. The inventory has 45 active and eleven preserved
   rows at normalized SHA-256
   `96136b33d90f67592e85d6f60e2eef4618282590402408fbf30bab97287aa5b6`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
7. Exact Clang warnings-as-errors Debug runtime and application targets build
   with eight workers. The runtime, SystemVerilog conformance, inventory,
   resource, source-package, and source-line slice passes 6/6; `git diff
   --check` is clean.
8. Proceed only to Batch 185 Change 13: implement streaming and aggregate
   assignment changes. Do not run Release, clean-first, sanitizers, hosted-CI
   monitoring, commit, or push before Change 20. At Change 20, compile clean
   Release before the Debug suite.

## Batch 185 active checkpoint - after Change 13

1. Batch 185 Changes 1-13 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. Streaming concatenations are executable assignment targets. Unpacking
   validates the slice, consumes the required most-significant source bits,
   rejects an undersized source, applies block ordering, and uses the existing
   concatenated-assignment publication path.
3. Positional assignment-pattern targets deconstruct through that same ordered
   lvalue machinery. Empty, keyed, malformed, and nested streaming targets
   reject before writes. Dynamic lvalue indices retain capture-once behavior.
4. Independently authored witnesses cover left and right stream ordering,
   oversized-source consumption, positional aggregate deconstruction,
   negative width/key cases, and interpreter plus LLVM O0/O2 equivalence.
5. S23-B185-C13 is preserved. The inventory has 44 active and twelve preserved
   rows at normalized SHA-256
   `c2a23ca42cda4bfb196890080885cb67753258c5ad329f4425ebe0d64662b79e`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
6. Exact Clang warnings-as-errors Debug application compilation uses eight
   workers. The focused conformance, inventory, resource, source-package, and
   source-line slice passes; `git diff --check` is clean.
7. Proceed only to Batch 185 Change 14: implement revised operator and
   expression behavior. Do not run Release, clean-first, sanitizers, hosted-CI
   monitoring, commit, or push before Change 20. At Change 20, compile clean
   Release before the Debug suite.

## Batch 185 active checkpoint - after Change 14

1. Batch 185 Changes 1-14 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. The exact 2023 profile lexes and parses absolute and relative tolerance
   operators in `inside` ranges. Retained profiles reject both through the
   standard-profile gate, and lowering independently rejects non-2023 HIR.
3. Integral tolerance lowering evaluates both operands once, converts the
   tolerance to the center width, uses a widened intermediate for percentage
   calculation, truncates back to the center type, and accepts either computed
   bound order. This covers negative centers/tolerances and fixed-width wrap.
4. Independently authored witnesses cover inclusive/exclusive absolute bounds,
   positive/negative relative bounds, retained-profile rejection, and
   interpreter plus LLVM O0/O2 equivalence. The diagnostic catalog now also
   includes Change 3's conditional-compilation expression diagnostic.
5. S23-B185-C14 is preserved. The inventory has 43 active and thirteen
   preserved rows at normalized SHA-256
   `a5145e9a395419ccf7bdc1ef08b515e521a01b8df9013bf22fc33f6e62e9bc2d`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
6. Exact Clang warnings-as-errors Debug frontend and application targets build
   with eight workers. The focused frontend, conformance, diagnostic,
   inventory, resource, source-package, and source-line slice passes;
   `git diff --check` is clean.
7. Proceed only to Batch 185 Change 15: implement revised procedural statement
   behavior. Do not run Release, clean-first, sanitizers, hosted-CI monitoring,
   commit, or push before Change 20. At Change 20, compile clean Release before
   the Debug suite.

## Batch 185 active checkpoint - after Change 15

1. Batch 185 Changes 1-15 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. Exact SystemVerilog-2023 string `foreach` lowering captures the string and
   its byte length once, owns one fresh signed read-only index, rechecks the
   bound per iteration, routes `continue` through the increment, and routes
   `break` to the loop exit. Retained profiles reject this 2023 behavior.
3. Unsupported index cardinality, unnamed indices, non-string collections,
   index collisions, and writes to the implicit index reject before partial
   loop control flow is emitted.
4. Focused qualification repaired two accumulated defects: apostrophe-led
   positional assignment-pattern statements now enter assignment parsing, and
   associative-array indexed writes retain insertion semantics instead of
   using the direct existing-element operation intended for dynamic arrays and
   queues.
5. Independently authored witnesses cover byte indexing, `continue`, `break`,
   retained-profile rejection, unsupported cardinality, read-only indices,
   interpreter execution, and LLVM O0/O2 equivalence.
6. S23-B185-C15 is preserved. The inventory has 42 active and fourteen
   preserved rows at normalized SHA-256
   `77fbc15f63dea96824e2c131f4f6f53c56c712e165781e493e89a2d792897410`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
7. Exact Clang warnings-as-errors Debug application compilation and its
   dependency rebuild use eight workers. The frontend, elaboration, runtime,
   conformance, diagnostic, inventory, resource, source-package, and
   source-line slice passes 9/9; `git diff --check` is clean.
8. Proceed only to Batch 185 Change 16: implement revised task, function, and
   argument behavior. Do not run Release, clean-first, sanitizers, hosted-CI
   monitoring, commit, or push before Change 20. At Change 20, compile clean
   Release before the Debug suite.

## Batch 185 active checkpoint - after Change 16

1. Batch 185 Changes 1-16 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. Exact SystemVerilog-2023 task and function formals retain `const ref` and
   `ref static` independently. Omitted directions inherit the complete
   direction/reference qualifier profile. `const ref` is input-only and
   read-only; retained profiles reject `ref static`.
3. Call lowering accepts static module/process objects and pass-through
   `ref static` formals, while rejecting automatic callable locals, VHDL
   procedure storage, string elements, and dynamic-container elements before
   emitting a partial call. Function and task paths share the same lifetime
   classifier.
4. Class callable profiles and task conversion preserve both qualifiers.
   Portable owning-unit schema 28 serializes them and directly rejects schema
   27, so object and cache identities cannot reinterpret the changed ABI.
5. Independently authored witnesses cover qualifier inheritance, const
   write rejection, 2017 rejection, automatic-local rejection, static-formal
   pass-through, interpreter execution, compiled LLVM O0/O2 equivalence, and
   portable function/task round trips.
6. S23-B185-C16 is preserved. The inventory has 41 active and fifteen
   preserved rows at normalized SHA-256
   `ec0fb360b82f4d86883993e409cae6b5f109aff80e27af5e09fe69b8ef822d90`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
7. Exact Clang warnings-as-errors Debug frontend, library, and application
   targets build with eight workers. The frontend, library artifact,
   conformance, diagnostic, inventory, resource, source-package, and
   source-line slice passes 8/8; `git diff --check` is clean.
8. Proceed only to Batch 185 Change 17: implement revised clocking and
   interprocess synchronization behavior. Do not run Release, clean-first,
   sanitizers, hosted-CI monitoring, commit, or push before Change 20. At
   Change 20, compile the clean Release configuration before running the Debug
   suite, so Release compiler repairs cannot invalidate prior Debug results.

## Batch 185 active checkpoint - after Change 17

1. Batch 185 Changes 1-17 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. Clocking input/output skews now participate in project resolution discovery
   and normalization. Parameter substitution reaches default and member skews;
   elaboration then requires a known nonnegative constant and checks the folded
   duration against the 64-bit simulation-time limit.
3. Module code resumed by `##` now hands off to the Reactive region after the
   Observed-region clocking sample. Program code retains its existing Reactive
   ownership. The clocking event alias remains lowering-private, preventing a
   false public VPI signal/scope collision.
4. Counting semaphores retain signed initial balances. Zero-count get,
   try-get, and put operations complete without changing state; negative
   operation counts reject; positive waiters retain FIFO service; and returned
   keys use checked signed arithmetic. Mailbox FIFO behavior remains unchanged.
5. Independently authored runtime and application witnesses cover signed/zero
   semaphore counts, parameter-derived input/output skews, sampling/driving
   timing, invalid dynamic skew, 2017/2023 profiles, interpreter and compiled
   LLVM O0/O2 execution, cold/warm cache reuse, and runtime-state round trips.
6. S23-B185-C17 is preserved. The inventory has 40 active and sixteen
   preserved rows at normalized SHA-256
   `0f1cb80eee9c1da6bcf3f1559594c973fcdff2e7f75a992aa1802470e07834b4`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
7. Exact Clang warnings-as-errors Debug runtime and application targets build
   with eight workers. The runtime, synchronization, time, diagnostic,
   inventory, resource, source-package, and source-line slice passes 8/8.
8. Proceed only to Batch 185 Change 18: prevent 2023 semantics from leaking
   into older profiles. Do not run Release, clean-first, sanitizers, hosted-CI
   monitoring, commit, or push before Change 20. At Change 20, compile the
   clean Release configuration before running the Debug suite, so Release
   compiler repairs cannot invalidate prior Debug results.

## Batch 185 active checkpoint - after Change 18

1. Batch 185 Changes 1-18 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. Multiple interface-class inheritance, streaming-concatenation assignment
   targets, and assignment-pattern targets require the exact 2023 profile and
   reject retained profiles with `FSIM-SV-PARSE-369` or
   `FSIM-SV-PARSE-370`. Existing tolerance-range, `ref static`, and
   function-background-process gates remain exact-profile checks.
3. The 2023 constant-expression rule for clocking skews is exact-profile only.
   A dynamic skew continues to build under the established 2017 behavior and
   rejects under 2023 with `FSIM-ELAB-CLOCK-008`.
4. The paired profile-isolation corpus tests every revised form in both 2017
   and 2023 and retains a positive 2017 baseline. Five existing tests whose
   source names claimed 2023 but whose helpers selected 2017 now request the
   exact 2023 revision explicitly.
5. S23-B185-C18 is preserved. The inventory has 39 active and seventeen
   preserved rows at normalized SHA-256
   `ca8c7655aff39a3645a8ba60fd2d78260163ac3c2bff5594459718ca13a30ebe`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
6. Exact Clang warnings-as-errors Debug frontend and application targets build
   with eight workers. The frontend, synchronization, diagnostic, inventory,
   resource, source-package, and source-line slice passes 7/7.
7. Proceed only to Batch 185 Change 19: prove the revised frontend forms through
   semantic, artifact, and cache round trips. Do not run Release, clean-first,
   sanitizers, hosted-CI monitoring, commit, or push before Change 20. At
   Change 20, compile the clean Release configuration first, resolve any
   compiler errors, and only then run the Debug suite so Release repairs cannot
   invalidate completed Debug qualification.

## Batch 185 active checkpoint - after Change 19

1. Batch 185 Changes 1-19 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `1d579baa`.
2. The exact-2023 artifact witness combines the revised class, data,
   assignment, expression, procedural, callable, process, and clocking forms.
   It reloads the `.fsimobj` semantic units and class declarations, then
   verifies exact revision and provenance identities in the `.fsimdesign`.
3. With both producer source and object hidden, the design artifact runs
   identically through interpreter and LLVM O2. Cold compilation stores every
   miss, warm and relocated runs reuse the same native-cache entries, and
   specialization-cache identities remain stable.
4. Prior object portable-schema and design-format headers reject
   transactionally under the stable v3 artifact diagnostics. The witness uses
   the current object format 7, portable schema 11, owning-unit schema 28, and
   design format 12 without compatibility readers.
5. S23-B185-C19 is preserved. The inventory has 38 active and eighteen
   preserved rows at normalized SHA-256
   `f9563a7227c58207c6414804756c22376cbb8e4038ec9e8dd2c34dcd0ac82aeb`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
6. Exact Clang warnings-as-errors Debug application, library, object, and
   design-artifact targets build with eight workers. The application,
   library, object, design, diagnostic, inventory, resource, source-package,
   and source-line slice passes 9/9; `git diff --check` is clean.
7. Proceed only to Batch 185 Change 20. First compile the clean Release
   configuration and resolve every compiler error. Only after that Release
   compile is clean, run the clean Debug suite so Release repairs cannot
   invalidate completed Debug qualification. Then update closure documents,
   create the single Batch 185 implementation commit, and push it. Do not run
   sanitizers or hosted-CI monitoring; those remain reserved for Batch 190.

## Batch 185 closure checkpoint

1. Batch 185 Changes 1-20 are complete on `codex/v3`. The independently
   worded 56-row SystemVerilog-2023 inventory has 38 active and eighteen
   preserved rows at normalized SHA-256
   `f9563a7227c58207c6414804756c22376cbb8e4038ec9e8dd2c34dcd0ac82aeb`.
   The remaining rows are owned one-to-one by Batches 186 and 187.
2. Clean Clang 22/LLVM 22.1.8 warnings-as-errors Release and Debug builds use
   eight workers. Release compiled all 2,939 steps first and its complete
   suite passed 406/406 in 145.44 seconds. Debug then compiled all 2,939 steps
   and its complete suite passed 406/406 in 144.92 seconds.
3. Release qualification repaired the clocking-block namespace boundary:
   elaboration keeps the clocking event signal in a private lookup for
   modport forwarding, publishes the forwarded port alias only, and leaves
   the public clocking name available for its VPI clocking-block object.
4. The mixed VHDL execution regression selects exact compatibility-profile
   points using `fsim-synopsys-ieee-compat-v2`; stored VHDL standard spellings
   remain `2008` and `2019`. Focused elaboration, SystemVerilog interface and
   conformance, synchronization, and VHDL logic9 tests pass in Release before
   the full clean qualification.
5. Frozen release governance contains 2,693 production diagnostics, 1,430
   bounded sources, 1,732 SPDX-owned files, and 749 release test/control
   files. The deterministic source manifest remains at 1,797 paths with
   SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
6. Sanitizers and hosted-CI monitoring were intentionally not run. Both remain
   reserved exclusively for Batch 190 under the ten-batch cadence.
7. Create and push the single Batch 185 implementation commit. Then proceed
   only to Batch 186 Change 1, preserving the same Changes 1-19 and Change 20
   release-first cadence.

## Batch 186 active checkpoint - after Change 1

1. Batch 186 Change 1 is complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Immediate assertions now retain observed-deferred `#0` from the 2009
   profile and final-deferred syntax from the 2012 profile, including the
   exact 2023 profile. Nonzero observed delays and invalid deferred action
   shapes reject with `FSIM-SV-PARSE-371` and `FSIM-SV-SEM-250`; older profile
   gates remain enforced by `FSIM-SV-PARSE-348`.
3. Conditions are sampled in their originating procedural execution. Selected
   user-task input arguments are captured before the action forks, and
   targeted per-site cancellation is an immediate internal boundary rather
   than a source-visible suspension. Repeated executions coalesce by static
   pass/failure site while outcome changes cancel both previously pending
   choices.
4. Observed actions resume in the Reactive region and final actions in the
   Postponed region. Null pass/failure actions, default failures, severity
   actions, and permitted observed user-subroutine calls retain separate
   behavior. Internal malformed handoffs diagnose with
   `FSIM-ELAB-SVASSERT-001` or `FSIM-ELAB-SVASSERT-002`.
5. The independently authored application witness covers cross-process
   procedural sampling, argument capture, last-execution cancellation, null
   actions, interpreter and LLVM O0/O2 equivalence, and cold/warm cache reuse.
   The frontend witness covers 2005 rejection, 2009 observed acceptance, 2009
   final rejection, exact-2023 acceptance, nonzero delay rejection, and
   invalid observed/final actions.
6. S23-B186-C01 is preserved. The inventory has 37 active and nineteen
   preserved rows at normalized SHA-256
   `9103f36fdc342e57b1747ac53af68770975235c83ec2debeeda8358a1de499d2`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
7. Exact Clang warnings-as-errors Debug frontend and application targets build
   with eight workers. The frontend, assertion application, diagnostic,
   inventory, resource, source-package, source-line, and CTest-command slice
   passes 8/8.
8. Proceed only to Batch 186 Change 2: implement concurrent-assertion
   revisions. Do not run Release, clean-first, sanitizers, hosted-CI
   monitoring, commit, or push before Change 20. At Change 20, first compile
   the clean Release configuration and resolve every compiler error, then run
   the Release suite. Only after the Release lane is green, run the clean
   Debug suite so Release repairs cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 2

1. Batch 186 Changes 1-2 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Concurrent directives now retain property-versus-sequence form in the
   frontend model and semantic SystemVerilog HIR. `cover sequence` accepts
   named sequence declarations and inline sequence expressions. `assert`,
   `assume`, and `restrict` sequence forms reject with `FSIM-SV-SEM-251`.
3. Resolution substitutes named-sequence formals before selecting its
   effective clock and `disable iff` condition. Inline sequence clocks and
   disable conditions are structured directly. Both forms reuse the existing
   bounded attempt, completion, action, coverage, callback, and
   assertion-control execution paths; formal sampling-region and action-order
   revisions remain owned by Change 3.
4. The independently authored runtime witness covers named and inline
   two-cycle sequences, pass and failure outcomes, pass actions, in-flight
   completion while `$assertoff` suppresses new attempts, interpreter and LLVM
   O0/O2 equivalence, and cold/warm native-cache reuse. Frontend witnesses
   cover structure, illegal directive combinations, retained SystemVerilog
   acceptance, and Verilog-profile rejection. The HIR witness preserves form,
   name, kind, and coverage-slot ordering.
5. S23-B186-C02 is preserved. The inventory has 36 active and twenty
   preserved rows at normalized SHA-256
   `92abe22b169c604a5cb66581e6dd6cf88a673c55d348653c512f1ce5d90c0cf8`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
6. The exact Clang warnings-as-errors Debug frontend, application, and
   semantic-HIR targets build with eight workers. The frontend, HIR,
   assertion-runtime, diagnostic, inventory, resource-portability,
   source-package, source-line, and CTest-command focused slice passes 9/9;
   the final run completes in 19.15 seconds at 109,152 KiB peak RSS with zero
   swaps, and `git diff --check` is clean.
7. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 3: update
   the formal concurrent-assertion execution model. Preserve all Changes 1-2
   work and do not begin checker revisions from Change 4 in the same bounded
   slice.
8. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven
   source repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 3

1. Batch 186 Changes 1-3 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Generated concurrent-assertion processes carry an explicit frontend role.
   Elaboration uses that role to assign the Observed scheduler region and to
   lower design-signal reads against the time slot's Preponed sample. Ordinary
   procedural processes continue to use current-value reads.
3. Assertion outcome coverage and callbacks execute in Observed. One private
   scheduler marker advances source pass, failure, and sequence-match actions
   into Reactive. Control-flow lowering restores sampled-read state per branch,
   and pending end-of-run serialization strips private markers before retaining
   source actions.
4. The independently authored race witness changes assertion data in the same
   slot as the triggering clock and proves that the property observes the
   earlier sampled value. Its timeline proves the pass outcome precedes its
   Reactive action. Interpreter and LLVM O0/O2 cold/warm runs agree, while
   frontend and runtime structural checks prove the explicit role, sampled
   reads, Observed ownership, and Reactive barriers.
5. Existing concurrent-property tests now stage data before the sampling edge,
   and their pending-attempt expectation reflects canonical completion before
   current-edge vacuity. The first-match structural witness verifies that
   outcome publication precedes the Reactive action boundary and source-side
   match updates.
6. S23-B186-C03 is preserved. The inventory has 35 active and 21 preserved rows
   at normalized SHA-256
   `3bb269559140d189aa9f877d568690c98156fdec9105e92b1cf33fcee925ebc6`.
   The source manifest remains at 1,797 paths with SHA-256
   `d2e888f0152c9d216a39c20c594e2fcd509833852ad7de7a29cc23d04b022d97`.
7. Exact Clang warnings-as-errors Debug frontend, application, HIR, and runtime
   targets build with eight workers. The frontend, HIR, assertion-runtime,
   runtime, diagnostic, inventory, resource-portability, source-package,
   source-line, and CTest-command focused slice passes 10/10 in 20.49 wall
   seconds at 122,180 KiB peak RSS with zero swaps; `git diff --check` is
   clean. `src/runtime/simir_state.cpp` remains unchanged.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 4:
   implement checker revisions. Preserve all Changes 1-3 work and do not begin
   constrained-random work from Change 5 in the same bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 14

1. Batch 186 Changes 1-14 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 14 normalizes implicit generate names after parsing. Ordinals are
   scoped, every construct consumes one ordinal, alternatives share a name,
   nested scopes restart at one, direct nested conditionals do not add a scope,
   and leading zeroes avoid declaration/future-label collisions. Irreconcilable
   collisions report `FSIM-SV-SEM-386`; malformed or profile-incompatible
   generated classes report `FSIM-SV-PARSE-387`.
3. Generate bodies retain ordinary and 2023 interface classes. Hierarchy
   specialization resolves only selected declarations, so an invalid class in
   an inactive alternative is inert. Selected declarations cross the
   elaboration/application boundary and are regenerated into the existing
   durable class-specialization and HIR state; interpreter and LLVM execution,
   native cache reuse, class methods, and VPI generated-scope publication see
   the same selected class.
4. Loop-generate elaboration records visited genvar values and reports
   `FSIM-ELAB-GEN-014` for a repeated cycle, in addition to the existing
   immediate non-advancement check. The application-side ownership invariant
   reports `FSIM-ELAB-GEN-015` rather than silently dropping a selected class.
   Class method delays, including classes in generate bodies, participate in
   min/typ/max selection and project-resolution normalization.
5. The semantic SystemVerilog HIR reuses the parent scope for direct nested
   conditional generates, shares one scope for same-named alternatives, and
   keeps differently named alternatives as siblings. A real non-template
   helper moved from `elaboration_analysis.cpp` into
   `elaboration_generated_declarations.cpp`; the former is 1,988 lines and all
   governed translation units remain at or below 2,000 lines.
6. Independently authored frontend, elaboration, semantic-HIR, and application
   evidence covers scoped names, collisions, shared alternatives, direct
   nesting, selected generated interface/ordinary classes, inactive invalid
   declarations, repeated genvar cycles, VPI publication, interpreter
   execution, and LLVM O0/O2 cold/warm cache reuse.
7. S23-B186-C14 is preserved. The SystemVerilog-2023 inventory has 24 active
   and 32 preserved rows at normalized SHA-256
   `f30c8f1c41522325eafb2a0941629de6db0e6e8270e2fb0a253fe300d9c6d6f2`.
   The source manifest has 1,878 paths at SHA-256
   `598f08c43cbb314411b132a2e29f8f2d3f8e0f74f2b36f171b71a52958949b1f`.
8. Exact Clang warnings-as-errors Debug frontend, elaboration, application,
   semantic-HIR, LLVM, runtime, API, and artifact targets build with at least
   eight workers. The focused diagnostics, source-line, source-manifest,
   inventory, portability, schema, artifact, API, runtime, elaboration,
   application, and LLVM closure passes 26/26 in 28.12 wall seconds.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 15:
   implement gate, switch, and UDP revisions. Preserve all Changes 1-14 and do
   not begin specify/timing-check work from Change 16 in the same bounded slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 15

1. Batch 186 Changes 1-15 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 15 corrects built-in gate arity. `buf` and `not` accept one or more
   output terminals followed by exactly one input and emit one independently
   named continuous driver per output. N-input logic gates accept one or more
   inputs. Empty terminal sets and non-lvalue additional outputs remain
   diagnosed.
3. Named switch arrays materialize every declared range element. Scalar
   terminals broadcast, equal-count packed terminals map by declared packed
   direction, unary signed literal bounds are accepted, and each directional
   driver receives a stable indexed identity. Unnamed arrays and mismatched
   terminal widths are rejected without a scalar fallback.
4. Programs reject built-in and user-defined primitive instances, including
   generated regions. UDP validation rejects cross-language revision metadata
   and empty compatibility profiles. Generated UDP execution units retain the
   declaration's exact standard revision and compatibility profile.
5. Independently authored frontend and application evidence covers gate arity,
   multiple outputs, output lvalues, switch-array indices and direction, program
   exclusions, exact 2023 sequential UDPs, artifact inspection, interpreter
   execution, and LLVM O0/O2 cold/warm cache reuse with identical results.
6. The legacy SystemVerilog closure audit was repaired to follow the artifact
   witness moved by the translation-unit remediation and its two owner-path
   inventory digests were refreshed. This is audit continuity, not a new
   language claim.
7. S23-B186-C15 is preserved. The SystemVerilog-2023 inventory has 23 active
   and 33 preserved rows at normalized SHA-256
   `1405423db36a5effa8344daf705930a4eac449bef346f4ff504525a7b2fabb66`.
   The source manifest has 1,878 paths at SHA-256
   `598f08c43cbb314411b132a2e29f8f2d3f8e0f74f2b36f171b71a52958949b1f`.
8. Exact Clang warnings-as-errors Debug frontend, elaboration, library,
   application, runtime, LLVM, and API targets build with at least eight
   workers. The focused diagnostics, source-line, source-manifest, inventory,
   portability, schema, artifact, API, runtime, elaboration, application, and
   LLVM closure passes 26/26 in 24.80 wall seconds. `git diff --check` is clean.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 16:
   implement specify-block and timing-check revisions. Preserve all Changes
   1-15 and do not begin SDF backannotation work from Change 17 in the same
   bounded slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 4

1. Batch 186 Changes 1-4 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Checker declarations work at compilation-unit and design-unit scope. A
   checker owns structured sequence/property declarations and concurrent
   directives while retaining its complete original body token stream.
3. Checker instances have a distinct frontend model for ordered, named,
   implicit, wildcard, open, and defaulted connections. Connection parsing
   retains arbitrary balanced actual-token expressions. Stable diagnostics
   cover mixed forms, unknown and duplicate formals, excess actuals, missing
   required actuals, malformed boundaries, and duplicate instance names.
   Explicit named/open connections take precedence over wildcard matching.
4. Frontend specialization creates collision-free, instance-qualified copies
   of checker declarations and directives before ordinary assertion reference
   resolution. It substitutes actuals and prior-formal defaults into clocks,
   predicates, and actions. Each generated assertion consequently retains
   Change 3's sampled Preponed reads, Observed evaluation/outcomes, and Reactive
   source actions.
5. The independently authored frontend matrix proves compilation-unit
   ownership, two instance forms, retained connections, generated declarations
   and processes, and all semantic connection diagnostics. The application
   witness puts the checker and consuming design in separate files of one
   explicit source-set compilation unit. It executes defaulted named, explicitly
   connected ordered, and wildcard-with-open-default instances with
   instance-qualified coverage through interpreter and LLVM O0/O2 cold/warm
   paths; outcomes, sampled reads, scheduler ownership, actions, and native-cache
   reuse agree. File compilation units remain intentionally isolated by the
   existing project contract.
6. Checker specialization and its application witness moved into focused TPP
   owners after the source-line gate identified growth beyond existing hard
   ceilings. Both new files are in the deterministic source-package manifest;
   the established large assertion owners remain below their pre-existing
   2,500-line hard limits.
7. S23-B186-C04 is preserved. The inventory has 34 active and 22 preserved rows
   at normalized SHA-256
   `f73af7699738eb952ba19e9996451d82880b4bc2c047e75e8820e7aed2eb81e4`.
   The source manifest has 1,799 paths at normalized SHA-256
   `91dfe9e67e2fb955e953e7eaf863c0bca8060dea0f288a41ddf65b7bf638c50b`.
8. Exact Clang warnings-as-errors Debug frontend and application targets build
   with eight workers. The final focused slice covers frontend, assertion
   runtime, semantic HIR, runtime, diagnostic, inventory, portability,
   source-package, source-line, and CTest-command owners. `git diff --check` is
   clean. The 10/10 slice completes in 22.69 wall seconds at 146,232 KiB peak
   RSS with zero swaps.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 5:
   implement constrained-random and solver revisions. Preserve all Changes 1-4
   work and do not begin functional-coverage work from Change 6 in the same
   bounded slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 5

1. Batch 186 Changes 1-5 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 5 adds a typed `unique` constraint form from exact-profile parsing
   through semantic HIR, class constraint lowering, source-retained inline
   templates, design-artifact validation, and LLVM validation. Named random
   properties become pairwise inequalities; materialized container names
   expand to their element variables before comparison. Single-item forms are
   true, empty and malformed forms are diagnosed, and unsupported expression
   items fail before solver execution.
3. `object.randomize(null)` is represented as an explicit no-properties
   selection rather than a property named `null`. It evaluates class and inline
   constraints against singleton current-value domains, returns one or zero,
   and does not publish properties, revisions, `randc` state, or random-stream
   progress. Mixing the null selection with named properties produces
   `FSIM-SV-CLASS-022`.
4. Independently authored runtime tests prove successful and failed checker
   calls leave state unchanged and that a checker call cannot perturb a later
   equal-seed assignment. The source application proves ordinary and impossible
   unique constraints plus checker behavior through interpreter and compiled
   O0/O2 cold/warm cache paths. Semantic HIR tests prove array expansion,
   dependency ownership, satisfaction, and duplicate-value rejection.
5. The first focused gate found `application_simulation.cpp` at 2,505 physical
   lines, while a translation-unit-aware audit showed that textual fragments
   made the real compilation unit 7,907 lines. Selection decoding moved into
   `application_class_randomization.cpp`. Class specialization, object,
   property, randomization, and constructor execution now build independently
   in the 787-line `application_class_execution.cpp` component behind a narrow
   private interface. Checker parsing and its application witness also moved
   from non-template `.tpp` fragments into separately compiled sources.
6. The remediation also converted the 3,965-line SimIR execution boundary and
   interpreter fragments into independent 1,993- and 1,972-line sources plus a
   268-line shared owner. Default virtual process-execution methods and code-
   coverage validation moved out of high-fanout public headers into compiled
   runtime sources, reducing those headers to 447 and 192 lines.

   The follow-up debt closure removes all 36 `.tpp` files and partitions all
   54 legacy oversized implementation/test units into real compiled sources.
   It deletes the allowlist and non-increasing-baseline escape hatch: the
   repository contract now rejects any `.tpp`, any authored C/C++ source or
   header above 2,000 lines, and any implementation unit above the same target,
   with zero current debt in all three categories. Substantial non-template
   implementations also moved out of shared parser, artifact, runtime,
   application, and test-support headers. Remaining header bodies are genuine
   templates, compile-time `constexpr` mappings/tables, trivial value
   accessors/constructors, or SystemC SDK adapters required at template
   instantiation sites. The former 2,533-line monolithic elaborator header is
   split into an 864-line shared-detail surface plus 1,005-line lowering and
   692-line hierarchy interfaces. More than 110 implementation files now
   include only the interface they own; the two actual lowerer/hierarchy
   crossing points include both. The last oversized public interface,
   `simir.hpp`, is 1,997 lines after its non-template execution-point
   constructor moved to the compiled runtime owner.
7. S23-B186-C05 is preserved. The inventory has 33 active and 23 preserved rows
   at normalized SHA-256
   `58e4aae68ba7f4ccd9502c658d04106279c6ce9e9edf21afdf13d2e7a1033c86`.
   The path-only VHDL inventory update has normalized SHA-256
   `1e2cbef28c69d4612cd731b72d719f72c1eada48fcb8f4ce0061a14a175bf6d4`.
   The refreshed source manifest has 1,870 paths at SHA-256
   `f85251f5e07ed00c347e98c00ac4c40edc86291a5cea3642eb0e6e00faf59510`.
8. Exact Clang warnings-as-errors Debug frontend, runtime, application, and
   merged application/runtime test targets build with eight workers. The final
   focused 14/14 slice covers frontend, runtime, random application, class
   application, assertions, artifact phases, semantic HIR, diagnostics, both
   affected inventories, portability, source-package, source-line, and CTest-
   command owners. It completes in 28.95 wall seconds at 151,888 KiB peak RSS
   with zero swaps; the post-documentation rerun passes the same 14/14 in 28.66
   wall seconds at 151,868 KiB peak RSS with zero swaps. `git diff --check` is
   clean. The structural-debt follow-up rebuilds the affected frontend,
   elaboration, runtime, artifact, application, LLVM, and test targets under
   the same Clang warnings-as-errors Debug contract. Its final focused slice
   passes 16/16 in 50.96 wall seconds, including both source-structure gates
   and the elaboration, runtime, LLVM, artifact, VHDL, mixed-language, and
   coverage-point owners.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Batch 186 Change 6 remains the active
   functional-coverage slice; the requested structural and legacy-debt
   remediation does not close or advance that change. Preserve all Changes
   1-5 and the in-progress Change 6 work, and do not begin utility-system-task
   work from Change 7 in the same bounded slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 6

1. Batch 186 Changes 1-6 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 6 implements the SystemVerilog-2023 functional-coverage deltas:
   covergroup inheritance through a parent class, real and shortreal
   coverpoints, absolute and relative real-bin tolerances, immutable
   `type_option.real_interval`, and immutable instance
   `option.cross_retain_auto_bins`. Every new form is isolated from older
   profiles and has stable negative diagnostics.
3. Inherited declarations preserve parent origin identity while local shadows
   and cross operands resolve in the derived effective group. Real values keep
   their exact scalar kind and IEEE payload through parsing, resolution, SimIR,
   interpreter and LLVM lowering, callbacks, illegal-bin reports, artifacts,
   and cache keys.
4. Finite explicit cross products are materialized before the first sample.
   Reports and `.fsimcov` therefore retain zero-hit static coverpoint bins and
   cross tuples; `option.cross_retain_auto_bins=0` removes residual automatic
   tuples without removing declared cross bins. Excluded, illegal, ignored,
   and zero-weight states remain present with their reasons and zero score.
5. The v3 runtime schema is 60, the SystemVerilog functional-coverage schema is
   7, the owning-unit schema is 30, and the portable schema is 13. Stale and
   future objects retain strict rejection, and the expanded scalar/tolerance/
   report state is covered by artifact and cache round trips.
6. The coverage parser remains below the structural ceiling after moving the
   coverpoint-bin implementation into the separately compiled 500-line
   `verilog_parser_coverage_bins.cpp` owner. The live feature inventories and
   source-package manifest contain no `.tpp` references. Two historical width-
   audit rows now point at their relocated compiled parser owner.
7. S23-B186-C06 is preserved. The SystemVerilog-2023 inventory has 32 active
   and 24 preserved rows at normalized SHA-256
   `00ab93ec0544f7e8d863fd49c19262be5bf6cf28057862a85da2a136df88dd8f`.
   The path-only VHDL-2019 inventory is frozen at normalized SHA-256
   `895808a7d68642d217e4f5655e15ece0878c954d33fcd4000b0cb36ae3fd391a`.
   The source manifest has 1,875 paths at SHA-256
   `e61fb3d76659dcc645bc67a2c5206d5b0ac30b1bfe692ef72333deda0793d5b5`.
8. Exact Clang warnings-as-errors Debug frontend, database, artifact, library,
   application, and LLVM targets build with eight workers. All 51 coverage-
   named tests pass. The focused 18-owner slice had one stale SystemVerilog
   width-inventory source anchor; after updating both SystemVerilog and Verilog
   width ledgers, the affected inventory, source-package, and portability tests
   pass 3/3. `git diff --check` is clean.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 7:
   implement revised utility system tasks and functions. Preserve all Changes
   1-6 work and do not begin file-I/O work from Change 8 in the same bounded
   slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 7

1. Batch 186 Changes 1-7 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 7 implements the clarified SystemVerilog severity-task behavior in
   constant functions for retained SystemVerilog profiles. `$info` and
   `$warning` become non-failing elaboration messages; `$error` and `$fatal`
   emit `FSIM-ELAB-SVCONST-002` errors and reject the build. Inactive branches
   do not emit effects.
3. Constant-evaluation messages reuse the bounded runtime output formatter and
   one evaluation may retain at most 4,096 effects. The application diagnostic
   engine now receives elaboration notes and warnings while the existing
   errors-only result remains the transactional build-failure surface.
4. Constant-call cache entries retain transitive effects as well as values and
   deliver them once per distinct full source call-site chain. Internal
   reevaluation therefore does not duplicate a message, while two physical
   calls with identical arguments each emit once. Callable behavior identity
   includes the full report payload and formatting state, preventing stale
   cache reuse after message-only source edits.
5. Formatter declarations have one narrow public owner in
   `include/fsim/runtime/output_format.hpp`; the SimIR internal header consumes
   it rather than redeclaring the API. No persistent schema or ABI changed.
6. Independently authored elaboration tests exercise 2017 and exact 2023
   profiles, formatting, inactive branches, repeated calls, and error
   rejection. The application test proves that notes and warnings survive the
   build boundary without failing it.
7. S23-B186-C07 is preserved. The SystemVerilog-2023 inventory has 31 active
   and 25 preserved rows at normalized SHA-256
   `843bcde265248ebf3f5781c258a84ec3cb51509c05c12420e1fd13ebffea2025`.
   The source manifest has 1,877 paths at SHA-256
   `92726d614f9d1fcf10b36eea73cd4962504245e6b1ba71f02a5c9f1067f82a7c`.
8. Exact Clang warnings-as-errors Debug frontend, elaboration, application,
   LLVM, object/design artifact, and library targets build with eight workers.
   The final focused 12-owner slice passes 12/12 and covers diagnostics,
   source-line, source-package, SystemVerilog-2023 inventory, and portability
   gates as well as all affected compiled owners.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 8:
   implement revised file and input/output tasks. Preserve all Changes 1-7 and
   do not begin compiler-directive work from Change 9 in the same bounded
   slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 8

1. Batch 186 Changes 1-8 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 8 corrects SystemVerilog native unformatted file transfers. `%u`
   moves exact width-derived two-state bytes in host byte order and collapses
   X/Z output bits to zero. `%z` moves native `aval`/`bval` pairs per 32-bit
   word and preserves 0/1/X/Z. Both directions reject field modifiers,
   incomplete input is transactional, and the transfer is rejected above the
   existing 4,096-byte bound.
3. Raw conversions are valid only for `$fwrite` and `$fscanf`. Terminal
   display, strobe, monitor, string formatting, constant-function severity
   formatting, and `$value$plusargs` reject `%u`/`%z` before runtime. This
   prevents binary NUL data from escaping through text-only interfaces.
4. `$ungetc` returns zero for a successful pushback and minus one for failure
   in both the interpreter and compiled callback path. Its private scanner
   primitive still returns the character so internal lookahead can confirm
   exact preservation.
5. Runtime and owning-unit serialization retain the new enum values with
   explicit validity checks. Runtime schema 61, owning-unit schema 31, and
   portable schema 14 reject every stale/future identity. Object, library,
   nested-state, producer-diagnostic, and ABI-reference contracts are
   synchronized; the ABI reference also records the already-active class
   schema 12 rather than its stale schema-11 text.
6. Independently authored tests cover frontend identities and contextual
   rejection, elaborated operations, byte-level two-state/four-state round
   trips, X/Z preservation, incomplete input, pushback results, interpreter
   execution, compiled LLVM O0/O2 cold/warm execution, and artifact/cache
   reuse.
7. S23-B186-C08 is preserved. The SystemVerilog-2023 inventory has 30 active
   and 26 preserved rows at normalized SHA-256
   `8e6f76d3d929008e2991c61adbbbd9c558fc2fb327da20e91b476be40ee1cf2a`.
   The source manifest remains at 1,877 paths and SHA-256
   `92726d614f9d1fcf10b36eea73cd4962504245e6b1ba71f02a5c9f1067f82a7c`.
8. Exact Clang warnings-as-errors Debug targets build with eight workers. The
   focused frontend, elaboration, runtime, LLVM, application, library,
   object/design artifact, schema, inventory, ABI-reference, and portability
   slice passes 16/16 in 27.05 wall seconds.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 9:
   implement revised compiler directives. Preserve all Changes 1-8 and do not
   begin module/hierarchy work from Change 10 in the same bounded slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 9

1. Batch 186 Changes 1-9 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 9 closes the SystemVerilog-2023 compiler-directive revision delta.
   Parenthesized `ifdef`, `ifndef`, and `elsif` expressions use identifier
   macro-definedness, logical precedence, nested grouping, and right-associated
   implication/equivalence. Earlier profiles reject the expression form with
   `FSIM-SV-PP-052`; malformed expressions retain `FSIM-SV-PP-053` and recover
   without selecting invalid text.
3. The exact `begin_keywords "1800-2023"` selector is accepted only under the
   2023 profile. Existing nested keyword stacks restore the prior set, and the
   2017 and older profiles cannot opt into the later selector.
4. Application evidence proves compilation-unit scope: an explicit
   `source-set` shares ordered macro/directive state across roots and assigns
   one unit digest, while `file` mode isolates state and assigns distinct
   digests. No global or cross-project preprocessing state was introduced.
5. The Verilog/SystemVerilog preprocessor cache identity advances to
   `fsim-verilog-preprocessor-v7-systemverilog-2023-directives`, preventing
   stale reuse after the association correction. No persisted HIR, object,
   design, checkpoint, runtime, native ABI, or plugin schema changed.
6. The Windows LLVM contract now scans the compiled `llvm_jit_*.cpp/.hpp`
   owners after removal of non-template `.tpp` debt. The tool-portability
   contract includes the split `api_systemc_test.cpp` owner; both stale-path
   failures are green.
7. S23-B186-C09 is preserved. The SystemVerilog-2023 inventory has 29 active
   and 27 preserved rows at normalized SHA-256
   `16b453755ace27e206299bff04882cbff6dc068105a20c505ff85d039cb2e277`.
   The source manifest remains at 1,877 paths and SHA-256
   `92726d614f9d1fcf10b36eea73cd4962504245e6b1ba71f02a5c9f1067f82a7c`.
8. Exact Clang warnings-as-errors Debug targets build with eight workers. The
   final frontend, application, standard-mode, LLVM, API, artifact,
   diagnostics, inventory, source, and portability closure passes 23/23 in
   126.81 wall seconds. `git diff --check` is clean.
9. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 10:
   implement revised module and hierarchy behavior. Preserve all Changes 1-9
   and do not begin program-block work from Change 11 in the same bounded
   slice.
10. At Batch 186 Change 20, first compile the clean Release configuration and
    resolve every compiler error, then run the complete Release suite. Only
    after Release is green, run the clean Debug suite so a Release-driven source
    repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 10

1. Batch 186 Changes 1-10 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 10 preserves module body-parameter locality. SystemVerilog modules
   with any parameter-port list, including `#()`, make directly contained body
   `parameter` declarations local. Verilog-2001/2005 does so only for a
   nonempty parameter-port assignment list. Program and interface behavior is
   unchanged for their later owned slices.
3. Bind directives retain their source revision and owner library. Exact
   instance targets win over same-named type scopes. Retained profiles preserve
   type-wide name matching across configured logical-library instances and
   continue to apply ordinary configuration rules to generated bound instances.
4. Under SystemVerilog-2023, the controlling configuration selects both the
   type-wide bind target scope and the generated bound module through cell and
   default-library mappings. An instance mapping aimed at the generated bound
   instance is ignored, while cell mappings remain eligible. The selected
   logical library reaches ordinary specialization and child elaboration.
5. Independently authored tests cover retained and revised parameter locality,
   invalid local-parameter overrides, multi-library name matching, exact
   hierarchy presence and specialization library, revised ignored instance
   mapping, 137-bit values, interpreter and LLVM O0/O2 execution, and
   cold/warm native-cache
   reuse. No persisted schema, native ABI, or plug-in ABI changed.
6. S23-B186-C10 is preserved. The SystemVerilog-2023 inventory has 28 active
   and 28 preserved rows at normalized SHA-256
   `6edbbed5338c9b15ee88947b85c6668630db152a29eb9112bdf789facb38d840`.
   The source manifest remains at 1,877 paths and SHA-256
   `92726d614f9d1fcf10b36eea73cd4962504245e6b1ba71f02a5c9f1067f82a7c`.
7. Exact Clang warnings-as-errors Debug frontend and application targets build
   with eight workers. The final O0/O2 hierarchy application test passes, and
   the focused frontend, hierarchy, diagnostics, inventory, standard-mode,
   source, resource, portability, artifact, API, runtime, and CTest dependency
   closure passes 23/23 in 105.48 wall seconds.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 11:
   implement revised program-block behavior. Preserve all Changes 1-10 and do
   not begin interface/modport work from Change 12 in the same bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 16

1. Batch 186 Changes 1-16 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Simple module paths now parse polarity before `=>` or `*>`. Parallel paths
   require one source and one destination, while full paths retain lists.
   Edge-sensitive paths require a grouped destination and data source, reject
   source polarity, and cannot follow `ifnone`; simple paths reject the grouped
   edge-sensitive form. `FSIM-SV-SEM-391` and `FSIM-SV-SEM-392` own recovery.
3. Timing-check events resolve only scalar module terminals. Notifiers resolve
   only scalar variables, excluding nets from scheduler-owned notifier state.
   Public primary-source review confirmed that module-path edge identifiers
   remain `posedge`/`negedge` and that the `$width` threshold remains optional,
   so neither surface was changed.
4. Independently authored exact-SystemVerilog-2023 frontend and elaboration
   matrices cover polarity placement, parallel/full cardinality, grouped data
   forms, `ifnone`, internal-event rejection, net-notifier rejection, all
   twelve timing checks, and stable diagnostics.
5. Exact-2023 mixed-language application evidence covers report/notifier
   ordering, runtime-state serialize/restore, mapped-library relocation, VCD,
   and interpreter plus LLVM O0/O2 cold/warm/edit-cache equality. Every one of
   the three hierarchical notifiers reaches the expected asserted value.
6. S23-B186-C16 is preserved. The SystemVerilog-2023 inventory has 22 active
   and 34 preserved rows at normalized SHA-256
   `3adacaa9f83fb009f431cbe006640672c3261bde4b850600ba5523a3de538e54`.
   The source manifest remains at 1,878 paths and SHA-256
   `598f08c43cbb314411b132a2e29f8f2d3f8e0f74f2b36f171b71a52958949b1f`.
7. Exact Clang warnings-as-errors Debug frontend, elaboration, library,
   application, runtime, LLVM, and API targets build with eight workers. The
   focused diagnostics, source-line, source-manifest, inventory, portability,
   schema, artifact, API, runtime, elaboration, application, and LLVM closure
   passes 28/28 in 40.35 wall seconds. `git diff --check` is clean.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 17:
   implement SDF backannotation revisions. Preserve all Changes 1-16 and do not
   begin configuration/protected-envelope work from Change 18 in the same
   bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 17

1. Batch 186 Changes 1-17 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. SDF `IOPATH` linking now requires one exact elaborated path after instance,
   packed-selection, source-edge, destination, and unconditional, conditional,
   or `CONDELSE` filtering. Timing-check linking uses the exact instance prefix,
   reference/data terminal selections, and edge transitions.
3. Edge wrappers can reorder normalized SDF endpoint atoms. Resolution no
   longer trusts that order: the unique elaborated path or timing check binds
   and republishes source/destination or reference/data roles. Wrong-edge,
   wrong-select, and swapped-role candidates cannot steal an annotation.
4. A second exact path or check rejects the full transaction with
   `FSIM-SDF-ENDPOINT-006`; no partial mapping is published. Existing missing,
   ambiguous-object, stale-state, and resource diagnostics remain unchanged.
5. Independently authored endpoint evidence covers conditional, `CONDELSE`,
   edge-qualified path and timing-check ownership, decoy candidates, exact
   ambiguity, mixed VHDL/SystemVerilog/SystemC endpoints, and all supported SDF
   revisions. The drive/timing application differential now selects the exact
   SystemVerilog-2023 profile and retains interpreter/LLVM equality.
6. S23-B186-C17 is preserved. The SystemVerilog-2023 inventory has 21 active
   and 35 preserved rows at normalized SHA-256
   `d59d593bf6d4a25199ba15eb685c4137af253d2af28e743b3a573cb6e1287cd0`.
   The source manifest remains at 1,878 paths and SHA-256
   `598f08c43cbb314411b132a2e29f8f2d3f8e0f74f2b36f171b71a52958949b1f`.
7. Exact Clang warnings-as-errors Debug application and focused SDF targets
   build with eight workers. The diagnostics, source-line, source-manifest,
   inventory, portability, schema, artifact, API, runtime, SDF, specify, and
   LLVM closure passes 30/30 in 31.69 wall seconds. Relevant source files stay
   below 2,000 lines and `git diff --check` is clean.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 18:
   implement configuration and protected-envelope revisions. Preserve all
   Changes 1-17 and do not begin whole-surface engine closure from Change 19 in
   the same bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 18

1. Batch 186 Changes 1-18 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Protected envelopes now retain the opening span, hide lexer diagnostics
   whose byte offsets fall inside the physical payload, and diagnose a missing
   `end_protected` at compilation-unit completion. Payload text does not escape
   through emitted tokens or diagnostics, while declarations before and after
   a closed envelope remain visible.
3. Include boundaries segment protected byte ranges. An envelope opened in an
   included file and closed by the including root keeps both physical payload
   segments opaque, including lexer-invalid bytes. Existing unavailable-
   provider, nested-envelope, and unmatched-end diagnostics remain stable.
4. The preprocessor cache identity advances to
   `fsim-verilog-preprocessor-v8-protected-envelopes`, so stale diagnostic
   streams cannot survive the semantic change.
5. Independently authored exact-SystemVerilog-2023 configuration evidence
   composes an outer `use ... : config` rule with a different-library design
   root and an exact descendant remap. Specialization ownership is exact and
   interpreter/LLVM O0/O2 results agree. A missing nested configuration rejects
   the selected instance with `FSIM-ELAB-SVCONFIG-003`.
6. S23-B186-C18 is preserved. The SystemVerilog-2023 inventory has 20 active
   and 36 preserved rows at normalized SHA-256
   `3d595f0dcb409c4a6bae9090295f2951944ed6d2ecf334d616ecd319f30b4455`.
   The source manifest remains at 1,878 paths and SHA-256
   `598f08c43cbb314411b132a2e29f8f2d3f8e0f74f2b36f171b71a52958949b1f`.
7. Exact Clang warnings-as-errors Debug frontend and application targets build
   with eight workers. The focused configuration, preprocessing, diagnostics,
   source-budget, manifest, inventory, portability, schema, and Windows/LLVM
   closure passes 14/14 in 39.14 wall seconds. The preprocessor implementation
   is exactly 2,000 authored lines under the structure checker, the other
   relevant files remain below the limit, and `git diff --check` is clean.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 19: prove
   verification and timing behavior across interpreter and LLVM engines.
   Preserve all Changes 1-18 and do not begin Change 20 closure in the same
   bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 19

1. Batch 186 Changes 1-19 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. `cmake/RunSystemVerilogClosureMatrix.cmake` now runs 26 serial witnesses.
   The existing semantic, frontend, LLVM, artifact, VPI, UVM, and runtime lanes
   are joined by elaboration, assertions/checkers, randomization, coverage,
   file I/O, directives, hierarchy/configuration, interfaces, conformance,
   gates/UDPs, specify timing, and exact SDF endpoint/drive witnesses.
3. `tests/CMakeLists.txt` assigns those same witnesses to the closure fixture,
   so an ordinary complete CTest run executes each once and the closure driver
   only verifies fixture completion. A direct matrix run retains per-witness
   logs and the 1,200-second stage timeout.
4. `CheckSystemVerilog2023Inventory.cmake` freezes all thirteen added witness
   names, their fixture ownership, the 26/26 count, and the 29-stage status
   contract. The direct Debug matrix passes 26/26 in 74.18 wall seconds.
5. The incremental exact-Clang warnings-as-errors Debug build exposed one
   split-target defect: `fsim_sv_container_elaboration_tests` used the shared
   `has_diagnostic` helper without its implementation owner. Adding
   `elaborator_test_support.cpp` to that executable resolves the link, and the
   complete incremental Debug tree now builds with eight workers.
6. S23-B186-C19 is preserved. The SystemVerilog-2023 inventory has 19 active
   and 37 preserved rows at normalized SHA-256
   `ca4193e587e93d48ac90bc8d63c5085d5a7b79a7b8e160e2074324a5994e974e`.
   The source manifest remains at 1,878 paths and SHA-256
   `598f08c43cbb314411b132a2e29f8f2d3f8e0f74f2b36f171b71a52958949b1f`.
7. The inventory, source-line, source-package, and resource-governance slice
   passes 4/4 in 15.47 wall seconds, and `git diff --check` is clean.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 20. First
   compile the clean Release configuration and resolve compiler errors, then
   run the complete Release suite. Only after Release is green, run the clean
   Debug suite. Change 20 also owns documentation finalization, one
   implementation commit, and one push. Sanitizers and hosted CI remain
   reserved for Batch 188 under the active goal.

## Batch 186 active checkpoint - after Change 11

1. Batch 186 Changes 1-11 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 11 retains anonymous-program task, function, and ordinary-class
   declarations in the compilation-unit namespace. An interface class within
   an anonymous program is accepted only by the exact SystemVerilog-2023
   profile. Duplicate callables, malformed interface items, unsupported items,
   missing terminators, and forbidden anonymous-program end labels have stable
   diagnostics and recovery.
3. Direct and generated `$info`, `$warning`, `$error`, and `$fatal` items in a
   named 2023 program are source-owned report statements. Generate expansion
   selects the active branch before hierarchy elaboration evaluates its reports
   with the common bounded SystemVerilog constant formatter. Selected items run
   once, notes and warnings retain deterministic build-message order, errors
   and fatal reports reject the design, and no runtime process is materialized.
4. Retained program reactive scheduling is unchanged. The Change 10 regression
   found by the full elaboration target is repaired: pre-2023 type-wide binds
   again apply across configured logical-library instances, and only 2023 bound
   instances bypass instance-specific configuration mappings.
5. Independently authored tests cover 2017/2023 profile isolation, retained
   anonymous-program ownership, direct and conditional-generate HIR, parameter
   formatting, unselected-branch suppression, repeated builds, error rejection,
   zero runtime process materialization, and retained/revised bind mapping. No
   persisted schema, native ABI, plug-in ABI, or cache identity changed.
6. S23-B186-C11 is preserved. The SystemVerilog-2023 inventory has 27 active
   and 29 preserved rows at normalized SHA-256
   `dc748cea606ccbba0c4bb27a5120c50b032581cd9591636c658c3cee3214e3cc`.
   The source manifest remains at 1,877 paths and SHA-256
   `92726d614f9d1fcf10b36eea73cd4962504245e6b1ba71f02a5c9f1067f82a7c`.
7. Exact Clang warnings-as-errors Debug frontend, elaboration, and application
   targets build with eight workers. Frontend, elaboration, hierarchy,
   diagnostics, inventory, standard-mode, source, resource, portability,
   artifact, API, runtime, LLVM, and CTest dependency closure passes 23/23 in
   52.54 wall seconds. `git diff --check` is clean.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 12:
   implement revised interface, modport, clocking-connection, and virtual-
   interface behavior. Preserve all Changes 1-11 and do not begin package
   lookup work from Change 13 in the same bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 12

1. Batch 186 Changes 1-12 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 12 validates the source side of virtual-interface initialization and
   procedural assignment. Null, a same-specialization virtual interface, and a
   direct scalar or statically indexed same-specialization interface instance
   are accepted. An unrestricted source may narrow to a selected modport; a
   selected source cannot widen or rebind.
3. Interface parameter compatibility uses canonical elaborated identity, so
   default, positional, named, value, and type parameter spellings compare by
   resolved specialization rather than source syntax. Concrete initializer
   checks now also enforce the default specialization instead of treating an
   omitted virtual-interface parameter list as a wildcard.
4. Direct interface-instance sources receive deterministic, nonzero,
   pointer-free identities before process lowering and become ordinary 64-bit
   constant loads. Interpreter and LLVM therefore share the existing scalar
   operation path without a new callback, scheduler boundary, persisted schema,
   native ABI, plug-in ABI, or cache identity.
5. Independently authored tests cover virtual and concrete sources, static
   interface arrays, null transitions, legal narrowing, illegal widening and
   rebinding, parameter mismatch, retained 2017 elaboration, and 2023
   interpreter plus LLVM O0/O2 cold/warm execution with exact values and VCD.
6. `FSIM-ELAB-SVIFACE-012` owns incompatible assignment sources. S23-B186-C12
   is preserved. The SystemVerilog-2023 inventory has 26 active and 30
   preserved rows at normalized SHA-256
   `8635427a970f990287622c888545dac3fba4e479fb676ef94ae08cf829034b72`.
   The source manifest remains at 1,877 paths and SHA-256
   `92726d614f9d1fcf10b36eea73cd4962504245e6b1ba71f02a5c9f1067f82a7c`.
7. Exact Clang warnings-as-errors Debug elaboration and application targets
   build with eight workers. The focused frontend, elaboration, interface,
   diagnostics, inventory, standard-mode, source, resource, portability,
   artifact, API, runtime, LLVM, and CTest dependency closure passes 28/28 in
   101.45 wall seconds. `git diff --check` is clean.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 13:
   implement revised package, import, export, and lookup behavior. Preserve all
   Changes 1-12 and do not begin generate work from Change 14 in the same
   bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 active checkpoint - after Change 13

1. Batch 186 Changes 1-13 are complete and intentionally uncommitted on
   `codex/v3`; preserve the accumulated worktree through Change 20. The latest
   pushed base remains `a02f9635`.
2. Change 13 parses one or more package imports between a module, interface, or
   program name and its parameter-port or port list. Those imports are visible
   while parsing header parameter defaults and port types. A header import not
   followed by a legal parameter or port surface reports `FSIM-SV-PARSE-385`.
3. Wildcard imports remain potential until an unqualified reference or explicit
   re-export requires the name. Local declarations take precedence over
   wildcard candidates; a selected import that conflicts with a direct local
   declaration reports `FSIM-ELAB-SVPKG-009`.
4. Parameter and type dependencies are expanded before import materialization.
   Re-export ownership uses declaration identity rather than the path by which a
   declaration arrived, so the same constant, type, let, function, task,
   ordinary class, or interface class may be re-exported through multiple
   packages without a false ambiguity.
5. Independently authored frontend, elaboration, and application tests cover
   header visibility and recovery, local shadowing, unused duplicate wildcard
   candidates, explicit conflicts, transitive re-exports, interpreter
   execution, and LLVM O0/O2 cold/warm cache reuse. Two legacy fixtures now use
   standard header imports, and the retained ambiguity fixture references its
   conflicting name.
6. S23-B186-C13 is preserved. The SystemVerilog-2023 inventory has 25 active and
   31 preserved rows at normalized SHA-256
   `4ba2d4fcd448073a2734478636d05efd614f6726f1771bd184021ffde5c4c4e1`.
   The source manifest remains at 1,877 paths and SHA-256
   `92726d614f9d1fcf10b36eea73cd4962504245e6b1ba71f02a5c9f1067f82a7c`.
7. Exact Clang warnings-as-errors Debug frontend, elaboration, and application
   targets build with eight workers. The focused frontend, elaboration,
   application, diagnostics, inventory, portability, source, schema, artifact,
   API, runtime, and LLVM closure passes 22/22 in 24.97 wall seconds.
8. No Release qualification, clean-first build, sanitizer, hosted-CI
   inspection, commit, or push ran. Proceed only to Batch 186 Change 14:
   implement generate-construct revisions. Preserve all Changes 1-13 and do not
   begin gate, switch, or UDP work from Change 15 in the same bounded slice.
9. At Batch 186 Change 20, first compile the clean Release configuration and
   resolve every compiler error, then run the complete Release suite. Only
   after Release is green, run the clean Debug suite so a Release-driven source
   repair cannot invalidate Debug qualification.

## Batch 186 completion checkpoint

1. Batch 186 Changes 1-20 are complete on `codex/v3`. The accumulated batch
   worktree is ready for its one implementation commit and push; the latest
   pushed base before that commit is `a02f9635`.
2. Change 20 repaired the package-import qualification regressions in
   `fsim.application.sv_functions` and
   `fsim.application.sv_aggregate_multidimensional`. Imported type dependency
   discovery uses the reference occurrence, includes type-parameter defaults,
   and excludes parser-created implicit-net placeholders from explicit local
   declaration shadowing. Both cases pass in focused and complete runs.
3. The non-template header/`.tpp` remediation, including legacy debt, is
   complete. The translation-unit structure contract measures 1,510 authored
   sources, zero `.tpp` files, and zero sources over 2,000 lines. The source
   package contains 1,878 ordered paths and 23 governed exclusions; SPDX and
   test/control inventories contain 1,813 and 775 entries respectively.
4. Release qualification ran first as required. The clean exact-Clang
   warnings-as-errors Release configuration compiled 3,131 build steps with
   eight workers, and its complete suite passed 406/406 in 149.90 seconds.
5. Only after Release was green, the clean exact-Clang warnings-as-errors Debug
   configuration compiled 3,131 build steps with eight workers. Its complete
   suite passed 406/406 in 172.42 seconds. The fresh Debug dependency tree used
   the already-verified Tcl 9.0.4 source archive from the clean Release tree
   after the sandbox could not resolve the upstream host; the archive SHA-256
   was verified before use.
6. The SystemVerilog-2023 inventory remains at nineteen active and 37
   preserved rows with normalized SHA-256
   `ca4193e587e93d48ac90bc8d63c5085d5a7b79a7b8e160e2074324a5994e974e`.
   The 1,878-path source manifest remains at SHA-256
   `598f08c43cbb314411b132a2e29f8f2d3f8e0f74f2b36f171b71a52958949b1f`,
   and the release feature matrix is frozen at SHA-256
   `72ffbd202b8f0d995de037bed421f2164bc09e3b6c11d40924bb72be5de38331`.
7. No sanitizer or hosted-CI lane ran. Those remain reserved for Batch 188
   under the active goal. After the single Batch 186 commit and push, proceed
   to Batch 187 Change 1 only: implement the SystemVerilog-2023 DPI declaration
   and runtime revisions. Preserve the exact 20-change cadence and do not pull
   Change 2 header work into the same bounded slice.

## Batch 187 Change 1 checkpoint

1. Batch 187 Change 1 is complete in the accumulated `codex/v3` worktree.
   DPI imports retain optional formal names and legal named-input defaults,
   reject `ref` and `const ref`, and compare canonical name/default-independent
   profiles whenever multiple declarations share one C linkage name across
   compilation-unit or design-unit ownership.
2. Runtime exported-callback and imported-task registries accept explicit v3
   declaration contracts. Export callbacks reject import-only qualifiers;
   imported tasks require task kind and reject `pure` before publication.
3. Exact Clang warnings-as-errors Debug builds of `fsim_frontend_tests` and
   `fsim_runtime_tests` succeeded with eight workers. Their complete binaries
   pass. The SystemVerilog-2023 inventory has 18 active and 38 preserved rows
   at normalized SHA-256
   `f49635744a00e284a0f80c446aeae97e24c0abdac96b551c7865f397ad4f2544`.
4. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 2: update the standard DPI C layer and
   `svdpi.h`. Do not begin foreign-code inclusion/context work from Change 3
   in the same bounded slice.

## Batch 187 Change 2 checkpoint

1. Batch 187 Changes 1-2 are complete in the accumulated `codex/v3`
   worktree. Preserve both changes uncommitted through Change 20.
2. A root-level installed `svdpi.h` owns the canonical scalar, vector, open-
   array, scope, context, and SystemVerilog-2023 time declarations. C11 and
   C++20 probes freeze its layouts, constants, and representative signatures.
3. The shared `fsim_tf` link surface exports the standard implementation
   version and normalized canonical bit-select/part-select utilities. Focused
   runtime evidence covers word crossings and exact aval/bval four-state
   behavior.
4. Installed-public, binary-install ownership, foreign-ABI freeze, source-
   package, source-line, resource-portability, and SystemVerilog-2023 inventory
   policies govern the new public surface. Deprecated implementation-specific
   packed-array helpers remain excluded from the portable boundary.
5. S23-B187-C02 is preserved. The inventory has 17 active and 39 preserved
   rows at normalized SHA-256
   `76ec401a542e44250549ca43b4c7b809d284ff6196f8b85c8ade9cbfa6965526`.
   The source package has 1,880 files at SHA-256
   `afd51c75b2a79f2abd03c3ebbba353e9766408034fd1458728a6c57fd4fa77aa`.
6. Exact Clang warnings-as-errors Debug runtime compilation and the complete
   runtime binary pass. The focused runtime, inventory, ABI freeze, source,
   install-ownership, installed-public, and portability policy set passes 8/8
   in 35.61 wall seconds. No Release, clean-first, sanitizer, hosted-CI,
   commit, or push action ran.
7. Proceed only to Batch 187 Change 3: implement revised foreign source
   inclusion and context behavior. Do not begin the PLI/VPI overview work from
   Change 4 in the same bounded slice.

## Batch 187 Change 3 checkpoint

1. Batch 187 Changes 1-3 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. DPI foreign source planning selects explicit C11 for `.c` and C++20 for
   `.cc`/`.cpp`/`.cxx` on POSIX and MSVC-compatible argv surfaces while
   retaining path, duplicate, visibility, calling-convention, and shell-
   independence policy.
3. A versioned 112-byte x86-64 C bridge owns a bounded 64-frame active-context
   stack. Export callbacks and imported tasks enter and leave the exact top
   frame across success, re-entry, exception, suspension, and disable paths.
4. Standard scope, name, scope lookup, caller, scope-keyed user-data, disabled-
   state, simulation-time, time-unit, and time-precision APIs reach only the
   current simulation. A real independently authored C plug-in routine proves
   those calls through a loaded symbol; outside an invocation they fail safely.
5. S23-B187-C03 is preserved. The inventory has 16 active and 40 preserved
   rows at normalized SHA-256
   `1f3795b88ecf4443486a740b7a571f50c781ab8c86ba2f6e078d0981b3212a25`.
   The source package has 1,881 files at SHA-256
   `aac27442cec7438a1b60d46d188ec5941600050fdf421a869cc7fa31326c0da0`.
6. Exact Clang warnings-as-errors Debug runtime compilation and the complete
   runtime binary pass. The focused runtime, inventory, ABI freeze, source,
   install, and portability closure passes 8/8 in 29.40 wall seconds. No
   Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
7. Proceed only to Batch 187 Change 4: reconcile the standardized PLI/VPI
   lifecycle overview with the direct v3 plug-in model. Do not begin the VPI
   object-model work from Change 5 in the same bounded slice.

## Batch 187 Change 4 checkpoint

1. Batch 187 Changes 1-4 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. The VPI loader accepts either the validated direct v3 bind descriptor or,
   when that symbol is absent, the standardized ordered null-terminated
   `vlog_startup_routines` table. It never combines the two entry models.
3. Direct v3 startup/shutdown containment and exactly-once teardown remain
   unchanged. Standard startup is bounded at 4,096 entries and publishes the
   image only after every entry returns; no nonstandard shutdown is invented.
4. An independently authored C plug-in proves two ordered standard startup
   routines and observable effects. C11/C++20 probes freeze the standard symbol
   and routine-pointer contract alongside the existing direct ABI.
5. S23-B187-C04 is preserved. The inventory has 15 active and 41 preserved
   rows at normalized SHA-256
   `62e9c5c6f14d401645c89826eac72ac93d715d526a9d0c7b456e8ae170e224cc`.
   The source package has 1,882 files at SHA-256
   `81437e575d304d48355bfbe363101be3ec23bdb8137a1e0498c6a8d8b12255e8`.
6. Exact Clang warnings-as-errors Debug runtime compilation and the complete
   runtime binary pass. The focused runtime, inventory, ABI freeze, source,
   install, and portability closure passes 8/8 in 21.13 wall seconds. No
   Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
7. Proceed only to Batch 187 Change 5: update the complete VPI object model.
   Do not begin added/revised VPI routine implementation from Change 6 in the
   same bounded slice.

## Batch 187 Change 5 checkpoint

1. Batch 187 Changes 1-5 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. The VPI object-kind enum is append-only. Root through min/typ/max retain
   explicit identities 0 through 21, and the complete 2023 hierarchy,
   primitive, callable, statement, assertion, coverage, declaration, type,
   selection, call, and attribute taxonomy follows those values.
3. Object lookup now exposes live-child counts and canonical creation
   ordinals. Generic properties own their returned strings, carry explicit
   result types, inherit existing type provenance, and distinguish absent
   optional metadata from an unsupported property.
4. Typed iterators select one exact kind globally or below a validated parent.
   Relationship iterators select parent, children, internal scopes,
   declarations, ports, nets, variables, parameters, processes, assertions,
   drivers, expressions, arguments, types, and coverage in deterministic
   creation order. Every scan revalidates generation and liveness.
5. Independently authored runtime evidence covers numeric stability,
   capabilities, structural objects, properties, typed and relationship
   traversal, invalid selectors, cross-simulation rejection, and release after
   snapshot. `vpi_object.cpp` remains at 1,997 lines.
6. S23-B187-C05 is preserved. The inventory has 14 active and 42 preserved
   rows at normalized SHA-256
   `2fb4beb07ebaf03112c36bb38a69fea0a961cdd0760d36353ecc34b638708ea1`.
   The source package remains at 1,882 paths and SHA-256
   `81437e575d304d48355bfbe363101be3ec23bdb8137a1e0498c6a8d8b12255e8`.
7. Exact Clang warnings-as-errors Debug runtime compilation succeeds and the
   complete runtime binary passes. Source-line, source-package, inventory, and
   resource-portability policy tests pass. No Release, clean-first, sanitizer,
   hosted-CI, commit, or push action ran.
8. Proceed only to Batch 187 Change 6: implement every revised or added VPI
   routine. Do not begin the assertion API from Change 7 in the same bounded
   slice.

## Batch 187 Change 6 checkpoint

1. Batch 187 Changes 1-6 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. `SystemVerilogVpiRoutineKind` and its index-stable 42-entry catalog cover
   the complete 2023 callback, system-callable, hierarchy, property, value,
   delay, time, lifetime, comparison, data, user-data, control, diagnostic,
   formatted-output, MCD, and file routine surface. Every entry names one sized
   service family and explicit handle/result/callback policy.
3. `invoke_systemverilog_vpi_2023_routine` validates the v2 host prefix,
   callback, request size/family, one-megabyte text ceiling, and null/optional/
   required handle policy before callback entry. It contains exceptions and
   validates callback/result status, size, reserved fields, and required
   returned handles before publication.
4. Stable `FSIM-VPI-ROUTINE-001` through `-007` error views distinguish unknown
   routines, invalid hosts, invalid requests, missing handles, exceptions,
   callback rejection, and malformed results. Invalid host callback pointers
   are never entered.
5. Independently authored runtime evidence freezes all 42 names and indices,
   rejects unknown/vendor selectors, and covers every dispatch failure class.
   S23-B187-C06 is preserved. The inventory has 13 active and 43 preserved rows
   at normalized SHA-256
   `afdb5f6a0487117f5bdcd48867b4d121fbfe6c7cf1d3301bbe5cd49722d26a10`.
   The source package remains at 1,882 paths and SHA-256
   `81437e575d304d48355bfbe363101be3ec23bdb8137a1e0498c6a8d8b12255e8`.
6. Exact Clang warnings-as-errors Debug runtime compilation and the complete
   runtime binary pass. Source-line, source-package, inventory, and resource-
   portability gates pass. No Release, clean-first, sanitizer, hosted-CI,
   commit, or push action ran.
7. Proceed only to Batch 187 Change 7: implement the assertion API. Do not
   begin the standardized coverage API validation from Change 8 in the same
   bounded slice.

## Batch 187 Change 7 checkpoint

1. Batch 187 Changes 1-7 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. `SystemVerilogVpiAssertionApi` validates generation-qualified assertion
   objects and exposes global or object-selected reset, enable, disable, and
   kill controls. A common-control hook must accept the request before API
   state changes; failure and exception paths leave no partial state.
3. Integrated `Simulation` control uses the existing concurrent-assertion
   spawn filter, target enable/disable overrides, dynamic-process cancellation,
   and pending-attempt map. HDL and foreign controls therefore share one
   scheduler/execution model.
4. Every common completion records saturating attempt, success, failure,
   vacuous, disabled, and aborted counts before using the existing persistent
   VPI callback manager. Disabled observations remain visible but do not count
   as attempts. Object tracking is limited to 65,536 entries and combined
   event text to one MiB; reservations preserve resource checks across control
   re-entry.
5. Runtime evidence covers all assertion kinds/outcomes and failure classes.
   Application evidence globally disables the assertion engine, selectively
   enables one hierarchy assertion, executes it, and matches API statistics to
   the callback stream. Exact Clang warnings-as-errors Debug runtime and VPI
   application targets compile; `fsim.runtime` and `fsim.application.vpi` pass.
6. S23-B187-C07 is preserved. The inventory has 12 active and 44 preserved
   rows at normalized SHA-256
   `2b2cbaf8c4d55140ed3606c9a7524ab0a160895be3f8e3a63513a72881075850`.
   No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
7. Proceed only to Batch 187 Change 8: validate the complete standardized
   coverage API against the unified v3 coverage database. Do not begin the
   data-read API from Change 9 in the same bounded slice.

## Batch 187 Change 8 checkpoint

1. Batch 187 Changes 1-8 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. The standardized coverage service retains the exact six controls, four
   metric types, presence and aggregate properties, six assertion counters,
   FSM/state relations and values, generation-qualified iterators, and typed
   failure boundaries. New evidence enters every control/type family and
   contains statistics and control provider exceptions.
3. Integrated assertion coverage now reads Change 7 assertion API state
   directly. Start, stop, reset, and check map to the common enable, disable,
   reset, and saturation state; attempt, success, failure, vacuous, disabled,
   and killed properties cannot drift from the callback completion stream.
4. An independently authored coverage-enabled application fixture publishes a
   bounded statement inventory, runs concurrent assertions, compares coverage
   properties to assertion API statistics, atomically saves and decodes a
   direct-v3 `.fsimcov`, and proves duplicate-run merge rejection leaves it
   unchanged. Unavailable toggle storage returns NOCOV and creates no file.
5. The post-documentation database schema/model/codec/merge/partial-merge/
   robustness, SystemVerilog functional, PSL, VPI application/runtime,
   source-budget, source-package, resource-portability, and inventory lane
   passes 14/14 in 29.77 wall seconds. Its first run found one inventory
   evidence-owner formatting error; the correction retains one repository
   path per ownership field.
6. S23-B187-C08 is preserved. The inventory has 11 active and 45 preserved
   rows at normalized SHA-256
   `255ead76f3d672af85aa021e2f8bdc498bd361cd1bfa5ba725be3fa030f34916`.
   The source package remains at 1,882 paths and SHA-256
   `81437e575d304d48355bfbe363101be3ec23bdb8137a1e0498c6a8d8b12255e8`.
7. Exact Clang warnings-as-errors Debug VPI runtime/application targets compile
   with eight workers. No Release, clean-first, sanitizer, hosted-CI, commit,
   or push action ran.
8. Proceed only to Batch 187 Change 9: implement the standardized data-read
   API. Do not begin normative header updates from Change 10 in the same
   bounded slice.

## Batch 187 Change 9 checkpoint

1. Batch 187 Changes 1-9 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. `SystemVerilogVpiDataReadService` freezes the standard traverse/collection,
   property, access, loaded-iteration, and navigation identities. It supports
   limited-interactive, history-preserving interactive, and post-process
   histories over the existing generation-qualified VPI registry.
3. Scope and collection loading selects only value-bearing objects. A
   collection transaction stages only its affected histories before an
   all-or-none commit; it never takes a whole-registry or whole-history
   snapshot. Loaded-object iteration is declaration ordered and hierarchy
   filtered.
4. Object/traverse collections, kind and Boolean filtering, single and common-
   time collection traversal, minimum/maximum/previous/next/time navigation,
   no-value positions, exact canonical values, unload/close behavior, and
   explicit reader-handle release are implemented with distinct checked
   failures and resource ceilings.
5. Every `Simulation` owns a live limited-interactive reader clocked by the
   common scheduler. Application evidence loads a real hierarchy object and
   observes its effective forced value through both interpreter and LLVM runs.
6. Exact Clang warnings-as-errors Debug runtime/application targets compile
   with eight workers. The post-documentation source-line, source-package,
   SystemVerilog-2023 inventory, resource-portability, VPI application,
   complete runtime, and data-read lane passes 7/7 in 16.06 wall seconds.
7. S23-B187-C09 is preserved. The inventory has 10 active and 46 preserved
   rows at normalized SHA-256
   `47b41d236d3d7284556080298cef7b548aca6ac0cd43e0b3ace4cf43e02a4c61`.
   The source package has 1,885 paths at SHA-256
   `642a2c5b0b60e695c56fb4cace39742d55f63438678f2eac251b30d7c36b85a6`.
8. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 10: update the normative VPI and
   compatibility headers over this typed reader service.

## Batch 187 Change 10 checkpoint

1. Batch 187 Changes 1-10 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. The installed SDK now owns root `vpi_user.h` and `sv_vpi_user.h` names plus
   the internal `vpi_abi.h` and `vpi_bridge.h`. C11/C++20 probes freeze public
   scalar, handle, time, vector, value, delay, callback, assertion-attempt, and
   bridge layouts, numeric identities, and representative signatures.
3. `fsim_tf` exports the core, array-value, assertion, and data-reader C entry
   points. A bounded 64-frame thread-local context marshals them through the
   Change 6 checked dispatcher, bounds text to one MiB, supports nested re-
   entry, and returns safe failure values outside a live context.
4. The plugin loader enters that context for v2-host standard startup tables
   and direct startup. V1 hosts retain their frozen loader behavior without an
   invented service callback. The C startup fixture proves a normative call
   reaches the v2 lifecycle service; invalid and mismatched context frames are
   rejected without host entry.
5. Root and runtime headers are installed byte-for-byte, owned by the install
   manifest, and compiled/linked by the installed C consumer against
   `fsim::tf`. Source-line, source-package, foreign-ABI, binary-ownership,
   installed-public, resource-portability, runtime, and inventory evidence
   passes 8/8 in 21.43 wall seconds after exact Clang warnings-as-errors Debug
   target compilation.
6. S23-B187-C10 is preserved. The inventory has 9 active and 47 preserved rows
   at normalized SHA-256
   `faa1fe14de216236dfb3edf0cfb7ad10244636e47d2b5e562f18ca6c2ed4ba37`.
   The source package has 1,889 paths at SHA-256
   `f1da31ccafd7296f02d40d5ee7f11e77007198db74cecf4cae4b49790f11619f`.
7. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 11: update standard package behavior. Do
   not begin random-distribution work from Change 12 in the same bounded slice.

## Batch 187 Change 11 checkpoint

1. Batch 187 Changes 1-11 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. `systemverilog_standard_package` provides a zero-allocation declaration
   view and exact revision/digest for each SystemVerilog source profile.
   SystemVerilog-2005 through 2017 expose `mailbox`, `process`, `semaphore`,
   and `randomize`; SystemVerilog-2023 additionally exposes parameterized
   `weak_reference` and records the revised final `process` contract.
3. Qualified and implicit standard class spellings share one parser/type path.
   `std::process::self()` reaches the existing process runtime. Unknown or
   profile-ineligible `std` members diagnose `FSIM-ELAB-SVPKG-011`, and source
   cannot redeclare compiler-owned `package std` (`FSIM-SV-SEM-270`).
4. Every parsed SystemVerilog unit records its package revision and declaration
   identity. Portable-library schema 32 round-trips both fields and rejects
   schema 31; sorted unique identities participate in the whole-design and
   specialization cache keys.
5. The new non-template cache-key helper is a separate `.cpp` translation unit.
   `application_run.cpp` is 1,999 lines and remains below the project-wide
   2,000-line ceiling.
6. Exact Clang warnings-as-errors Debug frontend/library/application targets
   compile with eight workers. The focused application, library, schema,
   source-line, source-package, SystemVerilog-2023 inventory, and resource-
   portability lane passes.
7. S23-B187-C11 is preserved. The inventory has 8 active and 48 preserved rows
   at normalized SHA-256
   `a8c3f9025f94744741ba5eee824ad59453813e59c927c606657467fb9eb29ec9`.
   The source package has 1,895 paths at SHA-256
   `98461ffe85853c8a2b46e09f4c25a209d6d2fa4f15d13f0116873efbcf4de6b8`.
8. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 12: update the standardized random-
   distribution implementation. Do not begin annex/deprecation work from
   Change 13 in the same bounded slice.

## Batch 187 Change 12 checkpoint

1. Batch 187 Changes 1-12 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. All seven integer `$dist_*` routines use one bounded transactional service
   for interpreter and compiled execution. The standardized sampler consumes
   the low 23 bits of each updated nonzero seed through exact binary32
   construction, so replay is deterministic across LLVM O0/O2 and the
   interpreter.
3. Invalid distribution domains and X/Z arguments return zero without changing
   the seed and emit stable source-located warnings. Equal or reversed uniform
   bounds return the first bound without consuming the seed. Negative normal
   deviation and zero-mean Erlang are legal. Rejection sampling is limited to
   one million draws and rolls back the seed on exhaustion.
4. `RandomDistribution` carries source provenance through lowering, native-
   cache keys, and `.fsimdesign` serialization. The owning runtime-state schema
   is 62 and explicitly rejects stale schema 61 and future schema 63;
   portable-unit schema 32 is unchanged.
5. Exact Clang warnings-as-errors Debug application, runtime, and LLVM targets
   compile with eight workers. The focused random, artifact, runtime, LLVM,
   inventory, ABI-reference, nested/stale/owning schema, source-line, source-
   package, and resource-portability evidence passes.
6. S23-B187-C12 is preserved. The inventory has 7 active and 49 preserved rows
   at normalized SHA-256
   `62bf468043f8569d9dd52a0b7001bb2fb44eba841c4f726116f93f3052a808a2`.
   The source package remains 1,895 paths at SHA-256
   `98461ffe85853c8a2b46e09f4c25a209d6d2fa4f15d13f0116873efbcf4de6b8`.
7. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 13: apply the normative syntax, keyword,
   and deprecation annex requirements. Do not begin legacy TF/ACC interaction
   proof from Change 14 in the same bounded slice.

## Batch 187 Change 13 checkpoint

1. Batch 187 Changes 1-13 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. SystemVerilog-2017 and 2023 use distinct keyword-set identities with the
   same reserved-word contents. The `keyword-profile` selector now reaches
   the SystemVerilog-2005 keyword set from exact 2023 without restoring any
   source form removed from the 2023 grammar.
3. A separate non-template parser translation unit owns the annex policy.
   Exact 2023 rejects a clocked `$sampled` second argument, sequence `.ended`,
   general checker `always`, and operator-overload bind declarations with
   `FSIM-SV-DEPR-001`. Specialized checker always forms remain accepted.
4. Exact 2023 retains `defparam` and procedural `assign`/`deassign` with one
   source-located `FSIM-SV-DEPR-002` warning per construct. SystemVerilog-2017
   receives neither the removal errors nor candidate warnings.
5. Exact Clang warnings-as-errors Debug frontend/application targets compile
   with eight workers. The focused frontend, application, inventory,
   compatibility, standard-mode, source-line, source-package, and resource
   lane plus declared dependencies passes 33/33.
6. S23-B187-C13 is preserved. The inventory has 6 active and 50 preserved rows
   at normalized SHA-256
   `59d12129b1d171f6eab42541b1c265b6c26e6006847bd602c7ef20ec0aabc4f6`.
   The source-package manifest has 1,896 paths at SHA-256
   `077c3da9a3937c99745aad917b5c65f200375171a7f2aa516cbfef69ba28a72a`.
7. `verilog_parser_units.cpp` is 1,922 lines after moving the non-template
   defparam implementation into the annex translation unit. Every touched
   implementation file remains below the 2,000-line ceiling.
8. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 14: prove legacy TF/ACC interaction with
   the exact 2023 profile. Do not begin new-construct coverage proof from
   Change 15 in the same bounded slice.

## Batch 187 Change 14 checkpoint

1. Batch 187 Changes 1-14 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. Exact SystemVerilog-2023 source now has focused coexistence evidence for a
   context DPI import and registered TF task/function call sites. The direct-v3
   C TF plug-in resolves in every retained Verilog/SystemVerilog profile,
   including exact 2023, while all VHDL profiles reject that namespace.
3. The application TF scheduler binds and executes the loaded direct-v3 task
   specifically as SystemVerilog-2023. It retains the established serialized
   call, publication, and callback boundaries.
4. During one TF callback, the ACC context and standardized DPI context map
   module/root scopes onto the same simulation-owned VPI object identities.
   Caller location, time/scale, user data, TF arguments, values, work area,
   generation, and teardown remain coherent and bounded.
5. Exact Clang warnings-as-errors Debug frontend, application, and runtime
   targets compile with eight workers. The focused frontend, application,
   runtime, legacy-inventory, source, SystemVerilog-2023 inventory, and
   resource-contract lane passes 11/11 in 40.95 wall seconds.
6. S23-B187-C14 is preserved. The inventory has 5 active and 51 preserved rows
   at normalized SHA-256
   `f1f38eac68ee70717a89d41b20309a89be024819910ffcc5d30743af66656c3c`.
   The source-package manifest remains 1,896 paths at SHA-256
   `077c3da9a3937c99745aad917b5c65f200375171a7f2aa516cbfef69ba28a72a`.
7. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 15: prove code, assertion, and functional
   coverage for the supported new constructs. Do not begin scheduler-phase
   callback proof from Change 16 in the same bounded slice.

## Batch 187 Change 15 checkpoint

1. Batch 187 Changes 1-15 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. Exact-2023 statement-coverage evidence now covers the revised streaming and
   aggregate-pattern assignment targets, tolerance membership expression, and
   string `foreach`. Each source has two hierarchy instances and one
   executable point; declarations and the null loop body create no synthetic
   points. Source/instance identity, hit counts, and aggregation agree through
   interpreter, Debug, and LLVM O0-O3 execution.
3. The exact-2023 real-coverpoint application proves stable declaration,
   runtime-instance, and distinct exact/tolerance-bin identities. Both bins
   score one hit and 100-percent coverage through interpreter and LLVM O0/O2.
4. Concurrent assertion registration now publishes the elaborated instance
   path and semantic source-span identity in the coverage record and every
   assertion event. Exact-2023 checker assertions prove the identities and
   pass totals through interpreter and cold/warm LLVM O0/O2, closing the empty
   identity fields previously forwarded to callbacks and VPI.
5. Exact Clang warnings-as-errors Debug application targets compile with eight
   workers. The coverage database, source budget/package, inventory, resource,
   code/metric equivalence, functional coverage, assertions, common runtime,
   and VPI coverage lane passes 11/11 in 45.99 wall seconds.
6. S23-B187-C15 is preserved. The inventory has 4 active and 52 preserved rows
   at normalized SHA-256
   `e69c5efb680116f3025e2cac8a68e0cdb0e6c3a4e6af429f2d5f6a088db58db0`.
   The source-package manifest remains 1,896 paths at SHA-256
   `077c3da9a3937c99745aad917b5c65f200375171a7f2aa516cbfef69ba28a72a`.
7. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 16: prove DPI and VPI callbacks across all
   governed scheduler phases with deterministic re-entry containment. Do not
   begin artifact/cache/checkpoint/debug/trace proof from Change 17 in the same
   bounded slice.

## Batch 187 Change 16 checkpoint

1. Batch 187 Changes 1-16 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. An integrated runtime fixture executes DPI exports and immediate VPI
   lifecycle notifications in all eight scheduler regions: active, inactive,
   update, observed, reactive, re-inactive, re-update, and postponed. Region,
   time, user-data, simulation, registration, and one-shot identities remain
   exact.
3. VPI callback dispatch now owns a per-kind re-entry guard. Recursive
   delivery of the same active notification returns `ReentrantDispatch`
   instead of recursing or silently dropping a request. Different kinds and
   later notifications remain available.
4. Nested DPI callbacks retain the scheduler region, install and restore their
   own scopes, and preserve the outer foreign call context. Recursive DPI entry
   reaches the existing 64-frame context ceiling, returns its contained error,
   and unwinds with no borrowed context or scope left behind.
5. Exact Clang warnings-as-errors Debug runtime compilation passes with eight
   workers. The runtime, SystemVerilog-2023 inventory, and resource-portability
   lane passes 3/3 in 35.03 wall seconds.
6. S23-B187-C16 is preserved. The inventory has 3 active and 53 preserved rows
   at normalized SHA-256
   `4b27e72c1b2d7d75080ebc67712a9cf7e69e6b16c17e95445d2ff92ad4a81aec`.
   The source-package manifest remains 1,896 paths at SHA-256
   `077c3da9a3937c99745aad917b5c65f200375171a7f2aa516cbfef69ba28a72a`.
7. Before any future hosted CI run authorized by the cadence, inspect the most
   recent applicable run, conclusion, and failing logs, and resolve known
   actionable errors before triggering the new run.
8. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 17: prove artifact, cache, checkpoint,
   debug, and trace behavior. Do not begin the cross-platform foreign-
   application proof from Change 18 in the same bounded slice.

## Batch 187 Change 17 checkpoint

1. Batch 187 Changes 1-17 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. The exact-2023 conformance artifact runs after its source and object are
   hidden and after the design is relocated. Profile, class-specialization,
   result, and cache-key identities agree across interpreter, cold/warm LLVM
   O2, and Debug execution.
3. Each engine captures the same byte-exact versioned checkpoint and decodes it
   back to the original artifact. The cold compiled run owns misses/stores;
   warm and relocated compiled runs own the corresponding hits.
4. `DebuggerControl` observes the final SystemVerilog signal in Debug. An
   artifact-only compiled CLI run emits a filtered VCD carrying the hierarchy,
   signal, final value, and exact `systemverilog-2023` provenance.
5. The persistence proof was added to the 1,855-line SystemVerilog conformance
   unit. The existing 1,956-line artifact-phase translation unit was not
   enlarged.
6. Exact Clang warnings-as-errors Debug application compilation passes with
   eight workers. The conformance application, SystemVerilog-2023 inventory,
   and resource-portability lane passes 3/3 in 23.85 wall seconds.
7. S23-B187-C17 is preserved. The inventory has 2 active and 54 preserved rows
   at normalized SHA-256
   `457cfcb70a73fecacf3e538a44039d0ad72c6b1a4840848ce79a5b9a2490bb6b`.
   The source-package manifest remains 1,896 paths at SHA-256
   `077c3da9a3937c99745aad917b5c65f200375171a7f2aa516cbfef69ba28a72a`.
8. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 18: prove independently authored C and C++
   foreign applications on both platforms. Do not begin inventory closure from
   Change 19 in the same bounded slice.

## Batch 187 Change 18 checkpoint

1. Batch 187 Changes 1-18 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted through Change 20.
2. Independently authored C11 and C++20 VPI reference applications use only
   the public v3 ABI and exercise all ten typed host services across
   interpreter, LLVM O0, and LLVM O2 host identities, relocation, lifecycle,
   malformed-result, resource, old-host, and post-unload cases.
3. `fsim.runtime.vpi_reference_plugins` selects that bounded proof from the
   existing runtime executable. The C and C++ shared libraries, compile
   definitions, dependencies, resource lock, and CTest labels are identical in
   Linux and Windows configurations; no second harness or duplicate test
   compilation was added.
4. Exact Clang warnings-as-errors Debug runtime compilation passes with eight
   workers, and both reference libraries build locally. The dedicated runtime,
   source budget/package, inventory, and resource-portability lane passes 5/5
   in 34.64 wall seconds.
5. S23-B187-C18 is preserved. The inventory has 1 active and 55 preserved rows
   at normalized SHA-256
   `f38a9e912a836c4e6c057518b3c5dc5b41ea3f433e1a4332e2c6018ea605bf68`.
   The source-package manifest remains 1,896 paths at SHA-256
   `077c3da9a3937c99745aad917b5c65f200375171a7f2aa516cbfef69ba28a72a`.
6. Hosted Windows execution remains deferred to the authorized v3.0 release
   qualification. Before triggering that run, inspect the most recent
   applicable CI status and failing logs and resolve known actionable errors.
7. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Proceed only to Batch 187 Change 19: close the final active
   SystemVerilog-2023 inventory row. Do not begin Change 20 qualification in the
   same bounded slice.

## Batch 187 Change 19 checkpoint

1. Batch 187 Changes 1-19 are complete in the accumulated `codex/v3`
   worktree. Preserve them uncommitted until Change 20 performs the standard
   closure.
2. All 56 independently worded SystemVerilog-2023 delta rows are preserved and
   zero remain active. The normalized inventory SHA-256 is
   `69f7d088e4d0a1f1acd09e1b675423dcf6240294c889b45fb2a518e819053cc9`.
3. The governed SystemVerilog closure selection passes 29/29 in 90.28 wall
   seconds. Its 26 witnesses plus inventory, audit, and matrix completion cover
   semantics, frontend, elaboration, libraries/artifacts, interpreter/LLVM,
   VPI/runtime, Debug/VCD, assertions, randomization, coverage,
   hierarchy/interfaces, timing/SDF, UVM, typed boundaries, and mixed
   VHDL/SystemC composition.
4. C11/C++20 VPI applications remain locally green; their identical hosted
   Windows selector is deferred to the authorized v3.0 release CI.
5. No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
   Change 20 must first compile the clean Clang Release configuration and fix
   any compiler errors. Only then run the final Debug suite, update closure
   documentation, commit once, and push once. Batch 187 is not a sanitizer or
   hosted-CI boundary.
6. Before any later authorized hosted CI trigger, inspect the most recent
   applicable run, conclusion, and failing logs and resolve known actionable
   errors first.

## Batch 187 Change 20 checkpoint

1. Batch 187 is complete in the accumulated `codex/v3` worktree. Its single
   implementation commit and push follow the final documentation checks; do
   not split the batch into additional commits.
2. The clean Clang warnings-as-errors Release build completed all 3,149 build
   steps. After correcting exact-evidence audit drift and the obsolete local
   prohibition on the standardized `vpi_printf` export, the full Release suite
   passes 408/408 in 571.45 wall seconds.
3. The audit evidence is the measured 2,758 diagnostics, 1,525 authored
   sources, 1,828 authored SPDX entries, 777 test/control paths, and 567
   v1-conformance test/control paths. The governed IEEE license reference is
   accepted only for the exact standardized `include/vpi_user.h` and
   `include/sv_vpi_user.h` public headers; Apache remains required everywhere
   else.
4. The subsequent clean Clang warnings-as-errors Debug build completed all
   3,149 build steps. The full Debug suite passes 408/408 in 527.08 wall
   seconds.
5. Full SystemVerilog-2023 support is declared complete: all 56 independently
   worded delta rows are preserved and zero remain active at normalized
   SHA-256
   `69f7d088e4d0a1f1acd09e1b675423dcf6240294c889b45fb2a518e819053cc9`.
   The source-package manifest contains 1,896 paths at SHA-256
   `077c3da9a3937c99745aad917b5c65f200375171a7f2aa516cbfef69ba28a72a`.
6. Batch 187 is not a sanitizer or hosted-CI boundary, so neither lane ran.
   Sanitizer and hosted qualification remain owned by Batch 188 Change 20
   under the active v3.0 release goal.
7. After the one commit and push, proceed only to Batch 188 Change 1: audit
   zero unresolved rows across coverage, PLI, VHDL, and SystemVerilog. Do not
   begin Release, sanitizer, or hosted-CI qualification before Change 20.
8. Whenever Batch 188 Change 20 authorizes hosted CI, first inspect the most
   recent applicable run, its conclusion, and every failing log, and resolve
   known actionable errors before triggering a new run.
9. The pre-push inspection of Batch 186 run `34439219992` identified four
   actionable classes: unconditional compiled-process assertions in an LLVM-
   disabled VHDL-2019 test, a concurrent Windows cache-shard creation race,
   Windows process-start pressure under four-way hosted execution, and stale
   Windows package audit inventories. The test now distinguishes LLVM-enabled
   and disabled builds; cache publication validates the directory
   postcondition after a racing create; all five hosted build/test commands use
   two workers; and package audits expect 395/399 non-recursive tests plus
   1,259 entries.
10. The focused cache, resource-portability, source-line, and source-package
    lane passes 4/4. At the owner's direction, do not repeat the already-green
    Batch 187 full Release/Debug qualification for these CI-preflight-only
    changes before commit and push.

## Batch 188 Change 1 checkpoint

1. Batch 187 is committed and pushed as `18e1e56d`. Batch 188 Change 1 is
   complete in the new intentionally dirty `codex/v3` worktree; preserve it
   uncommitted through Change 20.
2. `v3_release_integration_inventory.tsv` binds six exact domains to their
   existing validators: coverage foundation, broad coverage metrics, IEEE TF,
   IEEE ACC, VHDL-2019, and SystemVerilog-2023. They contain zero active and
   163 preserved rows in aggregate.
3. The release-integration checker rejects duplicate or missing domains,
   unsafe/missing owner paths, malformed counts/digests, unresolved state, and
   any drift in the underlying validators' exact zero-active evidence. The
   integration ledger SHA-256 is
   `6a210c1c5e46bcd775c74dda440f276a9137770a501df45d1ac1502f7b1e8fe8`.
4. The six underlying inventory tests, release-integration test, source-line
   budget, and source-package manifest pass 9/9 in 2.50 wall seconds. The
   source-package manifest contains 1,895 ordered paths at SHA-256
   `25a46f42df572bf5dce5acfb24d871f8c72f023add1710b2b6602974aad106df`.
5. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 2: freeze the v3 manifest, ABI,
   object, design, checkpoint, and cache schemas. Do not begin v2 rejection
   proof from Change 3 in the same bounded slice.
6. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 2 checkpoint

1. Batch 188 Changes 1-2 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `v3_schema_freeze_inventory.tsv` binds six exact schema domains and 267
   frozen owner rows to the existing project-manifest, native-foreign-ABI,
   portable-object, design/library, nested semantic/checkpoint, and incremental
   native-cache validators. Its SHA-256 is
   `424f2ec0ce15aa11231d8cbd4d929134213046eda1ec98cbb14c2040385a4d90`.
3. The aggregate checker pins manifest schema 3; native plugin, TF, SVDPI
   context, and ACC query ABI 3; object format 7; portable schema 14;
   owning-unit schema 32; design format 12; library format 5; runtime
   checkpoint 62; semantic/design IR schema 4; class schema 12;
   SystemVerilog constraint/coverage schema 7; UVM schema 3; VHDL HIR schema
   4; and LLVM native-cache namespace `v168`.
4. The aggregate freeze, six underlying freeze validators, source-line budget,
   and source-package manifest pass 9/9 in 7.64 wall seconds. The source
   package contains 1,897 ordered paths at SHA-256
   `99827563c8dc397c84bb9e47beccc740c28223ee1472b38c7c979078f23d717c`.
5. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 3: prove deterministic
   rejection of all versioned v2 inputs. Do not begin older-profile
   requalification from Change 4 in the same bounded slice.
6. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 3 checkpoint

1. Batch 188 Changes 1-3 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `v3_v2_input_rejection_inventory.tsv` pins 15 exact v2-to-v3 boundaries
   across six families: manifests; object, portable-unit, design, and library
   artifacts; eight nested checkpoint/state payloads; the LLVM native cache;
   and the common native-plugin ABI. Its SHA-256 is
   `59e448c6f0c448e329a59810eb4c4b0c4b621056b147dcb1408f6b04f83b0a0e`.
3. Exact probes reject v2 manifest schema 2, object format/schema 6/10,
   owning-unit schema 26, design format 11, library format/schema 5/10,
   runtime/semantic/design-IR/class/constraint/coverage/UVM/VHDL-HIR schemas
   48/3/3/10/6/4/2/1, LLVM cache namespace `v116`, and native ABI 2 without
   compatibility, migration, partial publication, decoded state, or fallback.
4. The audit corrected `portable_stale_schema_contract.tsv` from superseded
   portable schema 13 to frozen schema 14. Its SHA-256 is
   `8d50da489568ec8764dc2fff02f58224f82a7855bf655c85b8a2eaa55b2ab14b`.
5. Exact Clang warnings-as-errors Debug target builds completed. The seven
   runtime rejection probes, aggregate checker, stale-schema policy,
   source-line budget, and source-package manifest pass 12/12 in 6.60 wall
   seconds. The package contains 1,899 ordered paths at SHA-256
   `54a4078400fe925eacf3b32f69ab76509a0b288edee644da5ef28467b231452e`;
   1,525 authored sources remain at or below 2,000 lines with zero `.tpp`
   files.
6. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 4: requalify every retained
   older HDL standard profile. Do not begin coverage-corpus qualification from
   Change 5 in the same bounded slice.
7. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 4 checkpoint

1. Batch 188 Changes 1-4 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `v3_retained_profile_qualification.tsv` pins 13 retained profiles:
   VHDL-1987/1993/2000/2002/2008, Verilog-1995/2001/2001-noconfig/2005,
   and SystemVerilog-2005/2009/2012/2017. Selection, behavior,
   mixed-language, interpreter/LLVM, artifact, cache, relocation, checkpoint,
   and diagnostic evidence remains owned. The ledger SHA-256 is
   `7ec4c1c842a711bae73a8d979f817c2943a90daf0251a90dd7d80ed10911943b`.
3. The first closure attempt found stale `--parallel 4` assertions in the MSVC
   Debug, MSVC Release, and Windows LLVM contract checkers. They now agree with
   the governing two-worker hosted-CI limit and pass 3/3.
4. The corrected VHDL and Verilog/SystemVerilog closure lane passes 23/23 in
   60.25 wall seconds. The profile-selection test, release ledger, source-line
   budget, and source-package manifest pass 4/4 in 1.38 wall seconds.
5. The source package contains 1,901 ordered paths at SHA-256
   `a9b42f883e7c04fa8dbacdd92d69d133ff6334053d57623c303e4bad1478beb9`;
   `application_test_classes.cpp` remains within policy at 1,999 lines.
6. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 5: run the complete HDL code,
   functional, and PSL coverage corpus. Do not begin the TF/ACC PLI corpus from
   Change 6 in the same bounded slice.
7. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 5 checkpoint

1. Batch 188 Changes 1-5 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. The complete `coverage`-label corpus passes 58/58 in 56.01 wall seconds. It
   covers code coverage across Verilog/SystemVerilog/VHDL and interpreter/LLVM
   O0-O3, SystemVerilog functional coverage, PSL coverage, aggregation,
   exclusions, unified database merge/report/CLI/VPI surfaces, and malformed
   input/resource containment.
3. The first run found a stale four-worker assertion in the v1 release wrapper
   and exact legacy inventory counts that rejected additive v3 files. Hosted
   assertions now require two workers. Historical FST/SDF/v1 inventory counts
   are enforced as deletion-detecting floors while every current authored file
   is still inspected for an approved SPDX notice.
4. The v1 release candidate plus affected FST, SDF application/VITAL,
   resource-portability, Windows-package, and v2 release-record gates are
   green after repair.
5. The source package remains 1,901 ordered paths at SHA-256
   `a9b42f883e7c04fa8dbacdd92d69d133ff6334053d57623c303e4bad1478beb9`.
6. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 6: run the complete TF/ACC PLI
   corpus. Do not begin VHDL-2019 corpus qualification from Change 7 in the
   same bounded slice.
7. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 6 checkpoint

1. Batch 188 Changes 1-6 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. The exact `tf`/`acc` label union passes 39/39 in 0.22 wall seconds. It covers
   C/C++ ABI layouts, common native metadata, every TF lifecycle/value/time/
   context/control seam, every ACC handle/lookup/traversal/value/timing/
   callback seam, scheduler coordination, containment, TF/ACC and ACC/VPI
   coherence, vendor rejection, and Linux/Windows plugin probes.
3. The frozen TF and ACC inventories plus the shared v3 release-integration
   ledger are part of the green lane.
4. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 7: run the complete VHDL-2019
   corpus. Do not begin SystemVerilog-2023 corpus qualification from Change 8
   in the same bounded slice.
5. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 7 checkpoint

1. Batch 188 Changes 1-7 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. All 14 VHDL-2019 inventory-owned witnesses pass in 65.38 wall seconds. The
   lane includes the zero-active 37-row inventory, frontend, elaboration,
   runtime/VHPI, 64-bit INTEGER, views/types/composites, protected/access/
   physical behavior, projected waveforms, standard APIs, PSL, artifacts and
   checkpoints, plus mixed-language interpreter/LLVM/cache/debug/VCD
   equivalence.
3. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 8: run the complete
   SystemVerilog-2023 corpus. Do not begin mixed-language/foreign-interface
   composition from Change 9 in the same bounded slice.
4. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 8 checkpoint

1. Batch 188 Changes 1-8 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. The 56-row SystemVerilog-2023 inventory-owned corpus and its closure
   dependencies pass 36/36 in 88.41 wall seconds. It covers frontend/HIR,
   aggregates, classes/containers/processes/randomization/synchronization,
   assertions and coverage, interfaces/hierarchy/type parameters/generate,
   files, gates/switches/UDP, specify/SDF timing, DPI/VPI, artifacts,
   checkpoints, UVM, and mixed-language interpreter/LLVM/cache/debug/VCD
   behavior.
3. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 9: qualify mixed-language and
   foreign-interface composition. Do not begin interpreter/LLVM/Debug
   equivalence from Change 10 in the same bounded slice.
4. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 9 checkpoint

1. Batch 188 Changes 1-9 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. The mixed-language and foreign-interface composition lane passes 15/15 in
   136.27 wall seconds. It joins multi-root VHDL/Verilog/SystemVerilog/SystemC
   boundaries, typed conversions, SDF mixed resolution and foreign
   observation, the complete DPI/VPI/VHPI runtime and reference-plugin
   surface, TF/ACC plug-in and scheduler coordination, the frozen v3 foreign
   ABI, and interpreter/LLVM, cache, artifact, debugger, VCD, callback, and
   lifecycle evidence.
3. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 10: qualify interpreter, LLVM
   O0-O3, and Debug equivalence. Do not begin artifact/cache/checkpoint/trace/
   debugger qualification from Change 11 in the same bounded slice.
4. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 10 checkpoint

1. Batch 188 Changes 1-10 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. The focused engine-equivalence lane passes 10/10 in 48.64 wall seconds. It
   explicitly exercises interpreter, LLVM O0/O1/O2/O3, and Debug across
   Verilog, SystemVerilog, VHDL, mixed SystemC boundaries, code-coverage
   metrics, classes/UVM, PSL, VPI, display ordering, caches, artifacts,
   callbacks, debugger observations, and VCD state.
3. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 11: qualify artifacts, caches,
   checkpoints, traces, and debugger observations. Do not begin fuzz,
   malformed-input, resource, or security readiness from Change 12 in the same
   bounded slice.
4. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 11 checkpoint

1. Batch 188 Changes 1-11 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. The focused persistence and observability union passes 36/36 in 2.85 wall
   seconds. It covers object/design/library/coverage/SCV artifacts, native and
   project caches, corruption isolation, source-hidden relocation,
   non-project restart, runtime checkpoints and replay, VCD/FST/SystemC/SCV
   traces, SDF observations, debugger/callback correlation, and deterministic
   archive rendering.
3. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 12: complete fuzz,
   malformed-input, resource, and security readiness. Do not begin warning
   audit ownership from Change 13 in the same bounded slice.
4. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 12 checkpoint

1. Batch 188 Changes 1-12 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. A fresh local Clang 22.1.8 warnings-as-errors libFuzzer build completes.
   The deterministic four-seed frontend corpus passes 20,000 mutations in
   seven seconds with 45 MiB final RSS and no crash, timeout, or leak finding.
3. The complete 72-test `resource` lane passes 72/72 in 0.67 wall seconds. An
   additional 11-test malformed/corrupt/containment/security-policy union
   passes 11/11 in 22.41 wall seconds, including frontend recovery, artifact
   codecs, diagnostics, cache isolation, producer validation, TF/ACC failure
   containment, and the cross-platform resource contract.
4. LibFuzzer's 912 generated corpus discoveries were removed after the run;
   the four tracked governed seeds remain unchanged and the corpus has no
   untracked files.
5. No Release, clean-first, sanitizer, hosted-CI inspection, commit, or push
   action ran. Proceed only to Batch 188 Change 13: freeze Linux and Windows
   warning-audit ownership. Do not begin hosted platform/toolchain definition
   work from Change 14 in the same bounded slice.
6. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 13 checkpoint

1. Batch 188 Changes 1-13 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `v3_warning_audit_inventory.tsv` freezes nine warnings-as-errors lanes:
   five Linux Clang lanes and four Windows LLVM-MinGW Clang lanes spanning
   Debug, Release, LLVM-off/on, and frontend fuzz configurations. The ledger
   SHA-256 is
   `92f5da8d3cd328c8924a4383861b52c0c2ecb2608046980e202a7585946d13ff`.
3. `CheckV3WarningAudit.cmake` pins compiler selection,
   `FSIM_WARNINGS_AS_ERRORS=ON`, common `-Werror` ownership, and two-worker
   hosted build/test commands. Its first run exposed only an interpolated
   checker literal; the corrected checker passes, while the retained MSVC,
   Windows LLVM, tool-portability, resource-portability, source-line, and
   source-package contracts pass 7/7 alongside it.
4. The Debug build regenerated and completed 103 warnings-as-errors link
   steps with eight local workers. No Release qualification, clean-first,
   sanitizer, hosted-CI inspection, commit, or push action ran. Proceed only
   to Batch 188 Change 14: freeze hosted platform and toolchain definitions.
   Do not begin release documentation from Change 15 in the same bounded
   slice.
5. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 14 checkpoint

1. Batch 188 Changes 1-14 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `v3_hosted_toolchain_inventory.tsv` freezes nine hosted configurations:
   Linux Clang Debug/Release, LLVM Debug/Release, and fuzz plus Windows
   LLVM-MinGW Debug/Release with LLVM disabled and enabled. The ledger SHA-256
   is `0d2009b5f38a5833c94cacf91898c4f81d1c97ea4d057712b107ff1591b0492a`.
3. `CheckV3HostedToolchains.cmake` pins runner images, Clang/LLVM 22.1.8,
   LLVM-MinGW 20260616, warnings-as-errors, 120-minute timeouts, two-worker
   commands, and artifact/log ownership. The hosted freeze and retained
   warning, Windows, tool/resource, source-line, and source-package contracts
   pass 9/9 after correcting alphabetical manifest order.
4. No Release qualification, clean-first, sanitizer, hosted-CI inspection,
   commit, or push action ran. Proceed only to Batch 188 Change 15: publish
   v3.0 examples, user guides, API references, and known limitations. Do not
   begin deterministic artifact preparation from Change 16 in the same
   bounded slice.
5. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 15 checkpoint

1. Batch 188 Changes 1-15 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. New independently authored documentation covers unified `.fsimcov`
   operation, IEEE TF/ACC PLI, SystemVerilog-2023, the direct-v3 C/API and
   schema boundary, and known v3.0 limitations. README links the release set.
3. `examples/v3_coverage` is a runnable SystemVerilog-2023 opt-in code and
   functional-coverage example. A copied clean example elaborates and runs to
   `$finish` at tick 15, delta 1.
4. The six-row `v3_release_documentation.tsv` ledger has SHA-256
   `60d80fe5bb26ccd12434aa2796339efae427b31675f9b9afe0f4a94e537565fe`.
   Its checker, source-package manifest, and source-line budget pass 3/3.
5. No Release qualification, clean-first, sanitizer, hosted-CI inspection,
   commit, or push action ran. Proceed only to Batch 188 Change 16: prepare
   deterministic source and existing archive artifacts. Do not begin license,
   SBOM, provenance, or private-reference exclusion auditing from Change 17 in
   the same bounded slice.
6. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 16 checkpoint

1. Batch 188 Changes 1-16 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `packaging/v3-archive-layout.tsv` freezes one portable source archive and
   four Clang binary ZIPs: Linux Clang 22 and Windows LLVM-MinGW 20260616, each
   with LLVM disabled and LLVM 22.1.8 enabled. Its SHA-256 is
   `991e93c3c46359a3b4ed2388489fef4e1be5508b338778002c9212745f351426`.
3. `create_deterministic_zip.py` now writes a fixed staging path and atomically
   replaces the destination only after the archive closes successfully. The
   static artifact freeze and the complete repeated source/binary fixture
   packaging test pass 2/2 in 17.78 wall seconds.
4. No Release qualification, clean-first, sanitizer, hosted-CI inspection,
   commit, or push action ran. Proceed only to Batch 188 Change 17: complete
   license, SBOM, provenance, and private-reference exclusion audits. Do not
   begin release-record or tag-message work from Change 18 in the same bounded
   slice.
5. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 17 checkpoint

1. Batch 188 Changes 1-17 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `packaging/v3-supply-chain-inventory.tsv` freezes 13 project and governed
   third-party license, notice, SPDX SBOM, patch/source provenance, exclusion,
   and independently authored language-inventory owners. Its SHA-256 is
   `991174ef94f597bf5c4b956619e6c102bebe3adfc477e04becd99c1ff673b626`.
3. `CheckV3SupplyChain.cmake` validates the inventory and SBOM contracts and
   scans every packaged textual input for prohibited private reference
   locations/document paths without recording those locations or private
   reference hashes. After repairing one checker string delimiter, the new
   audit plus source-package, deterministic packaging, IEEE package,
   VHDL-2019, SystemVerilog-2023, SystemC, and SCV provenance tests pass 8/8.
4. No Release qualification, clean-first, sanitizer, hosted-CI inspection,
   commit, or push action ran. Proceed only to Batch 188 Change 18: freeze the
   v3.0 release record and exact tag message. Do not change active product or
   package version identities from Change 19 in the same bounded slice.
5. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 18 checkpoint

1. Batch 188 Changes 1-18 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted through Change 20.
2. `packaging/v3-release-record.txt` freezes candidate/tag `v3.0.0`, annotated
   tag message `fsim v3.0.0`, release-document digests, all five archive names,
   governance digests, unsigned disposition, and the complete Change 20
   qualification/publish boundary. Its SHA-256 is
   `a3e7fc3147b1019ae374f344f402268ca6f625943092d280f7774925690095c2`.
3. `docs/changelog-v3.md` independently summarizes unified coverage, IEEE
   TF/ACC, VHDL-2019, SystemVerilog-2023, retained profiles, and direct v2
   rejection. The release record and related documentation/artifact/supply/
   source contracts pass 6/6.
4. Active compiled and package identities remain unchanged as required. No
   Release qualification, clean-first, sanitizer, hosted-CI inspection,
   commit, or push action ran. Proceed only to Batch 188 Change 19: set every
   product/version/package identity to `3.0.0`. Do not begin full Release or
   Debug qualification from Change 20 in the same bounded slice.
5. At the later Change 20 CI boundary, inspect the most recent applicable run,
   conclusion, and failing logs before triggering any new run.

## Batch 188 Change 19 checkpoint

1. Batch 188 Changes 1-19 are complete in the intentionally dirty `codex/v3`
   worktree; preserve them uncommitted until Change 20 qualification succeeds.
2. CMake, compiled version identity, pkg-config, CLI/Tcl, active package
   policy, deterministic archive defaults, Linux/Windows target definitions,
   and hosted Windows artifact names now use `3.0.0`/`v3.0.0`. `fsim
   --version` reports `fsim 3.0.0 (C API 1)`.
3. Active Linux package targets are Clang 22 with LLVM disabled/enabled;
   historical v2/GCC records remain explicitly archival. Every active hosted
   package target uses two workers and Batch 188 ownership. The updated hosted
   ledger SHA-256 is
   `0d2009b5f38a5833c94cacf91898c4f81d1c97ea4d057712b107ff1591b0492a`.
4. The 11-row `v3_version_identity.tsv` ledger has SHA-256
   `394256500832e38e3e15bbc4715e4494312192de1a39a1eea9bf4e77b3b3be2c`.
   The version cut rebuilt 450 exact-LLVM warnings-as-errors Debug actions and
   the focused product/package/install/archive/Windows/Tcl/runtime/governance
   lane passes 17/17 in 20.78 wall seconds.
5. No full Release/Debug qualification, sanitizer, hosted-CI inspection,
   commit, or push action ran. Proceed to Batch 188 Change 20. Compile Release
   first, run the complete Release suite, then perform a clean Debug build and
   complete Debug suite. Inspect the most recent applicable hosted CI run and
   failing logs before the one commit/push that triggers new CI. After push,
   run sanitizers and monitor/fix hosted CI until every required lane is green;
   only then create/push the exact annotated `v3.0.0` tag and publish.

## Batch 188 Change 20 local-qualification checkpoint

1. Batch 188 Changes 1-20 are implemented in the intentionally dirty
   `codex/v3` worktree. Local Release and Debug qualification are complete;
   the single closing commit/push, sanitizers, replacement hosted CI, release
   artifacts, tag, and publication remain.
2. Before any release build or new hosted run, the latest applicable CI run
   `34484367110` for Batch 187 commit `18e1e56d` and its failing logs were
   inspected. Linux compilation completed and tests exposed stale four-worker
   policy and exact-count assertions. All four Windows LLVM-MinGW jobs failed
   to link `vpi_register_assertion_cb` and `vpi_load_init` because the
   SystemVerilog VPI extension declarations lacked the DLL link-surface
   contract.
3. The CI repair applies import/export decoration to all twelve
   SystemVerilog VPI extension routines, updates current two-worker/test-count
   policy, registers `v3_coverage` in clean-machine closure, refreshes release
   governance digests, and adds SPDX ownership to the new ledgers. The foreign
   ABI, Windows-package, clean-machine, release-record, and governance gates
   are green locally.
4. Qualification followed the required order. The clean-first Clang 22.1.8
   plus exact LLVM 22.1.8 Release build completed 3,149/3,149 actions with
   eight workers and warnings-as-errors. Its complete run passed 417/419 in
   516.50 seconds; after correcting the two integration-only failures, the
   dependency-expanded affected closure passed 22/22 in 150.85 seconds and
   accounts for every Release owner.
5. The subsequent clean-first Clang/LLVM Debug build completed 3,149/3,149
   actions with eight workers and warnings-as-errors. The complete Debug suite
   passes 419/419 in 532.00 seconds.
6. Immediately before this checkpoint, run `34484367110` remains the newest
   applicable hosted run and remains failed; no replacement CI run has begun.
   Audit the complete diff and source manifest, commit/push the Change 20
   candidate, then run sanitizers and monitor/fix every Linux and Windows lane.
   Do not tag or publish until all required local and hosted lanes are green.

## Batch 188 Change 20 hosted-repair checkpoint

1. Candidate commit `cad15781` is pushed on `codex/v3`. Its replacement hosted
   run `34504855725` completed failed: every Linux lane passed and all four
   Windows LLVM-MinGW configurations compiled and linked, confirming the VPI
   DLL-linkage fix. Each Windows test job then reported the same nine failures:
   eight byte-governance checks saw CRLF checkout materialization and
   `fsim.application.random` compared the canonical `/` source path with a
   native `\` path spelling.
2. `.gitattributes` now forces LF for repository text on every host while
   preserving the existing `-text` treatment for governed binary inputs. The
   random application assertion compares against `generic_string()`. These
   changes directly cover all nine common Windows failures; hosted confirmation
   remains pending until this repair commit is pushed.
3. The Batch 188 sanitizer boundary is complete. It repaired self-owned VHDL
   access/file subtype assignment, VHDL scalar-attribute object shorthand,
   SystemC shutdown-helper lifetime, and the upstream SCV stream-core leak.
   The SCV fix is recorded as a sixth governed source patch with deterministic
   LF materialization and updated manifest, tree, ABI, package, and supply-chain
   identities. The final ASan/UBSan suite with leak detection passes 415/415 in
   929.15 seconds.
4. Final local qualification after all repairs followed the required order.
   A fresh Clang 22.1.8 plus LLVM 22.1.8 Release build completed 3,149/3,149
   actions with eight workers and warnings-as-errors, and its complete suite
   passes 419/419 in 532.87 seconds. A fresh equivalent Debug build then
   completed 3,149/3,149 actions and its complete suite passes 419/419 in
   539.28 seconds. The only build interruption was sandbox DNS blocking the
   pinned Tcl 9.0.4 download; resuming the same build with network access
   completed without a source failure.
5. Before starting another hosted run, verify that `34504855725` is still the
   latest applicable completed run and inspect its conclusion. Audit and commit
   this repair diff, push once, then monitor and fix every required hosted lane
   until green. Do not create or push `v3.0.0`, publish release artifacts, or
   begin Batch 189 before hosted closure is green.

## Batch 188A Change 1 checkpoint

1. Replacement Batch 188A is now authoritative and consists of exactly twenty
   changes between Batch 188 and Batch 189. It creates no release tag and does
   not renumber Batches 189-197. This supersedes the earlier Batch 188 tag and
   publication direction. The accepted handoff is `parse/check -> build and
   fold HIR -> destroy AST -> link HIR -> specialize HIR -> lower to SimIR ->
   build DesignIR`; DesignIR remains post-specialization hierarchy state.
2. The live starting point is clean, synchronized `codex/v3` commit
   `a867847cec10ac831e10674f7dc9de17ba294916`. The source tree and local/remote
   refs matched when Change 1 began. This commit is also the performance/RSS
   baseline for the replacement batch.
3. Compilation must return one `semantic::CompiledDesign` and no reachable
   parser or AST storage. Existing VHDL and SystemVerilog HIRs are the only
   owning compilation IRs. Shared elaboration may use non-owning dispatch views;
   specialization may own a unit-scoped HIR copy or overlay cached by the
   existing in-memory key. No persistent specialization cache, template
   hierarchy, recipe IR, symbolic SimIR, or new CLI switch is allowed.
4. Work is split into four cohesive waves: Changes 2-5 establish ownership;
   Changes 6-10 fold and persist compiled HIR; Changes 11-15 link and
   specialize; Changes 16-20 cut hierarchy/lowering over and close
   compatibility. Run focused tests only at wave boundaries. Change 20 owns
   clean eight-worker Release then Debug suites, representative measurements,
   and one four-lane, LLVM-enabled-only hosted matrix: Linux Debug/Release and
   Windows Debug/Release. Do not repeat sanitizers before Batch 190, and
   commit/push only once after local qualification.
5. Audit net progress at meaningful seam and validation boundaries. Hourly
   checkpoint recording is no longer required. If work churns, execute the
   shortest remaining dependency here. Preserve repository WebKit style and
   use cohesive helpers, translation-unit partitioning, deduplication, or
   obsolete-code deletion rather than compressed lines. The 2,000-line ceiling
   is waived for this batch and its gate is not a Batch 188A closure condition.
6. Proceed to Change 2: introduce `semantic::CompiledDesign` and its minimal
   non-owning language-dispatch accessors. Do not begin HIR completion from
   Change 3 in the same bounded slice.

## Batch 188A 2026-09-16 11:53 MDT progress audit

1. Net progress is positive rather than churn. `CheckedProject` now carries one
   `semantic::CompiledDesign` owner; the prior independent semantic, SV-HIR,
   and VHDL-HIR members are gone. Both HIRs retain instance/association,
   generate, defparam, bind, timing, UDP, coverage, source, and compilation
   provenance needed by the next cutover slices.
2. Compilation and object-check paths construct both HIR projections instead
   of skipping object HIR. A deterministic compiled-HIR bundle codec reuses the
   semantic and language codecs at bundle schema 1, SV-HIR schema 8, and
   VHDL-HIR schema 5. Its corruption and deterministic round-trip coverage is
   green in `fsim.application.systemverilog_hir`.
3. The normalization pass annotates residual expression, generate, and
   defparam dependencies and marks dependency-independent HIR expressions.
   The single linker relocates every semantic/HIR dense ID, rejects logical
   unit collisions, rebuilds dependency/reference metadata, and normalizes the
   merged result. Focused SV and VHDL projected tests pass.
4. The AST lifetime boundary is not yet closed: `CheckedProject::parsed`, the
   public `ParsedDesign` elaboration overloads, and `SpecializedUnit::unit`
   remain. Therefore Changes 2-3, 6-8, and 11 have implemented foundations but
   are not accepted as complete, and no later change is claimed complete.
5. The shortest remaining dependency is the HIR-native elaboration ownership
   seam: change public/internal hierarchy lookup and specialization state to
   consume `const semantic::CompiledDesign&` and unit-scoped HIR records. Only
   after that seam is executable may object/cache publication switch to the
   bundle and compile-local AST storage be destroyed.

## Batch 188A 2026-09-17 08:46 MDT progress audit

1. Net progress remains positive. Public `check_project()` and `load_objects()`
   now return an AST-free `CheckedProject`; parser ownership lives only in an
   internal `CompilationWorkspace`, and moving out the compiled-project base
   destroys that workspace before either public API returns. CLI and Tcl unit
   counts, plus the focused HIR lifetime tests, now inspect semantic/HIR state.
2. `.fsimobj` format 8 and portable schema 15 now publish one deterministic
   compiled-HIR bundle, restore and relocate its logical source identities,
   and route decoded bundles through the common linker. The legacy AST unit
   payloads remain temporarily beside the bundle solely for the not-yet-ported
   internal elaborator; they are no longer the public compilation result.
3. Warnings-as-errors `fsim_application` builds pass with eight workers.
   `fsim.application.systemverilog_hir`,
   `fsim.application.vhdl_projected`, and
   `fsim.application.artifact_phases` pass after the ownership and object
   persistence changes. `git diff --check` is clean.
4. No completion claim is made for Changes 5, 9, 10, or 12: internal build,
   library export/import, and elaboration still retain or reconstruct AST
   storage. The object format currently carries both the authoritative bundle
   and transitional syntax payloads, so the prohibition on owning AST payloads
   is not yet satisfied.
5. The shortest remaining dependency is mapped-library parity: publish and
   restore the identical compiled-HIR bundle under library format 6, then make
   the linked bundle authoritative while keeping the transitional AST sidecar
   isolated. After that wave boundary, cut the elaborator API and
   `SpecializedUnit` ownership over to HIR.

## Batch 188A 2026-09-17 09:48 MDT progress audit

1. Net progress remains positive. Library format 6 now publishes and decodes
   compiled-HIR bundle schema 1, and the checker excludes the compatibility
   syntax projection for every mapped logical library before linking those
   decoded bundles into the public `CompiledDesign`.
2. Public application tests no longer inspect `CheckedProject` syntax. The HIR
   now retains SystemVerilog scheduling and standard-package provenance plus
   complete VHDL predefined-environment, standard-package, value-domain,
   executable-width, and static range metadata needed by those consumers.
3. The build phase no longer discards the checked semantic/HIR product and
   rebuilds it after AST elaboration. The compiled design remains the stable
   owner through specialization and lowering; the legacy syntax adapter is
   still isolated inside the internal build workspace and is not yet removed.
4. The first broad Debug application run exposed representation-boundary
   defects. Fixes in flight canonicalize HIR standard spellings, retain static
   VHDL range bounds, reject post-link HIR validation errors, complete
   structural SystemVerilog expression records, and index the `.fsimobj`
   compiled bundle separately from design units.
5. The shortest remaining dependency is still the HIR-native hierarchy seam:
   finish this wave's focused regression repair, then change unit lookup and
   specialization ownership from `ParsedDesign`/`DesignUnit` to compiled HIR.
   Transitional AST sidecars and the internal AST elaborator remain explicit
   blockers; no cutover or batch-completion claim is made.

## Batch 188A 2026-09-17 10:48 MDT progress audit

1. Net progress remains positive. Root, library, configuration, package,
   entity, port-surface, and mixed-language target selection now begin from
   `semantic::CompiledDesign`; syntax records are obtained only after HIR
   identity selection as a temporary lowering adapter.
2. The combined warnings-as-errors application target builds with eight
   workers. The 86-test application boundary reached 85 passes and one
   localized UDP transition-delay regression; validating the synthetic UDP
   lowering profile against the compiled UDP record fixed it, and the focused
   transition-delay test is green again.
3. The next hierarchy wave is deliberately broad: VHDL component binding,
   SystemVerilog interface lookup, and builder package/bind/extern registry
   initialization are proceeding in parallel before another combined build.
   This keeps compile/test iterations at cohesive subsystem boundaries.
4. Source changes remain WebKit-style and `git diff --check` was clean at the
   preceding wave boundary. No line-count target is being met through compressed
   expressions or declarations.
5. The shortest remaining dependency is specialization ownership. After the
   current lookup wave, replace `SpecializedUnit`'s `DesignUnit` copy with a
   unit-scoped HIR working representation and move residual parameter,
   generate, callable, and process lowering onto HIR. Transitional AST object
   and library sidecars remain explicit blockers, so no completion claim is
   made.

## Batch 188A 2026-09-17 11:48 MDT progress audit

1. Net progress remains positive. The linker now validates and relocates the
   complete semantic and language-HIR identity surface deterministically, and
   shared non-owning declaration, type, expression, statement, process, and
   instance dispatch begins from `semantic::CompiledDesign`.
2. Project resolution and declared-precision validation are authoritative from
   compiled HIR for direct source, decoded objects, and mapped libraries. The
   visitor covers retained SystemVerilog timing, UDP, clocking, declaration,
   statement, and instance delays plus VHDL waveform, rejection, and nested
   disconnection delays while preserving configured minimum, typical, and
   maximum selection. AST delay mutation remains isolated as a temporary
   lowering adapter.
3. Parser-independent specialization overlays now validate declaration
   ownership, retain residual expression and generate dependencies, include
   VHDL primary-entity generics, prefer canonical identities, fall back per
   declaration when necessary, and ignore local parameter, specparam, and
   local-package bookkeeping that is not part of the specialization surface.
4. SystemVerilog HIR now retains typed DPI, clocking, assertion, checker, and
   complete covergroup declaration and initial-instance state. VHDL HIR now
   retains unit and nested-generate disconnection specifications. The SV and
   VHDL builders were split into cohesive translation units below the source
   line ceiling; the compiled-HIR codec is being partitioned next. All current
   static and whitespace checks are clean.
5. Broad builds from the individual lanes were deliberately deferred so one
   eight-worker warnings-as-errors build can validate this complete wave. The
   shortest remaining dependency after that boundary is still AST-free
   specialization and lowering: evaluate residual HIR once per specialization
   and remove `SpecializedUnit::unit`, then remove object/library AST sidecars
   and every `ParsedDesign` elaborator API. No later change or batch completion
   is claimed.

## Batch 188A 2026-09-17 12:48 MDT progress audit

1. Net progress remains positive. The combined eight-worker Debug
   warnings-as-errors boundary rebuilt and linked the semantic, elaboration,
   application, and all three test executables after the HIR, codec, and
   specialization partitions. Semantic and elaboration suites pass.
2. Focused application testing exposed four representation-boundary
   regressions. Full nested minimum/typical/maximum delay information is now
   retained for every additional HIR delay component, restoring established
   1 ns, 1 ps, and 1 fs automatic-resolution behavior. Distinct file-unit
   classes now publish collision-free canonical VPI identities, and class
   constructor execution accepts bounded sized packed literals.
3. The new class case and the original transition-delay resolution assertion
   now pass. Artifact, mapped-library resolution, UDP artifact, and later class
   artifact paths converge on one remaining diagnostic: SystemVerilog HIR
   instance ID 0 does not match its semantic identity after projection. A
   single linker/identity repair owns that common failure.
4. Source-budget work remains WebKit-style: SV and VHDL HIR builders, the
   design-artifact codec, hierarchy/class resolution, and specialization are
   split into cohesive translation units with clean whitespace and line-length
   checks. No compressed-source workaround was introduced.
5. After the instance-identity repair and one authoritative grouped rerun, the
   shortest remaining architectural dependency is a parser-independent
   `SpecializedHirUnit`: overlay-first specialized records, selected generates,
   deterministic synthetic hierarchy records, and HIR-native final lowering.
   Sidecar publication cannot be deleted first because current specialization,
   hierarchy materialization, coverage, UDP, and final lowering still consume
   the AST adapter. No batch-completion claim is made.

## Batch 188A 2026-09-17 13:43 MDT progress audit

1. Net progress remains positive. The common linker now compares complete
   relocated origin chains instead of object-local numeric origin IDs, and the
   explicit semantic/SystemVerilog compilation-unit plus nested-class scopes
   eliminate the class-only coverage crash. The combined Debug build was green
   before these latest partitions; artifact-phase coverage passes, and the DPI
   follow-up corrected an import-profile test expectation rather than changing
   HIR semantics.
2. `SpecializedHirUnit` now owns deterministic, unit-scoped replacement records
   for declarations, types, expressions, statements, processes, and instances,
   selected-generate IDs, and canonical actual metadata with immutable
   `CompiledDesign` fallback. The existing specialization cache retains this
   working HIR owner; its `DesignUnit` remains explicitly isolated as the
   temporary unported lowering adapter.
3. UDP declaration validation and resource limits are enforced at compiled-HIR
   validation, codec, and linker boundaries. Compiled candidates now drive UDP
   table/profile construction and hierarchy dispatch. Object and mapped-library
   publication/loading no longer emit or consume `.fsimudp`; primitive metadata
   inventory is validated against the authoritative bundle while established
   collision diagnostics and inspect output remain stable.
4. VHDL configuration root, block-chain, rule, binding, instance-ID, identity,
   and validation selection is HIR-authoritative, including a decoded-bundle
   parity test that reorders the compatibility AST. Its large implementation is
   being split at the selection/association boundary before the root-owned
   build. Ordinary-cache ownership is under a separate graph-first audit.
5. The shortest remaining dependency is executable class HIR: parameters,
   relations, member and callable ID links, per-member origins, and generated
   classes are being added to the existing HIR stores. After that representation
   is validated, move class specialization/execution and final HIR-to-SimIR
   lowering off the AST adapter, delete `.fsimclass` and residual UDP codec
   surfaces, then remove every `ParsedDesign` elaborator API. No acceptance or
   batch-completion claim is made.

## Batch 188A 2026-09-17 14:38 MDT progress audit

1. Net progress remains positive. Class HIR now retains parameters, relations,
   typedef/member links, property initializers, callable declarations and body
   statement IDs, per-member origins, nested classes, and generate-alternative
   ownership in the existing semantic/HIR stores. A five-defect adversarial
   review then drove repairs for case-choice dependencies, alternative identity,
   class ownership/body closure, and recursive external-reference discovery.
2. VHDL configuration selection is HIR-authoritative and split into bounded
   selection/application and validation translation units. UDP execution now
   selects the compiled declaration directly, and object/mapped-library flows
   retain only metadata inventory plus the compiled bundle; production no
   longer publishes or reads `.fsimudp`.
3. The ordinary cache now stores the raw compiled-HIR bundle before
   specialization/elaboration under the existing checksum-framed ObjectCache
   V1 envelope. Its cache key validates the compiled-HIR schema, governed
   producer revision, and bijective role/ordinal source
   relocation, and canonical equality with the fresh compilation before using
   the temporary AST lowering adapter. Valid wrong bundles self-heal as misses,
   and honest oversized bundles bypass caching. The coordinated ABI/schema
   reference and governed digest pass their standalone check.
4. The first direct HIR-to-SimIR lowering slice is implemented in four bounded
   translation units for scalar expressions, structured statements, and simple
   processes without reconstructing syntax. Independent review found seven
   correctness blockers before the integrated build: unresolved bindings could
   drop operations, residual expressions were not specialized, signed/context
   semantics and VHDL equality were incomplete, wildcard first-execution was
   wrong, and several unsupported update/loop/sensitivity forms passed the
   capability gate. Transactional fallback and stricter gating are in repair.
5. The shortest remaining dependency is to finish that direct-lowering repair,
   then run one eight-worker warnings-as-errors build and grouped semantic,
   elaboration, application, object, and library tests across the complete
   class/configuration/UDP/cache/lowering wave. After evidence-backed repairs,
   port the remaining callable/timing/assertion/class consumers, remove
   `.fsimclass`, legacy UDP codecs, `SpecializedUnit::unit`, and every
   `ParsedDesign` elaborator API. No acceptance or batch-completion claim is
   made.

## Batch 188A 2026-09-17 15:28 MDT progress audit

1. Net progress remains positive. The first consolidated eight-worker Debug
   warnings-as-errors build across the semantic, elaboration, and application
   test executables completed all 575 steps. Two partition-boundary compile
   defects in the VHDL configuration helper were repaired before the green
   build; no source compression or single-edit build loop was introduced.
2. Direct test execution exposed two integration defects not visible to the
   compiler: one class-closure corruption test assumed a diagnostic ordering,
   and compiled VHDL configuration validation ran against a SystemVerilog root.
   The expectation now matches the first precise ownership failure and the
   validation entry point is language-gated. The grouped runtime suites remain
   pending until the current parallel repair wave rejoins.
3. Independent audits found a real compiled-bundle allocation-amplification
   path in the shared codec reader and redundant frontend expression ownership
   in post-elaboration specify records. Separate agents now own the aggregate
   decode-budget/exception boundary and specify-state cleanup, allowing those
   changes to proceed without overlapping the hierarchy/configuration files.
4. The AST-lifetime audit confirms the central acceptance blocker: the public
   compiled elaborator still requires a `ParsedDesign` lowering adapter,
   `HierarchyBuilder` retains it, and specialization caches retain owning
   `DesignUnit` copies. Timing HIR, class executable bodies, exact expression
   type semantics, and production population of specialization replacements
   remain data dependencies for a complete cutover.
5. The shortest remaining dependency is to rejoin and verify the decode and
   post-elaboration cleanup lanes, then remove `.fsimclass` through the
   compiled class HIR before broadening direct HIR lowering. After that, replace
   the specialized-unit AST owner and delete `ParsedDesign` elaborator APIs.
   No acceptance or batch-completion claim is made.

## Batch 188A 2026-09-17 16:20 MDT progress audit

1. Net progress remains positive. The consolidated warnings-as-errors Debug
   build completed all 1,255 steps after one missing test include was repaired.
   The first grouped verification wave then reduced sixteen observed failures
   to a small set of shared identity, relocation, and stale-fixture causes.
2. Valid repeated semantic source identities are no longer rejected as
   relocation collisions. Relocation still rejects distinct original
   path-and-digest identities that converge on one portable name; the cache,
   object, mapped-library, and affected application routes have focused passing
   evidence for the repaired boundary.
3. Artifact metadata round trips and stale-schema expectations are aligned with
   `.fsimobj` 8, mapped-library 6, portable schema 15, compiled-HIR bundle 1,
   and `.fsimdesign` 13. Direct VHDL HIR equality lowering now has a valid
   non-name expression fixture and the elaboration suite passes.
4. The user has explicitly waived the 2,000-line source ceiling ahead of its
   removal in the next batch. Batch 188A will not run or restructure code for
   `fsim.source-line-budget`; normal readable WebKit-style formatting remains
   required, and no compressed-line workaround is permitted.
5. The shortest remaining dependency is to rejoin the HIR ownership and bundle
   portability fixture repairs, rerun the grouped wave once, then resume the
   AST-ownership cutover at the class sidecar/runtime and public elaborator
   seams. `SpecializedUnit::unit`, the lowering adapter, `.fsimclass`, and
   `ParsedDesign` elaborator APIs remain acceptance blockers. No acceptance or
   batch-completion claim is made.

## Batch 188A 2026-09-17 17:26 MDT progress audit

1. Net progress remains positive. The repaired baseline completed a 442-step
   eight-worker Debug warnings-as-errors build and passed 19 of the 20 focused
   semantic, elaboration, application, object, library, cache, and ABI tests.
   The remaining extern-module collision exposed a linker distinction between
   prototypes and definitions rather than a broad pipeline regression.
2. `SpecializedUnit` is now an HIR-only cache owner keyed by the existing
   specialization key. Owning `DesignUnit` state is isolated in a transient
   `LegacyAstSpecializationWorkspace`, and prepared SystemVerilog roots no
   longer retain AST specializations between predeclaration and instantiation.
3. Runtime class constructor, instance/static function, and UVM function/task
   dispatch now prefer semantic class specializations and executable HIR. An
   explicit counter records executable frontend-adapter fallback; heap layout,
   UVM registration metadata, randomization, and constraints remain named
   adapter dependencies instead of being represented as a second owning IR.
4. The linker now permits repeated extern prototypes and exactly one concrete
   definition, with compiled lookup selecting the concrete definition. The
   integrated 826/418-step rebuilds found one local Werror defect and then
   passed. A 22-test gate exposed four focused regressions: Logic4 was promoted
   to Logic9 in the new evaluator, provisional root specialization duplicated
   delay diagnostics, language-based unit lookup lacked definition preference,
   and covergroup resolution ignored class-qualified `@sv-new:<type>` calls.
   All four repairs are staged for one consolidated rebuild.
5. The shortest remaining dependency is to rebuild and rerun that 22-test gate
   once, then implement the compiled hierarchy identity/instance/port slice.
   The hierarchy audit counted 65 direct `HierarchyBuilder::parsed_` uses
   across 17 translation units, so deletion must follow real HIR lookup and
   port-association cutovers rather than replacing the owner with another AST
   view. No acceptance or batch-completion claim is made.

## Batch 188A 2026-09-17 18:26 MDT progress audit

1. Net progress remains positive. The prior consolidated eight-worker Debug
   warnings-as-errors build completed 128 steps and the 23-test semantic,
   elaboration, runtime, application, object, library, cache, and ABI gate
   passed 23/23 before the current hierarchy wave began.
2. Decoded compiled-HIR lifetime coverage now destroys the compile-local parser
   workspace before bundle decode and verifies semantic unit, process, source,
   and origin identity through relocation and execution. Public checked-design
   ownership has a compile-time no-`parsed` guard; the transient structural
   lowering adapter remains the explicit acceptance blocker.
3. Specialized HIR now exposes deterministic direct and recursively generated
   instance records, selected-generate instance sets, compiled port and actual
   association views, exact linked child UnitIds, and direct child occurrence
   source/origin provenance. Generated compatibility occurrences use exact HIR
   source identity and reject ambiguous alternative matches rather than
   selecting by spelling.
4. SystemVerilog configuration designs, default liblists, instance/cell rules,
   use/config selections, origins, and linked targets are retained by HIR 8,
   validated by the bundle codec and linker, and preferred by root/child
   configuration selection. Direct HIR lowering also covers runtime packed
   bit and indexed part reads plus conservative selected local/signal writes.
5. The merged 1,163-step build reached the new hierarchy/configuration units
   and stopped on two identical C++20 lambda syntax defects in the dynamic
   selection preflight; both are repaired for the resumed grouped build. The
   shortest remaining dependency is that build plus the focused gate, followed
   by HIR-native dependent-generate selection and association/specialization
   application. The waived source-line gate was not run. No acceptance or
   batch-completion claim is made.

## Batch 188A 2026-09-17 19:23 MDT progress audit

1. Net progress remains positive. The repaired hierarchy/configuration wave
   completed an eight-worker Debug warnings-as-errors build and the expanded
   semantic, elaboration, runtime, application, object, library, cache, and ABI
   gate passed 23/23 before the next two HIR execution tranches began.
2. Exact child provenance no longer assumes that semantic occurrence origins
   and language-HIR origins are identical. Generated compatibility occurrences
   accept a uniquely qualified leaf with exact source identity, and VHDL
   32/64-bit integer process locals are accepted as dynamic HIR indices without
   widening the supported signal domain.
3. Compiled-design validation now checks generated-instance ownership, linker
   relocation includes those instances, and canonical input ordering makes
   link records deterministic. The semantic regression target and suite pass.
4. A decoded compiled-HIR bundle can now enter the public elaborator without a
   parser or compatibility AST for a flat SystemVerilog root. That path creates
   unit signals, retains direct unit/process/source/origin provenance, and
   lowers processes from HIR; its lifetime test passed after a 314-action
   eight-worker warnings-as-errors build.
5. Dependent-generate selection and callable invocation gates are in flight.
   The shortest remaining dependency is to merge and validate them once, then
   apply compiled associations and extend the parser-free hierarchy beyond the
   flat-root slice. The source-line gate remains waived and was not run. Public
   `ParsedDesign` overloads and the transient AST specialization workspace are
   still acceptance blockers; no completion claim is made.

## Batch 188A 2026-09-17 20:30 MDT progress audit

1. Net progress remains positive. Four disjoint tranches landed together:
   compiled named/positional/default/open association specialization,
   parser-free flat VHDL roots and concurrent statements, generated-class HIR
   identity handoff, and compiled-bundle-only object/mapped-library unit
   publication with `.fsimir` removed.
2. All stale workers were stopped before validation. The consolidated
   eight-worker Debug warnings-as-errors build completed after two narrow
   integration repairs: private VHDL name helpers were replaced by the shared
   configuration comparison and two shadowed association locals were renamed.
   The resumed build passed all 957 actions.
3. The focused semantic, elaboration, HIR, cache, object, library, and schema
   gate passed 10/12 initially. All five previously stale schema contracts now
   pass at object 8, mapped library 6, portable 15, compiled-HIR bundle 1,
   Design 13, SV HIR 8, and VHDL HIR 5.
4. The generated-class lifetime failure is repaired by conservatively allowing
   class-only generate HIR in parser-free SystemVerilog roots and returning
   stable selected class identities; its focused test now passes after the
   compilation workspace is destroyed.
5. The shortest remaining dependency is parser-free structural child creation
   and port association for decoded object/library bundles. Removing `.fsimir`
   correctly exposed this boundary as `FSIM-ELAB-HIR-001` for a simple SV leaf
   instance. The next cohesive edit is direct HIR child specialization, signal
   association, process/concurrent lowering, and recursive occurrence
   provenance. The source-line gate remains waived and was not run; no
   completion claim is made.

## Batch 188A 2026-09-17 21:30 MDT progress audit

1. Net progress remains positive. Parser-free recursive SystemVerilog and VHDL
   child hierarchy creation now covers compiled target resolution, parameter
   or generic identity, port aliases and open outputs, selected instance
   generates, direct process or concurrent lowering, exact occurrence
   provenance, and cycle guards from decoded bundles.
2. The owning `.fsimclass`, `.fsimir`, and `.fsimudp` sidecar codecs are now
   absent from production sources and public headers. Their structural-AST
   round-trip tests were deleted, source relocation metadata moved to a
   syntax-free header, and object/library metadata retains explicit negative
   rejection coverage for all three legacy suffixes.
3. The portable object, stale-schema, producer-diagnostic, and aggregate v3
   schema freezes now describe the single `FSIMCHIR` compiled-design bundle;
   their direct checks pass at object 8, portable 15, compiled-HIR bundle 1,
   mapped library 6, and Design 13. The broader resource/composition contracts
   are being updated to the same owner before the joined build.
4. Mutable string storage and assignment plus class instance/static method
   argument binding and packed/string copyout are static-ready in direct HIR
   lowering. All agent work has stopped for consolidation; completed agent
   records cannot be deleted by the collaboration API, but no stale worker is
   executing.
5. The shortest remaining dependency is the joined eight-worker build and
   `fsim.application.classes` gate. Its predicted next boundary is ordinary
   module callable output/inout/ref binding, followed by containers and
   randomization. The waived source-line gate was not run; no acceptance or
   batch-completion claim is made.

## Batch 188A 2026-09-17 22:30 MDT progress audit

1. Net progress remains positive. Compiled-HIR constant references, preserved
   SystemVerilog value forms, checked class casts, packed class properties,
   recursive child hierarchy, and the single-bundle artifact path now compose
   in one warnings-as-errors application build.
2. Class task calls now use the existing automatic callable frame directly
   from decoded HIR, including instance receivers, packed input/output/inout
   formals, copyout, task returns, and deferred callable-body lowering. A
   normalized constant HIR delay emits `WaitFor`, preserving suspension and
   simulation time without reconstructing syntax.
3. The 137-action eight-worker Debug warnings-as-errors application build
   passed. The focused class artifact advanced past the task/delay boundary to
   the next unsupported records: class-handle container access. Fixed,
   dynamic, queue, and associative handle read/write, resize, push, pop, and
   size hooks are now implemented as one HIR-to-SimIR tranche and await the
   joined validation cycle.
4. No stale agent is executing. The collaboration API retains completed and
   interrupted records but exposes no delete operation; the sole historical
   worker still marked active was interrupted before this cycle.
5. The shortest remaining dependency is to validate the class-container
   tranche, then follow the first remaining decoded-bundle residual rather
   than widening unrelated paths. Public `ParsedDesign` elaboration overloads
   and the transient AST specialization workspace remain acceptance blockers.
   The waived source-line gate was not run; no completion claim is made.

## Batch 188A 2026-09-17 23:25 MDT progress audit

1. Net progress remains positive. The direct compiled-HIR class path now
   passes all three execution engines, its native-cache rerun, and object and
   design-artifact elaboration after adding class built-ins, defaulted and
   named callable actuals, width normalization, immediate assertions, event
   controls, and inline randomize-constraint preservation.
2. Inline constraints now remain HIR associations through compilation,
   linking, specialization, artifact encoding, and final ClassMethodCall
   lowering. Callable debug locals are unit-qualified, eliminating collisions
   between independently lowered methods.
3. The latest 146-action eight-worker Debug warnings-as-errors application
   build passed. The class regression now reaches the relocated-design
   inspection and reports an intentionally empty legacy frontend class
   specialization vector while the compiled class HIR and SimIR remain in the
   artifact.
4. No stale agent is executing. Completed and interrupted records cannot be
   deleted by the collaboration API; all available historical workers remain
   stopped. The source-line gate remains waived and was not run.
5. The shortest remaining dependency is the HIR-only class runtime metadata
   cutover: remove the relocated-artifact test and Simulation dependency on
   AST-owning frontend specializations, derive runtime class behavior from
   compiled HIR and SimIR, then validate the entire class gate once. Public
   ParsedDesign elaboration overloads and the transient AST specialization
   workspace remain later acceptance blockers; no completion claim is made.

## Batch 188A 2026-09-18 00:25 MDT progress audit

1. Net progress remains positive. The HIR-only class runtime metadata path now
   supplies semantic class descriptors, UVM registration, bounded class-handle
   queues, and randomize callback validation without requiring a frontend
   class specialization in a relocated design artifact.
2. Direct interpreter, debug-interpreter, LLVM, native-cache, object, and
   design-artifact class execution reach the relocated artifact constructor.
   The remaining failure is localized to `class_top.source_accumulator`: its
   serialized two-state integer initializer is known zero, but hierarchy
   construction incorrectly assigns `sv_wire` resolution and converts the live
   value to high impedance before VPI publication.
3. Temporary broad lowering diagnostics have been removed. The retained VPI
   diagnostic now reports the published path, runtime signal identity, and
   signal name, making artifact metadata failures actionable without exposing
   parser or AST ownership.
4. No stale agent is executing. The collaboration API retains stopped records
   and provides no delete operation; the active roster contains only the root
   agent. The source-line gate remains waived and was not run.
5. The shortest remaining dependency is to trace the incorrect variable/net
   resolution metadata through compiled-HIR projection, correct it, remove the
   temporary test probe, and run one joined eight-worker build plus the full
   class gate. Public `ParsedDesign` APIs and transient AST specialization
   ownership remain later acceptance blockers; no completion claim is made.

## Batch 188A 2026-09-18 01:24 MDT progress audit

1. Net progress remains positive. The public application build now invokes
   only the compiled-design elaboration API after projecting coverage and
   destroying compile-local AST storage; HIR delay selection and normalization
   run after that lifetime boundary.
2. Coverage reconstruction and SystemVerilog/VHDL delay normalization now
   consume compiled HIR directly. The joined 465-action, eight-worker Debug
   warnings-as-errors build passed for the application and UVM targets, and
   the dedicated compiled-HIR cache regression passed.
3. A focused HIR-lowering test no longer reparses sources to manufacture an
   AST lowering adapter. Its first parser-free run exposed a VHDL signal
   construction rollback defect for a bit-vector default; metadata creation is
   now transactional and bit-string constant evaluation is the active repair.
4. No stale agent is executing. Completed records cannot be deleted by the
   collaboration API; only the root agent is active. The waived source-line
   gate was not run.
5. The shortest remaining dependency is to complete VHDL bit-string folding,
   rerun the three compiled-HIR regressions, then address the first failure in
   the class/artifact/time focused gate. Internal legacy AST test adapters and
   broader AST-based elaborator ownership remain acceptance blockers; no
   completion claim is made.

## Batch 188A 2026-09-18 02:24 MDT progress audit

1. Net progress remains positive. Decoded VHDL default generics now select
   dependent generates, root defaults participate in specialization identity,
   and direct SystemVerilog HIR lowering has advanced through bounded loops,
   named-block disable, Verilog source provenance, static memories, residual
   specialization evaluation, and canonical wide parameter identities.
2. The parser-free HIR lowering regression passes. The SystemVerilog HIR gate
   now reaches the nettype/alias/let fixture after passing interface classes,
   DesignIR construction, loops, named disable, cache provenance, static
   memory simulation, and VPI canonical-parameter checks.
3. One cohesive ownership slice is active: generate-local aliases and lets are
   retained in HIR, selected generate declarations/processes/statements are
   routed to lowering, and HIR aliases are compiled into whole-signal identity
   groups or bidirectional slice transfers. Its joined build and execution
   gate are pending; no intermediate success is claimed.
4. The active workers have accurate lanes for compiled-let lowering and VHDL
   hierarchy regression. Artifact/cache and class-runtime workers completed
   their gates and stopped; there are no stale running workers. The source-line
   gate remains waived and was not run.
5. The shortest remaining dependency is to make the nettype/alias/let fixture
   pass from a decoded bundle, then rerun the generated-class and VHDL gates
   against the same integrated hierarchy path. Broader timing/assertion/bind
   and final AST-ownership closure remain later blockers; no completion claim
   is made.

## Batch 188A 2026-09-18 03:30 MDT progress audit

1. Net progress remains positive. The production elaboration entry point now
   constructs hierarchy from `semantic::CompiledDesign` directly, production
   `ParsedDesign` overloads are gone, and the legacy syntax route is isolated
   behind test support while the remaining internal AST ownership is audited.
2. A joined eight-worker Debug warnings-as-errors build completed all 712
   remaining actions successfully after one pointer-shadowing compile repair.
   The focused compiled-HIR matrix passes 8 of 18 gates, including direct HIR
   lowering, classes through relocated artifacts, cache, object, design
   artifact, library artifact, semantic class specialization, and UVM.
3. The ten remaining focused failures have been reduced to four concrete
   implementation lanes: resolved-net driver classification, VHDL
   process/concurrent/configuration capability, class static storage across
   multiple roots, and iterative-generate occurrence specialization. The
   first three have accurately named active workers; the root owns the fourth.
4. No stale or unused agent is running. Completed and interrupted records are
   inert and cannot be deleted through the collaboration API. The source-line
   gate remains waived at the user's direction and was not run.
5. The shortest remaining dependency is to represent each selected iterative
   generate occurrence in specialized HIR so its declarations, processes,
   concurrent statements, instance actuals, and genvar-dependent values reach
   hierarchy construction without reconstructing syntax. One joined build and
   focused execution matrix will follow this cohesive source wave.

## Batch 188A 2026-09-18 04:30 MDT progress audit

1. Net progress remains positive. A complete 913-action Debug
   warnings-as-errors build passed before the current source wave, and the
   focused compiled-HIR matrix then passed 9 of 18 gates. The class multi-root
   static-storage failure is closed; the remaining failures are localized to
   generated hierarchy materialization, VHDL direct-HIR capability, and one
   stale-layout elaboration executable.
2. The cohesive hierarchy wave now materializes separate SystemVerilog and
   VHDL generate occurrences with hierarchy-local identities, paths, signals,
   processes, concurrent statements, aliases, and instances. VHDL constant
   input actuals and SystemVerilog dependent typedef widths are resolved from
   the occurrence specialization without rebuilding syntax.
3. VHDL executable-HIR resolution now covers architecture/entity and package
   declaration/body scopes, inherited use clauses, selected generic-package
   members, procedure names, enumeration attributes, and scalar unary `not`.
   The joined 405-action eight-worker Debug rebuild is currently running; no
   result from this source wave is claimed yet.
4. The stale `fsim.elaboration` crash was traced under GDB to a deliberate
   mixed object layout after `Lowerer` changed; no speculative source patch was
   made. All implementation workers have stopped. One accurately named,
   read-only worker is auditing the remaining AST ownership cutover; there are
   no stale running workers. The waived source-line gate was not run.
5. The shortest remaining dependency is to finish the joined build, run the
   18-test focused matrix once, and repair only the newly exposed first-order
   residuals. After that gate, the next cohesive slice is deletion of the
   legacy AST ownership surfaces identified by the audit; no completion claim
   is made.

## Batch 188A 2026-09-18 05:30 MDT progress audit

1. Net progress remains positive. Two joined Debug warnings-as-errors rebuilds
   completed successfully after integration repairs; the latest 135-action
   rebuild includes the second focused hierarchy/lowering repair wave.
2. The compiled-HIR path now covers occurrence-specialized generates, direct
   HIR processes/callables, selected VHDL port adapters, linked package and
   callable provenance, context-imported generic defaults, configuration-rule
   exclusions, imported SystemVerilog nettypes, and retained HIR timing
   controls. Dynamic integral evaluation no longer folds mutable variables.
3. The last executed nine-test matrix preceded the latest repair wave and
   therefore remains 0/9. Each failure has since received a targeted source
   fix, but those tests have not yet been rerun; no pass claim is made.
4. All implementation workers have handed off and were stopped. One debugger
   probe hung while calling an inferior method, and both debugger and inferior
   were explicitly terminated; the latest process audit shows no GDB, test,
   or Ninja process. The waived source-line gate remains unrun.
5. The shortest remaining dependency is to rerun the nine-test matrix against
   the successful joined build, repair any residuals as one cohesive wave,
   then execute the broader focused and full validation gates. Internal legacy
   AST ownership remains the principal architectural acceptance blocker after
   functional closure.

## Batch 188A 2026-09-18 06:33 MDT progress audit

1. Net progress remains positive. One complete 404-action Debug
   warnings-as-errors build and the subsequent 277-action consolidated
   elaboration/application rebuild both passed with eight workers.
2. The nine-test compiled-HIR matrix improved from 0/9 to 5/9 before the
   latest wave. The generated-class SystemVerilog gate now passes, and the
   VHDL configuration gate passes after canonicalizing object-index actuals;
   the VHDL type-generic gate now reaches its later predefined-attribute
   coverage instead of failing its earlier unspecified-type and mode-view
   cases.
3. Direct compiled-HIR specialization now retains configuration-cell
   identity, implicit net drive semantics, declaration-order parameter and
   localparam metadata, and more integral system-function coercions. The HIR
   lowerer also gained static string-object initialization plus direct display,
   formatted/string output, file-close/flush, and monitor-control operations.
4. A new per-fixture elaboration selector allowed all 70 legacy elaboration
   fixtures to run in eight parallel processes. Four currently pass; the
   failures cluster around explicit unported HIR declaration, statement,
   callable, file, and composite-layout adapters rather than parser ownership
   or nondeterministic crashes. One concurrent-build stale-object race was
   found, so workers now hand off source-only waves and the root owns joined
   builds. No GDB, test, CMake, or Ninja process remains after the latest
   probes, and the waived source-line gate remains unrun.
5. The shortest remaining dependency is the active three-part source wave:
   close the later VHDL predefined-attribute layout case, normalize the
   remaining signed/sized SystemVerilog specialization metadata, and identify
   the first unsupported string-process HIR construct. Then rerun the focused
   gates and repartition the refreshed fixture inventory before deleting the
   remaining internal legacy-AST ownership surfaces.

## Batch 188A 2026-09-18 07:36 MDT progress audit

1. Net progress remains positive. Multiple consolidated Debug
   warnings-as-errors rebuilds completed successfully with eight workers; the
   latest completed joined elaboration/application build finished all 279
   actions before the current source-only wave.
2. Direct compiled-HIR lowering now covers SystemVerilog file calls and memory
   transfer, automatic string callables, conditional string expressions,
   string generate selection, fixed-array runtime containers, VHDL
   predefined enumeration attributes, and accumulated parameter-association
   diagnostics. The focused gates have advanced beyond each of their earlier
   failures to later, independently identified boundaries.
3. The current cohesive source wave adds direct string-port object aliasing
   and driver validation, parser-free `$sformatf` negative diagnostics, VHDL
   type-actual inference, and packed-structure/packed-union width recovery for
   process-local HIR declarations. These edits have not yet received their
   joined build or execution gate, so no result from this wave is claimed.
4. Completed source workers were stopped after handoff; one accurately named
   VHDL type-actual worker remains active. A live process audit found no GDB,
   LLDB, CMake, or Ninja process. The waived source-line gate remains unrun as
   directed.
5. The shortest remaining dependency is to finish the VHDL handoff, build the
   four-lane source wave once, and rerun the VHDL type-generic, SystemVerilog
   specialization, string, and file gates together. The refreshed failures
   will determine the next broad HIR-lowering partition; legacy AST ownership
   remains the architectural acceptance blocker after functional closure.

## Batch 188A 2026-09-18 08:36 MDT progress audit

1. Net progress remains positive. The latest joined Debug
   warnings-as-errors elaboration/application rebuild completed all 273
   actions with eight workers after two missing-field initializer warnings
   were repaired.
2. Four of the six immediate focused gates pass: direct string lowering,
   direct file lowering, VHDL configurations, and SystemVerilog preprocessor
   generates. The VHDL type-generic and SystemVerilog specialization gates
   advanced to later HIR-only failures; targeted layout and cast/replication
   fixes are present in the current unbuilt source wave.
3. A refreshed eight-process fixture inventory records 5 of 69 elaboration
   fixtures passing in `/tmp/fsim-elab-inventory.H8xaN9`, improving the prior
   snapshot by closing the string and file clusters. The remaining failures
   are concentrated in process/callable HIR coverage, hierarchy target
   resolution, composite layout, and structural syntax-adapter removal.
4. The compiled root no longer retains an AST unit pointer, obsolete
   ParsedDesign test-adapter coverage has been removed, and the ownership
   audit identifies the mixed `Lowerer` and legacy `HierarchyBuilder` forest
   as the remaining production AST reachability. All source agents have been
   stopped after handoff except one accurately named read-only VHDL gap audit;
   no stale GDB remains. The waived source-line gate remains unrun as directed.
5. The shortest remaining dependency is one joined eight-worker build of the
   current VHDL layout, SystemVerilog cast/case/configuration, mixed-language,
   and test-adapter wave, followed by the focused matrix and a refreshed
   inventory. The next source partition will be selected from those results.

## Batch 188A 2026-09-18 09:48 MDT progress audit

1. Net progress remains positive. The joined Debug warnings-as-errors build
   for the direct-HIR process/case, compiled hierarchy/layout, and public
   AST-lifetime wave completed all 684 actions successfully with eight
   workers.
2. The corrected focused matrix now passes 8 of 18 gates. VHDL
   configurations, SystemVerilog preprocessor generates, strings, files, case
   qualifiers, direct HIR lowering, and multi-library resolution pass; the
   compiled-HIR cache and VHDL type-generic gates now fail only their later
   specialization-identity assertions.
3. The remaining execution failures are partitioned into SystemVerilog
   residual expression/case lowering, VHDL concurrent and edge-sensitive
   execution, specialization identity stability, and configuration/driver
   diagnostics. The identity and configuration source waves have handed off;
   the SystemVerilog source wave remains active while the VHDL failure shapes
   are being isolated without overlapping its files.
4. Public lifetime coverage now verifies that elaboration accepts only
   `CompiledDesign`, rejects `ParsedDesign`, and that published object and
   mapped-library artifacts contain one compiled-HIR bundle with no legacy
   per-unit syntax payloads. Completed workers are inert, no GDB, LLDB, CMake,
   or Ninja process remains, and the waived source-line gate remains unrun.
5. The shortest remaining dependency is to complete the two active lowering
   seams, join-build the four-part source wave once, and rerun the focused
   matrix plus the 69-fixture inventory. Functional closure will then unblock
   deletion of the remaining mixed `Lowerer` and legacy `HierarchyBuilder`
   AST ownership forest.

## Batch 188A 2026-09-18 10:50 MDT progress audit

1. Net progress remains positive. The association/binding, direct-HIR
   statement/expression/callable, declaration/layout, and structural-unit
   waves have all handed off. Direct compiled-HIR UDP lowering is integrated,
   and the latest joined Debug warnings-as-errors build completed all 477
   remaining actions successfully with eight workers.
2. The joined build first exposed missing equality support for the new
   specialized SystemVerilog and VHDL type payloads plus four local UDP
   warnings/errors. Those integration faults were repaired together; no
   execution result from this wave is claimed until the focused gates run.
3. The UDP path now constructs its SimIR process directly from the compiled
   declaration, including sequential state, edge tables, instance-array
   slicing, strengths, delays, source origins, and specialization provenance.
   It does not rebuild a frontend design unit or process for the compiled
   path.
4. The public `CheckedProject` lifetime boundary remains compiled-only; the
   remaining `parsed` state is held by the compile-local
   `CompilationWorkspace` and is discarded when the compiled project is
   released. Two read-only agents are mapping the structured timing-HIR seam
   and the remaining internal elaboration AST ownership. No GDB or LLDB was
   started, and the waived source-line gate remains unrun.
5. The shortest remaining dependency is to run the focused UDP,
   specialization, mixed-language, and VHDL gates against this clean build,
   repair any residuals as one cohesive wave, then implement structured
   specify/timing HIR and its direct consumer before deleting the remaining
   legacy elaboration ownership forest.

## Batch 188A 2026-09-18 11:45 MDT progress audit

1. Net progress remains positive. The joined Debug warnings-as-errors build
   completed all 700 actions successfully with eight workers, covering the
   elaboration and application targets after the direct-HIR UDP, process,
   hierarchy, association, and timing source waves were joined.
2. The compiled-HIR cache plus object, mapped-library, ordinary-cache,
   relocation, restartability, corruption-isolation, and deterministic
   artifact gates pass 8/8. The focused execution matrix exposed later
   residuals rather than artifact or lifetime regressions: 3 of its initial
   16 selectors passed, while the broader selector inventory concentrated
   failures in shared process/callable lowering, association/type layout,
   specify timing, and generated-scope handling.
3. The current cohesive source wave fixes deterministic wildcard and
   case-folded associations, VHDL constrained layouts and configured
   positional components, SystemVerilog packed-member/type-parameter layout,
   direct process/callable and switch metadata, structured timing values, and
   generated `defparam` ownership. The generate occurrence path now uses the
   selected region label instead of its sibling alternative label. These
   current edits have not yet received their joined build, so no execution
   result from them is claimed.
4. The completed association worker was stopped immediately after handoff.
   Three accurately named source workers remain active for process/callable
   closure, timing/generated-`defparam` closure, and removal of syntax-capable
   frontend type objects from DesignIR metadata. A live process audit found no
   GDB or LLDB process. The waived source-line gate remains unrun as directed.
5. The shortest remaining dependency is to finish those three handoffs, apply
   the narrow selected-generate `defparam` activation seam, run one joined
   eight-worker build, and rerun the full elaboration selector inventory plus
   the application and artifact gates. Functional closure then unblocks the
   remaining legacy `HierarchyBuilder`, mixed `Lowerer`, coverage, and foreign
   AST ownership removal.

## Batch 188A 2026-09-18 12:41 MDT progress audit

1. Net progress remains positive. The joined Debug warnings-as-errors build
   completed all 1,214 actions successfully with eight workers after the
   association, timing, generated-`defparam`, packed-type, coverage inventory,
   foreign provenance, procedural assignment, and constant-effect waves were
   combined.
2. The compiled bundle, object/design artifact, mapped-library, ordinary
   cache, corruption, relocation, restartability, determinism, and schema
   lanes pass 20 of 22 focused tests. The two failures are later
   generated-class ownership and VHDL specialization-display regressions;
   every persistence and format contract in those lanes passes. The coverage,
   foreign, SystemC, class, and procedural lane passes 28 of 33 tests.
3. The broad direct-HIR elaboration inventory currently passes 8 of 52
   selectors. Twenty-one failures share process or concurrent-statement
   capability gaps; the remaining failures concentrate in generated-class
   identity, specialization/type metadata, configuration selection, composite
   default layout, and expected-diagnostic transitions. Three source workers
   now own those non-overlapping root-cause groups.
4. Public elaboration accepts only `CompiledDesign`, and `SpecializedUnit`
   owns only `SpecializedHirUnit`. The refreshed ownership audit still finds
   the internal `LegacyAstSpecializationWorkspace` owning a `DesignUnit`, the
   legacy `HierarchyBuilder`/mixed `Lowerer` forest, and frontend coverage
   state retained by `BuiltProject`; these remain architectural acceptance
   blockers after functional closure. All batch GDB probes exited, no GDB or
   LLDB process remains, and the waived source-line gate remains unrun.
5. The shortest remaining dependency is to finish the active generated-class
   and class/UVM, VHDL specialization/layout, and direct process/concurrent
   handoffs, run one joined eight-worker build, and rerun the failing focused
   lanes. That result will define the next large deletion/cutover wave for the
   legacy elaboration AST forest and frontend coverage adapter.

## Batch 188A 2026-09-18 13:38 MDT progress audit

1. Net progress remains positive. The first joined Debug warnings-as-errors
   build completed all 579 remaining actions with eight workers after two
   local integration repairs. The focused persistence, coverage, foreign,
   class, and process lane then passed 31 of 38 tests. The expanded direct-HIR
   inventory now contains 69 selectors and passed 13 before the next source
   wave; this supersedes the earlier 8-of-52 snapshot.
2. The completed wave fixes generated-class alternative duplication, stale
   class-expression pointer memoization, VHDL scalar identity expectations,
   direct-HIR static unpacked-array access, procedural concatenation copy-out,
   coverage task emission, scoped VHDL named-type and protected-object
   materialization, specialized packed layouts, constant-function lexical
   lookup, inactive-generate traversal, local-parameter `defparam`
   diagnostics, and recursive SystemVerilog container metadata.
3. Runtime and artifact ownership advanced independently: persistent coverage
   state is HIR-native, class/UVM execution is HIR-only, the dead frontend
   class-property runtime overload and obsolete disabled coverage-codec test
   are deleted, and the design-artifact codec no longer declares frontend AST
   archive traits. `git diff --check` passes across the joined worktree.
4. The next eight-worker build reached action 475 of 1,197 and intentionally
   stopped where `ElaboratedDesignState::SignalInfo` still embeds syntax-
   capable `frontend::PackedMember`, `Type`, and `Expression` metadata. This
   is now an explicit architectural blocker rather than a hidden codec
   fallback. Parallel workers own its syntax-free metadata replacement, the
   remaining coverage-analysis AST APIs, and removal of the test-only
   `ParsedDesign` elaborator overload. No GDB or LLDB process remains, and the
   waived source-line gate remains unrun.
5. The shortest remaining dependency is to finish the syntax-free
   `SignalInfo`/DesignIR metadata cutover, resume the same joined build, and
   rerun the seven focused failures plus the 69-selector inventory. Functional
   deltas will then drive the HIR-only `Lowerer` split and deletion of the
   legacy hierarchy/specialization AST forest.

## Batch 188A 2026-09-18 14:30 MDT progress audit

1. Net progress remains positive. Four large ownership waves have joined the
   worktree: production `ParsedDesign` compatibility removal, test-only
   compile-and-elaborate isolation, syntax-free DesignIR signal/type metadata,
   and relocation of structural coverage analysis into the compile-local
   frontend. A fifth wave replaced the artifact coverage codec's frontend AST
   payload with canonical runtime state reconstructed and validated against
   compiled SystemVerilog HIR.
2. Production elaboration no longer references `ParsedDesign`,
   `legacy_parsed`, or `compatibility_unit`. Direct compiled paths use the new
   compact HIR-only `Lowerer` constructor for root/package and UDP lowering;
   the full legacy lowerer remains internally available and is still an
   acceptance blocker.
3. The joined Debug warnings-as-errors build passed the elaboration and
   application library boundaries, including the new coverage-state codec.
   Integration repairs removed one stale root-port variable, one dead
   configuration helper, adapted VHDL array-shape metadata, and qualified the
   new test-only compilation helper. The resumed eight-worker build is still
   running, so no focused execution result is claimed yet.
4. The standalone coverage-state layout genuinely changed and therefore uses
   schema 8; its freeze, stale-schema, v2-rejection, documentation, and
   determinism evidence moved with it. `git diff --check` passed before this
   resumed build. No GDB or LLDB process remains, and the waived source-line
   gate remains unrun.
5. The shortest remaining dependency is to finish the current build and run
   the grouped persistence/coverage/process, VHDL, and 69-selector direct-HIR
   suites. Their functional deltas, together with the read-only residual AST
   partition audit, will select the next large hierarchy/lowerer deletion
   wave.

## Batch 188A 2026-09-18 15:23 MDT progress audit

1. Net progress remains strongly positive. The hierarchy, SystemVerilog and
   VHDL specialization, shared specialization, and lowerer ownership waves
   removed the legacy AST elaboration forest in one cohesive cutover. The
   worktree now deletes more than 100,000 obsolete source lines while retaining
   eight HIR-only lowerer partitions and the compiled hierarchy paths.
2. A static acceptance scan now finds no `ParsedDesign`, `DesignUnit`, or
   prohibited structural expression, statement, process, generate, instance,
   parameter-override, or type AST in production elaboration headers or
   sources. A separate declaration/definition audit found no retained Lowerer,
   HierarchyBuilder, target, or VHDL configuration linkage gaps, and all
   retained translation units are present in the build and package manifests.
3. The first post-cutover Debug warnings-as-errors build exposed and repaired
   six mechanical deletion seams: obsolete constant-function memoization,
   compiled unit-candidate resolution, two truncated namespace boundaries,
   packed-index support, and the HIR literal carrier. The resumed eight-worker
   build now compiles the elaboration library and is progressing through the
   application layer.
4. `git diff --check` and the prohibited-AST scan passed before the resumed
   build. No GDB or LLDB process remains. The intentionally waived source-line
   gate and pre-Batch-190 sanitizer lanes remain unrun.

5. The shortest remaining dependency is to complete this joined build, repair
   any downstream compiled-HIR integration failures as a group, then rerun the
   57-test focused lane and 69-selector direct-HIR inventory. Those results
   will drive the next functional HIR-lowering wave rather than restoring any
   deleted AST path.

## Batch 188A 2026-09-18 16:06 MDT progress audit

1. Net progress remains positive. The complete post-cutover Debug
   warnings-as-errors build passed, then the joined hierarchy, executable-HIR,
   and coverage wave rebuilt all 108 affected actions with eight workers.
   VHDL inferred-type and nested-configuration regressions now pass, and the
   expression selector advances beyond the prior arbitrary-width and power
   sizing failures.
2. The current wave adds SystemVerilog signedness intrinsics, arbitrary-width
   constants, VHDL aggregate/member/stride-aware lowering, type-parameter and
   type-generic forwarding, package dependency closure, configuration suffix
   reconstruction, and transactional coverage-object materialization. The
   remaining coverage failure was reduced to a copied mutable HIR vector; its
   finder now retains a reference instead of returning a dangling pointer.
3. The focused regression run passed two of three tests. Coverage remains to
   be rebuilt and rerun after the pointer repair. The expression selector now
   reaches its VHDL negative-exponent diagnostic; the HIR path restores the
   locally-static nonnegative exponent check and `FSIM-ELAB-091` diagnostic.
4. Three independent source waves are active for VHDL composite/runtime
   lowering, SystemVerilog specialization/process lowering, and unified
   artifact/cache plus AST-lifetime closure. Their edit ownership is
   partitioned so the next compile/test iteration can validate a materially
   larger joined change. `git diff --check` passed before these waves, no GDB
   or LLDB process remains, and the waived line gate and sanitizer lanes remain
   unrun.
5. The shortest remaining dependency is to join those three waves, run one
   eight-worker Debug build, and rerun the focused application lane plus the
   69-selector direct-HIR inventory. Their residual failures will select the
   next cohesive lowering wave.

## Batch 188A 2026-09-18 16:54 MDT progress audit

1. Net progress remains positive despite the deliberately broader regression
   inventory. The latest joined warnings-as-errors Debug build passed all 175
   affected actions. The 57-test focused lane now exposes the remaining HIR
   execution surface directly: six pass and 51 fail, while the 69-selector
   direct inventory has 16 confirmed passes. These results replace the earlier
   narrow partial counts and are being used as the active repair baseline.
2. The current large wave has repaired canonical SystemVerilog child-value
   forwarding, compile-time declaration storage, VHDL composite aggregates and
   dynamic member access, mixed-language construction, SystemC boundary
   metadata, and unified `.fsimdesign` compiled-HIR persistence. A further
   mixed/SystemC wave has joined fixes for VHDL component defaults, alias
   subtype validation, and native child Logic4/Logic9 bindings.
3. The `.fsimobj` failure was traced to an architectural violation in object
   loading: compiler-supplied VHDL libraries were reparsed to reconstruct HIR.
   The active repair persists the complete selected compiler-package HIR in the
   same object bundle, projects primary and dependency libraries after decode,
   and links them without syntax reconstruction. Object compilation selects a
   complete standard environment so independently compiled VHDL units share a
   deterministic dependency bundle.
4. The coverage crash is no longer a lifetime failure. Batch GDB showed the
   HIR hierarchy path materialized `real` declarations as ordinary packed
   signals with scalar kind `None`; the active repair derives scalar metadata
   from the specialized HIR type and creates a canonical zero scalar payload.
   All batch debugger processes exited, `git diff --check` passes, and the
   waived line gate and pre-Batch-190 sanitizer lanes remain unrun.
5. Two executable-HIR waves remain active for VHDL callable/process storage and
   SystemVerilog callable/string/fork/case behavior, with a test-only worker
   adding multi-object dependency-bundle regressions. The shortest remaining
   dependency is to join those waves with the object and scalar repairs, run
   one eight-worker Debug build, then rerun the artifact, coverage, focused,
   and direct-HIR lanes before selecting the next large residual cluster.

## Batch 188A 2026-09-18 17:44 MDT progress audit

1. Net progress remains positive. The latest integrated warnings-as-errors
   Debug build completed all 922 actions with eight workers. The joined
   callable, scalar-metadata, object-bundle, and mapped-library wave repaired
   the SystemVerilog line-directive and real-valued coverage paths; the
   artifact-library, coverage, and line-directive focused tests now pass.
2. `.fsimobj` compilation and loading no longer reconstruct compiler packages
   from syntax. Each object persists its primary library plus the complete
   selected compiler dependency environment, loading projects both from the
   decoded bundle, verifies dependency identity, and links one shared
   environment. Library projection now retains only the closed semantic source,
   span, origin, and expansion provenance required by selected records, making
   compiler-library bundle bytes independent of the compiling object source.
3. The new two-object VHDL cache regression compiles separate work libraries,
   deletes both sources, and successfully loads and links both decoded bundles.
   Its last assertion was corrected to compare the loaded
   `ieee.std_logic_1164` records with one projected IEEE bundle because the
   model intentionally represents the package declaration and package body as
   two units; this is not a duplicated dependency environment.
4. The remaining selected-lane failure is `artifact_phases`: one VHDL
   architecture still reaches the structural-adapter guard, while the
   SystemVerilog residuals are now isolated to the `scalar_artifact` initial
   process and the `container_alias_artifact` callable process. Dedicated
   VHDL, SystemVerilog, and projection-integrity workers are active on those
   independent seams. No GDB or LLDB process remains, and the waived line gate
   and pre-Batch-190 sanitizer lanes remain unrun.
5. The shortest remaining dependency is to join those three cohesive repairs,
   run one eight-worker build, and rerun the five selected artifact/cache lanes.
   After that boundary passes, the 57-test focused lane and 69-selector direct
   inventory will establish the next broad functional cluster.

## Batch 188A 2026-09-18 18:44 MDT progress audit

1. Net progress remains positive. The latest integrated warnings-as-errors
   Debug build completed all 132 affected actions with eight workers. In the
   six-test artifact/cache/semantic boundary, compiled-HIR cache, library
   artifact, coverage, and line-directive tests pass; the artifact and semantic
   tests now fail at later, independently diagnosed seams.
2. Multi-input compiled-HIR linking now carries every top-level SystemVerilog
   collection. The linker had projected the covergroup instance into the
   `.fsimobj` correctly but omitted DPI and covergroup records when appending a
   non-leading bundle. Append, relocation, and structural range validation now
   cover both collections, with a regression that forces nonzero relocation
   and verifies their source, origin, profile, and expression identities.
3. `artifact_phases` advanced past its coverage-state assertions. Its next VPI
   failure exposed an implicit interface-port direction left as `unknown` by
   the HIR handoff. Declaration-aware hierarchy materialization now applies the
   SystemVerilog effective `inout` direction to interface ports, and the test
   advanced again to an LLVM `WaitSensitivity` lowering guard in the retained
   VHDL attribute-stimulus process.
4. Focused workers are repairing the VHDL association-resolution regression,
   the LLVM wait-sensitivity lowering gap, and incomplete top-level HIR
   corruption validation. A collection audit confirmed that all 11
   SystemVerilog and eight VHDL owning collections now traverse codec,
   projection, relocation, and link append; it separately identified an
   included-file ownership weakness for global DPI and covergroup records.
5. The shortest remaining dependency is to join the association, wait, and
   validation repairs, rerun the six-test boundary, then fix explicit
   projection ownership for included-file auxiliary SystemVerilog records
   before the broader 57-test and direct-HIR inventories. No GDB or LLDB
   process remains; the waived line gate and pre-Batch-190 sanitizer lanes
   remain unrun.

## Batch 188A 2026-09-18 19:40 MDT progress audit

1. Net progress remains positive. The joined ownership wave completed a clean
   warnings-as-errors Debug rebuild of all 737 affected actions with eight
   workers. Six of the seven semantic, HIR, artifact, cache, coverage, and
   line-directive boundary tests passed immediately; only the relocation-cache
   assertion exposed a new source-provenance canonicalization gap.
2. Top-level SystemVerilog DPI declarations and covergroup instances now carry
   explicit semantic owner scopes. Frontend grouping assigns included-file
   declarations to their compilation unit or declaring scope, the linker
   projects and relocates them by that ownership, and validators reject absent,
   cross-language, or owner-kind-inconsistent scope identities.
3. Direct, cached, object, and mapped-library regressions now compile a shared
   header containing global DPI plus package, class, and module covergroups.
   They verify header provenance and owner retention after each persistence
   path, including selected-library projection and link relocation.
4. The cache relocation failure was traced to preprocessor include ancestry:
   absolute producer paths were embedded inside expansion descriptions rather
   than stored as standalone source names. The active repair relocates bounded
   embedded references, relocates semantic expansion records, and removes the
   redundant expansion-description copy from HIR source tokens so the semantic
   origin graph remains the single authority. Its focused rebuild is running.
5. The shortest remaining dependency is to finish that rebuild, require the
   canonical bundle to contain no checkout path, and rerun the seven-test
   boundary. The broader 57-test lane and exact 69-selector inventory follow
   only after this boundary is green. The waived line gate and pre-Batch-190
   sanitizer lanes remain unrun, and all debugger sessions used for diagnosis
   exited.

## Batch 188A 2026-09-18 20:49 MDT progress audit

1. Net progress remains positive. The joined provenance, ownership, and HIR
   lowering wave completed a warnings-as-errors Debug rebuild of all 1,298
   affected actions with eight workers. Seven of the nine focused frontend,
   semantic, HIR, artifact, cache, coverage, assertion, line-directive, and
   library tests passed on the first boundary run.
2. Compiled-HIR validation now treats `SourceToken` and generated-text metadata
   as structural provenance: token kinds, source IDs, and generated-text enums
   are range checked, and malformed residual expansion data is rejected.
   Concurrent assertion metadata now carries source tokens instead of owning
   detached spellings, preserving origin identities through bundle round trips.
3. Direct `` `__FILE__ `` expansions and stringify/concatenation derivatives
   now retain generated-text provenance through preprocessing, parsing, HIR,
   persistence, relocation, and assertion-action lowering. The cache regression
   has advanced to an include-plus-macro ancestry loss caused by frontend token
   and source-span expansion stacks diverging at the preprocessing boundary.
4. Deferred immediate assertion lowering now captures action arguments before
   transferring execution into the reactive or postponed scheduler region,
   matching the prior runtime ordering. The assertion regression advanced past
   that case and now isolates unsupported HIR lowering for assertion-control
   system tasks in the concurrent-assertion fixture.
5. The shortest remaining dependencies are to make the finalized source span
   authoritative for include-plus-macro ancestry and port assertion-control
   task lowering to HIR. Those independent fixes are partitioned between two
   focused workers; the root will join them into one eight-worker rebuild and
   rerun the two failing tests, then the nine-test boundary. No GDB or LLDB
   process remains; the waived line gate and pre-Batch-190 sanitizer lanes
   remain unrun.

## Batch 188A 2026-09-18 21:38 MDT progress audit

1. Net progress remains positive. The source-span ancestry and cache-relocation
   boundary is green, including include-plus-macro provenance. The VHDL package
   constant repair completed an eight-worker semantic rebuild, and both the
   base semantic and SystemVerilog class-specialization tests pass.
2. Concurrent assertion controls and materialized checker processes now remain
   entirely in compiled HIR. Checker-instance records are retained as compile
   provenance rather than rejected as residual structure, while VPI publishes
   each process-owned assertion under a noncolliding `$assertion` child and
   preserves the source assertion name as an alias.
3. The SystemVerilog executable projection now distinguishes same-span sibling
   expressions by kind, text, type, decoded value, and generated-text metadata.
   This repairs synthetic gate-array terminal indices without reconstructing
   syntax; the transition regression requires four static slice writes and
   rejects the erroneous dynamic-index form.
4. The current lowering wave adds direct compiled-HIR support for `$system`,
   `$isunknown`, and the random functions, plus one captured packed-lvalue path
   for compound and prefix/postfix updates across delay and event controls.
   String static arrays now bypass scalar-string materialization and become
   fixed container objects usable by both memory-read and memory-write tasks.
5. VHDL resolver and imported package-type reconstruction is the final active
   worker in this wave. The shortest remaining dependency is to join that
   repair, run one warnings-as-errors Debug build with eight workers, and rerun
   the direct-HIR, assertion, specialization, transition, procedural, random,
   system-command, VPI, VHDL-overload, resolution, and file lanes. No GDB or
   LLDB process remains; the waived line gate and pre-Batch-190 sanitizer lanes
   remain unrun.

## Batch 188A 2026-09-18 22:25 MDT progress audit

1. Net progress remains positive. The joined compiled-HIR projection and
   lowering wave completed a clean warnings-as-errors Debug rebuild of all 121
   affected actions with eight workers. Four of the fourteen focused boundary
   tests passed: direct HIR lowering, call safe points, system command, and the
   core non-project CLI lane.
2. Generated and package-dependent unresolved names now have a guarded
   specialization-time constant fallback without folding writable runtime
   bindings. Direct call debug points cover signed conversions, system calls,
   unknown checks, and random calls. The fresh run confirmed the call-safe-point
   lane is green and advanced VPI, VHDL package specialization, and random
   lowering to narrower failures.
3. The remaining focused failures have been partitioned by ownership. Verilog
   resistive-switch lowering now accepts a legal literal MOS source without
   inventing bidirectional topology; VPI four-state parameter identities are
   normalized as canonical `svconst-v3` values; and imported VHDL simple type
   names are resolved through explicit or `.all` package context.
4. Process materialization now places concurrent drivers before lexical
   initial processes, restoring the time-zero continuous-driver transition.
   The transition regression confirms that ordering but exposed missing
   gate-array occurrence names in the HIR process projection. A separate
   concurrent-assertion fork-layout repair is active after VPI publication
   advanced into runtime validation.
5. The shortest remaining dependency is to join the active assertion repair
   with the strength, VPI, VHDL type, nested-selection assignment, and formatted
   display fixes, then rerun one eight-worker build and the focused boundary.
   No GDB or LLDB process remains; the waived line gate and pre-Batch-190
   sanitizer lanes remain unrun.

## Batch 188A 2026-09-18 23:13 MDT progress audit

1. Net progress remains positive. The combined direct-HIR wave completed a
   warnings-as-errors Debug build of all 979 actions with eight workers. The
   fourteen-test focused boundary still has four green lanes, while every
   remaining failure advanced to a narrower post-cutover semantic or lowering
   defect instead of an AST ownership fallback.
2. Transition-delay selection now discards the unselected minimum, typical,
   and maximum alternatives before time normalization, so the chosen delay no
   longer inherits representability diagnostics from inactive variants. HIR
   signal declarations also retain net delay, drive strength, charge strength,
   and charge decay for trireg runtime construction.
3. Repeated event controls now lower their compiled-HIR repeat count instead
   of collapsing assertion cycle delays to a single wait. Runtime signals
   recover the SystemVerilog event-variable marker from their linked HIR type,
   restoring named-event trigger routing for VPI callbacks.
4. Imported VHDL array-type adaptation now evaluates predefined type
   attributes through linked HIR declarations and retained dimensions. The
   completed VHDL wait repair also removes the empty trailing sensitivity wait
   when an explicit or transitively called wait already suspends the process.
5. The shortest remaining dependency is the active SystemVerilog statement
   repair for empty and residual block kinds. Once joined, the root will run a
   single eight-worker build and repeat the fourteen-test focused boundary,
   then repartition only the independently remaining failures. No GDB or LLDB
   process remains; the waived line gate and pre-Batch-190 sanitizer lanes
   remain unrun.

## Batch 188A 2026-09-19 00:09 MDT progress audit

1. Net progress remains positive. The joined HIR lowering wave completed a
   warnings-as-errors Debug rebuild of all 105 affected actions with eight
   workers. VPI callback routing and parameter specialization are now green;
   the focused boundary has five passing lanes and nine narrower failures.
2. Named-event runtime signals now initialize to a concrete zero state and
   mixed event-control lists retain per-term baselines and IEEE edge matching.
   Runtime loop lowering, `$srandom`, file-call debug points, and componentwise
   declaration-plus-continuous delay composition are present in the compiled
   HIR path, with the fresh failures identifying their remaining edge cases.
3. Imported VHDL callable aggregate actuals now carry the formal linked subtype
   into expression lowering, including residual bounds evaluated from HIR. A
   fresh statement-kind-zero failure isolates the remaining contextual path
   without reopening AST ownership.
4. The hydrated class regression was traced through the compiled callable body
   to `type_owner::access_t`. Named HIR type resolution now searches a unique
   class-owned typedef after package lookup, but the full class-type regression
   still needs one more specialization-time resolution repair.
5. The shortest remaining dependency is the active SystemVerilog statement
   and VHDL aggregate repair pair, while the root isolates class type,
   transition/resolution delay, VPI alias, and file-debug identity failures for
   the next combined build. No GDB or LLDB process remains; the waived line
   gate and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 01:09 MDT progress audit

1. Net progress remains positive despite one validator-churn iteration. The
   prior focused boundary advanced from seven to nine passing lanes, and the
   latest five-failure wave completed a clean warnings-as-errors Debug rebuild
   of all 110 affected actions with eight workers.
2. SystemVerilog procedural continuous-assignment drivers are now transferred
   from the HIR lowerer into the owning specialization, and anonymous process
   VPI objects remain direct instance children when a synthetic debug scope has
   the same path. HIR timing controls also emit debugger wait points before
   suspension, and object inspection reports the compiled-HIR bundle checksum
   for metadata-only units.
3. The first root-scoped HIR package/type validator restored the missing
   diagnostic families but was intentionally not accepted as complete: the
   focused run exposed false positives for built-in net and aggregate names,
   valid packed members, replication, and dynamic selections. The repair is
   active and is being narrowed to invalid retained-HIR states only.
4. VHDL overload resolution now matches package specification and body
   profiles by linked nominal type identity before anonymous layout fallback.
   The aggregate-call regression still fails after that repair, isolating a
   deeper contextual aggregate-actual typing edge without reopening AST
   ownership.
5. The shortest remaining dependency is to finish those two active validator
   and aggregate-context repairs, then repeat one eight-worker build and the
   fourteen-test boundary. No GDB or LLDB process remains; the waived line gate
   and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 02:06 MDT progress audit

1. Net progress remains positive. The joined validator, procedural-call, and
   VHDL residual wave completed a clean warnings-as-errors Debug rebuild of
   all 108 affected actions with eight workers. The fourteen-test boundary
   remains at nine passing lanes, but the validator failures now reach later
   package-selection and runtime-verification assertions.
2. Hydrated SystemVerilog type references can recover valid type identity from
   identical source provenance, and packed selection validation now honors
   declared coordinates such as `[7:4]`. The transition regression therefore
   reaches simulation with every expected value; its only mismatch is two
   unnecessary scalar UDP input-adapter processes.
3. Retained executable SystemVerilog calls no longer use residual return-value
   substitution. Fresh SimIR inspection found one earlier constant-selection
   classifier still hiding calls used as dynamic indices; that focused repair
   is active together with the package-selection residual.
4. VHDL file and TextIO state is now retained and lowered from HIR, and
   standard severity literals plus aggregate formal types have direct HIR
   handling. The unchanged statement-kind failures show that both predicates
   still have a narrower preflight or dispatch rejection, now under rebuilt
   debugger inspection.
5. The shortest remaining dependency is to join those three active residual
   repairs with direct scalar UDP input aliasing, then run one eight-worker
   build and repeat the focused boundary. No GDB or LLDB process remains; the
   waived line gate and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 03:04 MDT progress audit

1. Net progress remains positive. The joined package/context, callable, file,
   and overload wave completed a warnings-as-errors Debug rebuild of all 101
   resumed actions with eight workers after correcting three validator API
   mismatches against the live compiled-HIR types.
2. The five-test focused boundary has two green lanes: procedural assignments
   and transition delays. The three remaining lanes all advanced: package and
   context diagnostics reach generic legality, VHDL overload execution reaches
   package-constant validation, and file handling reaches a nested packed
   selection of a Verilog static-array element.
3. The compiled-HIR package/context validator now covers missing, malformed,
   ambiguous, cyclic, unevaluable, and range-invalid package/context states.
   Valid residual callable and overloaded-operator constants are being
   distinguished from genuinely invalid constant expressions without
   consulting parser storage.
4. VHDL unary and binary HIR expressions retain callable identity and dispatch
   through the existing overload resolver only when it selects a callable;
   intrinsic operators remain on their direct lowering path. File interface
   handles use reference semantics so close/open status changes copy back to
   the caller.
5. The shortest remaining dependency is to join generic-diagnostic legality,
   package-constant deferral, and nested static-array packed-write lowering in
   one eight-worker rebuild, then repeat the five-test boundary. Direct AST
   ownership checks remain clean across public elaboration and CheckedProject;
   no GDB or LLDB process remains, and the waived line gate and pre-Batch-190
   sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 04:05 MDT progress audit

1. Net progress remains positive. Two joined warnings-as-errors Debug
   rebuilds completed all 100 affected actions with eight workers. The
   fourteen-test boundary advanced to thirteen passing lanes; only
   `fsim.elaboration` remains, and its first missing nominal-type diagnostic
   has advanced from parameter defaults through direct assignments to callable
   arguments.
2. Nested packed-member offsets, VHDL null-array layout, and native wide
   container-write cache eligibility now pass their focused runtime lanes.
   HIR-only nominal parameter defaults and assignments now reject distinct
   enum and aggregate identities while preserving same-type casts and
   contextual patterns.
3. The live ownership audit found no structural frontend syntax owner in
   public elaboration, `CheckedProject`, `BuiltProject`, cache payloads, or
   `SpecializedHirUnit`; the only owning parser workspace remains compile
   local and is destroyed before elaboration. `.fsimdesign` correctly decodes
   its already-linked bundle directly because it is a post-link,
   post-specialization artifact and reconstructs no syntax.
4. The Change 19 audit found concrete closure work beyond the current
   behavioral regression: add one five-path diagnostic/runtime differential,
   real relocated 1/2/4/8-worker artifact-byte generation, direct bundle and
   mapped-library compiled-HIR schema rejection, and a durable AST ownership
   source gate. Performance baselines, clean Release/Debug qualification, and
   the replacement hosted matrix remain Change 20 work.
5. The shortest remaining dependency is the current cohesive nominal-type
   validator expansion covering callable arguments and returns, task copyout,
   nested patterns, equality, casts, and ports. Rebuild once, restore the full
   fourteen-test boundary, then implement the missing Change 19 gates before
   full qualification. No GDB or LLDB process remains; the waived line gate
   and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 05:38 MDT progress audit

1. Net progress remains positive, but repeated first-failure iterations in
   `fsim.elaboration` created avoidable churn. The active debugging strategy
   now evaluates and reports the complete wide-constant cluster in one pass,
   and the next compile/test boundary is deferred until the whole parameter
   handoff category is repaired.
2. A parser-independent arbitrary-width SystemVerilog constant evaluator now
   covers literals, four-state arithmetic and comparisons, selections,
   concatenation and streaming, array queries, casts, assignment patterns,
   package constants, and retained-HIR constant functions. The diagnostic
   sweep confirms the named-type cast, selected function assignments, enum
   values, and all local wide results together.
3. Untyped parameter defaults retain their self-determined widths by using
   the compiled `implicit` type marker, while explicitly typed defaults are
   converted through HIR type metadata. Wide localparams are evaluated for
   specialization reporting without becoming specialization-key actuals.
4. Batch debugger evidence isolated the remaining instance-boundary mismatch:
   the new evaluator and association canonical identity were correct, but the
   legacy scalar display path re-evaluated a 128-bit actual as zero. Canonical
   `svconst-v3` actuals now take precedence when materializing specialization
   metadata, and HIR evaluation is primary for parameter associations.
5. The shortest remaining dependency is to complete this single cohesive
   evaluator/handoff build, remove the temporary diagnostic sweep, and restore
   `fsim.elaboration` plus the fourteen-test boundary. Change 19 artifact and
   ownership gates and Change 20 clean qualification remain afterward. The
   batch GDB session exited and an exact process check was clean; the waived
   line gate and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 06:45 MDT progress audit

1. Net progress remains positive. One broad scalar-HIR category repair replaced
   the prior first-failure loop: exact scalar kind and decimal/time literal
   metadata now survive syntax destruction, serialization, linking,
   specialization, boundary construction, and final lowering.
2. HIR-native scalar specialization now covers package values, defaults,
   overrides, source-order dependencies, `real`, `shortreal`, `realtime`,
   `time`, and `chandle`. It emits the existing parameter diagnostics for
   invalid defaults and actuals while retaining deterministic specialization
   identities.
3. Scalar port compatibility and residual runtime lowering now diagnose type
   mismatches, nonfinite or out-of-range literals, and unsupported scalar
   operators. The complete `test_systemverilog_typed_constants` function is
   green, including inherited and overridden actuals, package values, time
   rounding, chandle identity, boundary rejection, and negative lowering cases.
4. Temporary constant, signal, runtime, and negative-case diagnostic sweeps have
   been removed in one cleanup edit. No worker remains active, completed worker
   records expose no deletion operation, and exact `gdb` and `lldb` process
   checks are clean.
5. The shortest remaining dependency is one eight-worker rebuild followed by
   the typed-constant and specialization functions, `fsim.elaboration`, and the
   fourteen-test boundary. Only after that boundary is green will work proceed
   to the missing Change 19 differential, determinism, schema-rejection, and
   ownership gates, then Change 20 clean qualification. The waived line gate
   and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 08:33 MDT progress audit

1. Net progress is positive after replacing single-assertion iterations with
   parallel seam ownership and grouped selector sweeps. Static-array callable
   slices, static-array port slices and selected-word adapters, and the full
   SystemVerilog type-parameter/type-operator matrix now pass together.
2. The compiled-HIR hierarchy path now owns container-port aliases, validates
   every invalid association in one elaboration, preserves slice direction and
   copyback, and constructs scalar adapters for selected container elements.
   Four-state defaults now use resolved HIR domains, including named enums and
   `time`, while reachable qualified package types participate in post-link
   validation.
3. VHDL file and TextIO diagnostics now cover object class, element subtype,
   writable targets, and implicit `SIDE` literals without syntax. The combined
   assertion/type selector has advanced past the file matrix to a later VHDL
   record-statement lowering seam.
4. The common packed-selection validator no longer treats HIR-encoded dynamic
   constructors or case-match bindings as ordinary packed nets. The affected
   container and `case matches` selectors now reach their actual residual
   statement lowering. One eight-worker warnings-as-errors Debug link passed;
   exact bounded debugger invocations exited and no debugger remains resident.
5. The shortest remaining dependency is the cohesive container/aggregate
   lowering seam: packed-structure element widths, dynamic/queue/associative
   whole patterns, queue mutation, static-slice ordering keys, and the two
   downstream container/case statements. After one grouped selector boundary,
   finish the remaining Change 19 artifact, determinism, schema, and ownership
   gates before Change 20 qualification. The waived line gate and
   pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 09:35 MDT progress audit

1. Net progress remains positive across the grouped elaboration seams. The
   compiled-HIR artifact codec and AST-lifetime governance gates are in place,
   VHDL nested record-member slice and index assignments now lower without
   syntax, and SystemVerilog static-container ordering predicates retain their
   iterator and index bindings.
2. The ordinary SystemVerilog container-method family is now routed through a
   shared HIR lowering path: runtime size queries, reductions, locators,
   ordering, element selection, and queue mutation no longer depend on the AST
   method nodes. The main container selector has advanced through its initial
   dynamic-array assertions to signed static-pattern key normalization.
3. All previously active seam workers have completed and are no longer being
   reused; the remaining work proceeds in the primary agent as requested. One
   eight-worker warnings-as-errors Debug link passed, every debugger invocation
   remained timeout-bounded, and exact `gdb` and `lldb` process checks were
   clean.
4. The shortest remaining dependency is to close the container selector as one
   semantic category, then fix the shared `case matches` packed-layout seam and
   the remaining VHDL aggregate/dynamic-slice/composite selectors before a
   single full elaboration inventory.
5. Change 19 closure still requires the grouped differential, corruption,
   determinism, compatibility, and AST-lifetime suites. Change 20 clean Debug
   and Release qualification, performance comparison, hosted Linux/Windows
   matrix and final commit/push remain afterward. This checkpoint's earlier
   conditional-tag expectation is superseded: Batch 188A creates no release
   tag.
   The waived line gate and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 10:37 MDT progress audit

1. Net progress remains positive after replacing the container first-failure
   loop with one diagnostic-recovery sweep. The complete SystemVerilog
   container selector and static-slice ordering selector are now green,
   including invalid methods, queries, patterns, slices, and port bindings.
2. The shared HIR recovery seam now retains a typed zero placeholder after an
   expected diagnostic, allowing one elaboration to collect all independent
   failures without a generic statement abort. Iterator/index predicates,
   associative indices, constructor/delete legality, memory transfer, and
   read-only slice copyout use their specific diagnostic families.
3. The VHDL nested record-member slice/index repair and the compiled-bundle
   codec and AST-lifetime governance gates remain integrated. All workers are
   completed and will not be reused; the remaining work proceeds in the
   primary agent. Exact `gdb` and `lldb` process checks remain clean.
4. The shortest remaining dependency is the tagged-union packed-member layout
   used by `case matches`. It will be repaired and validated with the already
   green container selectors in one boundary, followed by a grouped VHDL
   aggregate, dynamic-slice, and recursive-composite layout wave.
5. Change 19 still requires the full differential, corruption, determinism,
   compatibility, and AST-lifetime boundary. Change 20 clean Release and
   Debug qualification, performance comparison, hosted Linux/Windows matrix,
   final commit/push remain afterward. This checkpoint's earlier
   conditional-tag expectation is superseded: Batch 188A creates no release
   tag. The
   waived line gate and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 11:37 MDT progress audit

1. Net progress remains positive after assigning independent VHDL composite
   and aggregate-diagnostic seams to focused workers and retaining the
   SystemVerilog constant evaluator on the primary thread. The case-matches
   selector is now fully green across folded functions, pattern bindings,
   guards, tagged patterns, lazy conditionals, and wide conditional sizing.
2. Recursive VHDL composite defaults and nested enumeration aggregates now use
   flattened HIR record widths. The recursive-composite and dynamic-slice
   selectors are green, and object layout validation collects unconstrained,
   recursive, and unresolved composite-type diagnostics without syntax.
3. HIR statement-list lowering now continues after independent failures, so
   the invalid aggregate matrix emits all expected `VHAGG` diagnostics in one
   pass. Its remaining malformed-metadata regression is being converted from
   a post-parse AST mutation to a direct compiled-HIR mutation.
4. A broad elaboration run advanced to the package-specialization selector and
   exposed packed member slices being classified as unpacked-array slices. The
   classifier now requires an actual HIR container binding; this repair and
   the completed composite wave are intentionally awaiting one joined
   eight-worker build.
5. The shortest remaining dependency is to finish the AST-free malformed-HIR
   fixture, then run the case, container, aggregate, dynamic-slice, recursive-
   composite, and package-specialization selectors as one boundary before the
   next full elaboration inventory. Change 19 artifact/determinism/compatibility
   closure and Change 20 clean qualification remain afterward. Exact debugger
   process checks are clean; the waived line gate and pre-Batch-190 sanitizer
   lanes remain unrun.

## Batch 188A 2026-09-19 12:38 MDT progress audit

1. Net progress remains positive across one joined diagnostic and lowering
   wave. A 432-step, eight-worker Debug elaboration rebuild passed, and the
   complete case-inside selector is now green with its malformed cases driven
   by normalized `CompiledDesign` HIR rather than post-parse AST mutation.
2. SystemVerilog HIR now preserves `ref`, `const ref`, and `ref static`
   callable-formal qualifiers. Function/task association and streaming
   diagnostics consume those retained records directly, and the focused
   selectors advance past their former diagnostic failures to executable HIR
   lowering seams.
3. VHDL signal-attribute diagnostics and current-process driving checks are
   HIR-native. The configuration scope validator has also been ported to HIR:
   it checks static generate indices, duplicate sibling scopes, and retained
   instance/generate occurrences before hierarchy mutation. Its malformed
   scope regression now mutates normalized compiled HIR directly.
4. The remaining active lowering seam is grouped rather than assertion-sized:
   wide streaming, fixed-array callable returns, process/task statements, and
   VHDL-standard propagation are being closed together before one shared
   rebuild. The exact `gdb` and `lldb-22` process audit is clean.
5. The shortest remaining dependency is that joined eight-worker build and
   selector boundary, followed by a fresh full elaboration inventory. Change
   19 differential, corruption, determinism, compatibility, and AST-lifetime
   closure and Change 20 clean Debug/Release qualification, measurements,
   hosted matrix, and commit/push remain. Batch 188A creates no release tag;
   its replacement hosted matrix must be monitored and in-scope CI failures
   corrected before closure. The waived line gate and pre-Batch-190 sanitizer
   lanes remain unrun.

## Batch 188A 2026-09-19 13:37 MDT progress audit

1. Net progress remains positive across the joined callable, process-control,
   and VHDL expression/configuration wave. The last complete eight-worker
   Debug elaboration rebuild passed, fixed-array function returns are fully
   green, and nonstatic container-function lowering has advanced to four
   shared-profile failures rather than assertion-by-assertion repairs.
2. SystemVerilog function results now retain container working storage through
   automatic frames, recursive calls, conditional selection, copyout, and
   debug metadata. The remaining associative failures were traced to one
   common reconstruction bug: a retained `int` index profile was lowered as
   one bit. The common HIR type seam now restores the 32-bit builtin profile.
3. The process worker completed a cohesive HIR-native repair for malformed
   assignment control, two-state coercion, scoped debug locals, duplicate
   declarations, and same-language child boundary drivers. Its final VHDL
   signal-attribute call-point edit is awaiting the joined build. Exact `gdb`
   and `lldb-22` process audits are clean.
4. The shortest remaining dependency is to receive the VHDL worker handoff,
   finish the one dynamic-container conditional case, and run one coordinated
   eight-worker build across all three seams. The focused callable, process,
   selection, expression, and configuration selectors will then run as one
   boundary before a fresh complete elaboration inventory.
5. Change 19 differential, corruption, determinism, compatibility, and
   AST-lifetime closure and Change 20 clean Debug/Release qualification,
   measurements, final commit/push, and hosted Linux/Windows correction loop
   remain. Batch 188A creates no release tag. The waived line gate and the
   pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 14:37 MDT progress audit

1. Net progress remains positive across three coordinated seam-closure
   waves. The warnings-as-errors Debug elaboration target has rebuilt cleanly
   after each joined handoff, and the complete case/expression and
   SystemVerilog task selectors are now green. Function, process, selection,
   and VHDL configuration selectors have each advanced through several later
   regression groups rather than cycling on their prior assertions.
2. Direct HIR lowering now covers mixed signedness, packed query functions,
   nonstatic container calls and indexing, imported and qualified package
   tasks, transitive callable sensitivity, mutable strings, named-event
   aliases, malformed event expressions, wildcard event dependencies, and
   task-suspension restrictions. The former AST diagnostics for container
   conditional/equality/mutation profiles are emitted from retained origins.
3. VHDL configuration validation now accumulates invalid clause matrices,
   distinguishes component generic/port maps, missing entities, architecture
   selection, and recursive hierarchy cycles from retained HIR. Exact `gdb`
   and `lldb-22` process audits remain clean, and no stale debugger is running.
4. The shortest remaining dependency is the current joined build for four
   newly exposed seams: nonstatic function return/argument profiles,
   oversize strings, VHDL sequential loops, and recursive configuration
   classification. After those focused selectors pass, run a fresh complete
   elaboration inventory and close any remaining common HIR seams in batches.
5. Change 19 differential, corruption, determinism, compatibility, and
   AST-lifetime closure and Change 20 clean Debug/Release qualification,
   representative measurements, one commit/push, and the hosted Linux/Windows
   monitor-and-correct loop remain. Batch 188A creates no release tag. The
   waived line gate and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-19 15:37 MDT progress audit

1. Net progress remains positive across the compiled-HIR foundation and three
   parallel VHDL seams. The semantic, language-HIR, cache, object, library,
   design-artifact, deterministic-artifact, compatibility, schema, source
   manifest, retained-profile, and AST-lifetime gates are green. The complete
   SystemVerilog function-lowering selector is also green.
2. Generate specialization now evaluates VHDL enumeration generic defaults and
   selector-typed enumeration choices from retained HIR, diagnoses overlapping
   and invalid choices, preserves logic-literal references, and publishes
   generated-scope aliases. The generate selector has advanced to a later
   generated VHDL callable/statement-lowering seam.
3. Sequential loop lowering now executes residual nonconstant loops through HIR
   and restores orphan and labeled loop-control diagnostics, including jumps to
   labeled outer VHDL loops. Protected-object type linking is repaired, and the
   protected/reflection worker has restored a warnings-clean shared build while
   continuing the missing executable HIR paths.
4. The application artifact failure is independently localized to real-family
   assignment conversion: a 64-bit real value assigned to a shortreal target is
   generically truncated instead of converted, leaving the immediate comparison
   with zero. Exact debugger process checks are clean after the bounded trace.
5. The shortest remaining dependency is the joined loop and generate selector
   boundary, followed by protected/reflection completion and a fresh full
   elaboration inventory. Resource portability and diagnostic-catalog closure,
   remaining Change 19 differential gates, and Change 20 clean Debug/Release
   qualification, measurements, one commit/push, and hosted Linux/Windows
   monitor-and-correct work remain. Batch 188A creates no release tag; the
   waived line gate and pre-Batch-190 sanitizer lanes remain unrun.

## Batch 188A 2026-09-21 clean-context restart checkpoint

1. Live repository state is authoritative. The branch is `codex/v3`; local
   `HEAD` and `origin/codex/v3` are both
   `a867847cec10ac831e10674f7dc9de17ba294916`. The complete Batch 188A
   implementation remains intentionally uncommitted in the large dirty and
   untracked worktree. Do not reset, clean, discard, or split that worktree.
   Preserve the user-owned `phase.fst` file.
2. The latest cohesive optimization wave is present in the worktree:
   - `CompiledDesign` now has non-owning declaration indexes for both
     SystemVerilog and VHDL scopes and a case-correct VHDL package-member
     index covering declarations, enumeration literals, and physical units.
   - The one `CompiledDesignResolver` consumes those indexes and retains its
     complete-scan fallback while indexes are stale. No duplicate VHDL or
     SystemVerilog resolver was introduced.
   - `HierarchyBuilder` reuses successful SystemVerilog resolution-function
     registration per compiled unit and records that reuse in hierarchy
     checkpoints so rollback removes both the inserted resolution names and
     their registration marker.
   - `SpecializedHirUnit` uses direct dense record indexes on the normal
     immutable path and now falls back to the complete `CompiledDesign`
     lookup whenever a mutable test or tool invalidates those indexes.
   - The shared design-artifact codec reserves a bounded structural capacity
     estimate, capped at 8 MiB, for in-memory serialization. Encoded bytes,
     stream/checksum writers, and schemas are unchanged.
   - `tests/semantic/compiled_design_test.cpp` covers VHDL basic-name folding,
     extended-name case sensitivity, enumeration/physical package members,
     stale-index fallback and refresh, copy/move `string_view` lifetime, and
     specialized direct/fallback lookup.
3. Release compilation evidence after those edits is current. This command
   completed successfully with eight workers and linked `fsim` plus all named
   tests:

   ```text
   cmake --build build/ci-linux-release --target fsim fsim_semantic_tests fsim_design_artifact_tests fsim_application_tests fsim_application_tests_1 fsim_application_tests_2 fsim_application_tests_3 --parallel 8
   ```

   A later bounded rebuild of `fsim` and `fsim_semantic_tests` also completed
   after the stale-index repair. Therefore
   `build/ci-linux-release/fsim` contains every current source edit.
4. Focused validation after the joined build produced:
   - `fsim.artifact.design`: PASS.
   - `fsim.application.compiled_hir_cache`: PASS.
   - `fsim.semantic`: initially exposed the stale-index association bug, then
     PASS after the complete-HIR fallback repair.
   The artifact/cache pair has not yet been rerun after the final semantic-only
   repair. The complete Change 19 evidence suite and clean Change 20 suites
   must be rerun after all remaining implementation edits.
5. The newest official seven-sample performance evidence is
   `/tmp/fsim-batch188a-perf.yrx9rjgd/qualification.json`. Pure SystemVerilog
   passes at a 0.420949 total-wall ratio and mixed-language peak RSS passes at
   0.394216. Mixed compile is 0.956348 of baseline, but mixed elaboration is
   1.240071 and mixed compile-plus-elaborate is 1.099552, so the required 1.05
   wall-time gate remains open. The immediately preceding evidence is
   `/tmp/fsim-batch188a-perf.z8kk3kjk` at 1.096611; the serializer reservation
   did not produce a measurable gate improvement.
6. The current 50-run native profile is
   `/tmp/fsim-batch188a-mixed-current.perf.data`. Its main leaf costs include
   `memcmp` 3.62%, hardware SHA-256 3.43%, codec `Writer::raw` 3.10%, allocator
   and movement costs, `SpecializedHirUnit::find_declaration` 2.55%, codec
   `Reader::read_exact` 2.37%, and
   `CompiledDesign::lookup_indexes_current` 1.64%. The last item is the
   shortest safe next performance dependency: validated elaboration already
   proves the compiled design immutable, so carry that proof into
   `SpecializedHirUnit` and skip repeated revision/owner checks only for units
   created from `ValidatedCompiledDesign`. Keep the checked fallback for the
   public unvalidated factory. Ensure token creation rejects stale indexes or
   otherwise proves their current revisions before enabling the fast path.
7. A live requirement audit found one substantive Change 6 gap. The current
   `compiled_design_normalization.cpp` folds only dependency-free host-sized
   integer/boolean unary and binary expressions. The planned dependency-
   independent name/type/range/default, pure constant-call, aggregate, and ROM
   initializer folding is not proved and appears absent; the current
   normalization test covers only scalar unary/binary cases. This must be
   implemented and tested rather than marking Change 6 complete.
8. Changes 2-5 and 7-18 have strong implementation evidence, and Change 19's
   differential, corruption, determinism, schema rejection, and AST-lifetime
   tests/gates exist. Closure is not yet proved after the latest edits. The
   authoritative plan still marks only Change 1 complete and must not be
   updated to claim later changes until their current evidence passes.
9. After the validated-specialization fast path, rebuild once with eight
   workers, rerun `fsim.semantic`, `fsim.artifact.design`, and
   `fsim.application.compiled_hir_cache`, then rerun the seven-sample
   qualifier. If the mixed wall gate remains above 1.05, profile the new binary
   and attack a measured aggregate seam rather than resuming assertion-sized
   churn. Then close the missing normalization categories and run the complete
   Change 19 boundary.
10. Change 20 remains fully open: clean warnings-as-errors Release followed by
    Debug builds and complete suites with eight workers, final representative
    measurements, diff/source-manifest audit, one commit and push, and the
    four-lane LLVM-only hosted Linux/Windows monitor-and-correct loop. Do not
    run sanitizers before Batch 190 and do not create a release tag for Batch
    188A. Keep exactly one top-level build, test, debugger, or profiler active
    at a time. The final process audit at this checkpoint found no build, test,
    debugger, or profiler process, and no worker agent remains active.

## Batch 188A 2026-09-21 15:24 MDT clean-context handoff

1. This handoff supplements the immediately preceding restart checkpoint; all
   repository, validation, performance, and no-go state recorded there remains
   current. Live `codex/v3` `HEAD` and `origin/codex/v3` are still
   `a867847cec10ac831e10674f7dc9de17ba294916`. The large dirty and untracked
   worktree is intentional Batch 188A work. Preserve it in full, including
   `phase.fst`; do not reset, clean, discard, or split it.
2. Three workers were used only for bounded discovery after the preceding
   checkpoint. They made no edits and started no build, test, debugger, or
   profiler. The normalization and specialization workers completed; the
   read-only performance worker was interrupted for this clean restart. No
   worker remains active. Root also made no implementation edit after the
   preceding checkpoint; this handoff text is the only new repository change.
3. The validated-specialization fast path is ready as one cohesive patch.
   `ValidatedCompiledDesign` currently proves only `design.valid()`, while all
   six `SpecializedHirUnit::find_*` paths repeat
   `lookup_indexes_current()`. Make validation reject stale lookup indexes,
   carry a private validated-index fast-path flag through
   `SpecializedHirUnitFactory`, `working_specialization()`, construction, and
   the two derived-specialization methods, and skip the revision/owner check
   only when that flag is set. Keep complete-HIR fallback for public
   unvalidated factories. Extend
   `test_compiled_design_indexed_lookup_contract()` with validated lookup,
   stale unvalidated fallback, and stale validation rejection cases.
4. Change 6 requires a larger cohesive normalization wave, not a scalar-only
   patch. `fold_expression()` presently covers only dependency-free host-sized
   integer/boolean unary and binary expressions. Extend dependency traversal
   through aggregate/assignment-pattern and call associations; substitute
   dependency-free constant names from declaration initializers while
   preserving the consuming expression identity and origin; materialize folded
   bounds in SystemVerilog `PackedRange` and VHDL `RangeConstraint` records;
   mark structural aggregates folded only when all choices and values fold;
   evaluate only explicitly pure, defined, side-effect-free constant callables
   in the supported scalar subset; and apply the contract to defaults,
   declaration/type/member initializers, and ROM aggregates. Retain parameter,
   generic, package, hierarchy, recursive, impure, unresolved, or otherwise
   dependent constructs as residual.
5. Expand `test_compiled_design_normalization()` with positive and residual
   cases for constant names, types/ranges/defaults, pure calls, structural
   aggregates, and ROM initializers, plus source/origin preservation and
   second-pass determinism. Existing tests cover scalar arithmetic/boolean
   folding, overflow and divide-by-zero retention, hierarchy dependencies,
   determinism, and origins only. The existing application ROM case proves
   only scalar `FEATURE_ROM_WORD_0 = 2 + 3`, not aggregate ROM folding.
6. Resume by assigning the specialization and normalization waves to separate
   workers. Root should inspect the existing writer/profile evidence and own
   any independent codec optimization. Join all edits before one eight-worker
   build; never run overlapping top-level builds. First rebuild `fsim` and
   `fsim_semantic_tests`, run `fsim.semantic`, then rebuild/run
   `fsim.artifact.design` and `fsim.application.compiled_hir_cache` if their
   sources changed. Rerun the seven-sample performance qualifier only after
   the joined focused boundary is green.
7. If mixed compile-plus-elaborate remains above 1.05, collect a new profile
   from the joined executable and choose a measured aggregate seam. Do not
   infer a win from the earlier capacity reservation. The last authoritative
   profile and qualifier paths remain
   `/tmp/fsim-batch188a-mixed-current.perf.data` and
   `/tmp/fsim-batch188a-perf.yrx9rjgd/qualification.json` respectively; the
   baseline executable remains
   `/tmp/fsim-batch188a-baseline.0kq0Um/build/ci-linux-release/fsim`.
8. After normalization and the performance gate close, run the complete Change
   19 boundary, then Change 20 clean Release and Debug warnings-as-errors builds
   and full suites with eight workers, measurements, diff/source-manifest
   audit, one commit and push, and the four LLVM-only hosted Linux/Windows
   monitor-and-correct loop. Do not run sanitizers before Batch 190 and do not
   create a Batch 188A release tag.
9. The complete Change 19 evidence boundary has one direct CTest selector,
   `batch188a-change19`, over an explicit 29-test inventory. Configuration
   fails if the inventory count changes, contains a duplicate, or names an
   unregistered test; the selector adds a label to the existing tests and does
   not introduce a wrapper or duplicate their execution. After the joined
   build, run the exact boundary with eight CTest workers:

   ```text
   ctest --test-dir build/ci-linux-release --output-on-failure --parallel 8 -L '^batch188a-change19$'
   ```

   This handoff records the selector registration only; that boundary has not
   been executed after the latest implementation edits.

## Batch 188A 2026-09-21 closure qualification checkpoint

1. This checkpoint supersedes the open work in the preceding handoffs. Changes
   2-19 are implemented and marked complete in the authoritative plan. The
   compiled-design boundary owns semantic state plus the two language HIRs;
   parser workspaces are destroyed before checked compilation returns, and
   elaboration, specialization, linking, lowering, artifacts, caches, and
   mapped libraries consume compiled HIR rather than reconstructing it from
   syntax. The consolidated `CompiledDesignResolver` remains the only shared
   resolver for both languages.
2. Change 19 closure is green: the independent elaboration inventory passes
   69/69 cases, and the exact `batch188a-change19` selector passes 29/29.
   Differential direct/object/cache/library/design paths, corruption and old-
   format rejection, relocation and worker-count determinism, schema checks,
   and AST-lifetime governance are all represented. The required formats are
   `.fsimobj` 8, mapped library 6, portable schema 15, compiled-HIR bundle 1,
   SystemVerilog HIR 8, VHDL HIR 5, and `.fsimdesign` 13; semantic, DesignIR,
   and runtime-state schemas remain unchanged.
3. The last correctness seam was an unsound construction-time VHDL signal
   specialization in LLVM setup. Removing that `ReadSignal`-to-constant
   rewrite restored mutable mixed-language signal behavior without changing
   the specialization boundary. Focused LLVM and mixed-language tests pass,
   and both warnings-as-errors LLVM-enabled configurations built successfully
   with eight workers.
4. Performance work used Callgrind and `perf`, not inference. Baseline/current
   instruction counts were 68,788,912/71,381,059 for VHDL compilation,
   274,467,662/252,206,248 for SystemVerilog compilation, and
   955,334,421/871,832,393 for elaboration. `perf stat` then localized the
   remaining wall delta to extra minor faults and encode-side ownership churn.
   Replacing temporary owning HIR codec states with non-owning spans reduced
   the compile profiles to 71,145,500 and 251,278,662 instructions and retained
   byte-identical SV and VHDL compiled-HIR payload hashes.
5. The final alternating seven-sample evidence is
   `/tmp/fsim-batch188a-perf.lu1ar5o2/qualification.json`. Relative to
   `a867847c`, pure-SystemVerilog compile/elaborate total wall is 0.961110 and
   peak RSS is 0.954706; mixed-language total wall is 1.037303 and peak RSS is
   0.991303. Reported mixed subphases are 1.089256 compile and 1.006031
   elaborate. Both gated totals are below 1.05, and baseline/current simulation
   output equivalence passes in every sample.
6. Current full-suite qualification covers all 423 registered tests in both
   LLVM-enabled warnings-as-errors configurations. Release passed 422 tests in
   the complete parallel run and its sole source-anchor inventory miss passed
   immediately after the reviewed anchor update. Debug likewise passed 422 in
   the complete run and its sole digest audit miss passed immediately after
   synchronizing the reviewed inventory digest. All runtime, LLVM, compiled-
   HIR, artifact, AST-lifetime, and Change 19 tests passed in the complete
   runs. No sanitizer was run, as required before Batch 190.
7. Change 20 remains open only for the final diff/process/governance audit, the
   single Batch 188A commit and push, and monitoring/correcting the four hosted
   LLVM-only lanes: Linux Debug/Release and Windows Debug/Release. Do not create
   a Batch 188A release tag. Preserve `phase.fst`, and do not clean or reset the
   intentional accumulated worktree.
