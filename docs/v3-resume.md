<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 restart handoff

Read [implementation_plan_v3.md](implementation_plan_v3.md) first. It is the
authoritative v3 batch and status record. Preserve the completed v1 and v2
history in their existing plan and resume documents.

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
5. Release-closing Change 20s additionally own release artifacts, the exact
   release record, annotated tag, and publication after their required local
   lanes are green. They do not add sanitizer or hosted-CI runs unless the
   closing batch is also Batch 180 or Batch 190.
6. Batch 180 and Batch 190 are the scheduled tenth-batch sanitizer and
   non-documentation hosted-CI boundaries. Batch 178 is neither boundary: do
   not configure sanitizers or inspect hosted CI during this batch.
7. Use at least eight local build workers and retain 120-minute qualification
   timeouts. Avoid formatting-only header changes; make only necessary semantic
   header edits and keep formatter churn confined to changed implementation
   sources where practical.
8. Create v3 manifest, native ABI, object, design, checkpoint, cache, and
   plugin schemas directly. Reject all versioned v2 inputs deterministically.
   Do not implement compatibility readers, migrations, fallbacks, or dual
   writes. Preserve every existing HDL language profile.
9. The two privately supplied language standards are read-only references.
   Never copy, commit, package, quote, or log their contents, locations, or
   file hashes. Repository inventories may record standard identifiers, clause
   numbers, independently written summaries, and test owners only. Author all
   tests and examples independently.
10. Batch 178 establishes the language-neutral code-coverage foundation only.
    Condition, expression, toggle, and FSM coverage remain Batch 179 work;
    unified database/API/report work remains Batch 180 work.
11. Changes 1-4 register the obligation matrix, define coverage point/metric/
    run/result types, canonicalize relocation-independent source identity, and
    derive stable point IDs from language, construct, span, and source identity.
12. Changes 5-8 discover executable Verilog/SystemVerilog and VHDL statement
    points, model decisions and individually addressable branch arms, and
    derive covered, partial, and uncovered line states.
13. Changes 9-13 attach inventories to elaborated instances, add a validated
    SimIR coverage-hit operation, implement saturating interpreter counters,
    lower equivalent LLVM O0-O3 counters, and preserve identity/hits in the
    Debug engine.
14. Changes 14-18 exclude non-executable and statically removed constructs,
    assign stable instance identities, retain both instance and source-union
    results, add opt-in manifest/CLI controls with no default overhead, and
    include coverage identity in v3 object/design/native-cache keys.
15. Change 19 proves Verilog/SystemVerilog/VHDL engine and aggregation
    equivalence. Change 20 runs the standard batch closeout and freezes the
    foundation inventory.
16. Preserve the public v3 coverage contract: [coverage],
    --code-coverage, --coverage-metrics, and --coverage-db are opt-in; one
    versioned .fsimcov database eventually carries distinct code,
    SystemVerilog functional, and PSL namespaces. Do not prematurely implement
    Batch 179 or 180 surfaces.
17. Begin with Change 1 only. Register one clause-neutral coverage obligation
    and ownership matrix whose rows have stable independent identifiers,
    independently worded obligations, exact Batch 178 change ownership,
    implementation/test/diagnostic owners, profile and engine scope, closure
    evidence, and explicit active status. Add a bounded validator and
    deterministic normalized identity following existing inventory patterns.
18. Change 1 validation must be focused and warning-clean in the existing
    exact-LLVM Debug tree. Exercise the new inventory owner plus affected
    diagnostic/source-budget/inventory gates. Record exact commands, results,
    elapsed time, resource evidence when available, and the normalized matrix
    identity in both authoritative v3 documents.
19. After Change 1, preserve its intentionally dirty worktree and proceed one
    numbered change at a time. Do not reset, commit, push, run Release
    qualification, run sanitizers, or inspect hosted CI before Batch 178
    Change 20.
20. The immediate transition is to validate these two documentation files,
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
   `06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234`.
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
   `06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234`.
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
   `06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234`.
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
   `e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443`.
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
   `e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443`.
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
   `e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443`.
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
