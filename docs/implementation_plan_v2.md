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
2. **Pending.** Define the executable SystemVerilog-2017 constraint subset and
   normalize retained constraint blocks, property qualifiers, spans, and
   inherited ownership into canonical semantic HIR.
3. **Pending.** Resolve constrained properties, parameters, local variables,
   class selections, and method references against exact specializations before
   solver construction.
4. **Pending.** Compose base-to-derived constraint blocks with named block
   identity, override legality, `constraint_mode`, and deterministic inherited
   enable state.
5. **Pending.** Add checked per-object `rand` and `randc` state without exposing
   host pointers or collapsing declared signedness, width, enum, or handle
   profiles.
6. **Pending.** Derive deterministic simulation-, root-, object-, and call-local
   random streams from the project seed while preserving replay across engines
   and artifacts.
7. **Pending.** Add a resource-governed finite-domain bit-vector/integer/enum
   solver with explicit variable, clause, search, and elapsed-work budgets.
8. **Pending.** Lower equality, relational, arithmetic, logical, conditional,
   unary, and four-state legality constraints with exact width and signedness.
9. **Pending.** Implement `inside` ranges/sets, `dist` weights, and soft
   constraints with deterministic conflict and overflow handling.
10. **Pending.** Implement constraint implication, `if`/`else`, bounded
    `foreach`, and array/container element selection without arbitrary element
    limits.
11. **Pending.** Implement `solve ... before` ordering, dependency-cycle
    diagnostics, and stable declaration-independent solver ordering.
12. **Pending.** Execute object `randomize()` with optional variable lists and
    inline `with` constraints transactionally, returning zero without partial
    writes when no solution exists.
13. **Pending.** Execute bounded `std::randomize` over local integral, enum, and
    supported container values with the same solver and seed semantics.
14. **Pending.** Run inherited virtual `pre_randomize` and `post_randomize`
    callbacks with checked failure containment and post-hook execution only
    after successful assignment.
15. **Pending.** Implement property `rand_mode` and constraint-block
    `constraint_mode` query/update methods with per-object state and access
    validation.
16. **Pending.** Implement exact `randc` permutation cycles, reset/reseed rules,
    domain-change invalidation, and artifact-safe cycle state.
17. **Pending.** Add parse, resolution, unsupported-form, unsatisfiable,
    resource-budget, callback, stale/null, and solver-corruption negatives with
    cataloged diagnostics and transactional rollback.
18. **Pending.** Preserve constraint HIR, solver provenance, seeds, modes, and
    `randc` state through interpreter, LLVM O0/O2 service boundaries,
    debugger/callback/trace inspection, `.fsimobj`, `.fsimdesign`, mapped
    `.fsimlib`, relocation, and cold/warm/edit caches.
19. **Pending.** Add complete positive differentials and synchronize
    architecture, language support, diagnostics, feature matrix, inventories,
    UVM readiness boundaries, and restart evidence.
20. **Pending.** Run exact-LLVM Debug and Release plus source, catalog,
    inventory, installed-public-contract, Windows ABI, differential, and
    release gates after eight-worker builds, then commit and push once. Batch
    149 is not a CI boundary and runs no sanitizer or hosted CI-monitoring gate.

## Locked remaining v2 batch roadmap

The remaining v2 release sequence is Batches 150-175. Every batch has exactly
20 changes. The compact allocations below partition all 20 changes; before a
batch starts, its ranges are expanded into 20 individually numbered status
entries without changing scope or priority. For Batches 150-175, Changes 1-19
remain one recoverable accumulated worktree and Change 20 owns full gates,
documentation, one commit, and one push. Local builds use at least eight
workers. GitHub
Actions uses four. Only Batches 150, 160, and 170 run the LLVM-disabled
sanitizer immediately before commit and then monitor and repair every
non-documentation CI failure.

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

- **Changes 1-4:** complete `shortreal`, `real`, `realtime`, real/time literals,
  exact type propagation, constant folding, parameters, ports, and callable
  profiles.
- **Changes 5-8:** add deterministic IEEE-754 runtime storage, arithmetic,
  comparison, conversion, formatting, scheduling, debugger, callback, and
  trace behavior.
- **Changes 9-12:** implement `chandle`, `null`/equality/casts, host-safe opaque
  identity, Unicode code-point string semantics, and remaining standard string
  methods.
- **Changes 13-16:** close real/time/string/chandle aggregate, container,
  package, function/task, file-I/O, artifact, relocation, and cache behavior.
- **Changes 17-19:** add transactional negatives, cross-engine differentials,
  documentation, diagnostics, feature rows, inventories, and restart evidence.
- **Change 20:** run the monitoring-batch sanitizer, full Debug/Release and
  release gates, commit/push once, then inspect and repair all non-doc CI jobs.

### Batch 151 - Arbitrary-width packed values and aggregate closure

- **Changes 1-4:** replace the remaining executable 64-bit assumptions with
  resource-governed arbitrary-width two-/four-/nine-state scalar storage and
  exact signed/type metadata.
