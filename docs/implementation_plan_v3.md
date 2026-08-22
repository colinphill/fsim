<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v3 implementation plan

This is the authoritative batch and status record for fsim v3. Development
starts on branch codex/v3 from clean v2 checkpoint
22d5e2ba43b5e7711db6fe0b27d7e9e0d5345095.

## Governing contract

- Allocate exactly twenty implementation batches, Batches 178 through 197,
  with exactly twenty numbered changes in every batch.
- Changes 1 through 19 accumulate in one recoverable worktree with focused
  warnings-as-errors Debug builds and tests. Change 20 alone owns clean full
  Debug and Release qualification, documentation, one implementation commit,
  and one push.
- A release-closing Change 20 also owns sanitizers, hosted Linux and Windows
  qualification, release artifacts, and its annotated tag after every required
  lane is green.
- Batch 180 and Batch 190 remain tenth-batch sanitizer and hosted-CI
  boundaries.
- Local builds use at least eight workers. Local and hosted qualification
  commands retain 120-minute timeouts.
- Avoid formatting-only header changes. Make semantic header edits only when
  required and keep formatting work confined to changed implementation sources
  where practical.
- Define v3 schemas and native ABIs directly. Readers reject v2 manifests,
  objects, designs, checkpoints, caches, and plugins deterministically; v3
  adds no compatibility readers, migrations, fallbacks, or dual-write paths.
- Retain all existing HDL language profiles.
- The two privately supplied language standards are read-only references.
  Never copy, commit, package, quote, or log their contents, and never record
  their locations or file hashes in repository artifacts. Inventories may
  contain standard identifiers, clause numbers, independently written feature
  summaries, and test ownership. All tests and documentation examples must be
  independently authored.

## Release map

| Release | Closing batch | Included work |
| --- | ---: | --- |
| v3.0.0 | 188 | HDL coverage, IEEE TF/ACC PLI, VHDL-2019, and SystemVerilog-2023 |
| v3.1.0 | 191 | Deterministic and throughput-oriented parallel elaboration and simulation |
| v3.2.0 | 193 | Explicit compiled-LLVM performance lowering |
| v3.3.0 | 195 | LLDB native-plugin debugging on Linux and Windows |
| v3.4.0 | 197 | LLVM Release DEB, RPM, and Inno Setup packages |

## Public contracts

- Manifest additions: [coverage], [elaboration].jobs, [run].jobs,
  [run].parallel_policy, [build].lowering_profile, and [[plugins]].
- CLI additions: --code-coverage, --coverage-metrics, --coverage-db, --pli,
  --elab-jobs, --sim-jobs, --parallel-policy, and --lowering-profile.
- Add fsim coverage merge and fsim coverage report.
- Add fsim native-debug with --attach PID and --break-on-plugin-load, using
  LLDB exclusively.
- Extend fsim debug with LLDB handoff controls for HDL/native boundaries.
- Add VHDL 19/2019 and SystemVerilog 23/2023 standard spellings.
- Use one versioned .fsimcov database with separate code, SystemVerilog
  functional, and PSL coverage namespaces.

## v3.0.0

### Batch 178 - coverage identity, statement, line, and branch foundation

1. **Complete.** Register a clause-neutral coverage obligation and ownership
   matrix. The 17-row code_coverage_inventory.tsv ledger assigns Changes 2-18
   one-to-one across the language-neutral model, stable source/point identity,
   both HDL statement families, branch/line semantics, instance inventory,
   SimIR/interpreter/LLVM/Debug execution, exclusions, instance identity,
   aggregation, opt-in controls, and v3 artifact/cache identity. Every row is
   active and binds IEEE1076/1364/1800, all thirteen retained HDL profiles,
   independently written obligations, exact implementation and
   positive/negative/engine/aggregation/artifact evidence owners, diagnostics,
   and resource policy. The bounded registered validator freezes exact
   twenty-batch/twenty-change allocation, unique IDs/domains/change ownership,
   safe repository-relative paths, clause-neutral text, state transitions, and
   normalized SHA-256
   4099bbe2a9a8ba45021a6661411a4db1b735ac04803d2d2538510adfcc634cce.
   The eight-worker exact-LLVM Debug tree regenerates and builds 811/811
   targets without warning or error output. The first focused five-owner run
   passes four and correctly exposes the new validator missing from the source
   manifest; after regenerating the exact 1,503-path manifest, the inventory,
   diagnostic catalog, source-line budget, source-package manifest, and CTest
   command-uniqueness owners pass 5/5 in 4.78 seconds at 21,376 KiB peak RSS
   with zero swaps. The final post-documentation slice passes the same 5/5 in
   8.40 seconds at 21,168 KiB peak RSS with zero swaps; its retained log
   SHA-256 is
   0312441ab7caa11e53b1c5a13ed84a4e8009ab2d8ec646d6539ba141c04bbbdf.
   No Release, sanitizer, hosted-CI, commit, or implementation source action
   ran.
2. **Complete.** Define language-independent coverage point, metric, run, and
   result types. The internal code_coverage.hpp model gives run, point, and
   counter identities distinct strong types; freezes language-neutral
   statement, branch, and derived-line metric spellings; and defines explicit
   empty, uncovered, partial, covered, and excluded result states. A run owns
   a dense bounded point inventory, while point results retain typed metric,
   counter, hit, and status identity and metric results retain exact covered,
   partial, uncovered, excluded, and total counts without a synthetic combined
   score. Allocation-free validation rejects zero/mismatched identities,
   non-point counter metrics, counter/result ownership drift, incoherent hits
   and states, invalid component totals or derived status, duplicate/missing
   metric results, point/metric disagreement, and configured point/summary
   resource overrun through cataloged FSIM-COV-001. The focused test supplies
   positive, negative, typed-identity, empty-run, count/status, and resource
   witnesses. Its warnings-as-errors exact-LLVM Debug target builds with eight
   workers in 0.76 seconds at 143,036 KiB peak RSS and its 1/1 test passes in
   0.02 wall seconds at 20,140 KiB peak RSS, both with zero swaps. The final
   model/inventory/diagnostic/resource/source-budget/package/CTest-owner slice
   passes 7/7 in 6.78 CTest and wall seconds at 21,948 KiB peak
   RSS with zero swaps; its marker-clean retained log SHA-256 is
   173018316cd2a4a958775db1a2a0e8ace285f4fdadb0d9a46674267721061499.
   The ledger now has one preserved and sixteen active rows at normalized
   SHA-256 e5d223030d3e4f3f863118632cd385e19a4d8ba5f6004bdbac887b2e02e50639,
   and the exact 1,505-path source manifest SHA-256 is
   2f2ceafc9090288c5c82eeb77b91e6a62889818bbba907d1acf65154419942ac.
   Source canonicalization and point-ID generation remain Changes 3 and 4;
   no Release, sanitizer, hosted-CI, commit, or push action ran.
3. **Complete.** Canonicalize source paths and content identities independently
   of checkout location. The internal coverage_source_identity frontend seam
   reduces absolute or root-relative native paths to bounded, generic UTF-8,
   checkout-relative logical paths after lexical normalization. It rejects
   missing roots/sources, root/source incompatibility, checkout escape,
   root-only sources, embedded NULs, malformed UTF-8, paths beyond 1 MiB, and
   contents beyond 1 GiB through cataloged FSIM-COV-002. Each result retains
   the logical path, exact 256-bit raw-content SHA-256, and a length-framed,
   domain-separated composite SHA-256 over schema, logical path, exact content
   byte extent, and content digest; the checkout root is never an identity
   input. Independently authored
   tests prove absolute/relative spelling equivalence, lexical normalization,
   relocation between checkout roots, known content hashing, distinct-path and
   distinct-content separation, empty-content identity, traversal/encoding
   failures, and resource bounds. The warnings-as-errors exact-LLVM Debug
   target builds with eight workers in 1.12 seconds at 234,416 KiB peak RSS,
   and the focused test passes 1/1 in 0.02 wall seconds at 19,888 KiB peak RSS,
   both with zero swaps. The accumulated model/source/inventory/diagnostic/
   resource/source-budget/package/CTest-owner slice passes 8/8 in 6.69 seconds
   at 21,920 KiB peak RSS with zero swaps; its retained marker-clean log
   SHA-256 is
   383f87fb50f99a3d5343e5b38106fe9c8ececdb46cf63beb1d469c26b1ad25bf.
   The ledger now has two preserved and fifteen active rows at normalized
   SHA-256 6826c48a6a543760828083bc216b0a5e5487edc79b0feb89153c12c08226f282,
   and the exact 1,508-path source manifest SHA-256 is
   a45b640702c1c91e5a66f532b844dfc8c8c879f03661e0dc78e98c661d737331.
   Stable coverage-point ID generation remains Change 4; no Release,
   sanitizer, hosted-CI, commit, or push action ran.
4. **Complete.** Generate stable point IDs from language, construct kind, span,
   and source identity. The coverage_point_identity frontend seam freezes
   explicit Verilog, SystemVerilog, and VHDL language tags plus statement and
   true/false/case/default/implicit branch-arm construct tags. It validates the
   authenticated Change 3 source identity and a nonempty, ordered `[begin,end)`
   64-bit byte span wholly inside that source. A length-framed,
   domain-separated SHA-256 over the complete source digest, language,
   construct, and both offsets yields a stable 128-bit CodeCoveragePointId;
   no registry, allocation index, traversal order, or checkout root enters the
   calculation. FSIM-COV-003 owns invalid source, language, construct, span, or
   zero-output failures. Differential tests independently vary language,
   construct, begin/end offsets, logical path, content, relocation, and request
   order, authenticate the source byte extent, and freeze canonical point ID
   d8adc683731fb1114e3957ecec2b508c. The repaired warnings-as-errors
   exact-LLVM Debug source/point targets build with eight workers in 1.00
   second at 234,648 KiB peak RSS; the final identity pair passes 2/2 in 0.02
   wall seconds at 19,612 KiB peak RSS, both with zero swaps. The accumulated
   model/source/point/inventory/diagnostic/resource/source-budget/package/
   CTest-owner slice passes 9/9 in 6.46 wall seconds at 21,924 KiB peak RSS;
   its retained marker-clean log SHA-256 is
   65919f458b9457c3901fc8ec4c3b0eb0f32a837b8816f48ddedb9e5e06aa1e1c.
   The ledger now has three preserved and fourteen active rows at normalized
   SHA-256 39cf78cfd86524613722ad1b4b053afb35aba5bfe702644595c8c20027c890f2,
   and the exact 1,511-path source manifest SHA-256 is
   9ca48d38b31c35820c07d860b65de7569e4e752b326f7502249908536c9f1588.
   Statement discovery remains Change 5; no Release, sanitizer, hosted-CI,
   commit, or push action ran.
5. **Complete.** Discover executable Verilog/SystemVerilog statement points.
   The elaboration-stage verilog_coverage_points seam accepts a retained
   statement forest plus an exact physical-source-to-authenticated-identity
   map, recognizes the Verilog-2005 and SystemVerilog-2017 frontend families,
   and emits Change 4 statement identities in lexical depth-first order.
   Assignments, controls, calls, waits, assertions, process controls, output,
   and other executable statement kinds are points; lexical Block containers,
   explicit Null statements, declaration collections, and normalized for-loop
   update fragments are not. True/false bodies and every case alternative are
   traversed without allocating counters or changing execution. Discovery is
   transactional and rejects VHDL, invalid or ambiguous sources, unknown
   physical mappings, invalid spans, duplicate points, and source, statement,
   or nesting limit violations under FSIM-COV-004; allocation failures map to
   the same bounded resource result. Independently authored parsed Verilog and
   SystemVerilog tests cover nested blocks, declarations, if/case/loop bodies,
   relocation, root-order independence, direct Change 4 equivalence, and all
   rejection families. The final warnings-as-errors exact-LLVM Debug target
   builds with eight workers in 2.48 seconds at 1,018,916 KiB peak RSS and the
   focused discovery test passes 1/1 in 0.01 seconds at 19,896 KiB peak RSS,
   both with zero swaps; their retained log SHA-256 values are
   1687f738a5acea31a8c0d098eb04595699d5bf9f0f54450e3675a64c643c0a55 and
   8ff5b775878ed321529d83e997b2c790da074c76a7c8e45a20feba5bfde24164.
   The accumulated model/source/point/Verilog-discovery/inventory/diagnostic/
   resource/source-budget/package/CTest-owner slice passes 10/10 in 6.29 wall
   seconds at 21,944 KiB peak RSS; its retained marker-clean log SHA-256 is
   b8414e72c242d59cc1cdf5d505eba3eb3330a14706ca94ca4ceaf5439da4c22b.
   The ledger now has four preserved and thirteen active rows at normalized
   SHA-256 85ace6c335e8730f1b24281b16681048953c761a77e892809a4cef67e21a07eb,
   and the exact 1,514-path source manifest SHA-256 is
   da251e7c07d35af4e0fd9fc48fb6b935dac928d1e4cf1435d057a36503999c55.
   VHDL statement discovery remains Change 6; no Release, sanitizer,
   hosted-CI, commit, or push action ran.
