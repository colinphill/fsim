<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 restart handoff

Read [implementation_plan_v2.md](implementation_plan_v2.md) first; it is the
authoritative v2 batch/status record. Preserve the completed v1 history in
`v1-resume.md`.

- Branch: `codex/v2`, tracking `origin/codex/v2`.
- Baseline: `1462f18`; annotated `v1.0.0` points to `6450599`.
- Current unit: Batch 142, exactly 20 changes, complete from clean pushed Batch
  141 closeout `0d3be3e`. Its authoritative VITAL path/wire/pulse contract and
  per-change status are recorded in `implementation_plan_v2.md`. Changes 1-20
  are complete; Batch 143 is the next implementation unit. Batch 142 is not a
  CI-monitoring batch.
- Batch 142 accumulated work materializes all three public path-record/array
  families and lowers scalar signal delay, all three wire-delay profiles, and
  all three path-delay profiles to one append-only `VitalDelay` operation.
  The common scheduler covers static and null path choices, shortest remaining
  delay, 01/01Z transitions, custom maps, default suppression, all four glitch
  modes, fast/negative preemption, checked time arithmetic, and independent
  X/report controls. Focused interpreter, LLVM O0/O2, debug, cold/warm cache,
  runtime-state, `.fsimobj`, relocated `.fsimdesign`, callback, diagnostic, and
  VCD evidence passes. The JIT table extends from 560 to 568 bytes with
  `vital_delay` at offset 560; the prior prefix is unchanged. Documentation,
  inventories and focused gates pass at 1,688 diagnostics, 476 bounded
  sources, 566 SPDX-owned artifacts, 1,099 execute rows, 4,396 evidence cells,
  343 evidence paths, and 99 runtime owners. Exact-LLVM Debug and Release pass
  111/111 tests after eight-worker builds, in 135.60 and 109.60 seconds
  respectively. Source, catalog, inventory, installed-public-contract, Windows
  ABI, differential, and release-candidate gates pass. Do not run a sanitizer
  or inspect hosted CI for this non-monitoring batch.
- Batch 141 Changes 1-20 are complete. Clean-room
  VITAL timing metadata, exact nine-state edge matching, persistent timing
  state, all five timing-check procedures, delayed sampling, Trigger-driven
  skew deadlines, flag/report semantics, and catalog-ready negative profiles
  execute in the interpreter and LLVM O0/O2. The integration fixture also
  exercises both setup/hold profiles, recovery/removal, period/pulse, and both
  skew phases with cold/warm native-cache and VCD equivalence. All four
  state-table profiles cover transition/static symbols, first-row priority,
  retention, no-match X, Z output, null input, zero states, and both vector
  directions. Explicit `.fsimobj`/`.fsimdesign` standalone compiled execution,
  runtime-state round trips, debugger mode, callbacks, and VCD match the source
  interpreter. Architecture, language support, diagnostics, feature matrix,
  inventory, and restart records are synchronized. Exact-LLVM Debug and Release
  each pass 110/110 tests after eight-worker builds, in 141.34 and 114.76
  seconds. The reviewed inventory and release baselines are 1,683 diagnostics,
  473 bounded sources, 563 SPDX-owned artifacts, 1,097 execute rows, 4,388
  evidence cells, 340 evidence paths, and 97 runtime owners. Source, catalog,
  inventory, installed-public-contract, Windows ABI, differential, and
  release-candidate gates pass. No sanitizer or hosted CI monitoring ran
  because Batch 141 is not a scheduled boundary.
