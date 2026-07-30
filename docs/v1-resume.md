<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v1 resume handoff

This is the short entry point for resuming v1 implementation. The
[implementation plan](implementation-plan.md) remains the chronological record,
and the [feature matrix](feature-matrix.md) remains the release authority.

## Snapshot

- Recorded: 2026-07-30.
- Branch: `codex/resumable-jit`.
- Implementation baseline: completed feature-batch-65 cadence checkpoint; the
  preceding batches 54–64 checkpoint is `31d6def`.
- The source-size refactor is complete: all 263 authored C/C++ source, header,
  and test files are at or below the 2,000-line hard limit; the allowlist is
  empty and the maximum is 1,987 lines.
- The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 54
  configured tests in 319.51 seconds, and Release passed all 55 configured
  tests in 132.56 seconds on 2026-07-30.
- The diagnostic catalog covers all 1,071 production codes.
- No CI state was inspected during feature batch 65 or this handoff.

The repository is a substantial pre-alpha executable simulator, not fsim v1.
Many language families have strong bounded evidence, but every broad v1
contract row is release-blocking until it has complete positive, negative,
elaboration, interpreter, and JIT evidence.

## Progress by milestone

| Milestone | Status | Current result |
|---|---|---|
| 1. Platform and semantic spine | In progress | Cross-platform C++20/CMake foundation, exact LLVM adapter, dependencies, diagnostics, manifest, native ABIs, Tcl, and CI definitions exist. Unicode/path and remaining console-interrupt validation are open. |
| 2. Internal vertical slice | In progress, near architecture gate | Interpreter, hybrid LLVM JIT, cache, deterministic scheduler, mixed hierarchy, VCD, debugger, and examples execute. Bounded typed 1–64-bit SystemVerilog constants, same-language type parameters, immutable string parameters, automatic integral functions and suspending tasks, full unsigned-64 values, and entity-level VHDL-2008 unclassified interface type, bounded interface function/procedure/package generics, generic subprogram templates/instances, recursive block/generate configurations and references, and scoped/package-visible overloaded scalar/vector and composite component declarations with deterministic default binding now have interpreter/O0/O2/cache evidence; wider/complete typing, runtime strings, remaining VHDL hierarchy/generic semantics, source metadata, and complete differential coverage still block the gate. |
| 3. Near-full synthesizable frontends | Pending | Broad bounded VHDL and SV execution exists, but the explicit language-specific typed HIR/DesignIR layering and full promised language semantics are incomplete. |
| 4. Procedural testbenches, SystemC, visibility | In progress | Extensive procedural, SystemC, C API, debugger, Tcl, and trace slices execute. Dynamic testbench data, files, fork/event completeness, richer SystemC behavior, and remaining API metadata are open. |
| 5. Release hardening | In progress | Cache hardening, diagnostics, sanitizer/fuzz jobs, cross-platform workflows, install smoke tests, and normalized traces exist. Full platform closure, benchmarks, imported tests/packages, and all matrix rows remain open. |

## Implemented foundation

The following capabilities have executable evidence and should be extended,
not rebuilt:

- handwritten VHDL and Verilog/SystemVerilog preprocessing, parsing, semantic
  checks, source spans, macro ancestry, and stable diagnostics;
- a common elaborated hierarchy with stable instance, process, signal, driver,
  scope, source, and specialization identities;
- typed SimIR, a deterministic reference interpreter, and a single-thread
  active/inactive/update/postponed scheduler with delta-oscillation diagnosis;
- bounded SystemVerilog module/package functions with explicit automatic
  activation frames, constant evaluation, package visibility, nested
  nonrecursive runtime calls, SimIR call/return control, debugger safe points,
  and transitive native-cache provenance;
- bounded SystemVerilog module/package tasks with explicit automatic
  activation frames, parameter-sized integral input/output/inout formals,
  deterministic deferred copy-in/copy-out, nested function/task calls,
  delays, named-event/condition waits, valueless early return after
  suspension, package visibility/time context, lifecycle and recursion
  diagnostics, debugger locals across stop/resume, and
  interpreter/LLVM O0/O2/cache equivalence;