- **Changes 5-8:** close wide arithmetic, comparison, shifts, streaming,
  reductions, selectors, updates, casts, system functions, and unknown-state
  behavior across interpreter and LLVM.
- **Changes 9-12:** implement nested packed structs, unequal-width/tagged
  unions, anonymous enums/aggregates, member initializers, and complete nominal
  assignment/cast legality.
- **Changes 13-16:** preserve wide values through parameters, ports,
  functions/tasks, debugger/VCD, mixed boundaries, artifacts, relocation, and
  native caches without arbitrary length caps.
- **Changes 17-19:** add overflow/resource/corruption negatives, engine and
  boundary differentials, public docs, matrices, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 152 - Unpacked data, file/memory, and procedural closure

- **Changes 1-4:** close multidimensional unpacked arrays, subarray slices,
  unpacked aggregate members/unions, assignment patterns, queries, and
  recursive value-copy semantics.
- **Changes 5-8:** close string-element containers, string associative indices,
  cross-language aggregate/container values, standard descriptors,
  multichannel I/O, and multidimensional/string/aggregate memory files.
- **Changes 9-12:** implement runtime real/variable delays, general edge
  expressions, runtime-selected force/release, nonlocal/suspending references,
  and nested/nonintegral static locals/tasks.
- **Changes 13-16:** close simultaneous fork-site re-entry, process handles,
  mailboxes, semaphores, event/container ordering, shuffle, and scheduler
  lifetime/failure containment.
- **Changes 17-19:** add artifacts/caches/debug/trace coverage, negatives,
  differential fixtures, docs, inventories, and restart evidence.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 153 - Program, clocking, and interface closure

- **Changes 1-4:** implement program blocks, program instances, reactive-region
  scheduling, initialization/final behavior, and hierarchy/debug identities.
- **Changes 5-8:** implement clocking blocks, input/output skews, cycle delays,
  sampled/driven values, default clocking, and race-free scheduler integration.
- **Changes 9-12:** close virtual interfaces, interface arrays, interface
  classes, generic interface expressions, modport callables, clocking members,
  and parameterized interface typing.
- **Changes 13-16:** preserve these constructs through recursive/multiple-root
  mixed hierarchy, interpreter/LLVM, callbacks/traces, artifacts, relocation,
  and caches.
- **Changes 17-19:** add legality/race/resource negatives, full differentials,
  docs, diagnostics, matrices, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 154 - SystemVerilog concurrent assertion closure

- **Changes 1-4:** own and resolve sequence/property/checker declarations,
  formal arguments, local variables, clocks, disables, and hierarchical/package
  references.
- **Changes 5-8:** implement sequence concatenation/repetition, fusion,
  intersection, throughout/within, first-match, matched/triggered, and endpoint
  semantics.
- **Changes 9-12:** implement property implication, delay ranges, until/nexttime,
  always/eventually, strong/weak, accept/reject, abort, and vacuity rules.
- **Changes 13-16:** schedule concurrent assert/assume/cover/restrict, assertion
  controls, pass/fail actions, callbacks, debugger, trace, coverage counts, and
  multi-root behavior.
- **Changes 17-19:** add formal/type/clock/resource negatives, interpreter/LLVM
  and artifact differentials, documentation, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 155 - SystemVerilog functional coverage closure

- **Changes 1-4:** parse, own, specialize, and resolve covergroups, coverpoints,
  crosses, sampling events/methods, arguments, options, and per-instance state.
- **Changes 5-8:** implement automatic/default/explicit/illegal/ignore bins,
  ranges, wildcards, transitions, arrays, iff guards, and deterministic overlap
  rules.
- **Changes 9-12:** implement cross bins, binsof/intersect, weights, goals,
  at-least thresholds, merge/per-instance/type coverage, and standardized
  percentage calculation.
- **Changes 13-16:** expose callbacks, reports, debugger/trace inspection,
  save/restore, multiple roots, artifacts, relocation, caches, and API access.
- **Changes 17-19:** add malformed/resource/overflow negatives, deterministic
  differentials, docs, diagnostics, feature/inventory evidence, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 156 - DPI-C import/export closure

- **Changes 1-4:** parse and validate DPI imports/exports, names, pure/context
  qualifiers, functions/tasks, scopes, and exact C/SystemVerilog profiles.
- **Changes 5-8:** implement scalar, real, string, chandle, packed/unpacked,
  fixed/open-array, struct, enum, and in/out/inout/ref marshalling with checked
  lifetime and ownership.
- **Changes 9-12:** implement `svScope`, open-array accessors, disabled-state
  helpers, exported callbacks, suspending tasks, re-entry, exceptions, and
  scheduler containment.
- **Changes 13-16:** add portable plug-in discovery/build/link, symbols,
  artifacts, ABI/version checks, relocation, cache provenance, and Windows/POSIX
  calling-convention behavior.
- **Changes 17-19:** add C/C++ fixtures, malformed ABI/profile negatives,
  engine/multi-root differentials, docs, matrices, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 157 - IEEE VPI closure