- Batch 142 preserved one accumulated worktree through Changes 1-19. It owns
  the three path-record/array families, three `VitalPathDelay` profiles, three
  `VitalWireDelay` profiles, `VitalSignalDelay`, path selection, transition and
  output-map delay selection, all four glitch modes, pulse rejection,
  preemption controls, diagnostics, artifacts, both engines, callbacks,
  debugger, and VCD. Change 20 completed the full exact-LLVM Debug/Release
  gates, documentation closeout, one commit, and one push. Do not run a
  sanitizer or inspect hosted CI for this batch.
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
- Batch 137 implements manifest-free, explicitly scripted `compile`,
  `elaborate`, and `simulate` phases. Its exact public syntax, `.fsimobj` and
  `.fsimdesign` artifact contracts, portable HDL-only boundary, provenance,
  positive/negative coverage, and Change 20 gates are recorded in
  `implementation_plan_v2.md`. Preserve the clean `461ffae` baseline and begin
  with CLI command separation; SystemC inputs must receive an actionable
  Batch 138 diagnostic rather than being partially serialized.
  Change 2 is complete: the three commands parse and dispatch without manifest
  discovery, phase paths are absolute/normalized, option conflicts and missing
  required inputs reject, and SystemC compile is routed to Batch 138. The
  focused exact-LLVM Debug application test passes after an eight-worker build.
  Change 3 is complete: canonical little-endian format-1 `fsim-object.bin`
  metadata records language/standard/library, compilation mode/digest,
  definitions, contained include roots, source indexes, owning-unit indexes,
  and independent SHA-256 payload identities. The publisher validates the
  exact payload set, refuses overwrite, stages transactionally, installs by one
  rename, and makes the `.fsimobj` tree read-only. Malformed, truncated,
  trailing, unsafe-path, checksum, overwrite, and round-trip coverage passes in
  `fsim.artifact.object` after an eight-worker build.
  Change 4 is complete: `fsim compile` performs real VHDL or
  Verilog/SystemVerilog analysis with explicit language, standard, library,
  definitions, include roots, and compilation-unit policy; it revalidates
  checked source bytes, relocates source identities, serializes every owning
  unit, and publishes the requested object without manifest discovery. The
  production CLI object is loaded and its portable unit is deserialized in the
  focused application test; overwrite is rejected. The focused object and
  application suite passes 2/2 after an eight-worker exact-LLVM Debug build.
  Changes 5-6 are complete. Object metadata now rejects any stored compilation
  digest inconsistent with its ordered inputs and unit index. Publication is
  exact-set, checksum-validated, staged, atomically installed, read-only, and
  overwrite-safe. `fsim::app::load_objects` consumes repeated objects in CLI
  order, verifies every source and unit, gives each object's contained sources
  a digest-qualified namespace, restores portable owning units without the
  producer source files, and rebuilds valid semantic and HIR projections.
  Two independently compiled SystemVerilog objects merge in declaration order;
  a corrupt unit and a repeated object reject transactionally. The focused
  object/application suite passes 2/2 after eight-worker builds. Change 7 is
  complete: a production VHDL compile is split into independent package and
  dependent design objects; declaration order loads, reversed order fails,
  identifiers are case-normalized, and mismatched logical-library ownership
  rejects. A macro defined in one SystemVerilog object does not affect the next
  independently compiled object. The focused application test passes after an
  eight-worker build. Change 8 now defines the standalone `.fsimdesign`
  contract and its complete executable/debug payload boundary.
  Change 8 is complete. Canonical little-endian format-1 `fsim-design.bin`
  records runtime ABI, roots and selected identities, search scope, bindings,
  timing/seed/optimization policy, ordered object content identities, cache and
  specialization keys, state counts, and independently checksummed required
  runtime, semantic, and DesignIR payloads. Its design digest excludes producer
  paths and covers all compatibility/provenance fields. Publication is exact,
  transactional, overwrite-safe, atomically installed, and read-only;
  round-trip, truncation, trailing data, inconsistent digest, bad payload,
  overwrite, and write-denial coverage passes in `fsim.artifact.design` after
  an eight-worker build. Changes 9-10 are complete. Production `fsim
  elaborate` loads repeated objects, resolves the requested roots through the
  ordinary elaborator, and publishes the standalone design transactionally.
  Canonical state codecs preserve the complete semantic model, runtime
  processes/signals and all SimIR operation alternatives, DesignIR,
  specialization keys, and debug/source identity without producer-absolute
  paths. The restored payloads reserialize byte-for-byte, validate against
  each other, and execute to the expected stop time while both the producer
  sources and object directories are hidden. Focused exact-LLVM Debug object,
  design-artifact, and application tests pass 3/3 after eight-worker builds.
  Changes 11-12 are complete. The production `simulate` path consumes only the
  checksummed design payloads: it runs successfully with every producer source
  and object hidden, while checksum-corrupt, missing-payload, and incompatible
  runtime-ABI copies fail in the loader before scheduler construction. The
  same artifact passes interpreter, optimized LLVM, and debug/O0 execution;
  duration, max-delta, seed, fixed-delay compatibility, VCD path, and trace
  filtering are connected and covered. A mismatched delay request rejects
  because delay selection is fixed at elaboration. Focused exact-LLVM Debug
  application coverage passes after eight-worker builds. Changes 13-14 are
  complete. Object/design trees remain read-only; standalone `--cache`,
  `--file-root`, and trace paths place LLVM objects, HDL file state, and VCD
  output under explicit consumer locations. The design digest now salts native
  module identity in addition to ordered object/specialization provenance and
  LLVM's ABI/host/options fingerprint. Focused cold/warm evidence records
  miss/store then hit, while debug/O0 and a changed design digest miss
  independently. Changes 15-16 are complete. A new scripted-phase fixture
  compiles separate VHDL and SystemVerilog objects containing package/context,
  generic/configuration, parameter specialization, inferred cross-language
  hierarchy, and two roots. Restored interpreter/LLVM state, callbacks,
  relative semantic/debug sources, final values, stop time, and two-root VCD
  agree. IEEE projections now use stable `fsim-standard/...` logical source
  names and consumer-local backing paths instead of installation absolutes.
  SystemC compile rejects before publication with an actionable Batch 138
  diagnostic. Changes 17-18 are complete. Public C++ `compile_artifact`,
  `elaborate_artifact`, object/design loaders, and metadata-only inspection
  records cover phase/schema/ABI, language/library, roots, digests, units,
  processes, and compatibility without changing the v1 C ABI. Direct API and
  production coverage spans VHDL-2008, Verilog-2005, SystemVerilog-2017,
  relocation, mixed/multi-root hierarchy, package/context/configuration,
  generic/parameter specialization, interpreter, LLVM O0/O2, cold/warm cache,
  debugger-mode execution, callbacks, and VCD. Change 19 now closes the
  remaining negative matrix and public documentation/inventory work.
  Change 19 is complete: missing/ambiguous resolution, duplicate/reordered
  objects, option conflicts, schema/checksum/identity/ABI corruption,
  overwrite/write attempts, and partial publication are covered across the
  focused artifact/application suites. CLI help, README, architecture,
  language support, diagnostics, CM-088, the executable non-project tutorial,
  and inventories are synchronized. Focused catalog/source/inventory and
  installed-public-contract gates pass with 1,653 diagnostics, 459 bounded
  sources, 549 SPDX-owned artifacts, and 196 test/control files. Change 20 now
  owns full exact-LLVM Debug/Release and release gates, one commit, and one
  push. Change 20 is complete: both exact-LLVM configurations built with eight
  workers; Debug passed 109/109 tests in 138.17 seconds and Release passed
  109/109 in 106.09 seconds. The complete suites include artifact,
  source/catalog/inventory, installed-public, legality, differential, and
  release-candidate gates. The reviewed matrix now contains 1,083 execute rows,
  4,332 evidence cells, 331 evidence paths, and 96 runtime owners. No sanitizer
  or CI monitoring ran because Batch 137 is not a scheduled boundary.
