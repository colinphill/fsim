<!-- SPDX-License-Identifier: Apache-2.0 -->
# fsim v1 resume handoff

This is the short entry point for resuming v1 implementation. The
[implementation plan](implementation-plan.md) remains the chronological record,
and the [feature matrix](feature-matrix.md) remains the release authority.

## Snapshot

- Recorded: 2026-08-01.
- Branch: `codex/resumable-jit`.
- Implementation baseline: pushed feature-batch-111 VHDL analysis-order,
  package/body context, transitive callable provenance, and recursive
  configuration-binding closure at commit `fdafa67`, plus an intentionally
  dirty Batch 112 ordinary-callable overload-resolution checkpoint described
  below. Do not reset the worktree to the pushed commit.
- The source-size refactor is complete: all 336 authored C/C++ source, header,
  and test files are at or below the 2,000-line hard limit; the allowlist is
  empty and the maximum is 2,000 lines.
- The post-Batch-110 Debug-footprint repair partitions the 115-alternative
  SimIR `Operation` into eight semantic storage groups while retaining flat
  construction, query, and visitation helpers. Five large visitors now use a
  single typed dispatch body rather than a 115-lambda overload set, and the
  application inventory is linked into four selector-driven hosts while every
  named CTest remains a separate process. GCC Debug uses `-Og` and compressed
  DWARF; Ninja link/archive concurrency remains eight.
- Exact single-action measurements after the partition reduced
  `lowerer_process.cpp` from 5,003,456 KiB to 911,748 KiB peak compilation
  memory, `runtime_execution_tests.cpp` from 7,789,344 KiB to 954,596 KiB,
  and a representative GNU ld.bfd application-host link from 5,309,296 KiB
  to 1,398,856 KiB. The completed Debug tree is 3,748,237,353 bytes; its four
  application hosts total 338,591,400 bytes and the elaboration archive is
  230,510,732 bytes.
- After an optimized-only nested-variant temporary-lifetime failure was
  repaired with in-place group construction, the exact LLVM 22.1.8 Debug and
  Release regressions passed 64/64 in 114.51 and 95.40 seconds. Scoped locals
  completed in 1.07/1.17 seconds, files in 0.95/0.87 seconds, and containers
  in 114.51/95.40 seconds. Focused LLVM-disabled ASan/UBSan elaboration and
  file-application tests also pass after repairing a fork-parent reference
  invalidated by dynamic-process vector growth.
- The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 64
  configured tests in 485.70 seconds, and Release passed all 64 configured
  tests in 159.27 seconds on 2026-08-01. Debug/Release scoped locals completed
  in 0.92/0.79 seconds, LLVM in 3.10/2.71 seconds, mutable strings in 0.94/0.41
  seconds, files in 2.11/0.68 seconds, containers in 375.83/99.00 seconds, and
  the monolithic application in 40.66/14.10 seconds. Debug aggregate/
  multidimensional execution completed in 2.03 seconds.
- The diagnostic catalog covers all 1,415 production codes.
- Batch 111 requires deterministic VHDL semantic analysis in manifest order
  after parallel parsing. Architectures, package bodies, use/context clauses,
  configuration declarations, and explicit entity/configuration bindings now
  require their dependencies to have been analyzed earlier in the selected
  library. Eight stable order diagnostics cover forward dependencies. The
  package-body audit also fixed body-only context expansion and carries its
  transitive sources through exported callable provenance and selective native
  cache invalidation. Exact LLVM 22.1.8 Debug and Release passed 65/65 tests in
  112.30/97.27 seconds; analysis order took 0.02 seconds in both and scoped
  locals remained 0.98/1.03 seconds. Focused LLVM-disabled ASan/UBSan tests
  also pass. Batch 111 is not a CI boundary, but run `30712751618` was
  inspected after an explicit request and passed all 12 jobs. The final
  Windows/MSVC/LLVM Debug job completed in 25 minutes 55 seconds. The coarse
  Actions build step made the Windows jobs appear hung, but every configuration
  advanced and completed within the established timing envelope.
- Batch 110 and its mandatory non-documentation CI boundary are closed.
  Initial run `30709270776` showed Windows' default 1 MiB test-executable
  stack was no longer sufficient after the operation/test-host refactor:
  MSVC configurations reported early access violations or stopped making
  progress in later monolithic tests. `scoped_locals` itself remained healthy
  at 0.28 seconds. Repair commit `a5d69e0` centralizes an 8 MiB stack reserve
  for every MSVC-compatible Windows test host and bounds the API and Windows
  application tests. Replacement run `30710421676` passed all 12 jobs.
  Windows MSVC Debug passed 63/63 in 359.78 seconds, including container
  elaboration in 0.32 seconds, scoped locals in 0.25 seconds, and the API in
  0.29 seconds. MSVC LLVM Debug passed 64/64 in 1,092.19 seconds, and
  ASan/UBSan passed 63/63 in 516.76 seconds.
- Batch 112 is in progress: complete VHDL name and overload resolution,
  visibility, constant evaluation, legality, and resolution functions. The
  current dirty checkpoint replaces one-name/one-index function and procedure
  maps with ordered overload sets, preserves same-designator package imports
  and qualified package materialization by declaration identity, adds
  contextual result/actual-profile selection in the new
  `src/elaboration/lowerer_overloads.cpp`, diagnoses duplicate and ambiguous
  callable profiles, and conservatively follows every overload body for
  wildcard sensitivity. The exact LLVM Debug elaboration target builds with
  eight workers and the pre-existing `fsim.elaboration` test passes in 0.11
  seconds after preserving its targeted non-writable-procedure diagnostic.
  The focused overload fixture now passes local and wildcard-package integer/
  Boolean function and procedure selection plus no-match, ambiguity, and
  duplicate-profile diagnostics. The integer-family precheck recognizes
  integer-returning overload candidates, and the positive fixture keeps
  function and procedure outputs independently driven. The synchronized
  diagnostic catalog covers 1,419 production codes. Two-/three-part
  direct-selected package callables now materialize with package/body source
  provenance. The new application differential passes in 0.32 seconds for the
  interpreter, LLVM O0/O2 cold/warm cache reuse, and an edited package body
  that changes both results and the specialization key. All 339 authored
  sources pass the 2,000-line gate. Broader contextual name/type resolution,
  constant evaluation, legality, and resolution-function slices remain
  pending. Keep this paragraph and the ten-task checklist below current as
  those milestones land.
- The Batch 100 boundary is closed at repair commit `d2f216e`. Initial run
  `30640792808` and replacements `30647570353`/`30651503050` passed every
  ordinary job but exposed exact 1,500.27/1,800.14/2,400.12-second sanitizer
  container timeouts. LLVM-disabled builds now retain one complete container
  semantic/debugger pass without redundant compiled/O2 aliases, while LLVM
  builds retain the full interpreter/JIT O0/O2 matrix. Final run `30656887493`
  passed all 12 jobs. ASan/UBSan passed 58/58 in 876.53 seconds with containers
  in 588.70 seconds; Windows MSVC Debug passed 58/58 in 336.76 seconds with
  scoped locals in 0.33 seconds. GitHub builds now use parallelism four after
  the operation-storage footprint repair reduced compiler and linker memory.
- The feature-batch-70 boundary inspection found remote CI run
  `30542845249` failing non-LLVM GCC Debug, Release, and ASan/UBSan because
  LLVM-only cache-key test helpers were unguarded. The first repair guarded
  two helpers and initially raised every workflow build from two to eight
  workers;
  replacement run `30553184827` exposed four more helpers with the same
  non-LLVM warning. All six are now guarded. The next replacement run
  `30553851223` reached the full Windows tests and exposed host newline
  translation in SystemVerilog file streams plus Debug frontend stack
  exhaustion. Binary stream transport and an 8 MiB Windows Debug stack for
  the accumulated frontend matrix were repaired, and workflow builds returned
  to two workers to avoid hosted Ubuntu memory pressure. The later operation-
  storage footprint repair permits the current four-worker setting.
  Replacement run
  `30555745832` passed all 12 Linux and Windows jobs.
- The Batch 80 boundary inspection found that Batch 72 had embedded four
  memory-load expressions in every recursive statement and a two-expression
  static range in every type. The resulting MSVC Debug parser frames exhausted
  Windows' default stack while parsing nested scoped-local assignments.
  Memory-load lowering now reuses the statement's existing value, target, and
  task-argument slots, and static ranges use vector-backed zero-or-one storage
  to retain deep value-copy semantics without the inline footprint.
- Repair run `30590422725` proved `fsim.application.scoped_locals` fixed in
  0.28 seconds, then exposed the same accumulated-frame pressure in the
  monolithic application fixture. The scoped regression retains a 60-second
  bound, the MSVC Debug application regression retains a 600-second bound and
  phase checkpoints, and its accumulated fixture receives an 8 MiB Debug
  stack. Ordinary CI jobs retain a 45-minute budget because two-worker Windows
  builds legitimately exceed 25 minutes; the Batch 100 sanitizer investigation
  raised only its job budget to 70 minutes.
- Final non-documentation run `30594329846` passed all 12 Linux and Windows
  jobs. Windows MSVC Debug passed scoped locals in 0.36 seconds, SV containers
  in 196.86 seconds, the monolithic application test in 80.55 seconds, and all
  58 configured tests; the job completed in 16 minutes 26 seconds. ASan/UBSan
  passed in 30 minutes 58 seconds. The Batch 80 feature and repair commits are
  `ba1f6da`, `909cedc`, `f3d6db8`, `78c816f`, and `0934482`.
- Batch 81 adds one optional source-spanned locator iterator binder, exact
  lexical collision/leakage checks, and direct signed 32-bit `.index`
  predicate leaves. Static arrays use signed declared indices; dynamic arrays
  and queues use current zero-based indices. Typed predicate value kinds,
  schema-34 cache identity, interpreter/LLVM O0/O2, generated hierarchy,
  bounded queues, and suspended tasks are covered without changing the public
  native ABI. Batch 81 is not a CI-inspection boundary, so no Actions run was
  inspected.
- Batch 82 adds optional source-spanned `with` transformations to all five
  unpacked-container reductions. A bounded typed 64-node graph binds implicit
  `item` and signed-32 `item.index`, admits pure comparisons/logical
  composition and one element-typed conditional mask, preserves empty and
  four-state semantics, and executes through the shared interpreter/native
  kernel. Schema 35 records exact graph structure and constants without a
  public ABI change. Batch 82 is not a CI-inspection boundary, so no Actions
  run is inspected.
- Batch 83 adds optional source-spanned `with` keys to `sort` and `rsort`.
  Implicit `item` or one named iterator and signed-32 `.index` lower through
  the shared bounded typed graph. Keys are computed once per original element
  before stable ascending/descending four-state ordering, preserving equal-key
  order, object/port/callable coherence, and suspension behavior. Validated
  schema 36 records key presence, structure, constants, index typing, and
  ordering mode without a public ABI change. Batch 83 is not a
  CI-inspection boundary, so no Actions run is inspected.
- Batch 84 adds optional source-spanned `with` transformations to `min`, `max`,
  `unique`, and `unique_index`. Implicit `item` or one named iterator and
  signed-32 `.index` lower through a locator-specific use of the shared
  bounded typed graph. Keys are computed once per original element before
  extrema or uniqueness selection, while results retain the first original
  element or signed original index. Validated schema 37 records transform
  presence, structure, constants, index typing, and locator mode without a
  public ABI change. Batch 84 is not a CI-inspection boundary, so no Actions
  run was inspected.
- Batch 85 adds one optional source-spanned named iterator to all five
  reduction transformations. Exact lexical scope, implicit-net suppression,
  collision/leakage rejection, receiver-typed values, and direct signed-32
  `.index` reuse the existing graph. Implicit and named spellings lower to
  identical semantic metadata, so schema 37 and the public ABI remain
  unchanged while interpreter/LLVM O0/O2/cache evidence covers objects,
  ports, hierarchy, functions, and suspended tasks. Batch 85 is not a
  CI-inspection boundary, so no Actions run was inspected.
- Batch 86 adds one source-spanned `DefaultChoice` plus signed integral index
  keys to direct one-dimensional static-array assignment patterns. Exactly one
  default fills unmentioned ascending or descending declared indices before
  converted, unique, in-range explicit keys replace their slots. Exact
  element conversion, X/Z values, typed temporary atomicity, positional
  compatibility, objects, ports, hierarchy, automatic callables, suspension,
  interpreter/LLVM O0/O2, and schema-38 cache identity are covered without a
  public ABI change. Batch 86 is not a CI-inspection boundary, so no Actions
  run was inspected.
- Batch 87 adds direct direction-preserving `[left:right]` selections of
  one-dimensional integral static arrays as contextual selected-range values.
  Slice-to-whole, whole-to-slice, and slice-to-slice assignment map exact
  compatible elements ordinally across differing declared indices and
  directions. Typed source snapshots plus selected staging and one whole-array
  commit make overlap atomic while preserving signed bit and four-state X/Z
  values through objects, ports, hierarchy, automatic functions, and tasks
  after suspension. Schema 39 and container semantic revision 15 record both
  ranges, profiles, operations, and provenance without a public ABI change.
  Batch 87 is not a CI-inspection boundary, so no Actions run was inspected.