6. **Complete.** Discover executable VHDL statement points. The independent
   vhdl_coverage_points elaboration seam accepts sequential process/subprogram
   or concurrent Statement forests, requires the VHDL language family plus one
   of all five retained VHDL revisions, and emits Change 4 VHDL statement IDs
   from exact authenticated source spans. Assignments, force/release, controls,
   loop exit/next, returns, procedure calls, assertions, waits, and reports are
   executable; Block containers, Null statements, declarations, and
   SystemVerilog-only statement forms are not. Nested bodies and case
   alternatives preserve lexical depth-first order. The transactional
   FSIM-COV-005 surface rejects non-VHDL input, unknown revisions, invalid or
   ambiguous source maps, invalid spans, duplicate points, and bounded source,
   statement, nesting, or allocation failures. An independently authored
   VHDL-87 corpus parses under VHDL-87/93/2000/2002/2008 and proves nine
   sequential plus two concurrent points, revision-independent IDs,
   relocation, direct Change 4 equivalence, declarations/null exclusions, and
   all rejection families. The final warnings-as-errors exact-LLVM Debug target
   builds with eight workers in 5.64 seconds at 1,019,792 KiB peak RSS and the
   focused VHDL discovery test passes 1/1 in 0.01 seconds at 19,936 KiB peak
   RSS, both with zero swaps; retained log SHA-256 values are
   19dd10bc04bd8c20a823a686c679a6371625fa1978364cd8a81bc554cd86099b and
   f644833ddd828967a5575f4f4d14f12911a9f5be5b5fdfc13e5f252a622f03bd.
   The accumulated model/source/point/both-language-discovery/inventory/
   diagnostic/resource/source-budget/package/CTest-owner slice passes 11/11
   in 6.53 wall seconds at 21,956 KiB peak RSS; its retained marker-clean log
   SHA-256 is
   8c0789e2ca6f28a71effd5dcd0828d8995af5778f4e188d5ef14102552f03eaa.
   The ledger now has five preserved and twelve active rows at normalized
   SHA-256 d59b49067d8762ca5cb30308ac396b8b1ccb50dd57975b85c52434ed48ad0fd1,
   and the exact 1,517-path source manifest SHA-256 is
   d1624162d805841a421d7b231f86dc9c7a8bcd1ea00bb1a1181bc72e609c1f29.
   Branch-arm modeling remains Change 7; no Release, sanitizer, hosted-CI,
   commit, or push action ran.
7. **Complete.** Model decision branches and individually addressable branch
   arms. The language-neutral coverage_branches elaboration seam walks retained
   Verilog, SystemVerilog, and VHDL statement forests in lexical depth-first
   order and emits canonical Change 4 identities for every explicit and
   implicit decision alternative. If decisions own true plus explicit-false or
   implicit-false arms; case decisions own every source alternative plus an
   implicit no-match arm when no default exists; conditional, repeat, and
   bounded loops own body and completion arms while unconditional forever/loop
   forms add none. Each point retains language, decision family, arm kind,
   source index, arm index, and authenticated exact source span. Transactional
   FSIM-COV-006 validation rejects invalid languages, malformed explicit arms,
   unauthenticated or ambiguous sources, invalid spans, duplicate identities,
   and bounded source, statement, arm, nesting, or allocation failures. The
   independently authored mixed-language corpus proves explicit/default/
   implicit arms, lexical order, relocation, language-separated identity,
   canonical Change 4 equivalence, unconditional-loop exclusion, and every
   rejection family. Integrating the merged compact RareVector statement model
   exposed obsolete reverse iterators in the Change 5-7 walkers; index-based
   reverse scheduling repaired all three without materializing snapshots or
   changing lexical output. The warning-clean three-target exact-LLVM Debug
   build completes with eight workers in 5.06 seconds at 351,164 KiB peak RSS,
   and all three discovery tests pass in 0.02 seconds at 20,132 KiB peak RSS,
   both with zero swaps. The final post-documentation 12-owner slice passes in
   6.65 seconds at 21,724 KiB peak RSS; its marker-clean retained log SHA-256 is
   63bafcf367a4c73df4ca01fe48f2f2780acb9968d4f1ed271a338b1544ccbc47.
   The ledger has six preserved and eleven active rows at normalized SHA-256
   ac76be94a2b5858f26264ddbc73ae66b0f8d3e6a4b0065b32d6e70e238ced658,
   and the exact 1,546-path source manifest SHA-256 is
   ff9fe4d8da06ad14d461df4b9bc89466dce7962cde17d2a063cc18d4dbefc5b1.
   Line-state derivation remains Change 8; no Release, sanitizer, hosted-CI,
   coverage implementation commit, or push action ran.
8. **Complete.** Derive covered, partial, and uncovered line states from
   statement points. Both language statement discoverers now retain each
   executable statement's one-based source start line without changing its
   stable point identity. The runtime-only coverage_line_state seam accepts
   aligned statement point/counter ownership and results, rejects branch or
   mismatched ownership, and groups points deterministically by source index
   and start line. A line is covered when every scored point is covered,
   uncovered when none is covered, partial when scored outcomes differ, and
   excluded only when all owned points are excluded; excluded points do not
   distort a line's scored state. The result includes exact per-line component
   counts and a derived line metric summary but no line point ID or counter.
   Two in-place deterministic sorts detect duplicate point identity and avoid
   maps or whole-container snapshots. FSIM-COV-007 transactionally rejects
   count, identity, source, line, point-result, status, and configured resource
   failures under ceilings of 65,536 sources, 1,048,576 statement points and
   derived lines, and 2^31 physical line numbers. Independently authored tests
   prove same-line partial scoring, covered/uncovered/excluded combinations,
   input-order independence, empty inventories, exact metric components,
   point/counter alignment, no branch inputs, status/hit coherence, and every
   resource family. The first three-target warnings-as-errors Debug build
   completes with eight workers in 7.88 seconds at 925,812 KiB peak RSS. A
   test-only corpus state leak made the first resource assertion observe an
   earlier deliberately invalid excluded hit; resetting the corpus repaired it
   without production changes. The final three tests pass in 0.02 seconds at
   20,148 KiB peak RSS, both with zero swaps. The final post-documentation
   13-owner slice passes in 6.65 seconds at 22,000 KiB peak RSS; its
   marker-clean retained log SHA-256 is
   f253ece171aec4764becbac09dee3fb4daa24e9a276a6fdf76722cf9987f1be4.
   The ledger has seven preserved and ten active rows at normalized SHA-256
   e3e371762cb273ce3805cfe0e8a11ee18bd94f879dd2691a8ac95dfeb254fc79,
   and the exact 1,549-path source manifest SHA-256 is
   56938d51923500842e81a6720175a29654df8099ce399d8340371aed8d64e9f9.
   Instance inventory attachment remains Change 9; no Release, sanitizer,
   hosted-CI, commit, or push action ran.
9. **Complete.** Attach coverage inventories to elaborated design instances.
   The language-neutral coverage_inventory seam projects ownership from the
   elaborated design's dense SpecializationInfo table, so every HDL occurrence
   is identified by its specialization ID, canonical instance path, language,
   primary source, and source dependencies rather than reconstructed process
   names. Attachment succeeds only when exactly one draft exists for every
   specialization. Every point must own a stable nonzero statement/branch ID,
   authenticated source in that specialization's source set, bounded nonempty
   span and physical line, and no duplicate instance-local identity. A bounded
   pointer index canonicalizes arbitrary draft order without copying point
   containers; only final retained point vectors are materialized and sorted
   by stable point identity. Dense design-global counters are then assigned in
   specialization/point order, retaining identical source point IDs with
   distinct counters across repeated hierarchy instances. The optional design
   inventory remains absent by default, is published only after complete
   validation, exposes const access, and survives copy/move design-state round
   trips; malformed stored owners, order, counters, totals, or sources are
   rejected before runtime construction. FSIM-COV-008 owns transactional
   failures and ceilings of 65,536 sources, 1,048,576 instances, 1,048,576
   total points, 2^31 physical line numbers, and the uint32 counter space.
   Independently authored tests prove arbitrary-order determinism, empty
   instance inventories, repeated-instance point IDs with distinct counters,
   all retained language families, source ownership, every malformed and
   resource family, transactional replacement, actual SystemVerilog hierarchy
   attachment, immutable access, and valid/corrupt state round trips. The
   public ownership-header fan-out required a 102-step eight-worker
   warnings-as-errors Debug rebuild in 2:02.02 at 2,471,232 KiB peak RSS with
   zero swaps. After eliminating the initial deep draft snapshot, the final
   incremental target rebuild passes in 6.75 seconds at 1,001,372 KiB peak RSS;
   its marker-clean retained log SHA-256 is
   7ec5b28487a6b74559dcb4cd76147da4e3a74cbe7e03aa4e872f46b4f644702a.
   The final post-documentation accumulated 14-owner slice passes in 6.56
   seconds at 21,920 KiB peak RSS with zero swaps; its marker-clean retained
   log SHA-256 is
   90e1fe422e274065afa5889b6dbbf44e1d4b05e2021780980a30412b4bda54f1.
   COVBASE-C02 through COVBASE-C09 are preserved and nine rows remain active at
   normalized SHA-256
   15d03a6bf5cfe44cb14c2460e7f87c6f47753a89dbf5f0b715626471c289a8ee;
   the exact 1,552-path source manifest SHA-256 is
   fbe9422409f3709c51ef39062deda6ecd4b5eda48bdfb4cec62b9e54c11b7a19.
   The SimIR coverage-hit operation remains Change 10; no Release, sanitizer,
   hosted-CI, clean-first, commit, or push action ran.
10. **Complete.** Add a validated SimIR coverage-hit operation. The appended
   CodeCoverageHit output alternative owns only a stable point ID, executable
   statement/branch metric, and dense uint32 counter; it cannot name a
   register, signal, container, object, scheduler target, or other mutable
   simulation state. A read-only validator checks every hit against the exact
   canonical point inventory for one elaborated instance, including nonzero
   identity, executable metric, sorted unique point ownership, contiguous
   instance counter ownership, design-global counter bounds, and duplicate
   instrumentation. Validation is transactional under FSIM-COV-009 and maps
   allocation, point-count, operation-count, and instruction-width failures to
   ResourceLimit. The operation appends after all existing output alternatives
   and participates in artifact expansion, LLVM structural validation, the
   native cache key, and an explicit host boundary; Changes 11 and 12 retain
   exclusive ownership of interpreter mutation and native counter lowering.
   Repeated hierarchy instances continue to share one immutable operation body:
   a compact instruction/counter override restores each instance's dense
   counter during validation, artifact/debug expansion, re-sharing, replacement,
   and copy-on-write materialization. Independently authored tests prove exact
   statement and branch ownership, every malformed/resource family, unrelated
   state immutability, duplicate rejection, stable operation indices, and
   counter-only sharing. The warnings-as-errors LLVM 22.1.8 Debug fsim build
   compiled all 480 affected elaboration, LLVM, artifact, application, and CLI
   steps with eight workers in 4:57.79 at 3,913,628 KiB peak RSS and zero swaps;
   the focused runtime test passes 1/1. COVBASE-C02 through COVBASE-C10 are
   preserved and eight rows remain active at normalized SHA-256
   6ceff8253c7e3d41e6d5fb6dd3812d1163e82eaf3b3676e1dc0a9f33f1c22855;
   the exact 1,555-path source manifest SHA-256 is
   af4e87a0951ad69343cb5d73f49df98d52f216c4e28487006e363748f5c1cab4.
   Saturating interpreter counter execution remains Change 11; no Release,
   sanitizer, hosted-CI, clean-first, commit, or push action ran.
11. **Complete.** Implement saturating interpreter counters with overflow
   reporting. A simulation-owned dense uint64 table is configured or restored
   before start and retains a same-sized byte flag table so the execution path
   allocates nothing. Each direct-interpreter CodeCoverageHit resolves its
   effective per-instance counter override, performs exactly one increment,
   and advances exactly one instruction. UINT64_MAX is sticky: the first
   attempted increment beyond it sets the counter's overflow flag, increments
   the bounded overflow count, and invokes the optional counter-identity report
   hook once; later attempts remain saturated without wrapping or re-reporting.
   Missing and out-of-range tables fail explicitly under FSIM-COV-010. Table
   replacement is bounded to 1,048,576 counters, allowed only before start,
   and transactional on allocation or length failure. Tests prove ordinary
   statement/branch increments, unavailable/out-of-range failures, the final
   representable increment, first and repeated overflow, sticky inspection,
   single callback reporting, rejected replacement state preservation, and
   execution through a compact hierarchy-instance counter override. Compiled
   execution intentionally remains at the unavailable host boundary; Change 12
   owns native LLVM counter lowering rather than disguising host callbacks as
   compiled support. The warnings-as-errors exact-LLVM 22.1.8 Debug fsim build
   compiled all 460 affected elaboration, LLVM, artifact, application, and CLI
   steps with eight workers in 4:52.21 at 2,789,560 KiB peak RSS and zero swaps.
   COVBASE-C02 through COVBASE-C11 are preserved and seven rows remain active
   at normalized SHA-256
   f46e9700c20df9868cf345d231085bc2b43516d46a6b906bcc51ee192d6a80a0;
   the source manifest remains exactly 1,555 paths at SHA-256
   af4e87a0951ad69343cb5d73f49df98d52f216c4e28487006e363748f5c1cab4.
   Native LLVM counter lowering remains Change 12; no Release, sanitizer,
   hosted-CI, clean-first, commit, or push action ran.