- **Changes 1-4:** add a versioned VPI host ABI, plug-in loader/lifecycle,
  error model, object handles, iterators, names, hierarchy, and source metadata.
- **Changes 5-8:** implement type/property queries and value get/put for nets,
  variables, parameters, memories, arrays, classes, strengths, delays, and
  four-/nine-state values.
- **Changes 9-12:** implement time/delay APIs, callbacks for value/time/region/
  lifecycle events, callback removal, control operations, force/release, and
  scheduler-safe re-entry.
- **Changes 13-16:** implement system task/function registration,
  compiletf/sizetf/calltf, user data, MCD/vlog I/O, argv/product/version, and
  save/restart-safe behavior.
- **Changes 17-19:** add reference plug-ins, invalid/stale/reentrant negatives,
  cross-engine/artifact tests, docs, diagnostics, matrices, and inventories.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 158 - IEEE VHPI closure

- **Changes 1-4:** add a versioned VHPI host ABI, library/plug-in lifecycle,
  error model, handles, iterators, selected/indexed names, hierarchy, regions,
  and source metadata.
- **Changes 5-8:** implement type/constraint/subtype/object queries and scalar,
  enum, physical, access, array, record, file, protected, resolved, and
  nine-state value transfer.
- **Changes 9-12:** implement drivers/transactions, force/deposit/release,
  delays, time/phase callbacks, signal/process/lifecycle callbacks, and safe
  callback removal/re-entry.
- **Changes 13-16:** implement foreign subprogram/model registration, generic/
  port association, user data, assertions/output, multiple roots, artifacts,
  relocation, and cache/ABI provenance.
- **Changes 17-19:** add C fixtures, invalid/stale/profile/resource negatives,
  interpreter/LLVM/mixed differentials, docs, matrices, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 159 - UVM object, factory, configuration, and reporting foundation

- **Changes 1-4:** compile unmodified UVM 1.2 and UVM 2020-3.1 package/macro
  foundations and close any required class, macro, package, virtual-interface,
  process, and DPI compatibility gaps without vendoring the standard library.
- **Changes 5-8:** execute `uvm_object`/`uvm_component` construction, hierarchy,
  type/object wrappers, registry macros, factory type/instance overrides, and
  deterministic factory diagnostics.
- **Changes 9-12:** implement config/resource pools, precedence, wildcard scope,
  typed get/set, command-line settings, callbacks, and multi-root isolation or
  sharing according to UVM rules.
- **Changes 13-16:** implement report objects/servers/handlers/catchers, actions,
  verbosity, IDs, files, summaries, quit counts, and deterministic formatting.
- **Changes 17-19:** add standard examples and negative matrices across all
  engines/artifacts, UVM-version docs, feature evidence, inventories, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

### Batch 160 - UVM phases, objections, and TLM - CI monitoring boundary

- **Changes 1-4:** implement common/domain phase graphs, build/connect/end-of-
  elaboration/start-of-simulation/run/extract/check/report/final ordering,
  jumps, synchronization, and custom phases.
- **Changes 5-8:** implement objections, drain time, all-dropped callbacks,
  phase-ready/end hooks, task cancellation, process lifetime, and scheduler
  quiescence across multiple roots.
- **Changes 9-12:** implement TLM1/TLM2 ports/exports/imps, blocking/nonblocking
  transport, analysis ports/FIFOs, sockets, payloads, phases, and connection
  validation.
- **Changes 13-16:** integrate phase/TLM state with debugger, callbacks, traces,
  DPI/VPI, artifacts, relocation, deterministic replay, and cache provenance.
- **Changes 17-19:** add UVM phase/TLM examples, race/deadlock/connection
  negatives, cross-engine differentials, docs, matrices, inventories, and handoff.
- **Change 20:** run the monitoring-batch sanitizer, full Debug/Release and
  release gates, commit/push once, then inspect and repair all non-doc CI jobs.

### Batch 161 - UVM sequences, callbacks, and register model

- **Changes 1-4:** implement sequence items/sequences, sequencer arbitration,
  locks/grabs, priorities, relevance, response queues, macros, and deterministic
  randomization integration.
- **Changes 5-8:** implement drivers, monitors, agents, scoreboards,
  sequence-driver handshakes, virtual sequences/sequencers, callbacks, and
  transaction recording.
- **Changes 9-12:** implement UVM register blocks/maps/registers/fields/memories,
  adapters, predictors, frontdoor/backdoor access, mirrors, reset, and rights.
- **Changes 13-16:** implement register sequences, HDL paths through VPI/VHPI,
  coverage, byte enables, endianness, multiple maps, callbacks, and debugger
  inspection.
- **Changes 17-19:** add representative sequence and register environments,
  arbitration/model negatives, engine/artifact parity, docs, evidence, and handoff.
- **Change 20:** run full non-sanitized Debug/Release and release gates, then
  commit and push once without hosted CI monitoring.

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
