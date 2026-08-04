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

## Batch 135 - multiple top-level roots - Planned

1. **Planned.** Establish this exact 20-change Batch 135 plan and restart
   status before implementation begins.
2. **Planned.** Replace the singular project top setting with an ordered list
   of top specifications while migrating existing schema-2 manifests.
3. **Planned.** Add stable user-selected aliases for top roots and reject
   empty, duplicate, or hierarchy-unsafe aliases.
4. **Planned.** Add repeatable CLI top specifications with explicit aliases,
   preserving the manifest list unless the command line supplies replacements.
5. **Planned.** Extend the public build and elaboration APIs with immutable
   ordered root requests while retaining source compatibility for one root.
6. **Planned.** Resolve every root independently through the Batch 133/134
   language and library resolver contracts.
7. **Planned.** Diagnose missing, ambiguous, or duplicate root selections
   transactionally before constructing any hierarchy.
8. **Planned.** Elaborate all roots into one design with alias-prefixed
   hierarchy paths and no implicit connectivity between roots.
9. **Planned.** Share one scheduler and simulation time domain across all HDL
   and SystemC roots.
10. **Planned.** Share language library, package, configuration, and global
    signaling state across roots while preserving per-instance local state.
11. **Planned.** Make initialization, delta cycles, time advancement, and
    shutdown deterministic across the ordered root list.
12. **Planned.** Admit cross-root references only through language-defined
    global mechanisms and diagnose unsupported hierarchical shortcuts.
13. **Planned.** Merge all roots into one trace namespace rooted at their
    aliases, with deterministic VCD name and identifier allocation.
14. **Planned.** Expose every root in one debugger session with unambiguous
    aliased paths, breakpoints, stepping, inspection, and callbacks.
15. **Planned.** Execute multiple SystemC roots and mixed HDL/SystemC roots in
    one kernel without duplicate lifecycle callbacks or plug-in initialization.
16. **Planned.** Include the ordered aliased root set and each selected
    canonical identity in elaboration, specialization, and native-cache
    provenance.
17. **Planned.** Preserve single-root output, diagnostics, cache behavior, and
    C/C++ API compatibility as the degenerate multiple-root case.
18. **Planned.** Add positive execution coverage for HDL-only, SystemC-only,
    mixed-language, global-signaling, trace, debugger, callback, and cache
    scenarios under the interpreter and LLVM O0/O2.
19. **Planned.** Add negative and migration coverage for aliases, duplicate
    roots, partial resolution failure, unsupported references, and old
    single-top manifests, and update public documentation and examples.
20. **Planned.** Update public/release records, run exact-LLVM Debug and Release
    plus source/catalog/inventory/release gates, then commit and push once.
    Batch 135 is not a CI boundary and runs no sanitizer gate.

## Forward priority order

1. Batch 135: multiple aliased top-level roots in one simulation.
2. Read-only out-of-tree `.fsimlib` directory mappings.
3. Explicit non-project compile, elaborate, and simulate artifact phases.
4. Separate incremental SystemC compilation and linking.
5. Complete VHDL-2008/VITAL, Verilog-2005, SystemVerilog-2017 classes/UVM,
   VPI, DPI, and VHPI.
6. Older VHDL, Verilog, and SystemVerilog standard modes.
7. Full SDF annotation with 2.1/3.0 compatibility and VITAL integration.
8. FST tracing for every value exposed through the trace model.
9. Cross-platform artifact, migration, conformance, debugger, trace, and
    documentation closure before declaring v2.0.