12. **Complete.** Lower equivalent counters through LLVM O0-O3. Covered
   processes publish a compact per-instruction map from the shared native body
   to the effective hierarchy-instance counter, plus the simulation-owned dense
   uint64 counter table, through appended v3 runtime ABI fields. Generated code
   bounds-checks both tables, increments the mapped counter directly, and never
   crosses the host boundary on the ordinary path. UINT64_MAX remains sticky;
   only saturation or an invalid/unavailable table enters the checked runtime
   service, preserving exact first-overflow reporting and typed generated-code
   failure. Uncovered processes allocate no hit map, and native body/cache
   sharing no longer depends on an instance's dense counter assignment. LLVM
   tests prove compact overrides, direct increments with zero callbacks,
   saturation, and unavailable storage at O0, O1, and O2. The application
   executor test proves the same behavior through the real Interpreter bridge
   at O0, O1, and O2, while explicitly verifying the existing application O3
   selection maps to the O2 pipeline. The appended C runtime layout is checked
   through byte 848. The warnings-as-errors exact-LLVM 22.1.8 Debug targets
   rebuild successfully with eight workers, and the accumulated Change 2-12
   identity, discovery, branch, line, inventory, SimIR, runtime ABI, LLVM,
   application, diagnostic, resource, and packaging slice passes 16/16 in
   22.21 wall seconds at 188,788 KiB peak RSS with zero swaps. COVBASE-C02
   through COVBASE-C12 are preserved and six rows remain active at normalized
   SHA-256
   88c765c97812ce58334f9020683b968ea83367ed2b7f4a1c37d55e7f0031f0c8;
   the exact 1,559-path source manifest SHA-256 is
   c672ba9c52ff209c63a9797c00e3007da79d0a6e054eb0ca19b79eaeb0ec0506.
   Debug-engine coverage remains Change 13; no Release, sanitizer, hosted-CI,
   clean-first, commit, or push action ran.
13. **Complete.** Preserve point identity and hits in the Debug engine. A
   bounded read-only Debug snapshot walks effective per-instance
   CodeCoverageHit operations, retains the exact instruction, 128-bit point
   identity, metric, dense counter identity, and current hit count, and never
   advances or mutates simulation state. It rejects invalid metrics or point
   identities, duplicate points, unavailable/out-of-range counter storage,
   instruction overflow, allocation failure, and explicit point-ceiling
   exhaustion transactionally. The interpreter Debug path and the real LLVM
   O0 Debug-instrumented executor both stop at the identical DebugPoint source
   location and lexical scope before the covered statement with zero hits,
   then resume through the same point/counter with exactly one hit. Additional
   evidence proves compact hierarchy-instance counter overrides and every
   snapshot rejection family. The warnings-as-errors exact-LLVM 22.1.8 Debug
   fsim and application targets build with eight workers, and the accumulated
   Change 2-13 focused slice passes 16/16 in 21.90 wall seconds at 186,340 KiB
   peak RSS with zero swaps. COVBASE-C02 through COVBASE-C13 are preserved and
   five rows remain active at normalized SHA-256
   c33e826346da6c621f54b9e87259e7041c5e9a6fdcf0a9ea4322df169a5aae63;
   the exact 1,560-path source manifest SHA-256 is
   784eeae5e98184db6eeefe8b3de11346d50813a39bc2d2d260fd707a4d4f185e.
   Static/declaration exclusions remain Change 14; no Release, sanitizer,
   hosted-CI, clean-first, commit, or push action ran.
14. **Complete.** Exclude non-executable declarations and statically removed
   constructs. A language-neutral bounded exclusion pass consumes explicit
   elaboration-owned declaration and static-removal source intervals, validates
   their source identities and spans, merges their interval union per source,
   and removes only statement or branch points wholly contained by that union.
   Enclosing decisions, neighboring executable statements, point IDs, metrics,
   source spans, lines, and input ordering remain byte-for-byte unchanged; an
   empty or declaration-only exclusion set is identity-preserving. Duplicate,
   malformed, unknown-source, invalid-kind, invalid-point, allocation, and
   point/exclusion-ceiling failures publish no partial result. Existing
   independently authored discovery corpora now explicitly prove procedural
   declarations create no point under Verilog/SystemVerilog and all five
   retained VHDL profiles. Focused evidence additionally proves removal of
   statement and branch points from a statically removed subtree without
   removing its enclosing executable decision. The exact-LLVM 22.1.8
   warnings-as-errors Debug targets build with eight workers, and the
   accumulated Change 2-14 slice passes 17/17 in 21.85 wall seconds at 190,808
   KiB peak RSS with zero swaps. COVBASE-C02 through COVBASE-C14 are preserved
   and four rows remain active at normalized SHA-256
   f5249bede2e64f2b4224b394d455cea1c7c80e848c972d2d6a00b65fabbb5a0a;
   the exact 1,563-path source manifest SHA-256 is
   7c5057a6bb0d81d656388c7f0c8153447163ccf174ff2b7b9dbee44facb3da5c.
   Canonical hierarchy-instance identity remains Change 15; no Release,
   sanitizer, hosted-CI, clean-first, commit, or push action ran.
15. **Complete.** Assign stable hierarchical instance identities. A dedicated
   v3 identity schema hashes the canonical hierarchy path, explicit retained
   HDL profile, logical library, elaborated unit, and canonical semantic
   parameter identities into a 128-bit instance ID. Every string is
   length-delimited, parameter inputs are sorted and duplicate names rejected,
   and neither specialization ordinal, allocation order, checkout path, nor
   source location participates. The bounded transactional constructor rejects
   empty or embedded-NUL hierarchy, missing library/unit, unknown language,
   malformed or duplicate parameter identity, zero digest, allocation failure,
   and explicit hierarchy/library/unit/parameter ceilings. Coverage inventory
   construction stores the ID on every instance, rejects duplicate identities,
   and recomputes it when validating retained or restored design state. Real
   parameterized-generate elaboration proves distinct root and generated-child
   IDs and exact repeatability across independent elaborations; focused
   evidence also proves root, generate index, profile, library, unit, and
   parameter-value distinctions plus parameter-order invariance. The
   exact-LLVM 22.1.8 warnings-as-errors Debug fsim and focused targets build
   with eight workers, and the accumulated Change 2-15 slice passes 18/18 in
   29.41 wall seconds at 185,752 KiB peak RSS with zero swaps. COVBASE-C02
   through COVBASE-C15 are preserved and three rows remain active at normalized
   SHA-256
   3a365eb191c738797c33b9bc02607a0afd58ffba9b3f02d45ec2ae2b88483e32;
   the exact 1,566-path source manifest SHA-256 is
   83f3bab8d3c21d169c595db72467c532d30188987a59d48f824804f9adfdd36d.
   Source aggregation remains Change 16; no Release, sanitizer, hosted-CI,
   clean-first, commit, or push action ran.
16. **Complete.** Compute source-aggregate unions without losing instance
   results. A bounded runtime aggregation pass first validates the complete
   language-neutral run/result, then requires one canonical source/instance
   owner for every dense counter. It partitions every exact point ID, metric,
   counter, hit count, and status into its instance result while separately
   unioning equal point IDs per canonical source. A source point is covered
   when any occurrence is covered, excluded only when every occurrence is
   excluded, and otherwise uncovered; total, covered, uncovered, and excluded
   occurrence counts remain explicit. Hit sums saturate at uint64 max and mark
   saturation instead of wrapping. Source and instance metric results are
   derived independently, with no synthetic cross-metric score. Source points
   sort by stable ID, while instance points retain dense-counter execution
   order. Invalid run/result, missing or noncanonical ownership, unknown
   source/instance, duplicate instance point, conflicting source/metric
   definition, allocation failure, and explicit instance/source/point ceilings
   publish no partial aggregate. Focused evidence proves two hierarchy
   instances can disagree on the same branch while the source union covers it
   and both original instance statuses/counters remain intact; it also proves
   exclusion, empty owners, saturation, deterministic order, every typed
   rejection family, and resource limits. The exact-LLVM 22.1.8
   warnings-as-errors Debug fsim, LLVM, application, and focused targets build
   with eight workers, and the accumulated Change 2-16 slice passes 19/19 in
   22.30 wall seconds at 189,200 KiB peak RSS with zero swaps. COVBASE-C02
   through COVBASE-C16 are preserved and two rows remain active at normalized
   SHA-256
   19eed8adb905adb0b4bbfb435bae5a50107e2713079b2fdf6bce525cd9025bb1;
   the exact 1,569-path source manifest SHA-256 is
   8b15040e411ba76571cf2c989e48e79febae73e44f0088a53f7555dce7a11020.
   Opt-in controls remain Change 17; no Release, sanitizer, hosted-CI,
   clean-first, commit, or push action ran.
17. **Complete.** Add opt-in manifest and CLI enablement with no default
   overhead. The project manifest is now schema 3 and accepts one optional
   `[coverage]` table whose only Change 17 field is the strictly typed Boolean
   `enabled`; omission and explicit false both select the disabled state.
   Schema 0, 1, 2, 4, missing, and out-of-range identities are rejected
   directly through the existing regeneration diagnostic, with no schema-2
   compatibility reader, migration, fallback, or dual write. The public
   `--code-coverage` flag overrides a loaded manifest or direct-source config
   for HDL compile, elaborate, and simulate phases and is rejected for check
   and SystemC-only phases. One immutable Boolean crosses the application
   build boundary. Disabled builds create no coverage inventory, counter
   table, callback, or per-operation runtime check; the control function is a
   fixed, allocation-free read of the configuration bit. Independently
   authored evidence proves default/true/false manifest behavior, wrong type,
   unknown key, duplicate table, stale schema, CLI phase rejection and
   override, and a real disabled elaboration with no coverage inventory. The
   project-manifest freeze now authenticates sixteen schema-3 rows including
   the coverage table at SHA-256
   4d858da38980443e2af0c7d8f25a2ff45b299fc155e1908193c8c95beeed064f.
   The warnings-as-errors exact-LLVM Debug application/project/CLI/fsim
   targets build incrementally with eight workers after repairing one
   test-only range-loop copy warning. The accumulated Change 2-17 slice passes
   22/22 in 7.53 wall seconds at 76,620 KiB peak RSS with zero swaps; its
   retained pre-documentation log SHA-256 is
   c68de97b63eb09a23f3a88abfed83fc17553c96a58dca6fda9b55efac3b81a54.
   The final post-documentation rerun passes the same 22/22 in 7.02 wall
   seconds at 76,388 KiB peak RSS with zero swaps; its retained log SHA-256 is
   a1ed4c3508b7c1a996ebada808bec7c1acba9859159fee9b929af25d1d88c869.
   COVBASE-C02 through COVBASE-C17 are preserved and one row remains active at
   normalized SHA-256
   f757d4d59a1d7e71ef7b66fd816ffd38544bb951f1cc939b52f932f84347bf96;
   the regenerated source manifest contains 1,568 paths at SHA-256
   e1a240e411c0cc41bcf324180f86b951b502bfcb8e0713b10d4e84b3b7d4870f.
   Artifact/design/cache identity remains Change 18; no Release, sanitizer,
   hosted-CI, clean-first, commit, or push action ran.
18. **Complete.** Add coverage identities to objects, designs, and
   native-cache keys. One canonical schema-3 identity now binds the enablement
   bit to either disabled model `none` or enabled model
   `fsim-code-coverage-foundation-v3` and authenticates the tuple with a
   domain-separated SHA-256 digest. `.fsimobj` format 7 stores and validates
   the identity and includes it in compilation digests; `.fsimdesign` format
   12 stores the design identity plus the matching identity of every object
   input and includes both in design provenance. Elaboration rejects objects
   whose identity differs from the current request, and standalone design
   restoration recovers the immutable enablement bit. Versioned format-6
   objects and format-11 designs are rejected directly with no v2 reader,
   migration, fallback, or dual write. Design cache keys and V3 cache records
   include the same digest. LLVM native-object key schema v168 includes it for
   both process-local and immutable-design modules, preventing reuse across
   enabled and disabled builds. Independently authored evidence proves
   deterministic identity construction; schema-2, model, and digest rejection;
   object/design round trips and coverage-sensitive digests; direct v2/future
   format rejection; application artifact flow; and native-cache hit/miss
   separation. Current ABI/schema, diagnostic, object/design freeze, source,
   and resource contracts carry the v3 identities. The accumulated exact-LLVM
   warnings-as-errors Debug Change 2-18 slice passes 30/30 in 27.12 wall
   seconds at 189,432 KiB peak RSS with zero swaps; the retained
   pre-documentation log SHA-256 is
   2f75e419da52b698c664bacc490d296a0ef6ead1ec5629a9cde811c707be1a8e.
   The final post-documentation rerun passes the same 30/30 in 32.79 wall
   seconds at 190,060 KiB peak RSS with zero swaps; its retained log SHA-256
   is a395b80e1683fc9a93b2dc0c4dadd430d96330f6e54e41be1b69382cf7a2ee9a.
   COVBASE-C02 through COVBASE-C18 are preserved at normalized SHA-256
   06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234;
   the regenerated source manifest contains 1,571 paths at SHA-256
   e45bb9ac875ae03ce24bc31ac876c80217ce89505be9ea523593a0f7fc17d92f.
   No Release, sanitizer, hosted-CI, clean-first, commit, or push action ran.