- Build every target with at least eight workers. Changes 1-19 accumulate in
  one worktree and Change 20 owns full Debug/Release gates, documentation, one
  commit, and one push. Do not run sanitizers or monitor CI in Batch 139;
  sanitizers remain reserved for Batch 140.

- Batch 138 starts from clean pushed commit `31b983d`. Its 20-change contract
  implements true one-translation-unit `.fsimscobj` compilation and separate
  ordered `.fsimscplugin` linking, then integrates the linked native artifact
  into project builds and manifest-free HDL/SystemC elaboration. Standalone
  `.fsimdesign` publication must embed selected plug-ins and reload, re-elaborate,
  remap, and bind their native hierarchy without producer sources or object
  artifacts. Changes 1-11 now provide canonical host-specific object and
  plug-in metadata, transactional read-only publication, exact dependency
  revalidation, separate compiler/linker execution, sorted factory inventory,
  macro exports across translation units, typed schemas, legacy entry points,
  and cold/warm/selective cache evidence. Project builds route through the same
  cache while preserving shared registry identity. Manifest-free elaboration
  accepts repeated logical-library plug-ins; format-2 designs embed only
  selected images and reload them without producer sources or intermediate
  objects by reconstructing hierarchy paths and remapping runtime handles.
  Public phase inspection covers all four artifact types. Focused exact-LLVM
  Debug evidence passes `fsim.systemc.incremental`, `fsim.application`,
  `fsim.application.systemc_matrix`, `fsim.application.typed_boundaries`, and
  `fsim.systemc-portability-contract`. Preserve the accumulated worktree until
  the single Change 20 commit and push. Change 20 is complete: exact-LLVM Debug
  passed 110/110 in 137.08 seconds and Release passed 110/110 in 106.22 seconds,
  both after eight-worker builds. Focused post-review design/application reruns
  pass in both configurations. A manual execution of the documented
  three-language artifact flow also passed after all producer objects and the
  original plug-in were renamed, proving embedded SystemC-parent/VHDL-proxy
  reconstruction through tick 3. The current reviewed inventory is 1,659
  diagnostics, 463 bounded sources, 553 SPDX-owned artifacts, 197 test/control
  files, 1,084 execute rows, 4,336 evidence cells, 334 evidence paths, and 97
  runtime owners. No sanitizer or CI monitoring ran.