- entity-level VHDL-2008 unclassified interface type generics with
  same-language constrained subtype-indication resolution over supported
  vectors, portable integers, nominal enumerations, records, and
  one-dimensional arrays; nested forwarding, dependent object specialization,
  and canonical cache identities;
- bounded VHDL-2008 interface function generics with retained pure scalar
  profiles, required/named/box defaults, local and directly visible package
  actuals with matching package bodies, constant/default folding, nested
  forwarding, specialization-local binding, debugger call points, and
  interpreter/LLVM O0/O2/cache equivalence;
- bounded VHDL-2008 interface procedure generics with retained
  constant/variable scalar profiles and `in`/`out`/`inout` modes,
  required/named/box defaults, matching local/package actuals, nested
  forwarding and procedure calls, deterministic ordered copy-in/copy-out,
  debugger call points and live formals/locals, and
  interpreter/LLVM O0/O2/cache equivalence;
- bounded VHDL-2008 interface package generics and entity/architecture-local
  generic package instances over existing value/type/function/procedure
  families, with explicit/default/box maps, declaration-ordered
  specialization, selected constants/types/subprograms, nested forwarding,
  exact instance/source identity, debugger-visible procedure frames, and
  interpreter/LLVM O0/O2/cache equivalence;
- bounded VHDL-2008 generic function/procedure templates and local or
  package-visible instantiations over existing value/type/function/procedure
  families, with explicit/default/box maps, declaration/body conformance,
  dependent helper specialization, nested interface-subprogram forwarding,
  canonical transitive identity, debugger call metadata, and
  interpreter/LLVM O0/O2/cache equivalence;
- bounded VHDL-2008 configuration declarations and architecture declarative
  configuration specifications over component-style instances, with recursive
  static-block and selected for/if/case-generate rules, explicit
  label/`all`/`others` selection, entity/configuration/open aspects, nearest-
  scope precedence, named generic/port-map composition, configuration-top
  selection, referenced-subtree activation, direct-entity isolation, canonical
  transitive source identity, and interpreter/LLVM O0/O2/debugger/cache
  equivalence;
- bounded VHDL-2008 component declarations in architecture, entity, package,
  block, and selected-generate regions with retained value-generic and
  scalar/vector, enumeration, named-subtype, non-nested-record, and
  one-dimensional scalar-element-array port profiles; nearest lexical/package
  visibility without package-use re-export; overload selection by
  association/mode/nominal type/constraint/direction/dependent width;
  named/positional formal normalization; latest-analyzed compatible
  same-library default binding; configuration-map precedence; version-3
  scope/source/type/target identity; and interpreter/LLVM
  O0/O2/debugger/cache equivalence;
- packed Bit2, Logic4, and exact Logic9 values, wide-value runtime kernels,
  process-owned drivers, standard resolution, delayed/projected transactions,
  NBA/update writes, dynamic packed indexing, and committed-change visibility;
- LLVM 22.1.8 ORC/LLJIT execution at O2 for `run` and O0 for `debug`, explicit
  resumable process frames, per-process fallback, specialization modules, and
  a persistent checksummed native-object cache;
- explicit recursive VHDL, Verilog/SystemVerilog, and SystemC hierarchy in
  every parent/child direction, including bounded immutable construction
  actuals and explicit boundary resolvers;
- VCD, CLI debugger, safe points, statement/process/delta/time stepping,
  breakpoints, locals, hierarchy navigation, deposit/force/release, callbacks,
  and Ctrl-C integration;
- the versioned native C API and SystemC plug-in ABI;
- the fsim SystemC facade, compiled support library, cached plug-in compiler,
  factories, hierarchy, ports/exports/signals/events, methods, Boost.Context
  threads/cthreads, lifecycle, waits, notifications, and channel updates;
- schema-1 project loading and `check`, `build`, `run`, `debug`, and `tcl`
  command paths; and
- interactive and batch Tcl 9.0.4 over the common project, runtime, debugger,
  trace, diagnostic, and callback model.

The detailed bounded language coverage is recorded in
[language support](language-support.md). Do not infer support beyond that
document merely because the parser accepts a related form.

## Remaining v1 release blockers

### 1. Close the architecture gate