19. **Complete.** Prove Verilog/SystemVerilog/VHDL engine and aggregation equivalence.
   One independently authored mixed-language corpus parses and
   discovers canonical executable statement points from Verilog-2005,
   SystemVerilog-2017, and VHDL-2008 source. Each language has two distinct
   hierarchical instances of the same point; one executes and one remains
   uncovered. The serial interpreter is the reference. LLVM O0, O1, O2, the
   application O3 profile, and compiled Debug reproduce the exact dense
   counter vector, point IDs, per-instance counter/status results, and source
   aggregate unions. Every source retains two occurrences, one covered and
   one uncovered, while its union is covered; no instance distinction or
   language identity is erased. The resource contract freezes all three
   language sources, every LLVM/application optimization selection, Debug,
   and covered/uncovered aggregation assertions. With Change 19 complete all
   seventeen COVBASE-C02 through COVBASE-C18 rows and every declared evidence
   owner exist at normalized SHA-256
   06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234.
   The regenerated source manifest contains 1,572 paths at SHA-256
   d3ce8dc4beef96671f495f6585920e50880d311ece86ecf20deba6e271a0ab96.
   The accumulated exact-LLVM warnings-as-errors Debug Change 2-19 slice
   passes 31/31 in 27.22 wall seconds at 189,868 KiB peak RSS with zero swaps;
   the retained pre-documentation log SHA-256 is
   459fd71887b0d5af131774c512deac6663442ea3e89f3cf645ca1aaae63e39cf.
   After the documentation freeze, the same 31/31 slice passes in 27.03 wall
   seconds at 190,368 KiB peak RSS with zero swaps; its retained log SHA-256
   is 4d1f569e0d68ba5424b8a022004dc9cb86ddff88786dbb9565a961861aa6c524.
   No Release, sanitizer, hosted-CI, clean-first, commit, or push action ran.
20. **Complete.** Run standard batch closure and freeze the foundation inventory.
   The clean warnings-as-errors exact-LLVM Debug build completes
   2,497/2,497 steps with eight workers in 16:00.79 at 4,322,560 KiB peak RSS
   and zero swaps. Its complete marker-clean retained log has SHA-256
   30e187c8ae1edcc2e45a48a8f364be6137888c51b8de802aea88c23d3330dbd1.
   The corresponding sequential Debug suite passes 330/330 in 6:30.85 at
   1,099,524 KiB peak RSS and zero swaps; its retained log SHA-256 is
   a343f671a00de5fa705c6788c20a0ce91bbb0da4bc55ba8c177df495652f22fb.
   The clean warnings-as-errors LLVM Release build completes 1,302/1,302
   steps with eight workers in 13:40.12 at 1,891,240 KiB peak RSS and zero
   swaps. Its marker-clean retained log has SHA-256
   0b56552cab5114f4a85571acace795805dae21f66b2450ed94fa9629ef527b2b.
   The corresponding sequential Release suite passes 330/330 in 8:51.62 at
   1,099,280 KiB peak RSS and zero swaps; its retained log SHA-256 is
   602b510a6a259a7e70cdf8a532fba080dc2ae4a3f94f4d7600a64e43d3fae76c.
   Full-tree qualification repaired one stale aggregate design-object test,
   moved the coverage-specific native-cache matrix beside its lowering test
   to retain the 2,500-line hard limit, and advanced every deliberate v3
   schema, ABI, diagnostic, source, SPDX, and test/control freeze. The final
   inventories contain 2,560 production diagnostics, 1,218 bounded authored
   sources, 1,507 SPDX-owned files, 454 conformance test/control files, and
   656 FST test/control files. All seventeen COVBASE-C02 through COVBASE-C18
   rows remain preserved at normalized SHA-256
   06039618ff2c8530265b14e3250578f3fd0aa77d6e9b2a53a8049e7b760e8234;
   the 1,572-path source manifest remains at SHA-256
   d3ce8dc4beef96671f495f6585920e50880d311ece86ecf20deba6e271a0ab96.
   Batch 178 is not a sanitizer or hosted-CI boundary, so those lanes remain
   deferred exactly as required by the governing v3 contract.

### Batch 179 - condition, expression, toggle, and FSM metrics

1. **Complete.** Register exact condition, expression, toggle, and FSM obligations.
   The clause-neutral `code_coverage_metrics_inventory.tsv` matrix contains
   seventeen unique active COVMET-C02 through COVMET-C18 rows assigned
   one-to-one to Changes 2-18. It binds IEEE1076, IEEE1364, and IEEE1800 plus
   all thirteen retained HDL profiles to exact condition decomposition,
   short-circuit, binary and auxiliary unknown outcome, bounded expression,
   toggle-bin and selected-object, default/container-selection, unknown
   transition, inferred and described FSM, separate visit/transition, and
   transactional validation obligations. Every row has independently written
   scope and explicit implementation, positive, negative, engine,
   aggregation, artifact, diagnostic, and resource owners. The bounded
   validator enforces the twenty-batch/twenty-change plan, exact row/change/
   domain allocation, safe relative owners, active-to-preserved transitions,
   retained profiles, private-reference exclusion, and the explicit Batch 179
   exclusion of MC/DC. The resource contract and feature-matrix README freeze
   the same registration. The normalized ledger SHA-256 is
   ccbe30e82d1fa7954165a516c4930319eca69bb91382a3be291d9cb8805857c2;
   the regenerated 1,574-path source manifest SHA-256 is
   9b5d9e14c26b2b3f75eef8df4d39c02e634bcb06cc7a59fead02c5e6fe62df98.
   The exact-LLVM warnings-as-errors Debug tree regenerates with eight workers
   and requires no compilation. The seven-gate focused slice passes 7/7 in
   6.59 wall seconds at 22,752 KiB peak RSS with zero swaps; its retained
   pre-documentation log SHA-256 is
   93bedafd9ed7347acffe9c79fa0b011a5668c9fac43cecc962814b7f723b7342.
   The post-documentation rerun passes the same 7/7 in 7.05 wall seconds at
   22,596 KiB peak RSS with zero swaps; its retained log SHA-256 is
   431438923687df2bfd453d4e8e77d1d14e0f41db7c2475c012984ae34d5af01a.
   No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
2. **Complete.** Decompose Verilog/SystemVerilog decisions into stable atomic
   conditions. The new `verilog_coverage_conditions` elaboration seam walks
   retained if, conditional-loop, and immediate-assertion decisions without
   mutating their frontend expression trees. It preserves `&&`, `||`, and `!`
   as ordered path steps and publishes only the source-exact nonlogical atoms,
   so nested negation and future short-circuit instrumentation retain the
   original evaluation structure. Atomic identities use the canonical
   checkout-independent point algorithm and its new `atomic-condition`
   construct domain. Discovery is transactional and bounds authenticated
   sources, statement traversal, expression nodes, conditions, retained path
   steps, and both statement/expression nesting depths. Malformed arity,
   missing decisions, unknown sources, invalid spans, and duplicate identities
   have stable `FSIM-COV-015` ownership. The focused test covers all eight
   retained Verilog/SystemVerilog revisions, relocation, lexical identity,
   tree immutability, composite negation, assertions, diagnostics, and every
   governed resource family. COVMET-C02 is preserved; sixteen rows remain
   active. The normalized ledger SHA-256 is
   8bd4c99af60d486455f9fa6db513613ac0b28a9c7829fb90d6e96e8a450c36f2,
   and the 1,577-path source manifest SHA-256 is
   dbc2341d0eee406ab5ad13636e44a2c731bd6b6ca07fdaa467d5fcd66a091fc3.
   The exact-LLVM 22.1.8 warnings-as-errors Debug build uses eight workers.
   The nine-gate pre-documentation slice passes 9/9 in 7.05 wall seconds at
   22,560 KiB peak RSS with zero swaps; its retained log SHA-256 is
   c0ae27cd21d1e96d5177ea2c6d52b5852ea029e720a4425eb8a2145ef160ef96.
   The post-documentation rerun passes 9/9 in 7.65 wall seconds at 22,680 KiB
   peak RSS with zero swaps; its retained log SHA-256 is
   8aebeae90bf597b861aa52ee8eaa00e339b2c63edd95dbface66c78ccf1e307b.
   No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
3. **Complete.** Decompose VHDL Boolean decisions using equivalent rules. A
   shared `coverage_conditions` engine now owns the immutable bounded walk,
   source authentication, lexical decision/condition order, stable atomic
   identities, transactional publication, and common path/result types used
   by both language families. The VHDL wrapper validates all five retained
   revisions and recognizes if, conditional while, assertion, and explicit
   wait-until decisions. It preserves binary `and`, `or`, `nand`, `nor`,
   `xor`, and `xnor` plus unary `not` as exact ordered path nodes; comparisons,
   unary reductions, and the VHDL-2008 `??` condition conversion remain
   source-exact atoms. Synthetic true conditions used for unconditional loops
   and bare waits manufacture no coverage point. `FSIM-COV-016` owns VHDL
   language/revision, source, expression, span, duplicate, and resource
   failures. The focused corpus proves VHDL-87/93/2000/2002/2008 parsing,
   relocation, all logical operator families, AST immutability, decision
   ownership, profile rejection, malformed arity, source authentication,
   duplicate containment, and shared ceilings. COVMET-C02-C03 are preserved;
   fifteen rows remain active. The normalized ledger SHA-256 is
   12e6f0a9bfe99533b40331b625bc99f077958c83f603ae79a1457d7597f5a0aa,
   and the 1,582-path source manifest SHA-256 is
   432937b2bab3dd7418917e49e4d1db572bd05baecfb67646130392b562222118.
   The exact-LLVM warnings-as-errors Debug build uses eight workers. The
   ten-gate pre-documentation slice passes 10/10 in 8.20 wall seconds at
   22,864 KiB peak RSS with zero swaps; its retained log SHA-256 is
   eb7a9c2e27744485e89ba3f50da5bc49e6b91c5ccd5e12055994278933a78965.
   The post-documentation rerun passes 10/10 in 6.93 wall seconds at 22,644
   KiB peak RSS with zero swaps; its retained log SHA-256 is
   3a0f29c1cb9f31dbc59a203bbb3040cb4defe3ef52d6edd098aa405e9ecb5983.
   No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
4. **Complete.** Preserve short-circuit evaluation when recording condition
   outcomes. The language-neutral path operator/operand model now lives beside
   the new runtime `coverage_condition_evaluation` seam and remains available
   through the elaboration aliases used by Changes 2-3. For one decision, the
   runtime reconstructs and validates the complete expression tree from stable
   atomic paths, then invokes atom evaluators lazily in actual expression
   order. Definite false `and`/`nand` and definite true `or`/`nor` operands
   skip the complete right subtree; an unknown SystemVerilog operand evaluates
   the right side so four-state resolution remains correct. `xor`/`xnor` are
   eager and `not` preserves unknown truth. Evaluated observations and
   lexically ordered skipped atoms are separate; callback failure, exception,
   malformed/incomplete paths, duplicate/noncanonical identity, and every
   resource failure discard partial publication under `FSIM-COV-017`.
   Construction and evaluation are iterative and bound atoms, nodes, total
   path storage, and nesting. The focused corpus proves nested subtree skips,
   callback order, four-state determining/non-determining cases, VHDL
   `nand`/`nor` short-circuit behavior, eager `xor`/`xnor`, shuffled input,
   rollback, malformed plans, and all ceilings. The implementation audit also
   confirms existing SystemVerilog and VHDL lowering use the same governed
   branch behavior. No result counters or scoring were pulled forward from
   Change 5. COVMET-C02-C04 are preserved; fourteen rows remain active. The
   normalized ledger SHA-256 is
   f4c5bc29d245027bf0d47df29e98c98541079d56d6d58033fc8adb63e176157b,
   and the 1,585-path source manifest SHA-256 is
   92db9db0aab112cd7ffaeb3c06f83c06f565bc85bcfb67d70049a5acf3f1d640.
   The exact-LLVM warnings-as-errors Debug build uses eight workers. The
   eleven-gate pre-documentation slice passes 11/11 in 7.24 wall seconds at
   22,816 KiB peak RSS with zero swaps. The post-documentation rerun passes
   11/11 in 7.32 wall seconds at 22,640 KiB peak RSS with zero swaps. No
   Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
5. **Complete.** Record true, false, and auxiliary unknown four-state
   outcomes. The new runtime `coverage_condition_outcomes` model owns a dense
   point table with independent true/false scored counters and a third
   unknown-observation counter. Condition status is derived only from the two
   binary bins: neither is uncovered, one is partial, and both are covered;
   unknown activity never satisfies either bin. Updates authenticate unique
   table identities and exact condition-index/point ownership, reject invalid
   truth encodings and duplicate observations, and validate saturation state
   and all resource limits before mutation. The mutation pass allocates
   nothing, cannot fail, and saturates all three uint64 counters explicitly
   with per-counter overflow state and per-update overflow reporting, so
   transactional behavior requires no whole-table snapshot. Atoms omitted by
   Change 4 short-circuiting remain unchanged. `FSIM-COV-018` owns stable
   table, observation, saturation, and resource failures. The focused corpus
   proves true/false separation, unknown-only non-scoring, later completion of
   binary bins, skipped-atom stability, nonwrapping saturation, rollback for
   every invalid owner/truth/duplicate/state input, and both configured
   ceilings. No expression-combination expansion from Change 6 was pulled
   forward. COVMET-C02-C05 are preserved; thirteen rows remain active. The
   normalized ledger SHA-256 is
   6ce16dde407eeb50a7802363fb6652c7ff48a62f94c139cb56b083978bf2db6e,
   and the 1,588-path source manifest SHA-256 is
   2fb5ebfd35d1c50b0819d1124367e12d8e447e4013f31259410bd8182582c056.
   The exact-LLVM warnings-as-errors Debug build uses eight workers. The
   twelve-gate pre-documentation slice passes 12/12 in 7.15 wall seconds at
   22,592 KiB peak RSS with zero swaps. The post-documentation rerun passes
   12/12 in 7.27 wall seconds at 22,776 KiB peak RSS with zero swaps. No
   Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