- Batch 139 starts from clean pushed commit `83dd339`. It closes the VHDL
  signal timing and driver attribute foundation needed by later VITAL work:
  `'last_active`, `'driving`, `'driving_value`, static-duration `'stable` and
  `'quiet`, and the implicit `'transaction` and `'delayed` signals. The exact
  completed 20-change contract is in `implementation_plan_v2.md`. Changes 1-19
  remained one accumulated worktree; Change 20 alone owned the full exact-LLVM
  Debug/Release gates, documentation closeout, commit, and push.
  Batch 139 runs neither sanitizers nor GitHub CI monitoring.
  Changes 2-13 and 15 are complete: the parser retains all remaining timing and
  driver attribute designators; `'last_active` reads the kernel's independent
  redundant-transaction-aware timestamp; and `'driving`/`'driving_value` use
  stable process driver regions plus exact two-, four-, and nine-state driver
  contributions. Change 15 is complete for these direct queries through an
  append-only ABI tail at offsets 512-536 and LLVM O0/O2 lowering. Existing
  ABI offsets remain unchanged. Exact-LLVM Debug focused frontend,
  elaboration, C-ABI, LLVM, expression application, and design-artifact tests
  pass after eight-worker builds. Static-duration `'stable` and `'quiet`, plus
  typed `'delayed` and toggling `'transaction`, are interned hierarchy-local
  implicit signals driven by scheduler-visible support processes. Redundant
  transactions use transaction sensitivity, delayed values use transport
  projection, and support processes loop after each sensitivity wake. Ordinary
  expressions, VHDL process sensitivity attributes, and `wait on` attributes
  now select the derived signal rather than the prefix signal. The focused
  exact-LLVM Debug frontend and expression application tests pass with matching
  interpreter/compiled values across early/late timing windows and redundant
  transaction cases. Changes 14 and 16-19 are now complete. The differential
  adds packed nine-state driver values, a resolved multi-driver prefix,
  several redundant transactions at one timestamp, process and wait
  sensitivities, and cold/warm native-cache reuse. The standalone
  `.fsimdesign` phase test restores a hierarchy-local implicit `'stable(1)`
  signal in both engines, reads it through the debugger, observes callbacks,
  and verifies its deterministic VCD name. Negative elaboration covers
  invalid/nonstatic/negative durations, transaction arity, and missing
  `'driving_value` ownership through cataloged `FSIM-ELAB-VHATTR-003` through
  `008` diagnostics. Architecture, language-support/VITAL dependency notes,
  feature matrix, test inventory, and the public README are synchronized.
  Source and diagnostic catalog gates pass after splitting signal-query
  declarations, runtime helpers, native callbacks, and frontend fixtures along
  existing structural boundaries. Change 20 is complete: exact-LLVM Debug
  passed 110/110 tests in 134.43 seconds and Release passed 110/110 in 107.71
  seconds, both after eight-worker builds. Source, diagnostic-catalog,
  inventory, installed-public-contract, and release gates pass. The reviewed
  inventory is 1,667 diagnostics, 468 bounded sources, 558 SPDX-owned
  artifacts, 198 test/control files, 1,092 execute rows, 4,368 evidence cells,
  335 evidence paths, and 97 runtime owners. No sanitizer or CI monitoring ran
  because Batch 139 is not a scheduled boundary.

