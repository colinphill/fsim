<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 restart handoff

Read [implementation_plan_v2.md](implementation_plan_v2.md) first; it is the
authoritative v2 batch/status record. Preserve the completed v1 history in
`v1-resume.md`.

- Branch: `codex/v2`, tracking `origin/codex/v2`.
- Baseline: `1462f18`; annotated `v1.0.0` points to `6450599`.
- Current unit: Batch 136, exactly 20 changes, complete and ready for its one
  commit/push. Batch 137 non-project artifact phases are next. Batch 135 was
  committed and pushed as `e6a44fb`.
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
- Batch 134 adds `[elaboration].search_libraries` and repeated
  `--search-library`; command-line occurrences replace the manifest list. The
  parent library followed by first occurrences from that list is one complete
  ambiguity scope. Queries are lazy, so an unavailable configured library is
  diagnosed only when a reference needs the scope. Explicit targets bypass it.
  Exact-LLVM Debug passed 106/106 tests in 280.62 seconds and Release passed
  106/106 tests in 242.71 seconds. Source, diagnostic-catalog, inventory, and
  release gates pass. The batch is committed and pushed as one accumulated
  unit; no sanitizer or CI monitoring was run because Batch 134 is not a
  scheduled monitoring boundary.
- Batch 135 implements multiple aliased top-level roots sharing one scheduler,
  time domain, language-global state, trace namespace, and debugger session.
  Additive schema-2 `[[project.top]]` records and repeatable
  `--top ALIAS=TARGET` are implemented. The public elaborator resolves every
  root transactionally into one alias-prefixed design. SystemVerilog root-level
  packed global signals (including the conventional `glbl.GSR` pattern) are
  predeclared independently of manifest order; descendant shortcuts receive
  `FSIM-ELAB-ROOT-001`. Focused Debug evidence passes for interpreter and LLVM
  O0/O2 HDL execution, mixed VHDL/SystemVerilog and HDL/SystemC roots, two
  SystemC roots, ordered cache identity, alias-filtered VCD, debugger and
  callback behavior, and the synthetic `$root` C API hierarchy. Public docs,
  diagnostics, examples, and feature evidence are updated. Remaining work is
  closed: exact-LLVM Debug passed 106/106 in 284.56 seconds and Release passed
  106/106 in 243.62 seconds. The diagnostic/source/inventory/release gates pass
  with 1,635 diagnostics, 437 bounded C/C++ sources, 521 SPDX-owned artifacts,
  and 190 test/control files. The accumulated batch is committed and pushed
  once; no sanitizer or CI monitoring ran because Batch 135 is not a boundary.