6. **Complete.** Bound expression-combination expansion and report omitted
   combinations explicitly. The new runtime `coverage_expression` model
   authenticates an ordered atomic-point set, then emits a deterministic
   canonical binary prefix with the last lexical atom as the least significant
   bit. Independent ceilings bound atom count, retained combination count, and
   total packed uint64 words; the storage ceiling never creates a partial bin.
   For fewer than 64 atoms, the model reports the exact `2^N - retained`
   omitted count. Wider expressions publish the same exact formula
   symbolically (for example, `2^70-4`) and mark the numeric count inexact,
   avoiding both arithmetic overflow and a synthetic completion claim. A zero
   expansion budget is valid and reports the complete space as omitted.
   Invalid/duplicate identities and resource exhaustion fail transactionally
   under `FSIM-COV-019`. The focused corpus proves complete three-atom order,
   combination and packed-storage truncation, exact and symbolic omissions,
   wide packed truth layout, zero-budget behavior, transactional rejection,
   atom ceilings, and repeat determinism. No toggle behavior from Change 7
   was pulled forward. COVMET-C02-C06 are preserved; twelve rows remain
   active. The normalized ledger SHA-256 is
   d97a03c13050be51b8c792b5963b4634a5f2fee11ab7df78755c32478eaec66b,
   and the 1,591-path source manifest SHA-256 is
   5de6b0277c75ce52468cc84923229d96b0702b82a83d5a41a2378634e4808674.
   The exact-LLVM warnings-as-errors Debug build uses eight workers. The
   thirteen-gate pre-documentation slice passes 13/13 in 7.34 wall seconds at
   22,736 KiB peak RSS with zero swaps. The post-documentation rerun passes
   13/13 in 7.37 wall seconds at 22,944 KiB peak RSS with zero swaps. No
   Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
7. **Complete.** Define 0-to-1 and 1-to-0 toggle bins. The new runtime
   `coverage_toggle` model gives every stable point/bit owner two explicitly
   direction-qualified bin identities, independent uint64 counters, overflow
   flags, and uncovered/partial/covered status. A dense transition names its
   exact point, bit index, and outcome owner; the complete outcome table and
   batch are validated before an allocation-free mutation pass. Actual false
   to true and true to false events increment only their matching direction,
   same-value samples are no-ops, and ordered batches may contain multiple
   events for one bit. Both counters saturate and report overflow separately.
   Invalid/duplicate point-bit owners, inconsistent saturation, mismatched
   transition ownership, and resource excess fail transactionally under
   `FSIM-COV-020`. The focused corpus proves distinct bin identities, no
   direction aliasing, same-value behavior, status progression, repeated
   transitions, independent saturation, rollback, and both ceilings. The API
   accepts only binary transitions; X/Z diagnostics remain exclusively owned
   by Change 12. No object-inventory behavior from Change 8 was pulled
   forward. COVMET-C02-C07 are preserved; eleven rows remain active. The
   normalized ledger SHA-256 is
   65df83d604f7c074c0c1d426e6fed0762c9ca2aed0301a92cc8acc7c32899c15,
   and the 1,594-path source manifest SHA-256 is
   5a80f0114cb0a7914df029aa921cab0b95e0e660a3254ca9040dd60e5a66a9d6.
   The exact-LLVM warnings-as-errors Debug build uses eight workers. The
   fourteen-gate pre-documentation slice passes 14/14 in 7.85 wall seconds at
   22,644 KiB peak RSS with zero swaps. The post-documentation rerun passes
   14/14 in 7.44 wall seconds at 22,732 KiB peak RSS with zero swaps. No
   Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
8. **Complete.** Instrument Verilog/SystemVerilog ports, nets, signals, and
   retained variables. The new elaboration `verilog_toggle_inventory` model
   consumes one already-specialized semantic design unit and its immutable
   `CoverageInventoryOwner`; it never recovers ownership from source text or
   runtime process names. Verilog modules and SystemVerilog modules,
   interfaces, and programs share the model, while the source language remains
   part of every point identity. The builder normalizes the semantic unit's
   library exactly as hierarchy elaboration does, authenticates its canonical
   specialization identity and stable instance identity, and qualifies every
   selected declaration through the exact instance and already-expanded
   generate name. Each object retains one checkout-independent source
   `ToggleObject` point plus a distinct instance/path-qualified point. Ports,
   net spellings and user-defined nettypes, residual signals, and packed
   module/generate-scope variables receive explicit semantic kinds; every bit
   maps contiguously to an empty Change 7 outcome. Object order is canonical
   independently of declaration-container order. Real, string, handle,
   interface, and whole-container objects do not manufacture binary bins;
   explicit default-exclusion records and selected memory/array elements
   remain Changes 10 and 11. Independent ceilings bound authenticated sources,
   semantic input objects, selected objects, aggregate bits, per-object width,
   names, paths, lines, and instance identity. Invalid owners, languages,
   unit kinds, source mappings, spans, widths, duplicate paths/identities, and
   resource excess fail transactionally under `FSIM-COV-021`. The focused
   corpus proves real parser classification, all retained Verilog/SystemVerilog
   unit families, generated names, hierarchy separation, checkout relocation,
   canonical order, contiguous bit ownership, skipped nonbinary/container
   objects, rollback, and every resource boundary. No VHDL selection from
   Change 9 was pulled forward. COVMET-C02-C08 are preserved; ten rows remain
   active. The normalized ledger SHA-256 is
   acee3ca8c5d69e2caeb9e7bf1768fbeeb82e061bd8fc69b8a3ebfe5df1a486c5,
   and the 1,597-path source manifest SHA-256 is
   0d533fb73adc05a7a779e7d7efd1e91393e75555a1894013bf4febd53d58383e.
   The exact-LLVM warnings-as-errors Debug build uses eight workers. The
   fifteen-gate pre-documentation slice passes 15/15 in 7.54 wall seconds at
   22,844 KiB peak RSS with zero swaps. The post-documentation result is
   recorded in the active resume checkpoint. No Release, clean-first,
   sanitizer, hosted-CI, commit, or push action ran.
9. **Complete.** Instrument equivalent VHDL ports, signals, and retained
   variables. The new elaboration `vhdl_toggle_inventory` model consumes one
   already-specialized VHDL architecture, its resolved specialized entity-port
   view, and the immutable `CoverageInventoryOwner` for exactly one elaborated
   occurrence. This explicit two-unit boundary matches VHDL hierarchy
   elaboration: architecture declarations remain primary while entity ports
   are authenticated through the owner's exact source dependencies. Empty
   semantic libraries normalize to `work`; canonical
   `vhdl:library.entity(architecture)` identity, all five retained revision
   values, and the existing v3 instance identity are validated before object
   discovery. Ports, architecture signals, and architecture-scope shared
   variables receive explicit semantic kinds, checkout-independent source
   `ToggleObject` points, distinct instance/path-qualified points, and dense
   Change 7 outcomes for every directly packed bit. Scalar bit, std_logic,
   Boolean, and integer objects are eligible. One-dimensional concrete vectors
   qualify only when their element is exactly one bit or std_logic value;
   composite/multidimensional arrays remain Changes 10-11 work. Ordinary
   process/callable variables, non-shared architecture variables, files,
   access, protected, physical, string, and foreign SystemVerilog type shapes
   do not manufacture bins. Canonical path order is independent of declaration
   container order, source points are shared across hierarchy occurrences and
   retained revisions, and concrete points remain instance-distinct.
   Independent ceilings bound sources, semantic input objects, selected
   objects, aggregate bits, per-object width, names, paths, lines, and instance
   identity. Invalid revision/language/unit/owner/source/span/width metadata,
   source ownership, duplicate paths/identities, and resource excess fail
   transactionally under `FSIM-COV-022`. The focused corpus proves every
   retained VHDL profile, real parser entity/architecture separation, split
   entity-source ownership, shared-variable retention, dense outcomes,
   hierarchy separation, relocation and declaration-order stability, default
   nonbinary/composite/local omission, rollback, and resource boundaries. No
   explicit exclusion records from Change 10 were pulled forward. COVMET-C02-
   C09 are preserved; nine rows remain active. The normalized ledger SHA-256 is
   0946cbf0fa2084011b98cf6ae6ff2fac2b6a1955109a52a8b631c7e96d12fe4b,
   and the 1,600-path source manifest SHA-256 is
   743c690e13dd0ce177bb60f3471e4e4267211338bfcd8589f466d76f1eae08de.
   The exact-LLVM warnings-as-errors Debug build uses eight workers. The
   sixteen-gate pre-documentation slice passes 16/16 in 7.36 wall seconds at
   22,960 KiB peak RSS with zero swaps. The post-documentation result is
   recorded in the active resume checkpoint. No Release, clean-first,
   sanitizer, hosted-CI, commit, or push action ran.
10. **Complete.** Exclude automatic locals and memories by default. The new
    language-neutral `coverage_toggle_selection` elaboration model walks one
    already-specialized Verilog, SystemVerilog, or VHDL unit together with the
    resolved entity-port view used by VHDL. It records every default exclusion
    explicitly rather than silently dropping it: automatic callable locals,
    process and nested-block locals, static memories, SystemVerilog containers,
    and non-directly-packed VHDL arrays retain ordered reason sets. An object
    may therefore preserve multiple applicable reasons, such as procedural
    local, memory, and array, without manufacturing a scored bin. Ordinary
    retained scalar objects produce no exclusion and remain owned by Changes
    8-9. Each exclusion authenticates the source/span, retains a stable
    checkout-independent source `ToggleObject` point, and receives a distinct
    instance/path/reason-qualified identity. Recursive process, statement,
    function, task, and VHDL procedure traversal uses source-offset-qualified
    lexical scopes, canonical output ordering, and exact architecture/entity
    source ownership. Independent ceilings bound sources, declarations,
    exclusions, total reasons, names, hierarchy paths, nesting depth, lines,
    and instance identity. Invalid language/unit/owner/source/span/line/text,
    duplicate paths/identities, and resource excess discard all output under
    `FSIM-COV-023`. The focused corpus proves complete and ordered reason sets,
    default non-selection, hierarchy/relocation/declaration-order stability,
    VHDL array/shared-memory treatment, transactional failures, invalid scope
    text, and every configured ceiling. Explicit memory/array element opt-in
    remains exclusively Change 11 work. COVMET-C02-C10 are preserved; eight
    rows remain active. The normalized ledger SHA-256 is
    9bc16f6d399e3ec7807281fc41fcf6569bda6a668078bb076a1c3b66e4ac4503,
    and the 1,603-path source manifest SHA-256 is
    f2a52a4bfbcb9ae5b93ba895345872f1d9665e6b5075559693a71605d557ee30.
    The exact-LLVM warnings-as-errors Debug build uses eight workers. The
    seventeen-gate pre-documentation slice passes 17/17 in 7.14 wall seconds
    at 22,876 KiB peak RSS with zero swaps. The post-documentation result is
    recorded in the active resume checkpoint. No Release, clean-first,
    sanitizer, hosted-CI, commit, or push action ran.
11. **Complete.** Add explicit memory and array toggle-selection rules. The new
    language-neutral `coverage_memory_toggle` elaboration model consumes the
    authenticated default-exclusion inventory from Change 10 and exact
    hierarchy-path rules. Numeric static, dynamic, queue, integer-associative,
    and VHDL arrays require one explicit inclusive range per dimension;
    string-keyed associative arrays require exact nonempty keys. Every selector
    also names a nonempty packed-element bit slice. There is deliberately no
    wildcard, omitted-coordinate, omitted-bit, or implicit whole-container
    form. Change 10 exclusions now retain a bounded semantic shape: container
    kind, concrete elaborated dimensions where available, binary element width,
    and string-index classification. Parsed two-dimensional SystemVerilog
    memory evidence proves this metadata comes from the real semantic surface.
    Static and VHDL endpoints must lie inside every declared concrete bound;
    unresolved bounds fail rather than guessing. Dynamic/queue indices must be
    nonnegative, while associative indices retain their exact integer or string
    key. Canonical expansion gives each selected element a point derived from
    its exclusion identity and coordinates/key, preserves its common source
    point, and maps the exact bit slice densely into Change 7 outcomes. Output
    is independent of rule/range/key order. Overlapping selectors cannot alias
    a scored bit. Independent ceilings bound exclusions, rules, selectors,
    dimensions, keys/key bytes, range span, expanded elements/bits, and paths;
    all ownership, shape, selector, bound, key, overlap, collision, and resource
    failures discard the result under `FSIM-COV-024`. The focused corpus covers
    parsed multidimensional memory, dynamic and queue ranges, string
    associative keys, VHDL ranges, bit slices, canonical order, no implicit
    whole-container selection, transactional negatives, and all configured
    ceilings. Unknown/X/Z observation remains solely Change 12 work.
    COVMET-C02-C11 are preserved; seven rows remain active. The normalized
    ledger SHA-256 is
    5c33ffb1cb6200a0b33ba3e71c5690a42bfa8b7753750afaae36399319bb891f,
    and the 1,606-path source manifest SHA-256 is
    283b40814ca847f1f34783819ee1d3644c21033f14e749808f7283811b9ee420.
    The exact-LLVM warnings-as-errors Debug build uses eight workers. The
    eighteen-gate pre-documentation slice passes 18/18 in 7.42 wall seconds at
    22,980 KiB peak RSS with zero swaps. The post-documentation result is
    recorded in the active resume checkpoint. No Release, clean-first,
    sanitizer, hosted-CI, commit, or push action ran.