- Batch 140 starts from clean pushed commit `e4de752`. It is the scheduled
  CI-monitoring boundary and introduces clean-room compiler-supplied
  `ieee.vital_timing` and `ieee.vital_primitives` interfaces, their public
  static types/constants, delay calculation helpers, result maps, logic,
  tri-state, mux, decoder, and truth-table function families. It does not copy
  upstream VITAL package text whose redistribution terms are not established.
  Later Batch 141 owns timing checks/state tables, Batch 142 owns path/wire
  delay and pulse rejection, and Batch 143 owns memory models and vendor-model
  compatibility closure. Preserve one accumulated worktree through Changes
  1-19. Change 20 alone owns the LLVM-disabled ASan/UBSan regression, full
  exact-LLVM Debug/Release gates, documentation closeout, one commit, one push,
  and inspection/repair of every non-documentation GitHub Actions job.
  Changes 2-19 are complete and Change 20 is current. Direct and
  context-expanded imports lazily inject both clean-room packages after
  `std_logic_1164`, reject collisions, and retain virtual source digests plus
  the `ieee-vital:2000:fsim-clean-room-v1` revision. Typed package metadata
  exposes the 12 transition literals, 01/01Z/01ZX physical-time arrays,
  unconstrained delay-array families, fixed logic vectors, output/result maps,
  table-symbol subtypes, and two-dimensional truth/state tables without
  flattening their nominal identities. Integer-element VHDL array layout is
  now legal, so `VitalDelayType01` retains its exact 128-bit two-time shape.
  Composite zero/default constants and nonzero TIME-array/map generics now
  retain exact wide values and deterministic specialization identities. The
  common VITAL lowerer executes delay extension/calculation, output/result
  maps, BUF/INV/IDENT, all four tri-state gates, arbitrary-width and fixed
  2/3/4 logic, MUX/MUX2/4/8, DECODER/2/4/8, and both static truth-table result
  profiles. The focused exact-LLVM Debug differential passes after eight-worker
  builds with all nine states, weak inputs, custom maps, ascending/descending,
  65-element, singleton and null reductions, pessimistic unknown selectors,
  first-row truth matching, nonnegative physical delays, O0/O2 interpreter/JIT
  parity, cold/warm cache, debugger locals, callbacks, and VCD. The existing
  explicit compile/object/elaborate/design/standalone simulation test now
  carries a VITAL consumer through `.fsimobj` and `.fsimdesign` in both engines
  and its trace. Cataloged `FSIM-ELAB-VITAL-001` through `009` diagnostics and
  negative profile/dimension/map/delay/dynamic-table/symbol cases pass. The
  architecture, language support, diagnostic catalog, feature matrix, and test
  inventory describe the exact Batch 140 boundary and reserve timing/state
  tables, path/wire delays, pulse rejection, and memory/vendor closure for
  Batches 141-143. Change 20's exact-LLVM Debug regression passes 110/110 in
  137.92 seconds and Release passes 110/110 in 115.48 seconds, including all
  source, diagnostic-catalog, inventory, installed-public-contract, and release
  gates. The current inventory is 1,676 diagnostics, 469 bounded sources, 559
  SPDX-owned artifacts, 1,095 execute rows, 4,380 evidence cells, 337 evidence
  paths, and 97 runtime owners. The LLVM-disabled ASan/UBSan regression passes
  107/107 in 291.02 seconds with leak detection disabled because the managed
  runner executes under ptrace. That gate repaired strict incremental SystemC
  plug-in linking of sanitizer-instrumented support code and corrected
  LLVM-disabled native-cache assertions. Batch 140 commit `527a031` is pushed
  on `codex/v2`; initial hosted run `30898580367` completed with all four Linux
  build/test jobs, Ubuntu ASan/UBSan, and frontend fuzz passing. All
  four MSVC jobs stopped at the same warnings-as-errors signed/unsigned
  optional comparison in `src/cli/driver.cpp`. Both Windows Clang jobs reached
  tests and exposed POSIX-only absolute-path and permission assumptions,
  case-insensitive producer-path relocation, an incremental SystemC
  `/WHOLEARCHIVE` option placed before `/link`, and one mixed-SystemC failure
  whose assertion hid its diagnostic. The current accumulated repair worktree
  corrects the directly diagnosed defects and exposes that remaining
  diagnostic. Eight-worker exact-LLVM Debug and Release
  builds pass the same 12-test focused gate, including artifacts,
  source/catalog, incremental SystemC, application, mixed SystemC hierarchy,
  Tcl, API, and MSVC/tool portability contracts. The post-repair exact-LLVM
  Debug and Release regressions pass 110/110 in 320.46 and 287.88 seconds. The
  exact final tree passes the same 14-test cross-platform repair gate in both
  configurations after eight-worker builds. Repair commit `5443c4b` is pushed.
  Replacement run `30903250986` completed with all four Linux build/test jobs,
  hosted ASan/UBSan, and frontend fuzz passing. Its plain and LLVM MSVC Debug
  builds progressed beyond the original warning but both fail
  with `C1128` because `application_design_artifact_codec.cpp` exceeds COFF's
  default section count in unoptimized builds. All four Windows jobs that reach
  tests fail the same incremental-link, main-application, mixed-SystemC, and
  Tcl tests: the first three share an overlong cache publication staging path,
  while the Tcl fixture embeds unescaped native separators in TOML. The current
  repair adds target-scoped `/bigobj`, shortens collision-safe staging names,
  writes the TOML fixture path with generic separators, and prints cached-link
  diagnostics before assertion. Both local exact-LLVM configurations build
  with eight workers and pass the five affected tests; the Debug source,
  catalog, inventory, installed-public, and Windows portability gates also
  pass. Repair commit `4bf9195` is pushed. Replacement run `30906493862`
  completed with all six non-Windows jobs green, both MSVC Debug builds past
  the former COFF failure, and every prior staging, incremental-link,
  mixed-SystemC, and Tcl failure cleared. Its Windows configurations converge
  on one remaining main-application abort at `application: non-project cli`:
  Windows retains the loaded plug-in DLL while the test renames its containing
  artifact. Plain and LLVM MSVC Debug additionally reach the old 900-second
  SystemC-matrix timeout; `fsim.application.scoped_locals` remains quick at
  0.54 and 1.69 seconds. The current focused repair leaves the loaded DLL at a
  stable path, makes only its artifact root owner-writable, and hides the
  required metadata so the producer remains unusable. It also raises the
  bounded matrix timeout to 1,200 seconds and gives the plain MSVC job the
  existing 70-minute LLVM Windows ceiling. The exact-LLVM Debug application,
  matrix, and both portability contracts pass locally in 20.87 and 59.22
  seconds; Release passes them in 19.92 and 54.48 seconds. A final string-only
  construction cleanup leaves the application passing in 20.74 and 19.95
  seconds. Repair commit `696be29` is pushed. Replacement run `30911069043`
  completes with all six non-Windows jobs green and proves the timeout repair:
  plain MSVC Debug passes the SystemC matrix in 1,014.68 seconds and LLVM MSVC
  Debug passes it in 1,057.46 seconds; scoped locals remain quick at 0.41 and
  1.69 seconds. All six Windows variants now fail only the producer-hiding
  checkpoint because the metadata file itself retains the artifact's
  read-only attribute. The current one-line functional correction makes that
  file owner-writable before renaming it; after eight-worker exact-LLVM builds,
  the Debug and Release application tests pass in 20.42 and 19.72 seconds.
  Repair commit `e4f11ce` is pushed. Run `30916363303` repeats the same coarse
  `0xc0000409` application checkpoint and is canceled by request before the
  matrix completes. Because that checkpoint covers all producer mutations,
  embedded loading, structural assertions, and simulation, it cannot identify
  the failed operation. The current diagnostic worktree uses error-code
  overloads and explicit labels for every rename and permission change, prints
  embedded-design diagnostics, and marks load, validation, and simulation
  completion. It builds warning-clean with eight workers and passes the
  exact-LLVM Debug and Release application tests in 20.04 and 19.16 seconds.
  Diagnostic commit `0d67c82` is pushed. Run `30918625659` shows all three
  producer renames, both permission changes, embedded-design load and
  structural validation, and the embedded simulation call complete before the
  abort; it is then canceled. The next diagnostic reports the post-simulation
  status, callback count, and signal value and marks each later non-project
  phase. A temporary Windows MSVC Debug workflow builds only
  `fsim_application_tests` with four hosted workers and runs only
  `^fsim.application$` verbosely. The expanded test builds warning-clean with
  eight local workers and passes exact-LLVM Debug and Release in 20.01 and
  19.49 seconds. Focused run `30920656747` reports correct embedded SystemC
  status, three callbacks, and value `00000101`, then validates object metadata,
  the portable unit, relocated objects, state round-trip, and HDL design
  publication before aborting at the first published-HDL-object relocation.
  Those object roots are also read-only on Windows. The current repair makes
  only the two roots writable, performs labeled error-code renames, and prints
  embedded HDL design-load diagnostics. Focused run `30921845380` reports
  Windows error 5 at the first directory rename after both permission changes
  succeed. The earlier portable-unit input stream still holds a child file open,
  so Windows locks the containing directory; close the stream immediately after
  reading it. The final local exact-LLVM Debug and Release application tests
  pass in 19.58 and 18.86 seconds. Focused Windows run `30922930843` passes the
  sole MSVC Debug `fsim.application` case in 10 minutes 29 seconds. Remove the
  temporary workflow is removed in `446a654`. Final normal run `30923945372`
  passes all 12 jobs: every Linux, Windows, sanitizer, and fuzz job is green.
  Change 20 and Batch 140 are complete.