- Batch 136 adds read-only logical-library mappings to relocatable `.fsimlib`
  directories containing portable precompiled HDL plus optional strictly
  fingerprinted host-native artifacts. The exact 20-change contract is in the
  official v2 plan. Begin by auditing existing parsed-design serialization,
  cache provenance, SystemC plug-in, and LLVM object-cache seams; do not reduce
  the feature to mapped source directories that must be reparsed.
  `[[library_map]]` and repeatable `--map-library LIBRARY=DIRECTORY` are now
  implemented with ordered replacement, manifest-relative normalization, and
  pre-I/O validation for duplicate, reserved, unsafe, and project-built
  collisions. Exact-LLVM Debug focused project and application-core tests pass
  after an eight-worker build.
  The new `fsim::library` artifact layer now owns deterministic format-1
  `fsim-library.toml` serialization, strict parsing, contained portable-unit
  paths, content checksums, standards, dependencies, and lazy metadata-only
  loading. Its focused exact-LLVM Debug test passes.
  Change 6's publisher validates the complete indexed payload set and
  checksums, stages beside the destination, installs with one rename, makes
  the tree read-only, refuses overwrite, and cleans up transactionally on
  failure. The public project build/export command is connected.
  Change 7 has a schema-1 `FSIMUNIT` codec using fixed little-endian scalar
  encodings and declaration-ordered traversal of the complete owning
  `frontend::DesignUnit` graph. It rejects incompatible/truncated/trailing,
  excessively nested, cyclic, and producer-absolute artifacts. A
  parameterized SystemVerilog module with packed aggregate, assignment
  pattern, function, ports, and executable process restores and reserializes
  byte-for-byte without preprocessing or parsing. Source relocation and
  semantic/HIR rehydration are integrated and covered for both HDL families.
  Recursive source-span relocation is also implemented: producer-absolute
  logical or physical names must have explicit mappings and are rewritten to
  contained artifact identities before serialization; unmapped absolute names
  reject. Focused round-trip and relocation tests pass after an eight-worker
  exact-LLVM Debug build.
  Change 6 is complete: the public `fsim::app::export_library` API and
  repeatable build-only `--export-library LIBRARY=DIRECTORY` surface recheck
  exact source digests, serialize the selected logical library, include
  checksummed relocatable source text, and use the tested staging publisher.
  Focused exact-LLVM Debug library and application-core tests pass.
  Change 8 is complete for portable HDL units: required mapped payloads are
  checksum-verified, deserialized directly into the owning candidate design,
  and projected into valid semantic/HIR state without preprocessing or parser
  entry. A local SystemVerilog consumer now builds through a mapped exported
  child. Lazy selection leaves a missing mapped directory unopened for a
  qualified local top without hierarchy queries. Declared dependencies load
  depth-first with missing/cycle checks, and mapped metadata/unit/logical-source
  identities now contribute relocatable cache provenance.
  Change 7 is complete with direct owning-unit restoration and semantic/HIR
  reprojection demonstrated for exported SystemVerilog and VHDL units. Change
  11 is complete: mapped candidates participate in qualified top selection,
  cross-language child inference, language-specific identity, and complete
  local-plus-mapped ambiguity diagnostics. Change 13 is complete: mapped
  parameter specialization and package values survive restoration, one
  simulation can elaborate independent local and mapped aliased roots with
  distinct specializations, VHDL mapped configurations select their named
  architecture, and mixed-language inference plus boundary validation passes.
  Change 10 is complete with unavailable/corrupt/incompatible artifacts opened
  only on an effective query and checksum failure contained transactionally;
  the same corrupt mapping stays inert when unused. Change 12 is complete with
  declared depth-first dependency loading plus positive order, missing-mapping,
  and deterministic cycle evidence.
  Changes 9 and 15 are complete. After moving a `.fsimlib`, logical
  `sources/...` identities remain in the semantic model and a mapped source
  breakpoint sets and hits. Design and specialization cache keys remain equal,
  the relocated warm build hits cache, artifact path/size/permission/time
  snapshots remain unchanged, an append attempt is denied, and derived state
  stays in the consumer cache.
  Changes 14 and 16 are complete. Format-1 metadata indexes independently
  checksummed optional SystemC and LLVM native variants with exact runtime ABI,
  SystemC ABI or LLVM version/data-layout, compiler, target, CPU, feature,
  optimization, and cache-key identities. LLVM payloads are compiled from a
  temporary self-mapped portable artifact so producer and consumer logical
  source provenance is identical; an exact consumer obtains native object-cache
  hits. Exact SystemC plug-ins load from the read-only artifact. Deliberately
  incompatible LLVM and SystemC identities are ignored: LLVM recompiles the
  portable unit and SystemC recompiles the bundled source into the consumer
  cache. Accepted fingerprints participate in design and specialization/native
  provenance without using absolute artifact paths.
  Change 17 is complete. Build results and simulations retain ordered selected
  library provenance; normal build output reports mapped libraries; Tcl
  project/build dictionaries expose configured mappings and selected metadata;
  and append-only public C and C++ inspection surfaces report library name,
  metadata digest, unit count, native admission, kind, and fingerprint. Direct
  C, C++, Tcl, and C-header assertions pass. Mapping-only manifests are now a
  supported public project form. Changes 18-19 are complete with positive
  native reuse/fallback, both HDL families, mixed hierarchy, multiple roots,
  relocation/debugger/cache, interpreter/VCD, and deterministic
  corruption/dependency/mapping failures. Format-1 SystemC publication
  rejects producer-only include paths, definitions, compiler/linker options,
  and external libraries rather than claiming an unreproducible portable
  fallback. The runnable producer/consumer tutorial exports and runs through
  a mapped library to tick 2. Exact-LLVM Debug focused project, artifact,
  SystemC compiler, LLVM, application, Tcl, and C/C-header API tests pass 8/8;
  focused source/catalog/legality/differential/inventory/release gates pass
  7/7 with 1,643 diagnostics, 445 bounded sources, 534 SPDX-owned artifacts,
  1,082 execute rows, 4,328 evidence cells, and 95 runtime evidence owners.
  Change 20 is complete: exact-LLVM Debug passed 107/107 in 292.73 seconds and
  Release passed 107/107 in 251.81 seconds after eight-worker builds. Both full
  suites include source/catalog/inventory/installed-public/release gates. No
  sanitizer or CI monitoring ran because Batch 136 is not a boundary.
- Build every target with at least eight workers. Changes 1-19 accumulate in
  one worktree and Change 20 owns full Debug/Release gates, documentation, one
  commit, and one push. Do not run sanitizers or monitor CI in Batch 136;
  sanitizers remain reserved for Batch 140.