12. **Complete.** Track X/Z transitions diagnostically without scoring them as
    binary toggles. The Change 7 runtime outcome now retains independent
    saturating unknown-participation and high-impedance-participation counters
    and overflow flags beside the unchanged zero-to-one and one-to-zero scored
    counters. A new four-state transition surface accepts only canonical zero,
    one, unknown, and high-impedance values. A changed transition involving X
    increments the unknown diagnostic counter; one involving Z increments the
    high-impedance counter; X-to-Z increments each once. Same-value samples are
    no-ops. A scored direction can increment only when both endpoints are exact
    binary values, so X-to-one, zero-to-Z, and all other nonbinary transitions
    leave coverage status unchanged. Exact zero-to-one and one-to-zero events
    through the four-state path still reach their original bins. All four
    counters saturate independently without wrapping. The existing binary API
    remains source-compatible and validates the complete outcome saturation
    state. Both entry points authenticate unique point/bit owners, dense
    transition ownership, logic encodings, saturation invariants, and resource
    ceilings before an allocation-free mutation pass; invalid input publishes
    no observation under the existing `FSIM-COV-020` family. The focused
    corpus proves separate X/Z accounting including X-to-Z, same-nonbinary
    no-ops, zero binary score from diagnostic activity, later exact-binary
    completion, independent saturation, invalid encoding/state/ownership, and
    transactional limits. COVMET-C02-C12 are preserved; six rows remain
    active. The normalized ledger SHA-256 is
    544b1abfc20f62eb35af062e5be5051d377103b0b4d69d5ac3c62119e12d32ae.
    The source manifest remains 1,606 paths at SHA-256
    283b40814ca847f1f34783819ee1d3644c21033f14e749808f7283811b9ee420.
    The exact-LLVM warnings-as-errors Debug build uses eight workers. The
    eighteen-gate pre-documentation slice passes 18/18 in 7.60 wall seconds at
    22,980 KiB peak RSS with zero swaps. The post-documentation result is
    recorded in the active resume checkpoint. No Release, clean-first,
    sanitizer, hosted-CI, commit, or push action ran.
13. **Complete.** Infer enum- and case-based current-state objects. The new
    language-neutral `coverage_fsm_inference` elaboration model considers only
    retained unit signals/variables and output or buffer ports. It accepts
    VHDL enumeration literals, parsed SystemVerilog enum typedefs, and
    unambiguous exact-case evidence whose selector is a direct retained-object
    identifier and whose choices are simple explicit values. Enum declaration
    order is preserved; case-only state names are canonicalized. Wildcard,
    range, pattern, shadowed, duplicate-owner, and conflicting case-only
    descriptions cannot manufacture a current-state object. Stable source,
    instance-object, and per-state identities include the new
    `fsm-current-state-object` construct kind while remaining independent of
    checkout location and declaration-container order. No next-state object or
    legal-state set is present; Change 14 retains that ownership. Construction
    authenticates source and hierarchy ownership and enforces bounded source,
    object, case, choice, state, name, path, statement-depth, line, and instance
    inputs transactionally under `FSIM-COV-025`. The independently authored
    corpus covers parsed SystemVerilog enum/case and case-only objects, all five
    retained VHDL profiles, relocation and sibling instances, ambiguity,
    lexical shadowing, duplicate declarations, source failure, and resource
    ceilings. COVMET-C02-C13 are preserved; five rows remain active. The
    normalized ledger SHA-256 is
    9953619a01ad6b350a6f2b93ab31a8681441fefc6a48b6ef15b88c19dc71b5f7,
    and the 1,609-path source manifest SHA-256 is
    2590fd8392ba51d778cd9a2cad952aaf10db850d42176523c2af55850ecaef23.
    The exact-LLVM warnings-as-errors Debug build uses eight workers. The
    nineteen-gate pre-documentation slice passes 19/19 in 7.29 wall seconds at
    23,068 KiB peak RSS with zero swaps. The post-documentation result is
    recorded in the active resume checkpoint. No Release, clean-first,
    sanitizer, hosted-CI, commit, or push action ran.
14. **Complete.** Infer optional next-state objects and legal-state sets. The
    Change 13 model now recognizes a next-state object only when a direct
    procedural assignment connects a retained current-state target to a
    different retained identifier. Both declarations must have compatible
    enum sets, nominal/named types, or fully concrete scalar domain, width, and
    signedness. Lexical shadows, self-assignment, non-identifier expressions,
    incompatible types, multiple candidates, duplicate declarations, and a
    next object shared by multiple current objects cannot establish the
    optional relation. An enum-only object used solely as a next-state source
    is no longer misclassified as another current-state object. Every inferred
    current object publishes one stable legal-state-set identity referencing
    its existing ordered state IDs; enum evidence governs enum sets and exact
    case evidence is credited only when its canonical choices match that set.
    No missing state or transition is synthesized. The new stable
    `fsm-next-state-object` source kind keeps next declarations distinct from
    current declarations while relocation and sibling-instance behavior remain
    deterministic. Assignment, next-object, and legal-set ceilings extend the
    existing transactional `FSIM-COV-025` contract. Independently authored
    parsed SystemVerilog fixtures prove enum and scalar case-only next objects,
    while manual VHDL fixtures prove all five retained profiles; the corpus
    also covers relocation, ambiguity, lexical shadowing, exact ID reuse, and
    resource failure. Source attributes and pragmas remain solely Change 15/16
    work. COVMET-C02-C14 are preserved; four rows remain active. The normalized
    ledger SHA-256 is
    1254b56412974f2f33771d9d68059497eb72ab94d3deb203673ae9697dfff833,
    and the 1,609-path source manifest remains at SHA-256
    2590fd8392ba51d778cd9a2cad952aaf10db850d42176523c2af55850ecaef23.
    The public construct-kind change passes a complete 871-step, eight-worker,
    exact-LLVM warnings-as-errors Debug impact rebuild. The nineteen-gate
    pre-documentation slice passes 19/19 in 7.73 wall seconds at 23,196 KiB
    peak RSS with zero swaps. The post-documentation result is recorded in the
    active resume checkpoint. No Release, clean-first, sanitizer, hosted-CI,
    commit, or push action ran.
15. **Complete.** Implement standard SystemVerilog FSM description pragmas.
    Attribute instances immediately preceding a SystemVerilog module,
    interface, or program may now carry the exact vendor-neutral
    `fsm_current_state`, `fsm_next_state`, and `fsm_legal_states` keys. The
    parser preserves recognized specifications by attribute-instance group,
    retains decoded string values and source spans, ignores unrelated vendor
    attributes, and does not enable this meaning in Verilog profiles. Each
    group requires one uniquely resolved retained current-state object; an
    optional next-state name must resolve uniquely to a distinct compatible
    retained object, while an optional comma-separated legal-state list must
    contain at least two unique bounded names. A pragma may augment enum
    evidence, select a legal subset without manufacturing states, or describe
    a scalar FSM that has neither enum nor case evidence. Explicit pragma next
    state evidence takes precedence over procedural assignment inference;
    multiple compatible candidates suppress only the optional relation, and
    conflicting repeated descriptions suppress their pragma evidence pending
    Change 18 diagnostics. All SystemVerilog-2005, 2009, 2012, and 2017
    profiles behave identically. Malformed values, wrong-profile retained
    metadata, missing or incompatible objects, unknown enum legal states, and
    pragma group/specification/value ceilings fail transactionally under the
    extended `FSIM-COV-025` family. The independently authored corpus also
    proves relocation stability, grouped parsing, unknown vendor-key
    isolation, pragma-only scalar inference, enum legal subsets, explicit
    current/next linkage, ambiguity containment, and evidence provenance.
    COVMET-C02-C15 are preserved; three rows remain active. The normalized
    ledger SHA-256 is
    070d2ee13b6ab1e75d3209a84bb77a4ba368bb1b22ac97fd9f6d5de8717d34b0,
    and the 1,609-path source manifest remains at SHA-256
    2590fd8392ba51d778cd9a2cad952aaf10db850d42176523c2af55850ecaef23.
    The shared frontend-model change passes an 898-step eight-worker
    exact-LLVM warnings-as-errors Debug impact build in 10:35.31 at 3,949,796
    KiB peak RSS with zero swaps. The nineteen-gate pre-documentation slice
    passes 19/19 in 7.39 wall seconds at 23,280 KiB peak RSS with zero swaps.
    The post-documentation result is recorded in the active resume checkpoint.
    No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
16. **Complete.** Add VHDL source hints and language-neutral manifest FSM
    hints. The new bounded `coverage_fsm_hints` model carries independent
    VHDL-source and manifest contributions without changing HDL execution
    semantics. VHDL uses ordinary string-typed user attributes named exactly
    `fsm_current_state`, `fsm_next_state`, and `fsm_legal_states` on retained
    signal or variable names: the current marker accepts exact string values
    `true`/`false`, next names one retained object, and legal states are a
    comma-separated list. All five retained VHDL profiles canonicalize names
    and produce identical hint records; unrelated vendor attributes remain
    ordinary ignored user metadata. The schema-3 project manifest now accepts
    repeatable `[[coverage.fsm]]` tables with required exact `instance` and
    `current_state`, optional `next_state`, and optional string-array
    `legal_states`. The same table describes VHDL or SystemVerilog instances.
    The project reader retains typed entries transactionally and its frozen
    16-row manifest contract now hashes to
    be2e1aee6cdc3dafd30c8f7e20c933545394a2ebdd39595e6988a0738e0b8430.
    FSM inference revalidates every hint at its trust boundary, ignores valid
    entries for other instances, requires exact compatible retained objects,
    preserves separate source/manifest provenance on current, next, and legal
    records, and composes matching VHDL, manifest, and SystemVerilog pragma
    descriptions. Conflicting explicit legal descriptions suppress explicit
    evidence while preserving independently inferred enum/case results;
    Change 18 retains detailed conflict diagnostics. Attribute, entity-name,
    manifest-entry, combined-hint, legal-state, name, value, instance, and
    inference-side limits fail transactionally under `FSIM-COV-026` or the
    extended `FSIM-COV-025` boundary. The independently authored corpus proves
    both languages, all VHDL profiles, relocation, matching and conflicting
    cross-source composition, other-instance isolation, unknown vendor-key
    rejection, malformed schema/source entries, unknown states/objects, and
    every new resource family. COVMET-C02-C16 are preserved; two rows remain
    active. The normalized ledger SHA-256 is
    8a459c5baf3bc56592083540e6a828ecccb460fb373d7dbe525ff6f9e9005aa9,
    and the 1,612-path source manifest SHA-256 is
    decea49910c11df02edc37a2fb71701f01a8404a3c2b13d7a474c97b59ad1c53.
    The shared project/inference model changes pass a 632-step, eight-worker,
    exact-LLVM warnings-as-errors Debug impact build in 7:00.57 at 2,792,604
    KiB peak RSS with zero swaps. The twenty-two-gate pre-documentation slice
    passes 22/22 in 7.57 wall seconds at 23,520 KiB peak RSS with zero swaps.
    The post-documentation result is recorded in the active resume checkpoint.
    No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
17. **Complete.** Record state visits and legal transitions separately. The
    new language-neutral `coverage_fsm` runtime model consumes already stable
    instance, current-state-object, state, and explicitly declared legal-
    transition identities without depending on elaboration internals. It
    constructs independently identified `state-visit` and
    `legal-transition` bins, with the hierarchical instance identity included
    in every bin digest. Definitions are canonicalized across machine, state,
    and transition input order and retain the specialization and exact
    instance path. Every sample increments exactly one state-visit counter.
    The first sample for a machine establishes history without scoring a
    transition; later samples increment only a matching declared ordered
    transition. Undeclared pairs never manufacture coverage bins and instead
    increment a per-machine diagnostic counter. Previous-state history is
    independent per machine, so observations for multiple FSMs may interleave
    without cross-machine transitions. Visit, transition, and diagnostic
    counters saturate without wrapping and expose overflow once. Runtime-model
    and complete observation-batch validation precede mutation, rejecting
    invalid identities, duplicate ownership, non-canonical/tampered bins,
    invalid previous state, inconsistent saturation, foreign observations,
    and machine/state/transition/observation/path resource ceilings under
    `FSIM-COV-027`. Summaries publish state-visit and legal-transition totals
    separately and deliberately contain no synthetic FSM score. The
    independently authored corpus proves deterministic/reordered definitions,
    sibling instances, legal and undeclared transitions, independent
    histories, all saturation families, transactional model and batch
    failures, and every resource ceiling. COVMET-C02-C17 are preserved; one
    row remains active. The normalized ledger SHA-256 is
    b33e6b1d80bceeeee4586411b20b3d3eac8a8b1c7306747a8060532047f84a11,
    and the 1,615-path source manifest SHA-256 is
    645470d8324ce8378ce5db7808cac6cba79eb2e0e22a438b773fc7dcbd74f556.
    The runtime-library change passes a 135-step, eight-worker, exact-LLVM
    warnings-as-errors Debug impact build in 36.39 wall seconds at 1,649,264
    KiB peak RSS with zero swaps. The twenty-five-gate pre-documentation slice
    passes 25/25 in 1.27 wall seconds at 23,520 KiB peak RSS with zero swaps.
    The post-documentation result is recorded in the active resume checkpoint.
    No Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