- Batch 88 extends those direct selections to read-only system-query,
  `.size()`, reduction, transformation, extrema/uniqueness, and all six
  predicate-locator receivers. Selected range/profile and signed declared
  indices remain exact through module objects, input and writable ports,
  hierarchy, automatic functions, and tasks after suspension. Schema 40 and
  container semantic revision 16 record receiver types, consumer operations,
  graphs, and provenance without a public ABI change. Batch 88 is not a
  CI-inspection boundary, so no Actions run was inspected.
- Batch 89 passes compatible direct selections by value into fixed automatic
  function inputs and task input/output/inout formals. Equal-count ranges map
  ordinally into the formal profile; task output resets on every call and
  output/inout copy-out atomically replaces only the selected caller range
  after normal or early return, including after suspension. Exact X/Z state,
  nested calls, writable ports, generated hierarchy, schema 41, and container
  semantic revision 17 have frontend, elaboration, interpreter/LLVM O0/O2,
  and cache evidence without a public ABI change. Batch 89 is not a
  CI-inspection boundary, so no Actions run was inspected.
- Batch 90 extends direct writable selections to `reverse`, `sort`, and
  `rsort`, including optional implicit/named keys with selected declared
  `.index` values. Ordering occurs on an exact selected snapshot before one
  whole-array replacement preserves every surrounding element and X/Z bit.
  Module objects, writable ports, generated hierarchy, suspended tasks,
  schema 42, and container semantic revision 18 have frontend, elaboration,
  interpreter/LLVM O0/O2, and cache evidence without a public ABI change.
- The Batch 90 CI boundary is green. Initial run `30611906784` exposed an
  oversized MSVC application-test string literal; repair `cb63805` split the
  fixture into three writes. Replacement run `30612954452` passed that point
  and exposed implicit optional-byte construction under MSVC LLVM Debug;
  repair `eaf4842` made the constants explicitly `std::uint8_t`. Run
  `30613588803` was cancelled by the documentation checkpoint under the
  branch concurrency policy. Final replacement run `30613827882` passed all
  12 jobs. Windows MSVC Debug completed in 18 minutes 45 seconds, Windows
  MSVC LLVM Debug in 28 minutes 45 seconds, and ASan/UBSan in 43 minutes 59
  seconds, confirming the 45-minute job budget remains necessary.
- Batch 91 adds direct named and positional locally constant static-array
  `[left:right]` module-port actuals. Formal-typed recursive aliases provide
  read-only input views and atomic selected output/inout updates across
  differing compatible indices and directions, preserving exact signed,
  two-/four-state X/Z elements. Interval-aware driver ownership admits
  disjoint writers and rejects overlap. Nested/generated hierarchy,
  specialization, debugger, VCD, interpreter/LLVM O0/O2, schema 43, and
  container semantic revision 19 are covered without a native ABI change.
  Exact-LLVM Debug/Release passed all 59 tests in 412.19/116.86 seconds.
  Batch 91 is not a CI-inspection boundary, so no Actions run was inspected.
- Batch 92 adds direct locally constant `base +: width` and `base -: width`
  static-array selections. Known signed-32 bases and positive widths normalize
  to the receiver's declared direction, so both operators work on ascending
  and descending arrays when the computed interval is in range. Assignments,
  queries, reductions, locators, ordering, function/task actuals, module-port
  aliases, debugger, VCD, interpreter/LLVM O0/O2, schema 44, and container
  semantic revision 20 are covered without a native ABI change. Equivalent
  plus/minus intervals share cache identity. Exact-LLVM Debug/Release passed
  all 59 tests in 439.73/128.24 seconds. Batch 92 is not a CI-inspection
  boundary, so no Actions run was inspected.

The repository is a substantial pre-alpha executable simulator, not fsim v1.
Many language families have strong bounded evidence, but every broad v1
contract row is release-blocking until it has complete positive, negative,
elaboration, interpreter, and JIT evidence.

## Progress by milestone

| Milestone | Status | Current result |
|---|---|---|
| 1. Platform and semantic spine | In progress | Cross-platform C++20/CMake foundation, exact LLVM adapter, dependencies, diagnostics, manifest, native ABIs, Tcl, and CI definitions exist. Unicode/path and remaining console-interrupt validation are open. |
| 2. Internal vertical slice | In progress, near architecture gate | Interpreter, hybrid LLVM JIT, cache, deterministic scheduler, mixed hierarchy, VCD, debugger, and examples execute. Bounded typed 1–64-bit SystemVerilog constants, same-language type parameters, strings, text files, dynamic arrays/queues/integral-key associative arrays, one-dimensional static memories with `$readmem*`, direct same-language static and dynamic whole-container module ports, direct compatible static-array colon and locally constant indexed-slice assignment, read-only query/reduction/locator consumers, function/task slice actuals, module-port aliases, and atomic slice ordering mutation, unpacked-container bound/size/bit/dimension queries, positional container patterns plus static default/index-key patterns, reductions with bounded pure implicit/named-iterator `with` transformations, deterministic ordering with optional bounded pure `with` keys, extrema/uniqueness locators with optional bounded pure `with` transformations, and predicate locators with named iterators and signed declared/current indices, automatic integral/string/container functions and suspending tasks, full unsigned-64 values, and entity-level VHDL-2008 unclassified interface type, bounded interface function/procedure/package generics, generic subprogram templates/instances, recursive block/generate configurations and references, and scoped/package-visible overloaded scalar/vector and composite component declarations with value/type/function/procedure/package generics, deterministic default binding, typed component input defaults, and disconnected open output-family ports now have interpreter/O0/O2/cache evidence; wider/complete typing, remaining slice boundaries, multidimensional and cross-language container boundaries, remaining VHDL hierarchy/generic semantics, source metadata, and complete differential coverage still block the gate. |
| 3. Near-full synthesizable frontends | Pending | Broad bounded VHDL and SV execution exists, but the explicit language-specific typed HIR/DesignIR layering and full promised language semantics are incomplete. |
| 4. Procedural testbenches, SystemC, visibility | In progress | Extensive procedural, SystemC, C API, debugger, Tcl, and trace slices execute. Dynamic testbench data, fork/event completeness, richer SystemC behavior, and remaining API metadata are open. |
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
- bounded SystemVerilog module/package and selected generated functions with
  automatic, static, or implicit lifetime, ANSI/classic formals,
  positional/named/default associations, packed writable/reference transfer,
  bounded persistent packed static locals, eligible constant evaluation,
  package visibility, nested nonrecursive runtime calls, SimIR call/return
  control, debugger safe points, and transitive native-cache provenance;
- bounded SystemVerilog module/package and selected generated tasks with
  automatic or nonsuspending static/implicit lifetime, ANSI/classic formals,
  parameter-sized input/output/inout plus bounded packed reference transfer,
  named/default associations, deterministic deferred copy-in/copy-out, nested
  function/task calls, delays, named-event/condition waits, valueless early
  return after suspension, package visibility/time context, lifecycle and
  recursion diagnostics, debugger locals across stop/resume, and
  interpreter/LLVM O0/O2/cache equivalence;
- bounded one-dimensional integral SystemVerilog dynamic arrays, queues,
  integral-key associative arrays, and locally constant static unpacked arrays
  with typed module objects and automatic values, copy isolation, indexed
  access, dynamic/queue/associative methods, dense direction-aware fixed
  storage, manifest-confined `$readmemb`/`$readmemh`, suspending-task copy-out,
  direct same-language whole-container module-port aliases across
  nested/generated hierarchy, compatible formal-typed direct static-array
  slice port aliases with read-only input and atomic selected output/inout
  updates, direct bound/size/bit/dimension system queries
  and contextual positional/keyed assignment patterns plus exact-element-type
  `sum`/`product`/`and`/`or`/`xor` reductions with optional bounded pure
  `item`/named-iterator conditional transformations and deterministic
  no-argument `reverse` plus `sort`/`rsort` ordering with optional bounded
  pure `item`/named-iterator key graphs, plus `min`/`max`/`unique`/
  `unique_index` queue-valued locators with optional bounded pure
  `item`/named-iterator transformation graphs over
  objects/ports/callable values, debugger visibility, stable SimIR/native
  identities, and interpreter/LLVM O0/O2 equivalence;
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
  block, and selected-generate regions with retained value/type/function/
  procedure/package generic profiles plus scalar/vector, enumeration,
  named-subtype, non-nested-record, and one-dimensional scalar-element-array
  port profiles; explicit/omitted/box non-value actuals; declaration-ordered
  dependent-port specialization; nearest lexical/package visibility without
  package-use re-export; overload selection by association/mode/nominal type/
  constraint/direction/dependent generic profile; named/positional formal
  normalization; latest-analyzed compatible same-library default binding;
  configuration-map precedence; statically foldable scalar/vector/enumeration/
  record/array component input defaults; disconnected explicit or omitted
  open output-family ports; version-5 selected-actual/default/open/source/type/
  map/target identity; and interpreter/LLVM O0/O2/debugger/cache equivalence;
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
- Complete SystemVerilog strings beyond the bounded byte-oriented subset,
  type actuals beyond the bounded same-language packed subset, widths above
  64 bits, genvar-dependent typed constants, and remaining LRM expression
  typing.
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
  expression/aggregate actuals and entity-port defaults; incremental
  configurations and complete library analysis order; direct instantiation;
  blocks and generates.
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
- Extend the completed bounded interfaces/modports/packages slice only where
  the remaining v1 audit identifies a concrete uncovered legality boundary.
- Complete packed/unpacked types, nested aggregates, memories, remaining
  function forms, tasks, all `always` forms, expressions, gates, and
  assignment semantics.
- Complete general delays/events, fork/join, named events, and NBA/delta
  matrices.
- Complete general strings/files/dynamic arrays/queues/associative arrays and
  static memories beyond the bounded one-dimensional integral subsets,
  including same-language module ports and broader file forms.

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

## Completed batches 68 through 86

Bounded same-language SystemVerilog mutable strings now execute through the
reference interpreter and native LLVM O0/O2 without fallback. Module objects,
automatic function/task values, suspending-task frames, debugger reads and
bounded deposits, append-only plain-C helpers, exact semantic cache identity,
and cold/warm/selective-invalidation evidence are complete. Values remain
byte-oriented and limited to 4,096 bytes. Same-language manifest-relative
text files add opaque owned integer handles, bounded line I/O, exact
interpreter/LLVM O0/O2 lifecycle behavior, debugger-safe task suspension, and
content-independent cache identity. One-dimensional integral dynamic arrays
and queues add typed module/automatic storage, bounded allocation and methods,
copy-in/copy-out across suspended tasks, debugger visibility, and one shared
native callback. Integral-key associative arrays add exact key typing,
canonical numeric ordering, bounded insertion/replacement/read/deletion,
existence queries, traversal, copy isolation, and the same interpreter/native
callback behavior. Locally constant one-dimensional integral static arrays now
add ascending/descending dense storage, bit-zero/four-state-X defaults,
automatic function/task copy and suspension behavior, declared-index debugger
rendering, and manifest-confined bounded `$readmemb`/`$readmemh` with comments,
addresses, optional ranges, and exact X/Z state. Direct ANSI and basic
non-ANSI same-language static-array module ports now preserve exact
specialized ranges/types, read-only input and deterministic output/inout
aliases, nested/generated hierarchy, whole-array coherence, debugger paths,
and interpreter/LLVM O0/O2/cache behavior. Dynamic-array, queue,
bounded-queue, and integral-key associative-array ports now extend those
aliases with exact kind/element/index/bound specialization, allocation,
mutation, traversal, suspending-task copy-out, and four-bit named-index
execution. Direct supported container objects, port aliases, and callable
values now add static-folded or runtime `$left`/`$right`/`$low`/`$high`,
`$increment`, `$size`, `$bits`, `$dimensions`, and
`$unpacked_dimensions`, including empty dynamic results, the bounded
associative subset, suspended locals, formatted/debugger evidence, and
schema-28 interpreter/LLVM O0/O2/cache parity. Direct whole-container
apostrophe-brace assignments add exact-count static positional patterns,
resizing dynamic/queue positional patterns, typed unique-key associative
patterns, deterministic empty clearing, atomic temporary construction,
callable/port/suspension behavior, and schema-29 native parity. Static patterns
also accept exactly one default plus signed-32 locally constant index keys,
with direction-aware fill/override, exact X/Z conversion, the same atomic
temporary path, and schema-38 native identity across objects, ports,
hierarchy, automatic callables, and suspension. Direct compatible
one-dimensional static-array slices now retain selected declared ranges,
ordinally map slice-to-whole, whole-to-slice, and slice-to-slice assignments
across differing indices/directions, and preserve exact signed bit or
four-state X/Z elements. Typed source snapshots, selected staging, and one
whole-array replacement make overlap atomic across objects, writable ports,
hierarchy, automatic functions, and tasks after suspension through schema-39
interpreter/native/cache identity. The same direct slices now serve as
read-only receivers for selected-range system queries, `.size()`, all five
reductions with optional implicit/named transformations, all four
extrema/uniqueness locators, and all six predicate locators. Selected signed
declared indices remain exact across input/writable ports, hierarchy,
automatic functions, and suspended tasks through schema-40
interpreter/native/cache identity. Direct
no-argument `sum`, `product`, `and`, `or`, and `xor` methods now preserve
the exact element type, use explicit empty identities and shared four-state
semantics, reduce canonical storage order across every supported container
kind, and execute through validated schema-30 SimIR plus the existing native
container callback over objects, ports, callable values, and suspended
locals. Direct writable static arrays, dynamic arrays, queues, and bounded
queues now add no-argument `reverse`, `sort`, and `rsort` method statements.
They preserve declared/current index order, use stable deterministic
exact-width signed/unsigned four-state comparison, mutate coherent
object/port/callable storage across suspension, and execute through validated
schema-31 SimIR and the same native callback. Direct nonassociative containers
now add no-argument `min`, `max`, `unique`, and `unique_index` expressions
with typed queue results, empty/extrema/first-occurrence semantics, signed
declared or current indices, alias-safe replacement, and validated schema-32
interpreter/LLVM/cache parity across objects, ports, callable values, and
suspension. The same direct nonassociative containers now add `find`,
`find_index`, `find_first`, `find_first_index`, `find_last`, and
`find_last_index` with one bounded pure `item` predicate, exact value or
signed index queue results, X/Z-false selection, alias-safe replacement, and
validated schema-33 interpreter/LLVM/cache parity. Those predicate locators
now also accept one explicit source-spanned iterator binder and direct
`iterator.index` or `item.index` leaves. Static arrays expose signed declared
indices, dynamic arrays and queues expose current zero-based indices, typed
element/index comparison graphs reject mixed profiles, and schema 34 excludes
iterator spelling while preserving semantic index-node identity. The five
reductions now accept one optional bounded pure transformation with implicit
`item` or one named iterator, direct signed-32 `.index`, locally constant
element alternatives, logical/comparison composition, and one four-state
conditional mask. Named and implicit spellings lower to identical semantic
graphs. Each graph is evaluated once in declared/current order, preserves
empty identities and nonmutation, and uses validated schema-35 graph semantics
within the current schema-40 interpreter/native/cache identity.
`sort` and `rsort` now accept one optional bounded pure key graph with
implicit `item` or one named iterator and direct signed-32 `.index`. Every key
is evaluated once against the original declared/current index before stable
ascending/descending ordering; equal keys retain original order and exact
signed/unsigned four-state comparison. No-key ordering, coherent mutation,
ports, callable values, and suspension remain intact through validated
schema-36 interpreter/native/cache semantics.
The four extrema/uniqueness locators now accept one optional bounded pure
transformation graph with implicit `item` or one named iterator and direct
signed-32 `.index`. Keys are evaluated once against the original
declared/current index before selection. Extrema compare keys but return the
first original extremal element; uniqueness returns the first original element
or signed original index for each exact four-state key. No-`with`, empty,
capacity, alias, object/port/callable, hierarchy, and suspension behavior
remain intact through schema-37 locator semantics carried forward by the
current schema-44 interpreter/native/cache identity.
Unicode
code-point semantics, multidimensional or
aggregate/string-element containers, general expression or runtime-variable/
dynamic-container port actuals,
cross-language container boundaries, and unrestricted allocation remain
separate release-gate work.

