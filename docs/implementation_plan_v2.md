<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 implementation plan

This is the authoritative batch and status record for fsim v2. The v1 release
is preserved by annotated tag `v1.0.0` at `6450599`; v2 development starts on
`codex/v2` from post-v1 checkpoint `1462f18`.

## Working cadence

- Every v2 implementation batch contains exactly 20 numbered changes.
- Before any numbered change in a batch begins, expand that batch into twenty
  exact entries, write a clean-base restart plan in `v2-resume.md`, commit and
  push that documentation-only checkpoint, and end the current working
  context. The fresh context must reread both documents and verify the saved
  branch tip before implementation. This precursor does not consume a numbered
  change or the batch's single implementation commit.
- Changes 1 through 19 accumulate in one recoverable worktree with focused
  warnings-as-errors builds and targeted tests.
- Change 20 owns the full exact-LLVM Debug and Release regressions,
  documentation, one commit, and one push.
- Local builds use at least eight workers.
- The LLVM-disabled sanitizer suite runs locally only, immediately before
  committing every tenth batch. Ordinary batches do not configure, build, or
  run sanitizer targets, and hosted CI excludes sanitizer instrumentation.
- Every tenth numbered batch is a non-documentation CI-monitoring boundary;
  Batch 150 is the next boundary. Documentation-only runs are not monitored.

## Batch 133 - automatic cross-language resolution - Complete

1. **Complete.** Tag v1.0.0, create and publish `codex/v2`, and establish the
   v2 plan/resume records and 20-change cadence.
2. **Complete.** Advance manifests to schema 2 and make binding targets
   optional in the project and elaboration APIs.
3. **Complete.** Add schema-1-to-schema-2 migration support, an actionable
   schema rejection, and the `fsim migrate --to 2` command surface.
4. **Complete.** Introduce the central immutable unit-candidate lookup and
   resolution-result model instead of first-match selection.
5. **Complete.** Define canonical identities and target-language case rules
   for VHDL, Verilog/SystemVerilog, and SystemC candidates.
6. **Complete.** Group project-built SystemC factory indexes by logical source
   library and reject same-library public-name duplicates.
7. **Complete.** Record the stringized `SC_FSIM_HDL_MODULE(Type)` proxy type as
   its inferred implementation name without adding hierarchy.
8. **Complete.** Route unqualified tops through the common resolver with
   `work` as their parent library while retaining qualified tops.
9. **Complete.** Replace same-language and first-match hierarchy target
   selection with deterministic candidate selection.
10. **Complete.** Infer VHDL children beneath Verilog/SystemVerilog parents.
11. **Complete.** Infer Verilog/SystemVerilog children beneath VHDL
    components after component-declaration visibility checks.
12. **Complete.** Infer SystemC exported factories beneath HDL parents.
13. **Complete.** Infer VHDL and Verilog/SystemVerilog implementations beneath
    SystemC HDL proxies.
14. **Complete.** Preserve explicit target overrides and admit
    resolver-only bindings for inferred cross-language inout instances.
15. **Complete.** Replace explicit-binding-required failures with
    deterministic missing-target diagnostics.
16. **Complete.** Diagnose same-library ambiguity across languages and
    duplicate candidate identities in deterministic order.
17. **Complete.** Reject inferred VHDL entities with multiple eligible
    architectures unless configuration or an explicit target selects one.
18. **Complete.** Validate interfaces only after unique resolution and include
    selected identities in native/cache provenance.
19. **Complete.** Migrate representative fixtures and the three-language
    example while retaining explicit override and legacy coverage.
20. **Complete.** Update public and release records and run the exact-LLVM
    Debug and Release regressions plus source, catalog, inventory, and release
    gates. Debug passed 106/106 tests in 129.35 seconds and Release passed
    106/106 tests in 99.88 seconds. The accumulated batch is committed and
    pushed once, without a sanitizer run or CI monitoring because Batch 133 is
    not a scheduled CI boundary.

## Batch 134 - configurable multi-library resolution - Complete

1. **Complete.** Establish this exact 20-change Batch 134 plan and restart
   status before implementation begins.
2. **Complete.** Add `[elaboration].search_libraries` to schema 2 with an
   empty default and ordered logical-library validation.
3. **Complete.** Expose the elaboration search list through the public project
   configuration model without changing source-library ownership.
4. **Complete.** Add repeatable `--search-library LIBRARY` options to every CLI
   command that performs elaboration.
5. **Complete.** Make any CLI search-library occurrences replace the manifest
   list while preserving their command-line order.
6. **Complete.** Canonicalize each effective scope as the parent logical library
   followed by first occurrences from the configured list.
7. **Complete.** Extend the public elaboration request surface to accept the
   immutable ordered search-library list.
8. **Complete.** Replace single-library candidate lookup with lazy per-library
   queries made only when an unqualified reference is resolved.
9. **Complete.** Search unqualified root tops across `work` plus the effective
   configured libraries while retaining qualified top selection.
10. **Complete.** Resolve Verilog/SystemVerilog child module names across the
    complete effective scope.
11. **Complete.** Resolve visible VHDL component names across the complete
    effective scope with VHDL case rules and architecture checks intact.
12. **Complete.** Resolve HDL-parent SystemC factory names across the complete
    effective scope.
13. **Complete.** Resolve SystemC HDL-proxy implementation names across the
    complete effective scope.
14. **Complete.** Preserve explicit qualified binding targets as authoritative
    selections which do not consult the search list.
15. **Complete.** Diagnose ambiguity across every candidate in the complete
    scope rather than selecting the first library with a match.
16. **Complete.** Extend missing-target diagnostics with the ordered effective
    scope and canonical candidates considered.
17. **Complete.** Diagnose an unavailable configured logical library only when
    a resolution query needs it; unused unavailable entries remain inert.
18. **Complete.** Include the effective search scope and selected logical
    library in specialization and native-cache provenance.
19. **Complete.** Add manifest, CLI, API, hierarchy, SystemC, example, and
    documentation coverage for ordered search, override, ambiguity, missing,
    duplicate, and lazy-unavailable behavior.
20. **Complete.** Update public/release records and run the exact-LLVM Debug
    and Release regressions plus source, catalog, inventory, and release gates.
    Debug passed 106/106 tests in 280.62 seconds and Release passed 106/106
    tests in 242.71 seconds. The accumulated batch is committed and pushed
    once, without a sanitizer run or CI monitoring because Batch 134 is not a
    scheduled CI boundary.

## Batch 135 - multiple top-level roots - Complete

1. **Complete.** Establish this exact 20-change Batch 135 plan and restart
   status before implementation begins.
2. **Complete.** Replace the singular project top setting with an ordered list
   of top specifications while migrating existing schema-2 manifests.
3. **Complete.** Add stable user-selected aliases for top roots and reject
   empty, duplicate, or hierarchy-unsafe aliases.
4. **Complete.** Add repeatable CLI top specifications with explicit aliases,
   preserving the manifest list unless the command line supplies replacements.
5. **Complete.** Extend the public build and elaboration APIs with immutable
   ordered root requests while retaining source compatibility for one root.
6. **Complete.** Resolve every root independently through the Batch 133/134
   language and library resolver contracts.
7. **Complete.** Diagnose missing, ambiguous, or duplicate root selections
   transactionally before constructing any hierarchy.
8. **Complete.** Elaborate all roots into one design with alias-prefixed
   hierarchy paths and no implicit connectivity between roots.
9. **Complete.** Share one scheduler and simulation time domain across all HDL
   and SystemC roots.
10. **Complete.** Share language library, package, configuration, and global
    signaling state across roots while preserving per-instance local state.
11. **Complete.** Make initialization, delta cycles, time advancement, and
    shutdown deterministic across the ordered root list.
12. **Complete.** Admit cross-root references only through language-defined
    global mechanisms and diagnose unsupported hierarchical shortcuts.
13. **Complete.** Merge all roots into one trace namespace rooted at their
    aliases, with deterministic VCD name and identifier allocation.
14. **Complete.** Expose every root in one debugger session with unambiguous
    aliased paths, breakpoints, stepping, inspection, and callbacks.
15. **Complete.** Execute multiple SystemC roots and mixed HDL/SystemC roots in
    one kernel without duplicate lifecycle callbacks or plug-in initialization.
16. **Complete.** Include the ordered aliased root set and each selected
    canonical identity in elaboration, specialization, and native-cache
    provenance.
17. **Complete.** Preserve single-root output, diagnostics, cache behavior, and
    C/C++ API compatibility as the degenerate multiple-root case.
18. **Complete.** Add positive execution coverage for HDL-only, SystemC-only,
    mixed-language, global-signaling, trace, debugger, callback, and cache
    scenarios under the interpreter and LLVM O0/O2.
19. **Complete.** Add negative and migration coverage for aliases, duplicate
    roots, partial resolution failure, unsupported references, and old
    single-top manifests, and update public documentation and examples.
20. **Complete.** Update public/release records, run exact-LLVM Debug and Release
    plus source/catalog/inventory/release gates, then commit and push once.
    Exact-LLVM Debug passed 106/106 tests in 284.56 seconds and Release passed
    106/106 in 243.62 seconds. The catalog covers 1,635 diagnostics, the source
    gate covers 437 authored C/C++ files, and the composed release-candidate
    gate passes with 521 SPDX-owned artifacts and 190 test/control files.
    Batch 135 is not a CI boundary and ran no sanitizer or CI-monitoring gate.

## Batch 136 - out-of-tree precompiled library mappings - Complete

1. **Complete.** Establish this exact 20-change Batch 136 plan and restart
   record before implementation begins.
2. **Complete.** Add ordered `[[library_map]]` schema-2 records containing a
   logical `library` name and directory `path`, with manifest-relative path
   resolution and source-compatible project APIs.
3. **Complete.** Add repeatable `--map-library LIBRARY=DIRECTORY` options whose
   first occurrence replaces manifest mappings for check, build, run, debug,
   and Tcl project loads.
4. **Complete.** Reject empty, duplicate, reserved, path-unsafe, or project-built
   logical-library collisions before any mapped directory is opened.
5. **Complete.** Define versioned `fsim-library.toml` metadata for a relocatable
   `.fsimlib` directory, including logical name, producer/runtime schemas,
   language standards, unit index, and content checksums.
6. **Complete.** Add a deterministic project build/export path that
   publishes a complete library into a staging directory and atomically
   installs it as a read-only `.fsimlib` artifact.
7. **Complete.** Serialize the supported owning VHDL, Verilog/SystemVerilog, and
   cross-unit semantic/HIR records into portable, endian-stable artifacts
   without retaining producer-absolute paths.
8. **Complete.** Restore mapped portable units directly into the immutable unit
   candidate index without preprocessing or reparsing their original sources.
9. **Complete.** Retain relocatable logical source identities and optional
   source text/line tables so diagnostics and debugger locations remain useful
   after the artifact directory moves.
10. **Complete.** Query mapped libraries lazily through the Batch 133/134
    resolver and diagnose unavailable, corrupt, incompatible, or renamed
    directories only when the effective search scope needs them.
11. **Complete.** Include mapped candidates in complete-scope ambiguity,
    VHDL case-insensitive identity, SystemVerilog/SystemC case-sensitive
    identity, architecture eligibility, and explicit-qualified selection.
12. **Complete.** Resolve mapped-library dependencies through declared logical
    library names, reject dependency cycles or missing mappings, and never
    search undeclared host directories implicitly.
13. **Complete.** Preserve generic/parameter specialization, configuration,
    package/context visibility, mixed-language target inference, boundary
    validation, and multiple-root selection for mapped portable units.
14. **Complete.** Admit optional host-specific SystemC plug-ins and LLVM native
    objects only when their exact ABI, compiler/LLVM, target, CPU-feature, and
    content fingerprints match; otherwise fall back to portable artifacts.
15. **Complete.** Keep mapped directories read-only during check, build,
    elaboration, simulation, cache maintenance, debugger, and trace use; place
    all derived state in the consuming project's cache.
16. **Complete.** Add mapped directory metadata, ordered unit/dependency
    identities, and accepted optional-native fingerprints to design and native
    cache provenance so edits, relocation, and compatibility fallback behave
    deterministically.
17. **Complete.** Expose mappings and selected mapped-unit provenance through
    build output, Tcl project/build dictionaries, and the public C/C++ project
    and session inspection surfaces without breaking legacy clients.
18. **Complete.** Add positive export/import coverage for VHDL, SystemVerilog,
    mixed hierarchy, multiple roots, relocation, source diagnostics, debugger,
    VCD, interpreter, LLVM O0/O2, cold/warm cache, and optional-native reuse.
19. **Complete.** Add negative coverage for duplicate/colliding mappings,
    unavailable-on-use directories, format/schema/checksum corruption,
    wrong-library metadata, missing/cyclic dependencies, incompatible native
    artifacts, write attempts, and transactional partial-load failure; update
    public documentation, examples, diagnostics, inventories, and this record.
    Exact-LLVM Debug focused project, artifact, SystemC compiler, LLVM,
    application, Tcl, and C/C-header API coverage passes 8/8. The focused
    source/catalog/legality/differential/inventory/release suite passes 7/7;
    its reviewed baseline is 1,643 diagnostic emissions, 445 bounded C/C++
    files, 534 SPDX-owned artifacts, 1,082 execute rows, 4,328 linked evidence
    cells, and 95 runtime evidence owners. The runnable precompiled-library
    tutorial exports, maps, resolves, and simulates its relocated child to
    tick 2.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, and release gates, then commit and push once. Exact-LLVM Debug
    passed 107/107 tests in 292.73 seconds and Release passed 107/107 in 251.81
    seconds. Both configurations built with eight workers. The complete suites
    include the source, catalog, inventory, installed-public-contract, and
    release-candidate gates. Batch 136 is not a CI boundary and ran no
    sanitizer or CI-monitoring gate.

## Batch 137 - explicit non-project artifact phases - Complete

1. **Complete.** Establish this exact 20-change Batch 137 plan and restart
   record before implementation begins.
2. **Complete.** Add first-class `compile`, `elaborate`, and `simulate`
   commands that do not discover or load `fsim.toml` and reject project-only
   option combinations.
3. **Complete.** Define a versioned, checksummed, endian-stable `.fsimobj`
   directory schema for one explicitly scripted HDL compilation unit,
   including language, standard, logical library, preprocessing inputs,
   source provenance, and portable owning units.
4. **Complete.** Implement `fsim compile --language LANGUAGE --standard REVISION
   --library LIBRARY --output PATH FILE...` with explicit include/define and
   compilation-unit controls for VHDL and Verilog/SystemVerilog. The production
   CLI publishes a portable object containing independently checksummed,
   relocated source and owning-unit payloads; focused exact-LLVM Debug object
   and application coverage passes after eight-worker builds.
5. **Complete.** Make compile publication transactional and read-only, refuse
   overwrite, revalidate every source digest, and avoid project analysis,
   elaboration, native compilation, or simulation side effects. Publication
   validates the exact payload set and compilation digest before staging,
   installs with one rename, leaves no failed destination, and creates no
   elaboration/native/simulation state.
6. **Complete.** Load repeated `--object PATH` inputs in declaration order,
   validate their complete checksummed payloads, and merge compatible
   compilation units without parsing original sources. The public C++ loader
   namespaces contained sources by compilation digest, restores owning units
   directly, rebuilds semantic/HIR projections, and rejects corrupt or
   duplicate inputs while the producer sources are absent.
7. **Complete.** Enforce VHDL analysis dependencies, case rules, duplicate-unit
   identity, logical-library ownership, and SystemVerilog compilation-unit
   isolation across independently produced objects. Focused coverage splits a
   compiled VHDL package from its dependent entity/architecture, accepts only
   dependency order, verifies normalized VHDL names and ownership, and proves
   preprocessor state does not leak between SystemVerilog objects.
8. **Complete.** Define a versioned, standalone `.fsimdesign` directory schema
   that records selected roots, search scope, bindings, resolution, delay mode,
   time resolution, seed policy, complete elaborated runtime state, semantic
   source tables, and specialization provenance. Format 1 uses canonical
   little-endian metadata, content-only object provenance, required runtime,
   semantic, and DesignIR payloads, exact cache/specialization identities,
   independent checksums, transactional publication, and a read-only tree.
9. **Complete.** Implement `fsim elaborate --object PATH... --top ALIAS=TARGET
   --output PATH` with repeated search-library and explicit binding/resolver
   options and deterministic multi-root resolution. The production command
   loads ordered portable objects, performs ordinary multi-root elaboration,
   and transactionally publishes the requested standalone design. Focused
   exact-LLVM Debug coverage elaborates a selected SystemVerilog root from two
   independently compiled objects while all producer sources are unavailable.
10. **Complete.** Serialize and restore the complete HDL `ElaboratedDesign`
    runtime adapter, SimIR processes/objects, DesignIR hierarchy/boundaries,
    semantic source/debug model, and specialization identities without
    retaining producer memory addresses or absolute source paths. Canonical
    payload codecs preserve the semantic model, every SimIR operation variant,
    runtime processes/signals, DesignIR, roots, and specialization keys; the
    restored state reserializes byte-for-byte and executes to the expected
    stop time after both producer sources and object directories are hidden.
11. **Complete.** Prove that successful `simulate` loading invokes neither the
    frontend nor the elaborator and that corrupt/incompatible/partial design
    artifacts fail transactionally before constructing a scheduler. The
    standalone loader restores only the three checksummed state projections;
    focused coverage hides every producer source and object, then loads and
    runs successfully. Separate checksum-corrupt, missing-payload, and
    incompatible-runtime-ABI copies all reject through the loader before a
    `Simulation` is constructed.
12. **Complete.** Implement `fsim simulate --design PATH` with explicit
    interpreter/compiled/debug engine selection, duration, max-delta, seed,
    delay, trace-file, and trace-filter controls. Production CLI coverage runs
    the same source/object-independent artifact through interpreter, optimized
    LLVM, and debug/O0 engines, applies duration and seed overrides, writes a
    filtered VCD, and rejects a requested delay mode that differs from the
    artifact's fixed elaboration selection.
13. **Complete.** Keep `.fsimobj` and `.fsimdesign` directories read-only and
    place LLVM objects, traces, debugger state, file-I/O state, and every other
    derived output in explicit consumer paths. Both publishers enforce
    read-only trees. Standalone simulation accepts explicit `--cache` and
    `--file-root` directories, requires the file root to exist, and writes its
    filtered VCD to the requested consumer path; focused coverage confirms no
    LLVM or trace output appears inside the design artifact.
14. **Complete.** Include object/design metadata, ordered input digests,
    selected canonical identities, runtime ABI, LLVM host identity, and engine
    options in design/native cache provenance with exact-compatible reuse. The
    canonical design digest covers ordered object content, roots/selections,
    bindings, ABI, timing, engine policy, and state checksums and now salts each
    standalone native-module identity. LLVM's host/options fingerprint remains
    the final cache boundary. Focused cold/warm coverage observes miss/store
    then hit; debug/O0 and a changed artifact digest both miss independently.
15. **Complete.** Preserve multiple roots, cross-language inference,
    configurations, generics/parameters, packages/contexts, callbacks,
    debugger locations, normalized VCD, and deterministic interpreter/LLVM
    behavior across the three phase boundary. Focused scripted-phase coverage
    compiles independent VHDL and SystemVerilog objects containing a package,
    context, generic, configuration, parameter specialization, inferred
    SV-to-VHDL child, and two aliased roots. The restored design retains
    relative semantic/debug sources and callbacks; interpreter and LLVM agree
    on final values and stop time, and a filtered two-root VCD is normalized.
    This coverage also exposed and fixed producer-absolute IEEE projection
    paths by assigning stable `fsim-standard/...` logical names with separate
    consumer-local backing paths.
16. **Complete.** Reject SystemC source/factory/proxy inputs in Batch 137 with
    an actionable diagnostic routing them to Batch 138's separate incremental
    SystemC compile/link phases; do not silently omit native behavior. The
    object/design schemas admit portable HDL languages only, and production
    `fsim compile --lang systemc` fails before publication with the Batch 138
    route; focused coverage confirms no partial object is created.
17. **Complete.** Add public C++ artifact compile/elaborate/load APIs and stable
    inspection records for phase kind, schema, language/library, roots,
    digests, units, processes, and compatibility without changing the v1 C ABI.
    `fsim::app::compile_artifact`, `elaborate_artifact`, the existing object and
    design loaders, and metadata-only `inspect_artifact` form the public C++
    surface. Direct API tests publish/load a design and inspect both artifact
    kinds; incompatible schemas/ABIs remain transactional diagnostics. No C
    ABI symbol or layout changes.
18. **Complete.** Add positive scripted-phase coverage for VHDL,
    Verilog/SystemVerilog, mixed hierarchy, packages/configurations,
    parameters/generics, multiple roots, relocation, cold/warm cache,
    interpreter, LLVM O0/O2, debugger, callbacks, and VCD. Focused production
    and direct-API tests cover all listed dimensions, including a complete
    Verilog-2005 compile/elaborate/simulate path and source/object-independent
    execution after producer inputs are renamed away.
19. **Complete.** Add negative coverage for missing/duplicate/reordered objects,
    option conflicts, format/schema/checksum/identity corruption, incompatible
    ABI, unresolved/ambiguous units, overwrite/write attempts, and partial
    publication; update CLI help, architecture, language support, diagnostics,
    feature matrix, tutorial, inventories, and this record. The complete
    artifact negative matrix is split between metadata publisher/codec tests
    and production phase tests; missing and ambiguous targets leave no output.
    Public documentation now includes the command contract, architecture,
    portable-HDL boundary, ten diagnostic entries, CM-088 evidence, and an
    executable vertical-slice tutorial. Focused catalog/source/inventory and
    installed-public-contract gates pass with 1,653 diagnostics, 459 bounded
    sources, 549 SPDX-owned artifacts, and 196 test/control files.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, and release gates, then commit and push once. Batch 137 is not a
    CI boundary and runs no sanitizer or CI-monitoring gate. Both configurations
    built with eight workers. Exact-LLVM Debug passed 109/109 tests in 138.17
    seconds and Release passed 109/109 in 106.09 seconds. Those suites include
    artifact, source, diagnostic-catalog, inventory, installed-public-contract,
    legality, differential, and release-candidate gates. The reviewed matrix
    contains 1,083 execute rows, 4,332 evidence cells, 331 evidence paths, and
    96 runtime owners.

## Batch 138 - incremental SystemC compilation and linking - Complete

1. **Complete.** Establish this exact 20-change Batch 138 plan and synchronized
   restart record from the clean pushed Batch 137 baseline before implementation
   begins.
2. **Complete.** Add manifest-free `fsim systemc compile` and `fsim systemc
   link` command families with phase-specific option validation and no implicit
   project discovery.
3. **Complete.** Define a versioned, checksummed, host-specific `.fsimscobj`
   directory schema for one C++20 translation unit, including compiler/runtime
   ABI, target/toolchain, compile settings, source and dependency identities,
   and one native object payload.
4. **Complete.** Implement `fsim systemc compile --output PATH SOURCE` with
   explicit include, define, compiler, and compile-option inputs on GCC-like and
   MSVC-compatible toolchains.
5. **Complete.** Discover and checksum the complete translation-unit dependency
   closure, revalidate it after compilation, publish transactionally and
   read-only, refuse overwrite, and leave no partial artifact on failure.
6. **Complete.** Make translation-unit compilation independently cacheable and
   deterministic so an unchanged object is reused while edits rebuild only the
   affected source and its dependency closure.
7. **Complete.** Define a versioned, checksummed, host-specific `.fsimscplugin`
   directory schema containing one linked native plug-in, its logical library,
   ordered object identities, link inputs, ABI fingerprint, and exported factory
   inventory.
8. **Complete.** Implement `fsim systemc link --object PATH... --library LIBRARY
   --output PATH` with explicit library and link-option inputs and deterministic
   object ordering on GCC-like and MSVC-compatible toolchains.
9. **Complete.** Reject mixed compiler/runtime/target/compile-ABI objects,
   duplicate or reordered object identities, unsafe link inputs, incompatible
   options, and source/object mutation before publishing any plug-in artifact.
10. **Complete.** Load the newly linked library through the production SystemC
    ABI before publication, transactionally validate its entry point, factories,
    aliases, parameter schemas, and duplicate-name behavior, and record the
    sorted public factory inventory.
11. **Complete.** Support `SC_FSIM_EXPORT` and `SC_FSIM_EXPORT_AS` descriptors
    spread across independently compiled translation units while linking the
    fsim SystemC support library exactly once and retaining legacy handwritten
    entry-point compatibility.
12. **Complete.** Route project-mode SystemC builds through the same incremental
    compile-object/link pipeline, preserving the existing manifest and public
    build APIs while avoiding recompilation of unchanged translation units.
13. **Complete.** Add repeatable `--systemc-plugin PATH` inputs to manifest-free
    elaboration, merge each plug-in's logical-library factory candidates with
    ordered HDL `.fsimobj` inputs, and preserve automatic mixed-language target
    resolution and multiple roots.
14. **Complete.** Extend `.fsimdesign` metadata and publication to embed every
    selected linked SystemC plug-in plus its content and host fingerprints,
    factory inventory, instance construction records, and native provenance.
15. **Complete.** Reload embedded plug-ins during standalone simulation,
    reconstruct their hierarchy deterministically, remap serialized native
    handles, bind ports/events/channels/processes to the common runtime, and
    preserve lifecycle behavior without producer objects or source files.
16. **Complete.** Add public C++ compile/link/load/inspection APIs for SystemC
    object and plug-in artifacts and extend artifact inspection without changing
    the v1 C ABI.
17. **Complete.** Preserve response-file-safe process execution, bounded compiler
    output, Windows CRT/iterator policy, unique object names, concurrent cache
    locking, and the configured eight-way linker pool without introducing a
    shell-evaluated command path.
18. **Complete.** Add positive incremental coverage for multiple translation
    units, multiple exports and aliases, typed parameters, cold/warm/selective
    rebuild, project compatibility, SystemC-only and mixed HDL/SystemC roots,
    interpreter/LLVM O0/O2, debugger, callbacks, VCD, and relocation.
19. **Complete.** Add negative coverage for missing or duplicate inputs,
    format/schema/checksum/ABI/toolchain corruption, compile/link failures,
    incompatible or mutated objects, missing/invalid entry points, duplicate
    factories, overwrite/write attempts, and transactional partial failure;
    update CLI help, architecture, language support, diagnostics, feature
    matrix, tutorial, inventories, and restart evidence.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, and release gates with at least eight
    workers, then commit and push once. Batch 138 is not a CI boundary and runs
    no sanitizer or CI-monitoring gate. Both exact-LLVM configurations built
    with eight workers. Debug passed 110/110 tests in 137.08 seconds and Release
    passed 110/110 in 106.22 seconds; focused post-review artifact/application
    reruns passed in both configurations. The source-independent scripted
    three-language design also reloaded its embedded SystemC parent and VHDL
    proxy child, printed `PASS`, and stopped at tick 3 after every producer
    object/plug-in directory was renamed away. The reviewed inventory contains
    1,659 diagnostics, 463 bounded sources, 553 SPDX-owned artifacts, 197
    test/control files, 1,084 execute rows, 4,336 evidence cells, 334 evidence
    paths, and 97 runtime owners.

## Batch 139 - VHDL timing and driver attribute foundation - Complete

1. **Complete.** Establish this exact 20-change Batch 139 plan and synchronized
   restart status from clean pushed Batch 138 commit `83dd339` before
   implementation begins.
2. **Complete.** Extend the VHDL attribute parser and semantic model with
   `'last_active`, `'driving`, `'driving_value`, `'quiet`, `'transaction`, and
   `'delayed`, retaining the existing case-insensitive designator rules.
3. **Complete.** Define immutable typed HIR and SimIR operations for signal
   activity time, current-driver presence/value, and transaction toggles
   without exposing scheduler-owned storage to generated code.
4. **Complete.** Track each signal's latest transaction time independently of
   its latest effective-value event time, including redundant assignments and
   resolved multi-driver transactions.
5. **Complete.** Track stable process-to-driver identity through hierarchy,
   serialization, native-cache provenance, interpreter execution, and compiled
   process callbacks.
6. **Complete.** Implement scalar and packed-vector `'driving` in a process that
   owns a driver, returning false outside a valid driving context with the
   standard object and process legality checks.
7. **Complete.** Implement typed scalar and packed-vector `'driving_value` for
   the calling process's current driver contribution, including nine-state
   resolved signals and a diagnostic when no driver exists.
8. **Complete.** Implement `'last_active` as elapsed global-resolution time since
   the most recent transaction, with `TIME'HIGH` before any transaction and
   exact overflow behavior matching `'last_event`.
9. **Complete.** Implement zero-duration and static time-qualified `'stable(T)`
   as implicit Boolean signals that update in the correct delta and time
   regions after value-changing events.
10. **Complete.** Implement zero-duration and static time-qualified `'quiet(T)`
    as implicit Boolean signals driven by transaction activity, including
    redundant assignments that do not produce an event.
11. **Complete.** Implement `'transaction` as an implicit Boolean signal that
    toggles for every transaction affecting the prefix signal and can appear
    in ordinary expressions, waits, and sensitivity lists.
12. **Complete.** Implement `'delayed(T)` as an implicit signal preserving the
    prefix type/domain and reproducing effective-value events after the exact
    static delay, including the default zero duration.
13. **Complete.** Intern identical implicit attributes per elaborated signal and
    duration so repeated references share state, while distinct durations and
    hierarchy instances remain independent.
14. **Complete.** Integrate implicit attribute signals with delta scheduling,
    multiple transactions at one timestamp, cancellation-free delayed history,
    stop limits, callbacks, debugger reads, and deterministic VCD naming.
15. **Complete.** Add append-only C/JIT runtime callbacks and LLVM O0/O2 lowering
    for the new direct queries while preserving every existing v1 ABI offset
    and validating only the callback tail each compiled process needs.
16. **Complete.** Preserve the new operations, implicit signals, driver identity,
    and timing state through `.fsimobj`, `.fsimdesign`, native-cache,
    relocation, and standalone simulation round trips.
17. **Complete.** Add frontend/elaboration negative coverage for invalid
    prefixes, arguments, nonstatic or negative durations, unsupported types,
    illegal driver contexts, and overflow, with cataloged diagnostics.
18. **Complete.** Add interpreter/LLVM O0/O2 application differentials for
    scalar/vector, two-state/nine-state, redundant transaction, resolved
    multi-driver, hierarchy, sensitivity, debugger, callback, VCD, cold/warm
    cache, and standalone-artifact behavior.
19. **Complete.** Update architecture, language support, diagnostics, feature
    matrix, inventories, VITAL dependency notes, and this resume evidence with
    the exact bounded support and remaining VHDL/VITAL gaps.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, and release gates with at least eight
    workers, then commit and push once. Batch 139 is not a CI boundary and runs
    no sanitizer or CI-monitoring gate. Both exact-LLVM configurations built
    with eight workers. Debug passed 110/110 tests in 134.43 seconds and Release
    passed 110/110 in 107.71 seconds. The reviewed inventory contains 1,667
    diagnostics, 468 bounded sources, 558 SPDX-owned artifacts, 198
    test/control files, 1,092 execute rows, 4,368 evidence cells, 335 evidence
    paths, and 97 runtime owners.

## Batch 140 - VITAL package types and combinational primitives - Complete

1. **Complete.** Start from clean pushed Batch 139 commit `e4de752`, record
   this exact 20-change monitoring-batch contract, and synchronize the restart
   handoff before implementation changes.
2. **Complete.** Add clean-room compiler-supplied `ieee.vital_timing` and
   `ieee.vital_primitives` interfaces with an fsim-owned revision identity;
   do not copy or redistribute source whose license is not established.
3. **Complete.** Activate each VITAL package lazily from direct/context-expanded
   `use` clauses, inject its `std_logic_1164` dependency in deterministic
   order, reject project redeclarations, and retain source/cache provenance.
4. **Complete.** Materialize VITAL transition, delay, output/result-map,
   table-symbol, truth-table, and fixed-vector public types with their exact
   directions, nominal identities, and locally static constraints.
5. **Complete.** Publish the standard zero-delay, default-delay, default-map,
   and table constants and preserve their values through named constants,
   generic actuals, package visibility, and specialization identity.
6. **Complete.** Implement all `VitalExtendToFillDelay` overloads for scalar,
   01, 01Z, and 01ZX transition sets without truncating physical time.
7. **Complete.** Implement `VitalCalcDelay` overloads with exact nine-state old
   and new values, strongest applicable transition selection, unknown
   propagation, and checked nonnegative time results.
8. **Complete.** Implement default and caller-supplied `VitalResultMapType`,
   `VitalResultZMapType`, and `VitalOutputMapType` mapping with exact U/X/0/1/Z,
   weak-state, and don't-care handling.
9. **Complete.** Implement the scalar `VitalBUF`, `VitalINV`, and `VitalIDENT`
   function forms with exact result-map behavior.
10. **Complete.** Implement `VitalBUFIF0`, `VitalBUFIF1`, `VitalINVIF0`, and
    `VitalINVIF1`, including disabled Z and indeterminate-enable results.
11. **Complete.** Implement unconstrained-vector `VitalAND`, `VitalOR`,
    `VitalXOR`, `VitalNAND`, `VitalNOR`, and `VitalXNOR` reductions across
    ascending, descending, singleton, and null input ranges.
12. **Complete.** Implement the complete two-, three-, and four-input fixed-arity
    logical function family as profile-compatible wrappers over the common
    nine-state reduction semantics.
13. **Complete.** Implement `VitalMUX`, `VitalMUX2`, `VitalMUX4`, and `VitalMUX8`
    with direction-independent data/select indexing and pessimistic merging
    for unknown selectors.
14. **Complete.** Implement `VitalDECODER`, `VitalDECODER2`, `VitalDECODER4`,
    and `VitalDECODER8` with exact enable, output direction, and result-map
    behavior.
15. **Complete.** Implement both `VitalTruthTable` function profiles, including
    table symbols, first-matching-row order, X when no row matches, and the
    standard rejection of `'-'` as a truth-table output (retention belongs to
    the state-table API scheduled for Batch 141).
16. **Complete.** Preserve VITAL package/type/call provenance and results through
    project and non-project compilation, `.fsimobj`, `.fsimdesign`, standalone
    simulation, native-cache cold/warm reuse, debugger, callbacks, and VCD.
17. **Complete.** Add cataloged diagnostics and negative tests for malformed
    package profiles, delay/map/table dimensions, invalid symbols, unsupported
    dynamic composites, negative delays, empty required inputs, and ambiguous
    or missing VITAL declarations.
18. **Complete.** Add interpreter and LLVM O0/O2 differentials covering every
    function family, all nine logic states, custom maps, direction changes,
    hierarchy, contexts, cold/warm cache, and standalone artifact execution.
19. **Complete.** Update architecture, language support, diagnostics, feature
    matrix, inventories, VITAL compatibility notes, and restart evidence; keep
    timing checks, path/wire delays, pulse rejection, and VITAL memory models
    assigned explicitly to subsequent batches.
20. **Complete.** Run the LLVM-disabled ASan/UBSan regression immediately
    before commit, then exact-LLVM Debug/Release and all source, catalog,
    inventory, installed-public-contract, and release gates with at least eight
    workers; commit and push once, inspect every non-documentation GitHub
    Actions job, and repair any failure before completing this monitoring
    batch. The final exact-LLVM Debug regression passes 110/110 in 137.92
    seconds and Release passes 110/110 in 115.48 seconds. Their composed gates
    confirm 1,676 diagnostics, 469 bounded sources, 559 SPDX-owned artifacts,
    1,095 execute rows, 4,380 evidence cells, 337 evidence paths, and 97 runtime
    owners. The LLVM-disabled ASan/UBSan regression passes 107/107 in 291.02
    seconds with leak detection disabled because the managed runner executes
    under ptrace. The gate also repaired strict incremental SystemC plug-in
    linking of sanitizer-instrumented support code and made native-cache
    assertions reflect LLVM-disabled builds. Commit `527a031` is pushed on
    `codex/v2`. Hosted inspection is current: four Linux build/test jobs and
    the frontend fuzz job pass, the initial six Windows jobs exposed one MSVC
    warnings-as-errors comparison plus Windows path, permission, source-name,
    incremental-link, and mixed-SystemC portability defects. All six
    non-Windows jobs, including hosted ASan/UBSan, pass. The accumulated repair
    worktree passes post-repair 110/110 exact-LLVM Debug and Release regressions
    in 320.46 and 287.88 seconds. The exact final tree also passes the same
    14-test cross-platform repair gate in both configurations. Repair commit
    `5443c4b` is pushed. Replacement run `30903250986` completed with all four
    Linux build/test jobs, hosted ASan/UBSan, and frontend fuzz passing. Both
    MSVC Debug variants advanced past the original warning but expose
    COFF section-limit `C1128` in the archived DesignIR codec. All four Windows
    test jobs expose the same cached SystemC publication failures because the
    staging path repeats the full digest beyond the legacy Windows path limit,
    plus a Tcl fixture that writes an unescaped native path into TOML. The
    repair adds target-scoped `/bigobj`, uses a compact collision-safe staging
    name, writes the fixture path with generic separators, and prints cached-link
    diagnostics before its assertion. Eight-worker exact-LLVM Debug and Release
    builds pass the five affected tests and the Debug source, catalog,
    inventory, installed-public, and Windows portability gates. Repair commit
    `4bf9195` is pushed. Replacement run `30906493862` confirms that all six
    non-Windows jobs pass, `/bigobj` clears both MSVC Debug builds, and the prior
    staging, incremental-link, mixed-SystemC, and Tcl failures are gone. Every
    completed Windows configuration now fails only the main application at its
    non-project producer-hiding checkpoint; plain and LLVM MSVC Debug also show
    the SystemC matrix reaching the former 900-second test bound, while
    `fsim.application.scoped_locals` remains quick. Windows retains the loaded
    plug-in DLL, so the test now hides its required metadata rather than
    renaming the loaded artifact directory. The matrix timeout is raised to
    1,200 seconds and the plain MSVC job receives the same 70-minute ceiling as
    the LLVM Windows matrix. Both portability contracts and the application
    plus SystemC-matrix tests pass after eight-worker exact-LLVM Debug and
    Release builds. Repair commit `696be29` is pushed. Replacement run
    `30911069043` confirms all six non-Windows jobs green and the matrix bound
    effective: plain MSVC Debug passes it in 1,014.68 seconds and LLVM MSVC
    Debug in 1,057.46 seconds; scoped locals pass in 0.41 and 1.69 seconds. All
    six Windows variants now fail only the producer-hiding checkpoint because
    the metadata file retains its own read-only attribute after the artifact
    directory becomes writable. The focused correction makes that one file
    owner-writable before renaming it, and the exact-LLVM Debug/Release
    application tests pass locally in 20.42 and 19.72 seconds. Repair commit
    `e4f11ce` is pushed, but run `30916363303` repeats the same coarse
    `0xc0000409` application checkpoint and is canceled by request. That
    checkpoint spans every operation from producer relocation through embedded
    simulation, so it does not establish which assertion failed. The current
    diagnostic worktree reports error codes for every rename and permission
    change, prints embedded-design diagnostics, and marks load, validation, and
    simulation completion. It builds warning-clean with eight workers and the
    exact-LLVM Debug/Release application tests pass in 20.04 and 19.16 seconds.
    Diagnostic commit `0d67c82` is pushed. Run `30918625659` proves every
    producer mutation, embedded-design load and structural check, and embedded
    simulation call completes before the abort, then is canceled. The next
    diagnostic reports the post-simulation status, callback count, and value
    and marks each remaining non-project phase. A temporary Windows MSVC Debug
    workflow builds only `fsim_application_tests` with four hosted workers and
    runs only `^fsim.application$` verbosely, avoiding another full matrix while
    diagnosing this case. The expanded test remains warning-clean after
    eight-worker exact-LLVM builds and passes locally in 20.01 and 19.49
    seconds. Focused run `30920656747` reports correct embedded SystemC status,
    three callbacks, and value `00000101`, then passes object metadata, portable
    unit, relocation, state round-trip, and HDL design publication before
    aborting at the first published-HDL-object relocation. The current repair
    makes only the two read-only object roots writable, performs labeled
    error-code renames, and diagnoses embedded HDL design loading. Focused run
    `30921845380` reports exact Windows error 5 at the first object-directory
    rename after both permission changes succeed. The portable-unit input stream
    still holds a child file open, so Windows locks the containing directory;
    close that stream immediately after reading it. The final local
    exact-LLVM Debug and Release application tests pass in 19.58 and 18.86
    seconds. Focused Windows run `30922930843` passes the sole MSVC Debug
    `fsim.application` case in 10 minutes 29 seconds. The temporary workflow is
    removed in `446a654`. Final normal run `30923945372` passes all 12 jobs:
    four Linux build/test configurations, hosted ASan/UBSan, frontend fuzz,
    plain MSVC Debug/Release, Clang/LLVM Debug/Release, and MSVC/LLVM
    Debug/Release. Change 20 and Batch 140 are complete.

## Batch 141 - VITAL timing checks and state tables - Complete

1. **Complete.** Start from clean pushed Batch 140 closeout `c5c8a5c`,
   record this exact 20-change contract, and synchronize the restart handoff
   before implementation changes.
2. **Complete.** Materialize the public VITAL timing, period, glitch, skew,
   Boolean/time/logic access-array, initialization, and supporting record types
   required by timing checks, without importing third-party package bodies.
3. **Complete.** Implement exact `VitalEdgeSymbolType` transition matching for
   all nine logic states, weak-state normalization, no-event cases, and the
   standard rising, falling, ambiguous, and wildcard symbols.
4. **Complete.** Add persistent per-call VITAL timing-check state whose identity
   is stable across process suspension, hierarchy, artifact serialization,
   interpreter execution, and compiled callbacks.
5. **Complete.** Implement delayed test/reference sampling and event timestamps
   at the global time resolution, including zero delay, redundant transactions,
   simultaneous events, and checked physical-time arithmetic.
6. **Complete.** Implement both standard `VitalSetupHoldCheck` profiles with
   setup/hold high/low limits and exact reference-transition selection.
7. **Complete.** Implement setup/hold enable-direction controls, test/reference
   delay handling, simultaneous-boundary behavior, and persistent violation
   state across successive calls.
8. **Complete.** Implement `VitalRecoveryRemovalCheck` with active-high/low test
   semantics, recovery/removal enable controls, delayed sampling, and exact
   reference-edge behavior.
9. **Complete.** Implement `VitalPeriodPulseCheck` for period and high/low pulse
   widths, first-event initialization, unknown transitions, enable changes, and
   resolution-boundary comparisons.
10. **Complete.** Implement `VitalInPhaseSkewCheck` for both rise/rise and
    fall/fall directions with independent signal delays and directional limits.
11. **Complete.** Implement `VitalOutPhaseSkewCheck` for rise/fall and fall/rise
    directions with independent signal delays and directional limits.
12. **Complete.** Integrate `CheckEnabled`, `XOn`, `MsgOn`, header text, signal
    names, severity, deterministic violation flags, and assertion reporting
    without host-dependent formatting or duplicate messages.
13. **Complete.** Reject malformed timing profiles, negative or overflowing
    delays/limits, invalid edges, incompatible timing-state records, duplicate
    associations, and unsupported dynamic arguments with cataloged diagnostics.
14. **Complete.** Materialize state-table input/output/state storage and preserve
    previous input plus current state across delta cycles, suspension,
    hierarchy, serialization, and native-cache reuse.
15. **Complete.** Implement exact `VitalStateSymbolType` matching for static and
    transition symbols, including wildcard, unknown, binary, high-impedance,
    stable, and retention semantics over nine-state inputs.
16. **Complete.** Implement first-matching-row `VitalStateTable` evaluation,
    state-column updates, `'-'` retention, X on no match, and deterministic
    multi-output ordering for ascending and descending vectors.
17. **Complete.** Implement all four standard `VitalStateTable` procedure
    profiles, including scalar/vector results, optional state counts, null
    input/state ranges, multiple state variables, and named associations.
18. **Complete.** Preserve timing-check/state-table declarations, call state,
    results, and violations through project/non-project compilation,
    `.fsimobj`, `.fsimdesign`, relocation, standalone simulation, debugger,
    callbacks, VCD, and LLVM O0/O2 cold/warm execution.
19. **Complete.** Add focused positive/negative differentials and update
    architecture, language support, diagnostics, feature matrix, inventories,
    VITAL compatibility notes, and restart evidence; keep path/wire delays,
    pulse rejection, and memory/vendor closure assigned to Batches 142-143.
20. **Complete.** Exact-LLVM Debug and Release each pass 110/110 tests after
    eight-worker builds, in 141.34 and 114.76 seconds respectively. Source,
    catalog, inventory, installed-public-contract, Windows ABI, differential,
    and release-candidate gates pass. The reviewed baselines are 1,683
    diagnostics, 473 bounded sources, 563 SPDX-owned artifacts, 1,097 execute
    rows, 4,388 evidence cells, 340 evidence paths, and 97 runtime owners.
    Commit and push the accumulated batch once. Batch 141 is not a CI boundary;
    no sanitizer or hosted CI-monitoring gate ran.

## Batch 142 - VITAL path, wire, and pulse semantics - Complete

1. **Complete.** Start from clean pushed Batch 141 closeout `0d3be3e`, record
   this exact 20-change contract, and synchronize the restart handoff before
   implementation changes.
2. **Complete.** Materialize the public `VitalPathType`, `VitalPath01Type`,
   `VitalPath01ZType`, their unconstrained array types, and all standard
   `VitalPathDelay`, `VitalWireDelay`, and `VitalSignalDelay` declarations.
3. **Complete.** Decode path records and arrays with exact nominal layouts,
   declared directions, static/nonnegative physical delays, Boolean path
   conditions, null ranges, and deterministic positional/named associations.
4. **Complete.** Implement scalar `VitalSignalDelay` through the common signal
   scheduler for zero/nonzero delay, redundant transactions, delta cycles, and
   checked simulation-time arithmetic.
5. **Complete.** Implement scalar `VitalWireDelay(VitalDelayType)` with exact
   event propagation and transport semantics.
6. **Complete.** Implement scalar `VitalWireDelay(VitalDelayType01)` with exact
   low/high transition selection across all nine input states.
7. **Complete.** Implement scalar `VitalWireDelay(VitalDelayType01Z)` with exact
   rise, fall, and high-impedance transition selection.
8. **Complete.** Select sensitized VITAL paths from `InputChangeTime`, preserve
   simultaneous-path behavior, and choose deterministic effective delays
   without using interface compatibility to mask malformed path sets.
9. **Complete.** Implement `VitalPathDelay` single-delay scheduling, default
   delay selection, `IgnoreDefaultDelay`, and no-selected-path behavior.
10. **Complete.** Implement `VitalPathDelay01` transition-dependent scheduling,
    including unknown/weak transitions and `RejectFastPath` behavior.
11. **Complete.** Implement `VitalPathDelay01Z` transition-dependent scheduling,
    output-strength mapping, unknown/high-impedance transitions, and custom
    `VitalOutputMapType` values.
12. **Complete.** Implement `OnEvent` glitch handling with persistent
    `VitalGlitchDataType`, pulse detection, scheduled-time/value tracking,
    cancellation, X injection, and deterministic reporting.
13. **Complete.** Implement `OnDetect` glitch handling with immediate detection
    behavior distinct from `OnEvent` while retaining one scheduler-owned
    output-driver identity.
14. **Complete.** Implement `VitalInertial` mode using bounded rejection and
    cancellation rules shared with ordinary VHDL projected waveforms.
15. **Complete.** Implement `VitalTransport` mode without pulse rejection while
    preserving transaction order, equal-time replacement, and delta behavior.
16. **Complete.** Implement `NegPreemptOn` and fast/slow-path preemption at exact
    boundaries without underflow, overflow, duplicate reports, or stale output
    transactions.
17. **Complete.** Integrate `XOn`, `MsgOn`, `MsgSeverity`, output names, glitch
    state mutation, and callback/debugger visibility independently of whether
    an X waveform is emitted.
18. **Complete.** Preserve wire/path operations, glitch state, selected delays,
    and scheduled output behavior through project/non-project compilation,
    `.fsimobj`, `.fsimdesign`, relocation, standalone simulation, interpreter,
    LLVM O0/O2, cold/warm cache, callbacks, debugger, and VCD.
19. **Complete.** Add focused positive/negative differentials and update
    architecture, language support, diagnostics, feature matrix, inventories,
    VITAL compatibility notes, and restart evidence; keep memory models and
    vendor-library closure assigned to Batch 143.
20. **Complete.** Exact-LLVM Debug and Release each pass 111/111 tests after
    eight-worker builds, in 135.60 and 109.60 seconds respectively. Source,
    catalog, inventory, installed-public-contract, Windows ABI, differential,
    and release-candidate gates pass. The reviewed baselines are 1,688
    diagnostics, 476 bounded sources, 566 SPDX-owned artifacts, 1,099 execute
    rows, 4,396 evidence cells, 343 evidence paths, and 99 runtime owners.
    Commit and push the accumulated batch once. Batch 142 is not a CI boundary;
    no sanitizer or hosted CI-monitoring gate ran.

## Batch 143 - VITAL memory and vendor-model closure - Complete

1. **Complete.** Start from clean pushed Batch 142 closeout `0b8f212`, record
   this exact 20-change contract, and synchronize the restart handoff before
   implementation changes.
2. **Complete.** Materialize the clean-room `ieee.vital_memory` public
   enumerations, records, access types, unconstrained arrays, defaults, table
   symbols, and all standard subprogram profiles without redistributing an
   upstream package body. Focused package metadata and dependency projection
   pass in the exact-LLVM Debug application integration case.
3. **Complete.** Implement both `VitalDeclareMemory` profiles with checked
   positive geometry, stable access-backed storage, deterministic
   initialization, and text/binary load-file support through the confined file
   service. Direct runtime coverage exercises arbitrary 65-bit words, UX01
   initialization, hexadecimal and binary files, address controls, malformed
   input, and resource rejection. Source-level VHDL declarations pass the
   interpreter, LLVM O0/O2 cold/warm cache, debugger, runtime-state artifact,
   and relocated-design paths; the native cache advances to v78 after static
   load-file embedding closes relocation independence.
4. **Complete.** Decode ascending and descending address buses into good,
   unknown, invalid, and transitioned address states without host-width or
   signed-overflow dependence. Runtime coverage includes weak/unknown values,
   changed states, 130-bit valid and invalid buses, and unknown-over-invalid
   precedence.
5. **Complete.** Implement word-wide `VitalMemoryTable` matching with exact
   control, data, address, transition, steady, don't-care, implicit-read, and
   first-row semantics. The common transition mask and flag matcher covers all
   legal input symbols, default retention, invalid-symbol termination, and
   arbitrary-width corruption masks.
6. **Complete.** Implement enabled subword `VitalMemoryTable` behavior with
   declared bit ordering, partial writes, per-enable port flags, and exact
   corruption masks. Each subword independently matches vector-enable slices,
   derives its data transition state, and produces clipped partial-word masks,
   including a final short subword.
7. **Complete.** Implement read, write, read-not-write, high-impedance,
   no-change, transfer, retention, and whole/word/bit corruption actions across
   all public memory-table symbols. Data actions execute before memory actions
   for read-during-write behavior; targeted and enumerated tests cover every
   public action, conditional corruption, constants, exact Z, and scheduling
   suppression.
8. **Complete.** Preserve current/previous memory and data port state,
   output-disable state, previous controls/data/address/enables, and per-call
   table identity across delta cycles. Persistent word/subword evaluators retain
   all buses and independent port flags, recognize transitioned addresses, and
   suppress fully redundant calls only after current/previous states converge.
9. **Complete.** Implement both `VitalMemoryCrossPorts` profiles for read
   forwarding, read/write contention, multiple-write contention, read/read
   contention, unrelated addresses, disabled ports, and deterministic port
   order. Focused tests cover every mode, per-subword flags, address-local
   pairwise writes, exact-Z disabling, invalid ports, and dimension failures.
10. **Complete.** Implement both `VitalMemoryViolation` profiles, including
   scalar/vector violation flags, sized corruption masks, port-type behavior,
   data retention, and X/report/severity controls. Lookup aggregates sized
   vector chunks, expands selected subwords, rejects malformed tables
   transactionally, preserves invalid addresses, and separates action from
   message selection; read, write, and read/write gating reuse the table action
   engine.
11. **Complete.** Implement the vector VITAL memory setup/hold timing profiles
    for scalar/vector references, element delays, enabled checks, exact edge
    matching, per-pair state, and violation aggregation. Cross, parallel, and
    subword arcs use deterministic pair maps, checked broadcast/exact profiles,
    independent X/message controls, and the common scalar timing kernel.
12. **Complete.** Implement VITAL memory period/pulse checks over vector signals
    with per-element delay, threshold, persistent state, message-format, X,
    report, and severity behavior. Focused runtime tests cover enabled and
    disabled elements, high-pulse and period failures, quiet reporting, vector
    aggregation, subword geometry, and dimension rejection.
13. **Complete.** Implement every `VitalMemoryInitPathDelay` profile for scalar
    and vector output candidates, stable schedule records, subword geometry,
    retain behavior, and output maps. The normalized runtime initializes one
    checked record per bit, advances matured scheduled values, resets each
    selection pass, and accepts null output ranges.
14. **Complete.** Implement every `VitalMemoryAddPathDelay` profile for scalar
    and vector input events, scalar/vector single/01/01Z/01XZ delays,
    conditions, shortest-path selection, checked time arithmetic, and null
    ranges. One normalized core covers all 24 public overloads, cross,
    parallel, and subword indexing, simultaneous inputs, condition expansion,
    and bit/word retain event grouping.
15. **Complete.** Implement every `VitalMemorySchedulePathDelay` profile using
    the common projected-waveform scheduler, including output-retain pulses,
    bit/word corruption, high impedance, simultaneous paths, and delta-cycle
    ordering. Focused tests cover mapped values, scalar/per-bit/subword port
    gating, retained X-before-data waveforms, overflow, and transport waveform
    handoff through the kernel-owned execution context.
16. **Complete.** Preserve arbitrarily sized but resource-governed memory
    geometry and sparse access without hard semantic depth/width caps, while
    diagnosing overflow, malformed tables, inconsistent subwords, and
    unavailable load files transactionally. Small memories remain contiguous;
    large logical depths use an explicit default word plus bounded sparse
    materialization. Loads validate completely before mutation, whole-memory
    actions update the sparse default, and only one materialized word is
    subject to the common 256 MiB owning-storage budget. Focused runtime plus
    VITAL package/delay application tests pass after an eight-worker build.
17. **Complete.** Compile and execute representative clean-room vendor-style
    VITAL cell and memory models using common VITAL attributes, guarded timing
    generics, component configurations, escaped identifiers, pragmas, and
    zero-width/null compatibility idioms without vendor-name special cases.
    Entity, architecture, and generated declarative regions now accept static
    user-defined attribute declarations/specifications. A configured vendor
    cell executes guarded VITAL delay behavior through an extended identifier;
    a configured vendor memory declares generic-sized storage, and the same
    fixture retains null path ranges and synthesis pragma idioms. Interpreter,
    LLVM O0/O2 cold/warm cache, debugger, VCD, and artifact coverage pass.
18. **Complete.** Preserve memory objects, table state, cross-port state, timing
    state, path schedules, loaded contents, and selected provenance through
    project/non-project compilation, `.fsimobj`, `.fsimdesign`, relocation,
    standalone simulation, interpreter, LLVM O0/O2, cold/warm cache, debugger,
    callbacks, and VCD. Static `VitalDeclareMemory` load files are validated and
    embedded in SimIR, included in the native cache key, serialized through the
    generic artifact codec, and used by interpreter and LLVM callbacks; dynamic
    paths retain the confined runtime file service. The relocated artifact test
    deletes the original load file before standalone execution. The native
    cache advances to v78, while focused runtime, package integration, vendor
    cell/memory, runtime-state round trip, `.fsimobj`, relocated `.fsimdesign`,
    debugger, callback, and VCD evidence pass.
19. **Complete.** Add positive/negative differentials and update architecture,
    language support, diagnostics, feature matrix, inventories, VITAL/vendor
    compatibility notes, and restart evidence; leave SDF annotation to its
    later dedicated roadmap batch. The focused runtime, VITAL application,
    diagnostic-catalog, source-budget, IEEE-package, and inventory gates pass.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. Debug
    and Release each pass 111/111 tests in 133.33 and 104.39 seconds. The
    reviewed baselines are 1,692 diagnostics, 479 bounded sources, 569
    SPDX-owned artifacts, 200 test/control files, 1,101 execute rows, 4,404
    evidence cells, 346 evidence paths, and 100 runtime owners. Batch 143 is
    not a CI boundary and ran no sanitizer or hosted CI-monitoring gate.

## Batch 144 - Verilog-2005 user-defined primitive closure - Complete

1. **Complete.** Start after pushed Batch 143 closeout `cc5a6bb` and corrective
   LLVM-disabled VITAL callback build repair `e012444`, record this exact
   20-change contract, and synchronize the restart handoff before
   implementation changes.
2. **Complete.** Audit IEEE 1364-2005 UDP declaration, table, instance,
   delay, and state semantics against the existing compact Verilog HIR and
   publish the clean-room internal representation and diagnostic contract. UDP
   declarations remain distinct from top-selectable design units and retain
   level/output/edge symbols, ordered rows, state, timing context, and spans.
3. **Complete.** Parse combinational UDP declarations with ordered scalar
   terminals, exactly one output, input declarations, optional ANSI-style
   declaration forms where legal, checked end names, and source spans. Classic
   and ANSI positive fixtures pass the focused frontend suite.
4. **Complete.** Parse sequential UDP declarations with `reg` output refinement,
   optional legal initial output state, and deterministic declaration identity.
   ANSI `output reg` and explicit `reg`/`initial` forms share one HIR.
5. **Complete.** Tokenize and retain combinational table rows with exact
   level-input symbols, output symbols, separators, comments, and first-row
   priority without treating table text as ordinary expressions.
6. **Complete.** Retain sequential table rows with current-state columns,
   parenthesized edge descriptors, shorthand transition symbols, and explicit
   transition pairs. Focused HIR checks cover `(01)`, `r`, `n`, current-state
   don't-care, initial zero, and no-change output.
7. **Complete.** Implement exact UDP table symbol matching for `0`, `1`, `x`,
   `?`, `b`, rising/falling/positive/negative/any transitions, no-change `-`,
   and unknown output, with deterministic first-match behavior. The common
   matcher normalizes actual Z to unknown at its future execution boundary and
   covers `r`, `f`, `p`, `n`, `*`, explicit pairs, stable nonedges, current
   state, and ordered lookup in the focused frontend suite.
8. **Complete.** Validate terminal counts/directions, scalar-only profiles,
   output refinement, initial values, row widths, one-edge-per-row rules,
   duplicate declarations, malformed symbols, and unreachable table shapes
   through stable cataloged diagnostics. Codes `FSIM-SV-PARSE-224` through
   `243` and `FSIM-SV-SEM-130` through `144` are cataloged; positive/negative
   frontend, catalog, and source-budget gates pass.
9. **Complete.** Resolve UDP instantiations distinctly from built-in primitives
   and modules, including forward declarations, library identity, ambiguity,
   missing definitions, ordered terminals, and illegal named-port connections.
   Project snapshot merging now preserves declaration source order and logical
   libraries; focused elaboration distinguishes `udp:` candidates, detects
   module collisions, and proves built-in gates never enter unit resolution.
10. **Complete.** Retain optional instance names, comma-separated instances,
    bounded static instance arrays, scalar/vector terminal mapping, and
    one/two/three-value UDP propagation delays through the common delay model.
    Declaration-aware normalization disambiguates positional module parameters
    from UDP delays, array expansion creates deterministic scalar bridges, and
    ordinary modules reject UDP-only anonymous or delay syntax. Frontend,
    elaboration, diagnostic-catalog, and source-budget gates pass.
11. **Complete.** Elaborate each UDP declaration into one immutable normalized
    table specialization shared by its instances, with selected identity and
    table digest included in design and cache provenance. Canonical terminal,
    state, edge, row, and output metadata hashes to a stable SHA-256 identity;
    repeated instances share one `UdpTableInfo` and record its digest.
12. **Complete.** Execute combinational UDP tables as four-state continuous
    drivers with initialization, stable-input suppression, first-row priority,
    unmatched-row X, and normal resolved-net ownership. Declaration rows lower
    to ordinary case-equality SimIR control and pass repeated 0/1/X/Z deposits.
13. **Complete.** Execute sequential UDP tables with per-instance current output
    and previous input state, initial-state application, exact edge detection,
    no-change rows, unmatched-event retention, and delta-cycle determinism.
    Hidden per-instance state signals retain all prior inputs and an
    initialization marker in the same sole-driver process; focused rising,
    negative/no-change, state-retention, and repeated-edge execution passes.
14. **Complete.** Schedule UDP output transitions through the common inertial
    rise/fall/turnoff delay kernel, including zero delay, cancellation,
    same-value events, overflow checks, and VCD/callback publication. UDP table
    evaluation now drives a hidden scalar value through an ordinary delayed
    continuous driver, so the existing cancellation and publication path is
    shared without a UDP-specific scheduler. Focused interpreter and LLVM
    O0/O2 cold/warm-cache execution proves the `5/7/11` transition profile,
    cancellation of a short pulse, zero-delay deltas, same-value stability,
    unknown transitions, callbacks, debugger reads, VCD timestamps, and
    `FSIM-TIME-0003` scale-overflow diagnostics.
15. **Complete.** Cover UDPs under generates, parameter-specialized hierarchy,
    multiple roots, logical-library search, out-of-tree libraries, and mixed
    VHDL/SystemC boundaries without bypassing the central resolver. Focused
    elaboration covers one- and two-lane generated specializations, aliased
    roots, searched `vendor` declarations, a VHDL-to-SV-to-UDP path, and a
    SystemC-proxy-to-SV-to-UDP path; the application fixture executes a UDP
    hierarchy loaded lazily from a mapped read-only `.fsimlib`.
16. **Complete.** Preserve UDP declarations, normalized tables, instance state,
    selected provenance, and pending outputs through `.fsimobj`, `.fsimdesign`,
    relocation, standalone simulation, and cold/warm/edit native caches. The
    portable owning schema now has a distinct `.fsimudp` payload, source-span
    relocation, object/library indexing, checksum validation, and normalized
    runtime-state tables. A standalone design runs after its source and object
    are hidden, and edited table content causes native-cache misses and stores.
17. **Complete.** Add interpreter and LLVM O0/O2 differentials for
    combinational, level-sensitive sequential, edge-sensitive sequential,
    delayed, generated, resolved-driver, debugger, callback, and VCD behavior.
    Source and relocated-artifact fixtures compare changes, final state,
    debugger output, and VCD text across interpreter plus compiled cold/warm
    runs; O0/O2 timing and native-cache reuse remain identical.
18. **Complete.** Add transactional negative and resource coverage for malformed
    declarations/tables/instances, unsupported profiles, excessive table or
    array geometry, time overflow, corrupt artifacts, and invalid native HIR.
    UDP tables and static instance arrays now use 256 MiB owning-storage budgets
    derived from their actual materialized records instead of the former
    64-instance semantic ceiling. Portable declarations and restored design
    state reject malformed rows, invalid digests, duplicate identities, and
    inconsistent specialization provenance; duplicate UDP object inputs and a
    corrupted primitive payload reject without a partial result. The focused
    frontend, elaboration, library-artifact, and transition-delay cases pass.
19. **Complete.** Update architecture, language support, diagnostics, feature
    matrix, inventories, Verilog compatibility notes, and restart evidence;
    keep strengths/switch primitives and specify timing assigned to later
    Verilog-2005 closure batches and SDF assigned to its roadmap batch. Ten
    executable feature rows `SV-672` through `SV-681` cover the UDP surface and
    remove UDPs from the deferred inventory. Architecture documents the
    candidate, normalized-table, artifact, and ordinary-SimIR boundaries;
    language/README compatibility notes distinguish the remaining strength,
    switch, specify, and SDF work. Focused source, catalog, IEEE-package, and
    inventory gates pass at 1,735 diagnostics, 484 bounded sources, 574
    SPDX-owned artifacts, and 202 test/control files.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. Debug
    passes 111/111 tests in 310.43 seconds and Release passes 111/111 in 278.64
    seconds. The reviewed baselines are 1,735 diagnostics, 484 bounded sources,
    574 SPDX-owned artifacts, 202 test/control files, 1,111 executable feature
    rows, 4,444 evidence cells, 351 evidence paths, and 103 runtime owners.
    Batch 144 is not a CI boundary and ran no sanitizer or hosted CI-monitoring
    gate.

## Batch 145 - Verilog-2005 strength and switch-primitive closure - Complete

1. **Complete.** Start after pushed Batch 144 closeout `4be4a51`, record this
   exact 20-change contract, and synchronize the restart handoff before
   implementation changes. Keep Changes 1-19 in one recoverable accumulated
   worktree; Change 20 alone owns full gates, one commit, and one push.
2. **Complete.** Define a clean-room immutable Verilog strength model covering
   `supply`, `strong`, `pull`, `weak`, `large`, `medium`, `small`, and `highz`
   ranks, distinct zero/one drive strengths, charge strengths, source spans,
   canonical identities, and deterministic comparison rules.
3. **Complete.** Parse legal drive-strength pairs on continuous assignments, net
   declarations, built-in gates, UDP instances, and module-instance outputs;
   retain strength before delay syntax and reject illegal order, duplicate,
   same-polarity, or context-specific forms with stable diagnostics.
4. **Complete.** Parse pull strengths, supply-net semantics, and `trireg`
   `small`/`medium`/`large` charge strengths, including legal defaults and
   explicit high-impedance members without conflating strength syntax with
   ordinary parameter or delay parentheses.
5. **Complete.** Carry strength and charge descriptors through frontend HIR,
   merged snapshots, normalized hierarchy drivers, specialization identities,
   source/debug provenance, and public introspection without adding hidden
   hierarchy objects.
6. **Complete.** Replace strength-blind scalar Verilog resolution with a common
   strength-aware four-state resolver that compares zero and one components,
   produces exact unknowns on tied opposition, ignores high impedance, and
   preserves `wand`/`triand` and `wor`/`trior` wired-net combination semantics.
7. **Complete.** Apply default and explicit drive strengths to module ports,
   continuous assignments, primitive outputs, UDP outputs, procedural drivers,
   force/release, deposits, VHDL boundaries, and SystemC boundaries without
   changing their existing unstrengthened behavior.
8. **Complete.** Execute strength-qualified `buf`, `not`, `and`, `nand`, `or`,
   `nor`, `xor`, and `xnor` primitives, including scalar/vector terminals,
   static arrays, multiple outputs where legal, and common one/two-value delay
   scheduling.
9. **Complete.** Execute strength-qualified `bufif0`, `bufif1`, `notif0`, and
   `notif1` primitives with exact enabled, disabled, and unknown-control
   strength reduction plus common rise/fall/turnoff inertial delays.
10. **Complete.** Implement unidirectional `nmos`, `pmos`, `cmos`, `rnmos`,
    `rpmos`, and `rcmos` primitives with four-state controls, resistive strength
    reduction, scalar/vector terminals, static arrays, and shared delays.
11. **Complete.** Implement bidirectional `tran`, `rtran`, `tranif0`, `tranif1`,
    `rtranif0`, and `rtranif1` devices as cycle-safe connected-net components
    with conditional conductance, resistive strength reduction, arrays, and no
    artificial driver ownership or hierarchy level.
12. **Complete.** Implement `pullup` and `pulldown` sources, `supply0`/`supply1`
    nets, and `tri0`/`tri1` implicit pulls with their standard default and
    explicit strength rules, resolved multi-driver interaction, and normal
    debugger/trace visibility.
13. **Complete.** Implement `trireg` charge retention, exact charge-strength
    arbitration, drive-to-charge transitions, reconnection, initialization,
    and deterministic delta-cycle behavior for scalar and packed nets.
14. **Complete.** Implement `trireg` decay timing through the common checked
    inertial event model, including zero, finite, and effectively infinite
    decay, cancellation by renewed drive, time scaling, overflow diagnostics,
    callbacks, and VCD publication.
15. **Complete.** Cover strength and switch networks under generates,
    parameter-specialized hierarchy, multiple roots, searched and mapped
    logical libraries, recursive mixed-language wrappers, aliases, and
    disconnected or cyclic transmission topologies through the central
    resolver and scheduler.
16. **Complete.** Preserve strength descriptors, switch topology, charge state,
    pending decay, and selected provenance through `.fsimobj`, `.fsimdesign`,
    `.fsimlib`, relocation, standalone simulation, and cold/warm/edit native
    caches with append-only checked schema evolution.
17. **Complete.** Add interpreter and LLVM O0/O2 differentials for strength
    conflicts, wired nets, pulls, supplies, MOS devices, bidirectional and
    conditional switches, resistive chains, charge retention/decay, debugger,
    callbacks, and VCD across source and relocated artifacts.
18. **Complete.** Add transactional negative, ambiguity, topology, recursion,
    unsupported-profile, excessive-materialization, corrupt-artifact, invalid
    native-HIR, and time-overflow coverage with resource budgets derived from
    materialized graph and event storage rather than semantic length limits.
19. **Complete.** Update architecture, language support, diagnostics, feature
    matrix, inventories, Verilog compatibility notes, and restart evidence;
    leave specify timing and SDF assigned to their later dedicated batches.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. Debug
    and Release each pass 111/111 tests in 135.16 and 106.30 seconds. The
    reviewed baselines are 1,755 diagnostics, 489 bounded sources, 579
    SPDX-owned artifacts, 204 test/control files, 1,121 executable feature
    rows, 4,484 evidence cells, 356 evidence paths, and 104 runtime owners.
    Batch 145 is not a CI boundary and ran no sanitizer or hosted
    CI-monitoring gate.

## Batch 146 - Verilog-2005 specify timing closure - Complete

1. **Complete.** Start after pushed Batch 145 closeout `9b771eb`, record this
   exact 20-change contract, and synchronize the restart handoff before
   implementation changes. Keep Changes 1-19 in one recoverable accumulated
   worktree; Change 20 alone owns full gates, one commit, and one push.
2. **Complete.** Define clean-room frontend HIR for specify blocks, specparams,
   module paths, timing checks, pulse controls, notifiers, conditions, source
   spans, canonical identities, and deterministic introspection.
3. **Complete.** Parse specify blocks and specparam declarations, including
   scalar constants, mintypmax triples, path-pulse specparams, closing-name
   recovery, duplicate handling, and stable malformed-block diagnostics.
4. **Complete.** Parse simple and edge-sensitive parallel (`=>`) and full (`*>`)
   module paths, ordered source/destination terminal lists, polarity operators,
   destination data-source expressions, and `posedge`/`negedge` qualifiers.
5. **Complete.** Parse unconditional, `if`-conditional, and `ifnone` paths plus
   `pulsestyle_onevent`, `pulsestyle_ondetect`, `showcancelled`, and
   `noshowcancelled` declarations with exact terminal scoping.
6. **Complete.** Parse and normalize one, two, three, six, and twelve path-delay
   forms, parenthesized lists, specparam references, mintypmax selection, and
   checked project-tick conversion without reserving SDF syntax.
7. **Complete.** Evaluate specparams and path conditions after parameter
   specialization, enforce locally-static timing limits where required, and
   retain exact specialization and source provenance.
8. **Complete.** Resolve specify terminals through generated hierarchy, packed
   lanes, port aliases, multiple roots, searched/mapped libraries, and
   VHDL/SystemC boundaries while rejecting ambiguous, recursive, or
   direction-incompatible paths.
9. **Complete.** Add normalized runtime module-path arcs with source/destination
   lanes, transition-delay tables, condition state, polarity/data-source
   transforms, pulse policy, and immutable hierarchy/debug provenance.
10. **Complete.** Execute parallel and full paths through the common scheduler,
    selecting exact 0/1/X/Z transition delays, accumulating source and path
    timing, and preserving deterministic inertial replacement and delta-cycle
    ordering.
11. **Complete.** Execute state-dependent paths with ordered `if` selection and
    `ifnone` fallback, exact four-state condition truth, edge filtering,
    polarity transforms, and destination data-source evaluation.
12. **Complete.** Implement onevent/ondetect pulse handling and
    showcancelled/noshowcancelled X publication, including reject/error limits,
    overlapping pulses, cancellation, overflow checks, callbacks, and VCD.
13. **Complete.** Parse, normalize, and execute `$setup`, `$hold`, `$recovery`,
    `$removal`, `$skew`, `$period`, and `$width` timing checks with exact event
    controls, thresholds, conditions, persistent per-instance history, and
    optional notifier updates.
14. **Complete.** Parse, normalize, and execute `$setuphold`, `$recrem`,
    `$timeskew`, `$fullskew`, and `$nochange`, including optional timestamp/check
    conditions, delayed signals, event-based/remain-active flags, and notifier
    behavior.
15. **Complete.** Integrate path and timing-check state with generate and
    parameter-specialized instances, aliased multiple roots, recursive mixed
    hierarchy, interpreter and LLVM O0/O2 execution, debugger inspection,
    callbacks, VCD, force/release, and resolved multi-driver nets.
16. **Complete.** Preserve specify HIR, normalized arcs, timing-check state,
    pending pulses, notifiers, and selected provenance through `.fsimobj`,
    `.fsimdesign`, `.fsimlib`, relocation, standalone simulation, and
    cold/warm/edit native caches with append-only checked schema evolution.
17. **Complete.** Add positive differentials for every delay arity, path kind,
    condition, edge/polarity/data-source form, pulse policy, timing check,
    notifier, engine, optimization level, artifact phase, and cache state.
18. **Complete.** Add transactional syntax, staticness, terminal, direction,
    width, ambiguity, timing-limit, optional-argument, invalid-native-HIR,
    corrupt-artifact, excessive-materialization, and time-overflow negatives
    with cataloged diagnostics and storage-derived budgets.
19. **Complete.** Update architecture, language support, diagnostics, feature
    matrix, inventories, Verilog compatibility notes, and restart evidence;
    leave SDF annotation assigned to its later dedicated batch.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. Debug
    and Release each pass 112/112 tests in 136.29 and 106.68 seconds. The
    reviewed baselines are 1,777 diagnostics, 501 bounded sources, 591
    SPDX-owned artifacts, 208 test/control files, 1,127 executable feature
    rows, 4,508 evidence cells, 362 evidence paths, and 107 runtime owners.
    Batch 146 is not a CI boundary and ran no sanitizer or hosted CI-monitoring
    gate.

## Batch 147 - SystemVerilog class object-model foundation - Complete

1. **Complete.** Start from pushed Batch 146 closeout `eafadad`, record this
   exact 20-change contract, and synchronize the restart handoff before source
   changes. Keep Changes 1-19 in one recoverable accumulated worktree; Change
   20 alone owns full gates, one commit, and one push.
2. **Complete.** Add source-spanned owning HIR for class declarations,
   forward declarations, parameters, base selections, properties, methods,
   constraints, qualifiers, and canonical declaration identities without
   treating classes as top-selectable design units.
3. **Complete.** Parse compilation-unit, package, module, interface, and nested
   class declarations with optional lifetime, parameters, `extends`, closing
   names, forward typedefs, and deterministic recovery/duplicate diagnostics.
4. **Complete.** Parse declaration-ordered class properties with packed,
   string, container, class-handle, and bounded aggregate types plus
   `local`/`protected`, `static`, `const`, `rand`, and `randc` qualifiers.
5. **Complete.** Parse constructors, functions, tasks, extern prototypes,
   out-of-block definitions, pure virtual methods, default arguments, and
   method/property selected-name expressions including `this` and `super`.
6. **Complete.** Resolve class and forward-declared type names through lexical,
   import, package-qualified, nested, and parameterized scopes with canonical
   case-sensitive identities and cycle-safe provenance.
7. **Complete.** Specialize value/type class parameters, base-class actuals,
   property layouts, method profiles, static state, and complete transitive
   source/cache identity before executable lowering.
8. **Complete.** Enforce inheritance legality, single-base acyclicity, override
   profile compatibility, final/pure requirements, visibility, hiding, and
   unambiguous inherited member lookup.
9. **Complete.** Add a resource-governed runtime class heap with nullable opaque
   handles, generation-safe object identity, declared-class metadata, dynamic
   type, deterministic allocation/default initialization, and cleanup.
10. **Complete.** Execute `null`, `new`, constructor calls, handle assignment,
    equality/inequality, argument/return transfer, property reads/writes, and
    checked null/stale/downcast failures with alias-preserving semantics.
11. **Complete.** Execute instance methods with `this`, implicit member access,
    base-constructor ordering, `super` calls, automatic locals, recursion
    guards, task suspension, and deterministic copy-in/copy-out.
12. **Complete.** Implement virtual method slots and dynamic dispatch across
    base handles, explicit nonvirtual base calls, covariant class-handle
    returns where legal, pure-call rejection, and stable call/debug identity.
13. **Complete.** Implement per-specialization static properties and methods,
    class-qualified access, initialization order, inheritance visibility, and
    one common state across aliased roots and package import paths.
14. **Complete.** Support bounded class handles as direct elements of fixed and
    dynamic arrays, queues, associative arrays, packed-aggregate members where
    legal, and class properties, preserving aliases through container edits.
15. **Complete.** Integrate class construction and methods with processes,
    functions/tasks, generate specialization, multiple roots, recursive mixed
    hierarchy wrappers, reports/callbacks, debugger scopes, and traceable
    packed properties without exposing host pointers.
16. **Complete.** Preserve class declarations, specializations, heap state,
    handles, virtual slots, static members, and provenance through `.fsimobj`,
    `.fsimdesign`, mapped `.fsimlib`, relocation, standalone execution, and
    cold/warm/edit native caches with checked schema evolution.
17. **Complete.** Add interpreter and LLVM O0/O2 positive differentials for
    construction, aliases, inheritance, override dispatch, static state,
    method suspension, containers, debugger access, callbacks, and artifacts.
18. **Complete.** Add transactional syntax, type, visibility, inheritance,
    override, null/stale handle, allocation-budget, invalid-native-HIR,
    corrupt-artifact, and excessive-materialization negatives with cataloged
    diagnostics and storage-derived limits.
19. **Complete.** Update architecture, language support, diagnostics, feature
    matrix, inventories, class/UVM compatibility notes, and restart evidence;
    retain constraints/randomization and UVM library behavior for the next
    class-closure batches.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. The
    exact-LLVM Debug and Release suites pass 112/112 in 137.41 and 105.90
    seconds. The release-candidate evidence covers 1,137 executable rows,
    4,548 evidence cells, 372 exact paths, and 108 runtime owners. Batch 147 is
    not a CI boundary and ran no sanitizer or hosted CI-monitoring gate.

## Batch 148 - Source-executable SystemVerilog classes - Complete

1. **Complete.** Start from pushed Batch 147 closeout `37fbaaa`, record this
   exact 20-change contract, and synchronize the restart handoff before source
   changes. Keep Changes 1-19 in one recoverable accumulated worktree; Change
   20 alone owns full gates, one commit, and one push.
2. **Complete.** Extend checked type/value environments so source-declared
   module, process, function, task, block, argument, return, and class-property
   objects can carry canonical nullable class-handle types without collapsing
   them into packed integers or host pointers.
   module, process, function, task, block, argument, and return declarations
   resolve to their canonical lexical, package, or compilation-unit class
   identity. Module-scope handles migrate transactionally from the parser's
   unresolved signal surface into typed variables, and each ordinary design
   unit now retains its stable compilation-unit identity through portable
   owning-unit schema 6. Focused frontend, artifact round-trip, and source-line
   budget tests pass after eight-worker builds.
3. **Complete.** Parse and retain `new`, constructor actuals, `null`, class
   handle assignment/equality, `$cast`, selected properties, class-qualified
   statics, and method-call expressions/statements with deterministic source
   spans and recovery. Source `new`, `null`, and `$cast` now carry distinct
   owning expression markers; named constructor and instance-method actuals
   remain aligned with operands, including the explicit method receiver.
   Focused parsing proves handle assignment/equality, property selections,
   static property/function access, and named class-task calls.
4. **Complete.** Resolve every class object, constructor, property, method,
   static selection, `this`, `super`, and explicit base-qualified selection
   against the specialized class/member environment before lowering. A
   structurally separate expression-resolution pass walks class/module/
   process/function/task/block scopes, carries canonical handle types through
   assignments, returns, equality and casts, and rewrites construction,
   instance/static property, function, task, and base-constructor references
   to canonical identities with explicit receivers. The full frontend suite
   and `fsim.application` pass after eight-worker builds.
5. **Complete.** Add owning executable IR operations for class allocation,
   checked handle views, property reads/writes, and handle transfer while
   preserving opaque generation-safe identities across suspension. Semantic
   SystemVerilog HIR now owns class-handle type references; distinct null,
   allocation, checked-cast, instance/static property, and instance/static
   method expression kinds; canonical class/member identities; checked-access
   metadata; and typed assignment/return transfers without host addresses.
   A direct owning-HIR regression covers every operation before lowering.
6. **Complete.** Lower source `new` expressions and constructor calls with
   default/named/positional actual validation, transactional allocation, and
   mandatory base-to-derived initialization including implicit `super.new`.
   Source allocation now has an owning SimIR operation that carries aligned
   constructor registers and names into the simulation heap. Specialized class
   profiles retain executable formals, defaults, locals, and bodies through
   class-state schema 2. Runtime construction associates positional/named and
   default actuals, evaluates constructor locals and packed expressions, walks
   an explicit or implicit base constructor before the derived body, and writes
   owner-qualified hidden properties. The application proof checks a
   source-created derived handle and both base/derived initial values across
   interpreter, compiled-fallback, debugger, and standalone artifact paths.
7. **Complete.** Lower instance functions with implicit `this`, automatic and
   static locals, input/output/inout/ref copy rules, return values, recursion
   guards, and owner-qualified hidden-member access. Resolved expressions now
   retain operand-aligned directions plus packed return profiles through
   portable owning schema 7. SimIR owns instance property reads/writes and
   method calls; the simulation evaluator associates named/positional/default
   formals, preserves explicit static-lifetime locals, copies output/inout/ref
   values back, executes recursive calls under a host-stack guard, and honors
   canonical base/derived property and method ownership. The application proof
   covers all four formal modes, automatic and persistent locals, legal
   recursion, explicit `super` dispatch, property reads, hidden members, and
   producer-independent artifacts across all three engines. Full frontend and
   exact Debug core application tests pass after eight-worker builds.
8. **Complete.** Lower class tasks through the common scheduler with delays,
   event/wait suspension, persistent automatic frames, deterministic resume,
   copy-out, reports, assertions, and failure containment. Resolved class-task
   calls retain their source formals, locals, and bodies; elaboration discovers
   them recursively and synthesizes ordinary automatic task frames on the
   existing SimIR call stack. Delay, edge wait, assertion, debugger, register
   lifetime, return, and copy-out behavior therefore share the established
   scheduler path. Runtime-state schema 6 records explicit widths for new class
   results before LLVM validation selects interpreter fallback. The exact
   Debug core proof covers a local across delay, output/inout after resume, an
   assertion, a nested-class task waiting on a module edge, all three engines,
   and producer-independent artifacts.
9. **Complete.** Lower nonvirtual, explicit base, and virtual source calls using
   the stable compiler slots and the heap object's dynamic specialization;
   reject pure or profile-incompatible calls before execution. Ordinary and
   explicit `super` calls now retain distinct owning markers; SimIR records
   whether a call requests virtual dispatch. The runtime resolves the requested
   declaration profile, selects the first matching slot on the heap object's
   dynamic specialization chain, verifies exact profile identity, and rejects
   pure selections. The application proof calls `bump` through a base-typed
   handle to a derived object while a separate `super.bump` remains nonvirtual
   and owner-qualified. Exact Debug core and artifact paths pass across all
   engines after an eight-worker build.
10. **Complete.** Lower source static properties and methods through the shared
    per-specialization store with base-first initialization and identical
    state across imports, aliases, generated scopes, and multiple roots.
    Owning SimIR static-property reads/writes and static-method calls address
    canonical specialization/member identities and use the existing shared
    store. Source static functions execute against that store, while source
    static tasks are synthesized onto the common scheduler and preserve delay
    plus output copy-out behavior. Inherited class aliases select the same
    base declaration state. Property-read operations normalize the runtime's
    internal 64-bit integer storage to the declared executable width. The
    exact Debug application proof checks initialization, two static function
    calls, a delayed static task, inherited static-property selection, all
    engines, and producer-independent artifacts; focused owning-HIR, frontend,
    portable-artifact, and application tests pass after eight-worker builds.
11. **Complete.** Execute class-handle arguments, returns, direct properties,
    fixed/dynamic arrays, queues, associative arrays, and unpacked aggregates
    without losing aliases or bypassing declared element types and budgets.
    Runtime property access now bridges opaque 64-bit executable handles to
    dedicated handle-valued heap/static slots, validates every nonnull write
    against the declared class view, and preserves identity through source
    function returns plus suspending task input/output copy-out. Class property
    selections retain container metadata through resolution. Indexed
    fixed/dynamic/queue/associative reads and writes, dynamic `new[size]`, and
    queue `push_back`, `size`, and `pop_front` use owning class operations and
    the generation-safe typed container store. Explicitly bounded containers
    reserve their full budget; language-unbounded containers use an
    addressability-derived ceiling rather than an arbitrary element cap. The
    existing runtime aggregate proof continues to cover named unpacked handle
    members, aliases, type rejection, and exact bounded budgets. Exact Debug
    runtime, owning-HIR, frontend, portable-artifact, and application tests
    pass after eight-worker builds across all engines and standalone artifacts.
12. **Complete.** Integrate source class objects with module/process lifetime,
    initialization/final cleanup, functions/tasks, generate specializations,
    recursive mixed wrappers, and multiple roots on the single scheduler.
    Class type and expression resolution now recurse through generated bodies,
    migrate generated class objects from unresolved signal placeholders to
    typed variables, and preserve those variables while generate expansion
    scopes and qualifies their references. A final process proves class
    handles remain live through finalization. Parameter-specialized generated
    leaves nested beneath wrapper modules construct source objects in two
    aliased roots, share one static store and scheduler, and retain distinct
    heap objects. Interpreter, compiled, and debug engines agree on root
    signals, time, live-object count, and shared static state. Exact Debug
    runtime, owning-HIR, frontend, artifact, and application tests pass after
    eight-worker builds.
13. **Complete.** Extend interpreter and LLVM O0/O2 execution callbacks for
    class operations and resumable calls without embedding host addresses in
    native objects, runtime state, cache keys, or debugger handles. Native O0
    and O2 processes now return an append-only SimIR service-boundary status
    carrying only the immutable operation index and next program counter. The
    simulation-owned scheduler executes allocation, instance/static property,
    and instance/static method operations through the common typed hooks, then
    resumes the native frame immediately. Native-cache schema 81 hashes every
    canonical identity, register, actual, direction, dispatch, and width field
    without persisting a host address. Direct C ABI and LLVM O0/O2 tests prove
    instruction, PC, and register handoff; the exact Debug application core
    passes across interpreter, compiled, debug, and standalone artifact paths
    after an eight-worker build.
14. **Complete.** Expose source-created objects, dynamic/declared types,
    properties, statics, frames, and call identities through deterministic
    debugger inspection, packed change callbacks, safe points, and supported
    trace values while keeping handles opaque. Debugger `show`, `classes`,
    `class`, `class statics`, `class static`, and `class frames` now render
    declared/dynamic/specialized type identity, packed values, canonical call
    identity, automatic values, and opaque continuation/handle numbers. An
    uninitialized compiled local is reported deterministically rather than
    aborting inspection. Canonically sorted static and suspended-frame value
    snapshots expose no internal address. Source construction, instance/static
    writes, and instance/static calls publish packed time/delta changes; an
    independent safe-point observer sees live objects. Deterministic packed
    object/static trace snapshots feed the existing VCD writer. The exact
    Debug application core passes across interpreter, LLVM O2, debug/O0, VCD,
    and standalone artifacts after eight-worker builds.
15. **Complete.** Preserve lowered class operations, executable method bodies,
    live initial class state, handles, continuations, static state, and source
    provenance through `.fsimobj`, `.fsimdesign`, mapped `.fsimlib`, relocation,
    standalone execution, and cold/warm/edit caches with schema checks. The
    artifact execution proof exposed that the custom frontend-expression codec
    omitted class-call directions and result metadata. Class-state schema 3
    now retains names, directions, width, domain, and signedness together.
    With source and `.fsimobj` hidden, a copied read-only `.fsimdesign` and a
    relocated mapped `.fsimlib` retain class allocation/call operations,
    SimIR continuations, executable method statements and source provenance,
    null initial handles, initialized static state, and exact final behavior.
    Cold/warm native objects hit deterministically and a semantic delay edit
    misses/stores before exact source restoration. Focused library, frontend,
    elaboration, LLVM, runtime, and exact Debug application tests pass after
    eight-worker builds.
16. **Complete.** Add ordinary module/process/function/task fixtures that use
    source `new`, assignment, casts, properties, constructors, static and
    virtual methods, task suspension, containers, inheritance, and hiding.
    The ordinary module fixture now adds generation-safe class handles to an
    automatic module function and suspending module task. Runtime `$cast`
    lowers to a checked internal class service plus conditional destination
    transfer: a compatible derived-to-base view succeeds, an incompatible
    base-to-derived view returns zero and preserves the destination, and null
    remains a successful null view. The accumulated fixture jointly executes
    constructors, hidden properties, static/nonvirtual/virtual functions,
    class and module tasks, handle containers, recursion, aliases, final
    lifetime, and multiple roots across interpreter, LLVM O2, debug/O0, and
    source-hidden relocated artifacts.
17. **Complete.** Add interpreter, LLVM O0/O2, debugger, callback, multi-root,
    recursive hierarchy, artifact, relocation, and cold/warm/edit positive
    differentials for the complete source-executable class slice. One explicit
    engine snapshot now compares final time, hidden base/derived properties,
    shared static value, live-object count, and packed trace inventory across
    interpreter, compiled/O2, and debug/O0. The same accumulated proof covers
    deterministic instance/static callbacks and safe points, generated wrapper
    modules in aliased roots, direct LLVM O0/O2 class boundaries, copied
    standalone `.fsimdesign`, relocated mapped `.fsimlib`, and cold/warm/edit
    native objects. All focused differentials pass after eight-worker builds.
18. **Complete.** Add transactional parse, resolution, access, call-profile,
    constructor, pure-call, null/stale/cast, suspension, resource, native-HIR,
    and corrupt-artifact negatives with cataloged diagnostics and no arbitrary
    element-count limits. The diagnostics catalog now owns every class
    resolution and lowering code through `FSIM-SV-CLASS-015` and
    `FSIM-ELAB-SVCLASS-017`. Malformed native `ClassMethodCall` metadata fails
    module addition without publishing a symbol. Truncated, future-schema, and
    trailing class-state payloads reject. A failed source `$cast` returns zero
    while preserving its destination. Focused frontend/runtime proofs retain
    transactional declaration/access/profile/constructor failures plus pure,
    null, stale, recursion, suspension, alias, cycle, and exact resource-budget
    negatives. Language-unbounded class containers retain addressability-derived
    ceilings rather than an arbitrary element-count cap. Diagnostics, LLVM,
    and exact Debug application tests pass after eight-worker builds.
19. **Complete.** Update architecture, language support, diagnostics, feature
    matrix, inventories, the class/UVM closure boundary, and restart evidence;
    retain constraint solving/randomization and UVM library behavior for the
    next batches. Public architecture and language support now describe the
    source resolver, owning class HIR/SimIR, LLVM service boundary, debugger/
    callback/trace surface, schema-checked artifacts, and explicit UVM
    boundary. Five executable feature rows advance the reviewed release matrix
    to 1,142 rows, 4,568 evidence cells, and 377 exact paths. The reviewed
    inventory is 1,850 diagnostics, 526 bounded sources, 616 SPDX-owned
    artifacts, and 211 test/control files. Structural partitions move class
    inspection, container type construction, class JIT validation, and debug
    metadata behind focused owners; the source, catalog, inventory, legality,
    differential, and release-candidate gates pass.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. Batch
    148 is not a CI boundary and runs no sanitizer or hosted CI-monitoring
    gate. The first full Debug pass exposed receiver-aligned named-argument
    metadata left behind when ordinary interface-function lowering removed
    its receiver operand; packed and container-return paths now remove both
    transactionally, and the focused interface test passes in Debug and
    Release. The final exact-LLVM 22.1.8 Debug suite passes 112/112 in 331.12
    seconds and Release passes 112/112 in 289.34 seconds. Their accumulated
    source, catalog, inventory, installed-public-contract, Windows ABI/plan,
    differential, legality, and release-candidate gates are green. The final
    release evidence covers 1,142 executable rows, 4,568 linked evidence
    cells, 377 exact paths, and 108 runtime owners.

## Batch 149 - SystemVerilog constraint solving and randomization - In progress

1. **Complete.** Apply `/bigobj` to every MSVC-command-line target, including
   clang-cl, instead of only `fsim_application`; update the MSVC Debug contract
   to reject a return to target-local coverage. An eight-worker exact-LLVM
   Debug configuration/build plus source, MSVC Debug/Release, and Windows LLVM
   contract gates pass. At the user's explicit request, commit and push this
   correction together with the locked remaining-v2 roadmap as a clean
   checkpoint before Change 2; Changes 2-19 then accumulate from that baseline.
2. **Complete.** Define the executable SystemVerilog-2017 constraint subset and
   normalize retained constraint blocks, property qualifiers, spans, and
   inherited ownership into canonical semantic HIR. The owning HIR now
   flattens canonical class declarations while retaining their exact base
   ownership edge; records public/protected/local, static, const, `rand`, and
   `randc` property qualifiers; and owns source-spanned trees for names,
   literals, unary/binary/conditional expressions, calls, selections,
   concatenations, patterns, and `inside` sets/ranges. Constraint prototypes
   and static/pure/extern qualifiers remain distinct. Class resolution now
   canonicalizes constraint identities together with method identities.
   Exact-LLVM Debug frontend, SystemVerilog semantic-HIR, and source-line
   gates pass after eight-worker builds. Binding names and types against exact
   specializations remains Change 3.
3. **Complete.** Resolve constrained properties, parameters, local variables,
   class selections, and method references against exact specializations before
   solver construction. Each normalized name/call node now owns one typed
   binding per materialized specialization, including canonical declaration or
   method-profile identity, exact specialization identity, and locally
   evaluated parameter value. Reverse-layout lookup preserves derived hiding;
   `this`, `super`, qualified owners, local-access properties, and ordinary
   property/method references select their exact canonical owner. The focused
   HIR proof materializes default and edited parameter specializations and
   binds `MAX` independently to 3 and 7 while retaining the same canonical
   property and method declarations. Exact-LLVM Debug frontend, semantic-HIR,
   and source-line gates pass after eight-worker builds. Solver construction
   still consumes no source nodes and remains Change 7.
4. **Complete.** Compose base-to-derived constraint blocks with named block
   identity, override legality, `constraint_mode`, and deterministic inherited
   enable state. Each flattened class now owns a stable base-order composed
   view; a same-name derived block replaces the inherited slot while retaining
   both canonical identities and an explicit override edge. Static/nonstatic
   mismatch is marked illegal before solver construction. Defined blocks begin
   enabled, while pure/extern prototypes begin disabled, providing the exact
   deterministic default later copied into per-object `constraint_mode` state.
   Focused HIR coverage proves legal replacement, inherited identity, disabled
   pure prototypes, and an illegal static override. Exact-LLVM Debug frontend,
   semantic-HIR, and source-line gates pass after eight-worker builds.
5. **Complete.** Add checked per-object `rand` and `randc` state without
   exposing host pointers or collapsing declared signedness, width, enum, or
   handle profiles. Class specialization layouts and class-state schema 4 now
   preserve both qualifiers. Allocation materializes an independently owned,
   enabled state record beside each random property with its exact kind,
   executable width, signedness, nominal enum/handle profile, and revision;
   conceptual storage accounting includes that state and nominal identity.
   Checked lookup rejects nonrandom properties, and `randc` rejects string,
   handle, and container profiles before publication. Runtime coverage proves
   two objects do not share mode/revision state, generation-safe cleanup leaves
   no stale state, and budget/profile failures are transactional. The source
   class application and serialized class artifact retain `rand`/`randc` and
   construct exact 8-/4-bit runtime profiles across interpreter, compiled, and
   debugger engines. Exact-LLVM Debug runtime, core application, artifact, and
   source-line gates pass after eight-worker builds.
6. **Complete.** Derive deterministic simulation-, root-, object-, and
   call-local random streams from the project seed while preserving replay
   across engines and artifacts. A host-independent FNV-1a identity hash and
   explicit SplitMix-style mixer derive root streams, per-root allocation-
   ordinal object streams, property streams, and call-site plus invocation-
   ordinal streams without `std::hash` or host pointers. Source allocation
   carries the canonical root process identity across the interpreter,
   compiled, and debugger boundaries; public API allocation uses a stable
   `$api` root. Heap restart resets allocation ordinals, failed publication
   consumes no ordinal, and conceptual storage includes each property-local
   seed. Runtime coverage proves exact replay for equal project seed/root,
   independent object and call ordinals, and divergence for changed seed,
   root, or call site. Core application coverage proves identical root seed,
   object seed, and first call-local sample across all three engines; the
   artifact, runtime, application, and source-line gates pass after
   eight-worker exact-LLVM Debug builds.
7. **Complete.** Add a resource-governed finite-domain bit-vector/integer/enum
   solver with explicit variable, clause, search, and elapsed-work budgets.
   The runtime-owned value-semantic solver retains canonical variable and
   clause identities, exact width, signedness, nominal type, and `PackedLogic4`
   domains without host pointers. Iterative deterministic backtracking avoids
   variable-count recursion and returns either a complete assignment,
   unsatisfiable, or an exact search/elapsed resource-exhaustion reason with no
   partial values. Checked variable, clause, and aggregate-domain registration
   budgets fail transactionally through typed resource errors; malformed
   profiles, duplicate identities/domain values, and unknown or repeated
   dependencies reject before publication. Focused coverage proves repeatable
   mixed bit-vector/integer/enum solving, finite unsatisfiability, independent
   search and elapsed exhaustion, and every registration budget after an
   eight-worker exact-LLVM Debug build. Runtime and source-line gates pass.
   Declaration/domain order intentionally remains the search order until
   Change 11 adds solve-before and source-order-independent scheduling.
8. **Complete.** Lower equality, relational, arithmetic, logical, conditional,
   unary, and four-state legality constraints with exact width and signedness.
   A validated source-ordered runtime expression graph now evaluates variables,
   constants, unary plus/minus, bitwise/logical negation, add/subtract/multiply/
   power/divide/modulo, bitwise and logical operators, ordinary/case equality,
   signed or unsigned relations, and conditional selection without recursive
   host evaluation. Arbitrary-width resize, sign extension, truncated
   arithmetic, comparison, and division preserve the typed operand rules.
   Four-state evaluation retains controlling logical values and conditional
   X/Z bit merging; case equality remains known while an ordinary predicate
   that finishes X/Z rejects explicitly instead of becoming a solution.
   Semantic class bindings now carry exact executable width and four-state
   domain. The application lowerer selects one exact specialization, converts
   bounded decimal/binary/octal/hex literals and parameters, maps canonical
   properties to solver variables, and emits complete dependencies. A source
   differential lowers `payload + super.base_value <= MAX` for the `MAX=3`
   specialization and solves the exact 32-bit signed graph. Runtime coverage
   proves signed ordering, width wrap, power/divide/modulo, logical and
   conditional four-state behavior, repeatability, and illegal unknown roots.
   Exact-LLVM Debug semantic-HIR, runtime, and source-line gates pass after
   eight-worker builds.
9. **Complete.** Implement `inside` ranges/sets, `dist` weights, and soft
   constraints with deterministic conflict and overflow handling. Class
   constraint parsing and owning HIR now retain `soft` wrappers, membership
   sets/ranges, `dist :=` per-value items, and `dist :/` across-range items as
   distinct source-spanned nodes with exact specialization bindings. Membership
   lowers to typed equality/range predicates. Distribution lowering accepts
   exact literals or bound parameters, resizes them to the selected property,
   and registers checked finite-domain weights. Across-range rational weights
   use a checked common denominator; overlapping and composed weights reject
   overflow transactionally. A caller-supplied replay word drives a stable
   weighted permutation without host RNG state. Multiple distributions compose
   deterministically, including an unsatisfiable empty intersection. Hard
   clauses dominate soft clauses; conflicting soft clauses use later
   declaration priority after complete bounded search, never exposing a partial
   candidate. Runtime proof covers exact `:=`/`:/` normalization, replay,
   conflict, overflow, and hard/soft priority. The semantic-HIR differential
   parses, lowers, weights, and solves a source `soft ... inside` plus `dist`
   pair for one exact class specialization. Exact-LLVM Debug semantic-HIR,
   runtime, and source-line gates pass after eight-worker builds.
10. **Complete.** Implement constraint implication, `if`/`else`, bounded
    `foreach`, and array/container element selection without arbitrary element
    limits. The class parser and owning HIR retain source-spanned structured
    blocks, implication, conditional constraint sets, optional else branches,
    and named-index foreach selections. Each foreach iterator receives an
    exact specialization-local 32-bit signed binding rather than being confused
    with a property. Runtime graph construction provides checked implication,
    conditional-constraint, and arbitrary-length conjunction composition.
    Application lowering maps scalar properties and each materialized bounded
    container element to independent typed solver variables; constant and
    foreach-local selections validate their actual bounds, and foreach expands
    every supplied element with no language-visible count ceiling. Empty
    containers use the true empty conjunction. A source differential parses
    and solves implication, if/else, and a three-element static-array foreach
    for one exact class specialization. Runtime coverage constrains and solves
    300 separately materialized elements, crossing the former class of
    arbitrary 256-element caps while remaining governed by the solver's
    explicit variable/domain/search budgets. Exact-LLVM Debug semantic-HIR,
    runtime, and source-line gates pass after eight-worker builds.
11. **Complete.** Implement `solve ... before` ordering, dependency-cycle
    diagnostics, and stable declaration-independent solver ordering. Class
    constraint parsing and owning HIR retain nonempty source-spanned before/after
    variable lists with exact specialization bindings. Application lowering
    expands each list pair into checked solver edges. Edge registration rejects
    self/unknown references and detects a path back to the earlier variable
    before mutating the graph, reporting the canonical identity reached by the
    cycle. Search uses a deterministic topological order with canonical
    variable identity as the ready-set tie breaker, so unconstrained source
    declaration order and variable IDs cannot affect the assignment walk.
    Distribution permutation seeding likewise uses canonical identity rather
    than registration ID. Result vectors remain indexed by stable variable ID,
    separating external assignment ownership from internal scheduling. Runtime
    proof declares variables out of lexical order, verifies canonical order,
    applies an overriding edge, and proves a reverse edge rejects
    transactionally. The semantic-HIR differential parses and applies source
    `solve payload before choice` and verifies the resulting topological
    positions. Exact-LLVM Debug semantic-HIR, runtime, and source-line gates
    pass after eight-worker builds.
12. **Complete.** Execute object `randomize()` with optional variable lists and
    inline `with` constraints transactionally, returning zero without partial
    writes when no solution exists. A dedicated runtime transaction registers
    every packed object property under its canonical identity: selected,
    enabled `rand`/`randc` properties receive their complete finite domain and
    unselected properties retain singleton current-value domains for constraint
    visibility. Class and caller-supplied inline constraint graphs configure
    the same solver before one call-local replay word is consumed. The complete
    assignment, packed widths, two-state legality, random-state ownership, and
    every revision increment are validated before a final non-allocating commit;
    unsatisfiable and resource-exhausted attempts expose no property or revision
    writes, and constraint-construction failure occurs before publication.
    Uniform distributions make unconstrained fields replay-sensitive without
    host RNG state. Live project builds carry the owning constraint HIR into
    simulation, resolve source `object.randomize()` as a built-in 32-bit result,
    canonicalize optional property lists, compose enabled inherited class
    blocks, and lower ordinary, soft, distribution, and solve-before
    expressions into the transaction. Source proof calls both
    `randomize(generated_value)` and `randomize()` under an `inside` class
    constraint across the application engine matrix; runtime proof covers joint
    class/inline constraints, selected-only publication, equal-seed replay,
    unsatisfiable rollback, domain/search exhaustion, and configuration failure.
    Exact-LLVM Debug runtime and application gates pass after eight-worker
    builds. Portable constraint-HIR persistence remains owned by Change 18.
13. **Complete.** Execute bounded `std::randomize` over local integral, enum,
    and supported container values with the same solver and seed semantics. The
    runtime foundation now accepts unique caller-owned packed targets, complete
    integral domains, explicit nominal enum domains, and materialized bounded
    container-element domains. One inline constraint factory configures the
    shared solver; results are fully staged and swapped into every target only
    after complete validation. Focused runtime proof covers a joint integral,
    enum, and two-element container solution plus unsatisfiable and domain-budget
    rollback. Source `std::randomize` now lowers writable packed locals into one
    owning SimIR operation with exact widths, signedness, nominal identity, and
    explicit enum domains. The interpreter derives a 64-bit selection from the
    project-seeded process stream, executes the shared transaction, then uses
    ordinary callable copy-out assignments to publish all targets. A named
    1,048,576-value source-service budget returns zero for domains beyond the
    bounded implementation instead of allocating without limit. LLVM O0/O2
    validation recognizes the operation and deliberately selects interpreter
    fallback until Change 18 adds the durable solver service boundary. The
    application random differential proves equal interpreter/compiled replay
    for a joint local integral/enum call; the runtime proof supplies the bounded
    container-element and rollback matrix. Exact-LLVM Debug frontend, LLVM,
    application, runtime, and source-line gates pass after eight-worker builds.
14. **Complete.** Run inherited virtual `pre_randomize` and `post_randomize`
    callbacks with checked failure containment and post-hook execution only
    after successful assignment. Randomize callback lookup walks the exact
    dynamic specialization before its base-specialization chain, requires a
    nonstatic zero-argument `void` function profile, and therefore selects the
    most-derived inherited override. Source-function execution now admits
    fallthrough completion for `void` methods while retaining guarded recursion,
    locals, assignments, and conditional behavior. `pre_randomize` runs before
    solver construction and its valid side effects remain visible even when the
    constraints are unsatisfiable; `post_randomize` runs only after a successful
    atomic assignment. An exception or unknown control in either callback is
    contained as a zero result. Pre-hook failure restores its property writes;
    post-hook failure restores the complete pre-call property and random-revision
    snapshot, so a solved assignment is never exposed without a completed post
    hook. Adding a second derived constraint also exposed and fixed inherited
    constraint bindings: every base-owned expression now receives bindings for
    all exact derived specializations by following specialization base edges.
    The application matrix proves most-derived callback counts, pre-without-post
    on an unsatisfiable object, pre/post failure rollback, and zero random
    revisions after callback failure. Exact-LLVM Debug frontend, semantic-HIR,
    application, and source-line gates pass after eight-worker builds.
15. **Complete.** Implement property `rand_mode` and constraint-block
    `constraint_mode` query/update methods with per-object state and access
    validation. The parser and owning class HIR retain constraint visibility;
    selected mode calls resolve random properties and inherited constraint
    blocks to canonical identities and enforce public, exact-owner local, and
    owner-or-derived protected access. Each heap object owns independent
    property and constraint enable maps. Query and checked 0/1 update services
    return 32-bit language values, disabled properties are excluded from the
    randomized variable set, and disabled blocks are omitted while configuring
    the shared solver. Class-state schema 5 durably carries the composed
    constraint-mode inventory so direct project and reloaded standalone
    artifact execution agree before Change 18 persists the complete HIR.
    Runtime proof toggles one of two objects and rejects nonrandom/missing
    selections; frontend proof covers allowed and denied visibility paths; the
    source application matrix proves query, disable, re-enable, revision,
    satisfiable-disabled-block, and restored-unsatisfiable behavior. Exact-LLVM
    Debug frontend, semantic-HIR, runtime, application, artifact-reload, and
    source-line gates pass after eight-worker builds.
16. **Complete.** Implement exact `randc` permutation cycles, reset/reseed rules,
    domain-change invalidation, and artifact-safe cycle state. Each random
    property retains a host-independent stream seed, exact-domain signature,
    cycle ordinal, and sparse used-value indices; no standard-library RNG or
    host pointer crosses the runtime boundary. A deterministic Fisher-Yates
    permutation visits every exact finite-domain value once. Constraint-limited
    cycles restart only after the remaining candidates are unsatisfiable, while
    an explicit exact-domain change discards incompatible indices. Cycle state
    is staged with the assignment and revision and commits only after a
    satisfied solve; unsatisfiable/resource paths preserve it. The heap accounts
    sparse indices against its storage limit and releases them on reset/reseed.
    Explicit reset replays the current seeded first permutation, while object
    reseed clears call ordinals, rederives property streams, and resets every
    cycle. The portable value/index representation is ready for Change 18's
    live-state artifact/service serialization. Runtime proof covers two complete
    four-value cycles, reachable constrained cycles, domain invalidation,
    reset/reseed replay, accounting, and rollback. The source application
    differential proves one complete constrained `randc` cycle across direct
    and standalone-artifact execution. Exact-LLVM Debug runtime, application,
    and source-line gates pass after eight-worker builds.
17. **Complete.** Add parse, resolution, unsupported-form, unsatisfiable,
    resource-budget, callback, stale/null, and solver-corruption negatives with
    cataloged diagnostics and transactional rollback. Stable parser entries now
    cover malformed class constraints through distribution, conditional,
    foreach, and solve-before syntax. Resolution rejects nonrandom object lists,
    invalid mode arity/selections, and exact public/protected/local access with
    `FSIM-SV-CLASS-019` through `021`. Elaborator diagnostics
    `FSIM-ELAB-SVRAND-001` through `004` reject empty, nonidentifier, nonlocal,
    container, and over-width scope arguments; the standard `std::randomize`
    name is explicitly excluded from user-package import resolution. Runtime
    proof covers unsatisfiable, search/domain/elapsed and heap-storage budgets,
    callback failure, null/stale handles, malformed exact domains, and a corrupt
    clause that remains undetermined at a complete assignment. Every staged
    property, revision, mode, cycle, accounting, and container value remains
    unchanged on failure. The diagnostics catalog, frontend, random application,
    core application, runtime, and source-line gates pass after eight-worker
    builds.
18. **Complete.** Preserve constraint HIR, solver provenance, seeds, modes, and
    `randc` state through interpreter, LLVM O0/O2 service boundaries,
    debugger/callback/trace inspection, `.fsimobj`, `.fsimdesign`, mapped
    `.fsimlib`, relocation, and cold/warm/edit caches. A checksummed
    `sv-constraint-hir` payload with schema 1 now owns flattened class
    declarations, typed constraint trees/bindings, composed overrides, and
    default modes. Its structural validator rejects duplicate/missing class,
    property, constraint, base, and composition identities before publication.
    Standalone loading requires and restores that payload instead of fabricating
    an empty HIR; object and mapped-library flows continue rebuilding the same
    owning projection from their portable parsed units. Relocation proof removes
    the original source/object and executes the restored constraints and exact
    seeded `randc` cycle. A stable inspection surface exposes property and
    constraint paths, enable state, revision, stream seed, domain signature,
    cycle, and used count to debugger, callbacks, and trace backends without
    host pointers or RNG objects. Debugger and callback proofs observe all three
    staged cycle commits. `ScopeRandomize` remains an intentional validated
    interpreter fallback because the current JIT resume ABI has no solver
    status; its complete target/domain shape is now included in native cache
    identity so later service lowering cannot reuse an incompatible object.
    Exact-LLVM Debug LLVM, application, random, artifact, relocation/cache,
    diagnostics, and source-line gates pass after eight-worker builds.
19. **Complete.** Add complete positive differentials and synchronize
    architecture, language support, diagnostics, feature matrix, inventories,
    UVM readiness boundaries, and restart evidence. Architecture and language
    support now describe canonical constraint HIR, exact-specialization
    lowering, finite solver/resource policy, independent object modes and
    streams, transactional object/scope randomization, callbacks, exact-domain
    `randc`, inspection, artifacts, and the intentional scope interpreter
    service boundary. Feature rows `SV-713` through `SV-722` own the positive,
    negative, elaboration, and cross-engine/artifact evidence; class constraint
    solving/randomization is removed from the deferred inventory while
    covergroups and UVM library/runtime closure remain assigned to later locked
    batches. The reviewed baselines are 1,870 diagnostics, 537 bounded sources,
    627 SPDX-owned artifacts, 212 test/control files, 1,152 executable rows,
    4,608 evidence cells, 389 exact paths, and 108 runtime owners. Catalog,
    source, legality, SystemVerilog, inventory, differential, resource, and
    release-candidate gates pass.
20. **Complete.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. Exact
    LLVM 22.1.8 Debug passes 112/112 in 139.03 seconds and Release passes
    112/112 in 114.29 seconds. Both configurations include the catalog, source,
    legality, installed-public-contract, MSVC Debug/Release, Windows LLVM,
    inventory, differential, resource, and release-candidate gates. The single
    accumulated Changes 2-20 commit and push close the batch. Batch 149 is not
    a CI boundary and runs no sanitizer or hosted CI-monitoring gate.

## Locked remaining v2 batch roadmap

The remaining v2 release sequence is Batches 150-175. Every batch has exactly
20 changes. The compact allocations below partition all 20 changes; before a
batch starts, its ranges are expanded into 20 individually numbered status
entries without changing scope or priority, then the expanded restart plan is
committed and pushed as a documentation-only precursor and implementation
starts in a fresh context. For Batches 150-175, Changes 1-19 remain one
recoverable accumulated worktree and Change 20 owns full gates, documentation,
one implementation commit, and one push. Local builds use at least eight
workers. GitHub
Actions uses four. Only Batches 150, 160, and 170 run the LLVM-disabled
sanitizer locally immediately before commit and then monitor and repair every
non-documentation CI failure; hosted CI excludes sanitizer instrumentation.

For this roadmap, “language closure” means the standardized digital surfaces
of VHDL-2008 plus embedded PSL, Verilog-2005, SystemVerilog-2017, VITAL, UVM
1.2/UVM 2020-3.1, DPI, VPI, and VHPI, followed by the listed older language
revisions. VHDL-AMS analog semantics, proprietary packages/pragmas, full
Accellera SystemC kernel/TLM/AMS/CCI compatibility, GUI/reverse execution,
parallel simulation, coverage products beyond language-defined assertion/UVM/
covergroup data, standalone AOT executables, and Python/notebook packaging are
not v2 requirements unless the user explicitly expands scope. Every currently
deferred standardized digital-language row must either execute by Batch 175 or
carry an explicit evidence-backed scope disposition approved by the user.

### Batch 150 - SystemVerilog real, time, string, and foreign scalar closure

1. **Complete.** Own and parse `shortreal`, `real`, and `realtime` declarations
   plus decimal/exponent, time-unit, and special real/time literal forms with
   exact source spans and stable diagnostics. A focused scalar header now owns
   distinct shortreal/real/realtime/time type identities and exact canonical
   decimal payloads as digits times a signed power of ten, with source time
   units retained before semantic conversion. Module ports/variables, function
   returns/arguments/locals, tasks, class properties, typedefs, and type actuals
   recognize the scalar families. Literal spans cover unit suffixes;
   `FSIM-SV-PARSE-281` and `282` reject malformed/excessive exponents and
   lexically adjacent invalid units without consuming a whitespace-separated
   delay target. The complete frontend, semantic-HIR, catalog, and source gates
   pass after an eight-worker Debug build; the source gate covers 538 files.
2. **Complete.** Resolve exact real/time type propagation and conversions and
   implement deterministic constant folding for literals, unary/binary
   expressions, conditions, casts, and mixed integral operands. A centralized
   scalar folding service propagates shortreal/real/realtime/time kinds through
   identifiers, promotion, predicates, conditionals, and explicit casts. It
   converts the Change 1 exact decimal representation with locale-independent
   `from_chars`, requires IEC 559 binary32/binary64, retains canonical bits,
   applies exact source time-unit scaling, and uses checked signed arithmetic
   for pure integral operands. Constant proof covers real arithmetic and
   comparison, conditional branch typing, binary32 casts, nearest integral/time
   conversion, truth, and physical time scaling. Exact-LLVM Debug frontend,
   catalog, and 540-file source gates pass after eight-worker builds.
3. **Complete.** Preserve real/time constants, parameters, localparams, package
   values, defaults, specialization identity, and checked dependency ordering.
   A dedicated scalar environment now remains separate from the existing
   width/four-state integral and string environments. It evaluates typed and
   inferred scalar actuals/defaults in declaration order under each unit's
   exact timeunit/timeprecision context, applies declared conversion once,
   reconstructs source-independent typed expressions, and keys specialization
   identity from canonical kind plus IEEE/tick bits. Specialized package values
   import without host-integer loss, and parent values propagate through child
   overrides and generate parameter sites. Focused proof covers dependent
   real/localparam values, package real/realtime constants, nearest time
   conversion, distinct override identities, and rejected forward dependency.
   The SystemC construction-specialization tail was split into its own source
   to retain the 2,000-line contract. Exact-LLVM Debug elaboration, catalog,
   and 542-file source gates pass after eight-worker builds.
4. **Complete.** Close real/time net/variable ports and function/task argument,
   return, default, direction, overload, and cross-boundary profiles. Explicit
   `var` ports plus composite `wire`/`tri` real/time net declarations now retain
   net provenance independently from the scalar data type. Elaborated signal
   profiles carry shortreal/real/realtime/time identity; exact same-language
   module boundaries accept input/output/inout profiles while mismatched or
   mixed-language scalar profiles fail before packed adaptation. Specialized
   callable and port defaults receive scalar substitution/type propagation,
   and interface exports match complete return, formal kind, direction, and
   ref profiles rather than names alone. Proof covers `input var real`, `output
   var shortreal`, `inout wire time`, real/time nets and variables, real return
   plus realtime/time defaults, ref task formals, exact interface exports, and
   rejected scalar port/export mismatches. Exact-LLVM Debug frontend,
   elaboration, semantic-HIR, catalog, and 542-file source gates pass after
   eight-worker builds.
5. **Complete.** Add portable deterministic IEEE-754 `shortreal`/`real` and
   exact-tick `time`/`realtime` runtime storage plus arithmetic kernels and
   checked materialization budgets. The engine-neutral runtime value owns raw
   binary32, binary64, or unsigned tick bits with canonical kind identities.
   One shared add/subtract/multiply/divide kernel applies deterministic scalar
   promotion, binary32 result rounding, binary64 operations, and checked exact
   tick arithmetic; it rejects zero divisors, tick overflow/underflow,
   nonfinite values/results, invalid kinds, and non-nearest host rounding
   modes. The scalar arena uses stable 16-byte canonical accounting and
   independent value, byte, and operation ceilings with checked 32-bit IDs,
   load/store, and result materialization. Runtime proof covers negative zero,
   exact payloads for binary32 and binary64 arithmetic, realtime/tick
   promotion, every checked arithmetic failure, invalid IDs, nonfinite values,
   and exhausted storage/work budgets. Exact-LLVM Debug runtime, catalog, and
   544-file source gates pass after eight-worker builds.
6. **Complete.** Implement runtime comparison, logical truth, integral/real/time
   conversion, rounding, classification, and unknown/overflow behavior. The
   shared scalar kernel compares signed integral and unsigned time values
   exactly beyond binary64 precision, applies IEEE predicates to real-family
   values, and defines finite-only logical truth. Checked conversions cover
   signed integral, unsigned ticks, shortreal, real, and realtime with explicit
   nearest-away, truncation, floor, and ceiling modes plus binary32 narrowing.
   Packed ingress retains declared signedness and rejects X/Z as
   `UnknownValue`; packed egress rounds once and proves destination width/range
   before materialization. Classification distinguishes zero/sign, normal,
   subnormal, infinity, and NaN in the source binary32 or binary64 format.
   Proof covers exact large-tick and mixed-sign comparisons, negative zero,
   IEEE NaN equality/inequality, every rounding mode, signed/time overflow,
   real narrowing, known and unknown packed inputs, width overflow, and both
   float/double subnormal categories. Exact-LLVM Debug runtime, catalog, and
   544-file source gates pass after eight-worker builds.
7. **Complete.** Close real/time formatting, display/scan/file conversion,
   delay scaling, event scheduling, and time-system-function behavior. A shared
   engine-neutral text/time service formats canonical binary32, binary64,
   signed-integral, and exact unsigned-tick values with general/fixed/
   scientific/decimal/time modes, explicit precision/width/padding/suffix
   controls, locale-independent `to_chars`, and bounded output. Its matching
   bounded scanner trims only defined ASCII whitespace, consumes complete
   inputs, preserves integer text beyond binary64 precision, rounds decimal
   conversions once under an explicit policy, and distinguishes malformed,
   nonfinite, overflowing, and resource-exhausted input. Descriptor-level
   transactional file plumbing remains deliberately owned by Change 15; all
   display, scan, and text-file paths now have one conversion contract to call.
   Exact time contexts validate timeunit/timeprecision/project-resolution
   divisibility. Integral delays scale without floating-point conversion;
   real/realtime delays round half away from zero exactly once at declared
   precision before checked project-tick multiplication, and scheduling rejects
   negative values and target-tick overflow. `$time` rounds exact project ticks
   into local units, `$stime` applies its 32-bit unsigned result width, and
   `$realtime` returns local-unit binary64 under nearest host rounding. Focused
   proof covers locale-independent padded/suffixed output, exact values beyond
   2^53, signed/real scans, malformed/nonfinite/capped input, half-quantum
   delays, negative/context/arithmetic failures, scheduling overflow, every
   time function, and host-rounding rejection. Exact-LLVM Debug runtime,
   catalog, and 546-file source gates pass after eight-worker builds.
8. **Complete.** Expose real/time values consistently through interpreter,
   LLVM O0/O2, debugger mutation/inspection, callbacks, trace snapshots, and
   VCD policy. SimIR signals and debug locals now retain their exact scalar
   kind while a known packed carrier holds only the canonical raw binary32,
   binary64, or tick payload; scalar arithmetic and conversion never acquire
   packed semantics. Typed interpreter and application APIs own checked
   deposit, force, scheduling, inspection, snapshots, debug-local mutation,
   and independent change observers. Partial scalar updates, resolution,
   strength/charge behavior, unknown carriers, kind/width mismatches, and
   nonfinite ingress reject before publication. The debugger formats and scans
   these values through the shared Change 7 service. Trace state retains scalar
   kinds: real, shortreal, and realtime emit VCD real declarations/changes,
   including negative zero, while `time` remains an exact 64-bit vector so
   values beyond 2^53 cannot round through binary64. Runtime proof covers
   transport, callbacks, snapshots, debug locals, rejection, and VCD policy;
   LLVM and source-application differentials exercise the same raw payloads in
   interpreter, compiled O0, and compiled O2 modes, including signal
   load/store. Exact-LLVM Debug runtime, LLVM, application-time, catalog, and
   548-file source gates pass after eight-worker builds.
9. **Complete.** Own and resolve `chandle` declarations, `null`, equality,
   inequality, assignments, casts, parameters, ports, and callable profiles.
   The frontend now admits the opaque 64-bit identity as a variable data type
   in module, procedural, class, typedef, value/type-actual, port, function,
   and task positions without admitting it as a net or numeric type. Contextual
   resolution types `null`, assignments, returns, formal actuals, and
   `chandle` casts; only equality and inequality with another `chandle` or
   `null` produce an integral predicate. Arithmetic, logical truth,
   numeric/aggregate casts, and incompatible assignments reject through stable
   `FSIM-SV-SEM-174`/`175` diagnostics. The constant layer keeps null handles
   nonnumeric while preserving canonical specialization identity, dependent
   aliases, predicates, casts, and rejecting numeric parameter actuals.
   Same-language port and interface callable profiles require exact `chandle`
   identity, including return, direction, `ref`, and default-null formals.
   Non-null host identity, generation/lifetime checks, alias execution,
   debugger display, and callbacks remain exclusively Change 10 scope. Focused
   exact-LLVM Debug frontend, elaboration, SystemVerilog-HIR application,
   catalog, and 549-file source gates pass after eight-worker builds.
10. **Complete.** Implement host-safe opaque `chandle` identity, null/stale
    checks, alias transfer, lifetime policy, debugger display, and callbacks
    without serializing or exposing host pointers. A simulation-owned registry
    now issues generation-qualified 64-bit slot identities with zero reserved
    for `null`; neither snapshots, callbacks, traces, nor debugger output carry
    a host address or cleanup closure. Nonowning alias transfer preserves exact
    identity and publishes bounded metadata, while explicit release, clear, or
    registry destruction stales every alias and invokes foreign cleanup once.
    Checked live-handle, metadata-byte, observer, slot, generation, and alias
    budgets reject before publication. Cleanup failure cannot resurrect an
    identity, observer failure cannot roll back a published operation, and
    creation metadata allocation is transactional. Typed SimIR and application
    deposit/force paths validate live identities; interpreter and compiled
    O0/O2 signal access, independent scalar and registry callbacks, snapshots,
    debugger list/inspect/mutation, and VCD exact-vector output retain only the
    simulator identity. Focused proof covers null/live/stale states, reuse,
    equality, alias counts, one-shot and throwing cleanup, observer containment,
    resource exhaustion, public/debugger stale rejection, trace policy, and
    source execution in all three engines. Exact-LLVM Debug runtime,
    application-time, catalog, and 552-file source gates pass after an
    eight-worker build.
11. **Complete.** Replace byte-oriented SystemVerilog string execution with
    deterministic Unicode code-point indexing, length, slicing, assignment,
    comparison, iteration, and conversion semantics. One engine-neutral strict
    UTF-8 service now decodes only Unicode scalar values, rejects overlong,
    truncated, surrogate, bad-continuation, and above-U+10FFFF encodings, and
    exposes stable source-order code-point values plus byte spans. Length,
    inclusive slice, indexed read/replacement, equality/ordering, `getc`,
    `putc`, `substr`, case-insensitive ASCII folding over Unicode iteration,
    and numeric conversion all share that service. Indexed reads and mutation
    now use 32-bit scalar values; replacement re-encodes one complete code
    point transactionally and enforces the existing 4,096-byte storage bound.
    String-object ingress validates strict UTF-8, native validation rejects an
    invalid literal before compilation, and the renamed code-point SimIR/JIT
    operation invalidates byte-semantic native cache identities. Runtime proof
    covers one- through four-byte scalars, offsets, length/index/slice,
    expansion, scalar ordering, iteration, conversions, every malformed class,
    rollback, and resource exhaustion. Exact-LLVM Debug interpreter, LLVM
    O0/O2, mutable-string source application cold/warm/edit, catalog, and
    555-file source gates pass after eight-worker builds.
12. **Complete.** Implement the remaining standard string methods, including
    case conversion, comparison, substring/search, numeric conversion, and
    formatting, with locale-independent behavior. `toupper`/`tolower` now
    validate and traverse strict Unicode storage while applying only the
    language-defined ASCII mapping; non-ASCII scalar values remain unchanged.
    `compare`/`icompare`, inclusive `substr`, `getc`/`putc`, and indexed string
    expressions share Change 11 code-point order and 32-bit scalar results.
    Decimal, hexadecimal, octal, and binary parse/format methods retain exact
    32-bit wrap/sign behavior, underscore handling, and ASCII-only radix rules.
    The missing `atoreal` and `realtoa` methods now use the Change 7 scanner and
    formatter: binary64 payloads cross interpreter and native fallback without
    decimal or host-locale round trips, malformed real text returns the defined
    zero value, and canonical finite/nonfinite output is bounded. Parser arity,
    result kind/width, SimIR validation, native register transport, method
    mutation, and cache identity all include the completed method set. The
    mutable-string source matrix exercises every standard method plus
    `$sformat`/`$swrite`/`$sformatf`, Unicode substring/index behavior, real
    conversion, cold/warm/edit caches, and interpreter/LLVM O0/O2 equivalence.
    Exact-LLVM Debug frontend, elaboration, runtime, LLVM, application,
    catalog, and 555-file source gates pass after eight-worker builds.
13. **Complete.** Close real/time/string/chandle members, assignment patterns,
    fixed/dynamic arrays, queues, associative arrays, and nested unpacked
    aggregate/container copy and comparison behavior. Container HIR now retains
    one complete recursive element type, including heterogeneous named
    unpacked-struct members, while shared elaboration materializes exact
    packed, scalar, string, nested-container, and aggregate runtime profiles.
    Value-owned runtime storage, initialization, resize, resource accounting,
    copy, conditional selection, and logical/case equality recurse through
    strings, nested containers, and aggregate member boxes; real comparison is
    numeric, time/chandle comparison preserves exact raw identity, and strings
    use validated UTF-8 byte identity. Scalar assignment patterns and selected
    writes cover real/time/chandle carriers; whole string and recursive
    composite values support default construction, resize, independent copy,
    comparison, debugger formatting, object transport, and native-cache
    round trips. Packed-only slice, reduction, locator, ordering, and selected
    composite mutation paths reject before emitting unsafe operations through
    cataloged diagnostics. Focused frontend, elaboration, dedicated composite-
    container elaboration, runtime, LLVM, time/string/aggregate application,
    diagnostic-catalog, and 559-file source gates pass after eight-worker Debug
    builds; `git diff --check` is clean.
14. **Complete.** Close package, import, parameterized specialization,
    function/task, recursion, static/automatic lifetime, and multi-root behavior
    for all four scalar families. Scalar and string parameter substitution now
    traverses callable defaults, locals, nested statements, outputs, task
    actuals, and return expressions, so imported package functions and each
    parameterized child receive their exact specialized real/time/string/
    chandle constants. Built-in scalar cast marks are visible even in a child
    whose scalar types occur only in callables; canonical real/time casts keep
    their full 32/64-bit payload width. A typed scalar-binary SimIR operation
    and shared payload service execute arithmetic and comparisons in callable
    bodies through interpreter and native O0/O2 paths with validated register
    kinds, artifact/cache identity, exact chandle equality, and checked runtime
    errors. Static packed/scalar and string callable locals persist, automatic
    locals reinitialize, and automatic task copy-in/out covers real, time,
    string, and chandle values. Two independently selected roots instantiate
    distinct real/time/string specializations and static state while importing
    a package string function; both finish with all callable/lifetime checks
    set. Direct and indirect recursion retain their bounded deterministic
    `FSIM-ELAB-SVFUNC-006`/`FSIM-ELAB-SVTASK-008` rejection contract. Focused
    exact-LLVM Debug frontend, elaboration, runtime, LLVM, time/string/
    aggregate application, diagnostic-catalog, and 559-file source gates pass
    after eight-worker builds; `git diff --check` is clean.
15. **Complete.** Close bounded text/binary file I/O, scanning, formatting,
    descriptor/error behavior, and transactional copy-out for real/time/string/
    chandle values. `%e`/`%f`/`%g` scans and outputs now carry an exact scalar
    kind through file, display/monitor, and `$sformat*` SimIR; real/shortreal/
    realtime payloads use the shared locale-independent text service, and
    `time` accepts checked decimal or real text plus exact `%d`/`%t` output.
    `$fread` stages canonical 32/64-bit scalar payloads and scalar container
    elements before copy-out, while failed/overflowing scans leave the failed
    destination untouched and retain the standard prior-assignment count.
    Opaque chandle output preserves its exact hexadecimal identity, but text
    and binary input may restore only null so external bytes cannot fabricate
    a live or stale registry identity. Existing manifest confinement, handle
    ownership, close/EOF/error, seek, flush, lookahead, UTF-8, and byte/storage
    budgets remain shared by interpreter and compiled execution. Native
    validation and cache keys cover every format, target kind, width, two-state
    flag, and scalar discriminator; runtime artifact schema 9, constraint-HIR
    schema 2, native object schema 84, and scalar-file semantic revision 32
    invalidate older layouts. Focused proof covers e/f/g/t lowering, real/time/
    string/null-chandle text and binary round trips, underscore input,
    overflow and partial-conversion rollback, non-null chandle rejection,
    descriptor failures, runtime-state round trips, and O0/O2 interpreter,
    cold/warm, and edited-source parity. Exact-LLVM Debug frontend,
    elaboration, runtime, LLVM, scalar-file application, artifact, catalog,
    source-budget, and diff-whitespace gates pass after eight-worker builds.
16. **Complete.** Preserve every scalar family through `.fsimobj`, standalone
    `.fsimdesign`, relocated mapped `.fsimlib`, interpreter/LLVM service
    boundaries, and cold/warm/edit native caches with versioned schemas. The
    portable owning-unit codec now archives exact SystemVerilog decimal
    payloads, expression scalar identities, data-type scalar identities, and
    net provenance in the same canonical order as the standalone design codec.
    Owning-unit schema 8 and portable-library schema 5 reject older layouts,
    while deterministic round-trip proof covers real, shortreal, realtime,
    time, chandle, and negative decimal operands. Context-typed unary scalar
    constants convert once into the destination shortreal/real/realtime/time
    representation after object reload. The artifact-phase matrix now carries
    all five scalar carriers from source through object and standalone design,
    scalar-aware trace output, mapped-library export, physical relocation, and
    interpreter plus native O0/O2 cold/warm execution. Relocation preserves
    payloads and specialization keys, while an edited producer source changes
    both the executable payload and native key without reusing the prior
    semantic entry. Exact-LLVM Debug library/object/design artifact,
    application, diagnostic-catalog, and 559-file source gates pass after
    eight-worker builds; `git diff --check` is clean.
17. **Complete.** Add cataloged parse/type/profile/conversion, unsupported form,
    overflow/resource, invalid Unicode, stale/null, malformed artifact, and
    transactional rollback negatives. The focused frontend/elaboration matrix
    now proves malformed decimal/unit parsing, incompatible chandle semantics,
    parameter and full scalar-profile mismatches, finite-conversion overflow,
    and unsupported runtime scalar operators through stable
    `FSIM-SV-PARSE-281`/`282`, `FSIM-SV-SEM-174`/`175`, parameter/binding, and
    `FSIM-ELAB-SVSCALAR-001`/`002` diagnostics. Runtime negatives cover checked
    arithmetic/conversion/delay overflow, divide-by-zero, unknown packed input,
    text and work/storage limits, all malformed UTF-8 classes, null/stale handle
    alias/release behavior, callback/cleanup failure, and registry budgets.
    Failed scalar stores, expanding Unicode replacements, and resource-rejected
    chandle creation now explicitly prove unchanged prior state and zero partial
    publication. Both the portable owning-unit and standalone design-state
    codecs validate scalar-kind and exact-decimal enumeration ranges during
    write and read; malformed internal state rejects through cataloged
    `FSIM-LIB-0006` or `FSIM-ART-0013` before an artifact is published. Exact-
    LLVM Debug frontend/elaboration, runtime, library/application artifact,
    diagnostic-catalog, and 559-file source gates pass after eight-worker
    builds; `git diff --check` is clean.
18. **Complete.** Add complete cross-engine, optimization, debugger/callback/
    trace, scheduling, multiple-root, artifact/relocation, and cache positive
    differentials for all Batch 150 surfaces. The scalar-surface application
    now compares canonical real, shortreal, realtime, time, chandle, callable,
    and derived-result payloads plus callback counts directly across the
    interpreter and LLVM O0/O2. Its debugger exercises typed show/deposit/
    force/release, live and stale chandle inspection, snapshots, and independent
    scalar/registry observers, while VCD proof distinguishes real-family
    declarations from exact 64-bit time/chandle vectors. The time matrix compares
    rounded scheduling timestamps and byte-identical VCD output across engines
    and O0/O2 cold/warm caches. Existing focused matrices add strict-Unicode
    strings and methods with edited caches, scalar file I/O, recursive aggregate
    and independently specialized multiple-root state, standalone/object and
    relocated mapped-library artifacts, and edited semantic/native keys. Exact-
    LLVM Debug runtime, LLVM, application/artifact, time, mutable-string, scalar-
    file, aggregate/multiple-root, diagnostic-catalog, and 559-file source gates
    pass 9/9 in 34.15 seconds after eight-worker builds; `git diff --check` is
    clean.
19. **Complete.** Synchronize architecture, language support, diagnostics,
    feature rows, deferred boundaries, inventories, UVM readiness, and restart
    evidence. Architecture and language support now own the distinct real/time
    scalar plane, strict Unicode-scalar strings, generation-safe chandles,
    recursive composite/file transport, artifact schemas, and the explicit
    Batches 151-162 boundary through arbitrary-width, SVA/coverage, foreign
    interfaces, and UVM closure. Feature rows `SV-723` through `SV-732` own the
    positive, negative, implementation, and differential evidence; completed
    real-family/chandle/Unicode work is removed from the deferred data-model
    row. Artifact diagnostics explicitly cover invalid scalar enumerations.
    The reviewed baselines are 1,876 diagnostics, 559 bounded sources, 649
    SPDX-owned artifacts, 217 test/control files, 1,162 executable rows, 4,648
    evidence cells, 409 exact paths, and 110 runtime owners. Inventory and
    release-candidate gates pass, with the evidence matrix and sorted path set
    frozen by reviewed SHA-256 digests.
20. **Complete.** Run the LLVM-disabled sanitizer, exact-LLVM Debug and
    Release, source/catalog/inventory/installed-public/Windows ABI/differential/
    release gates after eight-worker builds, commit and push once, then inspect
    and repair every non-documentation hosted CI job. Local qualification is
    complete: the LLVM-disabled ASan/UBSan suite passes 109/109 in 760.62
    seconds with leak detection disabled because the managed runner executes
    under ptrace; address and undefined-behavior checks remain enabled. Exact
    LLVM 22.1.8 Debug passes 112/112 in 349.47 seconds and Release passes
    112/112 in 304.22 seconds. The boundary repaired one LLVM-disabled class-
    cache assertion so fallback execution requires zero cache activity, and
    replaced two synthesized-port aggregate constructions with explicit
    default-initialized declarations to avoid GCC 13's `-O3` false-positive
    move warning for a disengaged recursive optional delay. The accumulated
    implementation and qualification commits were pushed. The project
    source-size policy now uses a 2,500-line hard limit and requires any file
    that exceeds it to be refactored below 2,000 lines. The local ten-batch
    sanitizer cadence remains mandatory, while hosted CI excludes sanitizer
    instrumentation, including ASan/UBSan from its libFuzzer smoke target.
    The sanitizer-free Clang 22 libFuzzer target builds with eight workers and
    completes the exact 20,000-run hosted smoke command locally. Exact-LLVM
    Debug and Release each pass the application plus seven policy/release gates
    8/8 after the portability repairs. Pre-change hosted run `31049629546`
    reached and failed Linux test suites and both clang-cl Windows test suites;
    the Windows application host fail-fast entered class integration without
    further diagnostics, so the next push carried bounded class subphase traces.
    Those traces localized `0xc0000409` after artifact creation: the test kept
    its class-source input stream alive until function exit and then renamed
    that still-open file, which POSIX accepts but Windows rejects. The stream is
    now destroyed before the artifact rename; exact-LLVM Debug and Release
    `fsim.application` pass locally in 29.33 and 28.18 seconds. Replacement
    hosted run `31054730031` passes all eleven jobs, including both Windows
    MSVC test suites and both clang-cl test suites; this closes Batch 150.

### Batch 151 - Arbitrary-width packed values and aggregate closure

1. **Complete.** Replace the remaining host-word SystemVerilog constant representation with
   resource-governed arbitrary-width packed storage, retaining exact width,
   signedness, two-/four-/nine-state domain, nominal type, source, display, and
   canonical identity. `SystemVerilogConstantValue` now owns the shared
   arbitrary-width packed representation and retains low-word mirrors only for
   the explicitly deferred Changes 5-8 arithmetic fast path. Literal,
   parameter conversion, constant-function case matching, specify projection,
   display/expression substitution, and `svconst-v2` canonical identities use
   that owning value. A checked 16,777,216-bit materialization limit rejects
   oversized types before resize/allocation; diagnostic fallback uses a safe
   32-bit sentinel. Focused evidence covers 128-bit four- and two-state
   parameters, a 96-bit all-Z parameter, exact domain-distinct identities,
   the resource rejection, and updated specify/mixed identity consumers.
   Exact-LLVM Debug builds with eight workers; semantic HIR, elaboration,
   parameter sizing/type parameters, diagnostic-catalog, and source-budget
   gates pass 7/7, and `git diff --check` is clean.
2. **Complete.** Parse and materialize arbitrary-width binary/octal/hexadecimal/decimal,
   unbased-unsized, X/Z, concatenation, and replication constants with checked
   width/work/storage accounting and deterministic resource diagnostics. A
   portable word-vector decimal multiply/add path admits exact sized and
   unsized values without `__int128`; signed unsized sizing reserves its sign
   bit, explicit sizing truncates deterministically, and all paths materialize
   the shared packed owner. Binary/octal/hex digits, concatenation, and
   replication now scale to the 16,777,216-bit storage boundary. A separate
   67,108,864-unit work budget bounds decimal conversion and the current
   append-based concatenation/replication algorithm before allocation. Focused
   evidence adds sized and unsized 128-bit decimal values, 128-bit
   concatenation/replication, and a 10,000,000-bit replication work rejection.
   The eight-worker exact-LLVM Debug build plus seven semantic HIR,
   elaboration, parameter, catalog, and source gates pass; `git diff --check`
   is clean.
3. **Complete.** Preserve wide constants through typed parameter/localparam/specparam,
   package/import, generate, range, specialization-key, and
   cross-language constant-boundary evaluation without host-integer loss.
   Specialized units now export their exact integral constant environment, so
   wildcard and qualified package imports reconstruct the owning packed
   expression instead of requiring an `int64_t` package value. Generate-case
   equality compares sign-extended packed states at arbitrary width; range
   substitution projects a wide value to an integer expression only when the
   exact value fits. Checked wide-to-host conversion likewise admits small
   sign-extended values without accepting lossy high bits. Evidence covers
   package/local/top propagation, 128-bit specparams, high-bit-distinct child
   specialization keys, selected wide generate cases, a five-bit range driven
   by a 128-bit value, and a lossless SystemVerilog-to-VHDL natural generic;
   the existing high unsigned boundary still rejects. The eight-worker exact-
   LLVM Debug build and nine semantic HIR, elaboration, parameter, generate,
   mixed-conversion, catalog, and source gates pass; `git diff --check` is
   clean. Wide enum-base semantics remain in Changes 9-12 as allocated.
4. **Complete.** Carry arbitrary-width exact packed profiles and initial values through
   frontend types, semantic HIR, elaborated signals, SimIR registers, runtime
   admission, and interpreter/LLVM validation. The Change 1 owner now projects
   directly into `PackedLogic4`, so no host-word reconstruction remains at the
   specify/runtime admission boundary. Focused source evidence drives a
   128-bit four-state signal, a domain-distinct 128-bit two-state signal, and a
   96-bit all-Z signal from typed/package parameters through elaborated width/
   domain profiles, SimIR loads/stores, and interpreter execution. The shared
   runtime representation already retains exact nine-state planes, and its
   runtime plus LLVM validation suites remain green. The eight-worker exact-
   LLVM Debug build and eleven semantic HIR, elaboration, runtime, LLVM,
   parameter, generate, mixed-boundary, catalog, and source gates pass;
   `git diff --check` is clean.
5. **Complete.** Implement arbitrary-width signed/unsigned arithmetic, unary
   operations, divide/modulo/power overflow rules, and checked destination
   sizing. The owning packed representation now evaluates exact add/subtract,
   bounded shift-and-add multiplication, long division/remainder, and
   exponentiation by squaring without host-word projection. Signed division
   truncates toward zero, signed remainder retains the dividend sign, negative
   powers preserve the established zero/one/minus-one rules, and the signed
   minimum divided by minus one rejects before mutation. Unary negation and
   complement operate across every packed bit; unknown arithmetic produces an
   exact-width X result. Destination-width wrap and declaration truncation are
   explicit, while quadratic operations and power accumulation enforce the
   separate constant-work budget. Evidence covers high-bit 128-bit unsigned
   results, signed negative operands, divide/modulo/power, unary operations,
   wrap/truncation, unknown propagation, signed-overflow rejection, and a
   9,000-bit multiplicative-work rejection. The eight-worker exact-LLVM Debug
   build and eleven semantic HIR, elaboration, runtime, LLVM, parameter,
   generate, mixed-boundary, catalog, and source gates pass; `git diff --check`
   is clean.
6. **Complete.** Implement wide logical/case/wildcard equality, relational
   comparison, logical/arithmetic shifts, and unknown/sign extension semantics.
   Logical truth now scans the owning value rather than its low-word mirror;
   ordinary, exact case, and one-sided wildcard equality preserve arbitrary-
   width X/Z policy. Signed comparison first resolves the sign partition and
   then compares every packed bit, so unequal-width signed operands are
   correctly extended. Binary bitwise operations implement four-state
   dominance at every bit. Logical and arithmetic shifts accept exact wide
   counts, saturate counts beyond the destination without host conversion,
   propagate an unknown count to an exact-width X value, and copy the original
   Logic4/Logic9 sign state for arithmetic fill. Evidence covers 128-bit
   logical, equality, wildcard, signed/unsigned relational, bitwise, left/
   right/arithmetic, unknown-count, all-Z sign-fill, unequal-width extension,
   and huge-count cases. The eight-worker exact-LLVM Debug build and eleven
   semantic HIR, elaboration, runtime, LLVM, parameter, generate, mixed-
   boundary, catalog, and source gates pass; `git diff --check` is clean.
7. **Complete.** Implement wide streaming, concatenation/replication,
   reductions, bit/part/indexed selectors, selected updates, and assignment
   patterns. Before adding semantics, constant display and parameter-
   substitution services moved into a dedicated 520-line translation unit,
   reducing the 2,493-line evaluator to 1,984 lines as required by the source-
   limit policy; the completed evaluator remains at 2,135 lines. Streaming now
   reverses arbitrary-width slices directly in owning packed storage with
   width/work accounting and exact Logic9 preservation. Every reduction scans
   all bits with four-state dominance. Bit, fixed part, ascending, `+:`, and
   `-:` selections preserve X/Z and synthesize X for out-of-range bits;
   constant-function selected assignments update one bit or part transactionally
   while retaining untouched regions. Positional and scalar-default flat packed
   patterns materialize at the checked destination width, and ambiguous wide
   conditionals merge identical states bitwise. Evidence covers 128-bit stream,
   reductions, all selector forms, huge/out-of-range selection behavior, wide
   `inside`, selected function writes, and positional/default patterns. The
   eight-worker exact-LLVM Debug build and eleven semantic HIR, elaboration,
   runtime, LLVM, parameter, generate, mixed-boundary, catalog, and source gates
   pass; `git diff --check` is clean.
8. **Complete.** Complete wide casts, queries/system functions, interpreter and
   LLVM O0/O2 execution, and exact unknown-state behavior across all scalar
   domains. Integral constant casts now cover `bit`, `logic`/`reg`, `byte`,
   `shortint`, `int`, `longint`, four-state `integer`, and resolved named packed
   typedefs, with widths governed by the shared 16,777,216-bit limit. Named
   runtime casts likewise accept that governed width rather than a host-word
   ceiling; interpreter evidence zero-extends a 96-bit all-Z operand into an
   exact 128-bit named type. Two-state constant casts reject X/Z loss while
   four-state and nine-state conversions preserve or deliberately collapse
   every source state. Owning constants retain declared packed bounds, so
   `$bits`, `$left`, `$right`, `$low`, `$high`, `$size`, `$increment`,
   `$dimensions`, and `$unpacked_dimensions` distinguish descending and
   ascending 128-bit declarations even through parameter folding. `$clog2`
   scans arbitrary-width known magnitudes without host conversion and rejects
   negative or unknown operands. The parameter-sizing application executes 58
   captured outputs identically through the interpreter and compiled O0/O2
   engines, including wide query/cast results and preserved unknown bits, while
   retaining four compiled processes and cold/warm native-cache assertions.
   The eight-worker exact-LLVM Debug build and eleven semantic HIR,
   elaboration, runtime, LLVM, parameter, generate, mixed-boundary, catalog,
   and source gates pass; `git diff --check` is clean.
9. **Complete.** Implement nested packed structs and anonymous packed
   aggregates with exact member layout, defaults, assignment patterns,
   selectors, and updates. A shared recursive aggregate-type parser now admits
   anonymous packed struct/union declarations in module and procedural type
   positions and nested anonymous packed members while retaining named-member
   behavior. It computes exact declaration-ordered layouts beyond a host word;
   focused evidence fixes a 137-bit outer layout at offsets 105, 8, and 0, a
   nested 97-bit layout at offsets 1 and 0, and a 25-bit procedural-local
   layout. Recursive default materialization retains Logic4 X leaves and Bit2
   zero leaves. Contextual nested assignment patterns apply independent inner
   and outer `default:` arms, and dotted member selectors plus whole-member,
   bit/part-select, and leaf updates preserve all untouched bits. The aggregate
   application compares twelve exact outputs through the interpreter and cold/
   warm LLVM O0/O2 engines, including 137-bit defaults, patterns, selected
   writes, and 41-bit nested selector results with cache reuse. Targeted
   negatives retain member-initializer ownership for Change 11 and anonymous
   unpacked-struct ownership for Batch 152. The eight-worker exact-LLVM Debug
   build and thirteen frontend, semantic HIR, elaboration, runtime, LLVM,
   aggregate/parameter/generate/mixed-boundary, catalog, and source gates pass;
   `git diff --check` is clean.
10. **Complete.** Implement unequal-width packed unions and tagged unions with
    deterministic active-member, padding, read/write, comparison, and cast
    behavior. Unequal-width payloads occupy the low bits of the maximum-width
    union storage and narrow construction or selected writes clear canonical
    high padding. Tagged unions add a compact ordinal discriminator above that
    payload; recursive default construction selects and initializes member zero,
    explicit `tagged member value` construction and keyed patterns select exact
    members, direct selected writes update the tag, active reads return the
    member payload, and inactive four-state reads return X. Raw casts and all
    equality forms operate on the canonical representation. Semantic HIR keeps
    tagged and untagged unions distinct, while focused negatives reject missing
    context, invalid members, and delayed selected writes that cannot update
    metadata coherently. A 23-output aggregate application proves defaults,
    patterns, reads, writes, padding, tags, casts, and comparisons identically
    through the interpreter and cold/warm LLVM O0/O2. `TaggedUnion` was appended
    to the retained aggregate-kind enumeration so existing artifact ordinals
    remain stable for Change 16 schema work. The eight-worker exact-LLVM Debug
    build and thirteen frontend, semantic HIR, elaboration, runtime, LLVM,
    aggregate/parameter/generate/mixed-boundary, catalog, and source gates pass;
    `git diff --check` is clean.
11. **Complete.** Complete anonymous enums/aggregates, enum base/range
    inference, member initializers, nested aggregate constants, and packed
    aggregate queries. Anonymous enums now retain explicit and inferred literal
    sequences, default to signed 32-bit `int`, admit explicit integral atom or
    vector bases/ranges, reject nonintegral bases, and construct the first
    declared value as their recursive object default. Packed members retain
    optional initializer expressions through specialization and semantic HIR;
    one contextual constant service validates and materializes scalar, nested
    struct, union, tagged-union, keyed, positional, and default-pattern values.
    Invalid incomplete nested member defaults reject with
    `FSIM-ELAB-SVAGG-007`. Typed aggregate localparams use the same service and
    preserve exact nested bits. Type-only and selected-object `$bits`, `$left`,
    `$right`, `$low`, `$high`, `$size`, `$increment`, `$dimensions`, and
    `$unpacked_dimensions` queries use retained aggregate layouts. The expanded
    32-output aggregate application proves an 11-bit recursive member default,
    a six-bit nested aggregate constant, a default-base anonymous enum value,
    and all six representative type-only queries identically through the
    interpreter and cold/warm LLVM O0/O2. The eight-worker exact-LLVM Debug
    build and thirteen frontend, semantic HIR, elaboration, runtime, LLVM,
    aggregate/parameter/generate/mixed-boundary, catalog, and source gates pass
    in 11.65 seconds; `git diff --check` is clean.
12. **Complete.** Enforce complete nominal assignment, parameter, port,
    callable, pattern, equality, and explicit-cast legality for packed structs,
    unions, and enums. One resolved nominal-identity rule now covers procedural
    and continuous assignments, automatic/static initialization, contextual
    nested patterns, function arguments and returns, task input and copy-out,
    constant parameters/localparams, and same-language hierarchy boundaries.
    Equality requires two values of the same nominal packed type; a matching
    explicit cast is legal, while a cast to another same-layout nominal type is
    not. Constant integer/logic substitution and explicit casts retain canonical
    identity, enum literals are converted to their declared base width without
    hiding raw out-of-range/duplicate validation, and selected static-array
    elements recover their retained nominal element type. Contextual aggregate
    parameter patterns and member initializers recursively enforce nominal
    member legality. A focused positive/negative matrix proves matching and
    mismatched structs/enums across defaults, overrides, aggregate/enum ports,
    callables, nested patterns, equality, and casts. The eight-worker build and
    sixteen semantic, HIR, frontend, elaboration, LLVM, runtime, function/task,
    aggregate/parameter/generate/mixed-boundary, catalog, and source gates pass
    in 12.88 seconds; `git diff --check` is clean and every touched source remains
    below the 2,500-line hard limit.
13. **Complete.** Preserve wide scalar and aggregate values through parameters,
    localparams, ports, interfaces, modports, nets/variables, multiple roots,
    and mixed SystemVerilog/VHDL boundaries without arbitrary length caps.
    Hierarchy specialization now carries the exact packed-constant environment
    beside the legacy integer environment through root preparation, ordinary
    roots, child recursion, and foreign-child entry; wide values therefore
    remain available for override evaluation, nominal-domain recovery, module
    initialization, and each independently selected root without an `int64_t`
    projection. Cross-language width and signedness adapters no longer impose
    the former 64-bit admission ceiling; their existing packed `Extract`,
    `Concatenate`, and signal operations retain the complete value. Focused
    proof covers a 137-bit nominal struct parameter/localparam and port across
    two roots, a 137-bit anonymous struct through a parameterized interface and
    consumer modport, and 129-to-137-bit plus 137-to-129-bit signed conversion
    across a VHDL/SystemVerilog boundary. The eight-worker build, elaboration,
    runtime, LLVM, parameter, interface, mixed-conversion, catalog, and source
    gates pass; `git diff --check` is clean and every touched source remains
    below the 2,500-line hard limit.
14. **Complete.** Preserve them through functions/tasks/methods, recursion,
    ref/inout/copy-out, static/automatic locals, classes, and process/object
    lifetime. Function returns and formals, task formals, static callable
    locals, and class method/constructor actuals no longer impose the former
    64-bit admission ceiling; every executable positive packed width now uses
    the owning `PackedLogic4` frame/register/property representation. A
    137-bit source fixture proves module and class functions/tasks,
    constructor actual/default binding, automatic locals, static retained
    locals, bounded method recursion, direct `ref`, `inout` and output
    copy-out, delayed task resumption, process-local lifetime, and inherited
    object-property lifetime with exact high-bit checks. Runtime method
    evidence separately proves arbitrary-width automatic frames, object
    properties, suspended continuations, copy-out, and bounded recursive
    calls. A narrow cache-only top preserves the existing native cold/warm/edit
    contract while complete arbitrary-width LLVM lowering remains allocated to
    Change 18. The eight-worker build, full application suite, and focused
    elaboration, function, task, suspending-task, runtime, catalog, and source
    gates pass; `git diff --check` is clean and every touched source remains
    below the 2,500-line hard limit.
15. **Complete.** Preserve exact values and profiles through debugger show/
    deposit/force/release, callbacks, snapshots, VCD/trace, files/memory, and
    public runtime service boundaries. Binary `$fread` now assembles every
    byte directly into arbitrary-width `PackedLogic4` storage instead of a
    `uint64_t`, retaining big-endian order, partial-read zero fill, non-byte-
    aligned truncation, and the bounded-file limit. Packed `$fread` targets
    and packed/scalar container elements accept every positive executable
    width; two-state validation scans all unknown-state words rather than the
    legacy low word. A 137-bit service fixture proves debugger show/deposit/
    force/masked-deposit/release, owning read snapshots, exact callback copies,
    VCD vectors, scalar and fixed-memory `$fread`, `$readmemh`, `$writememh`,
    and `$writememb`. Class property callbacks, packed trace snapshots, and
    VCD retain a separate 137-bit object value, while the public C API reports
    width/buffer requirements and round-trips exact 137-bit deposit, force,
    masked deposit, release, and read values. The eight-worker build plus
    fourteen semantic, HIR, frontend, elaboration/container, LLVM, full-
    application, procedural, file/container, API, runtime, catalog, and source
    gates pass; `git diff --check` is clean and every touched source remains
    below the 2,500-line hard limit.
16. **Complete.** Preserve them through `.fsimobj`, `.fsimdesign`, mapped
    `.fsimlib`, schema validation, relocation, interpreter/LLVM service
    boundaries, and native cold/warm/edit caches. The owning-unit codecs now
    retain the appended arbitrary-width enum-value metadata in matching field
    order, and the owning-unit, portable-library, runtime, class, and
    SystemVerilog constraint-HIR schemas advance together. Enum fit and
    duplicate validation no longer narrows through `uint64_t`: governed-width
    signed/unsigned extension checks and complete packed canonical keys admit
    and distinguish 137-bit enumerators. Portable-unit tests round-trip a
    137-bit enum, recursive member initializers, and a tagged union. The
    artifact-phase fixture preserves an exact sparse 137-bit driven value and
    those frontend profiles through `.fsimobj`, standalone `.fsimdesign`,
    mapped and relocated `.fsimlib`, O0/O2 interpreter/compiled service
    boundaries, cold/warm hits, and edited cache-key invalidation while the
    existing narrow process retains its native compilation contract. Class
    and constraint-HIR round trips, relocated standalone designs, and mapped
    libraries retain the 137-bit class-property width. An eight-worker build
    and twelve semantic, frontend, HIR, elaboration, LLVM, application,
    object/design/library artifact, cache, catalog, and source gates pass;
    `git diff --check` is clean and every touched source remains below the
    2,500-line hard limit.
17. **Complete.** Add cataloged width/work/storage overflow, malformed
    profile/type, unsupported operation, lossy boundary, stale schema, and
    corrupt artifact negatives with transactional rollback evidence. The
    focused matrix ties the 16,777,216-bit generated-width rejection to
    `FSIM-ELAB-GEN-012`, replication/multiplication work exhaustion to
    `FSIM-ELAB-PARAM-005`, and static-container storage exhaustion to
    `FSIM-ELAB-SVCONTAINER-020`. Oversized input-line storage now rejects
    before publishing a partial or empty replacement string; the runtime
    sentinel remains unchanged while `$ferror` retains the bounded message.
    Invalid retained `PackedAggregateKind` and semantic `TypeForm` values now
    reject in both portable-unit and design-state writers, with direct
    malformed owning-unit, class-state, and constraint-HIR tests. A 137-bit
    formatted-scan target documents the still-bounded operation through
    `FSIM-ELAB-SVFILE-012`; existing two-state X/Z conversion and Logic9/Bit2
    mixed-boundary cases retain their cataloged lossy-boundary failures.
    Owning-unit, runtime, class, and constraint-HIR stale schemas plus
    truncation/trailing corruption reject, while failed overwrites and
    checksum-mismatched publications leave existing `.fsimobj`,
    `.fsimdesign`, and `.fsimlib` metadata and payload bytes unchanged and
    remove incomplete staging trees. The eight-worker build and focused
    elaboration, application, runtime/file, artifact/library, catalog, and
    source gates pass; `git diff --check` is clean and every touched source
    remains below the 2,500-line hard limit.
18. **Complete.** Add complete interpreter/LLVM O0/O2, debugger/trace,
    scheduling, multiple-root, mixed-boundary, artifact/relocation, and cache
    positive differentials for every Batch 151 surface. LLVM file-operation
    validation now admits every positive governed `$fread` target width instead
    of retaining a stale 64-bit metadata cap. The 137-bit binary-storage fixture
    therefore runs as a fully compiled one-process module at both O0 and O2,
    preserves the exact sparse packed signal and two-element packed memory,
    and proves cold misses/stores plus warm hits; the interpreter run retains
    debugger deposit/force/release, callback, snapshot, and VCD equality.
    Existing aggregate, parameter, interface, function/task/suspension, class,
    multiple-root, mixed-language conversion, artifact/relocation, and cache
    fixtures jointly cover the remaining Batch 151 value/profile surfaces.
    The eight-worker build and fifteen LLVM, application, parameter/interface/
    callable/file/aggregate/mixed-boundary, artifact/library, catalog, and
    source gates pass; `git diff --check` is clean and every touched source
    remains below the 2,500-line hard limit.
19. **Complete.** Synchronize architecture, language support, diagnostics,
    feature rows, deferred boundaries, inventories, release matrices, UVM
    readiness, public documentation, and the restart handoff. The public
    language contract now makes Batch 151 authoritative over historical v1
    64-bit/aggregate limitation text, describes governed arbitrary-width
    constants, aggregate/callable/class lifetimes, services, artifacts, and
    the remaining operation-specific formatted-scan/runtime-selection limits,
    and keeps programs, clocking, SVA/coverage, foreign interfaces, and UVM in
    their locked later batches. Architecture, README, and mixed-language docs
    distinguish the arbitrary-width owning runtime/service paths from the
    allocation-free 64-bit native word fast path and document exact 129/137-bit
    adapters. Four executable feature rows map positive, negative,
    elaboration, runtime, artifact, and LLVM/cache evidence. The reviewed
    matrix digest is
    `97da9b1e1135bfea5bb7c2879dce2e1196e448d5ffd5bfd05daff25e65aa4021`;
    inventories advance to 1,877 production diagnostics, 560 bounded C/C++
    sources, and 650 SPDX-owned artifacts. All 26 documentation, conformance,
    release, inventory, installation, and Linux/Windows portability contracts
    pass; `git diff --check` is clean.
20. **Complete.** Run full non-sanitized exact-LLVM Debug/Release and release
    gates after eight-worker builds, then commit and push once without hosted
    CI monitoring. The exact LLVM 22.1.8 Debug build is current and its full
    regression passes 112/112 in 349.27 seconds. The exact LLVM 22.1.8 Release
    configuration rebuilds all 383 steps with eight workers and its full
    regression passes 112/112 in 307.98 seconds. Both runs include the updated
    26-contract release/inventory/portability prefix, the 137-bit native file
    differential, artifacts, API, runtime, and all application matrices. Batch
    151 is not a ten-batch monitoring boundary, so no sanitizer ran and no
    hosted CI was inspected. Changes 1-20 close in one accumulated commit and
    one push.

### Batch 152 - Unpacked data, file/memory, and procedural closure

1. **Complete.** Make a leading index prefix of a multidimensional static
   unpacked array produce a shape-preserving remaining-rank value. Constant and
   runtime prefix indices now pass the declared bounds checks before mutation,
   selected values use isolated row-major snapshots, and compatible subarrays
   cross whole assignment, partial-target assignment, and fixed-array callable
   arguments for packed/scalar leaf profiles. The exact-LLVM Debug build uses
   eight workers; core elaboration, container elaboration, and the O0/O2 cold/
   warm aggregate application differential pass 3/3. Diagnostic-catalog and
   the 2,500-line source-policy gates pass 2/2, and `git diff --check` is clean.
2. **Complete.** Close multidimensional unpacked range and indexed slices,
   shape/direction adaptation, overlapping copy semantics, and subarray return/
   copy-out paths. Slices over remaining-rank values retain every trailing
   dimension, adapt equal per-dimension counts across range identities, and use
   isolated row-major snapshots before nested target replacement. The
   application differential covers range and indexed selection, overlapping
   self-copy, runtime prefix selection, fixed-array return, task copy-out, and
   `$size`/bound/rank queries through interpreter plus O0/O2 cold/warm compiled
   execution. The eight-worker exact-LLVM Debug build succeeds; focused
   elaboration/container/application gates pass 3/3, catalog/source-policy
   gates pass 2/2, and `git diff --check` is clean.
3. **Complete.** Own named and anonymous unpacked structs and unions as
   executable recursive container members, including selection, update,
   defaults, and lifetime. Recursive container profiles now retain ordered
   aggregate members and unpacked-union identity. Dedicated aggregate read,
   write, and whole-element copy operations execute through the interpreter and
   LLVM callback boundary with validated operands and deterministic cache keys;
   copies snapshot the source before publication, and equal-profile union arms
   share their scalar storage semantics. Named, anonymous, nested, defaulted,
   selected, updated, copied, and debugger-visible values run through the
   40-signal aggregate application differential across interpreter and cold/
   warm LLVM O0/O2 engines. Missing aggregate opening braces now recover at the
   declaration semicolon instead of consuming the remaining source. The full
   eight-worker exact-LLVM Debug build succeeds; semantic HIR, frontend,
   elaboration, container elaboration, runtime, LLVM, application, diagnostic-
   catalog, and 2,500-line source-policy gates pass 9/9, and `git diff --check`
   is clean.
4. **Complete.** Nested positional, keyed, and default assignment patterns now
   materialize recursive unpacked struct/union values in static, dynamic,
   queue, and associative arrays. Recursive type/object `$bits`, corrected
   aggregate `$dimensions`, four-state whole-container equality, snapshot
   copies, associative default/write/copy/delete behavior, and equal-profile
   union aliasing execute identically through the interpreter and compiled
   callback. An explicit aggregate-value profile separates recursive member
   boxes from dynamic aggregate containers, participates in validation and
   native cache identity, and advances the runtime-state artifact schema to 11.
   The full 363-step exact-LLVM Debug build succeeds with eight workers. The
   48-signal interpreter plus cold/warm LLVM O0/O2 application differential,
   full application, container elaboration positive/negative matrix, runtime,
   LLVM, diagnostic-catalog, and 2,500-line source-policy gates pass 7/7;
   `git diff --check` is clean.
5. **Complete.** Execute string-element static/dynamic/queue/nested containers
   with exact defaults, resize, selection, copy, comparison, and callable
   behavior. Append-only string read/write and typed nested-element read/write
   SimIR operations now preserve string registers and recursively owned
   container values through interpreter and compiled callbacks, validation,
   native-cache identity, debugger views, and runtime-state artifact schema 12.
   Positional patterns materialize fixed and bounded queue strings, dynamic
   resize preserves prefixes and empty defaults, integral associative lookup
   returns the string default before insertion, and fixed-to-dynamic nested
   selection performs isolated typed read/modify/write. The application matrix
   exercises direct and callable mutation plus copy/comparison through the
   interpreter and cold/warm LLVM O0/O2 engines. The full 363-step exact-LLVM
   Debug build succeeds with eight workers; full application, the focused
   aggregate differential, container elaboration, runtime, LLVM, diagnostic
   catalog, and 2,500-line source-policy gates pass 7/7, and
   `git diff --check` is clean.
6. **Complete.** Add canonical string associative indices, traversal, ordering,
   mutation, and resource-governed identity. String-indexed associative arrays
   now retain a distinct profile and lexicographically ordered strict-UTF-8
   key store through elaboration, SimIR, interpreter and compiled callbacks,
   portable artifacts, runtime-state schema 13, native-cache identity, and
   debugger rendering. Missing reads return element defaults; insertion,
   overwrite, existence, first/last/next/previous traversal, iterator
   mutation, deletion, copy, and equality preserve deterministic key/value
   alignment for packed and string elements. Key length, UTF-8 validity,
   element count, and recursively owned storage are checked before
   publication. The application differential exercises ordered traversal,
   selection, mutation, deletion, copy/equality, debugger order, and cold/warm
   LLVM O0/O2 reuse; runtime negatives reject malformed and oversized keys.
   Crossing the 2,500-line hard limit triggered a coherent container-algorithm
   split, reducing the offending file to 1,777 lines; the new translation unit
   is 809 lines. The eight-worker exact-LLVM Debug build succeeds, full
   application plus seven focused frontend, elaboration, runtime, LLVM,
   application, diagnostic-catalog, and source-policy gates pass 8/8, and
   `git diff --check` is clean.
7. **Complete.** Carry recursive aggregate/container values across
   SystemVerilog/VHDL and standard descriptor boundaries with explicit
   ownership and shape checks. Fixed recursively packable container ports now
   bridge packed signals through one runtime alias service shared by
   interpreter and compiled callbacks. Readable and writable ownership is
   direction explicit; registration rejects invalid IDs, duplicates, sliced,
   width-mismatched, and unsupported-profile aliases before simulation. Boundary
   admission compares fixed dimension counts, aggregate member grouping, leaf
   widths, and two-/four-state domains rather than flattened width alone.
   Recursive packing preserves declaration order and exact Logic4/Logic9
   states. A live VHDL array/SystemVerilog static-container input/output fixture
   and an equal-width wrong-shape negative pass in the focused container suite.
   The full eight-worker exact-LLVM Debug build succeeds; frontend, diagnostic-
   catalog, 2,500-line source-policy, full and container elaboration, runtime,
   LLVM, aggregate application differential, and full application gates pass
   9/9. Touched files remain below 2,000 lines, and `git diff --check` is
   clean.
8. **Complete.** Close multichannel I/O plus multidimensional, string, and
   aggregate memory-file reads/writes with transactional failure behavior.
   One-argument `$fopen` now returns independently owned multichannel bits,
   while ordinary two-argument descriptors occupy a disjoint tagged range;
   combined `$fdisplay`, `$fwrite`, `$fflush`, and `$fclose` validate every
   selected channel before acting, retain stdout bit zero, and preserve
   process ownership. Fixed multidimensional memories use deterministic
   row-major linear file addresses. String memories use bounded quoted UTF-8
   tokens with deterministic escaping, and recursively packable aggregate
   elements use the same declaration-order bridge as mixed-language ports.
   Text loads stage a complete replacement and publish only after every token,
   address, range, shape, UTF-8, and storage check succeeds. Binary `$fread`
   now admits multidimensional and recursively packed aggregate elements with
   the same staged container publication. Interpreter and cold/warm LLVM O0/O2
   source evidence covers multichannel fanout/close, text round trips, and
   multidimensional/aggregate binary reads. The full eight-worker exact-LLVM
   Debug build succeeds; frontend, catalog, 2,500-line source policy, full and
   container elaboration, LLVM, runtime, and file-application gates pass 8/8,
   and `git diff --check` is clean.
9. **Complete.** Implement runtime real-valued and variable delays with checked
   timescale normalization, rounding, overflow, and scheduler admission.
   Specialized SystemVerilog delay expressions now remain in the executable
   process when they depend on runtime packed, `time`, `real`, `shortreal`, or
   `realtime` values. `WaitFor` carries an optional typed source register,
   normalized timeunit scale, and project-tick timeprecision quantum; static
   waits retain their existing boundary shape. The kernel decodes interpreter
   and native-frame payloads identically, rejects unknown, negative, nonfinite,
   malformed, and overflowing values, rounds real delays to timeprecision, and
   checks final `now + delay` admission before queuing. Zero results retain the
   inactive-region delta rule. Native object cache schema v85 keys every new
   field, and runtime-state schema 14 archives them explicitly. Focused runtime
   evidence covers real rounding, integral scaling, negative values, conversion
   overflow, and scheduler overflow. The time application now uses runtime
   `realtime` and integer delay variables and passes interpreter plus cold/warm
   LLVM O0/O2 differentials. The full eight-worker exact-LLVM Debug build
   succeeds; design-artifact, diagnostics-catalog, 2,500-line source-policy,
   full/container elaboration, LLVM, time-application, and runtime gates pass
   8/8. Modified sources remain below 2,500 lines, the primary process lowerer
   remains below 2,000 lines, and `git diff --check` is clean.
10. **Complete.** Generalize edge expressions and add runtime-selected
    force/release targets with deterministic dependency and restoration
    semantics. SystemVerilog event lists now admit edge-qualified packed
    expressions mixed with direct signals. Lowering snapshots each expression,
    waits on its deduplicated readable signal dependencies, re-evaluates after
    every wake, applies exact four-state positive/negative transition rules,
    refreshes every baseline, and re-arms when a dependency changed without
    satisfying the requested event. Runtime-selected force/release bit targets
    reuse the checked signed 32-bit `DynamicIndex` mapping; interpreter and LLVM
    calculate the same declared-range offset, retain underlying driver updates
    while forced, and reveal the current underlying value on release.
    `ForceSignalSlice` and `ReleaseSignalSlice` carry append-only optional
    selection metadata, native cache schema v86 keys it, and runtime-state
    schema 15 retains it. Focused elaboration checks prove both selection
    mappings, while the procedural-assignment application proves derived
    positive/negative edges, a mixed direct/expression event, spurious-wake
    rejection, dynamic masking, and restoration through interpreter plus
    cold/warm LLVM O0/O2. The full eight-worker exact-LLVM Debug build succeeds;
    frontend, design-artifact, diagnostics-catalog, 2,500-line source-policy,
    elaboration, LLVM, and application gates pass 7/7, and
    `git diff --check` is clean.
11. **Complete.** Preserve nonlocal and suspending references across callable
    activation, re-entry, copy-out, and failure unwinding. Automatic functions
    and tasks now admit writable nonlocal, indexed, and sliced actuals instead
    of requiring direct caller-local identifiers; suspending tasks retain their
    existing value-copy activation while copy-out remains conditional on a
    successful return. Dynamic selectors are evaluated once at activation and
    captured in distinct signed temporaries, so a selector change during
    suspension or a later sequential call cannot redirect an earlier copy-out.
    Interpreter and cold/warm LLVM O0/O2 evidence covers module-scope function
    refs, sequential suspending task re-entry, selector mutation during a
    packed-part ref activation, and exception unwinding that leaves the actual
    unchanged. The full eight-worker exact-LLVM Debug build succeeds; frontend,
    diagnostics-catalog, 2,500-line source-policy, elaboration, LLVM, callable-
    closure, and suspending-task gates pass 7/7. Touched callable and fixture
    files remain below 2,000 lines, and `git diff --check` is clean.
12. **Complete.** Complete nested and nonintegral static locals/tasks with
    specialization, initialization, debugger identity, and restart-safe
    lifetime. Static callable allocation now walks every lexical block,
    initializes each declaration once through the ordinary typed-local path,
    records storage by source declaration identity, and rebinds packed, string,
    and container registers without replaying initializers. Fixed-container
    assignment-pattern initializers use the typed pattern lowerer instead of
    the slice-only value path. Static tasks may suspend sequentially; concurrent
    re-entry remains assigned to Change 13. Interpreter and cold/warm LLVM O0/O2
    evidence covers nested packed/string/fixed-array function and task locals,
    two retained calls, two suspended task calls, complete lexical debugger
    names and values, and fresh-simulation initialization. The full
    eight-worker exact-LLVM Debug build succeeds; frontend, diagnostic-catalog,
    2,500-line source-policy, full/container elaboration, runtime, LLVM,
    callable-closure, suspending-task, mutable-string, and container-application
    gates pass 11/11. `git diff --check` is clean.
13. **Complete.** Support simultaneous fork-site re-entry and generation-safe
    process handles through create, await, kill, status, and completed-state
    queries. Fork children from repeated execution of one lexical site now
    coexist in the site's active generation, and each process owns a monotonic
    nonzero generation encoded with its dense ID in a checked 64-bit handle.
    Append-only SimIR operations implement `process::self()`, `status()`,
    `completed()`, `await()`, and `kill()` through interpreter and LLVM host
    boundaries; stale/forged handles reject, terminal completion wakes explicit
    awaiters, and recursive kill records `KILLED` without confusing a reused ID.
    SystemVerilog `process` declarations and direct built-in calls lower through
    the typed source path. Runtime and source regressions prove waiting,
    finished, killed, completed, await, generation rejection, and simultaneous
    same-site re-entry through interpreter plus cold/warm LLVM O0/O2. The full
    eight-worker exact-LLVM Debug build succeeds; frontend, diagnostic-catalog,
    2,500-line source-policy, elaboration, runtime, LLVM, and fork-application
    gates pass 7/7, and `git diff --check` is clean.
14. **Complete.** Implement typed mailboxes and counting semaphores with
    blocking/nonblocking operations, fairness, wakeup, and bounded resource
    behavior. Contextual SystemVerilog `mailbox #(T)` and `semaphore` types now
    lower to checked 64-bit runtime handles. Append-only SimIR operations cover
    construction, `num`, `put`/`try_put`, `get`/`try_get`, `peek`/`try_peek`,
    and counted semaphore `get`/`try_get`/`put`. Bounded mailboxes preserve FIFO
    readers and writers across suspension and wakeup; semaphores preserve FIFO
    waiter ordering without bypass. Direct runtime tests prove nonblocking,
    bounded-capacity, copy-out, wakeup, and fairness behavior. The source
    application proves typed operations through interpreter plus cold/warm LLVM
    O0/O2. The full eight-worker exact-LLVM Debug build succeeds; frontend,
    diagnostic-catalog, 2,500-line source-policy, elaboration, runtime, LLVM,
    and synchronization-application gates pass 7/7, and `git diff --check` is
    clean.
15. **Complete.** Close named-event and container ordering interactions,
    deterministic shuffle, waiter ordering, and cross-process visibility.
    SystemVerilog `shuffle()` now lowers as an append-only container-ordering
    operation instead of an unsupported method. Interpreter and compiled
    execution share one unbiased Fisher-Yates implementation and consume the
    same deterministic per-process random stream. Named-event wakeups retain
    stable process-ID order, and cross-process container aliases publish each
    mutation before the next waiter executes. Direct runtime evidence proves
    deterministic draw consumption and an exact preserved permutation. The
    source application combines two event waiters with push, shuffle, reverse,
    and shared observations through interpreter plus cold/warm LLVM O0/O2,
    including one-specialization native-cache reuse. The full eight-worker
    exact-LLVM Debug build succeeds; frontend, diagnostic-catalog, 2,500-line
    source-policy, elaboration, LLVM, named-event, container-application,
    ordering-application, and runtime gates pass 9/9, and `git diff --check` is
    clean.
16. **Complete.** Harden scheduler ownership, lifetime, cancellation, exception
    propagation, and failure containment for the completed procedural surfaces.
    Cancelable task handles now carry their scheduler ownership and remain live
    only while their work is pending. Unrelated and moved-from schedulers cannot
    cancel transferred work; execution, explicit cancellation, pending-work
    discard, and owner destruction invalidate handles deterministically.
    Callback exceptions invalidate the executing handle, restore scheduler run
    state, propagate exactly once, and leave later stable-order callbacks
    resumable. The full eight-worker exact-LLVM Debug build succeeds. Direct
    ownership/lifetime/failure runtime evidence plus LLVM, safe-point, named-
    event, fork/process, procedural-assignment, suspending-task,
    mailbox/semaphore, and ordering-application gates pass 11/11. Diagnostic-
    catalog and 2,500-line source-policy gates remain green, and
    `git diff --check` is clean.
17. **Complete.** Preserve the Batch 152 data/procedural state through portable
    artifacts, relocation, native caches, debugger, callbacks, trace, and
    snapshots. Runtime-state schema 16 now versions the completed procedural
    operation set, while native object-cache schema v87 deliberately separates
    the appended shuffle identity from older objects. Typed mailbox/semaphore
    and named-event/container-ordering designs survive deterministic runtime-
    state serialize/deserialize/reserialize cycles before execution. A moved
    `.fsimlib` remains mapped without source paths, and its shuffle fixture
    produces identical interpreter/LLVM results, container snapshots, debugger
    output, signal callbacks, and VCD. Explicit cache-key evidence proves
    shuffle differs from reverse/sort/rsort. The eight-worker exact-LLVM Debug
    build and full application gate succeed; library, object/design artifact,
    diagnostic-catalog, 2,500-line source-policy, LLVM, synchronization,
    ordering, and runtime gates pass 9/9, and `git diff --check` is clean.
18. **Complete.** Add cataloged malformed/type/rank/resource/lifetime negatives
    plus engine, optimization, cache, multiple-root, and restart differential
    fixtures. Shuffle now has direct malformed-arity, associative/nonintegral
    receiver, missing-random-source, forbidden-key, and corrupt portable-enum
    evidence. Its application fixture elaborates two roots, round-trips runtime
    state, rejects an invalid archive enum before publication, relocates its
    `.fsimlib`, and agrees across O0/O2 interpreter plus cold/warm LLVM cache
    execution and restarted mapped-library execution. The full eight-worker
    exact-LLVM Debug build succeeds. Diagnostic-catalog, 2,500-line source-
    policy, elaboration, ordering, runtime, LLVM, full application, fork,
    container-application, and synchronization gates pass 10/10; `git diff
    --check` is clean.
19. **Complete.** Synchronize public architecture, language support,
    diagnostics, feature evidence, inventories, release contracts, and the
    restart handoff. README, architecture, and language support now describe
    the governed Batch 152 unpacked-data/file/procedural substrate and preserve
    the later program/clocking/SVA/foreign/UVM boundaries. Executable rows
    `SV-733` through `SV-740` map positive, negative, lowering/runtime, and
    interpreter/LLVM/cache/restart evidence. The reviewed matrix digest is
    `4c9bc37c9076a6e331ab09386c6d2a9437bdf001b7f5a6857208386bcc274ba6`;
    its 1,170 rows own 4,680 evidence cells across 414 exact paths with
    evidence digest
    `c76f1dbfecd0fc5392f2f7108da2f2d5c06830cd2777952d833821241d8e1fa5`.
    Inventories advance to 1,875 production diagnostics, 570 bounded C/C++
    sources, 660 SPDX-owned artifacts, and 221 authored test/control files;
    the new unpacked-aggregate lowerer now carries its required SPDX notice.
    All 26 documentation, conformance, release, inventory, installation, and
    Linux/Windows portability contracts pass, and `git diff --check` is clean.
20. **Complete.** Run full non-sanitized exact-LLVM Debug/Release and release
    gates with at least eight workers, then commit and push once without hosted
    CI monitoring. The final exact LLVM 22.1.8 Debug tree rebuilds with eight
    workers and passes 114/114 tests in 206.72 seconds. The Release tree
    completes its 448-step eight-worker build and passes 114/114 tests in
    163.74 seconds. Release optimization exposed one guarded optional-width
    false positive; using the already validated concrete packed/scalar width
    keeps semantics unchanged and satisfies `-O3 -Werror`. The transition-delay
    negative now expects the one still-illegal negative constant while runtime
    scalar delay remains executable. The complete 26-contract release prefix,
    diagnostic catalog, 2,500-line source policy, artifacts, LLVM, runtime, and
    application matrices are included in both full runs. Batch 152 is not a
    ten-batch monitoring boundary, so no sanitizer ran and hosted CI was not
    inspected. Changes 1-20 close in one accumulated commit and one push.

### Batch 153 - Program, clocking, and interface closure

1. **Complete.** Parse explicit `program ... endprogram` declarations through
   the shared SystemVerilog unit parser while retaining a distinct append-only
   frontend, general-semantic, and SystemVerilog-HIR unit kind. Program-owned
   parameters, ports, typedefs, functions, tasks, initial/final processes,
   source spans, and matching end labels remain scoped to the program. Focused
   frontend and SystemVerilog-HIR application tests pass, and the complete
   exact-LLVM Debug tree rebuilds warning-clean with eight workers.
2. **Complete.** Admit program design units to SystemVerilog qualified and
   simple top selection, logical-library candidate resolution, same-language
   instance binding, specialization, and named-type/package-import
   preparation while keeping Verilog top requests ineligible. A parameterized
   child program retains its port widths, imported typedef, specialization
   value, hierarchy path, and stable `sv:work.program(name)` identity; both
   qualified and simple standalone program roots elaborate with the same
   identity. The full exact-LLVM Debug tree builds with eight workers, and the
   focused elaboration, SystemVerilog-HIR, diagnostic-catalog, and source-policy
   gates pass.
3. **Complete.** Insert an append-only reactive scheduler phase after updates
   and before postponed observation, carry explicit reactive ownership on
   program SimIR and DesignIR processes, and route initial, timed, event,
   sensitivity, delta, fork, and zero-delay resumptions without moving ordinary
   module processes out of active/inactive scheduling. A module NBA commits
   before a program child samples it in the reactive region. Scheduler phase
   ordering, runtime routing, program elaboration/execution, and the append-only
   C API phase value pass focused tests. The complete exact-LLVM Debug tree
   rebuilds with eight workers; runtime, elaboration, API, C-header,
   SystemVerilog-HIR, diagnostic-catalog, and source-policy gates pass.
4. **Complete.** Integrate program initial/final lifecycle behavior with the
   reactive process ownership from Change 3 while ordinary module lifecycle
   callbacks remain active. Program and module final blocks execute exactly
   once after ordinary completion in deterministic active/reactive order.
   Stable child process names, specialization hierarchy, source locations,
   scopes, and debugger execution points retain the program instance identity.
   Focused elaboration/runtime execution proves module/program initial and
   final phase order, the NBA-to-reactive boundary, final signal state, and
   hierarchical debug source identity.
5. **Complete.** Add append-only frontend clocking-block and clocking-signal
   ownership to the shared module/interface/program design-unit model. Named
   blocks retain their event, input/output/inout declarations, optional signal
   aliases, spans, and matching end labels. Focused positive coverage proves
   ownership for all three unit kinds; negatives reject missing directions,
   duplicate members and blocks, undeclared unaliased signals, and mismatched
   end labels with cataloged diagnostics. The exact-LLVM Debug tree rebuilds
   warning-clean with eight workers, and the focused frontend,
   diagnostic-catalog, and source-policy gates pass.
6. **Complete.** Extend the append-only frontend clocking model with exact
   default and per-signal input/output skews, edge qualifiers, `#1step`, and
   ordinary time-qualified delays. Procedural `##` controls retain literal or
   expression-valued cycle counts as explicit wait metadata without conflating
   cycles with project ticks. Focused positives cover all forms and negatives
   reject incomplete/repeated defaults, inout skews, and non-SystemVerilog
   cycle controls with cataloged diagnostics. The complete exact-LLVM Debug
   tree rebuilds warning-clean with eight workers, and the frontend,
   diagnostic-catalog, and source-policy gates pass.
7. **Complete.** Elaborate each clocking-block event as a stable wait alias,
   materialize input members as owned sampled storage updated by an ordinary
   active-region process, and resolve output members as aliases of their driven
   signals. Program code remains reactive, so an event updates sampled storage
   before the waiting program reads it and drives the output. Focused execution
   proves aliased input sampling, event-name suspension, active sampler
   ownership, reactive consumption, output identity, and final values. The
   exact-LLVM Debug tree rebuilds with eight workers, and all six focused gates
   pass.
8. **Complete.** Own one explicit default clocking selection per design unit
   and lower `##` controls as repeated occurrences of its event rather than
   time ticks. Input skews use transport-delayed history signals, including
   `#1step` at the unit time precision; output skews use request signals and
   delayed drivers. Edge-qualified skews select their own executable sampling
   or drive edge. The end-to-end program changes its input in the clock slot,
   observes the one-step-old value after exactly two positive edges, waits for
   the next negative output edge, and drives one nanosecond later. Cataloged
   negatives reject repeated, undeclared, malformed, or absent default clocks
   and unavailable one-step precision. The complete exact-LLVM Debug tree
   rebuilds warning-clean with eight workers, and frontend,
   SystemVerilog-HIR, elaboration, runtime, diagnostic-catalog, and source-policy
   gates pass.
9. **Complete.** Add append-only virtual-interface type ownership for
   design-unit variables and class properties, including the optional
   `interface` qualifier, named interface type, parameter actual syntax,
   modport restriction, initializer, and null value. Named-type resolution
   keeps interface design-unit types distinct from typedefs and class handles.
   Hierarchy elaboration validates declared interface and modport identities,
   allocates nullable 64-bit storage, resolves direct instance initializers
   after child interface elaboration, and assigns deterministic nonzero,
   pointer-free identities. Focused positives prove concrete and null values
   through interpreter-visible state; negatives cover missing types, missing
   modports, unknown or wrong-type instances, expression-shaped initializers,
   and duplicate declarations. The complete exact-LLVM Debug tree rebuilds
   warning-clean with eight workers, and frontend, SystemVerilog-HIR,
   elaboration, runtime, diagnostic-catalog, and source-policy gates pass.
10. **Complete.** Reuse bounded instance-array ownership and post-specialization
    expansion for SystemVerilog interface arrays. Each element retains declared
    index order and a distinct hierarchy path, signal namespace, interface
    identity, and pointer-free virtual handle. Static indexed elements bind
    through interface/modport ports and initialize virtual-interface variables;
    repeated selection of one element compares equal while a different element
    compares unequal. Out-of-range and runtime-dynamic virtual selectors reject
    through the stable interface-actual diagnostic. The full Debug build and
    six focused gates pass.
11. **Complete.** Compose the existing interface-class identity and
    `implements` resolution with classes that own virtual-interface properties.
    Generic `interface` ports forward the exact concrete interface design-unit
    and pointer-free handle identity into child virtual-interface initializers,
    including indexed array elements and modport-restricted destination types.
    The forwarded alias compares equal to its source selection through
    interpreter-visible state. The full Debug build and six focused gates pass.
- **Change 12: Complete.** Modport function/task callables retain their exact
  import/export profiles through generic and modport-restricted boundaries.
  Modports may name a retained clocking block; elaboration forwards its event
  alias and directional sampled/driven members, and the append-only clocking
  member kind propagates into SystemVerilog semantic HIR. Parameterized
  virtual-interface declarations specialize named or positional actuals with
  the ordinary unit specializer and compare the resulting canonical identity
  against concrete instances and forwarded generic interface ports. Matching
  defaults/actuals preserve the pointer-free handle while a different
  specialization rejects with a stable cataloged diagnostic. The complete
  exact-LLVM Debug tree rebuilds warning-clean with eight workers, all six
  focused gates pass, and `git diff --check` is clean.
- **Change 13: Complete.** Preserve parameterized virtual-interface identity,
  imported callable visibility, and clocking-block event/member aliases through
  two recursive modport-restricted module boundaries without widening the
  selected view. In one multiple-root elaboration, a module hierarchy's direct
  and twice-forwarded virtual handles compare equal while a program root using
  a different interface specialization owns a distinct nonzero handle. The
  complete incremental Debug tree is current, all six focused gates pass, and
  `git diff --check` is clean.
- **Change 14: Complete.** Treat a virtual-interface initializer as a hierarchy
  object reference rather than an ordinary chandle assignment during semantic
  checking, while leaving exact instance/type/modport/specialization validation
  in elaboration. The dedicated interface application matrix proves direct and
  twice-forwarded nonzero handle equality plus an executable clocking sample
  identically through the interpreter and cold/warm LLVM at O0 and O2.
- **Change 15: Complete.** Add the direct and recursive virtual handles plus the
  forwarded clocking sample to the application signal-change/VCD capture. The
  interpreter and compiled engines produce identical final values and VCD,
  while cold builds miss and warm builds hit the native cache at both
  optimization levels. The full incremental Debug tree rebuilds warning-clean,
  the dedicated differential and all six focused gates pass, and `git diff
  --check` is clean.
- **Change 16: Complete.** Serialize the virtual-interface marker, concrete
  interface name, modport, and parameter actuals in exactly the same order in
  both portable owning-unit and design-state Type archives. Validate the new
  frontend and semantic modport-member enumerators. Bump owning-unit schema 10,
  portable-library schema 7, runtime-state schema 17, semantic-state schema 2,
  DesignIR-state schema 2, and class-state schema 7 with exact assertions and
  future-schema rejection. Direct codec coverage round-trips all virtual Type
  fields; class-state coverage retains a parameterized virtual property. The
  object/design artifact phase reloads a program-owned parameterized interface,
  forwarded virtual handle, and clocking sample, then preserves them through
  relocation, interpreter/compiled execution, VCD, and warm native-cache reuse.
  The complete exact-LLVM Debug tree rebuilds warning-clean; all eleven focused
  interface/application/artifact and standard gates pass, and `git diff
  --check` is clean.
- **Change 17: Complete.** Retain the selected modport view on every interface
  hierarchy alias. A restricted actual may bind only the same restricted view;
  widening it through a generic interface port or rebinding it to another
  modport rejects immediately with stable `FSIM-ELAB-SVIFACE-011` instead of
  producing later missing-member noise. Direct portable-unit round-trip
  coverage retains the append-only clocking modport-member kind, and a corrupt
  kind value rejects before publication. The complete incremental exact-LLVM
  Debug tree is current, all eleven focused interface, artifact, application,
  catalog, and source-policy gates pass, and `git diff --check` is clean.
- **Change 18: Complete.** Publish the v2 program/reactive, clocking, and
  virtual-interface capability in language-support and architecture references.
  Add `SV-741` through `SV-750` with positive, negative, elaboration, and runtime
  evidence for program ownership, reactive scheduling, clocking HIR/execution,
  cycle controls, virtual identities, recursive forwarding, restricted views,
  interpreter/LLVM/VCD/cache parity, and portable artifacts. Remove program and
  clocking blocks from the current deferred matrix while retaining the explicit
  historical v1-scope note.
- **Change 19: Complete.** Freeze the reviewed release inventory at 1,180
  execute rows, 4,720 evidence cells, 414 exact paths, 1,905 production
  diagnostics, 570 bounded sources, and 113 runtime owners. Advance the
  SystemVerilog, legality, differential, inventory, public, and candidate audit
  baselines plus the reviewed matrix digest
  `28623705ee34643f345c98f226b513af67d217adf84618b7c5d89a6160c18cb2`.
  All nine focused catalog, source, public, inventory, installed-contract,
  SystemVerilog, differential, release-audit, and candidate gates pass.
- **Change 20: Complete.** The exact-LLVM Debug tree is current and its complete
  suite passes 114/114 in 144.53 seconds. The exact-LLVM Release tree rebuilds
  all 457 affected steps warning-clean with eight workers and its complete
  suite passes 114/114 in 116.53 seconds. Both suites include every release,
  differential, inventory, public, portability, artifact, API/ABI, interface,
  and runtime gate. No sanitizer or hosted CI monitoring ran because Batch 153
  is neither boundary. Commit and push the accumulated batch once, then begin
  Batch 154.

### Batch 154 - SystemVerilog concurrent assertion closure

- **Change 1: Complete.** Add an append-only, design-unit-owned frontend HIR
  for `sequence`, `property`, and `checker` declarations. Each declaration
  retains its exact kind, name, name/header/body/full spans, and owning copies
  of every header and body token including macro/source provenance. A dedicated
  parser translation unit preserves balanced language content without retaining
  parser storage; duplicate names, mismatched closing names, missing names,
  missing header terminators, missing declaration terminators, and use outside
  SystemVerilog-2017 reject through stable cataloged diagnostics. The complete
  307-step exact-LLVM Debug dependency rebuild succeeds warning-clean with eight
  workers; frontend, catalog, and source-policy gates pass, and `git diff
  --check` is clean.
- **Change 2: Complete.** Structure each declaration's retained header and body
  into append-only formal-argument and local-variable records. Formals retain
  value, sequence, property, or untyped kind; direction and `local` qualifiers;
  type/default tokens; names; and exact source spans. Locals retain shared type,
  declarator, initializer, name, and exact source ownership, with delimiter-aware
  comma splitting. Stable cataloged diagnostics reject malformed headers,
  formals, and local declarations plus duplicate formal names and formal/local
  collisions. The complete 307-step exact-LLVM Debug dependency rebuild succeeds
  warning-clean with eight workers; frontend, catalog, and source-policy gates
  pass, and `git diff --check` is clean.
- **Change 3: Complete.** Own optional declaration clocks and `disable iff`
  clauses independently of the raw body token stream. Clock records retain the
  exact event tokens and span; disable records retain the exact condition tokens
  and span; the remaining expression tokens and span begin after leading locals,
  clock, and disable structure. Parenthesized and named clock events are
  supported, while malformed or empty events and disable conditions reject
  through stable cataloged diagnostics. The complete 307-step exact-LLVM Debug
  dependency rebuild succeeds warning-clean with eight workers; frontend,
  catalog, and source-policy gates pass, and `git diff --check` is clean.
- **Change 4: Complete.** Resolve each retained assertion reference occurrence
  after the complete design-unit declaration region is known. Source-owned
  canonical reference records distinguish formal, local-variable, design-unit
  object, assertion-declaration, hierarchical, and package-qualified paths;
  forward assertion names therefore resolve without declaration-order leakage.
  Stable cataloged diagnostics reject unresolved unqualified names while
  hierarchical and package paths remain explicit for later hierarchy/package
  specialization. The complete 307-step exact-LLVM Debug dependency rebuild
  succeeds warning-clean with eight workers; frontend, catalog, and
  source-policy gates pass, and `git diff --check` is clean.
- **Change 5: Complete.** Structure sequence declarations into ordered elements
  and source-owned concatenation delays. Scalar and ranged `##` delays retain
  their minimum/maximum tokens and exact spans; element abbreviations retain
  consecutive `[*]`, nonconsecutive `[=]`, and goto `[->]` repetition kinds and
  optional ranges. Stable cataloged diagnostics reject missing/malformed delays,
  empty concatenation elements, and repetition without an operand. The complete
  307-step exact-LLVM Debug dependency rebuild succeeds warning-clean with eight
  workers; frontend, catalog, and source-policy gates pass, and `git diff
  --check` is clean.
- **Change 6: Complete.** Mark scalar `##0` delays explicitly as sequence fusion
  without losing their ordinary delay tokens/spans, and retain every top-level
  `intersect` operand as its own source-owned token range and exact span. Stable
  cataloged diagnostics reject empty left or right intersection operands. The
  complete 307-step exact-LLVM Debug dependency rebuild succeeds warning-clean
  with eight workers; frontend, catalog, and source-policy gates pass, and `git
  diff --check` is clean.
- **Change 7: Complete.** Retain every top-level `throughout` and `within`
  occurrence as a typed binary operation with source-owned left/right operands
  and an exact full span. Structure each `first_match(...)` occurrence into its
  sequence argument and optional match-item tokens with an exact call span,
  including nested delay expressions without treating them as top-level
  operators. Stable cataloged diagnostics reject missing binary operands and
  missing, empty, or unbalanced `first_match` arguments. The complete 307-step
  exact-LLVM Debug dependency rebuild succeeds warning-clean with eight workers;
  frontend, catalog, and source-policy gates pass, and `git diff --check` is
  clean.
- **Change 8: Complete.** Retain `.matched` and `.triggered` endpoint
  observations on sequence declarations, sequence formals, and deferred
  hierarchical/package receivers. Each endpoint owns its exact receiver token
  stream and spelling, source span, method kind, invocation actuals, and whether
  optional empty method parentheses were written. Stable cataloged parse
  diagnostics reject missing/malformed receivers and nonempty method arguments;
  semantic diagnostics reject unqualified receivers that are not sequence
  declarations or sequence formals. The complete 307-step exact-LLVM Debug
  dependency rebuild succeeds warning-clean with eight workers; frontend,
  catalog, and source-policy gates pass, and `git diff --check` is clean.
- **Change 9: Complete.** Retain top-level overlapped `|->` and
  nonoverlapped `|=>` property implications as typed records with exact
  source-owned antecedent/consequent tokens and full spans. Property expressions
  separately own scalar and ranged `##` delays using the common sequence-range
  representation, including explicit scalar-zero fusion annotation and exact
  source spans. Recognition covers the lexer's maximal-munch `|=` plus `>`
  nonoverlapped token split. Stable cataloged diagnostics reject empty
  implication operands and missing, empty, or unbalanced delay values/ranges.
  The complete 307-step exact-LLVM Debug dependency rebuild succeeds
  warning-clean with eight workers; frontend, catalog, and source-policy gates
  pass, and `git diff --check` is clean.
- **Change 10: Complete.** Retain top-level `until`, `s_until`,
  `until_with`, and `s_until_with` as typed operations with exact
  source-owned left/right tokens and full spans. Retain `nexttime` and
  `s_nexttime` as typed prefix records with optional bracketed count tokens,
  exact operand tokens, and complete source spans. Stable cataloged diagnostics
  reject empty binary operands, missing prefix operands, and empty or
  unbalanced counts. The complete 307-step exact-LLVM Debug dependency rebuild
  succeeds warning-clean with eight workers; frontend, catalog, and
  source-policy gates pass, and `git diff --check` is clean.
- **Change 11: Complete.** Retain `always`, `s_always`, `eventually`, and
  `s_eventually` as typed prefix recurrence records with optional exact
  bracketed ranges, source-owned operands, and complete spans. Retain
  `strong(...)` and `weak(...)` as typed sequence-strength wrappers with
  exact sequence tokens and call spans. Stable cataloged diagnostics reject
  missing recurrence operands, empty or unbalanced ranges, and missing, empty,
  or unbalanced strength-wrapper operands. The complete 307-step exact-LLVM
  Debug dependency rebuild succeeds warning-clean with eight workers; frontend,
  catalog, and source-policy gates pass, and `git diff --check` is clean.
- **Change 12: Complete.** Retain `accept_on`, `reject_on`,
  `sync_accept_on`, and `sync_reject_on` as typed abort records with
  independently explicit asynchronous/synchronous policy and vacuous-success/
  failure outcome. Each record owns its exact condition tokens, property
  operand tokens, and complete source span. Stable cataloged diagnostics reject
  missing, empty, or unbalanced conditions and missing property operands. The
  complete 307-step exact-LLVM Debug dependency rebuild succeeds warning-clean
  with eight workers; frontend, catalog, and source-policy gates pass, and
  `git diff --check` is clean.
- **Change 13: Complete.** Retain design-unit concurrent `assert`, `assume`,
  `cover`, and `restrict property` directives as typed source-owned records
  with optional labels, exact property tokens, and complete spans. Each record
  explicitly owns the standard Preponed sampling, Observed evaluation, and
  Reactive action-region policy for later executable lowering. Stable
  cataloged diagnostics reject missing `property`, missing/unbalanced/empty
  property parentheses, and missing terminators. The complete 307-step
  exact-LLVM Debug dependency rebuild succeeds warning-clean with eight workers;
  frontend, catalog, and source-policy gates pass, and `git diff --check` is
  clean.
- **Change 14: Complete.** Retain concurrent pass and failure actions as
  independently present, source-owned token streams with exact spans, including
  failure-only directives, balanced compound statements, and an explicit null
  pass action before `else`. Retain all ten procedural assertion-control
  system tasks as typed policy on ordinary task-call HIR with their existing
  argument ownership. Stable cataloged diagnostics reject missing/unbalanced
  actions, restrict actions, and cover failure actions without changing the
  existing malformed-directive boundary. The complete 307-step exact-LLVM
  Debug dependency rebuild succeeds warning-clean with eight workers; frontend,
  catalog, and source-policy gates pass, and `git diff --check` is clean.
- **Change 15: Complete.** Promote every concurrent directive into public
  SystemVerilog semantic HIR with a stable explicit or synthesized name,
  deterministic coverage slot, exact property/action spelling, source/origin
  provenance, region policy, and append-only callback/debugger/trace/coverage
  observer policy. Assert/assume failures select the shared assertion callback;
  cover/restrict records remain debugger-, trace-, and coverage-visible without
  manufacturing a failure callback. Focused HIR evidence covers all four kinds,
  both naming forms, action/source ownership, observer policy, and slot order.
  The complete 107-step affected exact-LLVM Debug graph rebuilds warning-clean
  with eight workers; the HIR application, frontend, catalog, and source-policy
  gates pass 4/4, and `git diff --check` is clean.
- **Change 16: Complete.** Lower the executable scalar concurrent-property
  slice into ordinary source-spanned assertion processes, including named
  property indirection, positive/negative clock edges, stable explicit or
  synthesized process names, pass/failure actions, and the shared report path.
  Engine-neutral outcome markers feed deterministic per-instance
  attempt/pass/failure coverage plus retained pass/failure/disabled trace
  events and an exact public callback. Typed procedural assertion controls now
  execute without losing their ordinary task-call HIR: on/off/kill and
  pass/failure/vacuity policies gate samples and actions, while constant
  `$assertcontrol` selectors use the same policy path. Focused evidence proves
  exact interpreter, LLVM O2, LLVM debug/O0, callback/trace, control, source,
  repeated-instance, and aliased two-root parity. The complete exact-LLVM
  Debug tree rebuilds warning-clean with eight workers; the assertion, public
  HIR, frontend, catalog, and source-policy gates pass 5/5, and `git diff
  --check` is clean.
- **Change 17: Complete.** Reject every boundary of the bounded executable
  concurrent-property slice explicitly instead of silently omitting a runtime
  process. Stable `FSIM-SV-SEM-199` rejects property actual/formal/local use,
  `FSIM-SV-SEM-200` rejects non-packed or non-scalar predicates,
  `FSIM-SV-SEM-201` rejects clocks other than one direct design-unit object
  with an optional edge, and `FSIM-SV-SEM-202` rejects the 257th executable
  concurrent assertion in one unit. Focused negatives prove one exact cataloged
  diagnostic for each boundary, and malformed empty property ownership no
  longer reaches executable diagnostics. The complete exact-LLVM Debug tree
  rebuilds warning-clean with eight workers; frontend, executable-assertion,
  catalog, and source-policy gates pass 4/4, and `git diff --check` is clean.
- **Change 18: Complete.** Run one shared capture contract across direct
  interpreter, LLVM O2, LLVM debug/O0, aliased multiple roots, compiled
  `.fsimobj` plus standalone `.fsimdesign`, cold/warm native-cache reuse, and a
  relocated design artifact. Outputs, stable process identities, normalized
  report source identity, callback events, trace events, and runtime coverage
  remain exact; portable artifact source paths intentionally compare by
  filename/line/column after relocation. The complete exact-LLVM Debug tree is
  current and warning-clean with eight workers; the assertion differential,
  library/object/design artifact, catalog, and source-policy gates pass 6/6,
  and `git diff --check` is clean.
- **Change 19: Complete.** Synchronize the public README, architecture and
  language-support contracts with the distinction between full typed
  sequence/property/checker ownership and the bounded executable scalar slice.
  Add executable evidence rows `SV-751` through `SV-760` and advance the
  machine-checked release inventory to 1,190 rows, 4,760 evidence cells, 416
  exact evidence paths (187 test, 213 production, 16 release), 113 runtime
  owners, 1,936 cataloged diagnostics, 571 bounded sources, and 661 authored
  artifacts. The reviewed matrix digest is
  `f4b0eaf84c9f0a953cf835615abb7a02e41e90fe9ee26a4516412ee141137ae4`
  and the evidence-path digest is
  `1510f52287b0e83852c0f0291487f54af96780722ec232790367db41079dbdfd`.
  The synchronized assertion/HIR/frontend/catalog/source and release-audit
  slice passes 11/11. That gate exposed and closed a selected-expression/local-
  declaration ambiguity plus stale generated-process HIR expectations.
- **Change 20: Complete.** Rebuild the complete exact-LLVM 22.1.8 Debug and
  Release trees warning-clean with eight workers. The full non-sanitized Debug
  suite passes 114/114 in 362.82 seconds and Release passes 114/114 in 304.68
  seconds. Close the accumulated Batch 154 checkpoint with one commit and push;
  this batch is intentionally neither a sanitizer nor hosted-CI monitoring
  boundary.

### Batch 155 - SystemVerilog functional coverage closure

- **Change 1: Complete.** Add append-only, design-unit- and class-owned raw
  `covergroup` declaration records with stable owner kind, name,
  header/body/full spans, owning token copies, macro/source provenance,
  matching end names, duplicate checks, and SystemVerilog-2017-only
  diagnostics. A dedicated parser translation unit handles modules,
  interfaces, programs, packages, and classes while retaining balanced headers
  and recovering before outer terminators. Stable cataloged diagnostics reject
  missing names, header semicolons, declaration terminators, or closing-label
  names as well as mismatched labels, duplicates within one owner, and
  Verilog-2005 use. The complete 307-step exact-LLVM Debug dependency rebuild
  succeeds warning-clean with eight workers; frontend, catalog, and
  source-policy gates pass 3/3, and `git diff --check` is clean.
- **Change 2: Complete.** Structure covergroup constructor formals, optional
  sampling events or `with function sample` profiles, and declaration-scope
  instance/type option assignments without retaining parser storage. Formals
  own direction, `const ref` policy, type/name/default tokens, optional owning
  name tokens, and exact spans; sampling records retain their complete event or
  procedural-profile source. Top-level `option` and `type_option`
  assignments own stable scope, name/value tokens, exact spans, and source
  order while nested coverpoint options remain excluded. Stable cataloged
  diagnostics reject malformed or duplicate formals, sampling profiles, and
  option assignments. The complete 307-step exact-LLVM Debug dependency
  rebuild succeeds warning-clean with eight workers; frontend, catalog, and
  source-policy gates pass 3/3, and `git diff --check` is clean.
- **Change 3: Complete.** Own coverpoint and cross declarations in one
  source-ordered inventory with stable explicit or deterministic synthesized
  names, owning label tokens, exact declaration indices, and complete spans.
  Coverpoints retain expression, `iff` condition, and optional body tokens;
  crosses retain ordered operand spellings/tokens/spans, `iff` conditions,
  and optional bodies. Coverage-body recognition preserves expression
  concatenations while excluding nested bin/option content from declaration
  scanning. Stable cataloged diagnostics reject missing boundaries or
  expressions, short/empty cross operand lists, malformed guards, and
  duplicate names across both kinds. The complete 307-step exact-LLVM Debug
  dependency rebuild succeeds warning-clean with eight workers; frontend,
  catalog, and source-policy gates pass 3/3, and `git diff --check` is clean.
- **Change 4: Complete.** Resolve design-unit and class-owned covergroup types,
  constructor/sample actuals, coverpoint/cross references, and lexical,
  class, and package-qualified names after complete declaration collection.
  Canonical declaration and exact-profile specialization identities feed
  deterministic per-instance runtime identities; instances retain constructor
  actuals, source-ordered initial option state, and design-unit or class-method
  sample calls. Cross operands resolve to explicit declarations or stable
  implicit coverpoints. Stable cataloged diagnostics reject unknown coverage
  references, invalid cross operands, constructor/sample profile mismatches,
  and ambiguous types. Split the public owning HIR into 1,005-line `design.hpp`
  and 1,521-line `design_core.hpp` headers after the source-policy hard limit
  exposed the accumulated growth. The complete 370-step exact-LLVM Debug
  dependency rebuild succeeds warning-clean with eight workers; frontend,
  catalog, and source-policy gates pass 3/3, and `git diff --check` is clean.
- **Change 5: Complete.** Own source-ordered scalar explicit, automatic,
  default, `illegal_bins`, and `ignore_bins` coverpoint bins independently of
  parser storage. Exact signed-decimal values retain tokens, folded values,
  spans, stable names, and declaration indices; binless coverpoints receive a
  deterministic lazy `$auto[value]` identity while Change 6 array/range forms
  remain raw and accepted. A public sampler updates per-instance hit counts,
  excludes ignored values, falls through to default bins, and emits/stores
  stable `FSIM-SV-COV-001` illegal-bin diagnostics and reports. Stable parser
  and semantic diagnostics reject malformed scalar declarations and duplicate
  names. The complete 307-step exact-LLVM Debug dependency rebuild succeeds
  warning-clean with eight workers; frontend, catalog, and source-policy gates
  pass 3/3, and `git diff --check` is clean.
- **Change 6: Complete.** Implement scalar/ranged bin sets, wildcard matching,
  unsized/typed conversion, arrayed bins including unsized arrays, and checked
  expansion into stable bin identities. Source-order records retain exact or
  inclusive-range values, four-state wildcard masks, declared widths, source
  names, optional array extents/indices, and deterministic expanded names.
  Sized arrays distribute values without empty bins; unsized arrays expand one
  bin per scalar/range value; both are capped at 65,536 bins/values. The public
  sampler matches ranges and wildcard masks through the same stable hit path.
  `FSIM-SV-PARSE-320` and `FSIM-SV-SEM-213` reject malformed/unrepresentable
  values and invalid/resource-excessive expansions. The complete 307-step
  exact-LLVM Debug dependency rebuild succeeds warning-clean with eight
  workers; frontend, catalog, and source-policy gates pass 3/3, and
  `git diff --check` is clean.
- **Change 7: Complete.** Implement transition bins with source-ordered
  sequences, consecutive/goto/nonconsecutive repetition and bounded ranges,
  scalar/ranged concatenation delays, and sized/unsized transition arrays.
  The public HIR owns exact step values, repetition/delay kinds and bounds,
  sequence spans, stable expanded identities, and per-instance progress without
  parser storage or host addresses. Sampling keeps overlapping prefixes,
  advances gaps/repetitions deterministically, records only the first completed
  bin in source order, and resets the selected bin's progress after a hit.
  `FSIM-SV-PARSE-321` and `FSIM-SV-SEM-214` reject malformed sequences and
  empty/resource-excessive transition-array expansion. The complete 307-step
  exact-LLVM Debug dependency rebuild succeeds warning-clean with eight
  workers; frontend, catalog, and source-policy gates pass 3/3, and
  `git diff --check` is clean.
- **Change 8: Complete.** Execute coverpoint and bin `iff` guards,
  default/default-sequence exclusions, ignore/illegal precedence, and
  source-ordered overlap policies through one four-state-aware sampling path.
  Public samples retain value, unknown mask, and width; exact/range matches
  require known values while wildcard masks ignore unknowns only in don't-care
  positions. Guards use deterministic true/false/unknown logic and only true
  admits a sample. Ignore bins precede illegal bins, which precede regular bins,
  with source order within each class. Scalar defaults are excluded from
  transition coverpoints; default-sequence bins require prior state and no
  active/matched transition, and automatic bins reject unknown samples.
  `FSIM-SV-PARSE-322` rejects malformed bin guards/default-sequence forms. The
  complete 307-step exact-LLVM Debug dependency rebuild succeeds warning-clean
  with eight workers; frontend, catalog, and source-policy gates pass 3/3, and
  `git diff --check` is clean.
- **Change 9: Complete.** Construct automatic cross products lazily from the
  selected stable bin identities of resolved cross operands. A public
  covergroup transaction validates all input declaration indices before
  mutation, samples requested coverpoints in source order, then creates a cross
  tuple only when every operand produced a selected identity. Tuple identities
  preserve resolved operand order; repeat transactions increment checked
  per-instance hit state, while ignored/illegal operands update deterministic
  exclusion state instead of hits. Partial inputs do not form tuples and
  duplicate/invalid inputs reject before mutation. The complete 307-step
  exact-LLVM Debug dependency rebuild succeeds warning-clean with eight
  workers; frontend, catalog, and source-policy gates pass 3/3, and
  `git diff --check` is clean.
- **Change 10: Complete.** Implement explicit regular/ignored/illegal cross
  bins with owning selection tokens, stable names/indices/spans, and source
  order. `binsof(cp.bin)` and `binsof(cp).bin` resolve against cross operands
  and named scalar/array bins; complement, `&&`/`||` composition, and scalar or
  ranged `intersect` sets execute against the transaction's selected identities
  and sampled values. Explicit cross-bin ignore/illegal precedence feeds the
  same stable tuple/exclusion state as automatic products. `FSIM-SV-PARSE-323`
  and `FSIM-SV-SEM-215`/`216` reject malformed/duplicate bins and empty,
  unknown, or ambiguous selections. The complete 307-step exact-LLVM Debug
  dependency rebuild succeeds warning-clean with eight workers; frontend,
  catalog, and source-policy gates pass 3/3, and `git diff --check` is clean.
- **Change 11: Complete.** Implement coverpoint and cross `type_option` and
  instance `option` weights, goals, and `at_least` thresholds with bounded
  integer validation and instance-over-type precedence independent of source
  order. Resolved settings propagate to explicit, expanded, and automatic bins
  and to lazy cross state. Weight-zero selections retain their stable selected
  identity but are excluded from hit state; per-bin covered state changes at
  the exact `at_least` hit. Coverpoint and cross accumulation rejects a
  further increment at `uint64_t` maximum with stable
  `FSIM-SV-COV-002`, while `FSIM-SV-SEM-217` rejects invalid weight, goal,
  and threshold bounds. The complete 307-step exact-LLVM Debug dependency
  rebuild succeeds warning-clean with eight workers; frontend, catalog, and
  source-policy gates pass 3/3, and `git diff --check` is clean.
- **Change 12: Complete.** Add a public exact basis-point percentage engine for
  coverpoints, crosses, instances, and types. Logical regular bins contribute
  their resolved weights; ignored, illegal, and weight-zero bins are excluded,
  automatic bins/crosses use realized stable identities, and `at_least`
  covered state feeds the numerator. Item, instance, and type goals normalize
  with deterministic half-up rounding; empty coverage remains explicitly empty
  at zero. Type calculation sorts instances by runtime identity and either
  averages their instance results or unions/saturating-adds stable hit state
  first when `merge_instances` is enabled, while retaining `per_instance`
  policy. `FSIM-SV-SEM-218` bounds covergroup-level percentage options. The
  complete 307-step exact-LLVM Debug dependency rebuild succeeds warning-clean
  with eight workers; frontend, catalog, and source-policy gates pass 3/3, and
  `git diff --check` is clean.
- **Change 13: Complete.** Add one public sampling scheduler for explicit,
  event-driven, and procedural `sample()` profiles. It validates the requested
  trigger before mutation and routes interpreter, LLVM O0, and LLVM O2 modes
  through the same transaction and source-ordered callback path. Stable events
  own a monotonic sequence, runtime identity, trigger/mode, optional bin
  identity, and sampled value; callbacks run in pre-sample, coverpoint hit,
  illegal-bin, cross hit, and post-sample order. A scoped active-sample guard
  rejects callback reentrancy before mutation with `FSIM-SV-COV-003`, while
  `FSIM-SV-COV-004` rejects declaration/trigger mismatches. Focused evidence
  proves identical interpreter/O0/O2 signatures, all three trigger profiles,
  illegal callbacks, and outer-transaction completion after a rejected nested
  call. The complete 82-step exact-LLVM Debug dependency build succeeds
  warning-clean with eight workers; frontend, catalog, and source-policy gates
  pass 3/3, and `git diff --check` is clean.
- **Change 14: Complete.** Expose one public structured report tree and stable
  queries for covergroup types, instances, source-ordered coverpoints/crosses,
  logical or realized bins, goals, percentages, hit/exclusion counts, illegal
  reports, and exact source spans. Type reports reuse Change 12 ordering and
  percentage policy; item/bin report paths are instance-qualified so queries
  cannot silently collide across instances. Automatic identities and explicit
  cross tuples sort deterministically within source-ordered declarations.
  A deterministic text renderer formats exact two-decimal percentages and all
  threshold/weight/count/exclusion/source fields from the same structured
  records. Focused evidence proves reversed input instance order renders
  identically, missing queries return null, and explicit-cross tuple state is
  complete. The complete 82-step exact-LLVM Debug dependency build succeeds
  warning-clean with eight workers; frontend, catalog, and source-policy gates
  pass 3/3, and `git diff --check` is clean.
- **Change 15: Complete.** Expose public debugger snapshots and trace-event
  projections with stable instance-qualified paths, canonical alias targets,
  multiple coverage roots, and deterministic ordering. Debug observations
  include percentage, goal, hit, exclusion, threshold, covered, and illegal
  state; meaningful unsigned/Boolean values are explicitly VCD-compatible.
  Trace events preserve the shared execution callback sequence and optional
  sampled/bin identities while adding exact time/delta coordinates and alias
  projections. Focused evidence proves stable ordering, canonical paths,
  multiple roots, aliases, no host addresses, VCD compatibility, and exact
  pre/hit/illegal/post callback parity. The full incremental exact-LLVM Debug
  build succeeds warning-clean with eight workers; frontend, catalog, and
  source-policy gates pass 3/3, and `git diff --check` is clean.
- **Change 16: Complete.** Add one owning `SystemVerilogCoverageState` to the
  built-project boundary with declarations/specializations, instances, option
  and hit/progress state, reports, callback events, traces, aliases, and stable
  public identities. Owning-unit schema 11 preserves complete coverage
  definitions through `.fsimobj`; standalone coverage schema 1 is a required
  checksummed `.fsimdesign` payload and survives design-directory relocation.
  Exact nonempty save/restore evidence retains bin hits, transition/previous
  progress, cross/exclusion/illegal state, rendered reports, callbacks, traces,
  aliases, and deterministic bytes. Class-owned template state survives mapped
  library relocation and remains identical across interpreter plus cold/warm
  LLVM O0/O2 builds without host addresses. The full 135-step exact-LLVM Debug
  build succeeds warning-clean with eight workers; frontend, library/object/
  design artifacts, application, catalog, and source-policy gates pass 7/7,
  and `git diff --check` is clean.
- **Change 17: Complete.** Complete the exact malformed, name-resolution,
  type, option, bin, transition, cross-selection, sampling-profile, and
  unsupported-form matrix. `FSIM-SV-SEM-219` rejects real/string/chandle/
  event/void formals outside the bounded integral model, and
  `FSIM-SV-SEM-220` rejects unsupported coverpoint or cross `with`/`matches`
  selections. Focused evidence adds exactly-one checks for ambiguous
  class-qualified covergroup types, duplicate cross bins, unknown options,
  bounded-scalar type failures, and both unsupported selection boundaries while
  retaining every prior parser/resolution/runtime diagnostic. The full
  incremental exact-LLVM Debug build succeeds warning-clean with eight workers;
  frontend, catalog, and source-policy gates pass 3/3, and `git diff --check`
  is clean.
- **Change 18: Complete.** Publish shared declaration, aggregate-bin,
  cross-product, transition-work, transaction-input, and persistent-state
  limits. Static resolution rejects exact resource overflow with
  `FSIM-SV-SEM-221`; execution preflights worst-case state growth and rejects
  with `FSIM-SV-COV-005` before pre-sample callbacks or mutation. Existing
  `FSIM-SV-COV-002` remains the exact unsigned hit-count overflow boundary.
  Focused evidence covers every static budget plus transaction and storage
  exhaustion, while normalized interpreter/LLVM O0/O2 callback, report,
  multiple-root/alias debugger, and trace signatures remain byte-identical.
  Change 16's application matrix re-proves `.fsimobj`, `.fsimdesign`,
  relocation, mapped-library, and cold/warm O0/O2 parity. The full 82-step
  exact-LLVM Debug dependency build succeeds warning-clean with eight workers;
  frontend, library/object/design artifacts, application, catalog, and
  source-policy gates pass 7/7, and `git diff --check` is clean.
- **Change 19: Complete.** Synchronize README, architecture, language support,
  diagnostics, and ten executable rows `SV-761` through `SV-770`. The
  legality, release, SystemVerilog, differential, inventory, and final
  candidate gates freeze 1,200 executable rows, 4,800 evidence cells, 426
  exact paths (188 test, 222 production, 16 release), 114 runtime owners,
  1,975 diagnostics, 590 bounded sources, 680 authored artifacts, and 222
  authored test/control files. The reviewed matrix SHA-256 is
  `07862d8c6b20770ce61076ab21072a69d87cebb7632af3f57d8a74616005c9ed`;
  the evidence-path SHA-256 is
  `42df8d80fbb0fd947299c7641d2c224c0b53cdc1e4ab0e60f6afc4a41f9f5e33`.
  The final incremental exact-LLVM Debug build succeeds warning-clean with
  eight workers; frontend, library/object/design artifacts, application,
  catalog, source-policy, legality, release, SystemVerilog, differential,
  inventory, and candidate gates pass 13/13, and `git diff --check` is clean.
- **Change 20: Complete.** Run full non-sanitized exact-LLVM Debug/Release
  builds and release gates. The first Debug regression exposed a VITAL fixture
  that relocated only the older standalone-state payload set; copying the new
  required `sv-coverage.bin` payload restores that portable-artifact contract.
  The first optimized build exposed GCC's inability to prove a conditionally
  constructed automatic-bin optional was initialized; explicit emplacement
  preserves the same value flow and is warning-clean. The final exact-LLVM
  22.1.8 Debug tree builds with eight workers and passes 114/114 in 366.52
  seconds. Release regenerates and completes all 377 steps warning-clean with
  eight workers, then passes 114/114 in 308.38 seconds. No sanitizer or hosted
  CI was run or inspected because Batch 155 is not a monitoring boundary.
  Commit and push the accumulated Batch 155 checkpoint once.

### Batch 156 - DPI-C import/export closure

- **Change 1: Complete.** Add lossless DPI declaration ownership before profile
  validation. `ParsedDesign` owns compilation-unit imports/exports while
  packages, modules, interfaces, and programs own their local declarations;
  each record retains direction, explicit owner kind and identity, every token
  through the terminating semicolon, and the combined source span. Recognition
  requires the DPI string-literal form, so ordinary package import/export
  clauses remain on their existing parser path. `FSIM-SV-PARSE-324` rejects an
  unterminated declaration. Focused evidence covers every supported owner,
  imports and exports, balanced function profiles, exact source identity, and
  coexistence with package wildcard import/re-export. The affected exact-LLVM
  Debug frontend dependency graph builds warning-clean with eight workers;
  frontend, diagnostic-catalog, and source-line-budget gates pass 3/3.
- **Change 2: Complete.** Structure and validate the DPI link string, optional
  `pure` or `context` qualifier, callable kind, SystemVerilog identifier, and
  optional C identifier alias directly from the retained tokens. Every
  structured component owns its original token, so macro/source provenance and
  exact spans survive while normalized names remain convenient for later
  resolution. Alias parsing follows the standard pre-callable
  `c_identifier = function|task` form for imports and exports.
  `FSIM-SV-SEM-222` requires `"DPI-C"`; `FSIM-SV-PARSE-325`/`326` reject a
  missing callable kind or non-keyword SystemVerilog name. The first focused
  run exposed that semantic code 217 already belonged to coverage options and
  that a missing function name could be mistaken for the `int` return keyword;
  assigning the next free catalog code and excluding keyword candidates fixes
  both precisely. The complete 72-step affected Debug graph is warning-clean
  with eight workers, and the final frontend/catalog/source gates pass 3/3.
- **Change 3: Complete.** Parse exact import profiles without re-entering the
  executable callable parser. Functions retain return-type tokens; functions
  and tasks retain the complete parenthesized formal token sequence, full
  profile tokens/span, and ordered formals with explicit/default input,
  output, inout, ref, or const-ref direction, type tokens, owning name token,
  unpacked-dimension tokens, default tokens, and full formal span. Export
  declarations remain name-only profiles for Change 4 resolution.
  `FSIM-SV-PARSE-327`/`328` reject missing, unbalanced, trailing, or incomplete
  profiles. `FSIM-SV-SEM-223` through `227` reject export qualifiers, pure
  tasks, non-input pure-function formals, nonportable C aliases, and formal
  defaults while retaining exact negative evidence. The complete 72-step
  affected Debug graph builds warning-clean with eight workers, and
  frontend/catalog/source gates pass 3/3.
- **Change 4: Complete.** Resolve imports and exports against exact
  compilation-unit, package, module, interface, or program ownership. Every
  successful declaration publishes its effective C linkage name and a
  structure-plus-resolution `validated` state; exports additionally own a
  typed native callable profile with return type, ordered formal directions,
  types/names/reference policy, callable span, and exact function/task kind.
  Compilation-unit native callable definitions are accepted only when a
  preceding same-kind DPI export introduces the name, preserving the existing
  diagnostic contract for otherwise unqualified out-of-block class methods.
  `FSIM-SV-SEM-228` through `230` reject duplicate owner-local names/linkage,
  native/import conflicts, and missing or wrong-kind exports. The first
  focused run exposed the unqualified-class-method compatibility boundary;
  narrowing compilation-unit parsing to explicit DPI exports restores it. The
  complete 72-step affected Debug graph and final 19-step correction build are
  warning-clean with eight workers; frontend/catalog/source gates pass 3/3.
- **Change 5: Complete.** Add an ABI-neutral owning DPI scalar payload with
  bounded width and standard 32-bit aval/bval planes. Checked marshalling
  preserves two-state and four-state values across arbitrary word boundaries,
  rejects X/Z rather than coercing a two-state input, and validates exact
  output width, plane counts, unused high bits, empty values, and the default
  1,048,576-bit resource limit before materialization. Focused evidence covers
  65-bit four-state X/Z round-trip, exact two-state encoding, malformed planes,
  dirty unused bits, width mismatch, and resource failure. The first build
  rejected implicit construction of the explicit empty packed-value result;
  explicit error-result construction is warning-clean. Runtime and
  source-line-budget gates pass 2/2 with eight-worker builds.
- **Change 6: Complete.** Add direction-aware shortreal, real, realtime, UTF-8
  string, and chandle payloads. Real-family transfers preserve exact binary32/
  binary64 bits including negative zero and reject kind mismatch, dirty
  shortreal high bits, input writeback, and nonfinite values. String transfers
  own exact valid UTF-8 bytes, reject embedded NUL, malformed encoding,
  direction mismatch, and the default 1,048,576-byte resource excess. Chandle
  transfers preserve stable registry identities and explicit borrowed input
  versus writable output/inout/ref state; stale, released, borrowed-writeback,
  and direction-mismatched handles reject before use. Focused evidence covers
  each success and negative path. The first test build used an obsolete
  four-field fixture initializer; matching the public three-field descriptor
  restores a warning-clean eight-worker build. Runtime/source gates pass 2/2.
- **Change 7: Complete.** Add recursive scalar, fixed-array, struct, and enum
  descriptors with canonical flattened leaf order. Layout validation checks
  nonempty exact descriptors, unique struct members and enum values, enum
  representability, array multiplication and aggregate addition overflow,
  nesting depth, 65,536-leaf and 1,048,576-bit limits, and actual 32-bit
  aval/bval payload bytes. Direction-aware composite marshalling reuses Change
  5 scalar planes for every leaf, preserves packed and unpacked values without
  exposing host object layout, and rejects leaf width/count, enum, descriptor,
  direction, unknown-value, and resource mismatch before writeback. Focused
  nested array-of-struct-with-enum evidence and negatives pass. The 13-step
  affected runtime build is warning-clean with eight workers; runtime and
  source-line-budget gates pass 2/2.
- **Change 8: Complete.** Add registry-owned open-array handles with explicit
  transfer mode, contiguity, and generation-qualified lifetime. Every
  dimension preserves declared left/right order and derives exact low, high,
  increment, and checked size; multidimensional element access maps declared
  ascending or descending indices into canonical row-major flattened values.
  Element descriptors reuse Change 7's checked recursive layout and leaf
  accounting. Creation rejects empty, overflowing, resource-excessive, or
  value-count-mismatched shapes; element lookup and writeback reject rank,
  bounds, direction, leaf-count, noncontiguous, released, and stale-handle
  misuse. Focused evidence covers mixed-direction two-dimensional ranges,
  first/last elements, writable inout updates, contiguous copies,
  noncontiguous input handles, and post-release epoch failure. The 14-step
  affected runtime build is warning-clean with eight workers; runtime and
  source-line-budget gates pass 2/2.
- **Change 9: Complete.** Add simulation-owned `svScope` identities and exact
  named-scope lookup. Generation-qualified handles include the stable
  simulation identity, slot, and epoch; registration preserves canonical full
  names and parent identities while rejecting zero simulation identities,
  malformed/duplicate names, missing parents, and cross-simulation handles.
  Current scope belongs to an explicit scheduler execution context rather than
  process-global or thread-local state. Setting a scope returns the exact prior
  handle for nested restoration, and invalid changes leave the current scope
  untouched. Focused evidence covers roots, nested/generated names, parent and
  name lookup, two independent contexts, nested set/restore, malformed trees,
  and transactional cross-simulation rejection. The 12-step affected runtime
  build is warning-clean with eight workers; runtime/source gates pass 2/2.
- **Change 10: Complete.** Expose checked standard-style open-array dimension,
  range, whole-storage, and indexed-element accessors over Change 8 handles.
  Read-only transient pointers support input and writable arguments; mutable
  pointers require output/inout/ref direction. Whole-storage access requires a
  contiguous representation while indexed element access remains available
  for noncontiguous handles. Every pointer reports its exact flattened leaf
  count, multidimensional lookup retains declared-index semantics, and direct
  writable views update registry-owned storage without a second alias. Rank,
  bounds, direction, contiguity, and released/stale handles reject before a
  pointer is returned. Focused evidence covers dimension/range queries, first
  and last pointers, mutable element publication, noncontiguous element-only
  access, input protection, and post-release failure. The six-step affected
  runtime build is warning-clean with eight workers; runtime/source gates pass
  2/2.
- **Change 11: Complete.** Add disabled-state helpers and exported callback
  dispatch with exact linkage, scope, argument-direction, and exception
  boundaries. A registry binds each callback to a validated Change 9 scope and
  ordered transfer-mode profile. Dispatch installs that scope in the explicit
  execution context, invokes against a transactional owning frame, publishes
  output/inout/ref values only on success, and restores the prior scope on all
  paths. Input arguments expose read-only access; mutable input or out-of-range
  access becomes a typed failure. Pending disabled state must be explicitly
  observed and acknowledged, otherwise publication rejects and the state
  remains pending. Standard and nonstandard C++ exceptions are contained with
  rejected outputs and bounded error state. Focused evidence covers success,
  scope restoration, input-write rejection, acknowledged and unacknowledged
  disable, exception rollback, unknown name, arity mismatch, and duplicates.
  The corrected four-step affected runtime build is warning-clean with eight
  workers; runtime/source gates pass 2/2.
- **Change 12: Complete.** Add scheduler-backed suspending imported tasks with
  generation-qualified simulation ownership, exact scope/profile binding, and
  address-stable invocation state. Each invocation owns a cancelable scheduler
  handle and a transactional argument frame retained privately across timed
  suspensions; output/inout/ref values publish only on final completion.
  Resume reinstalls the exact task scope. Nested exported callbacks may install
  their own scope and must restore the task scope before returning, after which
  the task boundary restores its original caller scope. Cancellation removes
  pending resumes and clears unpublished values. Direction/arity, disabled
  state, cross-simulation handles, scheduling failures, standard/nonstandard
  exceptions, and registry destruction are contained without escaping into or
  poisoning later scheduler work. Focused evidence covers timed suspension,
  unpublished intermediate state, resume, nested callback re-entry, scope
  restoration, cancellation, exception failure, surviving later work, and
  malformed handles/names/profiles. The corrected five-step affected runtime
  build is warning-clean with eight workers; runtime/source gates pass 2/2.
- **Change 13: Complete.** Add a versioned portable DPI plug-in manifest and
  deterministic discovery/compile/link plan. Manifests own exact source,
  include, library, import-symbol, and export-symbol order. Validation rejects
  unsupported versions, invalid names/symbols, absolute or parent-escaping
  inputs, unsupported source extensions, duplicate paths/libraries, and
  cross-profile duplicate symbols. Planning uses argv vectors, never shell
  command text; source ordinals make object names collision-free and stable.
  POSIX plans use C++20, PIC, hidden visibility, and shared linking; MSVC plans
  use explicit C++20, EH, compile, object, DLL, and output switches. Discovery
  candidates preserve caller root order and canonical platform filenames.
  Focused evidence covers repeatability, two-source POSIX plans, MSVC plans,
  relocation-safe inputs, duplicate symbols, and version rejection. The
  12-step affected runtime build is warning-clean with eight workers;
  runtime/source gates pass 2/2.
- **Change 14: Complete.** Load planned artifacts through the existing hardened
  `platform::DynamicLibrary` abstraction, which uses local eager POSIX loading
  and safe-directory Windows loading. A move-only loaded plug-in owns the
  library lifetime, normalized artifact path, and separate exact import/export
  address maps. Loading resolves the complete declared inventory before
  publication; any open or missing-symbol failure destroys the provisional
  library and returns no partial plug-in. Lookups do not cross the import/
  export boundary. A real hidden-visibility shared-library fixture explicitly
  exports one symbol in each direction. Focused evidence loads that artifact,
  resolves both exact inventories, rejects cross-map lookup, and proves a
  missing symbol rejects transactionally. The first compile exposed one
  missing standard-library include; the corrected 13-step affected build is
  warning-clean with eight workers. Runtime/source gates pass 2/2.
- **Change 15: Complete.** Add a C-compatible ABI-v1 descriptor with explicit
  export/calling-convention macros, version, fixed-prefix struct size, pointer
  width, reserved flags, and a length-delimited plug-in name. The loader
  resolves and invokes the descriptor before any declared callable symbol and
  rejects missing/null/throwing descriptors plus version, size, pointer-width,
  flags, or exact-name mismatch transactionally. SHA-256 provenance separately
  covers canonical relative manifest fields and complete artifact bytes. The
  cache key covers both digests, toolchain identity, platform, ABI version, and
  pointer width while excluding source/build/discovery roots, so relocation is
  identity-preserving and content/toolchain changes are not. Focused evidence
  validates the real fixture, ABI version and pointer-width negatives, stable
  64-hex digests, repeatability, artifact identity, and deliberate toolchain
  cache divergence. The six-step affected runtime/fixture build is
  warning-clean with eight workers; runtime/source gates pass 2/2.
- **Change 16: Complete.** Close Windows/POSIX export visibility and calling
  conventions through the shared C ABI header: Windows exports use
  `__declspec(dllexport)` and explicit `__cdecl`, POSIX exports use default
  symbol visibility, and generated MSVC compile plans explicitly select `/Gd`.
  Symbol lookup now returns a moveable lease retaining shared module ownership,
  so callable addresses remain valid after the loaded plug-in facade is
  released. Normal destruction unloads after the final facade/symbol lease.
  An explicit thread-safe quarantine path intentionally gives a module
  process-lifetime ownership when addresses may have escaped during a failed
  external registration. Focused real-library evidence invokes a leased C ABI
  function after facade destruction, observes normal unload after lease
  release, and observes residency after quarantine. The first compile required
  the lifetime fixture result to be mutable; the corrected three-step affected
  build is warning-clean with eight workers. Runtime/source gates pass 2/2.
- **Change 17: Complete.** Extend the real plug-in fixture with independently
  compiled C and C++ translation units consuming the same public C ABI header.
  Both exports resolve from one hidden-visibility module, retain leases after
  facade destruction, and execute through the explicit calling-convention
  type with distinct expected results. A second real shared library exposes
  the expected callable inventory but advertises an incompatible ABI version;
  the loader rejects it before publishing any symbol. Existing real missing-
  symbol, callback/task arity, mutable-input, descriptor size/version/pointer/
  flags/name, and manifest symbol-profile negatives complete malformed boundary
  coverage. The 21-step affected runtime plus C/C++ fixture build is
  warning-clean with eight workers; runtime/source gates pass 2/2.
- **Change 18: Complete.** Invoke the real `dpi_add` symbol through the owning
  scalar marshal/unmarshal path and the compiled O0/O2 call paths with identical
  results. Two simulation identities and independent current-scope contexts
  share the leased symbol without aliasing; a scheduler-backed imported task
  resumes through that real symbol while nested callback scope restoration
  remains covered. The ten-step affected runtime build is warning-clean with
  eight workers; runtime/source gates pass 2/2 and `git diff --check` is clean.
- **Change 19: Complete.** Publish the bounded DPI support, negative, platform,
  and engine matrices in `systemverilog-dpi.md`; synchronize README,
  architecture, language-support, feature rows `SV-771` through `SV-780`,
  deferred ownership, source inventory, release notes, and this exact handoff.
  The Change 19 documentation/runtime/frontend policy gate is recorded in the
  restart handoff.
- **Change 20: Complete.** Synchronize the ten new feature rows and fourteen
  diagnostics across every frozen legality, release, differential, inventory,
  and candidate audit: 1,210 execute rows, 4,840 evidence cells, 438 exact
  paths, 117 runtime owners, 1,989 diagnostics, 609 bounded sources, and 700
  SPDX-owned files. The full exact-LLVM 22.1.8 Debug build is warning-clean
  with eight workers and passes 114/114 in 355.83 seconds; the independent
  Release tree builds all 414 steps warning-clean with eight workers and passes
  114/114 in 322.72 seconds. `git diff --check` is clean. Commit and push the
  Batch 156 checkpoint exactly once without sanitizer or hosted-CI monitoring.

### Batch 157 - IEEE VPI closure

- **Change 1: Complete.** Define the append-only VPI host ABI foundation before
  any dynamic loading or standard API publication. The C ABI fixes explicit
  host and plug-in versions, struct sizes, pointer width, reserved flags,
  simulation ownership, 64-bit nonpointer handle identity, bounded diagnostic
  views, calling conventions, and one bind symbol. C++ construction and
  validation reject version, size, pointer-width, flag, ownership, context, and
  callback mismatches. Independent C and C++ translation units freeze the
  40-byte host and 48-byte plug-in layouts and exercise bounded reporting. The
  affected exact-LLVM Debug runtime target builds warning-clean with eight
  workers; runtime and source-line-budget gates pass 2/2.
- **Change 2: Complete.** Load one VPI image through the hardened
  `platform::DynamicLibrary` layer only after host validation, resolve the
  exact bind symbol, invoke it behind an exception boundary, and validate and
  own the complete descriptor before publication. Bind/startup status failures,
  exceptions, foreign version/size/flags, missing names/lifecycle callbacks,
  missing symbols, and open failures unload without partial state. Successful
  startup publishes a move-only owner whose explicit or automatic shutdown runs
  exactly once before unload and retains status or exception failure for repeat
  inspection. A real hidden-visibility reference image exercises every path.
  The first focused build required an explicit conversion at two test-only
  result checks; the corrected eight-worker build is warning-clean. Runtime and
  source-line-budget gates pass 2/2.
- **Change 3: Complete.** Add simulation-owned, mutex-safe error and
  object registries. Last-error records own bounded code/message storage,
  validate raw ABI severity before narrowing, remain inspectable until the next
  call boundary, and never borrow plug-in buffers. Object handles are 64-bit
  nonpointers encoding a process-unique registry, slot, and generation.
  Hierarchy records preserve exact kind, parent, simple name, live-child count,
  and sibling uniqueness. Lookup and release distinguish malformed,
  cross-simulation, released, and stale handles; slot reuse advances generation,
  and owners cannot release before live children. Focused two-simulation
  positive/negative evidence covers ownership, hierarchy, duplicates, invalid
  kinds/parents, leaf-first release, and error lifetime. The final severity path
  accepts a raw integer to avoid out-of-range enum conversion before validation.
  The affected eight-worker build is warning-clean; runtime and source gates
  pass 2/2.
- **Change 4: Complete.** Extend the authoritative object registry
  with canonical simple and full names, normalized SystemVerilog escaped-name
  termination, exact root/relative lookup, and owned optional file/line/column
  metadata. Invalid dotted simple names and partial source locations reject
  before publication. Child iteration snapshots live objects in creation order
  into a separate generation-qualified handle space; iterators scan to an
  explicit end, release independently, reject object/iterator confusion and
  cross-simulation use, and become released then stale on reuse. Full-name maps
  and child counts remain synchronized across leaf-first object release.
  Focused escaped hierarchy, multi-root, source, lookup, ordering, end, release,
  reuse, and misuse evidence passes. The first build required one explicit
  test-only iterator-result conversion; the corrected eight-worker build is
  warning-clean. Runtime and source gates pass 2/2.
- **Change 5: Complete.** Add owning typed property metadata and
  queries for roots, modules, interfaces, programs, packages, generated scopes,
  ports, nets, variables, parameters, and named events. Records preserve exact
  Verilog-2005 versus SystemVerilog-2017 ownership, scalar category, net kind,
  port direction, static/automatic lifetime, bounded width, signedness, and
  constant status. Object-kind validation rejects foreign enum values,
  SystemVerilog-only objects in Verilog ownership, malformed scalar widths,
  signed real/string/event categories, missing port directions/net kinds,
  automatic nonvariables, and mutable parameters before hierarchy publication.
  Focused positive evidence covers every allocated Change 5 kind and exact
  property reads; negative and cross-simulation queries pass. The first build
  exposed one older aggregate missing the new explicit type field; the corrected
  eight-worker build is warning-clean. Runtime and source gates pass 2/2.
- **Change 6: Complete.** Extend object/type/property queries with recursive
  semantic descriptors for scalar, packed and unpacked fixed arrays, dynamic
  arrays, queues, associative arrays, structs, unions, enums, strings, classes,
  named class properties, and nominal class handles. Descriptors expose ranges,
  member and enum identity, widths, signedness, and container bounds without
  native pointers, byte offsets, or host layout. Checked recursion bounds depth,
  nodes, fixed elements, fixed bits, names, and arithmetic; rejects malformed
  shapes, duplicate identity, invalid associative keys, nonintegral or dynamic
  packed elements, overflowing ranges, language mismatch, and object-kind mismatch.
  The registry publishes immutable owning snapshots so caller mutation cannot
  alter queried metadata. Focused nested fixed/dynamic, object-integration,
  snapshot, malformed-input, and resource-limit evidence passes. The affected
  exact-LLVM Debug target builds warning-clean with eight workers; runtime and
  source gates pass 2/2.
- **Change 7: Complete.** Add canonical simulation-owned VPI values and
  mutex-safe checked reads for scalar, raw integer bits, real/shortreal, bounded
  string, full-width time, scalar strength, two-state vectors, four-state
  aval/bval planes, and four-plane nine-state vectors. Binding validates exact
  category, width, strength ranks, and resource limits; release and generation
  reuse discard stored values. Reads preserve X/Z and all U/W/L/H/don't-care
  states, 64/65-bit boundaries, signed raw bits, embedded NUL bytes, and
  distinct zero/one strength ranks. Caller-owned buffers report required sizes
  and remain untouched on undersized requests. Unknown/lossy conversions,
  mismatched types, duplicate binding, invalid formats, and released/stale/
  malformed/cross-simulation handles reject deterministically. The final
  exact-LLVM Debug target builds warning-clean with eight workers; runtime and
  source gates pass 2/2.
- **Change 8: Complete.** Implement reverse conversion and checked writes for
  scalar, raw integer, real/shortreal, string, time, strength, two-state,
  aval/bval four-state, and four-plane nine-state formats. Conversion validates
  buffer sizes, unused padding, state encodings, exact type/width, embedded
  bytes, overflow, and strength ranks before canonical publication. Deposits
  update the underlying value beneath a separate force layer; release reveals
  the latest deposit. Constants and input ports reject distinctly. A
  scheduler-backed controller preflights delayed writes, publishes in the common
  update phase with stable order, supports transport coexistence, inertial
  supersession, explicit cancellation, retained outcomes, and independently
  releasable scheduled handles. Time overflow and released/stale generation
  targets fail without partial state. Focused conversion, force/release,
  direction, delay, cancellation, strength, overflow, and generation evidence
  passes. The exact-LLVM Debug target builds warning-clean with eight workers;
  runtime and source gates pass 2/2.
- **Change 9: Complete.** Add validated decimal time profiles retaining
  exact unit and precision exponents through femtoseconds. Integer queries expose
  64-bit common-scheduler ticks as high/low words; scaled-real queries invert the
  configured unit mapping, and both retain delta identity. Delay conversion
  recombines integer words or maps nonnegative finite scaled units to precision
  ticks with deterministic half-up rounding. Invalid profile/format, negative,
  nonfinite, and current-time overflow reject before publication. Cancelable
  after-delay work maps onto the common active phase with caller stable order,
  retained fired/cancelled/callback-failed status, exception containment,
  cross-service identity, independent release, bounded resources, and teardown
  cancellation. Focused conversion, query, scheduling, cancellation, overflow,
  delta, isolation, and teardown evidence passes. The final exact-LLVM Debug
  target builds warning-clean with eight workers; runtime and source gates pass
  2/2.
- **Change 10: Complete.** Register and dispatch value-change, after-delay,
  read-write, read-only, next-time, and synchronization-region callbacks with
  exact object, time, value, user-data, registration-order, and multi-root
  identity. A simulation-owned callback manager maps synchronization work to
  update, value-change/read-write work to reactive, read-only work to
  postponed, and after-delay/next-time work to active scheduler regions.
  Registry observers receive copied visible values only after publication and
  mutex release; unchanged deposits and deposits hidden beneath force remain
  silent, while force and release publish exact visible transitions. The
  scheduler exposes its earliest future time without consuming work, callback
  handles retain manager ownership/status, and cross-simulation object
  registration rejects transactionally. Focused multi-root, phase, stable-order,
  exact-event, observer re-entry, future-time, force/release, and isolation
  evidence passes. The exact-LLVM Debug target builds warning-clean with eight
  workers; runtime and source gates pass 2/2.
- **Change 11: Complete.** Add start/end/reset/save/restart lifecycle
  callbacks, callback removal and self-removal, nested registration, exception
  containment, and scheduler-safe re-entry with no partially published value or
  callback state. The callback manager now covers start/end simulation plus
  start/end reset, save, and restart boundaries. Lifecycle notification captures
  one registration-ordered snapshot and schedules start work in active and end
  work in postponed regions. Removal publishes retained removed status before
  canceling scheduler/time work; self-removal and removal of a later callback
  take effect during dispatch, while nested registrations begin with the next
  notification. Callback invocation occurs outside manager locks, exceptions
  become per-registration failure without aborting later callbacks, and
  re-entered lifecycle/value work observes only fully published registry state.
  Focused lifecycle-pair, self/peer removal, delayed cancellation, nested
  registration, exception, exact identity, invalid/cross-manager, and reactive
  re-entry evidence passes. The exact-LLVM Debug target builds warning-clean
  with eight workers; runtime and source gates pass 2/2.
- **Change 12: Complete.** Implement stop, finish, reset, interactive
  control, force, and release operations at common scheduler safe points, with
  deterministic callback ordering, multi-simulation containment, and resumable
  status. A simulation-owned control service preflights typed object requests
  and queues force/release in update, stop/interactive in postponed, finish
  after end-of-simulation callbacks, and reset as start-reset, transactional
  initial-value restore, reactive publication, end-reset, then resumable stop.
  Object records retain owned initial values; reset clears force layers and
  notifies only visible changes after registry unlock. Stop and interactive
  retain pending scheduler work, reset resume discards old work and rewinds
  time/delta identity, and finish is terminal. Operation handles retain
  controller ownership plus pending/applied/failed value status. Focused
  force/release ordering, stop/interactive resume, reset callback/value order,
  scheduler rewind, finish terminality, malformed/type/cross-simulation
  preflight, and cross-controller evidence passes. The exact-LLVM Debug target
  builds warning-clean with eight workers; runtime and source gates pass 2/2.
- **Change 13: Complete.** Register system tasks and functions transactionally
  and execute compiletf, sizetf, and calltf with exact callable kind, return
  width/type, invocation scope, argument order, diagnostics, and exception
  boundaries. A simulation-owned system registry validates bounded
  `$identifier` names, callable kind, mandatory compile/call callbacks, and
  task-versus-function size/return profiles before publishing an entry under a
  stable pre-move key. Execution snapshots the registration outside its lock,
  resolves a live invocation scope through the object registry, owns the
  ordered argument vector, and presents the same exact kind, name, optional
  return type, scope identity, and argument span to each phase. Functions run
  compile, size, then call and require sizetf width to equal the registered
  profile; tasks run compile then call with no return profile. Rejections retain
  bounded owned phase diagnostics, and standard, nonstandard, and diagnostic
  allocation failures remain contained at the compile, size, or call boundary.
  Focused task/function phase-order, exact-profile/scope/argument, duplicate and
  malformed transactional rejection, foreign/non-scope/missing invocation,
  size rejection/mismatch, and all-phase exception evidence passes. The
  exact-LLVM Debug target builds warning-clean with eight workers; runtime and
  source gates pass 2/2.
- **Change 14: Complete.** Implement system-call and argument handles, typed
  result publication, per-registration and per-call user data,
  nested/reentrant calls, duplicate/late registration diagnostics, and
  deterministic teardown. Registration, call, and one-based argument handles
  carry registry ownership plus monotonic identity; call records retain their
  registration, scope, immutable ordered arguments, argument handles, user
  data, typed result, phase, execution error, and active/completed/failed
  state. Callbacks receive exact registration/call/argument handles and both
  user-data domains. Result publication is legal only for an active function
  in calltf, validates the registered type, rejects wrong/duplicate/task/early
  writes, and fails accepted functions that publish nothing. All callbacks
  remain outside locks, so nested different-callable and same-registration
  reentry create independent records. Explicit sealing rejects late
  registration with owned diagnostics; duplicate publication remains
  transactional. Unregister preserves already-retained calls, explicit call
  release stales call/argument handles, cross-registry handles reject, and
  idempotent teardown clears calls before registrations and prevents later
  callback phases. Focused active/retained handle, typed result, both user-data
  domains, nested/reentrant, release/unregister/seal, cross-owner, missing
  result, and callback-initiated teardown evidence passes. The exact-LLVM Debug
  target builds warning-clean with eight workers; runtime and source gates pass
  2/2.
- **Change 15: Complete.** Implement MCD allocation/control, vlog output,
  formatted diagnostics, command-line argv, product/version identity, severity
  routing, bounded strings, file ownership, and cross-platform descriptor
  behavior. A simulation-owned I/O service uses the established portable
  32-bit layout: bit zero is standard output, bits 1-30 are monotonic MCD
  channels, and the high bit tags monotonic file descriptors independently of
  native OS descriptor width. Owner-qualified handles reject cross-service use;
  MCD combination, fan-out validation, flush, composite close, append, stale
  detection, and idempotent teardown retain exact stream ownership.
  Manifest-root-relative UTF-8 paths reject absolute/traversing names, and only
  bounded write/append modes are admitted. Vlog and four diagnostic severities
  route through injected exception-contained sinks outside service locks.
  Bounded formatting supports ordered `{}` replacements plus escaped braces
  without C varargs, with malformed/count/size errors returned explicitly.
  Configuration retains bounded argv, product identity, and the canonical
  `fsim::version` default. Focused exact MCD/FD encoding, combined routing,
  vlog, append/flush/close, cross-owner, argv/product/version, severity/format,
  path/mode/bounds, sink exception, and teardown evidence passes. The
  exact-LLVM Debug target builds warning-clean with eight workers; runtime and
  source gates pass 2/2.
- **Change 16: Complete.** Preserve registrations, callbacks, user data,
  handles, forced values, and plug-in provenance safely across supported
  save/restart and artifact flows, explicitly invalidate nonrestorable external
  state, and reject ABI/content/cache mismatches without partial restoration.
  A public checkpoint contract separates same-process restart from portable
  artifact reload. Same-process restore requires the original simulation,
  external-owner inventory, content/cache identity, and ordered plug-in
  provenance; it restores underlying and forced object values transactionally
  while retaining exact object, callback, registration, call, and descriptor
  handles plus native callback/system user data. Portable restore requires an
  empty target external surface, validates schema, host and plug-in ABI,
  content, cache, ordered plug-in path/name/content/host provenance, complete
  typed object inventory, and every value before mutation, then remaps object
  handles by canonical full name. Native callback closures, pointer-valued user
  data, system registrations, retained calls/arguments, open descriptors, and
  dynamic-library contexts receive explicit counted invalidations requiring
  verified plug-in re-registration or resource reopening. Focused evidence
  covers exact-handle restart, callback and system user data, retained
  descriptor ownership, forced values, cross-registry remapping, all
  compatibility mismatches, missing and omitted objects, explicit invalidation
  classes, and rejection without partial target changes. The exact-LLVM Debug
  target builds warning-clean with eight workers; runtime and source gates pass
  2/2.
- **Change 17: Complete.** Add independently compiled C and C++ reference
  plug-ins covering hierarchy, values, time, callbacks, control, system
  tasks/functions, I/O, user data, lifecycle, and standard calling/export
  conventions on Windows and POSIX. The frozen 40-byte v1 reporting host remains
  unchanged. A versioned 56-byte v2 wrapper retains that complete v1 prefix and
  appends an independent service context plus one C-compatible bounded request/
  result invocation callback. Operation identity covers hierarchy, values,
  time, callbacks, control, system tasks, system functions, I/O, user data, and
  lifecycle without exposing C++ objects or native pointers as handles. Strict
  validation rejects truncated v2 tables or missing service context/callback,
  while v1 images retain their existing loader path. Separate C and C++ shared
  libraries use only the public header, exact bind symbol, calling convention,
  and visibility macro; each invokes all ten operation families at startup,
  preserves request handles/arguments/user data/text, and emits one exactly-once
  shutdown lifecycle request. Focused evidence freezes v1/v2/request/result
  layouts, both image names and paths, ordered service profiles, independent
  user-data bases, lifecycle flags, idempotent shutdown, and rejection by a
  reporting-only v1 host. The exact-LLVM Debug target builds warning-clean with
  eight workers; runtime and source gates pass 2/2.
- **Change 18: Complete.** Add malformed ABI/profile, missing symbol, startup/
  shutdown, invalid/stale/released/cross-simulation handle, iterator, callback-
  removal, recursive/reentrant, exception, resource, and post-unload negative
  matrices. The accumulated runtime matrix now covers invalid v1/v2 host and
  plug-in versions/sizes/flags/pointers, missing artifacts and bind symbols,
  bind/startup/shutdown failures and exceptions, malformed service requests and
  results, and startup-time host resource failures for both independent C and
  C++ images without partial publication. Object, iterator, value, time,
  callback, control, system-call, and I/O matrices distinguish malformed,
  missing, stale, released, and cross-owner handles; callback peer/self removal,
  nested lifecycle/value registration, same-callable and cross-callable
  recursion, callback exceptions, bounded-resource rejection, teardown, and
  post-unload stale service access remain covered in the same full runtime
  executable. Focused additions reject null ownership/requests, truncated
  request/result tables, invalid operations, missing sized text, malformed host
  results, and resource failures at startup for both language images; explicit
  unload produces no later host calls and stale post-unload access rejects.
  The exact-LLVM Debug target builds warning-clean with eight workers; runtime
  and source gates pass 2/2.
- **Change 19: Complete.** Add interpreter/LLVM O0/O2, multi-root, cold/warm
  cache, standalone object/design, mapped-library, relocation, and save/restart
  differentials; update public VPI documentation, examples, diagnostics,
  feature matrices, inventories, audits, and restart handoff. Independent C and
  C++ images now produce exact matching ten-service plus shutdown transcripts
  across interpreter, compiled O0, and compiled O2 labels with distinct
  simulation identities, repeated warm loads, and relocated image paths. The
  transactional checkpoint matrix retains exact same-process owners/handles,
  remaps portable object state, rejects content/cache/image/object mismatches
  before mutation, and reports seven native-state invalidation classes. The
  existing standalone `.fsimobj`, `.fsimdesign`, mapped-library, cache,
  application engine/artifact, and multi-root API owners pass 6/6 in 25.34
  seconds alongside the VPI runtime executable. Public README, architecture,
  language support, diagnostics, the VPI guide/buildable examples, inventory,
  legality, SystemVerilog, differential, and candidate audits are synchronized
  with `SV-781` through `SV-790`: 1,220 execute rows, 4,880 evidence cells,
  461 exact paths (201 test, 244 production, 16 release), 125 runtime owners,
  1,989 diagnostics, 652 bounded sources, 744 authored artifacts, and 244
  test/control files. The reviewed matrix SHA-256 is
  `f60cf9a97f96315f5068a3047e113f70405b9cead25f9e379e5d556b22f76c51`;
  the evidence-path SHA-256 is
  `d3e9c14916fe0ec37156679632dd3b5488936f94f81d63a02ea5f164b81c51bd`.
  The exact-LLVM Debug runtime target builds warning-clean with eight workers;
  the runtime executable passes, the nine catalog/source/public/inventory/
  release-candidate gates pass 9/9 in 14.01 seconds, and `git diff --check` is
  clean.
- **Change 20: Complete.** Run the full non-sanitized exact-LLVM Debug and
  Release builds, regressions, source, catalog, inventory,
  installed-public-contract, and release gates; then commit and push once
  without sanitizer or hosted CI monitoring because Batch 157 is not a
  monitoring boundary. The first optimized build exposed GCC's inability to
  prove the discriminator of a moved delayed-write `optional<variant>` was
  initialized. Replacing that optimizer-ambiguous move with an explicitly
  initialized stored value plus presence flag preserves the transaction and
  makes both focused Debug and Release runtime targets warning-clean; both
  runtime executables pass. The full exact-LLVM Debug tree then relinks all 20
  dependent targets warning-clean and passes 114/114 in 373.55 seconds. The
  independent Release tree completes the remaining 338 full-build steps
  warning-clean and passes 114/114 in 300.77 seconds. Both suites include every
  runtime, source, catalog, inventory, installed-public-contract, artifact,
  API/ABI, portability, and release gate. `git diff --check` is clean. Close
  the accumulated Batch 157 checkpoint with one commit and push, then begin
  Batch 158; do not run a sanitizer or inspect hosted CI at this boundary.

### Batch 158 - IEEE VHPI closure

- **Change 1: Complete.** Define the append-only VHPI host and plug-in C ABI foundation:
  explicit versions and structure sizes, pointer width, reserved flags,
  simulation ownership, stable nonpointer handles, bounded diagnostics,
  calling/export conventions, and one exact bind symbol.
- **Change 2: Complete.** Load and own one VHPI image through the hardened platform
  dynamic-library boundary with transactional bind/startup publication,
  checked descriptors, exception containment, and exactly-once shutdown before
  unload.
- **Change 3: Complete.** Add simulation-owned error state plus generation-qualified
  object and iterator handle registries with deterministic invalid, stale,
  released, exhausted, and cross-simulation rejection.
- **Change 4: Complete.** Implement selected/indexed-name identity, parent/child and
  relationship iteration, hierarchy regions, escaped/full names, source
  metadata, stable ordering, and checked name lookup.
- **Change 5: Complete.** Implement scalar type, subtype, base-type, constraint, range,
  direction, resolution, and object-declaration queries with recursive resource
  limits and canonical descriptor identity.
- **Change 6: Complete.** Add checked scalar, enumeration, physical, access, and null
  value transfer with exact position/unit/designated-subtype metadata and no
  host-layout aliases.
- **Change 7: Complete.** Add constrained/unconstrained array and record type/value
  transfer, multidimensional declared-index mapping, recursive fields/elements,
  partial-buffer contracts, and transactional publication.
- **Change 8: Complete.** Add file, protected, resolved, and exact nine-state logic values
  with simulation-owned identities, access control, resolver provenance, and
  lossless scalar/vector encodings.
- **Change 9: Complete.** Implement signal drivers, sources, projected transactions,
  waveform elements, rejection/inertial/transport policy, and deterministic
  relationship queries over the common scheduler.
- **Change 10: Complete.** Implement immediate and delayed deposit, force, release, and
  transaction cancellation with checked type/ownership, force layering,
  retained operation handles, and no partial writes.
- **Change 11: Complete.** Add exact time/unit/precision/delta queries and phase callbacks
  spanning update, postponed/read-only, next-time, synchronization, save,
  restart, reset, and terminal scheduling regions.
- **Change 12: Complete.** Add signal, process, event, transaction, assertion, and
  lifecycle callbacks with copied event data, safe self/peer removal, nested
  registration/re-entry, exception containment, and deterministic teardown.
- **Change 13: Complete.** Implement foreign subprogram and foreign-model registration,
  profile validation, call contexts, argument/result handles, lifecycle,
  re-entry, unregister, and retained user data without exposing C++ ownership.
- **Change 14: Complete.** Implement generic and port association queries, actual/formal/
  mode/class metadata, disconnected/open associations, object and call user
  data, stable owner identity, and cross-root rejection.
- **Change 15: Complete.** Add assertion/report/output services plus multiple-root and
  multiple-context isolation, severity/source metadata, bounded formatting,
  sink containment, and deterministic interleaving.
- **Change 16: Complete.** Integrate same-process restart and portable artifacts with
  schema/ABI/content/cache/plug-in provenance, canonical handle remapping,
  explicit native-state invalidations, relocation, and mapped-library identity.
- **Change 17: Complete.** Add independently compiled C and C++ VHPI reference images and
  platform fixtures that exercise the complete public ABI, service families,
  startup/shutdown, repeated load, and relocated load paths.
- **Change 18: Complete.** Add malformed ABI/profile, invalid/stale/released/cross-owner
  handle, iterator, buffer, callback-removal, recursive/reentrant, exception,
  resource, rollback, and post-unload negative matrices.
- **Change 19: Complete.** Add interpreter/LLVM O0/O2, mixed-language, multi-root,
  cold/warm cache, standalone object/design, mapped-library, relocation, and
  save/restart differentials; update public VHPI documentation, examples,
  diagnostics, matrices, inventories, audits, and restart handoff.
- **Change 20: Complete.** Run full non-sanitized exact-LLVM Debug and Release
  builds, regressions, source, catalog, inventory, installed-public-contract,
  and release gates; then commit and push the accumulated implementation once
  without sanitizer or hosted CI monitoring because Batch 158 is not a
  monitoring boundary. The complete exact-LLVM 22.1.8 Debug tree rebuilds
  warning-clean with eight workers and passes 114/114 tests in 368.78 seconds.
  The independent Release tree reconfigures and builds all 91 affected steps
  warning-clean with eight workers, then passes 114/114 tests in 312.67
  seconds. Both runs include every VHPI runtime/integration owner and the full
  release, evidence, inventory, installation, API/ABI, artifact, portability,
  and application surfaces.

### Batch 159 - UVM object, factory, configuration, and reporting foundation

- **Change 1: Complete.** Add a governed external UVM-source harness for
  unmodified UVM 1.2 and UVM 2020-3.1 package and macro entry points, with
  pinned provenance, isolated work/cache roots, explicit version identity, and
  no vendored copy. Default builds remain offline. Opt-in fresh-download and
  exact local-archive modes validate official Accellera URLs, byte sizes,
  archive SHA-256 identities, all 1,286 extracted files through canonical tree
  digests, twelve exact package/macro/DPI/license/release entry points, and
  generated manifests. Authored-source work roots reject before download.
- **Change 2: Complete.** Close the preprocessor and declaration surface
  required by both UVM macro sets. Physical-line-aware comment continuations,
  guarded includes, nested argument expansion, delimiter-balanced tuple
  forwarding, token composition (including punctuation-delimiting paste
  markers), composed special strings, and multiline or inline conditional
  replacement bodies now produce parseable generated declarations. Exact
  opt-in archive evidence preprocesses the complete unmodified UVM 1.2 and UVM
  2020-3.1 packages plus representative `uvm_macros.svh`,
  `uvm_object_utils`, and `uvm_analysis_imp_decl` entry points.
  Malformed/unmatched or unterminated replacement conditionals and malformed
  special strings reject through `FSIM-SV-PP-049` through `051`. The complete
  incremental exact-LLVM Debug tree builds warning-clean with eight workers;
  frontend, diagnostic-catalog, source-policy, and UVM harness gates pass 4/4.
- **Change 3: Complete.** Close the package/class/type surface required to
  analyze both UVM packages. Parameterized and forward class identities,
  string/type/value specialization actuals, qualified constructors and method
  definitions, extern/virtual/pure methods, `const ref` formals, qualified end
  labels, scoped typedefs/enums, class localparams, package const data, static
  members, unpacked typedef dimensions, and exact multidimensional packed type
  identities now remain typed and archive-stable. Procedural type tracking
  prevents class methods such as `uvm_object::compare` from being
  misdiagnosed as built-in string/container overloads. Focused frontend,
  portable-library, and application artifact gates pass; remaining full-source
  failures begin in the procedural/event/synchronization surface assigned to
  Change 4.
- **Change 4: Complete.** Close the virtual-interface, process, event,
  semaphore/mailbox, command-line, formatted-output, procedural-loop, class
  inheritance/call, and DPI prerequisites reached by the unmodified sources.
  Both package foundations analyze cleanly without a simulator-specific
  compatibility patch. Canonical class method bodies are hydrated at most once
  per executable root instead of being copied recursively through declaration
  call graphs; capped final scans reduce unmodified UVM 1.2 and UVM 2020-3.1
  from greater than 20 GiB to 1,689,236 KiB and 1,936,024 KiB peak RSS,
  respectively.
- **Change 5: Complete.** Execute `uvm_object` construction, monotonic instance
  identity, names and type names, deep clone/copy/compare, print/record hooks,
  and descriptor-driven field automation. Generation-qualified heap handles,
  bidirectional traversal maps, stable declaration-order fields, and explicit
  reference/cycle markers preserve aliasing and terminate recursive graphs;
  checked depth/object/field/output budgets and transactional rollback bound
  malformed or excessive work. Source-level `new`, `clone`, `copy`, `compare`,
  `print`, and `record` execute through interpreter, compiled, and debug
  engines while derived overrides remain dispatchable. The complete 77-step
  exact-LLVM Debug graph builds warning-clean with eight workers; nine focused
  runtime/frontend/artifact/elaboration/application/policy gates pass. Final
  unmodified UVM 1.2 and UVM 2020-3.1 scans both exit zero under a 3-GiB
  address-space ceiling at 1,688,064 KiB and 1,935,212 KiB peak RSS.
- **Change 6: Complete.** Execute `uvm_component` construction and
  parent/child hierarchy, full-name lookup, top-level ownership, deterministic
  traversal, duplicate rejection, and destruction/lifecycle behavior across
  multiple roots. A bounded simulation-owned component service isolates root
  contexts, preserves creation order, validates names/depth/children/path
  budgets transactionally, and performs iterative leaf-first teardown with
  contained lifecycle-hook failures. Source `new(name, parent)` carries
  aligned packed/string constructor actuals through interpreter and JIT
  boundaries; canonical component parent/count calls and clone rejection
  execute across all three engines. Focused runtime/application/JIT/policy
  gates pass, and both unmodified UVM standards remain below the 3-GiB cap.
- **Change 7: Complete.** Implement type/object wrappers and the
  object/component registry macro families, including parameterized
  registrations, stable type names, create-by-type/name, and duplicate or
  mismatched registration diagnostics. A bounded simulation-owned registry
  assigns opaque monotonic wrapper handles, retains deterministic registration
  order, resolves exact specialization/declaration/name identities, validates
  kind and returned specialization on creation, and rolls failed creation back
  without leaking heap, object, or component state. Macro-generated object,
  parameterized-object, and component utility methods return the same wrappers
  through interpreter, compiled, and debug execution. Focused runtime,
  application, LLVM, catalog, source-policy, and UVM harness gates pass, and
  both unmodified UVM standards remain below the 3-GiB ceiling.
- **Change 8: Complete.** Implement factory type and instance overrides,
  wildcard instance paths, precedence and replacement rules, recursive-loop
  rejection, debug traces, and deterministic override reports. A bounded
  simulation-owned factory resolves source-ordered instance overrides before
  type overrides, recursively chains selected wrappers, supports deferred
  name-based originals and linear `*`/`?` matching, and validates all targets
  through the Change 7 registry. Resolution loops, excessive depth, duplicate
  instances, unknown targets, and override/path/report limits reject without
  partial use-count publication. Source and application APIs create the
  resolved object/component types; interpreter, compiled, and debug execution
  agree on selected wrappers and stable reports. Focused gates and both capped
  unmodified UVM scans pass.
- **Change 9: Complete.** Implement typed resource-pool insertion, lookup,
  read/write, priority, auditing, callbacks, spell checking, and safe resource
  ownership. A bounded simulation-owned pool retains nominal packed, real,
  string, and generation-checked object values behind monotonic opaque handles.
  Linear `*`/`?` scope matching, precedence, mutable high/low queue priority,
  and creation-order ties produce deterministic name/type lookups without
  retaining query indexes. Read/write revisions and counts publish only after
  validation; read-only and nominal mismatches do not mutate values. Snapshot
  callback dispatch permits removal during invocation and contains callback
  exceptions in a fixed-size audit ring. Resource/callback/lookup/audit/spelling,
  width/string/path/accessor/report budgets reject before growth, and erasing an
  object-valued resource never takes ownership of the caller's heap object.
  Public application access exercises the same pool through interpreter,
  compiled, and debug execution. The full build, six focused gates, and both
  unmodified UVM scans pass below the 3-GiB address-space ceiling.
- **Change 10: Complete.** Implement `uvm_config_db` typed
  set/get/exists/wait-modified behavior, hierarchical precedence, wildcard and
  regular-expression scope matching, build-time versus runtime precedence, and
  callback wakeup ordering. A bounded simulation-owned config layer reuses
  Change 9 resources by exact context/pattern/field/type key. Build settings
  subtract checked context depth from default precedence; runtime settings
  restore default precedence, and same-precedence updates are last-wins without
  growing entries. Glob matching remains linear; `/.../` expressions compile
  to a deliberately restricted recursion-free atom stream and execute through
  a capped dynamic-programming matrix, rejecting unsupported backtracking
  constructs and excess work before publication. One-shot waiters prevalidate
  wake fanout, detach in registration order after value publication, and
  contain callback exceptions. Application and runtime evidence covers typed
  lookup/exists, hierarchy, glob/regex fields and instances, resource reuse,
  build/runtime ordering, spelling, callback ordering/failure/cancellation,
  nominal mismatch, and entry/waiter/wake ceilings. The full build, six focused
  gates, and both capped unmodified UVM scans pass.
- **Change 11: Complete.** Integrate command-line plusargs for factory,
  configuration, resource, verbosity, and timeout settings with exact parsing,
  precedence, repeated-option ordering, and cataloged malformed-option
  diagnostics. A bounded simulation-owned service retains all plusargs and
  ordered unknown options while parsing the UVM 1.2/2020 factory aliases,
  integer/bitstream/string config settings, resource/config trace switches,
  global and component verbosity, and timeout settings. Parsing, total bytes,
  values, fields, patterns, settings, fixed 4096-bit numeric conversion, and
  resource/config publication are explicitly capped before mutation. Instance
  overrides apply before source-ordered type overrides; integer settings apply
  before bitstreams and strings; repeated config values remain last-wins; and
  the first global verbosity/timeout wins while all occurrences remain counted.
  Run, simulate, and debug own isolated state, malformed or inapplicable
  settings emit `FSIM-UVM-CLI-001`, and `+UVM_TIMEOUT` remains framework state
  rather than replacing fsim's independent `--duration`. Runtime, application,
  LLVM, catalog, source-policy, UVM harness, and both capped unmodified-UVM
  scans pass.
- **Change 12: Complete.** Define and prove multi-root/multiple-context UVM
  singleton, factory, resource, configuration, callback, and command-line
  isolation or intentional sharing, including restart and teardown without
  state leakage. A public compile-time ownership contract makes every registry,
  factory, resource, configuration, callback, and plusarg service unique to one
  independently constructed `Simulation`, while equal component paths remain
  partitioned by explicit root handles. Application evidence creates equal
  `api_top` paths in two roots and proves that factory resolutions, resource
  callbacks/audits, config values/waiters, and command-line settings are
  deliberately shared within that simulation. A concurrently alive peer
  simulation reuses the same root identity and registered type names while
  publishing divergent factory, resource, config, callback, and plusarg state;
  neither context observes the other's mutations. Destroying that peer and
  constructing a third simulation restores empty mutable services and root
  handle 1 without changing the original context. The full warning-clean build,
  six focused gates, graph ownership review, and both capped unmodified-UVM
  scans pass.
- **Change 13: Complete.** Execute report-message construction and
  `uvm_report_object` severity/ID/verbosity routing, context/file/line metadata,
  element containers, and deterministic message composition. A bounded,
  simulation-owned `SystemVerilogUvmReportService` constructs immutable routed
  snapshots with monotonic sequence identity, generation-safe report-object
  handles and full names, exact severity/ID/message/verbosity/source/context
  fields, UVM_MEDIUM/UVM_NONE defaults, INFO threshold filtering, and
  prechecked-report behavior; warning/error/fatal routes do not repeat the
  INFO-only gate. Its insertion-ordered element container supports packed
  integer, escaped string, and object references with per-element log/display/
  record actions, copy/erase/clear accounting, deterministic canonical payload
  composition, stale-handle rejection, exception-contained route hooks, and
  explicit count/name/value/width/storage/field/composed-payload limits.
  Command-line global verbosity initializes the report service. Multi-root and
  concurrent/restarted-simulation application evidence proves equal object
  names retain distinct handles while report thresholds, sequences, counters,
  and hooks remain simulation-local. The 75-step warning-clean build, six
  focused gates, fresh graph bounds review, and both 3-GiB-capped unmodified-UVM
  scans pass.
- **Change 14: Complete.** Implement report-handler severity/ID actions,
  verbosity, files, hooks, overrides, default-file behavior, and hierarchical
  component policy. Each simulation owns bounded per-report-object handler
  state with the governed `(severity,ID)` then ID then severity/default
  precedence for actions and files, `(severity,ID)` then ID then maximum
  precedence for verbosity, standard INFO/WARNING/ERROR/FATAL default actions,
  nonzero file fallback, and ID-specific severity override precedence before
  resolved-severity action/file selection. Generic then severity-specific hooks
  run for `CALL_HOOK`, both execute even after rejection, and exceptions are
  contained and counted. The complete UVM component `_hier` setter family uses
  deterministic nonrecursive subtree traversal and atomically restores every
  descendant if handler or aggregate-setting caps reject the update. Equal-name
  components in a second root and sibling subtrees remain unchanged. Actual
  display/log/file execution and server accounting remain Change 15. The
  76-step warning-clean build, six focused gates, fresh graph/bounds review,
  and both 3-GiB-capped unmodified-UVM scans pass.
- **Change 15: Complete.** Implement report-server formatting, severity counts,
  ID counts, summaries, quit counts, max-quit behavior, file/display/log
  actions, and deterministic newline and numeric formatting. The bounded,
  simulation-owned report server composes the governed severity, optional
  verbosity, source, timestamp, object/context, ID, payload, and optional
  terminator fields; executes RECORD, DISPLAY, LOG, COUNT, EXIT, and STOP in
  UVM order; masks the standard-output bit from MCD log files; contains sink
  failures; and exposes deterministic severity/ID summaries. Severity, ID, and
  quit accounting reject overflow transactionally. Max-quit overridability,
  record-all, ID-summary, show-verbosity, and terminator controls are public and
  simulation-local. Packed element formatting now covers exact binary, octal,
  signed/unsigned decimal, hexadecimal, and four-state grouped digits under
  fixed width and output budgets. Runtime and three-engine application evidence
  proves exact text/newlines, file/display/record sinks, max-quit EXIT and STOP,
  count reset/restore, stdout de-duplication, bounds, and restarted-context
  isolation. The warning-clean full build, six focused gates, fresh graph/bounds
  review, and both 3-GiB-capped unmodified-UVM scans pass.
- **Change 16: Complete.** Implement report catchers and callback ordering,
  throw/catch/demote/modify behavior, re-entry and exception containment,
  recursion/resource bounds, removal during dispatch, and post-catcher
  accounting. Each simulation owns one bounded ordered catcher registry with
  global or exact report-object association, append/prepend ordering, stable
  opaque handles, callback enablement, and deterministic removal. Catchers
  observe prior retained severity, ID, message, verbosity, context, action, and
  element mutations through a constrained mutable context. THROW continues,
  CAUGHT suppresses later callbacks and server execution, and severity changes
  remap an unchanged default action exactly once unless the current catcher set
  an explicit action. Callback exceptions or invalid mutations restore that
  catcher's input message and continue. Dispatch snapshots exclude additions,
  honor disable/removal before each callback, and skip recursive catcher
  invocation for reports emitted during a catcher while retaining unique route
  sequence order. Caught/demoted/failure/re-entry statistics are simulation-
  local and deterministic; the report server accounts only the final thrown
  severity and ID. Runtime and three-engine application evidence covers the
  complete order, mutation, rollback, catch, re-entry, removal, isolation, and
  resource-bound matrix. The 75-step warning-clean build, six focused gates,
  fresh graph/bounds review, and both 3-GiB-capped unmodified-UVM scans pass.
- **Change 17: Complete.** Unmodified UVM 1.2 and UVM 2020-3.1 now run the
  same object/registry/factory/config/report example through direct source and
  portable object/design artifacts without source edits. Both libraries pass
  two aliased roots through interpreter, LLVM O0/O2, debug execution,
  simulation callbacks, VCD/FST, and isolated cold/warm native caches with one
  deterministic transcript and exact 7/11/pass signal transitions. Repeated
  class hydration is bounded per executable root instead of recursively copying
  the reachable UVM call graph into every call site. Portable reload no longer
  replays already-attached extern method bodies, repeated class expression
  resolution preserves process/chandle/null nominal types, ordinary suspending
  class tasks retain task-frame lowering, and legal self-qualified package
  types/constants/functions no longer form false visibility cycles. Governed
  analysis stays below 2 GiB; full executable compiles and two-root O0/O2
  elaborations stay below the explicit 4-GiB UVM 1.2 and 5-GiB UVM 2020 caps.
  The warning-clean build and focused frontend/elaboration/LLVM/application/
  runtime/catalog/source/UVM gates pass.
- **Change 18: Complete.** The aggregate UVM negative matrix rejects malformed
  names/profiles/values, nominal type mismatches, stale and cross-owner handles,
  override loops and limits, invalid wildcard/regex-like patterns, callback
  mutation/exception/re-entry failures, and every object/component/registry/
  factory/resource/config/report ceiling without partial state publication.
  Actual UVM 1.2 and UVM 2020-3.1 portable object/design artifacts reload and
  execute after independent design relocation with identical two-root output.
  Application/library artifacts also cover mapped-library relocation,
  same-process simulation restart and service isolation, while object/design/
  class/HIR schema tests reject future, truncated, trailing, and incompatible
  state before execution. The five artifact/application/runtime aggregate gates
  pass.
- **Change 19: Complete.** Public UVM/version documentation, the unmodified
  object/factory/config/report example, diagnostics, feature evidence,
  governed provenance, inventories, release audits, and measured resource
  baselines are synchronized. The reviewed release contract contains 1,240
  executable rows and 4,960 evidence cells across 518 exact paths; all 27
  documentation, conformance, inventory, installation, and portability gates
  pass.
- **Change 20: Complete.** The final exact-LLVM 22.1.8 Debug build is warning-
  clean and passes 115/115 tests in 414.95 seconds. The fresh 494-step Release
  build is warning-clean and passes 115/115 in 336.28 seconds. The first Debug
  run exposed a non-class indexed aggregate selection left in the new neutral
  `@sv-select` form; class resolution now folds non-class receivers back to
  their executable aggregate representation while retaining class-valued
  selection, with focused and full regression evidence. No sanitizer or hosted
  CI monitoring ran because Batch 159 is not a monitoring boundary.

### Batch 160 - UVM phases, objections, and TLM - CI monitoring boundary

- **Change 1 - Complete.** Define the simulation-owned UVM phase foundation
  before any
  scheduler behavior: opaque generation-checked phase/domain handles, exact
  phase kind and state, parent/predecessor/successor relationships, per-root
  participation, source-order custom registration, and bounded acyclic graph
  construction. Freeze standard common/runtime phase identities, ownership,
  traversal, mutation, duplicate, cycle, depth, node, edge, and root limits;
  add focused service tests and cataloged invalid-graph diagnostics. The new
  `SystemVerilogUvmPhaseService` is owned directly by each `Simulation`, binds
  domain participation to that simulation's UVM component roots, preserves
  exact standard identities and source registration order, and rejects stale
  or foreign handles, duplicate/reserved identities, invalid domain use,
  cycles, missing roots, and every frozen resource ceiling transactionally
  through `FSIM-UVM-PHASE-001` through `005`. Focused runtime proof covers the
  full positive graph and invalid-graph matrix, while application proof covers
  public ownership, independent live simulations, and fresh restart state. An
  eight-worker exact-LLVM Debug full build completes 147 steps; `fsim.runtime`,
  `fsim.application`, `fsim.llvm`, `fsim.diagnostics-catalog`,
  `fsim.source-line-budget`, and `fsim.uvm-source-harness` pass 6/6, followed
  by a focused ownership rerun of `fsim.application` in 28.43 seconds.
- **Change 2 - Complete.** Construct the standard common domain and runtime schedule with
  build, connect, end-of-elaboration, start-of-simulation, pre-reset through
  post-shutdown, extract, check, report, and final nodes. Implement insertion,
  predecessor/successor placement, shared and independent domains,
  synchronization edges, custom phase registration, deterministic graph
  linearization, and transactional rejection without exposing partial graphs.
  The simulation-owned service now builds the exact nine-node common chain and
  twelve-node runtime chain, places the runtime domain with common `run`, and
  admits both API-created and automatic component roots to both domains.
  Custom function/task phases support source-ordered tail, before, after,
  between, and parallel-with insertion with transactional adjacency rewiring.
  Domains remain independent until explicitly placed; whole-domain
  synchronization pairs matching identities in source order, while explicit
  synchronization supports differently named phases and both forms remove
  atomically. Deterministic topological order covers every standard and custom
  node. Mixed, cross-domain, reverse-anchor, duplicate, self, placement-cycle,
  synchronization-shape, node, edge, root, and mutation-limit failures reject
  without publishing partial graph state. Standard-root admission reserves the
  complete two-domain rollback budget, and failed automatic component
  construction independently cleans phase participation and component-root
  ownership. The eight-worker exact-LLVM Debug build completes 75 affected
  steps warning-clean; `fsim.runtime`, `fsim.application`, `fsim.llvm`,
  `fsim.diagnostics-catalog`, `fsim.source-line-budget`, and
  `fsim.uvm-source-harness` pass 6/6 in 29.56 seconds.
- **Change 3 - Complete.** Execute the function-phase subset over the component hierarchy
  with UVM-correct top-down or bottom-up traversal, per-component callbacks,
  phase-ready-to-end and phase-ended hooks, state transitions, exception
  containment, deterministic multiple-root ordering, and hierarchy-mutation
  rules. Prove build/connect/end-of-elaboration/start-of-simulation/extract/
  check/report/final ordering through interpreter, compiled, and debug engines.
  The simulation-owned executor now advances dormant function phases through
  scheduled, started, executing, ready-to-end, ended, cleanup, and done states.
  Build walks roots, tops, siblings, and dynamically created children top-down;
  every later function phase walks the frozen hierarchy bottom-up, preserving
  domain participation and component creation order. Per-component started,
  execute, ready-to-end, and ended callbacks contain exceptions as bounded
  `FSIM-UVM-PHASE-006` records and continue remaining components and hooks.
  Only the active build callback may add a direct child; root creation,
  teardown, non-build growth, nested guards, task-phase use, and re-execution
  reject without corrupting hierarchy or phase state. The application bridge
  resolves virtual source callbacks by dynamic specialization and executes
  build/connect/end-of-elaboration/start-of-simulation/extract/check/report/
  final methods in interpreter, compiled, and debug engines. Runtime proof
  covers live build growth, multi-root ordering, all hooks and states,
  exception containment, forbidden mutations, and every standard function
  phase. The source-method implementation was extracted into a focused
  592-line fragment, returning `application_simulation.cpp` to 1,934 lines.
  The eight-worker exact-LLVM Debug tree rebuilt all 77 initially affected
  steps and the final extraction rebuilt 15 steps warning-clean. `fsim.runtime`,
  `fsim.application`, `fsim.llvm`, `fsim.diagnostics-catalog`,
  `fsim.source-line-budget`, and `fsim.uvm-source-harness` pass 6/6 in 32.33
  seconds; `git diff --check` is clean.
- **Change 4 - Complete.** Execute task phases and domain control, including run and the
  pre/reset/post/configure/main/shutdown families, concurrent sibling domains,
  custom task phases, legal jump/forward/backward restart semantics, graph
  synchronization, phase completion, and phase-local process ownership. Close
  the source-level UVM method dispatch needed by unmodified phase callbacks and
  retain capped UVM 1.2/UVM 2020 package and executable memory baselines.
  Every standard and custom task phase now launches one generation-checked,
  phase/root/component-owned process per participating component in
  deterministic top-down order. Immediate tasks complete synchronously;
  suspended tasks leave the phase executing until each owned process completes
  or is cancelled, after which ready-to-end and ended hooks run and the phase
  reaches done. Callback and hook exceptions remain contained as
  `FSIM-UVM-PHASE-006`; invalid process, completion, synchronized-group, and
  jump state rejects as `FSIM-UVM-PHASE-007`, while process exhaustion rejects
  before publication through the existing resource diagnostic. Explicitly
  synchronized custom phases in sibling domains launch concurrently and retain
  independent root/process completion. Forward jumps cancel source work, mark
  intervening phases skipped, and leave the target executable; backward jumps
  cancel active work and reset the exact target-through-source interval.
  Runtime evidence covers run, all twelve runtime phase families, custom task
  phases, synchronization, suspension/completion/cancellation, ownership,
  hooks, hierarchy guards, limits, and forward/backward restart. The
  application bridge dispatches zero- or one-phase-argument source tasks and
  proves every task method and callback through interpreter, compiled, and
  debug engines. The main simulation source remains below target at 1,900
  lines after extracting a 111-line UVM phase fragment. The eight-worker
  exact-LLVM Debug tree rebuilds 75 affected steps warning-clean; the runtime,
  application, LLVM, catalog, source-policy, and UVM harness gates pass 6/6 in
  33.62 seconds. Final unmodified UVM 1.2 and UVM 2020-3.1 analyses exit zero
  under 3 GiB at 1,856,684 KiB and 2,126,780 KiB peak RSS. Their retained
  two-root compiled designs emit identical PASS transcripts under 4/5-GiB
  ceilings at 859,788 KiB and 956,912 KiB peak RSS. `git diff --check` is clean.
- **Change 5 - Complete.** Implement simulation-owned objections with source-object and
  description identity, local and propagated counts, raise/drop/set behavior,
  root and phase association, ordered raised/dropped callbacks, trace records,
  count queries, and deterministic all-dropped detection. Reject stale,
  cross-simulation, negative, overflowing, over-depth, and excessive source/
  description/count state before mutating the phase or hierarchy.
  A dedicated objection service now binds live UVM objects into owner-qualified
  source handles and keys every active count by exact phase, root, source, and
  description identity. Component sources propagate through their exact
  leaf-to-root ancestry; noncomponent sources contribute directly to their
  associated root. Raise, drop, and set preflight local, source-total,
  component-total, and root-total counts before publication, with set emitting
  only its positive or negative difference. Raised/dropped callbacks and
  retained trace records follow deterministic source-to-root order; individual
  callback exceptions are contained as `FSIM-UVM-OBJ-004`, and the final root
  zero transition records all-dropped detection without taking Change 6's drain
  or all-dropped-callback behavior early. `FSIM-UVM-OBJ-001` through `004`
  distinguish invalid ownership/lifetime, operation/association, bounded-state,
  and callback failures. Runtime proof covers component and noncomponent
  sources, exact description counts, all aggregate queries, set differences,
  callback order and containment, monotonic traces, all-dropped transitions,
  stale and cross-simulation handles, wrong roots/phases, negative and underflow
  counts, overflow, depth, source, description, entry, description-byte,
  callback-fanout, trace, and mutation ceilings, with each rejection preserving
  prior state. The new API, implementation, and focused test remain compact at
  236, 443, and 442 lines. The exact-LLVM Debug tree rebuilt 140 affected steps
  warning-clean, followed by a two-step final runtime-test rebuild; the runtime,
  application, LLVM, catalog, source-policy, and UVM harness gates pass 6/6 in
  32.14 seconds before the final documentation refresh. `git diff --check` is
  clean.
- **Change 6 - Complete.** Implement per-object/per-phase drain time, cancellation and
  restart of pending drains, all-dropped callback sequencing, re-raise during
  drain or callback, phase-ready-to-end re-entry, and stable simulated-time
  scheduling. Bound pending drains, callback fanout, re-entry, aggregate delay,
  and teardown; prove zero/nonzero and simultaneous drains across multiple
  roots without wall-clock dependence.
  The objection service now optionally binds the simulation scheduler and owns
  exact source/phase drain settings plus cancelable, generation-qualified
  pending drain work. Local and propagated counts still publish transactionally
  at drop time; source drains gate the root's all-dropped and ready-to-end
  notifications until every pending source in that phase/root completes.
  Re-raise cancels pending work, a later final drop restarts the full interval,
  and service destruction cancels all bounded scheduler handles before their
  callbacks can observe dead state. Zero drains finalize synchronously;
  nonzero drains use scheduler ticks and deterministic reactive-phase stable
  order. All-dropped runs before ready-to-end, and re-raise from either callback
  forces a later stable all-dropped/ready cycle. Callback exceptions remain
  contained as `FSIM-UVM-OBJ-004`; pending-setting/count, individual and
  aggregate delay, callback fanout/re-entry, future trace reservation, and
  mutation ceilings reject before count publication. Runtime proof covers
  exact source/phase setting identity, two simultaneous sources in one root,
  an independent second root, intermediate simulated-time limits, cancellation
  and restart, zero-drain all-dropped and ready re-entry, callback containment,
  missing-scheduler rejection, every new bound, and teardown cancellation. The
  objection API, implementation, and combined focused test remain below policy
  at 336, 750, and 738 lines. The final eight-worker exact-LLVM Debug build
  relinked 20 affected targets warning-clean after the 29-step runtime rebuild;
  the runtime, application, LLVM, catalog, source-policy, and UVM harness gates
  pass 6/6 in 33.67 seconds before the documentation refresh. `git diff
  --check` is clean.
- **Change 7 - Complete.** Tie task-phase completion to objection state and scheduler-
  owned process lifetime. Track phase-created process trees, kill or await them
  at legal boundaries, contain exceptions, cancel waits and drains, prevent
  post-phase callbacks, and reclaim every process on jump, timeout, root
  teardown, failed construction, or simulation destruction without disturbing
  caller-owned or another domain's processes.
  Phase and objection services now share an explicit simulation-local
  quiescence boundary. Task-phase completion rejects while any participating
  root retains an objection or pending drain, then captures immutable final
  process snapshots and reclaims every live process record before returning.
  Scheduler-backed child processes retain phase/root/component, parent, depth,
  registration, and cancelable-task identity. Parents cannot complete while a
  descendant is running; nested callbacks execute in deterministic simulated
  time, and callback exceptions are contained as `FSIM-UVM-PHASE-006` while
  their descendant trees are killed. Process-count/depth limits reject before
  publication. Timeout, jump, root teardown, and phase-service destruction
  cancel only owned scheduler handles and objection drains, invalidate live
  process handles, and prevent post-phase callbacks. Caller-owned scheduler
  tasks, sibling roots, and independently executing sibling domains remain
  untouched. Runtime proof covers objection and drain completion gates,
  immutable final snapshots, parent/child/grandchild await behavior, scheduled
  failure containment, timeout cleanup, jump cleanup with sibling-domain
  survival, root-selective teardown, destruction cancellation, stale handles,
  missing schedulers, and process-count/depth ceilings. The application proof
  consumes final snapshots rather than retaining reclaimed live handles across
  all thirteen task phases and all three engines. Phase API, implementation,
  and focused tests remain below source policy at 563, 1,905, and 1,616 lines.
  The exact-LLVM Debug tree rebuilt 140 affected steps warning-clean, then 71
  focused application/runtime steps and the remaining 75 full-tree steps after
  final snapshot integration. The runtime, application, LLVM, catalog,
  source-policy, and UVM harness gates pass 6/6 in 32.37 seconds. `git diff
  --check` is clean.
- **Change 8 - Complete.** Close phase scheduler quiescence across multiple roots and
  domains: ready/end hook stabilization, objection races, zero-time iteration
  and callback ceilings, timeout/stop interaction, jump cancellation, teardown,
  restart, and independent concurrently alive simulations. Add deterministic
  deadlock/livelock diagnostics and prove no phase, objection, drain, process,
  or scheduled-event state leaks into the next simulation.
  A bounded phase quiescence coordinator now owns the transition from executing
  task work through stable ready-to-end and ended hooks. It waits only through
  the simulation scheduler, retries ready-to-end when a callback raises an
  objection or starts a drain, and returns explicit completed, stopped, or
  timed-out results with exact tick, callback, iteration, cancellation, and
  final execution evidence. The application-owned phase service is attached to
  its actual interpreter scheduler after construction, so the same ownership
  contract is available through public simulation state. Missing pending work
  with live phase state diagnoses deadlock; delta-cycle livelock, ready-to-end
  re-entry, scheduler callback work, zero-time stabilization, and outer
  iteration ceilings diagnose as `FSIM-UVM-PHASE-008` after phase-local process
  and objection cleanup. Timeout and stop reclaim only phase-owned work;
  backward jump resets a stopped/timed-out interval for clean execution.
  Runtime proof covers a two-root ready objection/drain race that stabilizes at
  tick 3, exact tick-5 timeout with caller-task survival, stop cleanup and
  restart, no-work deadlock, zero-time delta livelock, callback and ready
  re-entry ceilings, and a separately owned live simulation that completes at
  tick 2 with no retained process or objection state. The phase header, main
  implementation, extracted coordinator, and focused test remain below policy
  at 591, 1,929, 110, and 384 lines; the application simulation remains 1,901
  lines. The final eight-worker exact-LLVM Debug continuation rebuilt 108
  affected steps warning-clean. The runtime, application, LLVM, catalog,
  source-policy, and UVM harness gates pass 6/6 in 32.93 seconds. `git diff
  --check` is clean.
- **Change 9 - Complete.** Define bounded TLM1 endpoint ownership and connection graphs for
  ports, exports, and implementation endpoints. Preserve interface/profile,
  min/max connection cardinality, source declaration order, hierarchy and
  root identity, fanout, chained exports, resolution, debug naming, and stable
  binding. Reject kind/profile/root/direction mismatches, cycles, duplicates,
  unconnected required endpoints, and count/depth/fanout excess atomically.
  Each simulation now owns a dedicated service with owner-qualified endpoint
  handles, exact nominal request/response profiles, forward/backward/bidirectional
  direction, component/root identity, declaration-ordered edges, and stable
  debug names. Ports and exports bind through ordered export chains to one or
  more implementation endpoints; a whole-graph resolution pass publishes no
  binding until every live endpoint, cardinality, depth, and terminal target is
  valid. Every mutation invalidates the prior revision, while rejected
  registration, connection, cycle, and resolution attempts preserve all
  previously published graph state. `FSIM-UVM-TLM1-001` through `004` separate
  ownership/lifetime, endpoint/cardinality, structural compatibility, and
  resource failures. Focused runtime proof covers two roots, stable fanout and
  chained resolution order, exact hierarchy/debug identity, foreign and stale
  handles, kind/interface/nominal-profile/direction/root mismatches, duplicate
  edges, cycles, required endpoints, endpoint/fanout/depth ceilings, and
  recovery after failed binding. The eight-worker exact-LLVM Debug tree rebuilt
  142 affected steps warning-clean. Runtime, application, LLVM, source-policy,
  and UVM harness gates passed on the first qualification run; after catalog
  synchronization, all six runtime, application, LLVM, catalog, source-policy,
  and UVM harness gates pass in 31.70 seconds. `git diff --check` is clean.
- **Change 10 - Complete.** Execute blocking and nonblocking put/get/peek/transport plus
  master/slave and bidirectional TLM1 interfaces. Implement bounded FIFOs,
  reservation and wake ordering, can/try behavior, request/response ownership,
  simulated-time blocking, cancellation at phase end, and payload nominal-type
  preservation through interpreter, compiled, and debug execution.
  The TLM1 service now owns generation-checked operation handles, exact request
  and response payload records, implementation-bound FIFO and transport state,
  and deterministic reservation order. Blocking put/get/peek operations either
  complete immediately or retain bounded FIFO reservations; every feasible
  waiter wakes in global reservation order without allowing a full FIFO to
  deadlock an older consuming operation. Nonblocking can/try surfaces preserve
  queue state on failure. Provider-side FIFO access closes master request/get-
  response and slave get-request/put-response flows, while bidirectional
  endpoints retain all four operations. Transport supports immediate try
  responses and externally completed blocking responses at exact scheduler
  time. Payloads retain nominal type, packed width/value, object identity,
  root, endpoint, and sequence ownership; stale or nominally incompatible
  objects reject before transfer. Phase timeout, completion, jump, root
  teardown, and service destruction cancel matching waits and scheduled
  completion callbacks without disturbing unphased operations. Runtime proof
  covers bounded FIFO capacity and pending counts, can/try semantics, blocked
  put/get/peek wake order, master/slave/bidirectional profiles, live/stale object
  payloads, delayed transport at tick 3, selective phase cancellation, and
  post-phase callback suppression. Application proof executes an exact 96-bit
  nominal payload round trip in interpreter, LLVM compiled, and debug engine
  contexts. `FSIM-UVM-TLM1-005` and `006` distinguish invalid execution from
  resource exhaustion. The final eight-worker exact-LLVM Debug tree rebuilt
  143 affected steps warning-clean; the public header, structural source,
  execution source, focused execution test, and application simulation remain
  below policy at 466, 362, 874, 464, and 1,905 lines. Runtime, application,
  LLVM, catalog, source-policy, and UVM harness gates pass 6/6 in 31.14 seconds;
  `git diff --check` is clean.
- **Change 11 - Complete.** Execute analysis ports, exports, implementation endpoints,
  subscribers, analysis FIFOs, and macro-generated analysis implementations.
  Preserve registration-order broadcast, per-subscriber value/object semantics,
  snapshot mutation rules, exception containment, recursive publication bounds,
  and cross-root connection validation while allowing intentional fanout and
  chained exports.
  Analysis is now an exact TLM1 interface profile over the existing bounded
  port/export/implementation graph. Each publication freezes its resolved
  implementation list and subscriber callbacks before delivery, broadcasts in
  resolution/declaration order across intentional fanout and chained exports,
  and assigns one publication payload sequence to every target. Value payloads
  are copied independently per subscriber; object payloads retain one shared
  live handle so ordered subscriber mutations remain visible. Subscriber and
  FIFO failures are contained per implementation and later snapshot targets
  continue. Graph or callback mutation during `write` affects only the next
  successfully rebound publication. Named analysis-implementation registration
  models macro-generated implementation families without collapsing distinct
  endpoint identities. Bounded analysis FIFOs preserve immutable publication
  snapshots, while publication count, callback fanout, recursive depth,
  retained failures, delivery sequence, and queued payloads are preflighted.
  Runtime proof covers ordered port/export fanout, cross-root rejection, value
  isolation, shared-object mutation, subscriber exceptions, full FIFO
  containment, graph/callback snapshot mutation, later rebinding, macro-named
  implementations, recursion cutoff, and atomic callback-ceiling rejection.
  Application proof broadcasts an exact 73-bit nominal payload through the
  interpreter, LLVM compiled, and debug engine contexts. `FSIM-UVM-TLM1-007`
  and `008` distinguish invalid/contained analysis work from bounded recursive
  publication exhaustion. The final eight-worker exact-LLVM Debug tree rebuilt
  140 affected steps warning-clean. The public header, structural source,
  execution source, analysis source, focused analysis test, and application
  simulation remain below policy at 519, 366, 874, 210, 320, and 1,905 lines.
  Runtime, application, LLVM, catalog, source-policy, and UVM harness gates pass
  6/6 in 30.41 seconds; `git diff --check` is clean.
- **Change 12 - Complete.** Execute the UVM TLM2 generic-payload and socket families,
  blocking and nonblocking forward/backward transport, debug transport, direct
  memory interface, initiator/target and passthrough sockets, phase values,
  delays, extensions, response status, and connection validation. Bound payload
  bytes, byte enables, streaming width, extensions, hops, callbacks, and
  outstanding transactions with deterministic nominal/profile diagnostics.
  The simulation-owned service now binds generation-checked, root-qualified
  initiator, target, and both passthrough socket families through exact
  protocol, nominal payload/phase, bus-width, cardinality, cycle, and hop
  validation. Blocking transport, nonblocking forward/backward phase state,
  debug transport, DMI acquisition/invalidation, annotated delays, extensions,
  response status, cancellation, and immutable completed/cancelled transaction
  snapshots preserve exact owner identity and deterministic target resolution.
  `FSIM-UVM-TLM2-001` through `005` distinguish invalid handles, socket graphs,
  payloads/DMI, callbacks/phases, and bounded resources. Runtime proof covers
  a three-hop passthrough chain, cross-root/profile/cycle rejection, payload and
  phase nominal mismatch, byte enables, streaming width, extensions, response,
  debug, DMI, forward/backward and custom phases, exception containment,
  cancellation, and payload, callback, outstanding-transaction, and hop
  ceilings. Application proof executes an exact 37-byte generic payload with
  extension, response, and tick-7 completion in interpreter, LLVM compiled,
  and debug contexts. The public header, structural source, transport source,
  focused test, and application simulation remain below policy at 430, 363,
  551, 498, and 1,908 lines. The final eight-worker exact-LLVM Debug tree
  rebuilt 140 affected steps warning-clean. Runtime, application, LLVM,
  catalog, source-policy, and UVM harness gates pass 6/6 in 35.45 seconds;
  `git diff --check` is clean.
- **Change 13 - Complete.** Expose phase, objection, connection, FIFO, socket, payload,
  drain, and process state through the debugger and public application queries.
  Provide stable read-only snapshots, hierarchy/root qualification, bounded
  enumeration and formatting, breakpoint/step behavior at phase transitions,
  and generation-safe rejection while simulations advance or tear down. The
  public `UvmDebugSnapshot` captures exact scheduler time/delta plus domain,
  phase, phase-process, objection, drain, TLM1 endpoint/FIFO/operation, and
  TLM2 socket/transaction state. Every connection retains its root-qualified
  debug name, and retained TLM payloads remain owning copies. Record, aggregate
  payload-byte, and formatted-output ceilings reject through
  `FSIM-UVM-DEBUG-002`; generation, service-revision, and time/delta checks
  convert stale or advancing capture into `FSIM-UVM-DEBUG-001`. The application
  now owns and publicly exposes the objection service alongside the other UVM
  services. Debugger `uvm summary|phases|objections|tlm1|tlm2|all` commands use
  the same bounded public snapshot, while domain-qualified phase breakpoints
  and `step phase` poll only scheduler safe points and preserve existing source,
  signal, time, statement, process, delta, and time-step behavior. Runtime proof
  covers exact objection/drain enumeration; application proof covers the full
  two-domain, 21-phase, two-root, four-endpoint/one-FIFO, and two-socket view,
  root-qualified formatting, resource rejection, and debugger phase-breakpoint
  registration in interpreter, LLVM compiled, and debug engines. The public
  application header, debugger, inspection unit, application simulation, phase
  implementation, and objection implementation remain below policy at 685,
  1,826, 259, 1,912, 1,946, and 876 lines. The final eight-worker exact-LLVM
  Debug tree rebuilt 75 affected steps warning-clean; runtime, application,
  LLVM, catalog, source-policy, and UVM harness gates pass 6/6 in 31.63 seconds,
  and `git diff --check` is clean.
- **Change 14 - Complete.** Integrate phase/TLM activity with simulation callbacks and
  VCD/FST traces. Define exact callback ordering for graph, phase-state,
  objection, drain, connection, transaction, FIFO, and quiescence events;
  contain callback mutation/re-entry/exceptions; publish deterministic trace
  names and value transitions without making observer presence alter scheduling.
  The simulation-owned `SystemVerilogUvmActivityService` now stamps immutable
  events with one monotonic sequence and scheduler time/delta, retains bounded
  history, and invokes a frozen token-ordered observer snapshot. Observer add/
  remove mutation affects only later events, re-entry is bounded, and standard,
  nonstandard, and failure-log-overflow exceptions remain contained so observer
  presence cannot change scheduler completion. Phase graph/state, objection,
  drain, TLM1/TLM2 connection and transaction, FIFO occupancy, and quiescence
  paths publish root-qualified identities and deterministic actions. Public
  `Simulation` accessors and hooks expose the same stream. VCD attaches in the
  reserved `__fsim.uvm.activity` hierarchy, replays pre-attachment history, and
  traces sequence, kind, action, root, value, and stable identity/detail hashes;
  its observer is detached before trace storage is released. This backend-
  neutral event contract is the FST integration boundary without prematurely
  implementing the FST encoder assigned to Batch 171. Runtime proof covers
  frozen callback mutation, exception and full-failure-log containment, bounds,
  objection/drain order, FIFO transitions, and quiescence completion.
  Application proof covers graph, phase, connection, transaction, and FIFO
  events in interpreter, LLVM compiled, and debug engines, plus deterministic
  VCD declarations and timestamp scaling. `FSIM-UVM-ACTIVITY-001` and `002`
  catalog invalid/contained observer work and resource exhaustion. The activity
  header/source, phase, objection, TLM1 header/execution, TLM2 header/transport,
  application trace/simulation, focused activity/objection/quiescence tests,
  and application matrix remain below policy at 125, 108, 1,990, 908, 532,
  961, 441, 605, 1,090, 1,928, 95, 788, 393, and 2,164 lines. The eight-worker
  exact-LLVM Debug tree rebuilt 79 affected steps warning-clean, followed by
  four focused test steps; runtime, application, LLVM, catalog, source-policy,
  and UVM harness gates pass 6/6 in 28.71 seconds, and `git diff --check` is
  clean.
- **Change 15 - Complete.** Integrate phase, objection, and TLM state with the public DPI
  and VPI boundaries. Add bounded generation-safe queries and callbacks,
  payload copy/borrow ownership rules, stable C layouts and diagnostics, and
  prove native plug-in unload, cancellation, multiple-root isolation, and
  restart without extending the frozen ABI incompatibly. The new append-only
  `fsim_uvm_foreign_host_v1` table is shared by DPI and VPI integrations and
  leaves their frozen plug-in descriptors unchanged. Its fixed-width C layouts
  expose simulation-qualified snapshot generations, records, and activity
  callbacks. Callers first query exact identity/detail/payload sizes and then
  copy into caller-owned buffers; callback strings are explicitly borrowed only
  for the invocation. The simulation-owned
  `SystemVerilogUvmForeignService` captures phase/process, objection/drain,
  TLM1 endpoint/FIFO/operation, and TLM2 socket/transaction state from the live
  services into bounded owning snapshots. Release makes generations stale,
  cross-service identities reject as wrong-simulation, and record/text/payload,
  retained-snapshot, callback, and caller-buffer bounds fail without partial
  copies. Callback status failures flow through Change 14's contained activity
  observer path, and service teardown removes every callback before foreign
  storage or a native image can disappear. Application proof exercises the C
  host in interpreter, LLVM compiled, and debug engines, including two roots,
  exact 37-byte copied TLM2 payload, zero-depth TLM1 FIFO, buffer sizing,
  cross-simulation and stale rejection, snapshot exhaustion, outstanding-
  transaction cancellation, callback failure containment, teardown, and fresh
  per-engine simulation identities. A C translation unit freezes the ABI
  layouts. `FSIM-UVM-FOREIGN-001` and `002` catalog identity/layout/callback and
  resource failures. The ABI header, C++ header, implementation, application
  header/simulation, C ABI probe, and application matrix remain below policy at
  122, 103, 382, 698, 1,932, 17, and 2,346 lines. The eight-worker exact-LLVM
  Debug tree rebuilt 77 affected steps warning-clean; runtime, application,
  LLVM, catalog, source-policy, and UVM harness gates pass 6/6 in 28.18 seconds,
  and `git diff --check` is clean.
- **Change 16 - Complete.** Version and preserve phase graphs, scheduler
  checkpoints, objections/drains, TLM connections/FIFOs/payloads, and required
  provenance in portable object/design/runtime artifacts. Prove relocation,
  corruption and schema negatives, interpreter/LLVM deterministic replay,
  repeated simulation, isolated cold/warm native-cache identities, and clean
  restart; do not archive host pointers, live callbacks, or nonportable process
  objects. The new schema-1 `SystemVerilogUvmCheckpoint` captures exact
  scheduler time/delta; phase execution, topology, edges, synchronization, and
  roots; objection/drain state; TLM1 endpoint graphs, operation payloads, and
  queued FIFO values; TLM2 socket graphs and transaction payload/phase/result
  state; and content, cache, design-artifact, and root-alias provenance. All
  serialized records own bounded text and bytes. Live callbacks and phase
  process objects are represented only by fixed-width counts and can never
  enter the portable record stream. `.fsimobj` continues to carry the portable
  class/source definition; design format 3 now requires the `sv-uvm` payload at
  `state/sv-uvm.bin`, framed by `FSIMUVM1`, while the same runtime model supports
  live checkpoint capture and exact replay comparison. Design publication
  creates a pointer-free 21-phase bootstrap checkpoint and design loading
  validates its schema, foreign ABI, provenance, structure, payload checksum,
  and exact clean-restart baseline before simulation. Runtime proof covers
  unchanged verification, deterministic construction, schema/ABI/provenance/
  root/state mismatch, resource ceilings, and explicit nonportable-record
  rejection. Application proof covers byte-stable repeated serialization,
  truncation, future schema, physical payload corruption, relocation between
  aliased roots, isolated cold/warm caches, clean restart, and exact live state
  equality across interpreter, LLVM compiled, and debug execution. The
  checkpoint header/source, foreign header/source, TLM1 header/execution,
  application header/codec/design/simulation, runtime test, artifact test,
  class matrix, and two focused case wrappers remain below policy at 121, 245,
  106, 548, 534, 970, 705, 952, 940, 1,942, 159, 915, 2,396, 11, and 11 lines.
  `FSIM-UVM-STATE-001` and `002` catalog invalid portable state and bounded
  capture/construction failures. The exact-LLVM Debug tree completed a full
  685-step warning-clean rebuild and the post-format-bump incremental build is
  clean; runtime, application, LLVM, catalog, source-policy, and UVM harness
  gates pass 6/6 in 32.00 seconds, and `git diff --check` is clean.
- **Change 17 - Complete.** One exact source fixture imports the unmodified UVM
  1.2 and UVM 2020-3.1 `uvm_object`, `uvm_component`, `uvm_phase`, parameterized
  blocking-put port, and TLM FIFO types. Its user component constructor and real
  build/connect/end-of-elaboration/start-of-simulation/run/extract/check/report/
  final callbacks execute on two aliased roots; simulation-owned intrinsic
  construction materializes upstream UVM base services without executing a
  second package-owned scheduler hierarchy. The public task-phase continuation
  keeps host/DPI/SystemC processes suspended while the simulation-owned
  objection/drain scheduler completes the run phase. Direct source, one
  portable object, relocated O0/O2 portable designs, interpreter, compiled,
  debug, callbacks, isolated cold/warm native caches, and activity traces all
  produce the exact transcript with phase order, objection `1/0`, three-tick
  drain, payload `37`, and result `42`. Executable assertions prove cold zero-hit
  miss/store and warm nonzero-hit zero-miss behavior. Every direct/O0/O2 cold,
  warm, and debug trace from both releases is 7,062 bytes and has SHA-256
  `4546c05e2625a4f934cbf1e30e3786e78b2abed76871a32434b0454eaac71853`.
  The `.fst`-designated files intentionally carry the backend-neutral VCD
  activity stream; the binary FST encoder remains assigned to Batch 171.
  Stage-isolated capped measurements keep UVM 1.2/2020.3.1 direct analysis at
  4,242,276/4,735,592 KiB RSS under 6 GiB, compile at 3,308,520/3,672,868 KiB
  under 5/6 GiB, O0/O2 elaboration at 3,198,008/3,198,256 and
  3,441,912/3,441,688 KiB under 5/5.5 GiB, and every execution below
  1,000,000 KiB under 3 GiB. Neither upstream library is patched. The final
  104-step eight-worker exact-LLVM Debug rebuild is warning-clean; runtime,
  application, LLVM, catalog, source-policy, and UVM-harness gates pass 6/6 in
  32.27 seconds, both dedicated exact-library tests remain registered, and the
  application header/simulation/source-method/phase bridge, fixture, focused
  test, and runner remain below policy at 709, 1,943, 652, 114, 82, 499, and 47
  lines.
- **Change 18 - Complete.** One executable aggregate contract now maps all 33
  `FSIM-UVM-PHASE`, `OBJ`, `TLM1`, `TLM2`, `DEBUG`, `ACTIVITY`, `FOREIGN`, and
  `STATE` diagnostics to runtime or application rejection evidence. It also
  requires positive graph/jump/synchronization/callback, objection/drain,
  process-cancellation, TLM1/TLM2, artifact/relocation/restart, observer, and
  every governed resource-ceiling owner. Exact-code assertions were tightened
  for contained TLM1 analysis/FIFO failures, stale and cross-service TLM2
  sockets, released/cross-simulation foreign handles, stale debug state, and
  checkpoint record/text/root/identity/payload/process/callback summaries. Live
  checkpoint capture now distinguishes provenance limit exhaustion from invalid
  artifacts and retains external process/callback counts without serializing
  host state. The exact two-root UVM fixture adds a ready-to-end objection race
  that deterministically drains at tick 5 and a suspended-process deadlock that
  rejects with `FSIM-UVM-PHASE-008` while leaving no process or objection state.
  Both outcomes agree through direct source, O0/O2 compiled cold/warm caches,
  debug, portable restart, and both unmodified UVM releases. The registered
  exact tests pass in 367.21 and 481.55 seconds; all fourteen final traces are
  10,357 bytes with SHA-256
  `f78d9f125531a9d4764e6c5336e0bfdf8d9893577932f1cbb0d198355fb7c7ac`.
  The eight-worker exact-LLVM Debug build completed 77 affected steps warning-
  clean. Catalog, aggregate-matrix, source-policy, UVM-harness, application-
  class, and runtime gates pass 6/6 in 7.65 seconds, and `git diff --check` is
  clean. The checkpoint header/source, foreign source, checkpoint/analysis/TLM2
  runtime tests, application class/exact matrices, aggregate contract, and
  runner remain below policy at 120, 267, 556, 193, 322, 462, 2,302, 565, 130,
  and 47 lines.
- **Change 19 - Complete.** The public README, UVM guide and exact transcript,
  architecture, language support, diagnostic catalog, upstream provenance,
  resource baselines, test inventory, feature evidence, release/inventory/
  differential audits, candidate corpus, and restart handoff now state the
  bounded Batch 160 phase/objection/TLM contract. `SV-801` through `SV-810`
  advance the reviewed matrix to 1,250 executable rows and 5,000 evidence cells
  across 545 exact paths: 237 test, 286 production, and 22 release paths with
  135 runtime owners. Matrix digest
  `25300a46781d8945f884358d4fabf05489415cbbee97405edffee7e4a0be4943`
  and evidence digest
  `49f1bb1b659bf6e63aa95402f9e0e302330647d0efa8d9c08f6d7cb1be2d62ba`
  are frozen into the release contracts. Inventories cover 2,023 production
  diagnostics, 769 bounded C/C++ sources, 870 SPDX-owned authored files, and
  285 authored test/control files; the governed upstream trees remain external
  build products. The main exact-LLVM Debug tree registers 118 tests and the
  governed-source configuration adds the two dedicated unmodified-UVM tests.
  All 28 documentation, catalog, conformance, inventory, installation,
  portability, UVM matrix, and release-candidate gates pass in 17.87 seconds;
  all synchronized authored files remain below policy and `git diff --check`
  is clean.
- **Change 20 - Complete.** The
  fresh LLVM-disabled GCC 13.3 ASan/UBSan tree built warning-clean with eight
  workers. Its first 116/117 run exposed one stale VITAL relocation fixture
  that copied the six pre-Batch-160 design-state payloads but omitted required
  `state/sv-uvm.bin`; the fixture now copies all seven payloads. The isolated
  repair passes, and the exact final sanitizer tree passes 117/117 in 1,180.07
  seconds with leak detection disabled and ASan/UBSan halt-on-error enabled.
  Fresh exact-LLVM 22.1.8 Debug and Release trees each complete 679-step
  warning-clean eight-worker builds and pass 117/117 tests in 381.93 and
  314.14 seconds. Those complete suites include the source, diagnostic,
  UVM-matrix, inventory, installed-public, resource, portability,
  differential, and release-candidate contracts.

  The governed-source tree additionally completes a 338-step warning-clean
  eight-worker rebuild and reruns the two exact unmodified upstream matrices:
  UVM 1.2 passes in 369.34 seconds and UVM 2020-3.1 passes in 486.87 seconds,
  2/2 in 856.23 seconds. Their authoritative build evidence contains exactly
  fourteen direct/O0/O2 cold/warm/debug traces, each 10,357 bytes with SHA-256
  `f78d9f125531a9d4764e6c5336e0bfdf8d9893577932f1cbb0d198355fb7c7ac`.
  All four qualification trees are no-op under eight workers and every final
  log is free of sanitizer, CTest-failure, and runtime-error markers. The
  accumulated implementation is commit
  `2b5810303c7f8e39f9846955d95871ada5bfcd2c`. Hosted portability repairs are
  commits `95d3bd6`, `75f5d47`, `a7a96eb`, `73313eb`, `97d6378`, `7e70919`,
  `534d31c`, `a94bc9e`, `7434520`, and `bc2a8ed`. Run `31286177825` then exposed
  a 44-byte malformed-SystemVerilog fuzz input that grew beyond 20 GiB and a
  recursive UVM clone that retained invalidated class-heap vector references on
  Windows. Commit `2158b2cd484b9b06f66621d2d3dca5eabb9cb322` adds parser
  progress invariants and exact fuzz evidence, snapshots UVM copy state across
  recursive allocation, and forces the reallocation case in its regression.
  Locally, the exact fuzz reproducer falls from `std::bad_alloc` at 1,925,644
  KiB RSS to success at 33,424 KiB; a deterministic 20,000-run fuzz campaign
  passes at 43,232 KiB maximum RSS; focused Debug, Release, sanitizer, repeated
  VHDL-composite, clang-cl `/W4 /WX`, portability, and full eight-worker builds
  pass.

  Replacement run `31304022606` passes the frontend fuzz smoke, all four Ubuntu
  jobs, both Windows clang-cl jobs, both ordinary Windows MSVC jobs, and Windows
  MSVC/LLVM Release. Its sole non-green job, Windows MSVC/LLVM Debug, passes
  tests 1-75 of 118 before GitHub cancels the still-running
  `fsim.application.sv_containers` test at the workflow's 70-minute job ceiling;
  the log contains no test failure. On 2026-08-09 the user explicitly accepted
  that result, directed increasing the Windows LLVM job ceiling to 120 minutes,
  and directed advancement to Batch 161. Batch 160 is therefore closed under
  that explicit acceptance. Save and push Batch 161's restart checkpoint and
  clear context before its implementation.

### Batch 161 - UVM sequences, callbacks, and register model

- **Change 1:** define simulation-owned, generation-checked sequence-item,
  sequence, and sequencer identities; exact parent/child and item ownership;
  lifecycle states; source-order registration; nominal request/response
  profiles; transactional construction; resource ceilings; and cataloged stale,
  cross-simulation, type, hierarchy, and limit diagnostics. Add focused runtime
  ownership and lifecycle evidence without executing sequence bodies early.
- **Change 2:** execute sequence pre-start, pre-body, body, post-body, and
  post-start callbacks with deterministic parent/child nesting, automatic phase
  objections, kill/stop semantics, response routing, exception containment,
  phase-process ownership, and exact interpreter/compiled/debug behavior.
- **Change 3:** implement sequencer request queues and all standard FIFO,
  random, strict-FIFO, strict-random, weighted, and user arbitration modes with
  priorities, relevance, wait-for-relevant, deterministic random streams,
  reseeding, bounded selection work, and stable tie breaking.
- **Change 4:** implement sequence lock and grab queues, unlock/ungrab, nested and
  child ownership, response-queue depth/error policy, item/sequence macros, and
  deterministic constraint-randomization integration. Prove cancellation,
  starvation bounds, stale handles, illegal ownership, and rollback.
- **Change 5:** implement driver-sequencer pull and push handshakes, including
  get-next-item/item-done, try-next-item, get/peek, put-response, request/response
  identity, pipelining, backpressure, phase timeout/jump cancellation, and
  retained completed/cancelled transaction snapshots.
- **Change 6:** integrate drivers, monitors, active/passive agents, subscribers,
  and scoreboards with component construction, configuration, phases, TLM1
  analysis, objections, and deterministic multiple-root teardown. Add bounded
  source-level UVM dispatch and focused positive/negative role evidence.
- **Change 7:** implement virtual sequencers and virtual sequences across
  multiple typed child sequencers, coordinated starts, priorities, locks,
  objections, reset/restart, sibling-domain execution, and exact process-tree
  cancellation without leaking requests or responses.
- **Change 8:** implement type-wide and instance UVM callbacks with ordered
  add/delete/prepend, iterator-safe mutation, callback masks, exception
  containment, and bounded re-entry. Add begin/end transaction recording,
  parent/link identity, attributes, timing, activity events, debugger visibility,
  and stable engine-neutral trace records.
- **Change 9:** define simulation-owned register-model block, map, register,
  field, and memory identities with hierarchy, build/lock/freeze, source-order
  declaration, widths, offsets, dimensions, ownership, bounded construction,
  and cataloged invalid-model diagnostics.
- **Change 10:** implement register-field access policies, volatile behavior,
  reset kinds/values, desired and mirrored values, predict modes, compare policy,
  individual-field/register/memory operations, status propagation, and atomic
  rollback for invalid rights, widths, indices, and values.
- **Change 11:** implement register maps with hierarchical submaps, bus widths,
  byte addressing/enables, little/big/FIFO endianness, unmapped registers,
  multiple maps, per-map rights, address lookup, burst memory access, and
  deterministic overlap/alignment/overflow rejection.
- **Change 12:** implement register adapters and predictors over the completed
  sequencer/driver/TLM services, generic bus items, frontdoor read/write/update/
  mirror operations, auto-prediction, explicit prediction, response/status
  conversion, and phase-owned timeout/cancellation behavior.
- **Change 13:** implement user frontdoors and VPI/VHPI-backed HDL paths for
  register, field, memory, and sliced/concatenated paths; support read, deposit,
  force/release where legal, multiple abstraction kinds, language-mixed roots,
  relocation-safe identities, and exact access diagnostics.
- **Change 14:** implement standard reset, hardware-reset, bit-bash, access,
  shared-access, memory-access, memory-walk, and register-model traversal
  sequences with resource-map selection, exclusions, rights awareness,
  deterministic random streams, and bounded failure aggregation.
- **Change 15:** implement register pre/post read/write callbacks at field,
  register, memory, map, and block scopes plus register coverage models,
  per-map/per-field sampling, reset/desired/mirror crosses, callback mutation,
  exception containment, and governed coverage/resource ceilings.
- **Change 16:** integrate sequences, callbacks, transactions, and register
  models with debugger commands/breakpoints, activity/VCD, DPI/VPI/VHPI foreign
  snapshots, portable object/design artifacts, relocation, checkpoint/restart,
  deterministic replay, cache provenance, and multiple-simulation isolation.
- **Change 17:** run one representative source environment against exact,
  unmodified UVM 1.2 and UVM 2020-3.1 sequence/driver/monitor/agent/scoreboard
  surfaces. Prove arbitration, locks, responses, virtual sequences, callbacks,
  and transaction recording through direct source, portable objects, O0/O2
  designs, interpreter, compiled cold/warm cache, and debug execution under
  retained memory caps.
- **Change 18:** run one representative exact UVM register environment through
  frontdoor and VPI/VHPI backdoor access, adapters/predictors, mirrors/resets,
  byte enables, endianness, multiple maps, callbacks, coverage, standard
  register sequences, artifacts, relocation, replay, and all execution engines.
- **Change 19:** consolidate the arbitration, handshake, callback, register,
  bounds, race, cancellation, stale/cross-owner, and negative diagnostic
  matrices; synchronize the public UVM guide/example, architecture, language
  support, diagnostics, provenance, resource baselines, conformance evidence,
  inventories, audits, and restart handoff; pass all documentation and release
  contracts.
- **Change 20:** run fresh full non-sanitized exact-LLVM 22.1.8 Debug and Release
  eight-worker builds, all regressions, exact upstream UVM sequence/register
  tests, memory and source audits, installed/public/portability contracts, and
  release gates. Commit and push the one accumulated Changes 1-20
  implementation only after every local gate is clean; Batch 161 is not a
  sanitizer or hosted-CI monitoring boundary.

### Batch 162 - UVM 1.2 and UVM 2020-3.1 conformance closure

- **Changes 1-4:** close printer, comparer, packer, recorder, copier, transaction,
  event/barrier/pool/queue, heartbeat, spell-challenge, and policy-class behavior.
- **Changes 5-8:** close command-line processor, plusargs, test selection,
  topology/reporting switches, timeout, seed handling, objection tracing, and
  factory/config tracing.
- **Changes 9-12:** close UVM 1.2 compatibility macros/APIs and UVM 2020-3.1
  additions, deprecations, behavioral differences, and version selection.
- **Changes 13-16:** run standard and project-owned smoke suites for factory,
  phases, sequences, TLM, callbacks, register model, coverage, DPI, and
  VPI/VHPI backdoors across platforms and engines.
- **Changes 17-19:** close every diagnosed UVM gap, freeze compatibility docs
  and evidence inventories, and publish a producer-independent tutorial.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 163 - VHDL-2008 and PSL digital-language closure

- **Changes 1-4:** inventory and close remaining VHDL-2008 lexical, declaration,
  type, expression, overload, generic, package, configuration, context, and
  external-name gaps.
- **Changes 5-8:** close remaining sequential/concurrent, access/file/protected,
  resolution, postponed, shared-variable, disconnect, guard, block, and generate
  semantics.
- **Changes 9-12:** parse, analyze, and execute the IEEE 1850 PSL subset embedded
  in VHDL, including clocks, sequences, properties, directives, abort/vacuity,
  reports, and coverage.
- **Changes 13-16:** integrate residual VHDL/PSL behavior with VITAL, mixed
  boundaries, multiple roots, debugger/callback/trace, artifacts, relocation,
  caches, and VHPI.
- **Changes 17-19:** add LRM-indexed positive/negative conformance matrices,
  cross-engine tests, docs, diagnostics, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 164 - Verilog-2005 residual language closure

- **Changes 1-4:** inventory and close remaining IEEE 1364-2005 lexical,
  directive, config/library, declaration, net/variable, expression, generate,
  and hierarchy rules.
- **Changes 5-8:** close remaining gate/switch/UDP, strength, charge, continuous/
  procedural assignment, event, task/function, memory, timing-control, and race
  semantics beyond Batches 144-146.
- **Changes 9-12:** close remaining specify/path/timing-check/pulse behavior,
  compiler directives, celldefine/unconnected-drive/default-net behavior, and
  standard system tasks/functions.
- **Changes 13-16:** integrate residual behavior with VPI/DPI, SDF-ready timing
  identity, mixed hierarchy, debugger/trace, artifacts, relocation, and caches.
- **Changes 17-19:** add LRM-indexed conformance and negative matrices,
  cross-engine/platform tests, docs, diagnostics, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 165 - SystemVerilog-2017 residual language closure

- **Changes 1-4:** inventory every remaining IEEE 1800-2017 grammar and semantic
  gap after Batches 147-155, including checker, let, nettype, alias, bind,
  package, interface, class, and callable corners.
- **Changes 5-8:** close remaining expression/type/assignment-pattern/cast,
  aggregate/container, streaming, random, process, timing, and scheduler rules.
- **Changes 9-12:** close remaining hierarchy/configuration, assertions,
  coverage, clocking/program, system task/function, file/memory, and
  introspection behavior.
- **Changes 13-16:** integrate all residual constructs with UVM, DPI/VPI,
  multiple roots, mixed language, debugger/trace, artifacts, relocation, and
  caches.
- **Changes 17-19:** publish a complete LRM-indexed zero-gap matrix with
  positives/negatives and cross-engine/platform evidence; update all docs and
  inventories.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 166 - Older VHDL standard modes

- **Changes 1-4:** add explicit VHDL-87, VHDL-93, VHDL-2000, and VHDL-2002
  modes, manifest/CLI selection, standard identity, cache keys, and diagnostics.
- **Changes 5-8:** implement revision-specific tokens, grammar, declaration/type,
  expression, association, subprogram, package, configuration, generate, and
  statement legality.
- **Changes 9-12:** provide revision-correct predefined environments, IEEE
  library profiles, semantic defaults, protected/shared rules, and migration
  diagnostics for newer constructs.
- **Changes 13-16:** preserve revision identity through libraries, artifacts,
  mixed boundaries, debugger, VHPI, relocation, caches, and non-project phases.
- **Changes 17-19:** add standard-specific positive/negative corpora and
  cross-platform/engine evidence; update docs, matrices, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 167 - Older Verilog and SystemVerilog standard modes

- **Changes 1-4:** add Verilog-1995/2001/2001-noconfig and SystemVerilog-2005/
  2009/2012 modes, manifest/CLI selection, identity, cache keys, and diagnostics.
- **Changes 5-8:** enforce revision-specific preprocessing, keywords, grammar,
  declarations/types, ports, hierarchy/generate, expressions, assignments,
  processes, assertions, classes, interfaces, and packages.
- **Changes 9-12:** provide revision-correct predefined names, system tasks,
  DPI/VPI profiles, default semantics, compatibility switches, and actionable
  newer-feature diagnostics.
- **Changes 13-16:** preserve revision identity through libraries, artifacts,
  mixed boundaries, debugger, relocation, caches, and non-project phases.
- **Changes 17-19:** add standard-specific positive/negative corpora and
  cross-platform/engine evidence; update docs, matrices, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 168 - SDF 4.0 parser, normalization, and artifacts

- **Changes 1-4:** implement complete SDF 4.0 lexical/parser coverage with
  headers, hierarchy dividers, escaped identifiers, conditions, triples,
  scaling, and precise diagnostics.
- **Changes 5-8:** accept SDF 2.1/3.0 inputs through explicit revision adapters
  and normalize all delay/timing-check constructs into one immutable SDF IR.
- **Changes 9-12:** resolve celltype/instance/wildcard/divider names against
  elaborated multi-root mixed hierarchy with deterministic missing/ambiguous
  diagnostics and explicit annotation scope.
- **Changes 13-16:** version and preserve normalized SDF, source provenance,
  timescale, selection, digests, and mapping through design artifacts,
  libraries, relocation, non-project phases, and cache keys.
- **Changes 17-19:** add parser/schema/corruption/resource negatives, standard
  fixture corpora, docs, diagnostics, matrices, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 169 - Verilog/SystemVerilog SDF annotation

- **Changes 1-4:** annotate module/interconnect/device/path/pulse delays onto
  elaborated Verilog/SystemVerilog specify and primitive timing objects with
  min/typ/max selection and incremental/absolute modes.
- **Changes 5-8:** annotate all standard timing checks, conditions, edge forms,
  notifier behavior, negative timing checks, retain/removal/recovery, and
  pathpulse semantics.
- **Changes 9-12:** define precedence and interaction with source delays,
  delay modes, transport/inertial queues, strengths/switches, force/release,
  multiple roots, and reannotation.
- **Changes 13-16:** expose annotation summaries/errors through CLI/API,
  debugger/callback/trace, VPI, artifacts, relocation, caches, and scripted
  compile/elaborate/simulate phases.
- **Changes 17-19:** add standard cell/timing fixtures, missing/mismatch/resource
  negatives, interpreter/LLVM/platform differentials, docs, and inventories.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 170 - VHDL/VITAL and mixed-language SDF - CI monitoring boundary

- **Changes 1-4:** map SDF cells, ports, generics, paths, and timing checks onto
  VHDL/VITAL primitives, delay records, wire/path functions, state tables,
  memory models, and vendor-compatible wrappers.
- **Changes 5-8:** implement VITAL/SDF precedence, timing generics, min/typ/max,
  transport/inertial/reject/pulse behavior, negative checks, and reannotation.
- **Changes 9-12:** annotate interconnect and timing across VHDL, Verilog,
  SystemVerilog, and SystemC proxy boundaries with explicit resolver/conversion
  semantics and multiple-root path identity.
- **Changes 13-16:** integrate mixed SDF with VHPI/VPI, debugger/callback/trace,
  artifacts, mapped libraries, relocation, caches, and non-project phases.
- **Changes 17-19:** add cross-language standard-cell/memory fixtures,
  ambiguity/mismatch/resource negatives, full engine/platform evidence, docs,
  matrices, inventories, and handoff.
- **Change 20:** run the monitoring-batch sanitizer, full Debug/Release and
  release gates, commit/push once, then inspect and repair all non-doc CI jobs.

### Batch 171 - FST tracing closure

- **Changes 1-4:** add a deterministic FST writer and public trace-format
  selection while preserving the existing trace model, hierarchy, aliases,
  timescale, scopes, and stable IDs.
- **Changes 5-8:** encode every supported scalar/vector/real/string/enum/
  physical/aggregate/class/strength value, X/Z/nine-state semantics, aliases,
  and changes without truncation.
- **Changes 9-12:** support multiple roots, mixed boundaries, callbacks,
  dynamic class/coverage/assertion values, selective tracing, late enablement,
  flush/close, and failure containment.
- **Changes 13-16:** integrate FST with CLI/Tcl/API/non-project simulation,
  artifacts, relocation, deterministic compression settings, and cross-platform
  byte identity where the format permits.
- **Changes 17-19:** add FST reader-based equivalence against VCD and internal
  traces, corrupt/I/O/resource negatives, docs, examples, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 172 - v2 artifact, ABI, and migration freeze

- **Changes 1-4:** inventory and freeze all v2 public C/C++/SystemC/DPI/VPI/VHPI
  ABIs, append-only layouts, symbol/version policies, installed headers, and
  compatibility tests.
- **Changes 5-8:** freeze `.fsimobj`, `.fsimdesign`, `.fsimlib`, incremental
  SystemC, native-cache, SDF, trace, coverage, assertion, and UVM schemas with
  explicit version/provenance identities.
- **Changes 9-12:** provide migrations from every v1/project schema and every
  supported earlier v2 artifact schema, plus clear unsupported downgrade and
  producer/version diagnostics.
- **Changes 13-16:** verify source-hidden relocation, read-only mappings,
  non-project restartability, mixed hosts/toolchains, stale/corrupt artifacts,
  and cache separation across Linux and Windows.
- **Changes 17-19:** publish ABI/schema/migration references, exhaustive positive
  and corruption matrices, inventories, release evidence, and restart handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 173 - Cross-platform conformance and performance qualification

- **Changes 1-4:** run complete VHDL/Verilog/SystemVerilog/SystemC/UVM/VITAL/
  SDF and foreign-interface conformance on Linux GCC/Clang and Windows
  MSVC/clang-cl Debug/Release with exact LLVM selection.
- **Changes 5-8:** run interpreter/LLVM O0/O2/debug, multiple-root, mixed-
  language, debugger, callbacks, VCD/FST, artifacts, relocation, and cold/warm/
  edit differentials with deterministic seeds and outputs.
- **Changes 9-12:** measure and repair compile, link, memory, elaboration,
  simulation, solver, UVM, SDF, and trace regressions while retaining four-job
  hosted and eight-worker local policies.
- **Changes 13-16:** execute long-duration, high-delta, wide-value, large-array,
  deep-hierarchy, plug-in, scheduler, failure-injection, and resource-limit
  stress suites without arbitrary language caps.
- **Changes 17-19:** close all failures, freeze performance/resource baselines,
  update portability/conformance matrices, inventories, and restart evidence.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 174 - v2 release candidate, packaging, examples, and documentation

- **Changes 1-4:** update every tutorial and example for multiple roots,
  libraries, non-project phases, incremental SystemC, UVM, DPI/VPI/VHPI, SDF,
  FST, older standards, debugger, and artifacts.
- **Changes 5-8:** complete user, language, architecture, API/ABI, plug-in,
  migration, diagnostics, troubleshooting, platform, and release documentation
  with no stale v1 limitation claims.
- **Changes 9-12:** verify source/binary packaging, install/uninstall,
  CMake/pkg-config discovery, runtime data, mapped libraries, headers, licenses,
  examples, and offline producer/consumer workflows.
- **Changes 13-16:** build signed/reproducible release-candidate archives for
  supported Linux/Windows toolchains and run clean-machine install, tutorial,
  artifact, plug-in, and compatibility smoke tests.
- **Changes 17-19:** resolve release-candidate defects, freeze changelog/known-
  issues/support matrices, complete inventories, and record exact artifact
  digests and restart evidence.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push the v2.0 release candidate once without hosted CI monitoring.

### Batch 175 - v2.0 final qualification and release

- **Changes 1-4:** audit every v2 priority and deferred-row disposition against
  executable evidence, confirming no required language, UVM, foreign-interface,
  standard-mode, SDF, FST, artifact, or platform gap remains.
- **Changes 5-8:** repeat clean exact-LLVM Linux Debug/Release, Windows hosted
  MSVC/clang-cl Debug/Release, interpreter/O0/O2/debug, sanitizer evidence from
  Batch 170, and all release-candidate smoke workflows.
- **Changes 9-12:** verify deterministic source and binary archives, SBOM/
  licenses, install layouts, ABI/schema versions, migrations, artifact digests,
  examples, and offline reproducibility.
- **Changes 13-16:** close any final release blockers transactionally and rerun
  every affected focused, full, portability, conformance, and packaging gate.
- **Changes 17-19:** freeze final docs/changelog/support policy, mark all v2
  roadmap items complete, prepare the exact release commit/tag notes, and record
  clean-context evidence.
- **Change 20:** run the final full Debug/Release and release gates, commit and
  push once, create and push annotated tag `v2.0.0`, verify the tag/artifacts,
  and declare v2 complete. This non-monitoring batch runs no new sanitizer.

## Forward priority order

1. **Completed in Batch 136:** read-only out-of-tree `.fsimlib` directory
   mappings.
2. **Completed in Batch 137:** explicit non-project compile, elaborate, and
   simulate artifact phases.
3. **Completed in Batch 138:** separate incremental SystemC compilation and
   linking.
4. **Batches 139-143 complete:** VHDL-2008/VITAL timing, primitive, path,
   memory, and vendor-model compatibility; Batch 163 closes the residual
   VHDL-2008/PSL digital surface.
5. **Batches 144-146 complete:** Verilog-2005 UDP, strength/switch, and specify
   timing; Batch 164 closes the residual current-standard surface.
6. **Batches 147-165:** SystemVerilog-2017 class, constraint, data, procedural,
   interface, assertion, coverage, DPI/VPI/VHPI, UVM, and residual language
   closure.
7. **Batches 166-167:** older VHDL, Verilog, and SystemVerilog standards.
8. **Batches 168-170:** SDF 4.0 with 2.1/3.0 input compatibility and complete
   Verilog/SystemVerilog/VHDL/VITAL/mixed annotation.
9. **Batch 171:** deterministic FST tracing for the complete trace model.
10. **Batches 172-175:** ABI/artifact migration freeze, cross-platform
    qualification, release candidate packaging/documentation, and v2.0 final
    release.