18. **Complete.** Diagnose ambiguous, incomplete, and conflicting FSM
    descriptions. The new bounded `coverage_fsm_validation` layer accepts
    language-neutral diagnostic candidates, validates the complete input,
    coalesces equal instance/object/kind/subject issues, unions their evidence
    origins without unbounded accumulation, canonicalizes the result, and
    generates stable relocation-independent diagnostic identities. It
    distinguishes current-state, next-state, and legal-state subjects and
    retains enum, exact-case, assignment, SystemVerilog pragma, VHDL source,
    and manifest origins. `FSIM-COV-028` reports ambiguous duplicate current
    state ownership, competing inferred next objects, and next objects shared
    across current machines. `FSIM-COV-029` reports explicit scalar FSMs that
    lack a state universe and exact cases that incompletely cover an enum.
    `FSIM-COV-030` reports differing repeated cases or pragmas, incompatible
    explicit next relations, enum/case disagreement, and cross-source
    pragma/VHDL/manifest disagreement. Inference publishes diagnostics on its
    language-neutral result, omits only the ambiguous, incomplete, or
    conflicting evidence, and preserves independently valid enum/case
    fallback. Malformed diagnostic state or candidate, diagnostic, merged-
    origin, instance-path, and object-name ceilings reject the entire
    inference construction transactionally. The independently authored
    corpus proves all three issue kinds and subjects, all evidence-origin
    families, deterministic coalescing, origin union, sibling-instance
    identity, fallback preservation, invalid enum/text/identity/origin input,
    inference-side resource propagation, and every standalone resource
    ceiling. COVMET-C02-C18 are all preserved and zero rows remain active. The
    normalized ledger SHA-256 is
    e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443,
    and the 1,618-path source manifest SHA-256 is
    30ca495468c5079caaff91c6964cbdc9605585be2c3640aa48c1eded6d3dbac6.
    The public inference-model change passes a 129-step, eight-worker,
    exact-LLVM warnings-as-errors Debug impact build in 35.18 wall seconds at
    1,649,292 KiB peak RSS with zero swaps. The twenty-six-gate pre-
    documentation slice passes 26/26 in 1.34 wall seconds at 23,476 KiB peak
    RSS with zero swaps. The post-documentation result is recorded in the
    active resume checkpoint. No Release, clean-first, sanitizer, hosted-CI,
    commit, or push action ran.
19. **Complete.** Prove metric semantics across generate instances, engines, and mixed designs.
    Two independently authored application witnesses now
    compose the complete Batch 179 runtime metric surface. The parsed-design
    witness builds real SystemVerilog-2017 and VHDL-2008 condition and toggle
    inventories for two generated sibling instances per language. It proves
    identical source identity but distinct hierarchy-qualified object and FSM
    bin identity, per-instance partial condition/toggle results, covered source
    unions, bounded expression enumeration, separate state-visit and legal-
    transition results, and bidirectional source aggregation without a
    synthetic score. The engine witness executes six generated instances in
    one mixed Verilog-2005/SystemVerilog-2017/VHDL-2008 design and compares the
    exact observation and broad-metric snapshot from the serial interpreter,
    LLVM O0, O1, O2, O3, and compiled Debug. It covers short-circuit skips,
    true/false/unknown condition accounting, expression bins, binary and X/Z
    toggle activity, FSM visits/transitions, and stable per-instance IDs.
    Enabled artifact identity now selects
    `fsim-code-coverage-broad-metrics-v3`; the former foundation-only model is
    rejected deterministically, while disabled identity remains `none`.
    Engine, aggregation, and artifact evidence owners for every COVMET row are
    now required to exist. The normalized seventeen-row ledger remains
    `e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443`;
    the source manifest contains 1,621 ordered paths at SHA-256
    `c0542c5b8baae097e53cccf2d282aab580efcf18b2853f76a23f2fb74a39b5b1`.
    The 334-step warnings-as-errors Debug impact build completes with eight
    workers in 4:31.29 at 1,649,512 KiB peak RSS and zero swaps. The cumulative
    pre-documentation focused set passes 34/34 in 1.87 wall seconds at 78,356
    KiB peak RSS with zero swaps. The post-documentation rerun passes the same
    34/34 in 1.58 wall seconds at 78,328 KiB peak RSS with zero swaps. No
    Release, clean-first, sanitizer, hosted-CI, commit, or push action ran.
20. **Complete.** Run standard batch closure and freeze the broad metric set; MC/DC remains excluded.
    The complete broad-metric surface is frozen with all seventeen
    COVMET-C02-C18 rows preserved, zero active rows, and normalized ledger
    SHA-256
    `e145b9139cf58989ddbd683ff5b478c966d473944684ea11b60cde661dba7443`.
    MC/DC remains explicitly outside Batch 179, and the 1,621-path source
    manifest remains
    `c0542c5b8baae097e53cccf2d282aab580efcf18b2853f76a23f2fb74a39b5b1`.
    Full-tree qualification refreshed the deliberate repository freezes to
    2,576 production diagnostics, 1,265 bounded authored sources, 1,556
    SPDX-owned files, 471 conformance test/control files, and 675 FST
    test/control files. Release optimization additionally exposed one copied
    structured binding in the toggle-inventory corpus; binding the immutable
    profile pair by reference removes that warning without changing behavior.
    The clean exact-LLVM warnings-as-errors Debug build completes 2,595/2,595
    steps with eight workers in 17:22.80 at 4,322,732 KiB peak RSS with zero
    swaps. The sequential Debug suite passes 348/348 in 6:26.96 at 1,100,028
    KiB peak RSS with zero swaps. The final uninterrupted clean LLVM Release
    build completes 1,351/1,351 steps with eight workers in 14:46.51 at
    1,893,948 KiB peak RSS with zero swaps. The sequential Release suite
    passes 348/348 in 8:55.50 at 1,099,516 KiB peak RSS with zero swaps.
    Normalizing only the new Batch 179 translation units to the repository's
    WebKit format triggers warning-clean eight-worker incremental rebuilds of
    196 Debug and 160 Release steps. The exact final tree then passes the full
    Debug suite 348/348 in 6:44.40 and the full Release suite 348/348 in
    8:46.99.
    Batch 179 is neither a sanitizer nor a hosted-CI boundary, so neither lane
    ran or is claimed here.

### Batch 180 - unified coverage database, standard API, and reports

1. Define the bounded, versioned .fsimcov container schema.
2. Store model fingerprint, source inventory, run metadata, metrics, and exclusions.
3. Move SystemVerilog functional coverage into its database namespace.
4. Move PSL coverage into its database namespace.
5. Implement deterministic serialization and atomic replacement.
6. Implement strict same-design merging as the default.
7. Implement explicit partial merging of unchanged point identities.
8. Implement SystemVerilog coverage constants and $coverage_control.
9. Implement $coverage_get, $coverage_get_max, $coverage_merge, and $coverage_save.
10. Implement the corresponding VPI coverage controls, properties, and traversal.
11. Support module, hierarchy, instance, and coverage-type selection.
12. Add source fsim coverage off/on controls with metric and reason.
13. Add external source, hierarchy, object, and metric exclusion rules.
14. Preserve every excluded point and reason in database and reports.
15. Implement source, instance, and combined report models without a synthetic grand score.
16. Implement deterministic text, HTML, and full-fidelity JSON reports.
17. Implement LCOV and Cobertura projections for supported metric families.
18. Implement coverage merge/report, per-metric thresholds, and CI exit status.
19. Test corruption, size ceilings, path safety, merge conflicts, and mixed-language regressions.
20. Run clean Debug/Release, sanitizer, and hosted monitoring closure for coverage.

### Batch 181 - IEEE legacy TF PLI

1. Register IEEE TF requirements and explicitly exclude vendor extensions.
2. Define the v3 native-plugin ABI and common loader metadata.
3. Provide standard-compatible veriuser.h declarations and constants.
4. Provide Linux shared-library and Windows import-library link surfaces.
5. Discover and validate standard TF registration tables.
6. Validate every registered task/function descriptor transactionally.
7. Implement checktf, sizetf, calltf, and their failure containment.
8. Implement misctf lifecycle and synchronization reasons.
9. Implement argument count, type, direction, and expression inspection.
10. Implement integer, real, string, vector, and expression value access.
11. Implement parameter and instance-specific access.
12. Implement simulation time, delay, and timescale access.
13. Implement scope, instance, work-area, and user-data lifetimes.
14. Implement TF output, warning, error, and finish/stop controls.
15. Implement read-only and read-write synchronization callbacks.
16. Register TF system tasks/functions in Verilog and SystemVerilog profiles.
17. Serialize TF calls through the scheduler coordinator.
18. Contain plugin exceptions, invalid pointers, unload, and re-entry.
19. Prove independently authored C/C++ plugins on Linux and Windows.
20. Run standard batch closure and freeze the TF surface.

### Batch 182 - IEEE ACC and complete legacy PLI closure

1. Register the complete IEEE ACC routine and object inventory.
2. Provide standard-compatible acc_user.h.
3. Implement initialization, shutdown, configuration, and error reporting.
4. Map ACC handles onto generation-qualified hierarchy/VPI handles.
5. Implement absolute and relative lookup by name.
6. Implement top, scope, module, instance, and child traversal.
7. Implement port, net, variable, parameter, primitive, path, and timing objects.
8. Implement scalar, vector, real, string, strength, and delay reads.
9. Implement deposit, force, release, and scheduled value updates.
10. Implement indexed and iterator-style acc_next_* traversal.
11. Implement path-delay and timing-check access.
12. Implement value-change-link callback registration.
13. Implement callback cancellation, ordering, and re-entry containment.
14. Preserve handle and callback validity across simulation safe points.
15. Share values, scopes, and work areas coherently between TF and ACC.
16. Prove ACC/VPI views refer to the same simulation objects.
17. Define deterministic PLI behavior under future parallel execution.
18. Reject unsupported vendor names with stable diagnostics.
19. Run the full TF/ACC engine, artifact, cache, and platform corpus.
20. Run standard batch closure and declare legacy IEEE PLI complete.

### Batch 183 - VHDL-2019 syntax, types, interfaces, and expressions

1. Build a private-reference-derived, independently worded 2008-to-2019 clause inventory.
2. Add VHDL-2019 enum, manifest, CLI, artifact, and cache identities.
3. Implement revised lexical, grammar, and conditional-analysis behavior.
4. Implement the 2019 protected-type changes.
5. Implement unspecified types and their inference constraints.
6. Enforce the 64-bit minimum predefined INTEGER range.
7. Parse and model interface view declarations.
8. Implement record views and nested view composition.
9. Implement view-based port declarations and associations.
10. Implement subtype and direction rules within complex interfaces.
11. Implement conditional expressions and their contextual typing.
12. Expose result-array constraints inside functions.
13. Implement revised dynamically allocated storage semantics.
14. Implement sequential block statements and nested declarative regions.
15. Add all new predefined attributes and legality rules.
16. Apply revised overload, visibility, and conformance rules.
17. Prevent every 2019 construct from leaking into older profiles.
18. Round-trip all new semantic forms through objects and designs.
19. Add positive, negative, recovery, and profile-differential tests.
20. Run standard batch closure and freeze the VHDL-2019 frontend.

### Batch 184 - VHDL-2019 runtime, environment APIs, VHPI, and closure

1. Elaborate interface views and nested directional connections.
2. Execute view-based signal and variable updates.
3. Execute revised allocation and automatic reclamation behavior.
4. Execute sequential blocks across wait, return, and exception boundaries.
5. Implement the standard simulator API additions.
6. Implement standard data and time APIs.
7. Implement standard directory APIs.
8. Implement standard environment APIs.
9. Implement current-file, line, and call-path APIs.
10. Implement the standardized PSL API.
11. Implement report/assert statement APIs.
12. Implement the reflection API and reflected type/value model.
13. Update predefined packages and governed package compilation.
14. Implement revised tool, conditional-analysis, and protection directives.
15. Update VHPI capabilities, information model, and property access.
16. Update VHPI callbacks, value access, tool execution, and headers.
17. Integrate VHDL-2019 constructs with code and PSL coverage.
18. Prove interpreter, LLVM, debug, artifact, cache, and mixed-language behavior.
19. Close every active VHDL-2019 clause row and publish independent documentation.
20. Run standard batch closure and declare full VHDL-2019 support complete.

### Batch 185 - SystemVerilog-2023 frontend, data model, classes, and processes

1. Build a private-reference-derived, independently worded 2017-to-2023 clause inventory.
2. Add SystemVerilog-2023 enum, manifest, CLI, artifact, and cache identities.
3. Implement keyword, tokenization, preprocessing, and lexical changes.
4. Implement revised design-unit and scheduling declarations.
5. Implement scalar, integral, literal, and type-system changes.
6. Implement packed and unpacked aggregate changes.
7. Implement string, event, handle, and dynamic-object changes.
8. Implement class declaration and inheritance changes.
9. Implement parameterized-class and specialization changes.
10. Implement constructor, method, virtual, and lifetime changes.
11. Implement process, fork/join, and process-control changes.
12. Implement assignment and assignment-pattern changes.
13. Implement streaming and aggregate assignment changes.
14. Implement operator and expression changes.
15. Implement procedural statement changes.
16. Implement task, function, and argument changes.
17. Implement clocking and interprocess synchronization changes.
18. Prevent 2023 semantics from leaking into older profiles.
19. Prove new forms through semantic, artifact, and cache round trips.
20. Run standard batch closure and freeze the SystemVerilog-2023 core frontend.

### Batch 186 - SystemVerilog-2023 verification, hierarchy, and timing