## Completed feature batch 87

Feature batch 87 completed **bounded one-dimensional static-array slices**:

1. Contextually retain a direct static-container `[left:right]` selection as
   source-spanned unpacked-slice HIR distinct from a packed part-select.
2. Fold both slice bounds as known signed 32-bit constants and accept only a
   nonempty, direction-preserving subrange of the declared static array.
3. Preserve the selected declared left/right bounds and exact element profile
   in the contextual runtime type without allocating a new object identity.
4. Read a direct RHS slice into a compatible whole static-array destination
   of the same element count, mapping source and destination by ordinal
   left-to-right position even when their declared indices differ.
5. Write a compatible whole static-array source into a direct writable LHS
   slice with direction-aware declared-index mapping.
6. Support slice-to-slice assignment through a typed snapshot so overlapping
   selections have deterministic source-before-replacement semantics.
7. Preserve exact element width, signedness, two-/four-state values, and X/Z
   state without widening or packed reinterpretation.
8. Carry slice assignment through module objects, writable direct ports,
   nested/generated hierarchy, automatic functions, and tasks across
   suspension in interpreter, LLVM O0, and LLVM O2 execution.
9. Diagnose nonconstant/unknown, empty, reversed, or out-of-range bounds;
   count/profile mismatch; read-only or indirect targets; and excluded
   dynamic, queue, associative, multidimensional, aggregate/string, sliced
   port-actual, and cross-language forms.
10. Version selected range/direction, source and destination profiles, overlap
    policy, operations, and source provenance in native-object identity; add
    full positive/negative/cache evidence and push the non-boundary batch.

Keep this batch to direct direction-preserving slices of one-dimensional
integral static arrays and exact compatible element profiles. Variable bounds,
indexed `+:`/`-:` unpacked slices, slice queries or methods, sliced module-port
actuals, multidimensional nesting, dynamic/queue/associative slices, element
conversion, and cross-language slices remain separate release-gate work.

### Archived Batch 87 implementation design

This design checkpoint was recorded against the clean Batch 86 baseline at
`a42fa1a760f5a111b835445ccf68c28994a18c91` and guided the completed Batch 87
implementation. The current pushed commit supersedes that baseline.

The initial code-path audit established the following:

- the SystemVerilog parser already emits source-spanned
  `ExpressionKind::Slice` nodes for `[left:right]`, `[base +: width]`, and
  `[base -: width]`, with the base, left, and right expressions retained as
  operands;
- packed selection is already handled by
  `Lowerer::constant_slice_selection` in
  `src/elaboration/lowerer_assignment.cpp` and by packed-expression lowering
  in `src/elaboration/lowerer_expression.cpp`;
- the unpacked-container assignment dispatch is also in
  `src/elaboration/lowerer_assignment.cpp`: it resolves the base identifier
  before container lookup and, at the checkpoint, handled whole and indexed
  targets but rejected a slice target with `FSIM-ELAB-SVCONTAINER-011`;
- `lower_container_expression` in
  `src/elaboration/lowerer_sv_containers.cpp` accepts only a direct identifier
  and returns either its container register or an object snapshot;
- existing `ContainerRead`, `ContainerWrite`, `CopyContainerRegister`,
  `ReadContainerObject`, and `WriteContainerObject` operations already cover
  interpreter, native, validation, and cache paths. A new public or SimIR ABI
  operation is not expected; and
- `lowerer_sv_containers.cpp` is already large. Put the executable slice
  implementation in a new
  `src/elaboration/lowerer_sv_container_slices.cpp`, add it to `CMakeLists.txt`,
  and keep only declarations and compact data structures in
  `src/elaboration/elaborator_internal.hpp`.

Implement the slice using existing container operations:

1. Recognize only a direct identifier base and `Slice::text == ":"`; require a
   fixed, one-dimensional integral container.
2. Constant-fold both bounds, require known values, convert the low 32 bits to
   signed indices using the Batch 86 policy, and validate inclusion plus
   declared direction. A one-element range is valid in either syntactic
   direction.
3. Derive a temporary `ContainerType` by retaining the source element profile
   and replacing only its declared left/right range. Do not allocate a new
   object identity.
4. Materialize every RHS slice into that selected-range temporary with
   `ContainerRead`/`ContainerWrite` before modifying a destination. This is the
   overlap snapshot.
5. Copy by ordinal left-to-right position, not numeric index equality. Require
   identical element count, width, signedness, and two-/four-state domain.
6. For a whole destination, build or copy the complete compatible source value
   and finish through the existing whole-container copy/writeback path.
7. For an LHS slice, snapshot the RHS first, copy the current whole destination
   into a replacement register, overwrite only selected elements in the
   replacement, and perform one final whole-container copy. Preserve the
   existing outer object writeback so object, port, hierarchy, callable, and
   suspended-task behavior remains coherent.

Expected integration points are a whole-target/slice-source branch in the
identifier-target portion of the container assignment dispatcher and a new
slice-target branch before its current generic rejection. Useful private
helpers include validated slice metadata, lowering an identifier-or-slice
static-container value, and ordinal register-to-register copying. Bump the
native cache schema from 38 to 39 and the container semantic version from 14
to 15 once the new semantic metadata is present.

Evidence should include:

- a frontend assertion that both sides of a static-array slice assignment are
  retained as colon `Slice` HIR with explicit operands and spans;
- elaboration fixtures covering descending-to-descending,
  ascending-to-descending, whole-to-slice, slice-to-whole, slice-to-slice, and
  overlapping snapshot assignment, including exact X/Z values;
- diagnostics for unknown/nonconstant, wrong-direction, out-of-range, count,
  width, signedness, and state-domain mismatch; indexed `+:`/`-:` selections;
  dynamic/queue/associative containers; indirect or read-only targets; and
  already excluded sliced port actuals and mixed-language boundaries;
- application coverage using the existing static arrays rather than adding
  object-order churn: ordinal `memory`/`binary` copying, automatic-function
  local slices, an overlapping task slice after suspension, and the existing
  writable static port leaf; and
- emitted operation/type checks, interpreter/LLVM O0/O2 state equivalence,
  cold/warm schema-39 cache identity, diagnostics-catalog coverage, and the
  2,000-line source budget.

Batch 87 is not a CI-inspection boundary. Its focused evidence and both full
exact-LLVM Debug and Release gates passed with eight-worker builds. No Actions
run was inspected.

## Completed feature batch 88

Feature batch 88 completed **bounded read-only static-array slice consumers**:

1. Reuse the Batch 87 contextual selected-range value for direct static-array
   slice expressions outside assignment without adding recursive HIR storage.
2. Support `$left`, `$right`, `$low`, `$high`, and `$increment` on a direct
   slice using its selected declared range and direction.
3. Support `$size`, `$bits`, `$dimensions`, and `$unpacked_dimensions` on a
   direct slice with the same optional constant-dimension policy as whole
   containers.
4. Reduce a direct slice through `sum`, `product`, `and`, `or`, and `xor`,
   preserving exact empty, width, signedness, and four-state behavior.
5. Apply implicit or named reduction transformations to a slice, exposing the
   slice's signed declared indices through `.index`.
6. Produce compatible queue results from slice `min`, `max`, `unique`, and
   `unique_index`, retaining original selected elements or indices.
7. Run all six `find*` predicate locators on a slice with X/Z-false selection
   and selected declared-index results.
8. Carry these read-only consumers through module objects, direct input and
   writable ports, nested/generated hierarchy, automatic functions, and tasks
   across suspension in interpreter, LLVM O0, and LLVM O2.
9. Diagnose unsupported indirect, variable/indexed, multidimensional,
   nonstatic, aggregate/string, mutating ordering, sliced port-actual, and
   cross-language slice consumers with stable frontend/elaboration coverage.
10. Version selected receiver range/profile, query or method operation,
    transformation/predicate graph, and source provenance in schema 40; add
    full positive/negative/cache evidence and push the non-boundary batch.

Keep Batch 88 read-only after materializing the direct slice snapshot.
Assignment remains the Batch 87 path. Mutating `reverse`/`sort`/`rsort` slice
receivers, slice arguments or returns at callable/port boundaries, variable or
indexed unpacked slices, multidimensional/nonstatic containers, element
conversion, and cross-language slices remain separate release-gate work.

Frontend evidence retains direct source-spanned query, transformed-reduction,
and predicate-locator slice receivers. The negative matrix covers
variable/unknown, indexed, reversed/out-of-range, nonstatic, indirect,
string/aggregate, mutating-order, sliced-boundary, multidimensional, and
cross-language exclusions through stable frontend and elaboration codes.
Schema 40/cache identity and all 288 authored sources pass their gates.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 251.36 seconds, including scoped locals in 0.89 seconds and containers in
181.18 seconds. Release passed all 59 tests in 69.11 seconds, including scoped
locals in 0.80 seconds and containers in 38.51 seconds. Batch 88 is not a
CI-inspection boundary, so no Actions run was inspected.

## Completed feature batch 89

Feature batch 89 completed **bounded static-array slice callable actuals**:

1. Direct function and task slice actuals remain explicit source-spanned colon
   `Slice` HIR.
2. Compatible function inputs receive only a formal-typed copy of the selected
   caller range.
3. Compatible task inputs receive the same value snapshot, including across
   suspension.
4. Task outputs atomically copy into a writable slice only after normal or
   valueless-early return and reset to their exact typed default each call.
5. Task inout slices use snapshot copy-in and selected atomic copy-out.
6. Equal-count actual and formal ranges map ordinally across different
   declared indices and directions.
7. Exact width, signedness, state domain, and X/Z bits survive without element
   conversion or packed reinterpretation.
8. Module objects, writable static ports, nested/generated hierarchy, nested
   nonrecursive calls, and suspended tasks agree in interpreter, LLVM O0, and
   LLVM O2.
9. Read-only output/inout, count/profile mismatch, nonconstant/indexed,
   indirect/nonstatic/multidimensional, recursion, sliced module-port, and
   cross-language boundaries retain stable diagnostics.
10. Schema 41 and container semantic revision 17 record formal and selected
    actual types, copy operations, return provenance, and transitive source
    identity with dedicated positive, negative, and cache evidence.

The task frame stores immutable output defaults as vector-backed register
metadata, avoiding recursive inline HIR growth and preserving the MSVC Debug
stack-footprint constraint. No new SimIR operation, runtime callback, public
ABI slot, or module object identity was required.