- Complete VHDL generic semantics beyond the implemented entity-level
  VHDL-2008 unclassified interface type, constrained-type-actual, and bounded
  interface-function/procedure/package and generic-subprogram slices,
  including general or nested generic package units, nested/composite generic
  subprograms, dependent type declarations, and broader unconstrained-object
  cases.
- Complete SystemVerilog runtime mutable strings, type actuals beyond the
  bounded same-language packed subset, widths above 64 bits,
  genvar-dependent typed constants, and remaining LRM expression typing.
- Retain source/debug metadata for every remaining executable construct.
- Make the semantic differential harness compare interpreter, O0, and O2
  final state, assertions, scheduling observations, failures, and normalized
  trace events for every supported semantic fixture.
- Extend mixed-language evidence to O0, assertion failures, broader trace
  behavior, and Windows LLVM 22.1.8.
- Finish remaining generated forms, including guarded VHDL blocks,
  noncanonical SV loop updates, nonintegral VHDL choices, and additional
  declarative/module items.

### 2. Establish the final semantic architecture

- Separate the compact common frontend representation into explicit typed
  VHDL HIR and SystemVerilog HIR.
- Make elaborated DesignIR specialization, constant evaluation, legality, and
  source/debug metadata explicit rather than relying on bounded frontend
  structures.
- Preserve dense stable IDs and native-cache identity through that migration.
- Keep the SimIR interpreter as the semantic oracle throughout the change.

### 3. Complete VHDL-2008 v1 scope

- Complete configuration semantics beyond the bounded declaration/specification
  slice; full libraries, packages/bodies, and contexts; generics; component
  non-value generics; incremental configurations and complete library analysis
  order; direct instantiation; blocks and generates.
- Finish name/overload resolution, constant evaluation, legality, resolution
  functions, complete synthesizable statements, and full promised composite
  type/aggregate/attribute behavior.
- Finish waits, assertions/reports, files, and TextIO.
- Close physical-time and inertial/transport/reject semantics beyond the
  implemented bounded forms.
- Review, license, bundle, and test the Apache-2.0 IEEE logic, numeric, bit,
  fixed, and floating-point packages.

### 4. Complete Verilog-2005/SystemVerilog-2017 v1 scope

- Finish directive semantics, parameters, generates, hierarchy, and
  specialization.
- Add interfaces/modports and complete packages.
- Complete packed/unpacked types, nested aggregates, memories, remaining
  function forms, tasks, all `always` forms, expressions, gates, and
  assignment semantics.
- Complete general delays/events, fork/join, named events, and NBA/delta
  matrices.
- Complete mutable strings and add files, dynamic/associative arrays, queues,
  and `$readmem*`.

### 5. Complete mixed-language and SystemC semantics

- Finish general ascending/descending ordinal vector mapping and portable
  Boolean/integer boundary conversions.
- Close every state-domain, signedness, width, resolver, multi-driver,
  construction-parameter, delay, delta, and NBA direction matrix.
- Broaden SystemC named hierarchy, sensitivity/list/error coverage, port-chain
  policies, standard channel behavior, compiler/cache fingerprints, and
  Windows thread/plugin evidence.
- Complete remaining native API object kinds and richer canonical value
  metadata.

### 6. Release hardening

- Finish Windows Unicode path, Ctrl-C, DLL, plug-in compiler, and cache tests.
- Expand sanitizer and fuzz corpora through complete frontends and serialized
  SimIR.
- Add benchmark runners for cold/warm build, events per second, memory, wide
  values, resolution, crossings, trace overhead, and debug overhead.
- Import only license-reviewed external tests and preserve their notices.
- Pass every required feature-matrix row on Ubuntu/GCC and Windows/MSVC in
  Debug and Release, with LLVM 22.1.8 where required.

Python automation remains explicitly post-v1. It must later wrap the same
opaque native session/object model already used by Tcl.

## Next ten-feature batch

Resume with **feature batch 66: bounded VHDL-2008 non-value component
generics**:

1. Retain component generic kinds and profiles for the existing interface
   type, function, procedure, and package families.
2. Parse bounded type/function/procedure/package generics in component
   declarations across the existing declarative regions.