1. Implement immediate-assertion revisions.
2. Implement concurrent-assertion revisions.
3. Update the formal concurrent-assertion execution model.
4. Implement checker revisions.
5. Implement constrained-random and solver revisions.
6. Implement functional-coverage revisions.
7. Implement utility system-task/function revisions.
8. Implement file and input/output task revisions.
9. Implement compiler-directive revisions.
10. Implement module and hierarchy revisions.
11. Implement program-block revisions.
12. Implement interface, modport, and virtual-interface revisions.
13. Implement package, import, export, and lookup revisions.
14. Implement generate-construct revisions.
15. Implement gate, switch, and UDP revisions.
16. Implement specify-block and timing-check revisions.
17. Implement SDF backannotation revisions.
18. Implement configuration and protected-envelope revisions.
19. Prove verification and timing behavior across interpreter and LLVM engines.
20. Run standard batch closure and freeze the design/verification surface.

### Batch 187 - SystemVerilog-2023 foreign APIs and complete closure

1. Implement DPI declaration and runtime revisions.
2. Update the standard DPI C layer and svdpi.h.
3. Implement revised foreign-code inclusion and context behavior.
4. Reconcile PLI/VPI overview rules with the v3 plugin model.
5. Update the complete VPI object model.
6. Implement every revised or added VPI routine.
7. Implement the assertion API.
8. Validate the complete standardized coverage API against Batch 180.
9. Implement the data-read API.
10. Update normative VPI and compatibility headers.
11. Update standard package behavior.
12. Update the standardized random-distribution implementation.
13. Apply normative syntax, keyword, and deprecation annex requirements.
14. Prove legacy TF/ACC interaction with the 2023 profile.
15. Prove code and functional coverage for new constructs.
16. Prove DPI/VPI callbacks across all scheduler phases.
17. Prove artifact, cache, checkpoint, debug, and trace behavior.
18. Prove independently authored C/C++ foreign applications on both platforms.
19. Close every active SystemVerilog-2023 clause row.
20. Run standard batch closure and declare full SystemVerilog-2023 support complete.

### Batch 188 - v3.0.0 integration and release

1. Audit zero unresolved rows across coverage, PLI, VHDL, and SystemVerilog.
2. Freeze v3 manifest, ABI, object, design, checkpoint, and cache schemas.
3. Prove deterministic rejection of all versioned v2 inputs.
4. Requalify every retained older HDL standard profile.
5. Run the complete HDL code/functional/PSL coverage corpus.
6. Run the complete TF/ACC PLI corpus.
7. Run the complete VHDL-2019 corpus.
8. Run the complete SystemVerilog-2023 corpus.
9. Qualify mixed-language and foreign-interface composition.
10. Qualify interpreter, LLVM O0-O3, and Debug equivalence.
11. Qualify artifacts, caches, checkpoints, traces, and debugger observations.
12. Complete fuzz, malformed-input, resource, and security readiness.
13. Freeze Linux and Windows warning-audit ownership.
14. Freeze hosted platform and toolchain definitions.
15. Publish v3.0 examples, user guides, API references, and known limitations.
16. Prepare deterministic source and existing archive artifacts.
17. Complete licenses, SBOM, provenance, and private-reference exclusion audits.
18. Freeze the v3.0 release record and exact tag message.
19. Set every product/version/package identity to 3.0.0.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.0.0.

## v3.1.0

### Batch 189 - parallel execution foundation and elaboration

1. Register parallel correctness, determinism, resource, and performance obligations.
2. Add separate elaboration/simulation job controls while preserving build -j.
3. Add deterministic and throughput execution-policy types.
4. Implement a bounded worker pool with cancellation and exception propagation.
5. Assign canonical task and diagnostic sequence identities.
6. Freeze immutable semantic inputs before parallel work begins.
7. Construct an elaboration dependency DAG.
8. Parallelize independent library and root preparation.
9. Parallelize independent top-level elaboration.
10. Parallelize safe instance-subtree elaboration.
11. Express generic, parameter, configuration, and binding dependencies.
12. Isolate per-task hierarchy and symbol-table deltas.
13. Validate deltas before publication.
14. Commit hierarchy deltas in canonical order.
15. Sort diagnostics independently of completion order.
16. Bound queues, memory, cancellation latency, and worker shutdown.
17. Serialize foreign elaboration hooks through the coordinator.
18. Prove equivalence at one, two, four, and eight workers.
19. Establish retained elaboration performance baselines.
20. Run standard batch closure and freeze parallel elaboration.

### Batch 190 - deterministic parallel simulation

1. Build process/signal conflict and dependency graphs.
2. Partition elaborated designs into scheduler regions.
3. Add region-local queues and worker ownership.
4. Preserve active, inactive, nonblocking, reactive, and postponed phases.
5. Add explicit phase barriers and epoch advancement.
6. Stage signal writes and resolutions per region.
7. Commit updates with canonical event identities.
8. Preserve timed-event and delta-cycle order.
9. Parallelize safe process execution.
10. Preserve wait, event, mailbox, semaphore, and process-control semantics.
11. Partition random streams independently of worker count.
12. Preserve report, file-I/O, and diagnostic order.
13. Use deterministic per-worker coverage shards.
14. Preserve trace and callback sequence identities.
15. Coordinate DPI, PLI, VPI, VHPI, and SystemC calls.
16. Quiesce all workers at debugger and control safe points.
17. Serialize worker-independent logical scheduler state.
18. Prove checkpoint/restart equivalence across worker counts.
19. Run race, deadlock, cancellation, and ThreadSanitizer stress.
20. Run clean Debug/Release, sanitizer, and hosted monitoring closure.

### Batch 191 - throughput policy, performance gates, and v3.1 release

1. Implement an explicit throughput-oriented work-stealing scheduler.
2. Permit variation only where the language standards leave order unconstrained.
3. Preserve all constrained event-region and update semantics.
4. Add automatic worker selection with jobs=1 as the serial reference.
5. Add independent elaboration and simulation overrides.
6. Qualify multiple roots and mixed-language partitions.
7. Qualify SDF, VITAL, timing checks, and assertions.
8. Qualify UVM, functional coverage, and code coverage.
9. Qualify every native extension family.
10. Qualify traces, files, callbacks, checkpoints, and debugger safe points.
11. Contain worker failures and publish no partial epoch.
12. Add 1/2/4/8-worker benchmark orchestration.
13. Record variance-qualified benchmark-specific scaling floors.
14. Add oversubscription and small-design overhead gates.
15. Add CPU, memory, queue, and shutdown resource ceilings.
16. Publish deterministic-versus-throughput guidance and examples.
17. Close all parallel execution inventory rows.
18. Freeze v3.1 ABI/schema and release records.
19. Set every release identity to 3.1.0.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.1.0.

## v3.2.0

### Batch 192 - performance lowering profile and frame elimination

1. Register observable/performance equivalence and incompatibility obligations.
2. Add observable and explicit performance lowering profiles.
3. Keep observable lowering as the default.
4. Restrict performance lowering to the compiled LLVM engine.
5. Include lowering profile and capabilities in artifact/cache identities.
6. Reject conflicting capabilities before lowering begins.
7. Perform process-local liveness and escape analysis.
8. Separate persistent state from transient values.
9. Promote eligible scalar locals to SSA values.
10. Promote eligible strings and containers.
11. Optimize class, access, and aggregate temporaries.
12. Retain only values live across suspension points.
13. Compact callable and suspended-task frames.
14. Remove process-local debugger materialization.
15. Remove runtime checkpoint serialization hooks.
16. Preserve code and functional coverage instrumentation.
17. Preserve global signal observation and tracing.
18. Diagnose debugger, local-trace, and save/restore incompatibilities.
19. Prove semantic equivalence across representative language constructs.
20. Run standard batch closure and freeze the lowering-profile contract.

### Batch 193 - fast runtime paths, benchmarks, and v3.2 release

1. Add direct compiled access to eligible signal and process state.
2. Specialize scheduler dispatch for lowered process capabilities.
3. Fuse safe SimIR operation sequences.
4. Fold and specialize runtime-invariant metadata.
5. Optimize callable entry, return, and copy semantics.
6. Optimize container and aggregate hot paths.
7. Optimize class and virtual-dispatch hot paths.
8. Retain governed foreign-call boundaries.
9. Retain coverage point identities through LLVM optimization.
10. Retain global trace identities through optimized lowering.
11. Integrate performance lowering with deterministic parallel execution.
12. Integrate it with throughput-oriented execution.
13. Allow direct LLDB-native debugging without HDL-local observation.
14. Reject coordinated HDL/native handoff for performance-lowered designs.
15. Establish benchmark-specific speed and memory gates.
16. Guard observable-mode and small-design non-regression.
17. Publish capability, failure, and selection documentation.
18. Close all performance-lowering inventory rows and release records.
19. Set every release identity to 3.2.0.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.2.0.

## v3.3.0

### Batch 194 - LLDB launch/attach and native symbol infrastructure

1. Register native-debugger security, lifecycle, and platform obligations.
2. Standardize native plugin load/unload notifications.
3. Record plugin path, build ID, ABI, source map, and symbol metadata.
4. Add fsim native-debug launch mode.
5. Add PID-based attach mode.
6. Add stop-on-plugin-load selection.
7. Discover and validate exact LLDB 22.1.8.
8. Generate deterministic LLDB command scripts without shell interpolation.
9. Normalize source-path substitution and symbol search paths.
10. Propagate fsim arguments, environment, working directory, and exit status.
11. Implement Linux LLDB launch.
12. Implement Linux LLDB attach and detach.
13. Provision Windows LLDB from the matching LLVM 22.1.8 CLANG64 toolchain set.
14. Implement native Windows LLDB launch.
15. Implement native Windows LLDB attach and detach.
16. Validate PE/COFF GNU-ABI symbols, stack unwinding, threads, and breakpoints.
17. Handle Unicode and whitespace-containing Windows paths.
18. Contain missing debugger, permission, timeout, and early-process-exit failures.
19. Add scripted Linux and Windows launch/attach tests.
20. Run standard batch closure and freeze the LLDB platform bridge.

### Batch 195 - coordinated HDL/native handoff and v3.3 release

1. Add scheduler-wide native-debug quiescence.
2. Publish safe foreign-entry and foreign-return events.
3. Add native handoff commands to the HDL debugger.
4. Add plugin, symbol, and boundary breakpoint selection.
5. Integrate DPI-C handoff.
6. Integrate TF/ACC PLI handoff.
7. Integrate VPI handoff.
8. Integrate VHPI handoff.
9. Integrate SystemC handoff.
10. Integrate the stable v3 fsim plugin ABI.
11. Resume all workers only after LLDB releases the boundary.
12. Preserve deterministic and throughput policy guarantees.
13. Support direct native debugging of performance-lowered designs.
14. Require observable lowering for coordinated HDL/native handoff.
15. Contain native exceptions, crashes, detach, unload, and reload.
16. Correlate HDL source points, native frames, time, delta, and plugin identity.
17. Prove every extension family on Linux and Windows LLDB.
18. Publish SDK helpers, LLDB examples, and troubleshooting guidance.
19. Set every release identity to 3.3.0 and close debugger inventory rows.
20. Run final local/hosted qualification, commit, push, tag, and publish v3.3.0.

## v3.4.0

### Batch 196 - DEB, RPM, and Inno Setup construction

1. Register package contents, platforms, dependencies, and installation ownership.
2. Build one canonical self-contained LLVM-enabled Release staging tree.
3. Bundle required LLVM and LLDB 22.1.8 runtime components.
4. Define Linux and Windows installation layouts.
5. Generate the Ubuntu 24.04 x86-64 runtime DEB.
6. Generate the matching -dev SDK DEB.
7. Generate the matching debug-symbol DEB.
8. Implement DEB install, upgrade, conflict, and removal scripts.
9. Generate the Rocky/RHEL 9 x86-64 runtime RPM.
10. Generate the matching -devel SDK RPM.
11. Generate the matching debuginfo RPM.
12. Implement RPM install, upgrade, conflict, and removal scripts.
13. Create the LLVM-MinGW x86-64 Inno Setup definition.
14. Implement non-admin per-user installation as the default.
15. Implement explicit elevated all-users Program Files installation.
16. Add runtime and plugin-SDK installer components.
17. Add a separately installable Windows symbol package.
18. Implement optional PATH, discovery, upgrade, and clean uninstall behavior.
19. Build installed SDK consumers and native plugins on every platform.
20. Run standard batch closure and freeze package construction.

### Batch 197 - supply chain, signing, installer validation, and v3.4 release

1. Make unsigned package payload staging deterministic.
2. Freeze package names, versions, architectures, and dependency metadata.
3. Install complete LLVM/LLDB licenses and notices.
4. Generate package-level SPDX SBOMs.
5. Generate checksums and provenance attestations.
6. Add DEB signing and verification hooks.
7. Add RPM GPG signing and verification hooks.
8. Add Windows Authenticode signing and verification hooks.
9. Mark unsigned CI artifacts explicitly when credentials are absent.
10. Test clean Ubuntu 24.04 install, discovery, upgrade, and uninstall.
11. Test clean Rocky Linux 9 install, discovery, upgrade, and uninstall.
12. Test Windows per-user and all-users install, upgrade, and uninstall.
13. Build and execute offline C/C++ plugin SDK examples.
14. Debug packaged plugins with packaged LLDB on Linux and Windows.
15. Prove self-contained execution without development dependencies.
16. Reject downgrade, architecture mismatch, corruption, and partial installation.
17. Enforce package entry, size, permissions, and resource ceilings.
18. Publish installation, signing, support-matrix, and release documentation.
19. Set every product and package identity to 3.4.0.
20. Run final local/hosted/package qualification, commit, push, tag, and publish v3.4.0.