The focused warnings-as-errors Debug gates passed: frontend in 0.06 seconds,
catalog in 0.05 seconds, source budget in 0.09 seconds, monolithic/split
elaboration in 0.41/0.10 seconds, LLVM in 1.81 seconds, runtime in 0.05
seconds, and the expanded container application in 234.23 seconds. The
catalog covers 1,222 production codes and all 289 authored sources satisfy the
2,000-line limit.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 299.83 seconds, including scoped locals in 0.89 seconds, containers in
230.95 seconds, and the monolithic application in 39.22 seconds. Release
passed all 59 tests in 89.83 seconds, including scoped locals in 0.80 seconds,
containers in 58.79 seconds, and the monolithic application in 13.55 seconds.
Batch 89 is not a CI-inspection boundary, so no Actions run was inspected.

## Completed feature batch 90

Feature batch 90 completed **bounded static-array slice ordering mutation**:

1. Direct `reverse`, `sort`, and `rsort` slice receivers remain explicit
   source-spanned colon `Slice` HIR.
2. `reverse` changes only selected ordinal positions.
3. `sort` stably applies the deterministic ascending element order only to the
   selected range.
4. `rsort` stably applies the deterministic descending element order only to
   the selected range.
5. Implicit or named keys bind the selected element type and expose selected
   signed declared indices through `.index`.
6. Every key is computed once from the selected snapshot and equal-key order
   is stable.
7. The finished selected value merges into one whole-array replacement,
   preserving surrounding elements and exact X/Z state.
8. Module objects, writable ports, nested/generated hierarchy, automatic
   tasks, and suspension agree in interpreter, LLVM O0, and LLVM O2.
9. Read-only, variable/unknown, reversed/out-of-range, indexed, indirect,
   nonstatic/multidimensional, malformed-key, sliced module-port, and
   cross-language boundaries retain stable diagnostics.
10. Schema 42 and container semantic revision 18 record selected type,
    ordering/key semantics, atomic operation shape, and provenance with
    dedicated positive, negative, and cache evidence.

The focused warnings-as-errors Debug gates passed: frontend in 0.06 seconds,
catalog in 0.05 seconds, source budget in 0.10 seconds, monolithic/split
elaboration in 0.40/0.11 seconds, LLVM in 1.82 seconds, runtime in 0.05
seconds, and the expanded container application in 258.05 seconds. The
catalog covers 1,222 production codes and all 290 authored sources satisfy the
2,000-line limit.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 59 tests
in 329.46 seconds, including scoped locals in 0.90 seconds, containers in
260.58 seconds, and the monolithic application in 39.18 seconds. Release
passed all 59 tests in 98.66 seconds, including scoped locals in 0.80 seconds,
containers in 67.70 seconds, and the monolithic application in 13.40 seconds.
The mandatory non-documentation boundary inspection first ran as GitHub
Actions run `30611906784` for feature commit `d071d98`. Windows MSVC Debug
rejected an oversized generated-fixture string literal, repaired by
`cb63805`. Replacement run `30612954452` passed that build point, then
Windows MSVC LLVM Debug rejected implicit optional-byte construction in the
slice-cache matrix, repaired by `eaf4842`. Authoritative replacement run
`30613588803` was cancelled by the documentation checkpoint under the branch
concurrency policy. Final replacement run `30613827882` passed all 12 jobs.
Windows MSVC Debug completed in 18 minutes 45 seconds, Windows MSVC LLVM
Debug in 28 minutes 45 seconds, and ASan/UBSan in 43 minutes 59 seconds.

## Completed feature batch 91

Batch 91 completes bounded direct static-array slice module-port actuals. Its
implementation retains these architectural choices:

- `ContainerObject` and public elaboration metadata may carry a
  `ContainerSliceAlias` consisting of an earlier target object plus the
  selected target left/right bounds. The alias object itself retains the
  child formal's fixed `ContainerType`, so child code sees only formal indices
  and never exposes unselected parent elements.
- Alias-aware runtime reads materialize the selected target range into formal
  ordinal order. Alias-aware writes copy the complete parent value, merge the
  complete formal value into the selected range, and commit one replacement.
  Recursive alias traversal therefore supports a later whole-port alias below
  a sliced connection while retaining atomic parent updates.
- Interpreter container operations, native execution callbacks, public
  inspection, and deposits use the same recursive alias-aware helpers. No
  native ABI slot or new SimIR operation has been introduced.
- Same-language hierarchy connection now accepts a whole object or a direct
  colon slice. It validates fixed shape, locally constant signed bounds,
  direction, range, equal element count, and exact element profile before
  creating the alias. Output/inout connections still reject a parent input
  port.
- Boundary-driver tracking now carries an optional selected interval.
  Disjoint slice writers may coexist; whole-object or overlapping writers
  retain the existing deterministic `SVPORT-008` rejection.

The following files contain that implementation:

- `include/fsim/runtime/simir.hpp` and
  `include/fsim/elaboration/elaborator.hpp`;
- `src/runtime/simir_internal.hpp`, `simir_containers.cpp`,
  `simir_interpreter.cpp`, and `simir_execution.cpp`;
- `src/elaboration/elaborator_internal.hpp`,
  `hierarchy_sv_ports.cpp`, `hierarchy_types.cpp`, and
  `hierarchy_systemc.cpp`; and
- aggregate-initializer compatibility adjustments in
  `tests/runtime/runtime_container_tests.cpp`.

The exact warnings-as-errors focused build uses:

```sh
cmake --build build/llvm22-ninja-debug --parallel 8 \
  --target fsim_runtime_tests fsim_elaboration_tests \
  fsim_sv_container_elaboration_tests
```

The focused Debug suites pass:

```text
fsim.elaboration                 0.43 s
fsim.elaboration.sv-container    0.12 s
fsim.runtime                     0.05 s
```

New evidence covers runtime aliases and deposits, parameter-substituted named
and positional HIR, input/output/inout, differing formal ranges and
directions, exact X/Z state, nested/generated whole forwarding, disjoint and
overlapping writers, read-only and malformed actuals, debugger paths, scalar
VCD, interpreter, LLVM O0/O2, and cold/warm cache identity. Schema 43 and
container semantic revision 19 retain code-relevant formal profile, operation,
object, specialization, and source identity while selected bounds remain
validated runtime metadata.

The complete exact-LLVM Debug regression passed all 59 tests in 412.19
seconds, including scoped locals in 0.90 seconds, containers in 342.15
seconds, and the monolithic application in 40.03 seconds. Release passed all
59 tests in 116.86 seconds, including scoped locals in 0.86 seconds,
containers in 85.11 seconds, and the monolithic application in 13.55 seconds.
The diagnostic catalog covers 1,222 production codes and the source gate
covers 291 authored files. Batch 91 is not a CI-inspection boundary, so no
Actions run was inspected.

## Completed feature batch 92

Batch 92 completes bounded locally constant indexed static-array slices. Its
implementation retains these architectural choices:

- Unpacked `base +: width` and `base -: width` remain explicit source-spanned
  indexed `Slice` HIR until contextual elaboration distinguishes them from
  packed indexed part-selects.
- Elaboration requires a known signed-32 base and width and a positive width,
  computes a checked numeric interval, validates it against the receiver, and
  orients the normalized selected range to the array's declared direction.
  Both operators therefore apply to ascending and descending receivers.
- The normalized fixed `ContainerType` flows through atomic assignment,
  queries, reductions, transformations, locators, ordering, fixed
  function/task actuals, and recursive same-language module-port aliases.
- Existing source snapshots and whole-parent replacement preserve overlap and
  suspension atomicity. Selected `.index` observations use the normalized
  signed declared indices; exact element width, signedness, state domain, and
  X/Z bits remain unchanged.
- Native-object schema 44 and container semantic revision 20 serialize the
  normalized range/profile, operation graph, specialization, and source
  identity. Equivalent plus/minus spellings of one interval reuse one native
  object without changing the public ABI.

The complete exact-LLVM Debug regression passed all 59 tests in 439.73
seconds, including scoped locals in 1.02 seconds, indexed containers in
363.49 seconds, and the monolithic application in 43.07 seconds. Release
passed all 59 tests in 128.24 seconds, including scoped locals in 0.85
seconds, indexed containers in 92.19 seconds, and the monolithic application
in 14.87 seconds. The diagnostic catalog covers 1,222 production codes and
the source gate covers 291 authored files. Batch 92 is not a CI-inspection
boundary, so no Actions run was inspected.

## Completed feature batch 93

Batch 93 completes bounded fixed-array function result values and direct slice
returns:

- One-dimensional integral fixed unpacked result dimensions remain explicit
  in HIR and specialize to exact bounds, width, signedness, and state domain.
- Function-name assignment and explicit value `return` share one typed result
  frame whose exact X default is restored on every activation.
- Whole, colon-slice, and locally constant indexed-slice returned values adapt
  ordinally while preserving exact X/Z planes.
- Nested nonrecursive calls copy results into isolated destinations; returned
  values assign to whole arrays or direct slices through snapshot/atomic merge
  semantics, including overlap.
- Module, package, imported, directly qualified, and parameter-specialized
  calls retain debugger result metadata, VCD witnesses, interpreter/LLVM
  O0/O2 parity, cold/warm reuse, and transitive invalidation.
- Native-object schema 45 and container semantic revision 21 serialize fixed
  result/source profile, operation, specialization, and provenance without a
  public ABI change.

The complete exact-LLVM Debug regression passed all 59 tests in 421.42
seconds, including scoped locals in 0.96 seconds, fixed-array functions in
7.90 seconds, containers in 339.70 seconds, and the monolithic application in
39.86 seconds. Release passed all 59 tests in 124.07 seconds, including scoped
locals in 0.81 seconds, fixed-array functions in 7.01 seconds, containers in
87.24 seconds, and the monolithic application in 13.76 seconds. The diagnostic
catalog covers 1,223 production codes and the source gate covers 291 authored
files. Batch 93 is not a CI-inspection boundary, so no Actions run was
inspected.

## Completed feature batch 94

Batch 94 completes bounded dynamic-array, queue/bounded-queue, and integral-key
associative-array function result values:

- Exact result kind, element profile, queue bound, and associative index
  profile survive HIR, parameter/typedef specialization, and lowering.
- Function-name assignment and explicit value return share exact empty
  per-activation defaults and isolated element/key copies across nested calls.
- Compatible results assign to whole objects or automatic container locals and
  flow directly into bounded function or task input actuals.
- Module/package/imported/qualified calls, parameter-bound queues,
  typedef-indexed associative values, debugger metadata, VCD witnesses,
  interpreter/LLVM O0/O2, cold/warm reuse, and package-edit invalidation agree.
- Exact result/target/function-actual/task-actual profile diagnostics prevent
  mismatched copies before execution.
- Native-object schema 46 and container semantic revision 22 serialize
  nonstatic kind/bound/profiles, operation graph, specialization, and
  provenance without a public ABI change.

The complete exact-LLVM Debug regression passed all 59 tests in 430.23
seconds, including scoped locals in 0.96 seconds, functions in 9.48 seconds,
containers in 351.17 seconds, and the monolithic application in 40.34 seconds.
Release passed all 59 tests in 128.98 seconds, including scoped locals in 0.81
seconds, functions in 8.48 seconds, containers in 89.85 seconds, and the
monolithic application in 13.91 seconds. The diagnostic catalog covers 1,225
production codes and the source gate covers 291 authored files. Batch 94 is
not a CI-inspection boundary, so no Actions run was inspected.

## Completed feature batch 95

Container-returning calls now retain exact fixed, dynamic, or queue type while
nested directly in the supported query family, element indexing, reductions,
extrema, uniqueness, predicate locators, and compatible container-valued
conditionals. Every runtime consumer owns one isolated snapshot. Known
conditions copy one alternative; X/Z merges equal-shape four-state elements,
coerces unknown two-state bits to zero, and resets unequal nonstatic shapes to
the empty value. Associative conditionals, incompatible profiles, and mutating
temporary methods have bounded diagnostics.

Interpreter and LLVM O0/O2 share the conditional semantic helper. Module and
package calls, imported/qualified resolution, debugger metadata, VCD witnesses,
cold/warm reuse, package-edit invalidation, and a 14-object cache matrix pass.
Native-object schema 47 and container semantic revision 23 serialize the exact
consumer/conditional graph. Debug passed 59/59 in 443.17 seconds (scoped locals
0.96, functions 11.55, containers 358.54, application 42.38); Release passed
59/59 in 136.97 seconds (scoped locals 0.89, functions 10.08, containers 94.70,
application 14.53). The catalog covers 1,230 codes and the source gate covers
291 files. Batch 95 is not a CI-inspection boundary, so no Actions run was
inspected.

## Completed feature batch 96

Exactly compatible fixed arrays, dynamic arrays, queues, bounded queues, and
integral-key associative arrays now support whole-container `==`, `!=`, `===`,
and `!==`. The elaborator retains both operands as exact typed container
expressions, validates kind, fixed range or queue bound, element profile, and
associative index profile, then emits one `CompareContainers` operation.

Logical equality compares exact size or key sets and elements in deterministic
container order. A known mismatch returns false immediately; otherwise any
unknown element comparison propagates X for four-state profiles, while
two-state profiles return a bit. Case equality compares the value and X/Z
planes exactly and always returns a known bit. Negated forms reuse the common
comparison followed by scalar logical negation. Function-result and compatible
conditional operands are evaluated once into isolated snapshots in lexical
order.

