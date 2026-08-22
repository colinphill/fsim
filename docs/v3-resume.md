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
5. Release-closing Change 20s additionally own their sanitizer runs, hosted
   Linux and Windows qualification, release artifacts, exact release record,
   annotated tag, and publication after all required lanes are green.
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