3. Resolve each formal in declaration order so dependent component ports can
   reference earlier non-value generic formals.
4. Normalize explicit, omitted-default, and box actuals against the selected
   component declaration before target binding.
5. Match same-language component and entity non-value generic profiles by
   kind, callable/package profile, defaults, and formal position.
6. Select equally visible component overloads after specializing dependent
   ports through their normalized non-value generic maps.
7. Compose architecture specifications and recursive configuration maps
   through non-value component formals while preserving direct-entity
   isolation.
8. Include canonical non-value formal profiles, selected actual identities,
   dependencies, and target mappings in version-4 component identity.
9. Diagnose missing/ambiguous/wrong-kind actuals, incompatible profiles,
   illegal defaults/boxes/maps, recursion, and implicit cross-language
   binding.
10. Add frontend, negative, elaboration, visibility/overload/configuration,
    runtime, debugger, interpreter/LLVM O0/O2, cold/warm, and edited-source
    selective-cache evidence, then run the scheduled gate.

Keep this batch to the existing bounded same-language interface type,
function, time-free procedure, and package generic families. General
composite expression actuals, new generic families, nested package/template
forms, incremental configurations, mixed-language component binding, and
general overload resolution remain separate release-gate work.

## Working cadence

- Implement ten related features before the next full regression.
- Use focused warnings-as-errors builds and targeted tests after each coherent
  change; do not run the full suite for every individual feature.
- At the tenth feature, run the exact LLVM 22.1.8 Debug and Release regression
  appropriate to the batch, update the plan/support/matrix documents, commit,
  and push the branch.
- Do not inspect GitHub Actions runs unless explicitly requested.
- Preserve the 2,000-line hard limit, prefer approximately 1,600 lines, split
  compilation units by responsibility, and do not move executable
  implementation into `_internal.hpp` files.
- Keep Windows portability in every implementation decision: use secure CRT
  or compatibility wrappers for environment access, avoid POSIX-only path and
  process assumptions, keep the native ABI plain C, and test MSVC/clang-cl
  behavior in the scheduled platform gate.

## Resume commands

Start by confirming that no newer implementation supersedes this handoff:

```sh
git status --short --branch
git log -5 --oneline --decorate
```

For a clean-context restart:

1. Use `docs/v1-resume.md` as the entry point, `docs/feature-matrix.md` as the
   release authority, and `docs/implementation-plan.md` only when historical
   detail is needed.
2. Confirm the branch is `codex/resumable-jit` and history contains
   `feat: add VHDL composite component profiles`.
3. Begin batch 66 at item 1 above. Do not rerun batch 65's full regression
   unless a later change can
   affect component/configuration binding, hierarchy, cache identity, or
   runtime behavior.
4. Keep batch-66 work within bounded VHDL-2008 non-value component generics.
   Record intentional scope changes in this handoff before implementation.
5. Use targeted tests during that batch, run the full Debug and Release gates
   after all ten features, then update the four documents named above, commit,
   and push.

The existing exact-LLVM build trees on the recorded development host are:

```sh
cmake --build build/llvm22-ninja-debug --parallel 8
cmake --build build/llvm22-ninja-release --parallel 8
```

Use a narrow test expression while batch 66 is in progress, for example:

```sh
ctest --test-dir build/llvm22-ninja-debug --output-on-failure \
  -R 'fsim\.(frontend|elaboration|llvm|application|source-line-budget)'
```

Both exact-LLVM build trees were rebuilt for feature batch 65. The configured
test counts differ because the Release tree includes the fetched-Tcl
relocation test; both recorded inventories are clean.

For a fresh checkout, configure exact LLVM explicitly:

```sh
cmake -S . -B build/llvm22-ninja-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFSIM_BUILD_TESTS=ON \
  -DFSIM_LLVM_MODE=ON \
  -DFSIM_WARNINGS_AS_ERRORS=ON \
  -DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm
```

Before declaring any row complete, consult:

- [implementation plan and progress](implementation-plan.md);
- [feature matrix and release evidence](feature-matrix.md);
- [language support](language-support.md);
- [cross-language semantic contract](cross-language-semantics.md); and
- [SystemC subset and plug-in model](systemc-subset.md).