Interpreter and LLVM O0/O2 call the same semantic helper. Module/package,
imported/qualified calls, specialization, debugger metadata, scalar VCD
witnesses, cold/warm reuse, and package-edit invalidation agree. Mixed scalar
operands, profile mismatches, relational ordering, and wildcard equality retain
targeted diagnostics. Native-object schema 48 and container semantic revision
24 serialize the comparison mode and exact operand graph; the dedicated matrix
contains 15 distinct native objects without a public ABI change.

The complete exact-LLVM Debug regression passed all 59 tests in 423.91 seconds
(scoped locals 0.93, functions 11.00, containers 343.67, application 40.03).
Release passed all 59 tests in 129.03 seconds (scoped locals 0.82, functions
9.92, containers 88.69, application 13.64). The catalog covers 1,233 codes and
the source gate covers 291 files. Batch 96 is not a CI-inspection boundary, so
no Actions run was inspected.

## Completed feature batch 97

Bounded SystemVerilog scalar integral `inside` expressions now retain one
source-spanned left operand and a nonempty ordered list of exact values and
explicit `[low:high]` range nodes. Elaboration requires exact width and
signedness compatibility, evaluates the left operand once, and tests members
in source order. Value members apply right-operand X/Z wildcard equality;
ascending closed ranges are inclusive and known reversed ranges are empty.
An unknown comparison remains X unless a later member definitely matches, and
a definite match branches past every remaining member.

The typed constant evaluator, interpreter, and LLVM O0/O2 agree for exact,
mixed, wildcard, unknown, signed, two-state, reversed-range, and later-match
cases. Module/package/imported/qualified calls may supply the left value,
members, and range bounds, with one evaluation for each reached call. Evidence
also covers skipped failing members, operation counts, debugger metadata, VCD,
cold/warm reuse, and package-edit invalidation. Empty or malformed lists,
nonintegral or incompatible operands, containers, nested membership, and
`case inside` retain targeted diagnostics.

Native-object schema 49 records the ordered membership graph and transitive
provenance while container semantic revision 24 and the public ABI remain
unchanged. The complete exact-LLVM Debug regression passed all 59 tests in
428.53 seconds (scoped locals 0.91, functions 11.93, containers 348.00,
application 39.94). Release passed all 59 tests in 127.57 seconds (scoped
locals 0.83, functions 10.12, containers 87.04, application 13.85). The
catalog covers 1,244 codes and the source gate covers 292 files. Batch 97 is
not a CI-inspection boundary, so no Actions run was inspected.

## Completed feature batch 98

Bounded SystemVerilog `case (selector) inside` statements now retain a
distinct source-spanned matching mode with ordered alternatives, exact values,
explicit closed ranges, and final-default metadata. Selector, value, and range
profiles require exact scalar-integral width and signedness. The selector
executes once; exact choices use right-choice X/Z wildcards, ascending ranges
are inclusive, and known reversed ranges are empty.

Unknown comparisons fall through, a later wildcard can match definitely, and
otherwise default executes. Source order and first definite selection are
preserved across mixed choices and alternatives, including short-circuiting a
later divide-by-zero call. Constant functions, interpreter, LLVM O0/O2,
debugger metadata, VCD, cold/warm reuse, and package-edit invalidation agree.
Non-SystemVerilog use, malformed or empty choices, `casez`/`casex`
combinations, deferred `case matches`, duplicate defaults, qualifiers,
nonintegral/incompatible operands, nested membership, and malformed HIR retain
targeted diagnostics.

Native-object schema 50 records the exact choice/range and ordered control-flow
graph while container semantic revision 24 and the public ABI remain
unchanged. Debug passed all 59 tests in 420.22 seconds (scoped locals 1.02,
functions 12.74, containers 338.48, application 39.71). Release passed all 59
tests in 125.07 seconds (scoped locals 0.83, functions 10.46, containers 84.69,
application 13.56). The catalog covers 1,255 codes and the source gate covers
292 files. Batch 98 is not a CI-inspection boundary, so no Actions run was
inspected.

## Completed feature batch 99

Bounded SystemVerilog `unique`, `unique0`, and `priority` qualifiers now retain
compact source-spanned metadata independently of exact, `casez`, `casex`, and
`case inside` matching modes. The selector executes once and choices are ORed
per alternative before qualifier checks, so comma-separated choices in one
item never count twice. Unknown comparison results are nonmatches.

`unique` warns for multiple matching alternatives and for no match without a
default; `unique0` warns only for multiple alternatives; `priority` warns only
for no match without a default. A default suppresses no-match warnings, and
ordinary first-matching-body/default execution remains source ordered. Common
source-aware warning reports agree through constant-function selection,
interpreter, LLVM O0/O2, callbacks, CLI rendering, cold/warm reuse, and source
edits. Duplicate, misplaced, non-SystemVerilog, invalid-HIR, and wrong-language
forms retain targeted diagnostics.

Native-object schema 51 records qualifier report/control-flow identity in an
18-object cache matrix while container semantic revision 24 and the public ABI
remain unchanged. Debug passed all 59 tests in 427.12 seconds (scoped locals
0.90, functions 12.52, containers 341.61, qualified assertions 6.07,
application 39.64). Release passed all 59 tests in 131.20 seconds (scoped
locals 0.79, functions 10.50, containers 86.11, qualified assertions 5.89,
application 13.47). The catalog covers 1,259 codes and the source gate covers
294 files. Batch 99 is not a CI-inspection boundary, so no Actions run was
inspected.

## Completed feature batch 100

Feature batch 100 completes **bounded SystemVerilog case-pattern matching**:

1. Retain `case (selector) matches` as a distinct source-spanned matching mode
   whose patterns are not flattened into ordinary case expressions.
2. Parse the form only in SystemVerilog and reject combinations with `casez`,
   `casex`, `inside`, duplicate matching keywords, or malformed alternatives.
3. Admit a bounded scalar-integral constant-pattern subset with exact width
   and signedness plus the unconditional `.*` wildcard pattern.
4. Evaluate the selector once and normalize each pattern result to a definite
   match, treating unresolved comparison state as a nonmatch.
5. Preserve one pattern per item, source-ordered first-body selection, and one
   final default.
6. Compose `unique`, `unique0`, and `priority` with the per-alternative pattern
   match bits and established warning rules.
7. Cover constant-function selection plus interpreter and LLVM O0/O2 runtime
   behavior with report/debug/VCD evidence.
8. Cover cold/warm reuse, source edits, specialization provenance, and exact
   native-cache identity.
9. Diagnose guarded (`&&&`), tagged, variable-binding, struct/array/member,
   type/class, comma-list, and malformed-HIR patterns without silently
   accepting unsupported syntax.
10. Native-object schema 52, full Debug/Release evidence, and the mandatory
    post-push non-documentation GitHub CI inspection close the boundary.

Keep Batch 100 to bounded scalar-integral case-pattern matching and explicit
tagged/composite-pattern diagnostics. Implementing tagged unions or general
destructuring patterns remains in the later aggregate-type batch.

The completed local implementation uses exact four-state case equality for
constant patterns, one unconditional `.*` marker, shared qualified-case report
control flow, and the existing scalar SimIR operations. Constant-function,
interpreter, LLVM O0/O2, debugger/VCD, cold/warm, package-edit, and 19-object
schema-52 cache evidence pass. The catalog covers 1,269 codes and all 295
authored files satisfy the source gate. Debug/Release passed 59/59 in
433.09/131.62 seconds; scoped locals remained quick at 0.89/0.77 seconds.
Feature commit `4730516` began the mandatory CI boundary. Runs `30640792808`,
`30647570353`, and `30651503050` passed every ordinary job but exposed exact
1,500.27-, 1,800.14-, and 2,400.12-second sanitizer container timeouts. Budget
repairs `7400061`/`c669187` made each bottleneck observable; root repair
`d2f216e` removes redundant compiled/O2 aliases only in LLVM-disabled builds
and restores a diagnostic 1,200-second test limit. Final run `30656887493`
passed all 12 jobs. ASan/UBSan passed 58/58 in 876.53 seconds, including scoped
locals in 1.44 seconds, containers in 588.70 seconds, and the monolithic
application in 154.76 seconds. Windows MSVC Debug passed 58/58 in 336.76
seconds, including scoped locals in 0.33 seconds, containers in 219.86 seconds,
and the monolithic application in 78.62 seconds. Batch 100 is closed.

## Completed feature batch 101

Feature batch 101 completes **SystemVerilog expression sizing and selection
closure**:

1. Retain explicit self-determined and context-determined width, signedness,
   and two-/four-state conversion metadata for the supported expression HIR.
2. Complete sized, unsized, unbased-unsized, unary, arithmetic, relational,
   equality, shift, power, conditional, concatenation, and replication sizing
   conversions within the v1 integral-width contract.
3. Apply exact SystemVerilog signed/unsigned extension, truncation, and result-
   type rules at assignments, arguments, returns, conditions, and selections.
4. Make `&&`, `||`, and `?:` short-circuit evaluation observable and preserve
   four-state truth/merge semantics without evaluating skipped function calls.
5. Guarantee lexical single evaluation for reached side-effecting function
   calls and dynamic selection bases, with interpreter/O0/O2 execution-point
   evidence.
6. Add runtime-base packed `base +: width` and `base -: width` reads with a
   positive locally constant result width and declared-direction mapping.
7. Define exact unknown/out-of-range dynamic part-select results and retain
   source/debug/VCD metadata without admitting dynamic procedural targets.
8. Add bounded fixed-width integral streaming concatenation for supported
   left/right stream directions, constant slice sizes, and nested ordinary
   concatenation operands.
9. Diagnose unsupported widths, ambiguous/incompatible conversions, dynamic
   stream sizes, aggregate/container streams, dynamic lvalue part-selects, and
   excluded expression side effects through stable frontend/elaboration codes.
10. Version the complete expression/conversion/selection graph in native-cache
    identity, add positive/negative/constant/interpreter/O0/O2/cache/debug/VCD
    evidence, run full Debug/Release gates, and push the non-boundary batch.

Keep Batch 101 to expression values and read selections. Procedural lvalue
closure, timed compound assignments, expression-form increments/decrements,
and force/release remain Batch 102.

The completed implementation retains source-spanned resolved expression
profiles, applies the bounded scalar context-sizing rules, branches around
skipped `&&`, `||`, and `?:` function calls, and permits time-free nonlocal
function writes so side effects are executable evidence. Runtime-base packed
read part-selects share one interpreter/LLVM operation with exact declared-
direction and partial/unknown fill semantics. Left/right integral streaming
concatenations share exact constant/runtime chunk order, including a partial
final chunk. Parser, elaboration, and JIT validation cover excluded forms.

Native-object schema 53 records every profile and dynamic-part-select field in
a 12-object cache matrix. Container semantic revision 24 and the public ABI
remain unchanged. A full-gate validation repair preserves exact wider profiles
for reference-only processes so the established partial JIT fallback still
compiles eligible sibling processes. The catalog covers 1,279 codes, and all
296 authored files pass the source gate with an empty allowlist and a 2,000-
line maximum. Debug/Release passed 59/59 in 430.48/132.39 seconds; scoped
locals completed in 0.86/0.80 seconds, parameter sizing in 2.79/1.89 seconds,
functions in 12.79/10.62 seconds, containers in 340.50/84.09 seconds,
assertions in 8.06/7.71 seconds, and the monolithic application in 39.44/13.12
seconds. Batch 101 feature commit `5e713cc` was pushed without monitoring its
non-boundary run.

## Completed feature batches 102 through 105

Batch 102 closes the bounded procedural-lvalue scope with one checked target
capture for whole, member, static/chained, dynamic-bit, and runtime-base
indexed-part selections. Partial dynamic-part writes, timed compound updates,
prefix/postfix expression values, and static selected force/release agree
through interpreter, LLVM O0/O2, scheduler, cache, debugger-safe boundaries,
and VCD evidence. Unknown or wholly out-of-range dynamic bases do not write;
selected force masks only its region while driver updates continue beneath it.
Native schema 54 and the append-only 512-byte runtime-v1 callback structure
carry the new semantics. The complete evidence and exact gates are recorded in
the implementation plan and feature-matrix rows SV-581 through SV-590.

Batch 103 closes the bounded SystemVerilog function/task scope. HIR and parser
paths retain automatic/static/implicit lifetime, ANSI and classic body
formals, input/output/inout/reference mode, defaults, and positional/named
associations. A common checked binder normalizes actuals and copy-out. Packed
static body locals persist across sequential process calls; unsafe static
suspension, nested/nonintegral static locals, nonlocal or suspending references,
nonintegral writable function formals, recursion, and malformed HIR remain
diagnosed. Existing container callable values retain their isolation and
suspension behavior. Selected generated callables, debugger locals and safe
points, VCD side effects, interpreter/LLVM O0/O2, schema-55 cold/warm/edit
identity, 1,303 diagnostics, the 301-file source gate, and both 60-test full
regressions pass. Feature-matrix rows SV-591 through SV-600 are the detailed
release evidence. Batch 103 is not a CI-inspection boundary.