- Batch 141 starts from clean pushed Batch 140 closeout `c5c8a5c` and completes
  all exactly 20 changes in one accumulated changeset. It materializes the
  remaining public VITAL timing/state types and implements exact nine-state
  edges, persistent setup/hold, recovery/removal, period/pulse, in-phase and
  out-phase skew checks, delayed sampling, Trigger deadlines, report/violation
  controls, and all four scalar/vector variable/signal state-table profiles.
  The implementation preserves state through interpreter and LLVM O0/O2,
  cold/warm native cache, debugger, callbacks, VCD, `.fsimobj`, `.fsimdesign`,
  relocation, and standalone execution. Positive and cataloged-negative VHDL
  integration coverage includes simultaneous boundaries, weak/unknown edges,
  first-row table priority, retention, no-match X, Z output, null inputs, zero
  states, and both vector directions. Exact-LLVM Debug passes 110/110 in 141.34
  seconds and Release passes 110/110 in 114.76 seconds after eight-worker
  builds. The source, diagnostic, inventory, installed-public, Windows ABI,
  differential, and release-candidate gates pass with 1,683 diagnostics, 473
  bounded sources, 563 SPDX-owned artifacts, 1,097 execute rows, 4,388 evidence
  cells, 340 evidence paths, and 97 runtime owners. The append-only JIT ABI
  retains its 544-byte compatible prefix and extends to 560 bytes. No sanitizer
  or hosted CI monitoring ran because Batch 141 is not a monitoring boundary.
  Batch 141 is committed and pushed as `0d3be3e`.
