<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v2 implementation plan

This is the authoritative batch and status record for fsim v2. The v1 release
is preserved by annotated tag `v1.0.0` at `6450599`; v2 development starts on
`codex/v2` from post-v1 checkpoint `1462f18`.

## Working cadence

- Every v2 implementation batch contains exactly 20 numbered changes.
- Changes 1 through 19 accumulate in one recoverable worktree with focused
  warnings-as-errors builds and targeted tests.
- Change 20 owns the full exact-LLVM Debug and Release regressions,
  documentation, one commit, and one push.
- Local builds use at least eight workers.
- Sanitizers run only immediately before committing a scheduled CI-monitoring
  batch. Ordinary batches do not configure, build, or run sanitizer targets.
- Every tenth numbered batch is a non-documentation CI-monitoring boundary;
  Batch 140 is the next boundary. Documentation-only runs are not monitored.

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

## Batch 140 - VITAL package types and combinational primitives - In progress

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
20. **In progress.** Run the LLVM-disabled ASan/UBSan regression immediately
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
    14-test cross-platform repair gate in both configurations. Repair
    commit/push and a fully green replacement matrix remain current.

## Forward priority order

1. **Completed in Batch 136:** read-only out-of-tree `.fsimlib` directory
   mappings.
2. **Completed in Batch 137:** explicit non-project compile, elaborate, and
   simulate artifact phases.
3. **Completed in Batch 138:** separate incremental SystemC compilation and
   linking.
4. **In progress:** complete VHDL-2008/VITAL, followed by Verilog-2005,
   SystemVerilog-2017 classes/UVM, VPI, DPI, and VHPI. Batch 139 supplied the
   signal timing/driver substrate; Batch 140 owns VITAL public types and
   combinational functions, Batch 141 owns timing checks/state tables, Batch
   142 owns path/wire delays and pulse rejection, and Batch 143 owns VITAL
   memory models plus vendor-library compatibility closure.
5. Older VHDL, Verilog, and SystemVerilog standard modes.
6. Full SDF annotation with 2.1/3.0 compatibility and VITAL integration.
7. FST tracing for every value exposed through the trace model.
8. Cross-platform artifact, migration, conformance, debugger, trace, and
    documentation closure before declaring v2.0.