Batch 104 closes the bounded SystemVerilog `always` and procedural-control
scope. Plain body-timed `always` starts at time zero and re-enters after proven
suspension; header event processes wait first; `always_ff` requires one edge
event and rejects nested timing. Runtime procedural `for` and single-evaluated
runtime `repeat`, cycle-safe `forever`, exact packed any-change expressions,
repeated intra-assignment event controls, and transitive cycle-safe wildcard
dependencies execute through shared SimIR. Interpreter/LLVM O0/O2, callbacks,
safe points, normalized VCD, schema-56 cold/warm/edit cache identity, 1,315
diagnostics, the 304-file source gate, and both 60-test full regressions pass.
Feature-matrix rows SV-601 through SV-610 are the detailed release evidence.
Batch 104 is not a CI-inspection boundary.

Batch 105 closes bounded SystemVerilog fork/process and NBA ordering.
Named/anonymous `fork` scopes, leading declarations, `join`, `join_any`,
`join_none`, `wait fork`, and recursive `disable fork` execute through dynamic
child processes with independent PCs and a shared lexical frame. A completed
intermediate child does not shield live grandchildren from cancellation. One
live activation per lexical fork site remains the checked bounded lifetime
rule, and fork inside a callable is rejected. Named-event blocking/nonblocking
races, stable dynamic-child NBA order, active/inactive/update/postponed region
interactions, and postponed direct-signal `$strobe` sampling now have exact
evidence. Child runtime/static identities keep debugger names and local reads
bounds-safe. Interpreter/LLVM O0/O2, callbacks, safe points, normalized VCD,
append-only resume statuses 11 through 14, schema-57 cold/warm/edit cache
identity, 1,324 diagnostics, the 310-file source gate, and both 61-test full
regressions pass. Feature-matrix rows SV-611 through SV-620 are the detailed
release evidence. Batch 105 is not a CI-inspection boundary.

Batch 106 closes bounded SystemVerilog delay, primitive, and continuous-driver
semantics. Locally constant parameter/localparam delay expressions retain
their module time scale and normalize after specialization with checked
unknown, negative, and overflow paths. Scalar/packed net declaration delays
and initializers, including generated declarations, lower through continuous
drivers; explicit assignment and net delays compose transition by transition.
Named static arrays of supported logic primitives map packed terminals
ordinally or broadcast scalars, while `bufif0`, `bufif1`, `notif0`, and
`notif1` add exact four-state enable, Z turnoff, and one-/two-/three-value
delays. Deferred MOS, switch, resistive, pull, strength, and path forms remain
explicitly diagnosed. Inertial cancellation, same-value pending transactions,
zero-delay publication, generated and specialized drivers, debugger/process
metadata, callbacks, normalized VCD, interpreter/LLVM O0/O2, and schema-58
cold/warm/edit identity have positive and negative evidence. The catalog has
1,335 diagnostics, the source gate covers 310 files, and both 61-test full
regressions pass. Feature-matrix rows SV-621 through SV-630 are the detailed
release evidence. Batch 106 is not a CI-inspection boundary.

Batch 107 completes bounded SystemVerilog interface, modport, and package
visibility closure. Parameterized interfaces retain members, behavior,
callables, time/source identity, and one-dimensional static instance arrays.
Explicit interface and modport ports bind named or positional whole/indexed
actuals through nested generated hierarchy. Checked input/output/inout/ref
views, callable import/export, read-only propagation, path-aware driver
ownership, selective/wildcard package re-export, transitive visibility,
collision/cycle rejection, debugger/VCD aliases, interpreter/LLVM O0/O2, and
schema-59 cold/warm/edit identity have positive and negative evidence. The
catalog covers 1,363 codes, the source gate covers 316 files, and both 62-test
full regressions pass. Feature-matrix rows SV-631 through SV-640 are the
detailed release evidence. Batch 107 is not a CI-inspection boundary.

Batch 108 completes bounded SystemVerilog preprocessing, directive, and
generate-specialization closure. Complete include directive arguments may be
assembled from nested macro fragments; identical/conflicting redefinitions,
`` `undefineall``, empty includes, and include-local conditional-frame
boundaries now have checked behavior. Source-ordered directive state retains
the supported compilation policies, with duplicate cell entry and unmatched
drive reset diagnosed. Physical/logical ancestry and transitive include bytes
remain explicit preprocessor-v5 cache inputs.

Unlabeled generate blocks receive stable source-ordered `genblkN` names.
Generated localparams evaluate per concrete genvar iteration before dependent
ranges, typedefs, enum literals, signal objects, callables, delays, instance
overrides, and connections specialize. Nested generated type environments
retain lexical shadowing. Debugger/VCD hierarchy, interpreter/LLVM O0/O2,
cold/warm reuse, and include-edit invalidation agree. Native schema 60 records
the specialized graph without a public ABI change. The catalog covers 1,368
codes, the source gate covers 317 files, and both 63-test full regressions
pass. Feature-matrix rows SV-641 through SV-650 are the detailed release
evidence. Batch 108 is not a CI-inspection boundary.

Batch 109 completes bounded SystemVerilog aggregate, multidimensional-array,
pattern, cast, and nominal-legality closure. Named packed structs, unions, and
enums retain recursive type structure, literals, exact layout, source
provenance, and nominal identity through aliases and type parameters. Bounded
unpacked structs admit scalar, enum, and nested packed or unpacked-struct
members. Chained member access, recursive positional/keyed/default patterns,
explicit casts, nominal assignment/equality checks, and exactly-one-member
packed-union patterns have checked positive and negative evidence.

Static arrays retain one through four ordered dimensions and at most 4,096
dense elements. Direction-aware row-major full-rank constant/runtime indexing,
per-dimension queries and checks, whole-value copy, generated/type-parameter
specialization, same-language ports, automatic functions/tasks, debugger
reads, callbacks, VCD, interpreter/LLVM O0/O2, cold/warm reuse, and source-edit
invalidation agree. Native schema 61 and container semantic revision 25 record
the recursive types, dimensions, and linear accesses without a public ABI
change. Multidimensional subarray slices, unpacked unions, unpacked-array
aggregate members, widths above 64 bits, and cross-language aggregate/
container boundaries remain deferred. The catalog covers 1,393 codes, the
source gate covers 319 files, and both 64-test full regressions pass.
Feature-matrix rows SV-651 through SV-660 are the detailed release evidence.
Batch 109 is not a CI-inspection boundary.

Resume with **feature batch 110: SystemVerilog strings, files, containers, and
memories release audit**:

### Emergency restart checkpoint — 2026-08-01

#### Latest continuation state (supersedes the older stopping point below)

Batch 111 is complete and pushed at `fdafa67`. The exact LLVM 22.1.8
warnings-as-errors Debug and Release regressions passed all 65 tests in 112.30
and 97.27 seconds. Scoped locals remained quick at 0.98/1.03 seconds. The
diagnostic catalog covers 1,415 production codes, and all 336 authored files
pass the empty-allowlist 2,000-line source gate. Batch 110 feature/footprint
repairs and its 12-job CI closure are pushed. The explicitly requested Batch
111 Actions inspection is run `30712751618`, which passed all 12 jobs. The
final Windows/MSVC/LLVM Debug job completed in 25 minutes 55 seconds; the
earlier apparent collective hang was not reproduced.

Batch 112 is now intentionally dirty and incomplete. Its first implementation
slice preserves ordinary VHDL function and procedure overload sets across
local and package visibility, selects packed calls by contextual result and
actual profiles, retains the existing SystemVerilog single-function behavior,
rejects duplicate/ambiguous/no-match profiles with new `VHOVER` diagnostics,
and follows every possible overload body for wildcard sensitivity. The
working set is `CMakeLists.txt`, `tests/CMakeLists.txt`,
`tests/elaboration/elaborator_test.cpp`,
`tests/elaboration/elaborator_test_support.hpp`, the new
`tests/elaboration/elaborator_vhdl_overload_test.cpp`,
`tests/app/application_case_dispatch.cpp`, the new
`tests/app/vhdl_overload_application_test.cpp`, `docs/diagnostics.md`,
`src/elaboration/elaborator_internal.hpp`,
`src/elaboration/lowerer_{functions,procedures,sv_strings,types}.cpp`,
`src/elaboration/{hierarchy_packages,elaboration_vhdl_packages}.cpp`, and the
new untracked `src/elaboration/lowerer_overloads.cpp`. The exact LLVM Debug
`fsim_elaboration_tests` target built successfully with `--parallel 8`. The
focused `fsim.elaboration` test now passes in 0.14 seconds with local,
same-package, cross-package wildcard, and two-/three-part direct-selected
integer/Boolean function/procedure selection plus no-match, ambiguity, and
duplicate-profile cases. VHDL function calls now retain named association
metadata, bounded functions and input-mode procedures retain defaults, and
result-context, named/defaulted, and recursively nested overload selection is
covered by the same focused elaboration test. Direct local designators now hide
use-visible package callables, imported same-profile homographs retain distinct
package owners and diagnose call-site ambiguity, and context-expanded plus
generic-package-instance overload sets elaborate successfully. The expanded
1.78-second application differential executes those cases plus explicit
integer-family and nominal-enumeration conversions in the interpreter and LLVM
O0/O2, passes cold/warm cache reuse, and retains the package-body edit that
changes values and the specialization key. Package-body declaration
conformance, missing bodies, pure signal reads/procedure calls, and mismatched
formal defaults now have focused negative legality coverage. The catalog gate
currently covers 1,435
production codes and all 340 authored sources pass the line gate. This is
foundation evidence only: the remaining Batch 112 semantic slices, full
gates, documentation closure, commit, and push are still required. Resume with
the residual contextual name/type lookup and legality cases.

The latest static-expression slice folds declaration-ordered package constants
through integer-subtype conversions, scalar and array type attributes,
arithmetic, dependent array bounds, and nested same-designator overloaded pure
function calls. Aggregate, indexed-name, and slice actuals select their
array/scalar profiles and execute in the same interpreter/LLVM matrix. Task 6
now has focused implementation evidence.

The latest name/type legality slice gives missing expression-context
function/type marks a dedicated diagnostic, preserves a distinct unknown or
wrong-context procedure diagnostic, and negative-tests unqualified nominal
aggregate ambiguity. Static integer conversions now diagnose declared-range
violations separately, while integer-family overflow retains the established
package-range diagnostic. Tasks 4, 5, and 7 now have focused implementation
evidence.

The latest runtime slice retains scalar and resolved-element-array VHDL
resolution indications and validates visible pure array-input/base-result
profiles. Supported one-return OR/AND resolver bodies now execute
deterministically across two delayed independent drivers in the interpreter
and LLVM O0/O2 for `bit`, exact Logic9, composite arrays, and a resolver plus
resolved subtype imported from a package. Exact `H`/`L` driver inspection,
elementwise composite results, and VCD output agree across engines at times 1
ns and 2 ns. Invisible, wrong-profile, ambiguous, and unsupported-body cases
are negative-tested. Tasks 8 and 9 now have focused implementation evidence.
The older checkpoint narrative below is retained as implementation history, but
its uncommitted and unvalidated claims are obsolete.

## Completed feature batch 112

1. Retain ordered ordinary VHDL function/procedure overload sets with exact
   declaration identity and unchanged SystemVerilog callable behavior.
2. Resolve local calls by association shape, defaults, formal profile, actual
   base type, and contextual result type.
3. Preserve overload sets through package/context/direct-selected visibility,
   bodies, generic-package materialization, provenance, and cache invalidation.
4. Complete simple/selected/indexed/slice/attribute/literal/subprogram lookup
   plus deterministic hiding, homograph, ambiguity, and missing-name legality.
5. Complete contextual literal, conversion, aggregate, attribute, operator,
   nested-call, and expected-result typing under VHDL base/nominal rules.
6. Extend locally static evaluation across constants, bounds, aliases,
   conversions, attributes, operators, and eligible pure user functions.
7. Add the dedicated subprogram/name legality pass, including body conformance,
   purity, declarations, object classes, modes, defaults, and call contexts.
8. Represent and validate scalar/composite resolution indications and visible
   resolution-function profiles on resolved subtypes and signals.
9. Execute supported resolution functions across independent drivers in the
   interpreter and LLVM O0/O2 with Logic9, scheduling, ABI, cache, VCD, and
   debugger evidence.
10. Close focused positive/negative, runtime differential, cache-edit,
    sanitizer, source/diagnostic, full Debug/Release, documentation, commit,
    and push evidence.

Batch status is **complete**. All ten tasks have implementation and gate
evidence. The LLVM-disabled ASan/UBSan focused gate passed all six selected
tests in 3.38 seconds with LeakSanitizer disabled because it cannot run under
the local ptrace environment. The LLVM 22.1.8 warnings-as-errors Debug suite
passed all 66 tests in 172.72 seconds, including overloads in 1.73 seconds and
scoped locals in 0.79 seconds. Release passed all 66 tests in 155.90 seconds,
including overloads in 1.76 seconds and scoped locals in 0.78 seconds. The
catalog contains 1,435 production codes and all 340 authored sources pass the
2,000-line gate. Native-object schema 69 separates the changed
overload/static lowering contract from Batch 111 cache entries; the public
runtime ABI and container semantic revision 28 remain unchanged.

## In-progress feature batch 113

1. Complete entity, architecture, component, and direct-instantiation generic
   interfaces with ordered class, subtype, mode, default, and source identity.
2. Resolve positional/named generic associations, `open` defaults, ordering,
   duplicate/unknown formals, required actuals, conversions, and static legality.
3. Evaluate declaration-ordered generic defaults and actuals across earlier
   generics, packages, attributes, conversions, aggregates, and pure functions.
4. Complete component conformance and binding plus direct entity, architecture,
   and configuration instantiation with exact generic/port profile checks.
5. Build stable value/type/subprogram/package specialization identities with
   transitive cache provenance and equivalent-instance sharing.
6. Support expression, conversion, qualified, slice, concatenation, and
   aggregate input-port actuals while checking writable output/inout actuals.
7. Implement `open` port actuals, input defaults, unconnected outputs/buffers,
   association ordering, and mode-specific missing/illegal-open diagnostics.
8. Propagate generic-dependent scalar/composite constraints through ports,
   component views, direct instances, hierarchy aliases, and boundary checks.
9. Prove multiple specializations across interpreter/LLVM O0/O2, scheduling,
   VCD, debugger, hierarchy, cache reuse, and source-edit invalidation.
10. Close focused positive/negative/runtime, sanitizer, source/catalog, full
    Debug/Release, matrix/docs, commit, and push evidence.

Batch status is **in progress**. Keep this ten-task list current in both the
official plan and this handoff. Change it to complete only after all ten tasks
and their gates close and work moves to Batch 114.

Current evidence: `open` generic actuals are retained as explicit default
selections, and indexed/general expression port actuals are retained in HIR
without the old parser-only rejection. Component and direct-entity value
generic defaults elaborate, while `open` on a required direct generic reports
`FSIM-ELAB-GENERIC-001`. Entity input defaults now execute for omitted and
explicit-`open` direct associations; non-input defaults are rejected. Static
literal/array-aggregate and dynamic arithmetic/slice/concatenation input actuals
execute through owned boundary signals and per-instance VHDL drivers, while
output/buffer/inout expressions require writable names. `hierarchy_ports.cpp`
holds the new boundary implementation and `hierarchy_types.cpp` remains exactly
2,000 lines. The focused frontend, elaboration, component interpreter/LLVM
O0/O2/cache, catalog, and 341-source line-budget evidence passes in the LLVM
Debug tree. Batch 113 and Tasks 2, 6, and 7 remain in progress.

Task 1 now has focused implementation evidence: implicit/explicit value
generics retain normalized constant class and input mode, invalid classes/modes
are diagnosed, and component conformance/cache identity includes the metadata
without changing type/function/procedure/package generic behavior. Task 3 now
proves declaration-ordered dependent arithmetic defaults for direct and
component instances, including an overridden earlier generic followed by
`open`, in the component interpreter/LLVM O0/O2 cold/warm differential. Task 3
is now focused-complete. Bounded up-to-64-bit packed value generics accept
contextual string/literal/aggregate defaults and actuals with state/width/range
checks. The application additionally folds a visible package constant, array
`'length`, built-in `positive` conversion, earlier generic references, and a
visible pure package function; overriding the first generic and selecting
`open` for the rest recomputes the chain across interpreter/LLVM O0/O2
cold/warm runs. Type validation moved to
`hierarchy_generic_interfaces.cpp` to preserve the source budget.

Batch 110 has advanced through these validated features:

1. deterministic string expression and mutation/conversion methods;
2. bounded `$swrite`, `$sformat`, and `$sformatf` formatting;
3. `$fgetc`, `$ungetc`, `$fscanf`, and `$sscanf` input;
4. binary `$fread`, including binary fopen aliases, packed and fixed-memory
   targets, partial reads, interpreter and LLVM O0/O2 execution, cold/warm
   cache reuse, and external input edits; and
5. `$fseek`, `$ftell`, `$rewind`, and explicit/all-file `$fflush`, including
   pushback-aware positioning and compiled application evidence.

After file positioning/flush, the complete focused eight-test gate passed:
frontend, diagnostics catalog, source-line budget, elaboration, LLVM,
mutable-string application, file application, and runtime. The file
application covered interpreter and compiled O0/O2 paths plus cold/warm and
edited-input cache behavior. The protected source counts at the latest live
check are 1,962 lines for `include/fsim/runtime/simir.hpp`, 1,996 for
`src/compiler/llvm_jit_validation.cpp`, and 1,982 for
`src/runtime/simir_execution.cpp`.

Feature 6, `$writememb` and `$writememh`, is implemented and validated. The
worktree contains:

- parser recognition and `frontend::Statement::memory_write` metadata;
- `runtime::simir::LoadMemory::write` plus deterministic
  `write_memory_text` serialization;
- elaborator, interpreter/application service, LLVM label/validation,
  cache-key, and fixed one-dimensional source handling;
- native cache schema 65 with container identity
  `bounded-static-associative-v26-memory-write`;
- frontend and elaboration positive and negative tests;
- runtime serialization, selected descending bounds, round-trip, and invalid
  bounds tests in `tests/runtime/runtime_container_tests.cpp`;
- application dumps for interpreter and compiled O0/O2 execution, cold/warm
  reuse, and edited binary input content; and
- generalized checked parser, semantic, and elaboration diagnostic text for
  both memory-file directions.

The complete nine-test focused gate passed again after restart on 2026-08-01:
frontend, diagnostics catalog, source-line budget, general and container
elaboration, LLVM, mutable-string application, file application, and runtime.
The file application passed in 1.72 seconds and the complete gate in 6.25
seconds. Keep `llvm_jit_validation.cpp` below 2,000 lines and do not enlarge
recursive frontend structures.

Feature 7, bounded dynamic-array construction and indexed queue mutation, is
implemented and validated. `new[size](initializer)` requires an exactly
compatible dynamic-array value, snapshots aliased initializers before resize,
copies the retained prefix, and defaults an expanded two-state tail to zero or
a four-state tail to X. Queues add checked `insert(index, item)` and
`delete(index)`, allow insertion at the current size, retain deterministic
bounded-queue back eviction, and reject negative, unknown, or out-of-range
indices. Parser/elaboration diagnostics, interpreter/native callbacks,
validation, and native cache identity all distinguish initialized resize and
indexed mutation. The Debug container application passed in 361.70 seconds;
the fast frontend/elaboration/LLVM/runtime regressions and a focused cold/warm
cache-identity test also passed.

Feature 8, nominal packed-aggregate container and memory interactions, is
implemented and validated. One-through-64-bit named packed struct/union/enum
elements now retain nominal identity through static, dynamic, queue,
associative, and one-through-four-dimensional static containers. Nested
patterns, element writes, `new[size](initializer)`, indexed queue mutation,
full-rank runtime indexing, generated same-language ports, type parameters,
automatic functions/tasks, debugger reads, callbacks, and normalized VCD
observation agree across interpreter and LLVM O0/O2. One-dimensional packed
aggregate memories also execute through `$fread`, `$readmemb`/`$readmemh`, and
`$writememb`/`$writememh`; edited external inputs reuse native objects while
changing exact word values. Distinct nominal aggregate element types are not
whole-container compatible. Multidimensional memory-file operands remain a
checked exclusion in both elaboration and the public runtime helper. Native
schema 67 and container semantic revision 28 record aggregate element identity
plus the construction/mutation operations without a public ABI change.

The complete feature-8 nine-test gate passed on 2026-08-01: frontend,
diagnostics catalog, source-line budget, general and focused container
elaboration, LLVM, file application, aggregate/multidimensional application,
and runtime. The two application differentials passed in 2.03 and 1.99 seconds.
The current protected source counts are 1,965 lines for
`include/fsim/runtime/simir.hpp`, 1,999 for
`src/compiler/llvm_jit_validation.cpp`, 1,982 for
`src/runtime/simir_execution.cpp`, 1,977 for
`src/elaboration/lowerer_sv_containers.cpp`, and 1,911 for
`tests/elaboration/elaborator_sv_container_test.cpp` after extracting the new
aggregate fixture into its own translation unit.

Feature 9, same-language mutable-string module ports, is implemented and
validated. ANSI and classic input/output/inout string ports alias one runtime
object through nested hierarchy, expose child aliases to the debugger, make
input ports read-only, reject descendant writes and independent sibling
writers, and preserve interpreter/LLVM O0/O2 cold/warm/source-edit behavior.
The focused frontend, elaboration, diagnostics-catalog, source-line-budget,
and mutable-string application gate passed; the application completed in 0.91
seconds. Native-object schema is now 68.

Feature 10, the SystemVerilog release-row audit, is complete. New
feature-matrix rows SV-661 through SV-670 record the ten Batch 110 slices, and
required rows V1-SV-01 through V1-SV-08 now join V1-SV-09 in the completed v1
group. The formal deferred table now names every audited checked exclusion,
including real/Unicode and wider runtime data, string-element containers,
multidimensional memory-file operands, standard/multichannel and postponed-
monitor file extensions, unsupported primitive families, and the bounded
control/callable exclusions. No audited accepted syntax is left parser-only or
silently discarded.

The exact LLVM 22.1.8 warnings-as-errors Debug regression passed all 64 tests
in 485.70 seconds, including scoped locals in 0.92 seconds, LLVM in 3.10
seconds, mutable strings in 0.94 seconds, files in 2.11 seconds, aggregate/
multidimensional execution in 2.03 seconds, containers in 375.83 seconds, and
the monolithic application in 40.66 seconds. Release passed all 64 tests in
159.27 seconds, including scoped locals in 0.79 seconds, LLVM in 2.71 seconds,
mutable strings in 0.41 seconds, files in 0.68 seconds, containers in 99.00
seconds, and the monolithic application in 14.10 seconds. The mandatory Batch
110 non-documentation boundary is closed by repair commit `a5d69e0` and
12-job replacement run `30710421676`. GitHub CI uses parallelism four and
documentation-only runs are not monitored. At that checkpoint the next batch
was Batch 111.

#### Earlier checkpoint history

Batch 110 is active and intentionally uncommitted on
`codex/resumable-jit`. `HEAD` and `origin/codex/resumable-jit` are both Batch
109 commit `95fb09b5f11765154b1864c06d27d9f84878b208`; preserve the complete
dirty worktree, including all twelve untracked source/header files. The
worktree passes `git diff --check`. No build, test, commit, push, or CI
inspection was started for this emergency checkpoint.

The first three bounded features are implemented in the worktree:

1. Deterministic byte-string expression methods `getc`, `toupper`, `tolower`,
   `compare`, `icompare`, and inclusive `substr` use a shared runtime helper
   and the existing generic native container callback without changing the
   public JIT ABI.
2. Mutating `putc`, `itoa`, `hextoa`, `octtoa`, and `bintoa`, plus expression
   conversions `atoi`, `atohex`, `atooct`, and `atobin`, have parser,
   elaborator, interpreter, LLVM, cache-key, and application coverage. Postfix
   calls now also parse on string literals and parenthesized expressions.
3. Bounded `$swrite`, `$sformat`, and `$sformatf` share one public output-format
   parser and deterministic packed/string/time formatter. They cover width,
   padding, literal percent, hierarchy, interpreter, LLVM O0/O2, and cold,
   warm, and edited cache behavior. Cache schema 62 records exact format
   metadata. Checked malformed-format and target diagnostics are
   `FSIM-ELAB-SVSTRING-019` and `FSIM-ELAB-SVSTRING-020`.

The twelve untracked files are:

- `include/fsim/frontend/input_format.hpp`;
- `include/fsim/frontend/output_format.hpp`;
- `include/fsim/runtime/file_binary.hpp`;
- `include/fsim/runtime/file_operations.hpp`;
- `include/fsim/runtime/file_scanning.hpp`;
- `include/fsim/runtime/string_methods.hpp`;
- `src/elaboration/lowerer_sv_file_binary.cpp`;
- `src/elaboration/lowerer_sv_file_scan.cpp`;
- `src/elaboration/lowerer_sv_string_format.cpp`;
- `src/runtime/simir_file_binary.cpp`;
- `src/runtime/simir_file_scanning.cpp`; and
- `src/runtime/simir_string_methods.cpp`.

Verify the complete tracked and untracked list with `git status --short` after
restart and do not normalize or discard any part of it.

Feature 4, file character input and pushback, is implemented and validated.
`$fgetc` and `$ungetc` reuse the existing `FileReadLine` operation with
explicit line/character/unget modes and the generic native callback, without
adding a public JIT ABI entry. Runtime file state has a deterministic one-byte
pushback slot; successful pushback clears EOF, `$feof` respects pending
pushback, and `$fgets` consumes it, including a pushed newline. The final
`FileReadLine` aggregate-order regression was repaired, and
`read_file_line` now consumes through `read_file_character`. The complete
eight-test focused gate passed: frontend, diagnostics catalog, source-line
budget, elaboration, LLVM, mutable-string application, file application, and
runtime.

Feature 5a, bounded formatted input scanning, is implemented and validated.
`$fscanf` and `$sscanf` support at most 64 conversions, `%%`, field widths,
assignment suppression, literal/whitespace matching, `%b/%o/%d/%i/%u/%h/%x`,
`%c`, and `%s`, with direct writable packed or string targets. Input is bounded
to 4,096 bytes and X/Z digits convert deterministically for two-state targets.
The interpreter, LLVM generic callback, exact cache metadata, parser arity,
checked diagnostics, runtime unit tests, and cold/warm/edited-input application
paths are present. The file application test passed in 0.98 seconds across its
interpreter/O0/O2/cache cases. Cache schema is now 63, and file semantic
identity is `simir-text-file-v2-scan-binary`.

Feature 5b, binary `$fread`, is the exact unvalidated stopping point. The
worktree contains the new `FileBinaryRead` SimIR operation, bounded binary
runtime helper, interpreter path, LLVM lowering/callback/validation, cache-key
metadata, parser arity checks, elaborator lowering, CMake entries, and checked
diagnostics `FSIM-ELAB-SVFILE-013` and `FSIM-ELAB-SVFILE-014`. Intended
semantics are big-endian/MSB-first packed placement plus direction-aware
one-dimensional fixed-memory targets with optional start/count and a 1 MiB
read bound. None of these newest `$fread` edits has been compiled or tested;
do not describe them as working until the gates below pass.

At the checkpoint, the protected source counts are 1,943 lines for
`include/fsim/runtime/simir.hpp`, 2,007 for
`src/compiler/llvm_jit_validation.cpp`, and 1,972 for
`src/runtime/simir_execution.cpp`. Split or compact the LLVM validator below
2,000 before the source gate. Keep recursive frontend expression/statement
structures compact to protect Windows MSVC Debug
`fsim.application.scoped_locals` performance.

On restart, read this file and verify live Git state, then resume exactly here:

```sh
git status --short --branch
git log -3 --oneline --decorate
git rev-parse HEAD
git rev-parse origin/codex/resumable-jit
git diff --check
wc -l include/fsim/runtime/simir.hpp \
  src/compiler/llvm_jit_validation.cpp \
  src/runtime/simir_execution.cpp
cmake --build build/llvm22-ninja-debug --parallel 8 \
  --target fsim_frontend_tests fsim_elaboration_tests fsim_llvm_tests \
    fsim_runtime_tests fsim_sv_file_application_tests
ctest --test-dir build/llvm22-ninja-debug --output-on-failure \
  -R 'fsim\.(frontend|elaboration|llvm|runtime|application\.sv_files)$'
```

Before that first build, inspect and complete these known `$fread` loose ends:

1. Add binary `fopen` mode aliases (`rb`, `wb`, `ab`, `rb+`, `r+b`, and their
   write/append forms) in `file_mode()` without weakening existing invalid-mode
   tests.
2. Add `$fscanf`, `$sscanf`, and `$fread` to the integer-result system-function
   recognition used by `lower_handle` in `lowerer_expression_system.cpp`.
3. Add frontend positive/arity tests, elaboration positive/negative metadata
   tests, runtime packed/fixed-memory byte-order and partial-read tests, and an
   application binary fixture covering interpreter, LLVM O0/O2, cold/warm
   cache reuse, and external input-content edits.
4. Recheck validator size after the new `FileBinaryRead` visitor; extract the
   handler rather than exceeding the 2,000-line hard limit.
5. Search for stale schema-62 expectations before finalizing schema 63.

Potential correctness points to retain during review: avoid undefined shifts
on zero-byte reads; partial packed reads are placed at the most-significant
end; container writes must validate exact type/profile; optional count without
start is invalid; start/count are memory-only; and the runtime helper, not the
process-only validator, must validate the live container profile.

After `$fread` passes, rerun the focused frontend, elaboration, LLVM, runtime,
file/mutable-string application, diagnostics-catalog, and source-line-budget
tests. Then continue in the approved order: file positioning/flush;
`$writemem*`; container construction/mutation; aggregate/multidimensional
memory-container interactions; the complete row/diagnostic/cache/debug/VCD
audit; and exact Debug/Release gates.

Do not inspect Actions yet. Batch 110 remains the mandatory non-documentation
CI boundary after its complete ten-feature commit and push; local builds use at
least eight workers, while GitHub Actions builds use parallelism four.

1. Audit every SystemVerilog v1 matrix row against live parser, elaborator,
   interpreter, LLVM, cache, debugger, VCD, and diagnostic evidence; record
   each remaining gap explicitly before changing scope.
2. Close remaining bounded mutable-string allocation, indexing, slicing,
   methods, conversions, formatting, callables, ports, and lifetime semantics.
3. Close remaining bounded file descriptor, mode, status, byte/text, positioned,
   formatted, and lifecycle semantics with manifest-confined deterministic I/O.
4. Close remaining dynamic array, queue, associative array, and static memory
   construction, methods, selection, pattern, equality, and callable behavior.
5. Close the interactions among aggregate element types, multidimensional
   memories, containers, strings, generated hierarchy, type parameters, and
   supported same-language port boundaries.
6. Add stable checked diagnostics for every audited unsupported or malformed
   string, file, container, and memory path; no accepted syntax may disappear.
7. Prove deterministic copy/alias/ownership, suspension, debugger, callbacks,
   normalized VCD, and interpreter/LLVM O0/O2 behavior across the supported
   audit slice.
8. Prove cold/warm reuse and edits to types, bounds, contents, file inputs, and
   operations invalidate exactly the affected native objects.
9. Advance the versioned native/container/file identities as required, update
   the matrix/support/plan/handoff documents, and run exact full Debug/Release
   and source/diagnostic gates.
10. Commit and push Batch 110, inspect its non-documentation GitHub Actions
    jobs, repair every actionable failure locally with at least eight build
    workers, push repairs, and confirm replacement checks. GitHub builds use
    parallelism four; documentation-only runs do not require monitoring.

The authoritative Batch 99 through 130 language-closure sequence is recorded
under **Forward language-closure feature batches** in the implementation plan;
preserve that order unless both documents are explicitly amended.

## Working cadence

- Implement ten related features before the next full regression.
- Use focused warnings-as-errors builds and targeted tests after each coherent
  change; do not run the full suite for every individual feature.
- Use at least eight parallel workers for every local project, test-support,
  and fetched-dependency build, including interim builds. Prefer
  `cmake --build <tree> --parallel 8` (or a larger value). GitHub Actions is
  the explicit exception: its hosted-VM builds use `--parallel 4`; the
  structurally partitioned operation storage keeps that setting within the
  intended memory envelope.
- At the tenth feature, run the exact LLVM 22.1.8 Debug and Release regression
  appropriate to the batch, update the plan/support/matrix documents, commit,
  and push the branch. A feature batch is not handed off as complete until its
  commit is present on `origin/codex/resumable-jit`.
- At every tenth numbered feature batch (70, 80, 90, and so on), inspect the
  GitHub Actions runs for the pushed handoff, diagnose and fix every actionable
  failure, rerun the affected local gates with at least eight build workers,
  push the repair, and confirm the replacement checks. Between those
  boundaries, inspect CI only when explicitly requested.
- Preserve the 2,000-line hard limit, prefer approximately 1,600 lines, split
  compilation units by responsibility, and do not move executable
  implementation into `_internal.hpp` files.
- Keep Windows portability in every implementation decision: use secure CRT
  or compatibility wrappers for environment access, avoid POSIX-only path and
  process assumptions, keep the native ABI plain C, and test MSVC/clang-cl
  behavior in the scheduled platform gate.

## Fresh-machine bootstrap

The pushed branch is the portable handoff. On a new Linux x86-64 machine,
install Git, Ninja, CMake 3.28 or newer, a C++20 compiler, and exact LLVM
22.1.8. CMake may use an installed exact Boost.Context 1.91.0 and compatible
Tcl 9.0 development package, or download the pinned fallbacks when network
access is available.

Clone and select the handoff branch:

```sh
git clone ssh://git@github.com/colinphill/fsim.git
cd fsim
git switch --track origin/codex/resumable-jit
git status --short --branch
git log -5 --oneline --decorate
```

History must contain Batch 68 commit `0a3c54f` (`feat: add bounded
SystemVerilog runtime strings`) and Batch 69 commit `a3cfc73` (`feat: add
bounded SystemVerilog text files`), followed by Batch 70 commit `2943a00`
(`feat: add bounded SystemVerilog dynamic containers`) and the newest pushed
Batch 71 bounded-associative-array handoff, Batch 72 bounded-static-memory
handoff, Batch 73 bounded-static-array-port handoff, Batch 74
bounded-dynamic-container-port handoff, and the newest Batch 75 bounded
unpacked-container-query handoff, Batch 76 bounded assignment-pattern handoff,
Batch 77 bounded reduction-method handoff, and the newest Batch 78 bounded
ordering-method handoff, followed by the newest Batch 79 bounded
locator-method handoff, Batch 80 predicate-locator handoff, and Batch 81
named-iterator/index-predicate handoff, followed by the Batch 82 bounded
reduction-transformation handoff and the Batch 83 bounded ordering-key
handoff, followed by the Batch 84 bounded locator-transformation handoff.
The Batch 85 named-reduction-iterator handoff, Batch 86 bounded static
default/index-key assignment-pattern handoff, and Batch 87 bounded
static-array-slice handoff follow it, followed by the Batch 88 bounded
read-only static-array-slice-consumer handoff and Batch 89 bounded
static-array-slice-callable-actual handoff, followed by Batch 90 bounded
static-array-slice-ordering-mutation handoff and Batch 91 bounded
static-array-slice-module-port handoff, followed by the Batch 92 bounded
indexed-static-array-slice handoff and the Batch 93 bounded fixed-array-function
return handoff, followed by the Batch 94 bounded nonstatic-container-function
return handoff and Batch 95 bounded function-result-consumer/conditional
handoff, then the Batch 96 bounded whole-container-equality handoff and Batch
97 bounded SystemVerilog membership-expression handoff, followed by Batch 98
bounded SystemVerilog case-inside statement handoff and Batch 99 bounded
SystemVerilog case-qualifier handoff, followed by the Batch 100 bounded
SystemVerilog case-pattern handoff and the Batch 101 SystemVerilog expression-
sizing, short-circuit, dynamic-read-selection, and streaming-concatenation
handoff, followed by the Batch 102 procedural-lvalue, timed-update,
expression-increment, and selected-force/release handoff and the Batch 103
function/task lifetime, association, reference, static-local, and generated-
callable handoff, followed by the Batch 104 always/procedural-control handoff,
the Batch 105 fork/process/NBA-ordering handoff, the Batch 106
delay/primitive/continuous-assignment handoff, the Batch 107 interface/modport
handoff, the Batch 108 preprocessing/generate handoff, and the Batch 109
aggregate/multidimensional-array handoff. Treat the newest pushed commit on the
same branch as the authoritative continuation and read this file from that
checkout before doing work.

Configure the exact warnings-as-errors build pair:

```sh
cmake -S . -B build/llvm22-ninja-debug -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DFSIM_BUILD_TESTS=ON \
  -DFSIM_LLVM_MODE=ON \
  -DFSIM_WARNINGS_AS_ERRORS=ON \
  -DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm
cmake -S . -B build/llvm22-ninja-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DFSIM_BUILD_TESTS=ON \
  -DFSIM_LLVM_MODE=ON \
  -DFSIM_WARNINGS_AS_ERRORS=ON \
  -DLLVM_DIR=/usr/lib/llvm-22/lib/cmake/llvm
cmake --build build/llvm22-ninja-debug --parallel 8
cmake --build build/llvm22-ninja-release --parallel 8
```

Because a new host is a new toolchain/environment boundary, establish its
   baseline once before changing behavior:

```sh
ctest --test-dir build/llvm22-ninja-debug --output-on-failure
ctest --test-dir build/llvm22-ninja-release --output-on-failure
```

The recorded timings above are evidence from the previous development host,
not performance expectations for the new machine. After the baseline passes,
resume Batch 110 below and return to focused tests until its tenth feature.

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
2. Confirm the branch is `codex/resumable-jit` and history contains the
   bounded SystemVerilog aggregate/multidimensional implementation.
3. Begin Batch 110 from the completed aggregate/multidimensional
   baseline described above.
   Inspect the live tree first and rerun focused evidence if the host changed.
4. Keep Batch 110 within the SystemVerilog string, file, container, memory, and
   complete-v1-row audit scope.
   Record intentional scope changes in this handoff before implementation.
5. Use targeted tests during that batch, run the full Debug and Release gates
   after all ten features, update the four documents named above, commit, push,
   and inspect/fix all non-documentation GitHub Actions jobs. Batch 110 is a
   mandatory CI-inspection boundary.

The existing exact-LLVM build trees on the recorded development host are:

```sh
cmake --build build/llvm22-ninja-debug --parallel 8
cmake --build build/llvm22-ninja-release --parallel 8
```

Use a narrow test expression while Batch 110 is in progress, extending the
frontend, string/file/container elaboration, hierarchy/native cache, and
application tests as audit repairs land:

```sh
ctest --test-dir build/llvm22-ninja-debug --output-on-failure \
  -R 'fsim\.(frontend|elaboration|llvm|application$|application\.sv_containers|application\.sv_files|diagnostics-catalog|source-line-budget)'
```

Both warnings-as-errors Ninja trees link the exact LLVM 22.1.8 backend. Their
recorded 64-test inventories are clean after the local feature-batch-109 gates.

Before declaring any row complete, consult:

- [implementation plan and progress](implementation-plan.md);
- [feature matrix and release evidence](feature-matrix.md);
- [language support](language-support.md);
- [cross-language semantic contract](cross-language-semantics.md); and
- [SystemC subset and plug-in model](systemc-subset.md).
